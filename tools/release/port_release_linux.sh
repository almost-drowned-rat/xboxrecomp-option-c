#!/usr/bin/env bash
# port_release_linux.sh - a port's Linux AppImage "<AppImageName>-<version>-x86_64.AppImage" (a local file only, never
# committed). 2026-11-16: Panzer Dragoon Orta's tools/release_linux.sh (Orta Recompiled; v1.1.0
# Linux port) moved into the toolkit with the port's names in a config file (maintainer's order 2026-11-15, step 4).
# The Linux twin of port_release.ps1: it reads the finished Linux build's build.ninja (main.c's compile flags +
# include folders, the game's link line) and ninja's header list of the port's sources, so the player's machine
# builds exactly what this one does. The AppImage holds:
#   AppRun, the .desktop entry + icon                        (<src>/packaging/linux)
#   usr/bin/<slug>                                           the toolkit's launcher with the port's names
#   usr/share/<slug>/tools/                                  RES, read-only (the launcher finds it by release.txt):
#     release.txt toolchain.txt lift.txt build.txt README.txt  the descriptors (toolchain.txt format 2, no URL)
#     src/ config/ sdk/ data/      the port's sources, its headers' copies, the prebuilt toolkit libraries, shipped data
#     lifter/                      the toolkit's translator (tools/ packages + templates/runtime)
#     llvm/ python/                clang + lld + a glibc sysroot, Python 3.12 + capstone (the toolchain bundle, read only)
#     licenses/
# Never in it: game data, generated C, the lifted game code.
#
# Runs inside WSL (Ubuntu 24.04) on the port's Linux build:
#   bash port_release_linux.sh <port.conf> <version> [bundle dir with llvm/ + python/ (default ~/b_bundle/out)] [--dev]
# --dev: the system's glibc / libstdc++ / libvulkan link line taken as it is, missing licence texts are warnings.
# OUT_DIR=<dir>: stage + AppImage there instead of $W/release and $W/deliver (tests).
#
# The config is a shell file (sourced); Orta's:
#   W=~/orta_linux                         # the port's WSL work folder: src/ (rsync of the repo), toolkit/, gen/, out*/build
#   SLUG=panzer-dragoon-orta-recomp        # the launcher's file name + share/<slug>
#   TARGET=orta_recomp                     # the port's CMake target
#   GAME=ORTA                              # the game exe (Linux name) and the lifter's --game-name
#   TITLE="Panzer Dragoon Orta Recomp"
#   APPIMAGE=Panzer_Dragoon_Orta_Recomp    # <APPIMAGE>-<version>-x86_64.AppImage
#   LICENCE=LICENSE-Orta-recomp-GPL-3.0.txt
#   PUBLIC_REPO=Orta-Recompiled
#   DATA="assets/pipelist_orta_pal.bin"    # optional, relative to src/: shipped into data/ by the launcher
#   DEPS=~/orta_deps                       # appimagetool/ + appimage_runtime/ + sdl3*/
#   ICON=assets/icon_256.png               # relative to src/
#   PUBLIC_TOOLKIT=~/xboxrecomp-option-c   # a public release: licenses/source from this clone of the public toolkit,
#   PUBLIC_TAG=v1.2.0                      # at this tag (default: the version); without it only a --dev build passes
#   LIFT_FILE=tools/lift.txt               # optional: the port's own lift steps (the same lift.txt as on Windows);
#                                          # default: Orta's steps (config/game.toml, --split 400)
#   LIFTER_FILES="port/hooks.toml=config/hooks.toml port/seeds=seeds/*_seeds.json"   # optional: under lifter/
#   RECOMPLAUNCHER_JSON=recomplauncher.json   # required (2026-11-24): the port's file, relative to src/ - AppDir root
set -eu
CONF="${1:?port config}"
VERSION="${2:?version}"
BUNDLE="${3:-$HOME/b_bundle/out}"
case "$BUNDLE" in --dev) BUNDLE=$HOME/b_bundle/out ;; esac
DEV=0; for a in "$@"; do [ "$a" = --dev ] && DEV=1; done
[[ "$VERSION" =~ ^[A-Za-z0-9._-]{1,31}$ ]] || { echo "version: up to 31 characters A-Z a-z 0-9 . _ -"; exit 1; }
DATA=""; ICON=assets/icon_256.png
# shellcheck disable=SC1090
. "$CONF"
for v in W SLUG TARGET GAME TITLE APPIMAGE LICENCE PUBLIC_REPO DEPS RECOMPLAUNCHER_JSON; do
  [ -n "${!v:-}" ] || { echo "$CONF: $v not set"; exit 1; }
