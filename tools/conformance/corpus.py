"""Whole functions: compile C with a 32-bit MSVC, lift the machine code back,
and require the lifted C to agree with the original.

The snippet cases in cases.py test instructions someone thought to write down.
This tests whatever the optimiser actually emitted -- its register allocation,
its branch layout, the idioms it reaches for -- which is the code a real title
is made of.

The corpus is compiled and then **linked** at a fixed base, and the lifted code
is taken from the linked image. That is the same shape a real XBE arrives in,
and it means relocations are simply not a problem: by the time we see the
bytes, a jump table, a float constant in .rdata and a call to a CRT helper are
all just addresses. Functions a corpus entry calls are lifted too -- __allmul
and __ftol2 are ordinary code in .text, and lifting them is what a real port
does rather than a special case here.

Every function is `__declspec(dllexport)` (`EXP`), or /O2 would inline it into
its only caller and there would be nothing left to lift, and `__cdecl` --
arguments on the stack right to left, caller cleans, result in eax / edx:eax /
st(0). That is the ABI the recompiler's generated code already assumes, so the
harness exercises the real calling path.

Signatures drive how the harness marshals arguments and reads the result:

    ii->i   int (int, int)          uu->u   unsigned (unsigned, unsigned)
    ll->l   long long (ll, ll)      dd->d   double (double, double)
    ff->f   float (float, float)
"""

_INT_ARGS = [
    (0, 0), (1, 1), (5, 3), (-7, 2), (100, -100), (-1, -1),
    (123456, -98765), (-2147483647 - 1, 5), (2147483647, 1),
    (0x7F, 0x80), (-32768, 32767), (3, 0), (0, 3), (-5, -9),
]

_UINT_ARGS = [
    (0, 1), (1, 1), (0xFFFFFFFF, 1), (0xFFFFFFFF, 0xFFFFFFFF),
    (0x80000000, 3), (12345, 67), (0xDEADBEEF, 0x10), (7, 0x7FFFFFFF),
]

_LONG_ARGS = [
    (0, 0), (3, 5), (-7, 11), (0x100000001, 0x30), (-1, -1),
    (0x7FFFFFFFFFFFFFFF, 2), (-0x8000000000000000, 3), (0xFFFFFFFF, 0xFFFFFFFF),
]

_DBL_ARGS = [
    (1.5, 2.0), (-3.25, 0.5), (100.0, 7.0), (2.0, 1.0), (0.0, 1.0),
    (-0.0, 2.0), (1e10, 3.0), (1e-10, 7.0), (-1.0, -1.0), (0.5, 0.5),
]

_FLT_ARGS = [
    (1.5, 2.0), (-3.25, 0.5), (10.0, 4.0), (1.0, 3.0), (0.0, 2.0),
    (-7.5, -1.25), (1e5, 3.0),
]


def Fn(name, sig, why, source, args, tol=0.0, also=()):
    """also: further functions (map names, e.g. "_op_add") to lift although no
    direct call reaches them -- the targets of an indirect call."""
    return {"name": name, "sig": sig, "why": why, "source": source,
            "args": args, "tol": tol, "also": list(also)}


