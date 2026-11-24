/* setup_ui_android.c - the toolkit launcher's setup UI (game_setup_ui.h: xr_setup_ui_*) on Android: the standard
 * Android setup screen in the Windows Vista look (maintainer 2026-11-23: ZOE's Android launcher is the design
 * standard of all Android ports). Nothing is drawn here: every call goes to PortActivity's static setup* methods
 * (JNI), which hand it to PortSetup.java / VistaSetupView.java on the UI thread. 2026-11-23 (Panzer Dragoon Orta; template since 2026-11-24); replaces
 * launcher/setup_ui_sdl.c (the Windows 95 style SDL dialog) on Android.
 *   xr_setup_ui_progress  the step text + fraction (the view shows itself on the first call)
 *   xr_setup_ui_cancelled "Quit anyway?" answered Yes
 *   xr_setup_ui_message   a box with OK (blocks until OK)
 *   xr_setup_ui_close     after a finished build: "... is ready" + Play (blocks), then the view goes
 *   port_setup_image_page the launcher's "which disc image?" page (xr_launcher_set_image_page): the form, blocks
 *                         until Install (the image copied into the app: its path) or Quit
 * All six functions of game_setup_ui.c are defined here, so the linker never pulls that archive member.
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "port_android.h"
#include "game_setup_ui.h"

#include <SDL3/SDL.h>
#include <android/log.h>
#include <jni.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct XrSetupUi {
    int shown;                          /* the view is up (a progress, a message or the page) */
    int failed;                         /* an error message since the last page: no "is ready" at the end */
    int cancelled;
};

/* PortActivity's static method `name` with signature `sig`; the env and class through SDL's activity */
static int jcall(const char *name, const char *sig, JNIEnv **penv, jclass *pc, jmethodID *pm)
{
    JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    jobject act = (jobject)SDL_GetAndroidActivity();
    jclass c;
    jmethodID m;
    if (!env || !act) return 0;
    c = (*env)->GetObjectClass(env, act);
    (*env)->DeleteLocalRef(env, act);
    m = (*env)->GetStaticMethodID(env, c, name, sig);
    if (!m) {
        if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
        (*env)->DeleteLocalRef(env, c);
        __android_log_print(ANDROID_LOG_ERROR, PORT_TAG, "[SETUP] PortActivity.%s%s missing", name, sig);
        return 0;
    }
    *penv = env;
    *pc = c;
    *pm = m;
    return 1;
}

static void jdone(JNIEnv *env, jclass c)
{
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, c);
}

XrSetupUi *xr_setup_ui_open(const char *title)
{
    (void)title;
    return (XrSetupUi *)calloc(1, sizeof(XrSetupUi));
}

void xr_setup_ui_progress(void *p, double fraction, const char *text)
{
    XrSetupUi *ui = (XrSetupUi *)p;
    static Uint64 last;
    JNIEnv *env;
    jclass c;
    jmethodID m;
    Uint64 now = SDL_GetTicks();
    jstring t;
    if (!ui) return;
    if (ui->shown && fraction < 1.0 && now - last < 100) return;     /* (the view animates by itself) */
    last = now;
    if (!jcall("setupProgress", "(Ljava/lang/String;F)V", &env, &c, &m)) return;
    t = (*env)->NewStringUTF(env, text ? text : "");
    (*env)->CallStaticVoidMethod(env, c, m, t, (jfloat)fraction);
    (*env)->DeleteLocalRef(env, t);
    jdone(env, c);
    ui->shown = 1;
}

int xr_setup_ui_cancelled(void *p)
{
    XrSetupUi *ui = (XrSetupUi *)p;
    JNIEnv *env;
    jclass c;
    jmethodID m;
    jboolean r;
    if (!ui) return 0;
    if (ui->cancelled) return 1;
    if (!ui->shown || !jcall("setupCancelled", "()Z", &env, &c, &m)) return 0;
    r = (*env)->CallStaticBooleanMethod(env, c, m);
    jdone(env, c);
    ui->cancelled = r ? 1 : 0;
    return ui->cancelled;
}

int xr_no_dialogs(void)
{
    const char *e = getenv("RECOMP_NO_DIALOGS");
    return e && *e == '1';
}

void xr_setup_ui_message(XrSetupUi *ui, int error, const char *text)
{
    JNIEnv *env;
    jclass c;
    jmethodID m;
    jstring t;
    __android_log_print(error ? ANDROID_LOG_ERROR : ANDROID_LOG_WARN, PORT_TAG, "[SETUP] message: %s", text ? text : "");
    if (ui && error) ui->failed = 1;
    /* the player quit ("Quit anyway?" Yes): the step that was stopped reports a failure - no box for it */
    if (ui && ui->cancelled) return;
    if (xr_no_dialogs() || !jcall("setupMessage", "(Ljava/lang/String;)V", &env, &c, &m)) return;
    t = (*env)->NewStringUTF(env, text ? text : "");
    (*env)->CallStaticVoidMethod(env, c, m, t);
    (*env)->DeleteLocalRef(env, t);
    jdone(env, c);
    if (ui) ui->shown = 1;
}

void xr_setup_ui_close(XrSetupUi *ui)
{
    JNIEnv *env;
    jclass c;
    jmethodID m;
    if (!ui) return;
    if (ui->shown && jcall("setupClose", "(Z)V", &env, &c, &m)) {
        (*env)->CallStaticVoidMethod(env, c, m, (jboolean)(!ui->failed && !ui->cancelled));
        jdone(env, c);
    }
    free(ui);
}

/* xr_launcher_set_image_page: 1 = iso set (install from it), 0 = quit */
int port_setup_image_page(void *p, const char *disc, char *iso, size_t n, const char *note)
{
    XrSetupUi *ui = (XrSetupUi *)p;
    JNIEnv *env;
    jclass c;
    jmethodID m;
    jstring r;
    int ok = 0;
    (void)disc; (void)note;
    iso[0] = 0;
    if (!jcall("setupImagePage", "()Ljava/lang/String;", &env, &c, &m)) return 0;
    if (ui) { ui->shown = 1; ui->failed = 0; }
    r = (jstring)(*env)->CallStaticObjectMethod(env, c, m);
    if (r) {
        const char *s = (*env)->GetStringUTFChars(env, r, NULL);
        if (s) {
            snprintf(iso, n, "%s", s);
            ok = iso[0] != 0;
            (*env)->ReleaseStringUTFChars(env, r, s);
        }
        (*env)->DeleteLocalRef(env, r);
    }
    jdone(env, c);
    __android_log_print(ANDROID_LOG_INFO, PORT_TAG, "[SETUP] disc page: %s", ok ? iso : "quit");
    return ok;
}
