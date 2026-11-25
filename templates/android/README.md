# Android APK template (option-c)

The Android packaging of an option-c port: one public APK that carries **no game code**. At the first start the
player picks their own disc image; the toolkit launcher extracts it, lifts it (embedded CPython), compiles and links
the game library with clang / lld on the device, and `loader.c` then `dlopen()`s it. Saves and media live in the
app's shared-storage folder (`Android/data/<appId>/files`).

Origin: Panzer Dragoon Orta Recomp's `packaging/android` (2026-11-11 to 11-19; proven on the Galaxy
S23, the AYN Odin 2 Portal and, through Turnip, the Galaxy Tab S8). Taken into the toolkit as MIT with the
maintainer's word on 2026-11-19. Orta's icons and names stay in Orta.

## What is in it

| Part | Files |
|---|---|
| Gradle project | `settings.gradle`, `build.gradle`, `gradle.properties`, `app/build.gradle` |
| Manifest, theme | `app/src/main/AndroidManifest.xml`, `app/src/main/res/values/styles.xml` |
| Java (package `xboxrecomp.port`, the same for every port) | `PortActivity` (asset extraction, BACK menu, saves import / export, driver and boot-video pickers), `SideMenu` (the BACK panel), `TouchOverlay` (touch controls; optional touch aiming), `RestartActivity` |
| Native, `libmain.so` | `app/jni/loader/`: `loader.c` (entry), `launcher_android.c` (the toolkit launcher with the port's config), `pylift.c` / `python_exe.c` (on-device lift), `ld_exe.c` (lld for clang), `touchpad.c` (virtual SDL gamepad), `driver_check.c` (Turnip choice on Adreno below Vulkan 1.3) |
| Vulkan shim, `libvulkan_port.so` | `app/jni/vkshim/`: the system driver, or a player-chosen Turnip through libadrenotools (`XR_VK_DRIVER=system|turnip` forces one; `xr_vk_driver()` tells which) |
| Dev aids | `app/jni/smoke/` (marker `smoke_test.txt`: Vulkan, pad / touch, route probes), `vkshim_test.c`, marker `lift_test.txt` (timed in-process lift) |
| Port example | `port_example/port_android.h` |

The Java package stays `xboxrecomp.port` in every port, so the JNI names in `jni/` never change; the port's
`applicationId` is its own (Android allows the two to differ).

## What a port supplies

**A folder (`-PportDir`)** with:
- `port_android.h` - the port's names (copy `port_example/port_android.h`): logcat tag, title, game library and its
  entry point, the lifter's game name and config package, the media env variable, the touch pad's name, optional
  touch aim and compile jobs, the launcher config (game, disc, title id, versions);
- `port_icon.h` - the launcher icon as a BMP array (the same file the Windows / Linux launcher uses).

The setup screen is the template's own since 2026-11-24 (from Panzer Dragoon Orta 1.3.0; the maintainer's
rule: ZOE's Android launcher is the design standard of every Android port, Android = the Windows Vista look):
`VistaSetupView.java` (drawn procedurally: the disc row, the graphics-driver row - the system driver or a Turnip
package from the player's own file, up to five kept -, Install / Quit / Play, the "Quit anyway?" question, the
progress with the step and its time), `PortSetup.java` (the bridge) and `jni/loader/setup_ui_android.c` (the toolkit
launcher's `game_setup_ui.h` over JNI; `xr_launcher_set_image_page` = the form). A port has no `setup_ui_sdl.c` any
more; it passes the game's name and the disc it asks for (`setupGame`, `setupDisc`).

The BACK menu has the fixed order of every Android port: Back to the game / Controls / Settings (Saves, Export log,
Boot animation, Graphics driver - five kept Turnip packages or the system driver -, About and licences).

**Soundtracks (ports with custom soundtracks, Gradle `-PmediaTracks=<the game's songs per album>`, e.g. THPS 2X: 128;
2026-11-24, the maintainer's design):** BACK > Settings > Soundtracks sits after Boot animation. *Import
folder...* opens the system folder picker (also SD cards); `MediaImport.java` copies the MP3 / FLAC / Ogg Vorbis / WAV
files and the .m3u / .m3u8 lists of the picked folder, sub folders kept, into `media/<folder name>/` - the media scan
(`src/media/media_scan.h`) then makes every folder that holds music one album, so a folder of album folders gives one
album each. Before copying it shows the albums, songs and size, the files it leaves out, albums over the game's limit
and a replace question; free space is checked; the copy has a progress bar with Cancel and leaves nothing behind when
cancelled. The page lists the albums (songs, size; tap = remove) and every change offers a restart (the game scans
media/ at its start). Copies, not links: the game opens plain paths, and the albums survive a removed SD card.

**The APU module (ports that use the APU emulation, e.g. Orta; not THPS, which is DirectSound HLE):** the runtime
SDK's `libxbox_apu.so` must go into the APK's `jniLibs/arm64-v8a/` next to the other native libraries - the game
looks for it by its bare name in the app's lib folder (`docs/apu_module.md`). Without it the game runs with no sound
emulation (since 2026-11-24 the APU register window then stays plain memory; before that the title's first APU
register write crashed - found on Orta Android).

**Thread placement check - every Android port (the maintainer's rule, 2026-11-24, after Orta Android's 33 ms
frames):** phone schedulers (EAS / WALT) may pack the runtime's busy threads onto one core at low load; D3D's
interrupt DPC then spins on the vblank bit while the NV2A flag thread that clears it waits for the same core. Since
option-c 50b0751 (`src/kernel/thread_name.h`) `xr_thread_name` gives APU frame / NV2A flags / timer-DPC /
NV2A exec a core each above the slowest cluster (`RECOMP_ANDROID_PIN=0` turns it off; the log line `[THREADS]
Android: ...` names the cores). Check each new port anyway: an A/B run with a recorded pad script (pinning on / off),
the long frames (> 25 ms) and GAME SPEED, the `[GATE] guest waits for a host ISR/DPC` maximum (Linux desktop ~1.4
ms; 20-30 ms = the packing), and the threads' run-queue waits (`/proc/<pid>/task/<tid>/schedstat`, the second field).
Orta on the Odin 2: long frames 64-73 -> 3-4 a minute, GAME SPEED 98 -> 99.9 %.

**A res folder (`-PportRes`)** with the icons: `mipmap-*/ic_launcher.png`, `mipmap-anydpi-v26/ic_launcher.xml`,
`drawable-nodpi/ic_launcher_fg.png`, and `values/colors.xml` (`ic_launcher_bg`).

**Gradle properties** (its build script passes them; nothing in a committed `local.properties`):

| Property | Meaning | Orta's value |
|---|---|---|
| `appId` | applicationId | `io.github.almostdrownedrat.orta` |
| `appName` | launcher label | `Orta Recomp` |
| `portTitle` | BACK menu title, dialogs | `Panzer Dragoon Orta Recomp` |
| `savesPrefix` | save backup file `<prefix>-saves-<stamp>.zip` | `Orta` |
| `touchName` | the virtual pad's name (= `PORT_TOUCH_NAME`) | `Orta touch controls` |
| `touchAim` | `true`: the BACK menu offers the aiming layout (needs `PORT_TOUCH_AIM`) | `true` |
| `setupGame` | the setup screen's game name | `Panzer Dragoon Orta` |
| `setupDisc` | the disc it asks for, with the region | `Panzer Dragoon Orta (Europe)` |
| `versionCode`, `versionName` | the APK's version | `7`, `0.1.1` |
| `keyAlias` | release key alias (with `releaseStore`, `releasePassFile`) | `orta` |
| `portDir`, `portRes` | the two folders above | (set when Orta adopts the template) |
| `sdl3Src`, `outDir`, `ndkPath`, `pythonPrefix`, `toolkit`, `adrenotoolsSrc`, `prebuiltLibs`, `assetsDir` | paths: SDL3 source, build root outside the repo, NDK r30, python.org's Android package, the option-c worktree, libadrenotools source, staged runtime libs, staged assets (`python/`, `tools/`, `probe/`) | Orta's `tools/build_android.ps1` / `.sh` |

Orta's `port_android.h` values: tag `ORTA`, title `Panzer Dragoon Orta Recomp`, `libORTA_game.so`,
`orta_game_main`, game name `ORTA`, config package `orta_config`, `ORTA_MEDIA_DIR`, `Orta touch controls`,
`orta_touch_aim`, jobs `3`; launcher `Panzer Dragoon Orta`, `Panzer Dragoon Orta (Europe)`, title id `0x4947002B`,
versions `{ "Europe", "57213FF35A19807DEF664F9018CC2923815DFA1F" }`.

## Adopting it in Orta

Orta's tree differs from the template in these names; a switch changes them on Orta's side:
- the Vulkan shim is `libvulkan_port.so` (was `libvulkan_orta.so`): Orta's `src/vk_check.c` and the link-line
  substitution in `tools/build_android.ps1` / `.sh` name it;
- `XR_VK_DRIVER` (was `ORTA_VK_DRIVER`), `xr_vk_driver()` (was `orta_vk_driver()`);
- Java package `xboxrecomp.port`, activity `PortActivity` (the applicationId stays Orta's, so installs update);
- the build script passes the properties above and stages `port_android.h`, `port_icon.h`, `setup_ui_sdl.c` and the
  icons.

## Licences

The template is MIT (`SPDX-License-Identifier: MIT` in each file). The Gradle / manifest skeleton follows SDL3's
`android-project` template (zlib); SDL's Java glue comes from the SDL3 source at build time and is not in this tree.
libadrenotools (BSD-2-Clause; its text is `LICENSE-libadrenotools-BSD-2.txt` here) is linked into
`libvulkan_port.so` from its own source tree and is not part of this tree; Turnip (Mesa, MIT) is never shipped, the
player picks it. The port's APK carries `THIRD_PARTY_NOTICES.txt` and the libadrenotools text.
