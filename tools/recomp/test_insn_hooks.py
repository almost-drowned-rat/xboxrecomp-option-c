"""Per-game instruction hooks (--hooks, hooks_config.py) and --weak-functions."""
import pytest

from tools.recomp import config, hooks_config, translator

BASE = 0x10000
# mov eax, 1 ; mov ecx, 2 ; add eax, ecx ; ret 8
IMAGE = bytes.fromhex("b801000000" "b902000000" "01c8" "c20800")
MOV_ECX = BASE + 5
ADD = BASE + 10


def _translate(monkeypatch, hooks):
    config._install([config.Section(".text", BASE, len(IMAGE), 0, len(IMAGE), True)],
                    entry_point=BASE, kernel_thunk_addr=BASE, origin="insn-hooks")
    monkeypatch.setattr(translator, "INSN_HOOKS", hooks_config.parse(hooks))
    db = {BASE: {"end": BASE + len(IMAGE), "size": len(IMAGE)}}
    return translator.FunctionTranslator(IMAGE, db).translate_function(BASE, db[BASE])


def test_parse_rejects_bad_entries():
    with pytest.raises(ValueError):
        hooks_config.parse('[[hook]]\nat = 0x10\ncall = "not a name"\n')
    with pytest.raises(ValueError):
        hooks_config.parse('[[hook]]\nat = 0x10\ncall = "f"\nbogus = 1\n')
    h = hooks_config.parse('[[hook]]\nat = 0x10\ncall = "f"\non_true = "ret"\n')
    assert h[0x10][0]["on_true"] == "ret" and hooks_config.returns(h[0x10][0])


def test_before_hook_runs_ahead_of_its_instruction(monkeypatch):
    code = _translate(monkeypatch, f'[[hook]]\nat = 0x{MOV_ECX:X}\ncall = "fix"\n')
    assert code.count("fix(RECOMP_ARGS);") == 1
    assert "extern void fix(RECOMP_PARAMS);" in code
    assert code.index("eax = 1;") < code.index("fix(RECOMP_ARGS);") < code.index("ecx = 2;")
    assert f"loc_{MOV_ECX:08X}:" in code
    assert "g_ebp" not in code                                  # no ebp in this function: nothing handed over


def test_after_hook_runs_behind_its_instruction(monkeypatch):
    code = _translate(monkeypatch, f'[[hook]]\nat = 0x{MOV_ECX:X}\ncall = "fix"\nafter = true\n')
    assert code.index("ecx = 2;") < code.index("fix(RECOMP_ARGS);")
    assert f"loc_{ADD:08X}:" in code


def test_returning_hook_leaves_like_the_ret_or_jumps(monkeypatch):
    code = _translate(monkeypatch, f'[[hook]]\nat = 0x{MOV_ECX:X}\ncall = "skip"\n'
                                   f'on_true = "ret"\non_false = 0x{ADD:X}\n')
    line = next(ln for ln in code.splitlines() if "skip(RECOMP_ARGS)" in ln)
    assert "extern int skip(RECOMP_PARAMS);" in line
    assert "if (hook_) { esp += 12; return; }" in line          # ret 8
    assert f"if (!hook_) {{ goto loc_{ADD:08X}; }}" in line
    assert f"loc_{ADD:08X}:" in code


# push ebp ; mov ebp, esp ; mov eax, [ebp+8] ; pop ebp ; ret
EBP_IMAGE = bytes.fromhex("55" "89e5" "8b4508" "5d" "c3")
EBP_LOAD = BASE + 3


def _translate_ebp(monkeypatch, hooks):
    config._install([config.Section(".text", BASE, len(EBP_IMAGE), 0, len(EBP_IMAGE), True)],
                    entry_point=BASE, kernel_thunk_addr=BASE, origin="insn-hooks-ebp")
    monkeypatch.setattr(translator, "INSN_HOOKS", hooks_config.parse(hooks))
    db = {BASE: {"end": BASE + len(EBP_IMAGE), "size": len(EBP_IMAGE)}}
    return translator.FunctionTranslator(EBP_IMAGE, db).translate_function(BASE, db[BASE])


def test_hook_gets_the_local_ebp_and_gives_it_back(monkeypatch):
    """ebp is a C local of the function: handed to the hook through g_ebp, taken back after it, g_ebp restored."""
    code = _translate_ebp(monkeypatch, f'[[hook]]\nat = 0x{EBP_LOAD:X}\ncall = "look"\n')
    line = next(ln for ln in code.splitlines() if "look(RECOMP_ARGS)" in ln)
    assert line.index("g_ebp = ebp;") < line.index("look(RECOMP_ARGS);") < line.index("ebp = g_ebp;")
    assert line.index("ebp = g_ebp;") < line.index("g_ebp = hook_g_ebp_;")
    code = _translate_ebp(monkeypatch, f'[[hook]]\nat = 0x{EBP_LOAD:X}\ncall = "pick"\non_true = "ret"\n')
    line = next(ln for ln in code.splitlines() if "pick(RECOMP_ARGS)" in ln)
    assert line.index("ebp = g_ebp;") < line.index("if (hook_)")      # taken back before the result branches


