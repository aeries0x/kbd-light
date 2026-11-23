// SPDX-License-Identifier: MIT
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "profiles.h"
#include "settings.h"

_Static_assert(DL_POINTS == 5, "dl_learned_load reads five offsets");

#define PROFILE(name, ...) { #name, { __VA_ARGS__ } },
const struct dl_profile dl_profiles[] = {
	DL_PROFILES(PROFILE)
	{ 0 },
};

#define RESPONSE(name, up, down, wait_up, wait_down, time_up, time_down)                   \
	{ #name, { .brighten_ratio = up, .dim_ratio = down, .brighten_wait = wait_up,      \
		   .dim_wait = wait_down, .brighten_time = time_up, .dim_time = time_down } },
const struct dl_response_preset dl_responses[] = {
	DL_RESPONSES(RESPONSE)
	{ 0 },
};

/* Keys of the Custom group, in the order of dl_point_lux. */
static const char *const custom_keys[DL_POINTS] = {
	"DarkNits", "DimNits", "IndoorNits", "BrightNits", "DaylightNits",
};

static const struct dl_profile *find_profile(const char *name)
{
	for (const struct dl_profile *p = dl_profiles; p->name; p++)
		if (!strcasecmp(p->name, name))
			return p;
	return NULL;
}

static const struct dl_response_preset *find_response(const char *name)
{
	for (const struct dl_response_preset *r = dl_responses; r->name; r++)
		if (!strcasecmp(r->name, name))
			return r;
	return NULL;
}

void dl_settings_defaults(struct dl_settings *s)
{
	const struct dl_profile *b = find_profile("balanced");

	memset(s, 0, sizeof(*s));
	s->enabled = true;
	s->learn = true;
	snprintf(s->profile, sizeof(s->profile), "balanced");
	snprintf(s->response, sizeof(s->response), "balanced");
	memcpy(s->custom, b->nits, sizeof(s->custom));
	s->min_nits = 1;	/* the DCP backlight's lowest step above off */
	s->deadband = 0.05;
}

static char *trim(char *v)
{
	char *end;

	while (isspace((unsigned char)*v))
		v++;
	end = v + strlen(v);
	while (end > v && isspace((unsigned char)end[-1]))
		*--end = '\0';
	return v;
}

static bool parse_bool(const char *v, bool def)
{
	if (!strcasecmp(v, "true") || !strcmp(v, "1") || !strcasecmp(v, "yes") || !strcasecmp(v, "on"))
		return true;
	if (!strcasecmp(v, "false") || !strcmp(v, "0") || !strcasecmp(v, "no") || !strcasecmp(v, "off"))
		return false;
	return def;
}

static void parse_double(const char *v, double *out, double lo, double hi)
{
	char *end;
	double d = strtod(v, &end);

	if (end != v && !*trim(end) && isfinite(d) && d >= lo && d <= hi)
		*out = d;
}

static void set(struct dl_settings *s, const char *group, const char *key, const char *v)
{
	if (!strcmp(group, "General")) {
		if (!strcmp(key, "Enabled"))
			s->enabled = parse_bool(v, s->enabled);
		else if (!strcmp(key, "Learn"))
			s->learn = parse_bool(v, s->learn);
		else if (!strcmp(key, "Profile") && (find_profile(v) || !strcasecmp(v, "custom")))
			snprintf(s->profile, sizeof(s->profile), "%s", v);
		else if (!strcmp(key, "Response") && find_response(v))
			snprintf(s->response, sizeof(s->response), "%s", v);
	} else if (!strcmp(group, "Custom")) {
		for (int i = 0; i < DL_POINTS; i++)
			if (!strcmp(key, custom_keys[i]))
				parse_double(v, &s->custom[i], 0.1, 10000);
	} else if (!strcmp(group, "Advanced")) {
		if (!strcmp(key, "MinNits"))
			parse_double(v, &s->min_nits, 0.01, 1000);
		else if (!strcmp(key, "Deadband"))
			parse_double(v, &s->deadband, 0, 0.5);
		else if (!strcmp(key, "Backlight"))
			snprintf(s->backlight, sizeof(s->backlight), "%s", v);
	}
}

