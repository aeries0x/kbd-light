// SPDX-License-Identifier: MIT
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <glob.h>
#include <math.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "panel.h"

#define KDE_SERVICE "org.kde.ScreenBrightness"
#define KDE_PATH "/org/kde/ScreenBrightness"
#define KDE_IFACE "org.kde.ScreenBrightness"
#define KDE_DISPLAY_IFACE "org.kde.ScreenBrightness.Display"
/* Passed with every change, and handed back in the BrightnessChanged signal. */
#define KDE_CONTEXT "aurora-display-light"
#define KDE_SUPPRESS_INDICATOR 0x1
/* PowerDevil's own changes: the level a power profile (AC, battery) sets. */
#define KDE_OWN_CLIENT "(internal)"
#define KDE_PROFILE_CONTEXT "profile_brightness"
#define KDE_RETRY_S 2.0

#define LOGIN_SERVICE "org.freedesktop.login1"

static int read_text(const char *path, char *buf, size_t len)
{
	int fd = open(path, O_RDONLY | O_CLOEXEC);
	ssize_t n;

	if (fd < 0)
		return -errno;
	n = read(fd, buf, len - 1);
	close(fd);
	if (n < 0)
		return -errno;
	buf[n] = '\0';
	return (int)n;
}

static long read_long(const char *dir, const char *file)
{
	char path[512], buf[32], *end;
	long v;

	snprintf(path, sizeof(path), "%s/%s", dir, file);
	if (read_text(path, buf, sizeof(buf)) <= 0)
		return -1;
	v = strtol(buf, &end, 10);
	return end == buf ? -1 : v;
}

/*
 * Whether a backlight's parent is an Apple DCP: a device tree compatible of
 * "apple,dcp" or "apple,<soc>-dcp". The latter is all some SoCs list.
 */
static bool apple_dcp_panel(const char *dir)
{
	char path[512], buf[256];
	int n;

	snprintf(path, sizeof(path), "%s/device/of_node/compatible", dir);
	n = read_text(path, buf, sizeof(buf));
	for (int i = 0; i < n; i += strlen(buf + i) + 1)
		if (!strcmp(buf + i, "apple,dcp") || !fnmatch("apple,*-dcp", buf + i, 0))
			return true;
	return false;
}

static int find_backlight(struct panel *p, const char *name)
{
	glob_t g;
	int ret = -ENODEV;

	if (name && *name) {
		snprintf(p->sysfs, sizeof(p->sysfs), "/sys/class/backlight/%s", name);
		ret = access(p->sysfs, R_OK) ? -ENODEV : 0;
	} else if (glob("/sys/class/backlight/*", 0, NULL, &g) == 0) {
		for (size_t i = 0; i < g.gl_pathc && ret; i++) {
			char type[32];
			char path[512];

			snprintf(path, sizeof(path), "%s/type", g.gl_pathv[i]);
			if (read_text(path, type, sizeof(type)) > 0 && !strncmp(type, "platform", 8) &&
			    apple_dcp_panel(g.gl_pathv[i])) {
				snprintf(p->sysfs, sizeof(p->sysfs), "%s", g.gl_pathv[i]);
				ret = 0;
			}
		}
		globfree(&g);
	}
	if (ret)
		return ret;
	p->name = strrchr(p->sysfs, '/') + 1;
	p->max_nits = read_long(p->sysfs, "max_brightness");
	p->min_nits = 1;
	return p->max_nits > p->min_nits ? 0 : -ENODEV;
}

/* Plasma: levels on the bus run from 0 (the lowest still visible) to kde_max. */
static double kde_to_nits(const struct panel *p, int v)
{
	return p->min_nits + (p->max_nits - p->min_nits) * v / p->kde_max;
}

static int nits_to_kde(const struct panel *p, double nits)
{
	return (int)lround((nits - p->min_nits) / (p->max_nits - p->min_nits) * p->kde_max);
}

#define ECHO_S 2.0

/*
 * PowerDevil reports a level it reads back from the compositor as a change
 * without a source. While a transition runs, that can be a level this program
 * sent a moment ago.
 */
