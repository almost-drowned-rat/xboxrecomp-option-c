/* SPDX-License-Identifier: MIT
 * touchpad.h - the touch controls as one virtual SDL gamepad (touchpad.c). */
#ifndef XR_ANDROID_TOUCHPAD_H
#define XR_ANDROID_TOUCHPAD_H

/* after the launcher, before the game: the gamepad subsystem up + the virtual pad attached */
void touchpad_init(void);

/* external pads 10, built-in 5, the touch pad 0 - for the toolkit's xbox_InputSetPadPriority */
int touchpad_pad_priority(const char *name);

/* the port's optional touch-aim entry (port_android.h PORT_TOUCH_AIM; NULL: none) */
void touchpad_set_aim(int (*fn)(int, float, float, float, float));

#endif