done
TRX=${TARGET//./[.]}               # the target as a regex (awk -v / grep -E / sed alike)

# the Linux build: the release one (out_rel/build, the toolchain file = glibc 2.35 sysroot) when not --dev, else the
# dev build (out/build); RELEASE_BUILD=<dir> picks one
if [ -n "${RELEASE_BUILD:-}" ]; then B=$RELEASE_BUILD
elif [ $DEV = 0 ]; then B=$W/out_rel/build
else B=$W/out/build; fi
SRC=$W/src                         # the port's sources as built (rsync copy of the repo)
TK=$W/toolkit                      # the pinned toolkit as built
SDLINC=$(sed -n 's/^SDL3_DIR:[A-Z]*=\(.*\)\/lib\/cmake\/SDL3$/\1/p' "$B/CMakeCache.txt" 2>/dev/null)/include
SYS=$HOME/deps/sys/usr
# the build machine's toolchain bundle (XR_LINUX_BUNDLE of the release build; paths into it become {TOOLS}/...)
DEVB=$(sed -n 's/^XR_LINUX_BUNDLE:[A-Z]*=//p' "$B/CMakeCache.txt" 2>/dev/null)
to_tools() {   # a path / flag into the build machine's bundle -> the same file in the shipped bundle (RES)
  if [ -n "$DEVB" ]; then printf '%s' "${1//$DEVB/\{TOOLS\}}"; else printf '%s' "$1"; fi
}
N=$B/build.ninja
[ -f "$N" ] || { echo "no build in $B"; exit 1; }
[ -x "$B/launcher/$SLUG" ] || { echo "no launcher $B/launcher/$SLUG"; exit 1; }
[ -x "$BUNDLE/llvm/bin/clang" ] || { echo "no clang in $BUNDLE/llvm/bin (toolkit tools/linux/make_toolchain_bundle.sh)"; exit 1; }
[ -x "$BUNDLE/python/bin/python3" ] || { echo "no python in $BUNDLE/python/bin"; exit 1; }

STAGE=${OUT_DIR:-$W/release}/$(date +%Y%m%d_%H%M%S)_$VERSION
[ -e "$STAGE" ] && { echo "exists: $STAGE"; exit 1; }
A=$STAGE/AppDir
R=$A/usr/share/$SLUG/tools
mkdir -p "$R" "$A/usr/bin" "$A/usr/share/applications" "$A/usr/share/icons/hicolor/256x256/apps"
warn() { if [ $DEV = 1 ]; then echo "WARNING (dev): $*"; else echo "ERROR: $*"; exit 1; fi; }

# -- the compile flags of main.c's object, the game's link line ---------------------------------------------------
vars() {   # vars <build line regex>: the "  NAME = value" lines after it
  awk -v re="$1" '$0 ~ re {f=1; next} f && /^  [A-Z_]+ = / {print; next} f {exit}' "$N"
}
MV=$(vars "^build CMakeFiles/$TRX[.]dir/src/main[.]c[.]o:")
FLAGS=$(sed -n 's/^  FLAGS = //p' <<<"$MV"); DEFINES=$(sed -n 's/^  DEFINES = //p' <<<"$MV")
INCLUDES=$(sed -n 's/^  INCLUDES = //p' <<<"$MV")
LINKLINE=$(grep -E "^build [^:]*/$GAME: C_EXECUTABLE_LINKER__$TRX" "$N" | head -1)
[ -n "$LINKLINE" ] || { echo "$GAME's link line not found"; exit 1; }
LV=$(vars "^build [^:]*/$GAME: C_EXECUTABLE_LINKER__$TRX")
LFLAGS=$(sed -n 's/^  LINK_FLAGS = //p' <<<"$LV"); LLIBS=$(sed -n 's/^  LINK_LIBRARIES = //p' <<<"$LV")
PORTOBJS=$(sed -e "s/^[^:]*: C_EXECUTABLE_LINKER__${TRX}_[A-Za-z]* //" -e 's/ | .*//' <<<"$LINKLINE" | tr ' ' '\n' | grep "^CMakeFiles/$TRX[.]dir/src/.*[.]o\$")
[ -n "$PORTOBJS" ] || { echo "no port objects on the link line"; exit 1; }

# include roots -> where they go below RES ({TOOLS})
map_path() {
  local p; p=$(realpath -m "$1")
  case "$p" in
    "$SRC/src"|"$SRC/src/"*)   echo "src${p#$SRC/src}" ;;
    "$TK"|"$TK/"*)             echo "sdk/toolkit${p#$TK}" ;;
    "$SDLINC"|"$SDLINC/"*)     echo "sdk/include${p#$SDLINC}" ;;
    "$DEPS"/sdl3*/include|"$DEPS"/sdl3*/include/*) echo "sdk/include${p#*/include}" ;;   # SDL3 (dev or sysroot build)
    "$SYS/include"|"$SYS/include/"*) echo "sdk/sysinclude${p#$SYS/include}" ;;
    "$B"|"$B/"*)               echo "sdk/build${p#$B}" ;;
    *) return 1 ;;
  esac
}
CC=()
# the player's compiler sees only the shipped glibc 2.35 headers (the bundle's sysroot), never his system's
[ -d "$BUNDLE/llvm/sysroot" ] && CC+=("--sysroot={TOOLS}/llvm/sysroot")
for f in $FLAGS; do
  case "$f" in -g|--sysroot=*|--sysroot) continue ;; esac   # the sysroot: once, above, as {TOOLS}
  CC+=("$(to_tools "$f")")
