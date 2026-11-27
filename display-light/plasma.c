// SPDX-License-Identifier: MIT
#include "plasma.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * KWin keeps one entry per screen it has seen under "outputs", each an object
 * with "connectorName" ("eDP-1" for a built-in panel) and, once set,
 * "automaticBrightness". A small reader that walks the whole document and
 * checks each object's own keys is enough; nothing else in it is needed.
 */

#define MAX_DEPTH 32
#define MAX_SIZE (4 << 20)

struct reader {
	const char *p;
	bool found;
};

static void skip_space(struct reader *r)
{
	while (*r->p == ' ' || *r->p == '\t' || *r->p == '\n' || *r->p == '\r')
		r->p++;
}

/* A string, copied (escapes kept as the escaped character, truncated to len). */
static bool read_string(struct reader *r, char *out, size_t len)
{
	size_t n = 0;

	if (*r->p != '"')
		return false;
	r->p++;
	while (*r->p && *r->p != '"') {
		if (*r->p == '\\' && r->p[1])
			r->p++;
		if (n + 1 < len)
			out[n++] = *r->p;
		r->p++;
	}
	if (*r->p != '"')
		return false;
	r->p++;
	if (len)
		out[n] = '\0';
	return true;
}

static bool read_value(struct reader *r, int depth, char *str, size_t len, bool *is_true);

static bool read_object(struct reader *r, int depth)
{
	bool internal = false, automatic = false;

	r->p++; /* '{' */
	skip_space(r);
	if (*r->p == '}') {
		r->p++;
		return true;
	}
	for (;;) {
		char key[64], str[64] = "";
		bool is_true = false;

		skip_space(r);
		if (!read_string(r, key, sizeof(key)))
			return false;
		skip_space(r);
		if (*r->p++ != ':')
			return false;
		skip_space(r);
		if (!read_value(r, depth + 1, str, sizeof(str), &is_true))
			return false;
		if (!strcmp(key, "connectorName"))
			internal = !strncmp(str, "eDP", 3);
		else if (!strcmp(key, "automaticBrightness"))
			automatic = is_true;
		skip_space(r);
		if (*r->p == ',') {
			r->p++;
			continue;
		}
		if (*r->p != '}')
			return false;
		r->p++;
		break;
	}
	if (internal && automatic)
		r->found = true;
	return true;
}

static bool read_array(struct reader *r, int depth)
{
	r->p++; /* '[' */
	skip_space(r);
	if (*r->p == ']') {
		r->p++;
		return true;
	}
	for (;;) {
		char str[1];
		bool is_true;

		skip_space(r);
		if (!read_value(r, depth + 1, str, sizeof(str), &is_true))
			return false;
		skip_space(r);
		if (*r->p == ',') {
			r->p++;
			continue;
		}
		if (*r->p != ']')
			return false;
		r->p++;
		return true;
	}
}

static bool read_value(struct reader *r, int depth, char *str, size_t len, bool *is_true)
{
	*is_true = false;
	if (depth > MAX_DEPTH)
		return false;
	switch (*r->p) {
	case '{':
		return read_object(r, depth);
	case '[':
		return read_array(r, depth);
	case '"':
		return read_string(r, str, len);
	default:
		/* a number, true, false or null */
		if (!strncmp(r->p, "true", 4))
			*is_true = true;
		if (!strchr("-0123456789tfn", *r->p))
			return false;
		while (*r->p && !strchr(",}] \t\r\n", *r->p))
			r->p++;
		return true;
	}
}

bool dl_plasma_auto_brightness_in(const char *json)
{
	struct reader r = { json, false };
	char str[1];
	bool is_true;

	skip_space(&r);
	/* a broken file counts for what was read of it */
	read_value(&r, 0, str, sizeof(str), &is_true);
	return r.found;
}

bool dl_plasma_auto_brightness(const char *path)
{
	FILE *f = fopen(path, "re");
	char *buf;
	size_t n;
	bool on;

	if (!f)
		return false;
	buf = malloc(MAX_SIZE + 1);
	if (!buf) {
		fclose(f);
		return false;
	}
	n = fread(buf, 1, MAX_SIZE, f);
	fclose(f);
	buf[n] = '\0';
	on = dl_plasma_auto_brightness_in(buf);
	free(buf);
	return on;
}

int dl_plasma_outputs_path(char *buf, size_t len)
{
	const char *config = getenv("XDG_CONFIG_HOME"), *home = getenv("HOME");

	if (config && *config == '/')
		snprintf(buf, len, "%s/" DL_KWIN_OUTPUTS_NAME, config);
	else if (home && *home == '/')
		snprintf(buf, len, "%s/.config/" DL_KWIN_OUTPUTS_NAME, home);
	else
		return -1;
	return 0;
}
