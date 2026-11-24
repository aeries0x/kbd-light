// SPDX-License-Identifier: MIT
/* Checks of the brightness model, and replays of light sequences (fixtures/). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "model.h"
#include "plasma.h"
#include "settings.h"

static int failures;

#define CHECK(cond, ...)                                                       \
	do {                                                                   \
		if (!(cond)) {                                                 \
			failures++;                                            \
			printf("FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); \
			printf(__VA_ARGS__);                                   \
			putchar('\n');                                         \
		}                                                              \
	} while (0)

static void params(struct dl_params *p, const char *profile, const char *response, double max)
{
	struct dl_settings s;

	dl_settings_defaults(&s);
	snprintf(s.profile, sizeof(s.profile), "%s", profile);
	snprintf(s.response, sizeof(s.response), "%s", response);
	dl_settings_params(&s, max, p);
}

/* A small deterministic noise source, so traces are the same on every run. */
static double noise(unsigned *state)
{
	*state = *state * 1103515245u + 12345u;
	return ((*state >> 8) & 0xffff) / 65535.0 * 2 - 1;
}

struct run {
	int transitions;
	double first_t;		/* time of the first transition */
	double last_target;
};

/* Feed lux(t) at 5 Hz from t0 to t1, running transitions to completion. */
static void feed(struct dl_controller *c, double t0, double t1, double (*lux)(double, void *),
		 void *arg, struct run *r)
{
	for (double t = t0; t < t1; t += 0.2) {
		double nits;

		dl_controller_level(c, t, &nits);
		if (dl_controller_sample(c, t, lux(t, arg))) {
			if (!r->transitions)
				r->first_t = t;
			r->transitions++;
			r->last_target = c->target;
		}
	}
}

static double constant(double t, void *arg)
{
	(void)t;
	return *(double *)arg;
}

struct step { double at, before, after; };

static double step(double t, void *arg)
{
	struct step *s = arg;

	return t < s->at ? s->before : s->after;
}

struct noisy { double level, amp; unsigned seed; };

static double noisy(double t, void *arg)
{
	struct noisy *n = arg;

	(void)t;
	return n->level * (1 + n->amp * noise(&n->seed));
}

struct dip { double level, at, length, depth; };

static double dip(double t, void *arg)
{
	struct dip *d = arg;

	return t >= d->at && t < d->at + d->length ? d->level * d->depth : d->level;
}

static void test_perceptual(void)
{
	struct dl_params p;

	params(&p, "balanced", "balanced", 500);
	CHECK(fabs(dl_perceptual(&p, 1)) < 1e-9, "floor");
	CHECK(fabs(dl_perceptual(&p, 500) - 1) < 1e-9, "top");
	for (double n = 1; n <= 500; n *= 1.7)
		CHECK(fabs(dl_nits(&p, dl_perceptual(&p, n)) - n) < 1e-6, "round trip at %.1f", n);
}

static void test_profiles(void)
{
	for (const struct dl_profile *pr = dl_profiles; pr->name; pr++) {
		struct dl_params p;
		double last = 0;

		params(&p, pr->name, "balanced", 500);
		for (int i = 0; i < DL_POINTS; i++)
			CHECK(fabs(dl_curve(&p, NULL, dl_point_lux[i]) - p.nits[i]) < 1e-6,
			      "%s passes its point %d", pr->name, i);
		for (double lux = 0; lux < 6000; lux = lux * 1.3 + 0.5) {
			double n = dl_curve(&p, NULL, lux);

			CHECK(n >= last - 1e-9, "%s rises with the light at %.1f lx", pr->name, lux);
			CHECK(n >= 1 && n <= 500, "%s within the panel at %.1f lx", pr->name, lux);
			last = n;
		}
	}
	/* a dimmer panel caps the curve, and the curve still rises */
	{
		struct dl_params p;

		params(&p, "brighter", "balanced", 400);
		CHECK(fabs(dl_curve(&p, NULL, 3000) - 400) < 1e-6, "capped to a 400-nit panel");
	}
}

