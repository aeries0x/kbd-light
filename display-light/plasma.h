/* SPDX-License-Identifier: MIT */
#pragma once

#include <stdbool.h>
#include <stddef.h>

/* KWin's output settings, in the user's config directory. */
#define DL_KWIN_OUTPUTS_NAME "kwinoutputconfig.json"

/*
 * Plasma has an automatic brightness of its own (Display Configuration,
 * "Automatic brightness"), kept per screen in KWin's output settings. When it
 * is on for the built-in panel, KWin follows the light itself and the service
 * leaves the panel to it: two of them would fight.
 */

/* Whether KWin's output settings (the file's text) turn it on for a built-in panel. */
bool dl_plasma_auto_brightness_in(const char *json);

/* The same for the file at path; false when it can't be read. */
bool dl_plasma_auto_brightness(const char *path);

/* Path of KWin's output settings; returns 0, or -1 when there is no config directory. */
int dl_plasma_outputs_path(char *buf, size_t len);