static bool kde_echo(const struct panel *p, int value)
{
	int slack = p->kde_max / 1000 + 1;

	for (size_t i = 0; i < sizeof(p->kde_sent) / sizeof(p->kde_sent[0]); i++)
		if (p->kde_sent[i].t > 0 && p->now - p->kde_sent[i].t <= ECHO_S &&
		    abs(p->kde_sent[i].value - value) <= slack)
			return true;
	return false;
}

static int on_brightness_changed(sd_bus_message *m, void *userdata, sd_bus_error *err)
{
	struct panel *p = userdata;
	const char *display, *client, *context;
	int value;

	(void)err;
	if (sd_bus_message_read(m, "siss", &display, &value, &client, &context) < 0 ||
	    strcmp(display, p->kde_display))
		return 0;
	p->kde_value = value;
	if (strcmp(context, KDE_CONTEXT) && !(!*client && kde_echo(p, value))) {
		/* a burst of changes counts as automatic only if every one of them was */
		bool automatic = !strcmp(client, KDE_OWN_CLIENT) || !strcmp(context, KDE_PROFILE_CONTEXT);

		p->foreign_automatic = p->foreign ? p->foreign_automatic && automatic : automatic;
		p->foreign = true;
		p->foreign_nits = kde_to_nits(p, value);
		/* the caller stamps the time: signals carry none */
		p->foreign_t = -1;
	}
	return 0;
}

static int on_displays_changed(sd_bus_message *m, void *userdata, sd_bus_error *err)
{
	struct panel *p = userdata;

	(void)m;
	(void)err;
	p->kde_display[0] = '\0';	/* look again */
	p->kde_retry_t = 0;
	return 0;
}

static bool kde_find_display(struct panel *p)
{
	sd_bus_error err = SD_BUS_ERROR_NULL;
	char **names = NULL;
	bool found = false;

	if (sd_bus_get_property_strv(p->user, KDE_SERVICE, KDE_PATH, KDE_IFACE, "DisplaysDBusNames",
				     &err, &names) < 0) {
		sd_bus_error_free(&err);
		return false;
	}
	for (char **n = names; n && *n; n++) {
		char path[256];
		int internal = 0, max = 0, value = 0;

		snprintf(path, sizeof(path), KDE_PATH "/%s", *n);
		if (!found &&
		    sd_bus_get_property_trivial(p->user, KDE_SERVICE, path, KDE_DISPLAY_IFACE,
						"IsInternal", &err, 'b', &internal) >= 0 &&
		    internal &&
		    sd_bus_get_property_trivial(p->user, KDE_SERVICE, path, KDE_DISPLAY_IFACE,
						"MaxBrightness", &err, 'i', &max) >= 0 &&
		    max > 0 &&
		    sd_bus_get_property_trivial(p->user, KDE_SERVICE, path, KDE_DISPLAY_IFACE,
						"Brightness", &err, 'i', &value) >= 0) {
			snprintf(p->kde_display, sizeof(p->kde_display), "%s", *n);
			p->kde_max = max;
			p->kde_value = value;
			found = true;
		}
		sd_bus_error_free(&err);
		free(*n);
	}
	free(names);
	return found;
}

static int kde_open(struct panel *p)
{
	int ret = sd_bus_open_user(&p->user);

	if (ret < 0)
		return ret;
	ret = sd_bus_match_signal(p->user, &p->kde_slot, KDE_SERVICE, KDE_PATH, KDE_IFACE,
				  "BrightnessChanged", on_brightness_changed, p);
	if (ret >= 0)
		ret = sd_bus_match_signal(p->user, NULL, KDE_SERVICE, KDE_PATH, KDE_IFACE,
					  "DisplayAdded", on_displays_changed, p);
	if (ret >= 0)
		ret = sd_bus_match_signal(p->user, NULL, KDE_SERVICE, KDE_PATH, KDE_IFACE,
					  "DisplayRemoved", on_displays_changed, p);
	/* PowerDevil restarting */
	if (ret >= 0)
		ret = sd_bus_add_match(p->user, NULL,
				       "type='signal',sender='org.freedesktop.DBus',"
				       "interface='org.freedesktop.DBus',member='NameOwnerChanged',"
				       "arg0='" KDE_SERVICE "'",
				       on_displays_changed, p);
	return ret < 0 ? ret : 0;
}

