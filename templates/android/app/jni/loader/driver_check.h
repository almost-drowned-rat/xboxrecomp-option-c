/* SPDX-License-Identifier: MIT
 * driver_check.h - before the first-start build: a Turnip driver for Adreno GPUs below Vulkan 1.3 (driver_check.c). */
#ifndef XR_ANDROID_DRIVER_CHECK_H
#define XR_ANDROID_DRIVER_CHECK_H

/* 1 = go on (the device is fine, or a driver was chosen); 0 = the player quit */
int driver_check(const char *files);

#endif
