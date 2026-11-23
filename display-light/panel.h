/* SPDX-License-Identifier: MIT */
/*
 * The built-in panel's brightness, set the way the desktop expects.
 *
 * - Plasma: through PowerDevil's org.kde.ScreenBrightness on the session bus.
 *   The compositor keeps owning the backlight: its idle dimming and its own
 *   adjustments apply on top of the level set here, and changes made with the
 *   brightness keys or the desktop's slider arrive as BrightnessChanged
 *   signals that can be told apart from this program's own.
 * - Elsewhere: through logind's SetBrightness for the user's session, or the
 *   sysfs file if logind refuses and the file is writable.
 *
 * Levels are in nits: the Apple DCP driver registers the built-in panel's
 * backlight with its brightness in nits, from 1 (the lowest level above off)
 * to max_brightness.
 */
#ifndef DISPLAY_LIGHT_PANEL_H
#define DISPLAY_LIGHT_PANEL_H

#include <stdbool.h>
#include <systemd/sd-bus.h>

enum panel_backend {
	PANEL_KDE,
	PANEL_LOGIND,
};

struct panel {
	enum panel_backend backend;
	sd_bus *user, *system;
	char sysfs[256];	/* /sys/class/backlight/<name> */
	const char *name;	/* points into sysfs */
	double min_nits, max_nits;

	/* Plasma */
	char kde_display[128];	/* D-Bus name of the internal display, empty until found */
	int kde_max;
	int kde_value;
	sd_bus_slot *kde_slot;
	double kde_retry_t;
	/* levels sent recently: a change without a source that matches one is an echo of ours */
	struct { int value; double t; } kde_sent[64];
	int kde_sent_next;
	double now;

	/* logind */
	char session[256];	/* object path of the user's graphical session */
	long written;		/* level last written, -1 if none */

	/* a change made by someone else, not yet taken */
	bool foreign;
	bool foreign_person;	/* known to be a person's: the keys, a slider or a program */
	char foreign_by[96];	/* who, for the log: the client and its context */
	double foreign_t;
	double foreign_nits;
};

/*
 * Find the built-in panel's backlight (name, or the first one whose parent is
 * an Apple DCP) and connect to the desktop. Returns 0, or -ENODEV when there
 * is no such panel.
 */
int panel_open(struct panel *p, const char *name, bool kde);

/* Whether PowerDevil's brightness interface is on the session bus. */
bool panel_kde_running(void);
void panel_close(struct panel *p);

/* Whether the backend can set the level now (Plasma: PowerDevil is up and the display found). */
bool panel_ready(struct panel *p, double t);

/* The fd to poll for bus traffic, and the events to wait for. */
int panel_fd(struct panel *p, short *events);
void panel_process(struct panel *p, double t);

/* The level the desktop has set, in nits; < 0 if unknown. */
double panel_level(struct panel *p);

/* What the backlight is at right now, read from sysfs (for traces). */
double panel_sysfs_level(struct panel *p);

int panel_set(struct panel *p, double nits);

/* The lid is closed, or every connected display is off. */
bool panel_dark(struct panel *p);

#endif