CORPUS = [
    Fn("c_arith", "ii->i",
       "mixed integer arithmetic -- whatever /O2 does with it",
       "EXP int __cdecl c_arith(int a, int b){ int x = a*3 + (b<<2) - (a>>1);"
       " x ^= (a&b); x += (a>b)?a:b; return x*2 - 7; }", _INT_ARGS),

    Fn("c_branches", "ii->i",
       "every signed and unsigned comparison, as the compiler lays them out",
       "EXP int __cdecl c_branches(int a, int b){ int r = 0;"
       " if(a<b) r+=1; if(a<=b) r+=2; if(a==b) r+=4; if(a>=b) r+=8;"
       " if(a>b) r+=16; if((unsigned)a<(unsigned)b) r+=32;"
       " if((unsigned)a>=(unsigned)b) r+=64; return r; }", _INT_ARGS),

    Fn("c_shifts", "ii->i",
       "the rotate idiom, arithmetic vs logical shift, and the &31 masking",
       "EXP int __cdecl c_shifts(int a, int b){ unsigned u = a; int n = b & 31;"
       " return (int)((u<<n)|(u>>((32-n)&31))) + (a>>(n&15)) - (int)(u>>n); }",
       _INT_ARGS),

    Fn("c_loop", "ii->i",
       "a counted loop: a back edge, so more than one basic block",
       "EXP int __cdecl c_loop(int a, int b){ int i, acc = a;"
       " for(i = 0; i < 16; i++){ acc = acc*2 + (b^i); if(acc > 1000000)"
       " acc >>= 3; } return acc; }", _INT_ARGS),

    Fn("c_nested", "ii->i",
       "nested conditionals inside a loop -- branch layout the optimiser picks",
       "EXP int __cdecl c_nested(int a, int b){ int i, r = 0;"
       " for(i = 0; i < 8; i++){ if(((a>>i)&1) != 0){ r += b + i; }"
       " else if(((b>>i)&1) != 0){ r -= a - i; } else { r ^= i; } }"
       " return r; }", _INT_ARGS),

    Fn("c_minmax", "ii->i",
       "min/max and abs -- where the compiler reaches for cmov or setcc",
       "EXP int __cdecl c_minmax(int a, int b){ int lo = a<b?a:b, hi = a<b?b:a;"
       " int d = hi - lo; if(d < 0) d = -d; return lo + hi*3 + d; }",
       _INT_ARGS),

    Fn("c_udiv", "uu->u",
       "unsigned divide and modulo, including the by-one and by-max cases",
       "EXP unsigned __cdecl c_udiv(unsigned a, unsigned b){ unsigned d = b|1u;"
       " return (a/d) + (a%d)*3u + (a>>1)/((d&0xFFu)|1u); }", _UINT_ARGS),

    Fn("c_bits", "uu->u",
       "bit twiddling: the compiler's shift/mask lowering",
       "EXP unsigned __cdecl c_bits(unsigned a, unsigned b){"
       " unsigned x = a ^ (b<<16) ^ (b>>16);"
       " x = ((x & 0x55555555u) << 1) | ((x >> 1) & 0x55555555u);"
       " x = ((x & 0x00FF00FFu) << 8) | ((x >> 8) & 0x00FF00FFu);"
       " return x + (a & ~b); }", _UINT_ARGS),

    Fn("c_mul64", "ll->l",
       "64-bit multiply and shift -- edx:eax pairs and the CRT helper shapes",
       "EXP long long __cdecl c_mul64(long long a, long long b){"
       " return a*b - (a>>3) + (b<<5); }", _LONG_ARGS),

    Fn("c_add64", "ll->l",
       "64-bit add/subtract: add/adc and sub/sbb across the halves",
       "EXP long long __cdecl c_add64(long long a, long long b){"
       " long long s = a + b, d = a - b; return s ^ (d << 1); }", _LONG_ARGS),

    Fn("c_cmp64", "ll->l",
       "64-bit comparison, which lowers to a two-step compare on the halves",
       "EXP long long __cdecl c_cmp64(long long a, long long b){ long long r = 0;"
       " if(a < b) r |= 1; if(a == b) r |= 2; if(a > b) r |= 4;"
       " if((unsigned long long)a < (unsigned long long)b) r |= 8;"
       " return r; }", _LONG_ARGS),

    Fn("c_dwork", "dd->d",
       "double arithmetic through the x87 stack",
       "EXP double __cdecl c_dwork(double a, double b){"
       " return a*b + a/b - b*b + a; }", _DBL_ARGS),

    Fn("c_dmix", "dd->d",
       "doubles with a comparison and a branch -- fcom feeding a real jump",
       "EXP double __cdecl c_dmix(double a, double b){ double r = a;"
       " if(a > b) r = a - b; else if(a < b) r = b - a; else r = a + b;"
       " return r*a - b; }", _DBL_ARGS),

    Fn("c_fwork", "ff->f",
       "float arithmetic -- single precision through the same x87 stack",
       "EXP float __cdecl c_fwork(float a, float b){"
       " return a*b + b - a/b + a*a; }", _FLT_ARGS),

    Fn("c_bittest_loop", "ii->i",
       "the minimal shape that broke: a loop over bit tests. /O2 unrolls it "
       "into `test dl, 1<<i` + `je`, and the last iteration becomes "
       "`test dl, dl` + `jns` -- an 8-bit sign test",
       "EXP int __cdecl c_bittest_loop(int a, int b){ int i, r = 0;"
       " for(i = 0; i < 8; i++){ if(((a>>i)&1) != 0){ r += b + i; } }"
       " return r; }", _INT_ARGS),

    # ── only possible once we lift a linked image ───────────────────────────

    Fn("c_switch", "ii->i",
       "a switch: /O2 builds a jump table in .rdata and dispatches through it",
       "EXP int __cdecl c_switch(int a, int b){ switch(a & 7){"
       " case 0: return b+1;   case 1: return b*2;  case 2: return b-7;"
       " case 3: return b^0x5A; case 4: return b<<2; case 5: return b>>1;"
       " case 6: return -b;    default: return b+100; } }", _INT_ARGS),

    Fn("c_switch_sparse", "ii->i",
       "a sparse switch, which the compiler may lower to a chain instead",
       "EXP int __cdecl c_switch_sparse(int a, int b){ switch(a){"
       " case 0: return b;     case 5: return b*3;   case 17: return b-1;"
       " case 100: return b^7; case -3: return b+42; default: return b>>2; } }",
       _INT_ARGS),

    Fn("c_fconst", "dd->d",
       "float literals, which live in .rdata and are read back at runtime",
       "EXP double __cdecl c_fconst(double a, double b){"
       " return a*3.14159265358979 + b*0.5 - 1.25; }", _DBL_ARGS),

    Fn("c_mul64_helper", "ll->l",
       "64-bit multiply, which MSVC lowers to a call to __allmul -- so the "
       "CRT helper gets lifted too",
       "EXP long long __cdecl c_mul64_helper(long long a, long long b){"
       " return a*b; }", _LONG_ARGS),

    Fn("c_flags_across_call", "ii->i",
       "a callee sets ZF and returns, the caller branches on it -- the shape "
       "of the XDK CRT's x87 math (sin/cos/pow check their argument in a "
       "helper call and jz on the result); ZF before the call is the opposite "
       "for a = 0 and a = 1, so stale flags after the ret show",
       "static __declspec(naked) void flag_helper(void){"
       " __asm { test ecx, ecx } __asm { ret } }"
       " EXP int __cdecl c_flags_across_call(int a, int b){ int r; __asm {"
       " mov ecx, a } __asm { cmp ecx, 1 } __asm { call flag_helper }"
       " __asm { jz was_zero } __asm { mov eax, b } __asm { jmp done }"
       " was_zero: __asm { mov eax, 7 } done: __asm { mov r, eax } return r; }",
       _INT_ARGS),

]

