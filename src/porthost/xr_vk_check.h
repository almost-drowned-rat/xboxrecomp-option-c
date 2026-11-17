/* xr_vk_check.h -- does this PC have a graphics device the Vulkan renderer can use? For the port's exe (before
 * anything else starts) and its launcher (before the setup window). Maintainer 2026-11-04: without a Vulkan 1.3
 * device both say so in a clear, polite message box instead of crashing or logging only (Vulkan 1.1 / 1.2 support
 * dropped the same evening). From Panzer Dragoon Orta's src/vk_check.h (Orta Recompiled), toolkit since
 * 2026-11-16. */
#ifndef XR_VK_CHECK_H
#define XR_VK_CHECK_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 1 when at least one device offers what the toolkit's renderer needs (nv2a_vk.c vk_device_ok: Vulkan 1.3 with
 * dynamic rendering, VK_KHR_push_descriptor, VK_KHR_swapchain, a graphics queue). found gets a short list of what
 * is there ("NVIDIA GeForce RTX 3070 (Vulkan 1.3.277)", "no Vulkan driver", ...). Loads the Vulkan loader at run
 * time; needs no Vulkan headers. android_lib: the APK's Vulkan library (its vkshim, e.g. "libvulkan_orta.so");
 * NULL = "libvulkan.so"; ignored elsewhere. RECOMP_VK_TEST_NO_DEVICE=1 (tests) pretends nothing fits. */
int xr_vk_check(char *found, size_t n, const char *android_lib);

/* The apology text for the message box (first person singular, public text), with the port's title
 * (host_set_port). */
void xr_vk_message(char *out, size_t n, const char *found);

#ifdef __cplusplus
}
#endif

#endif