done
for d in $DEFINES; do
  # a development aid, never in a release (maintainer 2026-11-16; toolkit XBOXRECOMP_ICALL_FEEDBACK) - the port's switch
  [ "$d" = -DRECOMP_ICALL_FEEDBACK ] && echo "WARNING: RECOMP_ICALL_FEEDBACK is in the player's build (remove it in the port's CMakeLists)"
  CC+=("$d")
done
for a in $INCLUDES; do
  [ "${a#-I}" != "$a" ] || continue
  p=${a#-I}
  if [ "$(realpath -m "$p")" = "$(realpath -m "$W/gen")" ]; then CC+=("-I{GEN}"); continue; fi
  m=$(map_path "$p") || { echo "include folder $p is not under a known root"; exit 1; }
  CC+=("-I{TOOLS}/$m")
done

# -- the headers the port's sources include (ninja's list), our sources, config/, data/, the generated port_icon.h -
NH=0
while read -r h; do
  case "$h" in ""|CMakeFiles/*|*.c|*/gen/*|/usr/*) continue ;; esac   # /usr: the compiler's / system's own
  [ -n "$DEVB" ] && [ "${h#$DEVB/}" != "$h" ] && continue            # the bundle's (compiler / sysroot): shipped
  m=$(map_path "$h") || { echo "header $h is not under a known root"; exit 1; }
  mkdir -p "$(dirname "$R/$m")"; cp "$h" "$R/$m"; NH=$((NH + 1))
done < <(cd "$B" && ninja -t deps $PORTOBJS | sed -n 's/^    //p' | sort -u)
SRCLINES=()
for o in $PORTOBJS; do
  rel=${o#CMakeFiles/$TARGET.dir/}; rel=${rel%.o}
  mkdir -p "$(dirname "$R/$rel")"; cp "$SRC/$rel" "$R/$rel"
  SRCLINES+=("src	{TOOLS}/$rel")
done
mkdir -p "$R/config/variants" "$R/sdk/build"
cp "$SRC/config/game.toml" "$R/config/"; cp "$SRC"/config/variants/*.toml "$R/config/variants/"
# shipped data (keys only, never game data): the toolkit launcher copies tools/data/* into data/ (launcher.c ship_data)
for d in $DATA; do
  [ -f "$SRC/$d" ] || { echo "data file missing: $SRC/$d"; exit 1; }
  mkdir -p "$R/data"; cp "$SRC/$d" "$R/data/"
  echo "data: $(basename "$d") ($(du -k "$SRC/$d" | cut -f1) KB)"
done
[ -f "$B/port_icon.h" ] && cp "$B/port_icon.h" "$R/sdk/build/"
echo "headers: $NH; port sources: ${#SRCLINES[@]}"
# the APU module (licence option C: the emulation as a replaceable shared library; launcher.c ship_data:
# tools/runtime/* -> next to the game exe). A build with XBOXRECOMP_APU_SHARED=OFF has none.
APUMOD=$B/xboxrecomp/src/apu/libxbox_apu.so
[ -f "$APUMOD" ] || APUMOD=$(find "$B" -name libxbox_apu.so -type f | head -1)
if [ -n "$APUMOD" ] && [ -f "$APUMOD" ]; then
  mkdir -p "$R/runtime"; cp "$APUMOD" "$R/runtime/libxbox_apu.so"
  echo "APU module: libxbox_apu.so ($(du -k "$APUMOD" | cut -f1) KB)"
elif grep -q '^XBOXRECOMP_APU_SHARED:BOOL=ON' "$B/CMakeCache.txt" 2>/dev/null; then
  echo "ERROR: the APU module libxbox_apu.so is not in the build $B"; exit 1
fi

# -- libraries (the link line, in order) --------------------------------------------------------------------------
mkdir -p "$R/sdk/lib"
LINK=()
for f in $LFLAGS; do
  case "$f" in --sysroot=*|-fuse-ld=*) continue ;; esac  # added once below, as {TOOLS}
  LINK+=("$(to_tools "$f")")
done
LINK+=("-L{TOOLS}/sdk/lib")
if [ -d "$BUNDLE/llvm/sysroot" ]; then LINK+=("-fuse-ld=lld" "--sysroot={TOOLS}/llvm/sysroot"); else warn "no llvm/sysroot in the bundle: the player's link would use his system's glibc"; fi
for a in $LLIBS; do
  if [ -n "$DEVB" ] && [ "${a#$DEVB}" != "$a" ]; then  # the bundle's own files (libc++.a, the libvulkan link stub)
    t=$(to_tools "$a"); [ -e "$BUNDLE${a#$DEVB}" ] || warn "$t is not in the shipped bundle $BUNDLE"
    LINK+=("$t"); continue
  fi
  case "$a" in
    -Wl,-rpath,*) continue ;;                          # the dev machine's dependency folder: never in a release
    -l*) lib=${a#-l}
         if [ -f "$SYS/lib/x86_64-linux-gnu/lib$lib.a" ] && [ ! -f "$R/sdk/lib/lib$lib.a" ]; then
           cp "$SYS/lib/x86_64-linux-gnu/lib$lib.a" "$R/sdk/lib/"           # -lSPIRV-Tools* (static)
         fi
         [ "$lib" = stdc++ ] && warn "-lstdc++: the system's GCC C++ runtime (release: libc++ static)"
         LINK+=("$a") ;;
    -*) LINK+=("$a") ;;
    *) s=$a; [ "${s#/}" = "$s" ] && s=$B/$a
       leaf=$(basename "$s")
       [ -f "$R/sdk/lib/$leaf" ] || cp "$s" "$R/sdk/lib/$leaf"
       case "$leaf" in *.so*) warn "$leaf on the link line (release: Vulkan by dlopen)";; esac
       LINK+=("{TOOLS}/sdk/lib/$leaf") ;;
  esac
done
echo "libraries: $(ls "$R/sdk/lib" | wc -l) ($(du -sm "$R/sdk/lib" | cut -f1) MB)"

# -- the lifter (the port's lift steps: config/game.toml + the variant file) ------------------------------------
L=$R/lifter
mkdir -p "$L/tools" "$L/templates"
cp "$TK/tools/__init__.py" "$TK/tools/gameconfig.py" "$TK/tools/variant_prefix.py" "$L/tools/"
for m in xbe_parser disasm func_id abi_analysis recomp ghidra_naming; do
  rsync -a --exclude __pycache__ --exclude tests --exclude 'test_*.py' "$TK/tools/$m/" "$L/tools/$m/"
done
cp -r "$TK/templates/runtime" "$L/templates/runtime"
# a port with its own lift (THPS 2X, 2026-11-24): LIFTER_FILES="<path under lifter/>=<source relative to src/> ..."
# (a wildcard or a folder -> into that folder), LIFT_FILE = its lift.txt relative to src/ (the same file as on Windows)
for pair in ${LIFTER_FILES:-}; do
  dst="$L/${pair%%=*}"; pat="${pair#*=}"
  # shellcheck disable=SC2206
  files=( $SRC/$pat )
  [ -e "${files[0]}" ] || { echo "LIFTER_FILES: nothing at $SRC/$pat"; exit 1; }
  if [ ${#files[@]} -gt 1 ] || [ -d "${files[0]}" ] || [[ "$pat" == *[*?]* ]]; then
    mkdir -p "$dst"; cp -r "${files[@]}" "$dst/"
  else
    mkdir -p "$(dirname "$dst")"; cp "${files[0]}" "$dst"
  fi
done
CFG="{TOOLS}/config/game.toml"
if [ -n "${LIFT_FILE:-}" ]; then
  tr -d '\r' < "$SRC/$LIFT_FILE" > "$R/lift.txt"
else {
  echo "# the lift (the same steps as the port's tools/lift.ps1): name, then python's arguments; {XBE} {PIPE} {GEN} {TOOLS}"
  printf 'mkdir\t{PIPE}/disasm\t{PIPE}/func_id\t{PIPE}/abi\t{PIPE}/recomp\t{GEN}\n'
  printf 'xbe_parser\t-m\ttools.xbe_parser\t{XBE}\t--json\t{PIPE}/xbe.json\t--quiet\n'
  printf 'disasm\t-m\ttools.disasm\t{XBE}\t-o\t{PIPE}/disasm\t--analysis-json\t{PIPE}/xbe.json\t--force\t--no-asm\t--game-config\t%s\n' "$CFG"
  printf 'func_id\t-m\ttools.func_id\t{XBE}\t--functions\t{PIPE}/disasm/functions.json\t--strings\t{PIPE}/disasm/strings.json\t--xrefs\t{PIPE}/disasm/xrefs.json\t-o\t{PIPE}/func_id\n'
  printf 'abi_analysis\t-m\ttools.abi_analysis\t{XBE}\t--disasm-dir\t{PIPE}/disasm\t--func-id-dir\t{PIPE}/func_id\t--output-dir\t{PIPE}/abi\n'
  printf 'recomp\t-m\ttools.recomp\t{XBE}\t--all\t--game-config\t%s\t--game-name\t%s\t--gen-dir\t{GEN}\t--disasm-dir\t{PIPE}/disasm\t--func-id-dir\t{PIPE}/func_id\t--abi-dir\t{PIPE}/abi\t-o\t{PIPE}/recomp\t--split\t400\t--partitions\t{PIPE}/partitions.json\t--repartition\n' "$CFG" "$GAME"
} > "$R/lift.txt"
fi
grep -q $'^recomp\t' "$R/lift.txt" || { echo "the lift has no recomp line (${LIFT_FILE:-default})"; exit 1; }

# -- the toolchain: the bundle (llvm/ + python/), copied; capstone + the lifter's path into Python -----------------
cp -a "$BUNDLE/llvm" "$R/llvm"
cp -a "$BUNDLE/python" "$R/python"
SP=$(ls -d "$R"/python/lib/python3.*/site-packages | head -1)
if [ ! -d "$SP/capstone" ]; then
  CS=$(ls -d "$HOME"/venv/lib/python3.12/site-packages/capstone | head -1)
  cp -a "$CS" "$SP/"; cp -a "$(dirname "$CS")"/capstone-*.dist-info "$SP/"
