/* SPDX-License-Identifier: MIT
 * vkshim_test.c - libvkshim_test_exe.so: loads libvulkan_port.so outside the game and reports which driver it chose
 * and the device's Vulkan version (2026-11-11; dev aid for the Turnip fallback, run with adb run-as:
 *   XR_NATIVE_DIR=<nativeLibraryDir> XR_HOST_ROOT=<files> LD_LIBRARY_PATH=<nativeLibraryDir> ./libvkshim_test_exe.so
 * [XR_VK_DRIVER=turnip]). The shim's own [VKSHIM] lines go to logcat (tag XRVK). */
#include <dlfcn.h>
#include <stdio.h>

int main(void)
{
    void *h = dlopen("libvulkan_port.so", RTLD_NOW);
    const char *(*drv)(void);
    if (!h) {
        printf("dlopen libvulkan_port.so: %s\n", dlerror());
        return 1;
    }
    drv = (const char *(*)(void))dlsym(h, "xr_vk_driver");
    printf("driver: %s\n", drv ? drv() : "?");
    return 0;
}