int dl_settings_read(struct dl_settings *s, const char *path)
{
	char line[512], group[64] = "";
	FILE *f = fopen(path, "re");

	if (!f)
		return errno == ENOENT ? 0 : -errno;
	while (fgets(line, sizeof(line), f)) {
		char *l = trim(line), *eq, *key;

		if (!*l || *l == '#' || *l == ';')
			continue;
		if (*l == '[') {
			char *end = strchr(l, ']');

			if (end) {
				*end = '\0';
				snprintf(group, sizeof(group), "%s", l + 1);
			}
			continue;
		}
		eq = strchr(l, '=');
		if (!eq)
			continue;
		*eq = '\0';
		key = trim(l);
		/* KConfig may mark a key immutable or expandable: Key[$i]=value */
		key[strcspn(key, "[")] = '\0';
		set(s, group, key, trim(eq + 1));
	}
	fclose(f);
	return 0;
}

void dl_settings_params(const struct dl_settings *s, double max_nits, struct dl_params *p)
{
	const struct dl_profile *prof = find_profile(s->profile);
	const struct dl_response_preset *resp = find_response(s->response);
	double last = 0;

	memset(p, 0, sizeof(*p));
	p->min_nits = s->min_nits;
	p->max_nits = max_nits > s->min_nits ? max_nits : s->min_nits * 2;
	p->deadband = s->deadband;
	p->response = (resp ? resp : find_response("balanced"))->response;
	for (int i = 0; i < DL_POINTS; i++) {
		/* never falling as the light rises, and within the panel's range */
		double n = prof ? prof->nits[i] : s->custom[i];

		n = fmin(fmax(n, p->min_nits), p->max_nits);
		p->nits[i] = last = fmax(n, last);
	}
}

static int xdg_path(char *buf, size_t len, const char *env, const char *fallback, const char *name)
{
	const char *dir = getenv(env), *home = getenv("HOME");
	int n;

	if (dir && *dir == '/')
		n = snprintf(buf, len, "%s/%s", dir, name);
	else if (home && *home)
		n = snprintf(buf, len, "%s/%s/%s", home, fallback, name);
	else
		return -ENOENT;
	return n < 0 || (size_t)n >= len ? -ENAMETOOLONG : 0;
}

int dl_config_path(char *buf, size_t len)
{
	return xdg_path(buf, len, "XDG_CONFIG_HOME", ".config", DL_CONFIG_NAME);
}

int dl_state_path(char *buf, size_t len)
{
	return xdg_path(buf, len, "XDG_STATE_HOME", ".local/state", "aurora-display-light/learned");
}

/* The file has one line per profile: its name and DL_POINTS offsets. */
void dl_learned_load(const char *path, const char *profile, double offset[DL_POINTS])
{
	char line[256];
	FILE *f = fopen(path, "re");

	memset(offset, 0, sizeof(offset[0]) * DL_POINTS);
	if (!f)
		return;
	while (fgets(line, sizeof(line), f)) {
		char name[32];
		double o[DL_POINTS];

		if (sscanf(line, "%31s %lf %lf %lf %lf %lf", name, &o[0], &o[1], &o[2], &o[3], &o[4]) ==
			    1 + DL_POINTS &&
		    !strcasecmp(name, profile)) {
			for (int i = 0; i < DL_POINTS; i++)
				offset[i] = isfinite(o[i]) ? fmin(fmax(o[i], -1), 1) : 0;
		}
	}
	fclose(f);
}

static int mkdir_parents(const char *path)
{
	char dir[512];

	snprintf(dir, sizeof(dir), "%s", path);
	for (char *p = dir + 1; *p; p++) {
		if (*p != '/')
			continue;
		*p = '\0';
		if (mkdir(dir, 0700) && errno != EEXIST)
			return -errno;
		*p = '/';
	}
	return 0;
}

int dl_learned_save(const char *path, const char *profile, const double offset[DL_POINTS])
{
	char tmp[520], line[256];
	FILE *in, *out;
	int ret;

	ret = mkdir_parents(path);
	if (ret)
		return ret;
	snprintf(tmp, sizeof(tmp), "%s.new", path);
	out = fopen(tmp, "we");
	if (!out)
		return -errno;
	in = fopen(path, "re");
	while (in && fgets(line, sizeof(line), in)) {
		char name[32];

		if (sscanf(line, "%31s", name) == 1 && strcasecmp(name, profile))
			fputs(line, out);
	}
	if (in)
		fclose(in);
	fprintf(out, "%s", profile);
	for (int i = 0; i < DL_POINTS; i++)
		fprintf(out, " %.4f", offset[i]);
	fputc('\n', out);
	if (fflush(out) || fsync(fileno(out))) {
		ret = -errno;
		fclose(out);
		unlink(tmp);
		return ret;
	}
	fclose(out);
	return rename(tmp, path) ? -errno : 0;
}
