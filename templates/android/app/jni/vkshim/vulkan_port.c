/* SPDX-License-Identifier: MIT
 * vulkan_port.c - libvulkan_port.so: the game's Vulkan library on Android (2026-11-11, for Panzer Dragoon Orta as
 * vulkan_orta.c / libvulkan_orta.so; template since 2026-11-19: the switch is XR_VK_DRIVER, the query xr_vk_driver()).
 *
 * The game library links this instead of the system's libvulkan.so (build.txt, the port's build script), and SDL
 * loads it too (SDL_HINT_VULKAN_LIBRARY, loader.c), so every Vulkan call of the game goes through ONE loader:
 *   - the system's libvulkan.so when its GPU offers Vulkan 1.3 (the S23, the Odin 2 Portal: their own driver);
 *   - otherwise, on an Adreno GPU, the system loader again but opened through libadrenotools (bylaws, BSD-2) with
 *     a Turnip driver (Mesa, MIT) the PLAYER chose (an adrenotools .adpkg.zip, e.g. MrPurple666's purple-turnip;
 *     loader.c asks for it beside the ISO, the BACK menu changes it - maintainer 2026-11-11: chosen, not shipped).
 *     Adreno devices whose maker ships only Vulkan 1.1 (the Galaxy Tab S8: Turnip gives Vulkan 1.4) run with it.
 * XR_VK_DRIVER=system|turnip forces one (tests). Each exported entry point is a jump through g_vk_slots[]
 * (vk_trampolines.S, generated from the NDK's API-29 libvulkan.so export list), filled at the first Vulkan call. Paths: XR_NATIVE_DIR (the adrenotools hook libraries), XR_HOST_ROOT (the
 * app's files: turnip/ = the driver, data/tmp/adreno/ = adrenotools' scratch), both set by loader.c. */
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <android/log.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <adrenotools/driver.h>
#include <android/dlext.h>
#include "android_linker_ns.h"            /* libadrenotools' linkernsbypass: the diagnosis in open_turnip */

#include "vk_names.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "XRVK", __VA_ARGS__)

__attribute__((visibility("hidden"))) void *g_vk_slots[VK_SHIM_COUNT];   /* read by vk_trampolines.S (PC-relative) */
static char g_driver[160] = "none";

static void missing(void)
{
    __android_log_print(ANDROID_LOG_FATAL, "XRVK", "[VKSHIM] a Vulkan entry point the loader does not export was called");
    abort();
}

/* the best device's API version and name through this loader (0: no instance / no device) */
static uint32_t probe(void *lib, char *name, size_t n)
{
    PFN_vkGetInstanceProcAddr gipa = (PFN_vkGetInstanceProcAddr)dlsym(lib, "vkGetInstanceProcAddr");
    PFN_vkCreateInstance ci;
    PFN_vkEnumeratePhysicalDevices ed;
    PFN_vkGetPhysicalDeviceProperties gp;
    PFN_vkDestroyInstance di;
    VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO, NULL, "vk probe", 1, NULL, 0, VK_API_VERSION_1_1 };
    VkInstanceCreateInfo info = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, NULL, 0, &app, 0, NULL, 0, NULL };
    VkInstance inst = VK_NULL_HANDLE;
    VkPhysicalDevice dev[8];
    uint32_t nd = 8, i, best = 0;
    name[0] = 0;
    if (!gipa) return 0;
    ci = (PFN_vkCreateInstance)gipa(NULL, "vkCreateInstance");
    if (!ci || ci(&info, NULL, &inst) != VK_SUCCESS) return 0;
    ed = (PFN_vkEnumeratePhysicalDevices)gipa(inst, "vkEnumeratePhysicalDevices");
    gp = (PFN_vkGetPhysicalDeviceProperties)gipa(inst, "vkGetPhysicalDeviceProperties");
    di = (PFN_vkDestroyInstance)gipa(inst, "vkDestroyInstance");
    if (ed && gp && ed(inst, &nd, dev) >= 0) {
        for (i = 0; i < nd && i < 8; i++) {
            VkPhysicalDeviceProperties p;
            gp(dev[i], &p);
            if (p.apiVersion > best) {
                best = p.apiVersion;
                snprintf(name, n, "%s", p.deviceName);
            }
        }
    }
    if (di) di(inst, NULL);
    return best;
}

