/* SPDX-License-Identifier: MIT
 * launcher_android.c - the toolkit's launcher (option-c src/launcher) with the port's names on Android
 * (2026-11-11, for Panzer Dragoon Orta; template since 2026-11-19: names from port_android.h).
 *
 * The twin of a port's launcher/main.c (Windows / Linux): the same XrLauncherConfig. Android differences:
 *   - the root is the app's internal storage (no portable folder; maintainer 2026-11-11), the read-only tools
 *     folder <files>/tools (XR_LAUNCHER_TOOLS, set by loader.c);
 *   - --no-start: an app cannot start a second program as the game - the launcher builds PORT_GAME_LIB into the
 *     root and returns, loader.c dlopen()s it in this process. */
#include "launcher.h"
#include "port_android.h"
#include "port_icon.h"

#include <SDL3/SDL.h>
#include <jni.h>

/* "Choose image...": SDL's Android file dialog returns a content:// URI; the installer reads by path. Java
 * (PortActivity.importContent) copies the document into <files>/data/import/<its name> and returns that path
 * (a /proc/self/fd/N path of the picker's descriptor was refused: "permission denied", S23 2026-11-11);
 * loader.c deletes the copy after the launcher. */
static int port_open_uri(const char *uri, char *path, size_t n)
{
    JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    jobject act = (jobject)SDL_GetAndroidActivity();
    jclass c;
    jmethodID m;
    jstring ju, jr;
    const char *r;
    int ok = 0;
    path[0] = 0;
    if (!env || !act) return 0;
    c = (*env)->GetObjectClass(env, act);
    m = (*env)->GetStaticMethodID(env, c, "importContent", "(Ljava/lang/String;)Ljava/lang/String;");
    if (m) {
        ju = (*env)->NewStringUTF(env, uri);
        jr = (jstring)(*env)->CallStaticObjectMethod(env, c, m, ju);
        if (jr) {
            r = (*env)->GetStringUTFChars(env, jr, NULL);
            SDL_snprintf(path, n, "%s", r);
            ok = path[0] != 0;
            (*env)->ReleaseStringUTFChars(env, jr, r);
            (*env)->DeleteLocalRef(env, jr);
        }
        (*env)->DeleteLocalRef(env, ju);
    }
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, c);
    (*env)->DeleteLocalRef(env, act);
    return ok;
}

int port_android_launcher(int argc, char **argv)
{
    /* the supported versions (port_android.h: region + SHA-1 of the XBE) */
    static const xr_game_version versions[] = {
        PORT_VERSIONS
    };
    XrLauncherConfig c = {
        PORT_LAUNCHER_GAME,
        PORT_LAUNCHER_DISC,
        PORT_TITLE,
        PORT_GAME_LIB,
        PORT_TITLE_ID,
        versions,
        sizeof versions / sizeof versions[0],
        g_port_icon_bmp,
        sizeof g_port_icon_bmp,
        NULL,                           /* slug: no AppImage folder */
        port_open_uri,
    };
    char *av[8];
    int n = 0, i;
    av[n++] = argc > 0 ? argv[0] : "port";
    for (i = 1; i < argc && n < 6; i++)
        av[n++] = argv[i];
    av[n++] = "--no-start";
    av[n] = NULL;
    return xr_launcher_main(&c, n, av);
}
