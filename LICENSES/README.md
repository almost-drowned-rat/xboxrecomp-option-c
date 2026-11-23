# Third-party licence texts

`LGPL-2.1.txt` is the verbatim GNU Lesser General Public License v2.1, from

    https://www.gnu.org/licenses/old-licenses/lgpl-2.1.txt

It applies to the files listed under "LGPL-2.1-or-later" in the top-level
NOTICE — the MCPX APU sources (src/apu) and `src/nv2a_gl/gl_vsh_ff.c`, extracted
from xemu and remaining the copyright of espes, Jannik Vogel and Matt Borgerson.
(`src/nv2a/nv2a_regs.h`, named here before, was removed on option-c in 2026-11.)

Shipping the verbatim text alongside those files is an LGPL requirement, not
a courtesy.

Releases (licence option C, maintainer 2026-11-17): the generic release scripts
(tools/release/port_release.ps1, port_release_linux.sh) put into every release
`licenses/THIRD_PARTY_NOTICES.txt` (the top-level THIRD_PARTY_NOTICES.txt: every
third-party part with its copyright and licence) and `licenses/source/` (from
tools/release/lgpl_source.py: the complete source of the LGPL parts as built,
with the headers they include and SOURCE.txt - toolkit commit, rebuild, relink).
The licence review is docs/licence_apu_ac97_ohci.md.
