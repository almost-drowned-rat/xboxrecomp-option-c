/* SPDX-License-Identifier: MIT
 * port_android.h - a port's names for the option-c Android template (templates/android). The port keeps its own copy
 * in its -PportDir folder, next to port_icon.h (the launcher icon as a BMP array, as on Windows / Linux) and
 * setup_ui_sdl.c (the launcher's progress UI). Every name below is the port's; the template's C code never names a
 * game. Example values for a port "MYGAME"; Panzer Dragoon Orta's are in templates/android/README.md. */
#ifndef PORT_ANDROID_H
#define PORT_ANDROID_H

/* logcat tag and the env prefix of the port's own switches */
#define PORT_TAG              "MYGAME"
/* the player-facing name: message boxes, the BACK menu title */
#define PORT_TITLE            "My Game Recomp"
/* the game library the launcher builds on the device and loader.c dlopen()s, and its entry point */
#define PORT_GAME_LIB         "libMYGAME_game.so"
#define PORT_GAME_MAIN        "mygame_game_main"
/* the lifter's --game-name and the Python package (in the staged site-packages) that holds the port's game.toml */
#define PORT_GAME_NAME        "MYGAME"
#define PORT_CONFIG_PKG       "mygame_config"
/* where the game finds the player's media (boot video, soundtracks): the env variable the port's host code reads */
#define PORT_MEDIA_ENV        "MYGAME_MEDIA_DIR"
/* the virtual gamepad's name (the BACK menu's Controllers page tells it apart by this name) */
#define PORT_TOUCH_NAME       "My Game touch controls"
/* optional: the game library's touch-aim entry (int fn(int down, float x, float y, float w, float h)); leave it
 * undefined when the port has none. Gradle -PtouchAim=true shows the aiming layout in the BACK menu. */
/* #define PORT_TOUCH_AIM     "mygame_touch_aim" */
/* optional: the on-device compile's parallel jobs (big lifted units on 8 GB phones: 3) */
/* #define PORT_LAUNCHER_JOBS "3" */

/* the toolkit launcher's config (src/launcher/launcher.h XrLauncherConfig): the game, the disc image name the
 * installer shows, the release title, the Xbox title id, and the supported versions (region, SHA-1 of the XBE) */
#define PORT_LAUNCHER_GAME    "My Game"
#define PORT_LAUNCHER_DISC    "My Game (Europe)"
#define PORT_TITLE_ID         0x00000000u
#define PORT_VERSIONS         { "Europe", "0000000000000000000000000000000000000000" },

#endif