def test_hook_line_behaves_in_c(monkeypatch, tmp_path):
    """The emitted line, compiled: the hook reads registers through ctx and the function's ebp, its writes stick,
    g_ebp is as before afterwards."""
    import shutil
    import subprocess
    cc = shutil.which("clang") or shutil.which("gcc")
    if not cc:
        pytest.skip("C compiler unavailable")
    code = _translate_ebp(monkeypatch, f'[[hook]]\nat = 0x{EBP_LOAD:X}\ncall = "look"\n')
    line = next(ln for ln in code.splitlines() if "look(RECOMP_ARGS)" in ln)
    src = """#include <stdio.h>
#include <stdint.h>
typedef struct { uint32_t r_eax, r_ebp; } X86Ctx;
#define RECOMP_PARAMS X86Ctx *ctx, uint8_t *base
#define RECOMP_ARGS ctx, base
#define g_eax (ctx->r_eax)
#define g_ebp (ctx->r_ebp)
void look(RECOMP_PARAMS) { (void)base; printf("seen eax=%u ebp=%u\\n", g_eax, g_ebp); g_eax += 1; g_ebp = 777; }
static void fn(RECOMP_PARAMS) {
    uint32_t ebp = 42;
@HOOK@
    printf("after eax=%u ebp=%u g_ebp=%u\\n", g_eax, ebp, g_ebp);
}
int main(void) { X86Ctx c = { 5, 9 }; fn(&c, 0); return 0; }
""".replace("@HOOK@", line)
    f = tmp_path / "h.c"
    f.write_text(src)
    exe = str(tmp_path / "h")
    built = subprocess.run([cc, "-O2", str(f), "-o", exe], capture_output=True, text=True)
    assert built.returncode == 0, built.stderr
    out = subprocess.run([exe], capture_output=True, text=True).stdout.split("\n")
    assert out[0] == "seen eax=5 ebp=42"                     # the function's ebp, not the published g_ebp (9)
    assert out[1] == "after eax=6 ebp=777 g_ebp=9"           # eax write via ctx, ebp write taken back, g_ebp restored


def test_after_a_ret_is_refused(monkeypatch):
    with pytest.raises(ValueError):
        _translate(monkeypatch, f'[[hook]]\nat = 0x{BASE + 12:X}\ncall = "f"\nafter = true\n')


def test_weak_function_is_overridden_at_link_time(monkeypatch, tmp_path):
    """The generated sub_X becomes a weak alias: alone it runs the generated
    body; linked with a strong sub_X, that one runs and can call sub_X_gen."""
    import shutil
    import subprocess
    cc = shutil.which("clang") or shutil.which("gcc")
    if not cc:
        pytest.skip("C compiler unavailable")
    name = f"sub_{BASE:08X}"
    code = translator.weaken(name, f"/* body */\nvoid {name}(RECOMP_PARAMS)\n{{\n    g = 1;\n}}\n")
    assert f"void {name}_gen(RECOMP_PARAMS)" in code and f"RECOMP_WEAK_ALIAS({name}, {name}_gen)" in code
    hdr = """#define RECOMP_PARAMS void *ctx, void *base
#define RECOMP_WEAK_ALIAS(name, target) \\
    extern void name(RECOMP_PARAMS) __attribute__((weak, alias(#target)));
"""
    gen = tmp_path / "gen.c"
    gen.write_text("int g;\n%svoid %s(RECOMP_PARAMS);\n%s\nvoid caller(void) { %s(0, 0); }\n" % (hdr, name, code, name))
    main = tmp_path / "main.c"
    main.write_text("#include <stdio.h>\nextern int g; void caller(void);\n"
                    "int main(void) { caller(); printf(\"%d\\n\", g); return 0; }\n")
    man = tmp_path / "man.c"
    man.write_text("extern int g; void %s_gen(void *, void *);\nvoid %s(void *ctx, void *base) { %s_gen(ctx, base); g += 10; }\n"
                   % (name, name, name))
    exe = str(tmp_path / "t")
    for srcs, want in (([gen, main], "1"), ([gen, main, man], "11")):
        built = subprocess.run([cc, "-O2", *map(str, srcs), "-o", exe], capture_output=True, text=True)
        assert built.returncode == 0, built.stderr
        ran = subprocess.run([exe], capture_output=True, text=True)
        assert ran.stdout.strip() == want