static void *open_turnip(void)
{
    const char *native = getenv("XR_NATIVE_DIR"), *root = getenv("XR_HOST_ROOT");
    char hook[1024], drv[1024], tmp[1024], so[1400], lib[256];
    struct stat st;
    void *h;
    if (!native || !root) return NULL;
    snprintf(hook, sizeof hook, "%s/", native);
    snprintf(drv, sizeof drv, "%s/turnip/", root);
    {   /* the player's driver package (PortActivity.importDriver): driver.txt line 1 = the library's file name */
        char txt[1100];
        FILE *f;
        snprintf(txt, sizeof txt, "%sdriver.txt", drv);
        if (!(f = fopen(txt, "r")) || !fgets(lib, sizeof lib, f)) {
            if (f) fclose(f);
            LOGI("[VKSHIM] no Turnip driver chosen (%s)", txt);
            return NULL;
        }
        fclose(f);
        lib[strcspn(lib, "\r\n")] = 0;
    }
    snprintf(so, sizeof so, "%s%s", drv, lib);
    if (stat(so, &st) != 0) {
        LOGI("[VKSHIM] the chosen Turnip driver is missing: %s", so);
        return NULL;
    }
    snprintf(tmp, sizeof tmp, "%s/data/tmp", root);
    mkdir(tmp, 0700);
    snprintf(tmp, sizeof tmp, "%s/data/tmp/adreno/", root);
    mkdir(tmp, 0700);
    h = adrenotools_open_libvulkan(RTLD_NOW, ADRENOTOOLS_DRIVER_CUSTOM, tmp, hook, drv, lib, NULL, NULL);
    if (!h) {
        /* adrenotools gives no reason: the same steps again, each logged (Tab S8 2026-11-11: silent failure) */
        struct android_namespace_t *ns;
        void *impl;
        const char *e;
        LOGI("[VKSHIM] adrenotools could not open the Turnip driver; linkernsbypass loaded: %d",
             (int)linkernsbypass_load_status());
        ns = android_create_namespace("xr-vk-diag", hook, NULL, ANDROID_NAMESPACE_TYPE_SHARED, NULL, NULL);
        LOGI("[VKSHIM]   namespace on %s: %p", hook, (void *)ns);
        if (ns) {
            LOGI("[VKSHIM]   link to default: %d", (int)linkernsbypass_link_namespace_to_default_all_libs(ns));
            impl = linkernsbypass_namespace_dlopen("libhook_impl.so", RTLD_NOW, ns);
            e = dlerror();
            LOGI("[VKSHIM]   libhook_impl.so: %p %s", impl, e ? e : "");
        }
    }
    return h;
}

static void vk_shim_init(void);
static pthread_once_t g_once = PTHREAD_ONCE_INIT;

/* the first Vulkan call (vk_trampolines.S, empty slot) or xr_vk_driver(); lazy because a load-time constructor here
   ran before libadrenotools' linkernsbypass had resolved the linker's namespace functions (Tab S8 2026-11-11) */
__attribute__((visibility("hidden"))) void vk_shim_init_once(void) { pthread_once(&g_once, vk_shim_init); }

static void vk_shim_init(void)
{
    const char *force = getenv("XR_VK_DRIVER");
    void *sys = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL), *use = sys, *tu = NULL;
    char name[256] = "", tname[256] = "";
    uint32_t api = 0, tapi = 0, i;
    if (!(force && !strcmp(force, "turnip"))) {
        api = sys ? probe(sys, name, sizeof name) : 0;
        LOGI("[VKSHIM] system driver: %s (Vulkan %u.%u.%u)", name[0] ? name : "no device", VK_API_VERSION_MAJOR(api),
             VK_API_VERSION_MINOR(api), VK_API_VERSION_PATCH(api));
    }
    /* a driver the player chose (driver.txt) is used whenever it works - also on a device whose own driver has 1.3
       (maintainer 2026-11-11: "praktisch fuer Geraete die bereits Vulkan 1.3 haben, aber dennoch Turnip wollen") */
    if (!(force && !strcmp(force, "system"))) {
        tu = open_turnip();
        if (tu) {
            tapi = probe(tu, tname, sizeof tname);
            LOGI("[VKSHIM] Turnip: %s (Vulkan %u.%u.%u)", tname[0] ? tname : "no device", VK_API_VERSION_MAJOR(tapi),
                 VK_API_VERSION_MINOR(tapi), VK_API_VERSION_PATCH(tapi));
            if (tapi >= VK_API_VERSION_1_3 || tapi > api) use = tu;
        }
    }
    snprintf(g_driver, sizeof g_driver, "%s", use == tu && tu ? "turnip" : "system");
    LOGI("[VKSHIM] the game uses the %s driver", g_driver);
    for (i = 0; i < VK_SHIM_COUNT; i++) {
        void *f = use ? dlsym(use, g_vk_names[i]) : NULL;
        g_vk_slots[i] = f ? f : (void *)missing;
    }
}

/* for logs / vk_check: "system" or "turnip" */
__attribute__((visibility("default"))) const char *xr_vk_driver(void) { vk_shim_init_once(); return g_driver; }
