// SPDX-License-Identifier: MIT
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "model.h"
#include "profiles.h"

const double dl_point_lux[DL_POINTS] = { DL_POINT_LUX };

/*
 * Lux is compared on a log scale with this added, so that in the dark a step
 * of a lux or two is not a large relative change.
 */
#define LUX_SOFT 5.0
/* Time constant of the smoothing that takes the edge off single odd samples. */
#define FAST_TAU 0.6
/* Samples that make up the light level at an adjustment (2 s at 5 Hz). */
#define RECENT 10

static double clamp(double v, double lo, double hi)
{
	return v < lo ? lo : v > hi ? hi : v;
}

double dl_perceptual(const struct dl_params *p, double nits)
{
	nits = clamp(nits, p->min_nits, p->max_nits);
	return log(nits / p->min_nits) / log(p->max_nits / p->min_nits);
}

double dl_nits(const struct dl_params *p, double perceptual)
{
	return p->min_nits * pow(p->max_nits / p->min_nits, clamp(perceptual, 0, 1));
}

static double lux_x(double lux)
{
	return log10(1 + clamp(lux, dl_point_lux[0], dl_point_lux[DL_POINTS - 1]));
}

/* The segment lux falls in, and where in it (0..1). */
static int segment(double lux, double *frac)
{
	double x = lux_x(lux);
	int j = 0;

	while (j < DL_POINTS - 2 && x > lux_x(dl_point_lux[j + 1]))
		j++;
	*frac = (x - lux_x(dl_point_lux[j])) / (lux_x(dl_point_lux[j + 1]) - lux_x(dl_point_lux[j]));
	*frac = clamp(*frac, 0, 1);
	return j;
}

static double point(const struct dl_params *p, const double offset[DL_POINTS], int i)
{
	return clamp(dl_perceptual(p, p->nits[i]) + (offset ? offset[i] : 0), 0, 1);
}

double dl_curve(const struct dl_params *p, const double offset[DL_POINTS], double lux)
{
	double f;
	int j = segment(lux, &f);

	return dl_nits(p, point(p, offset, j) * (1 - f) + point(p, offset, j + 1) * f);
}

static void remember(struct dl_ambient *a, double lux)
{
	a->recent[a->recent_next] = lux;
	a->recent_next = (a->recent_next + 1) % RECENT;
	if (a->recent_count < RECENT)
		a->recent_count++;
}

enum dl_ambient_event dl_ambient_sample(struct dl_ambient *a, const struct dl_response *r,
					double t, double lux)
{
	double l, d, dt;
	int dir;

	if (!(lux >= 0))
		return DL_AMBIENT_SAME;
	remember(a, lux);
	l = log(lux + LUX_SOFT);
	if (!a->valid) {
		a->valid = true;
		a->ambient = lux;
		a->fast = l;
		a->last_t = t;
		a->pending = 0;
		return DL_AMBIENT_CHANGED;
	}
	dt = clamp(t - a->last_t, 0, 5);
	a->last_t = t;
	a->fast += (l - a->fast) * (1 - exp(-dt / FAST_TAU));

	d = a->fast - log(a->ambient + LUX_SOFT);
	dir = d >= log1p(r->brighten_ratio) ? 1 : d <= -log1p(r->dim_ratio) ? -1 : 0;
	if (dir != a->pending) {
		a->pending = dir;
		a->pending_since = t;
	}
	if (!dir || t - a->pending_since < (dir > 0 ? r->brighten_wait : r->dim_wait))
		return DL_AMBIENT_SAME;
	a->ambient = fmax(0, exp(a->fast) - LUX_SOFT);
	a->pending = 0;
	return DL_AMBIENT_CHANGED;
}

static int cmp_double(const void *x, const void *y)
{
	double a = *(const double *)x, b = *(const double *)y;

	return (a > b) - (a < b);
}

double dl_ambient_recent(const struct dl_ambient *a)
{
	double v[RECENT];
	int n = a->recent_count;

	if (!n)
		return a->ambient;
	memcpy(v, a->recent, sizeof(v[0]) * n);
	qsort(v, n, sizeof(v[0]), cmp_double);
	return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
}

void dl_ambient_anchor(struct dl_ambient *a, double lux)
{
	a->valid = true;
	a->ambient = lux;
	a->fast = log(lux + LUX_SOFT);
	a->pending = 0;
}