static void test_settings_file(void)
{
	const char *path = "test-settings.tmp";
	struct dl_settings s;
	struct dl_params p;
	FILE *f = fopen(path, "w");

	fprintf(f, "# comment\n[General]\nEnabled=false\nProfile=custom\nResponse=calm\nLearn=false\n"
		   "[Custom]\nDarkNits=20\nDimNits=10\nIndoorNits=150\nBrightNits=300\nDaylightNits=900\n"
		   "[Advanced]\nDeadband[$i]=0.08\nMinNits=nonsense\n");
	fclose(f);
	dl_settings_defaults(&s);
	CHECK(dl_settings_read(&s, path) == 0, "read");
	remove(path);
	CHECK(!s.enabled && !s.learn, "booleans");
	CHECK(!strcmp(s.profile, "custom") && !strcmp(s.response, "calm"), "names");
	CHECK(fabs(s.deadband - 0.08) < 1e-9, "KConfig key flags are ignored");
	CHECK(s.min_nits == 1, "a bad value keeps the default");
	dl_settings_params(&s, 500, &p);
	CHECK(p.nits[1] == 20, "a custom curve that falls is lifted: %.1f", p.nits[1]);
	CHECK(p.nits[4] == 500, "and capped to the panel: %.1f", p.nits[4]);
	CHECK(p.response.dim_wait > 5, "calm response");

	dl_settings_defaults(&s);
	CHECK(dl_settings_read(&s, "does-not-exist") == 0, "a missing file is fine");
}

static void test_learned_file(void)
{
	const char *path = "test-learned.tmp";
	double a[DL_POINTS] = { 0.1, -0.2, 0.05, 0, 0.3 }, b[DL_POINTS] = { 0.01, 0, 0, 0, 0 };
	double r[DL_POINTS];

	remove(path);
	CHECK(dl_learned_save(path, "balanced", a) == 0, "save");
	CHECK(dl_learned_save(path, "dimmer", b) == 0, "save another");
	CHECK(dl_learned_save(path, "balanced", b) == 0, "replace");
	dl_learned_load(path, "balanced", r);
	CHECK(fabs(r[0] - 0.01) < 1e-9 && r[4] == 0, "replaced, not appended");
	dl_learned_load(path, "brighter", r);
	CHECK(r[0] == 0 && r[1] == 0, "a profile with nothing learned");
	remove(path);
}

/* A pick moves the curve through it, a little, locally, and keeps it rising. */
static void test_learning(void)
{
	struct dl_params p;
	double off[DL_POINTS] = { 0 }, before[DL_POINTS], n;

	params(&p, "balanced", "balanced", 500);
	CHECK(dl_learn(&p, off, 100, 250), "passes through a pick at a point");
	CHECK(fabs(dl_curve(&p, off, 100) - 250) < 0.01, "exactly: %.2f", dl_curve(&p, off, 100));
	CHECK(off[0] == 0 && off[1] == 0, "the dark end stays: %.3f %.3f", off[0], off[1]);

	CHECK(dl_learn(&p, off, 40, 120), "between points");
	CHECK(fabs(dl_curve(&p, off, 40) - 120) < 0.01, "exactly: %.2f", dl_curve(&p, off, 40));

	/* a pick far off the curve moves it only so far */
	memset(off, 0, sizeof(off));
	CHECK(!dl_learn(&p, off, 100, 2), "a far pick is not reached in one go");
	for (int i = 0; i < DL_POINTS; i++)
		CHECK(fabs(off[i]) <= DL_MAX_LEARN_STEP + 1e-9 || i != 2, "bounded step at %d: %.3f", i,
		      off[i]);

	/* a pick that would make the curve fall pushes the neighbours along */
	memset(off, 0, sizeof(off));
	dl_learn(&p, off, 10, 300);
	for (double lux = 0, last = 0; lux < 3000; lux = lux * 1.2 + 0.5, last = n) {
		n = dl_curve(&p, off, lux);
		CHECK(n >= last - 1e-6, "still rising at %.1f lx after a bright pick in the dark", lux);
	}

	/* the same two picks made back and forth settle the curve between them */
	memset(off, 0, sizeof(off));
	for (int i = 0; i < 40; i++) {
		memcpy(before, off, sizeof(off));
		dl_learn(&p, off, 98, i % 2 ? 226 : 332);
	}
	n = dl_curve(&p, off, 98);
	CHECK(n >= 225 && n <= 333, "between the picks: %.1f", n);
	CHECK(dl_curve(&p, off, 0) < 60, "a dark room is not dragged up: %.1f", dl_curve(&p, off, 0));
}

static void test_steady_and_noise(void)
{
	struct dl_params p;
	struct dl_controller c;
	struct run r = { 0 };
	struct noisy n = { 100, 0.15, 1 };

	params(&p, "balanced", "balanced", 500);
	dl_controller_init(&c, &p, NULL, 170);
	feed(&c, 0, 600, noisy, &n, &r);
	/* the first sample sets the level; noise of +-15 % must not move it after */
	CHECK(r.transitions <= 1, "steady light with noise: %d transitions", r.transitions);
}