static void logind_find_session(struct panel *p)
{
	sd_bus_error err = SD_BUS_ERROR_NULL;
	sd_bus_message *reply = NULL;
	const char *id, *path;

	if (sd_bus_get_property(p->system, LOGIN_SERVICE, "/org/freedesktop/login1/user/self",
				"org.freedesktop.login1.User", "Display", &err, &reply, "(so)") >= 0 &&
	    sd_bus_message_read(reply, "(so)", &id, &path) >= 0 && strcmp(path, "/"))
		snprintf(p->session, sizeof(p->session), "%s", path);
	sd_bus_message_unref(reply);
	sd_bus_error_free(&err);
}

bool panel_kde_running(void)
{
	sd_bus_error err = SD_BUS_ERROR_NULL;
	sd_bus_message *reply = NULL;
	sd_bus *bus = NULL;
	int has = 0;

	if (sd_bus_open_user(&bus) >= 0 &&
	    sd_bus_call_method(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus",
			       "org.freedesktop.DBus", "NameHasOwner", &err, &reply, "s", KDE_SERVICE) >= 0)
		sd_bus_message_read(reply, "b", &has);
	sd_bus_message_unref(reply);
	sd_bus_error_free(&err);
	sd_bus_flush_close_unref(bus);
	return has;
}

int panel_open(struct panel *p, const char *name, bool kde)
{
	int ret;

	memset(p, 0, sizeof(*p));
	p->written = -1;
	ret = find_backlight(p, name);
	if (ret)
		return ret;
	/* the lid, and the logind backend */
	if (sd_bus_open_system(&p->system) < 0)
		p->system = NULL;
	p->backend = kde ? PANEL_KDE : PANEL_LOGIND;
	if (kde)
		return kde_open(p);
	if (p->system)
		logind_find_session(p);
	return 0;
}

void panel_close(struct panel *p)
{
	sd_bus_slot_unref(p->kde_slot);
	sd_bus_flush_close_unref(p->user);
	sd_bus_flush_close_unref(p->system);
	p->user = p->system = NULL;
	p->kde_slot = NULL;
}

bool panel_ready(struct panel *p, double t)
{
	if (p->backend != PANEL_KDE)
		return true;
	if (p->kde_display[0])
		return true;
	if (t - p->kde_retry_t < KDE_RETRY_S)
		return false;
	p->kde_retry_t = t;
	return kde_find_display(p);
}

int panel_fd(struct panel *p, short *events)
{
	int e;

	if (p->backend != PANEL_KDE || !p->user)
		return -1;
	e = sd_bus_get_events(p->user);
	*events = e > 0 ? (short)e : POLLIN;
	return sd_bus_get_fd(p->user);
}

void panel_process(struct panel *p, double t)
{
	p->now = t;
	if (p->backend == PANEL_KDE) {
		while (p->user && sd_bus_process(p->user, NULL) > 0)
			;
	} else {
		long cur = read_long(p->sysfs, "brightness");

		if (p->written >= 0 && cur >= 0 && cur != p->written) {
			p->foreign_automatic = false;
			p->foreign = true;
			p->foreign_nits = cur;
			p->foreign_t = -1;
			p->written = cur;
		}
	}
	if (p->foreign && p->foreign_t < 0)
		p->foreign_t = t;
}

double panel_level(struct panel *p)
{
	if (p->backend == PANEL_KDE)
		return p->kde_display[0] ? kde_to_nits(p, p->kde_value) : -1;
	return read_long(p->sysfs, "brightness");
}

double panel_sysfs_level(struct panel *p)
{
	long v = read_long(p->sysfs, "actual_brightness");

	return v >= 0 ? v : read_long(p->sysfs, "brightness");
}