fi
# python runs with -I (no PYTHONPATH, no cwd): a .pth in site-packages puts the lifter on sys.path, relative to it
echo "../../../../lifter" > "$SP/xr_lifter.pth"
find "$R/python" "$L" -name __pycache__ -type d -prune -exec rm -rf {} +
"$R/python/bin/python3" -I -m compileall -q -j 0 "$L" "$SP/capstone" >/dev/null   # .pyc beside: the squashfs is read-only
"$R/python/bin/python3" -I -c "import sys; sys.path.insert(0, '$L'); import capstone, tools.recomp" \
  || { echo "the bundled Python cannot import capstone / the lifter"; exit 1; }

# -- licences, descriptors -----------------------------------------------------------------------------------------
mkdir -p "$R/licenses"
lic() {   # lic <name> <source>
  if [ -f "$2" ]; then cp "$2" "$R/licenses/$1"; else warn "licence text missing for $1: $2"; fi
}
lic "$LICENCE"                               "$SRC/LICENSE"
lic LICENSE-xboxrecomp-toolkit-MIT.txt       "$TK/LICENSE"
lic NOTICE-toolkit.txt                       "$TK/NOTICE"
lic LICENSE-xemu-parts-LGPL-2.1.txt          "$TK/LICENSES/LGPL-2.1.txt"
lic LICENSE-pl_mpeg-MIT.txt                  "$TK/src/bootvideo/third_party/pl_mpeg/LICENSE"
lic LICENSE-miniz-MIT.txt                    "$TK/src/shaderpack/third_party/miniz/LICENSE"
lic LICENSE-SMAA-MIT.txt                     "$TK/src/nv2a_vk/third_party/smaa/LICENSE.txt"
lic LICENSE-FSR1-MIT.txt                     "$TK/src/nv2a_vk/third_party/fsr1/LICENSE.txt"
lic LICENSE-DejaVu-fonts.txt                 "$TK/src/nv2a_vk/third_party/dejavu/LICENSE"
lic LICENSE-LiberationFonts-OFL.txt          "$TK/src/launcher/third_party/liberation/LICENSE"
lic LICENSE-SDL3-zlib.txt                    "$HOME/deps/SDL3/LICENSE.txt"
lic COPYRIGHT-glslang.txt                    "$SYS/share/doc/glslang-dev/copyright"
lic COPYRIGHT-SPIRV-Tools.txt                "$SYS/share/doc/spirv-tools/copyright"
lic LICENSE-capstone-BSD.txt                 "$(ls "$SP"/capstone-*.dist-info/LICENSE* 2>/dev/null | head -1)"
# the toolchain bundle's own licence folders (llvm/LICENSES = LLVM incl. libc++ / lld, glibc and the GCC runtime
# pieces + NOTICE; python/LICENSES = Python, capstone, the built-in libraries + NOTICE), kept whole
for d in llvm python; do
  if [ -d "$R/$d/LICENSES" ] && [ -n "$(ls "$R/$d/LICENSES")" ]; then cp -r "$R/$d/LICENSES" "$R/licenses/$d"
  else warn "the bundle has no $d/LICENSES folder"; fi