static void test_spikes(void)
{
	struct dl_params p;
	struct dl_controller c;
	struct run r = { 0 };
	struct dip d = { 200, 30, 1.0, 0.2 };	/* a hand over the sensor for a second */
	double l = 200;

	params(&p, "balanced", "balanced", 500);
	dl_controller_init(&c, &p, NULL, dl_curve(&p, NULL, 200));
	feed(&c, 0, 0.2, constant, &l, &r);
	r.transitions = 0;
	feed(&c, 0.2, 120, dip, &d, &r);
	CHECK(r.transitions == 0, "a passing shadow: %d transitions", r.transitions);

	d.at = 200;
	d.length = 4;	/* someone leaning in for a few seconds */
	feed(&c, 120, 300, dip, &d, &r);
	CHECK(r.transitions == 0, "a short shadow: %d transitions", r.transitions);
}

static void test_steps(void)
{
	struct dl_params p;
	struct dl_controller c;
	struct run r = { 0 };
	struct step off = { 60, 300, 3 }, on = { 60, 3, 300 };

	params(&p, "balanced", "balanced", 500);
	dl_controller_init(&c, &p, NULL, dl_curve(&p, NULL, 300));
	feed(&c, 0, 0.2, step, &off, &r);
	r.transitions = 0;
	feed(&c, 0.2, 120, step, &off, &r);
	CHECK(r.transitions == 1, "lights off: %d transitions", r.transitions);
	CHECK(r.first_t >= 60 + p.response.dim_wait && r.first_t < 60 + p.response.dim_wait + 1.5,
	      "after waiting %.1f s: at %.1f s", p.response.dim_wait, r.first_t);
	CHECK(fabs(r.last_target - dl_curve(&p, NULL, 3)) / r.last_target < 0.1,
	      "to the dark level: %.1f", r.last_target);

	dl_controller_init(&c, &p, NULL, dl_curve(&p, NULL, 3));
	memset(&r, 0, sizeof(r));
	feed(&c, 0, 0.2, step, &on, &r);
	r.transitions = 0;
	feed(&c, 0.2, 120, step, &on, &r);
	CHECK(r.transitions == 1, "lights on: %d transitions", r.transitions);
	CHECK(r.first_t < 60 + p.response.brighten_wait + 1.5, "brightening waits less: %.1f s",
	      r.first_t);
}

/* After the user picks a level, light moving around it doesn't pull the screen away. */
static void test_no_ramp_away(void)
{
	struct dl_params p;
	struct dl_controller c;
	struct run r = { 0 };
	struct noisy n = { 95, 0.2, 7 };
	double l = 111;

	params(&p, "balanced", "balanced", 500);
	dl_controller_init(&c, &p, NULL, 170);
	feed(&c, 0, 3, constant, &l, &r);
	feed(&c, 3, 40, constant, &l, &r);
	CHECK(dl_controller_adjusted(&c, 40, 327, DL_ADJUST_LEARN), "learned");
	memset(&r, 0, sizeof(r));
	feed(&c, 40, 400, noisy, &n, &r);
	CHECK(r.transitions == 0, "light 76-114 lx after a pick at 111: %d transitions",
	      r.transitions);
	CHECK(fabs(c.shown - 327) < 1, "kept: %.1f", c.shown);
}

static void test_hold(void)
{
	struct dl_params p;
	struct dl_controller c;
	struct run r = { 0 };
	struct step brighter = { 100, 100, 400 };

	params(&p, "balanced", "balanced", 500);
	dl_controller_init(&c, &p, NULL, 170);
	feed(&c, 0, 30, step, &brighter, &r);
	CHECK(!dl_controller_adjusted(&c, 30, 40, DL_ADJUST_HOLD), "a hold is not learned");
	memset(&r, 0, sizeof(r));
	feed(&c, 30, 99, step, &brighter, &r);
	CHECK(r.transitions == 0 && c.hold, "held");
	feed(&c, 100, 200, step, &brighter, &r);
	CHECK(r.transitions == 1 && !c.hold, "a fourfold change ends the hold: %d", r.transitions);

	/* the level coming back to the automatic one ends a hold too (idle dimming over) */
	dl_controller_init(&c, &p, NULL, 170);
	memset(&r, 0, sizeof(r));
	feed(&c, 0, 30, constant, &(double){ 100 }, &r);
	c.automatic = c.shown;
	dl_controller_adjusted(&c, 30, 50, DL_ADJUST_HOLD);
	CHECK(c.hold, "dimmed: held");
	dl_controller_adjusted(&c, 60, c.automatic, DL_ADJUST_HOLD);
	CHECK(!c.hold, "restored: automatic again");
}

