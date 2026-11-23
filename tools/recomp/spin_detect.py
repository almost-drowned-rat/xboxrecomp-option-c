"""spin_detect.py - polling loops in a lifted function's C (step 6c, maintainer 2026-11-17: a generic spin-wait
replacement next to the BlockOnTime override).

A guest thread that waits for another thread (or the GPU, or a device) without an OS wait spins: a short loop
that re-reads the same memory and compares, with no store and no call. On the console that loop ends within
microseconds or one GPU frame; here the other side is a host thread, and the guest burns a whole core meanwhile
(Orta's BlockOnTime did 0.85-0.9 cores until its override slept - xdk_d3d_wait.h). The translator marks the back
edge of such a loop RECOMP_SPIN() instead of the plain RECOMP_BACKEDGE(): the barrier plus the runtime's backoff
(xbox_spin_backoff: pause, then yield, then short sleeps while the same wait goes on).

A loop counts as polling when, between its label and the backward goto:
  - there are at most MAX_LINES statements,
  - no memory store (MEMxx(...) = ...), no call (a guest call passes "(ctx, base)"; RECOMP_CALL / ICALL / PUSH32 /
    POP32 / return), no label of another loop's start in between that the loop jumps over,
  - no label and no unconditional goto inside (straight-line text, so it IS the loop's body), no x87 work,
  - at least one memory read, and no register in any read's address is assigned inside the loop - the loop
    reads the SAME places again (strlen / memchr style scans move their pointer and do not qualify, also when
    they compare against a fixed value).
The rules are on the generated text, like the RECOMP_BACKEDGE pass that runs right before.
"""
import re

MAX_LINES = 16
REGS = ("eax", "ecx", "edx", "ebx", "esi", "edi", "ebp", "esp")
_MEM = re.compile(r"\bMEM(?:8|16|32|F|D)\s*\(")         # recomp_types.h's guest memory accessors
# anything that calls out, moves the stack, writes memory another way, or is an atomic: not a pure poll
_CALLISH = re.compile(r"\(ctx, base\)|\bRECOMP_\w*CALL\w*\b|\bPUSH\w*\(|\bPOP\w*\(|\breturn\b|\bRECOMP_UNIMPL\b|"
                      r"\b\w*(?:STORE|MOVS|STOS|LOCK|XCHG|CMPXCHG)\w*\s*\(|\b_lk_|\bmemcpy\b|\bmemset\b|\bmemmove\b")
_ASSIGN_OP = r"(?:[+\-*/%|&^]|<<|>>)?=(?!=)"
_LABEL = re.compile(r"^loc_[0-9A-Fa-f]+:")
_FPU = re.compile(r"\bfp_\w+\s*\(|\bg_fp_")                # x87 work: a computation, never a poll


def _balanced(text, open_idx):
    """Index just past the ')' that closes the '(' at open_idx, or -1."""
    depth = 0
    for i in range(open_idx, len(text)):
        c = text[i]
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return i + 1
    return -1


def mem_accesses(line):
    """[(address expression, is_store)] of the MEMxx(...) in one C line."""
    out = []
    for m in _MEM.finditer(line):
        open_idx = m.end() - 1
        end = _balanced(line, open_idx)
        if end < 0:
            continue
        addr = line[open_idx + 1:end - 1]
        store = re.match(r"\s*" + _ASSIGN_OP, line[end:]) is not None
        out.append((addr, store))
    return out


def _assigns(line, reg):
    """The line writes the register (plain or compound assignment, ++/--, or a macro taking it first, e.g. a
    partial-register setter)."""
    if re.search(r"\b%s\s*%s" % (reg, _ASSIGN_OP), line):
        return True
    if re.search(r"(?:\+\+|--)\s*%s\b|\b%s\s*(?:\+\+|--)" % (reg, reg), line):
        return True
    for m in re.finditer(r"\b(\w+)\s*\(\s*&?\s*%s\b" % reg, line):
        if not _MEM.match(m.group(0)) and not m.group(1).startswith("MEM"):
            return True
    return False


def is_polling_loop(body):
    """body: the C lines from the loop's label (exclusive) to the backward goto line (inclusive)."""
    stmts = [l for l in body if l.strip() and not l.strip().startswith(("/*", "//"))]
    if not stmts or len(stmts) > MAX_LINES:
        return False
    reads = []
    for l in stmts[:-1]:
        s = l.strip()
        # straight-line text only: a label inside (another jump target) or an unconditional goto means the lines
        # between label and back edge are not the loop's body (Orta: a switch's case blocks looked like a loop)
        if _LABEL.match(s) or s.startswith("goto "):
            return False
    for l in stmts:
        if _CALLISH.search(l) or _FPU.search(l):
            return False
        for addr, store in mem_accesses(l):
            if store:
                return False
            reads.append(addr)
    if not reads:
        return False
    for addr in reads:
        regs = [r for r in REGS if re.search(r"\b%s\b" % r, addr)]
        if any(_assigns(l, r) for r in regs for l in stmts):
            return False                    # a read that moves: a scan (with a fixed comparand), not a poll
    return True                             # every read is the same place again


ACQUIRE = "RECOMP_POLL_ACQUIRE();"


def _acquire_exits(line, loop_label):
    """Every goto of the line that leaves the loop gets the acquire in front of it."""
    def sub(m):
        if m.group(1).lower() == loop_label.lower():
            return m.group(0)
        return "{ %s goto loc_%s; }" % (ACQUIRE, m.group(1))
    return re.sub(r"goto loc_([0-9A-Fa-f]+);", sub, line)


def mark_spins(lines, backoff=True):
    """Polling loops (is_polling_loop): with backoff, RECOMP_BACKEDGE() on the back edge becomes RECOMP_SPIN();
    always (step 6d, ARM64 memory order), every line of the body that reads memory is followed by
    RECOMP_POLL_ACQUIRE() - and a goto on that line that leaves the loop gets it first - so what the polled flag
    guards is read after the flag on a weakly ordered host too (acquire fence on ARM64; on x86 a compiler barrier,
    TSO orders the loads already). Lines are inserted in place. Returns the number of loops."""
    labels = {}
    found = []                              # (start, back-edge index, loop label)
    for idx, line in enumerate(lines):
        m = re.match(r"^loc_([0-9A-Fa-f]+):", line)
        if m:
            labels[int(m.group(1), 16)] = idx
            continue
        if "RECOMP_BACKEDGE();" not in line:
            continue
        g = re.search(r"goto loc_([0-9A-Fa-f]+);", line)
        if not g:
            continue
        start = labels.get(int(g.group(1), 16))
        if start is None:
            continue
        if is_polling_loop(lines[start + 1:idx + 1]):
            found.append((start, idx, g.group(1)))
    for start, idx, label in reversed(found):  # back to front: insertions keep earlier indices
        if backoff:
            lines[idx] = lines[idx].replace("RECOMP_BACKEDGE();", "RECOMP_SPIN();", 1)
        for j in range(idx, start, -1):
            if not mem_accesses(lines[j]):
                continue
            lines[j] = _acquire_exits(lines[j], label)
            indent = re.match(r"\s*", lines[j]).group(0)
            lines.insert(j + 1, indent + ACQUIRE)
    return len(found)