done
[ -f "$R/licenses/llvm/LLVM.txt" ] || warn "no LLVM licence text in llvm/LICENSES"
ls "$R"/licenses/llvm/libc6*.txt >/dev/null 2>&1 || warn "no glibc licence text in llvm/LICENSES"
[ -f "$R/licenses/python/Python.txt" ] || warn "no Python licence text in python/LICENSES"
lic THIRD_PARTY_NOTICES.txt                  "$TK/THIRD_PARTY_NOTICES.txt"
# licence option C (maintainer 2026-11-17): the corresponding source of the LGPL parts (the xemu APU core with this
# project's changes, the headers it needs) in every release - licenses/source/, with SOURCE.txt
# The files come from the PUBLIC toolkit snapshot at the release's tag (config PUBLIC_TOOLKIT = its clone,
# PUBLIC_TAG default = the version): the private tree's comments are not scrubbed and its commits are not public.
# Without PUBLIC_TOOLKIT (a test release) the private tree is used and SOURCE.txt says so - never publish that.
if [ -n "${PUBLIC_TOOLKIT:-}" ]; then
  PTAG=${PUBLIC_TAG:-$VERSION}
  PSHA=$(git -C "$PUBLIC_TOOLKIT" rev-parse "$PTAG^{commit}") || { echo "ERROR: $PUBLIC_TOOLKIT has no tag $PTAG"; exit 1; }
  PTREE=$STAGE/public_toolkit; rm -rf "$PTREE"; mkdir -p "$PTREE"
  git -C "$PUBLIC_TOOLKIT" archive --format=tar "$PSHA" | tar -x -C "$PTREE"
  "$R/python/bin/python3" -I "$TK/tools/release/lgpl_source.py" "$PTREE" "$R/licenses/source" --version "$VERSION" \
    --commit "xboxrecomp-option-c, tag $PTAG, commit $PSHA" --notices "$TK/THIRD_PARTY_NOTICES.txt" \
    || { echo "ERROR: lgpl_source.py failed - the LGPL source bundle is incomplete"; exit 1; }