static int kde_set(struct panel *p, double nits)
{
	sd_bus_message *m = NULL;
	char path[256];
	int v = nits_to_kde(p, nits), ret;

	if (v == p->kde_value)
		return 0;
	snprintf(path, sizeof(path), KDE_PATH "/%s", p->kde_display);
	ret = sd_bus_message_new_method_call(p->user, &m, KDE_SERVICE, path, KDE_DISPLAY_IFACE,
					     "SetBrightnessWithContext");
	if (ret >= 0)
		ret = sd_bus_message_append(m, "ius", v, (uint32_t)KDE_SUPPRESS_INDICATOR, KDE_CONTEXT);
	/* fire and forget: a transition sends one of these per frame */
	if (ret >= 0)
		ret = sd_bus_message_set_expect_reply(m, 0);
	if (ret >= 0)
		ret = sd_bus_send(p->user, m, NULL);
	sd_bus_message_unref(m);
	if (ret >= 0) {
		p->kde_value = v;
		p->kde_sent[p->kde_sent_next].value = v;
		p->kde_sent[p->kde_sent_next].t = p->now;
		p->kde_sent_next = (p->kde_sent_next + 1) % (int)(sizeof(p->kde_sent) / sizeof(p->kde_sent[0]));
	}
	return ret < 0 ? ret : 0;
}

static int sysfs_write(struct panel *p, long v)
{
	char path[512], buf[32];
	int fd, len, ret = 0;

	snprintf(path, sizeof(path), "%s/brightness", p->sysfs);
	fd = open(path, O_WRONLY | O_CLOEXEC);
	if (fd < 0)
		return -errno;
	len = snprintf(buf, sizeof(buf), "%ld\n", v);
	if (write(fd, buf, len) != len)
		ret = -errno;
	close(fd);
	return ret;
}

static int logind_set(struct panel *p, double nits)
{
	sd_bus_error err = SD_BUS_ERROR_NULL;
	long v = lround(fmin(fmax(nits, p->min_nits), p->max_nits));
	int ret = -ENOTCONN;

	if (v == p->written)
		return 0;
	if (p->system && p->session[0]) {
		ret = sd_bus_call_method(p->system, LOGIN_SERVICE, p->session,
					 "org.freedesktop.login1.Session", "SetBrightness", &err, NULL,
					 "ssu", "backlight", p->name, (uint32_t)v);
		sd_bus_error_free(&err);
	}
	if (ret < 0)
		ret = sysfs_write(p, v);
	if (ret >= 0)
		p->written = v;
	return ret < 0 ? ret : 0;
}

int panel_set(struct panel *p, double nits)
{
	if (p->backend == PANEL_KDE)
		return p->kde_display[0] ? kde_set(p, nits) : -EAGAIN;
	return logind_set(p, nits);
}

/* Every connected display powered off, as when the desktop blanks the screens. */
static bool displays_off(void)
{
	int connected = 0, on = 0;
	glob_t g;

	if (glob("/sys/class/drm/card*-*/status", 0, NULL, &g) != 0)
		return false;
	for (size_t i = 0; i < g.gl_pathc; i++) {
		char buf[32], dpms[512];
		size_t len = strlen(g.gl_pathv[i]) - strlen("status");

		if (read_text(g.gl_pathv[i], buf, sizeof(buf)) <= 0 || strncmp(buf, "connected", 9))
			continue;
		connected++;
		snprintf(dpms, sizeof(dpms), "%.*sdpms", (int)len, g.gl_pathv[i]);
		if (read_text(dpms, buf, sizeof(buf)) <= 0 || !strncmp(buf, "On", 2))
			on++;
	}
	globfree(&g);
	return connected && !on;
}

bool panel_dark(struct panel *p)
{
	sd_bus_error err = SD_BUS_ERROR_NULL;
	int closed = 0;

	if (p->system &&
	    sd_bus_get_property_trivial(p->system, LOGIN_SERVICE, "/org/freedesktop/login1",
					"org.freedesktop.login1.Manager", "LidClosed", &err, 'b',
					&closed) < 0)
		closed = 0;
	sd_bus_error_free(&err);
	return closed || displays_off();
}