bool dl_learn(const struct dl_params *p, double offset[DL_POINTS], double lux, double nits)
{
	double f, w0, w1, want, have, k, P[DL_POINTS];
	int j = segment(lux, &f);

	w0 = 1 - f;
	w1 = f;
	want = dl_perceptual(p, nits);
	have = point(p, offset, j) * w0 + point(p, offset, j + 1) * w1;
	k = (want - have) / (w0 * w0 + w1 * w1);
	offset[j] += clamp(k * w0, -DL_MAX_LEARN_STEP, DL_MAX_LEARN_STEP);
	offset[j + 1] += clamp(k * w1, -DL_MAX_LEARN_STEP, DL_MAX_LEARN_STEP);

	/* Keep every point on the scale, and the curve rising with the light. */
	for (int i = 0; i < DL_POINTS; i++) {
		P[i] = point(p, offset, i);
		offset[i] = P[i] - dl_perceptual(p, p->nits[i]);
	}
	for (int i = j + 2; i < DL_POINTS; i++)
		if (P[i] < P[i - 1]) {
			P[i] = P[i - 1];
			offset[i] = P[i] - dl_perceptual(p, p->nits[i]);
		}
	for (int i = j - 1; i >= 0; i--)
		if (P[i] > P[i + 1]) {
			P[i] = P[i + 1];
			offset[i] = P[i] - dl_perceptual(p, p->nits[i]);
		}
	return fabs(dl_perceptual(p, dl_curve(p, offset, lux)) - want) < 1e-6;
}

void dl_ramp_start(struct dl_ramp *r, const struct dl_params *p, double t, double shown,
		   double target)
{
	r->from = dl_perceptual(p, shown);
	r->to = dl_perceptual(p, target);
	r->start = t;
	r->length = target > shown ? p->response.brighten_time : p->response.dim_time;
	r->active = true;
}

double dl_ramp_level(struct dl_ramp *r, const struct dl_params *p, double t)
{
	double f = r->length > 0 ? clamp((t - r->start) / r->length, 0, 1) : 1;

	/* Ease in and out, so a transition neither starts nor stops with a jolt. */
	f = f * f * (3 - 2 * f);
	if (t - r->start >= r->length)
		r->active = false;
	return dl_nits(p, r->from + (r->to - r->from) * f);
}

bool dl_worth_moving(const struct dl_params *p, double shown, double target)
{
	return shown <= 0 || fabs(target - shown) / shown >= p->deadband;
}

static void follow(struct dl_controller *c, double t)
{
	double from = c->shown;

	c->target = dl_curve(&c->p, c->offset, c->ambient.ambient);
	if (c->ramp.active)
		from = dl_ramp_level(&c->ramp, &c->p, t);
	if (from <= 0)
		return;
	if (!dl_worth_moving(&c->p, from, c->target)) {
		/* close enough: this is the automatic level */
		if (!c->ramp.active)
			c->automatic = from;
		return;
	}
	dl_ramp_start(&c->ramp, &c->p, t, from, c->target);
}

void dl_controller_init(struct dl_controller *c, const struct dl_params *p,
			const double offset[DL_POINTS], double shown)
{
	memset(c, 0, sizeof(*c));
	c->p = *p;
	if (offset)
		memcpy(c->offset, offset, sizeof(c->offset));
	c->shown = shown;
	c->automatic = -1;
}

void dl_controller_set_params(struct dl_controller *c, double t, const struct dl_params *p,
			      const double offset[DL_POINTS])
{
	c->p = *p;
	memcpy(c->offset, offset, sizeof(c->offset));
	if (c->ambient.valid && !c->hold)
		follow(c, t);
}

void dl_controller_restart(struct dl_controller *c)
{
	c->ambient.valid = false;
	c->ambient.recent_count = 0;
	c->ramp.active = false;
}

bool dl_controller_sample(struct dl_controller *c, double t, double lux)
{
	bool was = c->ramp.active;
	double start = c->ramp.start;

	if (dl_ambient_sample(&c->ambient, &c->p.response, t, lux) != DL_AMBIENT_CHANGED)
		return false;
	if (c->hold) {
		double ratio = (c->ambient.ambient + 5) / (c->hold_lux + 5);

		if (ratio < DL_HOLD_RATIO && ratio > 1 / DL_HOLD_RATIO)
			return false;
		c->hold = false;
	}
	follow(c, t);
	return c->ramp.active && (!was || c->ramp.start != start);
}

bool dl_controller_level(struct dl_controller *c, double t, double *nits)
{
	if (!c->ramp.active)
		return false;
	c->shown = *nits = dl_ramp_level(&c->ramp, &c->p, t);
	c->automatic = *nits;
	return true;
}

bool dl_controller_adjusted(struct dl_controller *c, double t, double nits, enum dl_adjust how)
{
	double lux = dl_ambient_recent(&c->ambient);
	bool learned = false;

	(void)t;
	c->ramp.active = false;
	c->shown = nits;
	/* Back to the level last set automatically, as when idle dimming ends. */
	if (c->automatic > 0 && fabs(nits - c->automatic) <= fmax(1, c->automatic * 0.01)) {
		c->hold = false;
		return false;
	}
	if (how == DL_ADJUST_LEARN && c->ambient.valid) {
		dl_learn(&c->p, c->offset, lux, nits);
		learned = true;
		c->hold = false;
	} else {
		c->hold = true;
		c->hold_lux = lux;
	}
	if (c->ambient.valid)
		dl_ambient_anchor(&c->ambient, lux);
	if (c->hold) {
		/* keep c->automatic: the level coming back to it ends the hold */
		c->target = nits;
	} else {
		c->target = dl_curve(&c->p, c->offset, lux);
		c->automatic = nits;
	}
	return learned;
}