else
  warn "no PUBLIC_TOOLKIT in the config: licenses/source comes from the PRIVATE tree - a test release only"
  "$R/python/bin/python3" -I "$TK/tools/release/lgpl_source.py" "$TK" "$R/licenses/source" \
    --version "$VERSION (PRIVATE TREE - not for publishing)" \
    || { echo "ERROR: lgpl_source.py failed - the LGPL source bundle is incomplete"; exit 1; }
fi
cat > "$R/licenses/README.txt" <<EOF
$TITLE $VERSION (Linux AppImage) - licences of everything in this release.

$TITLE (the port's own code): GPL-3.0-or-later. Built on the xboxrecomp toolkit, branch option-c
(MIT; its third-party parts in NOTICE-toolkit.txt: the xemu parts LGPL-2.1-or-later, pl_mpeg / miniz / SMAA /
dr_libs / stb_vorbis / stb_truetype / burnout3-sw MIT or public domain, the DejaVu Sans Mono Bold font of the status
overlays under the Bitstream Vera / Arev licence, the launcher window's Liberation Sans under the SIL OFL 1.1). Linked in: SDL3 (zlib), glslang and SPIRV-Tools (BSD / MIT /
Apache-2.0). Shipped for the translator and the first start: LLVM 23.1.1 clang + lld (Apache-2.0 with LLVM exception)
with a glibc 2.35 sysroot (LGPL-2.1-or-later), Python 3.12 (PSF licence) with capstone 5.0.9 (BSD).
The AppImage runtime (https://github.com/AppImage/type2-runtime, MIT) starts this file.
The compiler's and Python's own licence folders: llvm/ and python/ (each with a NOTICE.txt naming every file's origin).
THIRD_PARTY_NOTICES.txt: every third-party part with its copyright and licence.
source/: the complete source of the LGPL-2.1-or-later parts (the xemu MCPX APU core with this project's changes) as
built into this release; source/SOURCE.txt says how to rebuild that library and relink the game with it.

Source of this release: the public $PUBLIC_REPO repository, tag $VERSION.
EOF
{
  echo "# compile + link (from $TARGET's Linux build.ninja): cc = the common arguments, src / gen = the units, link"
  printf 'cc'; printf '\t%s' "${CC[@]}"; echo
  printf '%s\n' "${SRCLINES[@]}"
  printf 'gen\t*.c\n'
  printf 'link'; printf '\t%s' "${LINK[@]}"; echo
} > "$R/build.txt"
printf 'format\t2\nclang\tllvm/bin/clang\npython\tpython/bin/python3\n' > "$R/toolchain.txt"
echo "$VERSION" > "$R/release.txt"
cat > "$R/README.txt" <<EOF
$TITLE $VERSION - the files the launcher builds the game with (read-only inside the AppImage).
AI disclosure:  AI assistance yes (Claude Code by Anthropic).
Nothing here is game code or game data: the game is translated from your own disc image on your machine.
Licences: licenses/ (the toolkit's NOTICE lists every third-party part).
EOF

# -- AppDir + AppImage ----------------------------------------------------------------------------------------------
cp "$B/launcher/$SLUG" "$A/usr/bin/$SLUG"
cp "$SRC/packaging/linux/AppRun" "$A/AppRun"; chmod +x "$A/AppRun"
cp "$SRC/packaging/linux/$SLUG.desktop" "$A/"; cp "$SRC/packaging/linux/$SLUG.desktop" "$A/usr/share/applications/"
cp "$SRC/$ICON" "$A/$SLUG.png"; cp "$SRC/$ICON" "$A/usr/share/icons/hicolor/256x256/apps/$SLUG.png"
# recomplauncher.json (the maintainer 2026-11-24: a fixed part of every release): at the AppDir root, where RecompLauncher
# reads it from the image without running it - JSON, "format": 1, at most 1 MiB (a tar.gz the port makes from the same
# files carries it at its top)
RLJ=$SRC/$RECOMPLAUNCHER_JSON
[ "$(stat -c %s "$RLJ")" -le 1048576 ] || { echo "$RLJ is larger than 1 MiB"; exit 1; }
python3 -I -c 'import json,sys; d=json.load(open(sys.argv[1],encoding="utf-8-sig")); sys.exit(0 if d.get("format")==1 else 1)' "$RLJ" \
  || { echo "$RLJ: not JSON, or no \"format\": 1"; exit 1; }
sed '1s/^\xEF\xBB\xBF//' "$RLJ" > "$A/recomplauncher.json"     # (a BOM from a Windows editor dropped)

# glibc check: no binary in the AppDir may need more than the sysroot's glibc (2.35)
MAXG=$(find "$A" -type f \( -perm -u+x -o -name '*.so*' \) -exec sh -c 'objdump -T "$1" 2>/dev/null' _ {} \; \
       | grep -o 'GLIBC_2\.[0-9]*' | sort -t. -k2 -n -u | tail -1)
echo "highest glibc symbol version in the AppDir: ${MAXG:-none}"
if [ -n "$MAXG" ] && [ "${MAXG#GLIBC_2.}" -gt 35 ]; then warn "binaries need $MAXG > GLIBC_2.35 (release: build with cmake/linux-sysroot.cmake)"; fi

T=$DEPS/appimagetool/squashfs-root/AppRun
RT=$DEPS/appimage_runtime/runtime-x86_64
OUTD=${OUT_DIR:-$W/deliver}; mkdir -p "$OUTD"
OUT=$OUTD/$APPIMAGE-$VERSION-x86_64.AppImage
rm -f "$OUT"
ARCH=x86_64 "$T" --runtime-file "$RT" --comp zstd -n "$A" "$OUT" > "$STAGE/appimagetool.log" 2>&1 \
  || { echo "appimagetool FAILED"; tail -20 "$STAGE/appimagetool.log"; exit 1; }
echo "AppDir: $(du -sm "$A" | cut -f1) MB  (RES $(du -sm "$R" | cut -f1) MB: llvm $(du -sm "$R/llvm" | cut -f1), python $(du -sm "$R/python" | cut -f1), sdk $(du -sm "$R/sdk" | cut -f1), lifter $(du -sm "$L" | cut -f1))"
echo "AppImage: $OUT ($(du -m "$OUT" | cut -f1) MB, version $VERSION$( [ $DEV = 1 ] && echo ', DEV build' ))"
printf 'version %s\nappimage %s\nsha256 %s\ndev %s\n' "$VERSION" "$OUT" "$(sha256sum "$OUT" | cut -d' ' -f1)" "$DEV" > "$STAGE/release.txt"
