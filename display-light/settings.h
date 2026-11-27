/* SPDX-License-Identifier: MIT */
/*
 * Settings of aurora-display-light: the file a desktop's settings page writes,
 * the built-in profiles, and the adjustments learned from the user.
 */
#ifndef DISPLAY_LIGHT_SETTINGS_H
#define DISPLAY_LIGHT_SETTINGS_H

#include <stdbool.h>
#include <stddef.h>

#include "model.h"

#define DL_CONFIG_NAME "aurora-display-lightrc"

struct dl_profile {
	const char *name;
	double nits[DL_POINTS];
};

struct dl_response_preset {
	const char *name;
	struct dl_response response;
};

extern const struct dl_profile dl_profiles[];		/* ends with a NULL name */
extern const struct dl_response_preset dl_responses[];	/* ends with a NULL name */

struct dl_settings {
	bool enabled;
	bool learn;
	char profile[32];	/* a dl_profiles name, or "custom" */
	char response[32];	/* a dl_responses name */
	double custom[DL_POINTS];
	double min_nits;
	double deadband;
	char backlight[64];	/* a /sys/class/backlight name, or empty to find the panel's */
};

void dl_settings_defaults(struct dl_settings *s);

/* Read an INI file over s; a missing file leaves s as it is. Returns 0 or -errno. */
int dl_settings_read(struct dl_settings *s, const char *path);

/* Fill p from s for a panel whose brightest level is max_nits. */
void dl_settings_params(const struct dl_settings *s, double max_nits, struct dl_params *p);

/* The user's settings file and learned-adjustments file, from the XDG directories. */
int dl_config_path(char *buf, size_t len);
int dl_state_path(char *buf, size_t len);

/* Offsets learned for a profile (zero when none are saved). */
void dl_learned_load(const char *path, const char *profile, double offset[DL_POINTS]);
int dl_learned_save(const char *path, const char *profile, const double offset[DL_POINTS]);

#endif