# Deliberately absent: float-to-int. MSVC lowers it to __ftol2, whose modern
# LIBCMT implementation branches on __isa_available and, on the fast path, uses
# `fisttp` -- an SSE3 instruction. The Xbox is a Pentium III: no SSE3, and its
# XDK CRT's __ftol is plain x87. Comparing against the host's helper would be
# measuring this machine's CPU dispatch rather than the lifter, and the two
# sides cannot even agree on which branch to take (the native side has a CRT
# that ran its startup; the lifted side reads the image's initial value).
#
# The conformance runner reported the unlifted `fisttp` if such a function was
# pulled in; the lifter now lifts it (always chopping, sp00n 999a8c6), but the
# CPU-dispatch problem above stays, so the case stays out.


# -- C gate, G1 (2026-10-24): the register set across calls, tail calls, ICALL
# and SEH frames. C.1 moved every guest register into an explicit ctx; these
# check that what a callee writes, preserves or bridges (ebp through the XDK's
# __SEH_prolog) arrives in the caller. The esp delta across each call is folded
# into the result (x1000), and the harness also checks callee-saved registers,
# esp after the ret and the SEH chain head (fs:[0]) after every function.
_ZF_ARGS = [(1, 1), (2, 2), (5, 3), (4, 4), (0, 0), (7, 2), (-6, -6), (6, 7), (3, 0), (0, 3)]

