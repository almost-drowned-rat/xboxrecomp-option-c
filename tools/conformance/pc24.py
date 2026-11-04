"""Run the x87 PC=24 cases (cases_pc24.py) through the normal conformance runner.

    py -3 -m tools.conformance.pc24 [-k substr] [--keep] [-v]

Same toolchain and flow as `py -3 -m tools.conformance`, snippets phase only, with the case list swapped. Kept
separate (option-C branch) so the main suite's files stay untouched until the PC=24 cases are merged into
cases.py.
"""
import sys

from . import __main__ as runner
from .cases_pc24 import CASES_PC24


def main(argv=None) -> int:
    runner.CASES = CASES_PC24
    runner._WHY = {c["name"]: c["why"] for c in CASES_PC24}
    runner._TOL = {c["name"]: c.get("tol", 0.0) for c in CASES_PC24}
    return runner.main_with_args(["--only", "snippets"] + list(sys.argv[1:] if argv is None else argv))


if __name__ == "__main__":
    sys.exit(main())
