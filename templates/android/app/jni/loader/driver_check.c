/* SPDX-License-Identifier: MIT
 * driver_check.c - before the first-start build: does this device need a Turnip driver? (2026-11-11)
 *
 * The game needs Vulkan 1.3. When the system driver offers less on an Adreno GPU (the Galaxy Tab S8: Vulkan 1.1),
 * the player chooses a Turnip driver package beside the ISO (maintainer 2026-11-11: chosen like the ISO, not
 * shipped): an adrenotools .adpkg.zip, imported by PortActivity.importDriver into <files>/turnip; the game's Vulkan
 * library (vkshim/vulkan_port.c) then loads it. Asked BEFORE the build, so nobody waits minutes for a game that
 * cannot start. Other GPUs below 1.3 go on to the game's own apology box (the toolkit's xr_vk_check). */
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <SDL3/SDL.h>
#include <android/log.h>
#include <dlfcn.h>
#include <jni.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "driver_check.h"
#include "port_android.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, PORT_TAG, __VA_ARGS__)

/* the system driver's best device: API version + name (0: none) */
static uint32_t system_vulkan(char *name, size_t n)
{
    void *lib = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    PFN_vkGetInstanceProcAddr gipa = lib ? (PFN_vkGetInstanceProcAddr)dlsym(lib, "vkGetInstanceProcAddr") : NULL;
    VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO, NULL, "driver check", 1, NULL, 0, VK_API_VERSION_1_1 };
    VkInstanceCreateInfo info = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, NULL, 0, &app, 0, NULL, 0, NULL };
    VkInstance inst = VK_NULL_HANDLE;
    VkPhysicalDevice dev[8];
    uint32_t nd = 8, i, best = 0;
    PFN_vkCreateInstance ci = gipa ? (PFN_vkCreateInstance)gipa(NULL, "vkCreateInstance") : NULL;
    name[0] = 0;
    if (ci && ci(&info, NULL, &inst) == VK_SUCCESS) {
        PFN_vkEnumeratePhysicalDevices ed = (PFN_vkEnumeratePhysicalDevices)gipa(inst, "vkEnumeratePhysicalDevices");
        PFN_vkGetPhysicalDeviceProperties gp = (PFN_vkGetPhysicalDeviceProperties)gipa(inst, "vkGetPhysicalDeviceProperties");
        PFN_vkDestroyInstance di = (PFN_vkDestroyInstance)gipa(inst, "vkDestroyInstance");
        if (ed && gp && ed(inst, &nd, dev) >= 0)
            for (i = 0; i < nd && i < 8; i++) {
                VkPhysicalDeviceProperties p;
                gp(dev[i], &p);
                if (p.apiVersion > best) { best = p.apiVersion; SDL_snprintf(name, n, "%s", p.deviceName); }
            }
        if (di) di(inst, NULL);
    }
    if (lib) dlclose(lib);
    return best;
}

/* PortActivity.importDriver(uri) through JNI: "" = installed, else the sentence for the player */
static void import_driver(const char *uri, char *err, size_t n)
{
    JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    jobject act = (jobject)SDL_GetAndroidActivity();
    jclass c;
    jmethodID m;
    SDL_snprintf(err, n, "The driver could not be installed.");
    if (!env || !act) return;
    c = (*env)->GetObjectClass(env, act);
    m = (*env)->GetStaticMethodID(env, c, "importDriver", "(Ljava/lang/String;)Ljava/lang/String;");
    if (m) {
        jstring ju = (*env)->NewStringUTF(env, uri);
        jstring jr = (jstring)(*env)->CallStaticObjectMethod(env, c, m, ju);
        if (jr) {
            const char *r = (*env)->GetStringUTFChars(env, jr, NULL);
            SDL_snprintf(err, n, "%s", r);
            (*env)->ReleaseStringUTFChars(env, jr, r);
            (*env)->DeleteLocalRef(env, jr);
        }
        (*env)->DeleteLocalRef(env, ju);
    }
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, c);
    (*env)->DeleteLocalRef(env, act);
}

typedef struct { SDL_AtomicInt done; char uri[2048]; } Pick;

static void SDLCALL pick_cb(void *u, const char *const *files, int filter)
{
    Pick *p = (Pick *)u;
    (void)filter;
    if (files && files[0]) SDL_snprintf(p->uri, sizeof p->uri, "%s", files[0]);
    SDL_SetAtomicInt(&p->done, 1);
}

int driver_check(const char *files)
{
    char name[256], drvtxt[1024], msg[1200], err[512];
    struct stat st;
    uint32_t api = system_vulkan(name, sizeof name);
    int ok = 1;
    SDL_snprintf(drvtxt, sizeof drvtxt, "%s/turnip/driver.txt", files);
    LOGI("[DRIVER] system: %s (Vulkan %u.%u.%u), Turnip chosen: %s", name[0] ? name : "no device",
         VK_API_VERSION_MAJOR(api), VK_API_VERSION_MINOR(api), VK_API_VERSION_PATCH(api),
         stat(drvtxt, &st) == 0 ? "yes" : "no");
    if (api >= VK_API_VERSION_1_3 || !strstr(name, "Adreno") || stat(drvtxt, &st) == 0)
        return 1;
    SDL_Init(SDL_INIT_VIDEO);
    err[0] = 0;
    for (;;) {
        static const SDL_MessageBoxButtonData b[] = {
            { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Choose driver..." },
            { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Quit" },
        };
        SDL_MessageBoxData d;
        int id = 0;
        Pick p;
        SDL_snprintf(msg, sizeof msg,
                     "%s%sThis device's graphics driver (%s) offers only Vulkan %u.%u - " PORT_TITLE " needs "
                     "Vulkan 1.3.\n\nThe free Turnip driver for Adreno GPUs makes it run. Please choose a Turnip driver "
                     "package (.adpkg.zip), for example from github.com/MrPurple666/purple-turnip/releases.\n\n"
                     "You can change the driver later in the BACK menu.",
                     err, err[0] ? "\n\n" : "", name, VK_API_VERSION_MAJOR(api), VK_API_VERSION_MINOR(api));
        SDL_zero(d);
        d.flags = SDL_MESSAGEBOX_INFORMATION;
        d.title = "Graphics driver needed";
        d.message = msg;
        d.numbuttons = 2;
        d.buttons = b;
        if (!SDL_ShowMessageBox(&d, &id) || id != 1) { ok = 0; break; }
        SDL_zero(p);
        SDL_ShowOpenFileDialog(pick_cb, &p, NULL, NULL, 0, NULL, false);
        while (!SDL_GetAtomicInt(&p.done)) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) { }
            SDL_Delay(20);
        }
        if (!p.uri[0]) { err[0] = 0; continue; }
        import_driver(p.uri, err, sizeof err);
        if (!err[0]) { LOGI("[DRIVER] Turnip chosen: %s", p.uri); break; }
        LOGI("[DRIVER] import failed: %s", err);
    }
    SDL_Quit();
    return ok;
}