CORPUS.extend([
    Fn("c_ctx_saved_regs", "ii->i",
       "a callee clobbers and restores ebx/esi/edi/ebp and writes eax/ecx/edx: "
       "the caller sees the preserved and the written registers and a balanced esp",
       r"""static __declspec(naked) void ctx_clobber(void){ __asm {
 push ebx
 push esi
 push edi
 push ebp
 mov ebx, 0x11111111
 mov esi, 0x22222222
 mov edi, 0x33333333
 mov ebp, 0x44444444
 mov ecx, 0x55
 mov edx, 0x66
 lea eax, [ebx+esi]
 pop ebp
 pop edi
 pop esi
 pop ebx
 ret } }
EXP int __cdecl c_ctx_saved_regs(int a, int b){ int r, s; __asm {
 mov ebx, a
 mov esi, b
 lea edi, [ebx+esi*2]
 mov s, esp
 call ctx_clobber
 add eax, ecx
 add eax, edx
 mov ecx, esp
 sub ecx, s
 imul ecx, ecx, 1000
 add eax, ecx
 mov ecx, ebx
 imul ecx, esi
 add eax, ecx
 add eax, edi
 mov r, eax } return r; }""", _INT_ARGS),

    Fn("c_tail_call_asm", "ii->i",
       "a callee that tail-jumps into another function after changing an argument "
       "slot: the result comes back through both, esp balanced",
       r"""static __declspec(naked) int tail_target(void){ __asm {
 mov eax, [esp+4]
 mov ecx, [esp+8]
 lea eax, [eax+ecx*2]
 ret } }
static __declspec(naked) int tail_mid(void){ __asm {
 add dword ptr [esp+4], 5
 xor ecx, ecx
 jmp tail_target } }
EXP int __cdecl c_tail_call_asm(int a, int b){ int r, s; __asm {
 mov s, esp
 push b
 push a
 call tail_mid
 add esp, 8
 mov ecx, esp
 sub ecx, s
 imul ecx, ecx, 1000
 add eax, ecx
 mov r, eax } return r; }""", _INT_ARGS),

    Fn("c_tail_call_c", "ii->i",
       "whatever /O2 makes of a call in tail position (a jmp that reuses the "
       "caller's argument slots, or a call)",
       r"""static __declspec(noinline) int __cdecl tc_leaf(int a, int b){ return a * 5 - (b >> 2); }
EXP int __cdecl c_tail_call_c(int a, int b){ if (a & 1) return tc_leaf(b, a + b); return tc_leaf(a - b, b); }""",
       _INT_ARGS),

    Fn("c_icall_table", "ii->i",
       "indirect calls through a const function-pointer table: each goes through "
       "the dispatch (recomp_lookup) to a lifted function, which returns into the loop",
       r"""typedef int (__cdecl *op_t)(int, int);
static __declspec(noinline) int __cdecl op_add(int a, int b){ return a + b; }
static __declspec(noinline) int __cdecl op_sub(int a, int b){ return a - b; }
static __declspec(noinline) int __cdecl op_mul(int a, int b){ return a * b; }
static __declspec(noinline) int __cdecl op_mix(int a, int b){ return (a ^ b) + (a >> 3); }
static op_t const g_ops[4] = { op_add, op_sub, op_mul, op_mix };
EXP int __cdecl c_icall_table(int a, int b){ int acc = 0, i;
 for (i = 0; i < 4; i++) acc = acc * 31 + g_ops[(a + i) & 3](a, b + i);
 return acc; }""", _INT_ARGS, also=("_op_add", "_op_sub", "_op_mul", "_op_mix")),

    Fn("c_x87_across_call", "dd->d",
       "x87 results returned in st(0) through calls and used by the caller",
       r"""static __declspec(noinline) double __cdecl x87_leaf(double x, double y){ return x * y + 0.5; }
EXP double __cdecl c_x87_across_call(double a, double b){ double t = x87_leaf(a, b);
 return t * a + x87_leaf(b, t); }""", _DBL_ARGS),

    Fn("c_seh_xdk_frame", "ii->i",
       "the XDK's __SEH_prolog/__SEH_epilog form: the helper sets up the caller's "
       "ebp (bridged through g_seh_ebp), registers a frame at fs:[0] and saves "
       "ebx/esi/edi; the body works on ebp-relative locals; the epilog unlinks "
       "the frame and restores the registers",
       r"""static __declspec(naked) void seh_handler(void){ __asm {
 mov eax, 1
 ret } }
static const unsigned g_seh_scope[3] = { 0xFFFFFFFFu, 0, 0 };
static __declspec(naked) void seh_prolog(void){ __asm {
 push offset seh_handler
 mov eax, fs:[0]
 push eax
 mov eax, [esp+10h]
 mov [esp+10h], ebp
 lea ebp, [esp+10h]
 sub esp, eax
 push ebx
 push esi
 push edi
 mov eax, [ebp-8]
 mov [ebp-18h], esp
 push eax
 mov eax, [ebp-4]
 mov dword ptr [ebp-4], 0FFFFFFFFh
 mov [ebp-8], eax
 lea eax, [ebp-10h]
 mov fs:[0], eax
 ret } }
static __declspec(naked) void seh_epilog(void){ __asm {
 mov ecx, [ebp-10h]
 mov fs:[0], ecx
 pop ecx
 pop edi
 pop esi
 pop ebx
 leave
 push ecx
 ret } }
static __declspec(naked) int seh_body(void){ __asm {
 push 0Ch
 push offset g_seh_scope
 call seh_prolog
 mov eax, [ebp+8]
 mov ecx, [ebp+0Ch]
 mov ebx, eax
 mov esi, ecx
 mov edi, 7
 and dword ptr [ebp-4], 0
 mov [ebp-1Ch], eax
 imul ecx, eax
 add [ebp-1Ch], ecx
 or dword ptr [ebp-4], 0FFFFFFFFh
 mov eax, [ebp-1Ch]
 add eax, edi
 call seh_epilog
 ret } }
EXP int __cdecl c_seh_xdk_frame(int a, int b){ int r, s; __asm {
 mov s, esp
 mov ebx, a
 mov esi, b
 mov edi, 0x1234
 push esi
 push ebx
 call seh_body
 add esp, 8
 add eax, ebx
 xor eax, esi
 add eax, edi
 mov ecx, esp
 sub ecx, s
 imul ecx, ecx, 1000
 add eax, ecx
 mov r, eax } return r; }""", _INT_ARGS),

    Fn("c_seh_try_c", "ii->i",
       "MSVC's own __try/__except/__finally frame (inline SEH4 prolog, scope "
       "levels, the __finally on the normal path)",
       r"""/* The corpus DLL links no CRT (/NOENTRY), so the frame handler is
   supplied here. It never runs: nothing raises inside these frames. */
int __cdecl _except_handler3(void *rec, void *frame, void *cx, void *dc)
{ (void)rec; (void)frame; (void)cx; (void)dc; return 1; }
static volatile int g_seh_sink;
static __declspec(noinline) int __cdecl seh_leaf(int a){ g_seh_sink = a; return a * 3; }
EXP int __cdecl c_seh_try_c(int a, int b){ int r = 0;
 __try { r = seh_leaf(a) + b; __try { r ^= seh_leaf(b); } __finally { r += 7; } }
 __except (1) { r = -1; }
 return r + a; }""", _INT_ARGS),

    Fn("c_lock_cmpxchg_jz", "ii->i",
       "the spin-lock form: lock cmpxchg on a global, then jz on its ZF",
       r"""static volatile long g_lock_word;
EXP int __cdecl c_lock_cmpxchg_jz(int a, int b){ int r; g_lock_word = a; __asm {
 mov eax, b
 mov ecx, 77
 lock cmpxchg dword ptr g_lock_word, ecx
 jz lk_taken
 mov eax, 2
 jmp lk_done
lk_taken:
 mov eax, 1
lk_done:
 add eax, g_lock_word
 mov r, eax } return r; }""", _ZF_ARGS),

    # THPS 0x000D1F0F (Watch Replay crash), fix 1a0a2b8: a ZF joined
    # from two paths that both set it from eax, then eax reloaded before the jcc.
    Fn("c_zf_join_reload", "ii->i",
       "two paths set ZF from eax, they join, eax is reloaded, then jne: the "
       "branch must test the joined result, not the reloaded register",
       r"""EXP int __cdecl c_zf_join_reload(int a, int b){ int r; __asm {
 mov eax, a
 mov esi, b
 test esi, 1
 jz zf_even
 sub eax, esi
 jmp zf_join
zf_even:
 xor eax, esi
zf_join:
 mov eax, b
 jne zf_nonzero
 mov eax, 99
zf_nonzero:
 mov r, eax } return r; }""", _ZF_ARGS),
])