/* Replay a CSV of t,lux[,picked nits]; returns the number of transitions. */
static int replay(const char *path, const char *response, int *learned)
{
	struct dl_params p;
	struct dl_controller c;
	char line[256];
	int transitions = 0;
	FILE *f = fopen(path, "r");

	if (!f) {
		printf("FAIL cannot open %s\n", path);
		failures++;
		return -1;
	}
	params(&p, "balanced", response, 500);
	dl_controller_init(&c, &p, NULL, 226);
	*learned = 0;
	while (fgets(line, sizeof(line), f)) {
		double t, lux, pick = 0, nits;
		int n = sscanf(line, "%lf,%lf,%lf", &t, &lux, &pick);

		if (n < 2)
			continue;
		dl_controller_level(&c, t, &nits);
		if (n == 3 && pick > 0)
			*learned += dl_controller_adjusted(&c, t, pick, DL_ADJUST_LEARN);
		transitions += dl_controller_sample(&c, t, lux);
	}
	fclose(f);
	return transitions - 1;	/* not the first sample, which sets the level */
}

static void test_fixtures(const char *dir)
{
	char path[512];
	int learned, n;

	/*
	 * Rebuilt from the old controller's log of a J815 under Plasma (boot
	 * c7e566d8, 240 s of a lit room): the light levels at each of its eight
	 * transitions, held as plateaus, with noise.
	 */
	snprintf(path, sizeof(path), "%s/j815-room-swings.csv", dir);
	n = replay(path, "calm", &learned);
	CHECK(n >= 0 && n <= 2, "room swings, calm: %d transitions", n);
	n = replay(path, "balanced", &learned);
	CHECK(n >= 0 && n <= 4, "room swings, balanced: %d transitions", n);
	printf("room swings: balanced %d transitions\n", n);

	/*
	 * Rebuilt the same way from boot a6b02ab2: a pick of 327 nits at 111 lux,
	 * then the light at 68-97 lux; the old controller dimmed away 8 s later.
	 */
	snprintf(path, sizeof(path), "%s/j815-after-pick.csv", dir);
	n = replay(path, "balanced", &learned);
	CHECK(learned == 1, "the pick is learned");
	CHECK(n == 0, "no transition away from the pick: %d", n);
}

static void test_plasma_outputs(void)
{
	/* KWin's output settings, as it writes them (shortened) */
	static const char on[] =
		"[\n    {\n        \"data\": [\n            {\n                \"autoBrightnessCurve\": [[0, 0.2], [300, 0.7]],\n"
		"                \"automaticBrightness\": true,\n                \"brightness\": 0.75,\n"
		"                \"connectorName\": \"eDP-1\",\n                \"edidIdentifier\": \"APP 41744 0 0 0\"\n            },\n"
		"            {\n                \"automaticBrightness\": false,\n                \"connectorName\": \"DP-1\"\n            }\n"
		"        ],\n        \"name\": \"outputs\"\n    },\n"
		"    {\n        \"data\": [{\"lidClosed\": false, \"outputs\": [{\"enabled\": true, \"outputIndex\": 0}]}],\n"
		"        \"name\": \"setups\"\n    }\n]\n";
	static const char off[] = "[{\"data\": [{\"automaticBrightness\": false, \"connectorName\": \"eDP-1\"}], \"name\": \"outputs\"}]";
	static const char external[] =
		"[{\"data\": [{\"connectorName\": \"eDP-1\"}, {\"automaticBrightness\": true, \"connectorName\": \"HDMI-A-1\"}],"
		" \"name\": \"outputs\"}]";
	static const char escaped[] = "[{\"data\": [{\"x\": \"a\\\"}{,\", \"connectorName\": \"eDP-1\", \"automaticBrightness\": true}]}]";

	CHECK(dl_plasma_auto_brightness_in(on), "on for the built-in panel");
	CHECK(!dl_plasma_auto_brightness_in(off), "off");
	CHECK(!dl_plasma_auto_brightness_in(external), "on only for an external screen");
	CHECK(dl_plasma_auto_brightness_in(escaped), "escaped quotes and brackets in strings");
	CHECK(!dl_plasma_auto_brightness_in(""), "an empty file");
	CHECK(!dl_plasma_auto_brightness_in("[{\"data\": [{\"automaticBrightness\": tr"), "a cut-off file");
	CHECK(!dl_plasma_auto_brightness("does-not-exist"), "no file");
}

int main(int argc, char **argv)
{
	const char *fixtures = argc > 1 ? argv[1] : "fixtures";

	test_perceptual();
	test_profiles();
	test_settings_file();
	test_learned_file();
	test_learning();
	test_steady_and_noise();
	test_spikes();
	test_steps();
	test_no_ramp_away();
	test_hold();
	test_plasma_outputs();
	test_fixtures(fixtures);
	printf("%s\n", failures ? "FAILED" : "all passed");
	return failures != 0;
}
