/* SPDX-License-Identifier: MIT */
/*
 * The brightness model of aurora-display-light, kept free of I/O so it can be
 * tested and replayed offline.
 *
 * Brightness is handled in nits and, for anything a person perceives, on a
 * perceptual scale: P(nits) = ln(nits / min) / ln(max / min), 0 at the
 * panel's darkest level and 1 at its brightest. Equal steps of P look like
 * equal steps of brightness.
 */
#ifndef DISPLAY_LIGHT_MODEL_H
#define DISPLAY_LIGHT_MODEL_H

#include <stdbool.h>

/* The curve is given at these ambient levels, in lux. */
#define DL_POINTS 5
extern const double dl_point_lux[DL_POINTS];

/* How far one adjustment may move a point of the curve, in P. */
#define DL_MAX_LEARN_STEP 0.25

struct dl_response {
	double brighten_ratio;	/* light must rise by this fraction ... */
	double dim_ratio;	/* ... or fall by this fraction (as 1 / (1 + dim_ratio)) */
	double brighten_wait;	/* s it must stay there before the screen follows */
	double dim_wait;
	double brighten_time;	/* s a brightening transition takes */
	double dim_time;
};

struct dl_params {
	double nits[DL_POINTS];	/* the curve: nits at dl_point_lux[] */
	struct dl_response response;
	double min_nits;	/* the panel's darkest level */
	double max_nits;	/* and brightest */
	double deadband;	/* targets closer than this fraction to the shown level are left */
};

/* What the ambient filter decided for a sample. */
enum dl_ambient_event {
	DL_AMBIENT_SAME,
	DL_AMBIENT_CHANGED,
};

struct dl_ambient {
	bool valid;
	double ambient;		/* lux the current target is based on */
	double fast;		/* smoothed ln(lux + soft) */
	double last_t;
	int pending;		/* direction the light is moving in, or 0 */
	double pending_since;
	/* recent raw samples, for the light level at an adjustment */
	double recent[16];
	int recent_count, recent_next;
};

struct dl_ramp {
	bool active;
	double from, to;	/* P */
	double start, length;	/* s */
};

double dl_perceptual(const struct dl_params *p, double nits);
double dl_nits(const struct dl_params *p, double perceptual);

/* The curve, with the learned offsets (in P, one per point) applied. */
double dl_curve(const struct dl_params *p, const double offset[DL_POINTS], double lux);

/*
 * Feed one sensor sample taken at time t (s). Returns DL_AMBIENT_CHANGED when
 * the light has moved far enough, for long enough, that the screen should
 * follow; a->ambient is then the new level.
 */
enum dl_ambient_event dl_ambient_sample(struct dl_ambient *a, const struct dl_response *r,
					double t, double lux);

/* The light level right now, robust against a single odd sample: the median of the last two seconds. */
double dl_ambient_recent(const struct dl_ambient *a);

/* Take lux as the level the screen is set for, as after an adjustment. */
void dl_ambient_anchor(struct dl_ambient *a, double lux);

/*
 * Move the learned offsets so the curve gives nits at lux. Only the two points
 * around lux move, each by at most DL_MAX_LEARN_STEP; points further out move
 * only as far as needed to keep the curve from falling as the light rises.
 * Returns true if the curve now passes through the adjustment.
 */
bool dl_learn(const struct dl_params *p, double offset[DL_POINTS], double lux, double nits);

/* Start a transition from the shown level to target (both nits) at time t. */
void dl_ramp_start(struct dl_ramp *r, const struct dl_params *p, double t, double shown,
		   double target);

/* The level a transition shows at time t, in nits; ends it when done. */
double dl_ramp_level(struct dl_ramp *r, const struct dl_params *p, double t);

/* Whether target is far enough from shown to be worth a transition. */
bool dl_worth_moving(const struct dl_params *p, double shown, double target);

/*
 * The controller: what the screen should show, given the light and the
 * changes others make. The daemon and the replay tests both drive it.
 */
enum dl_adjust {
	DL_ADJUST_LEARN,	/* the user picked this level: fit the curve to it */
	DL_ADJUST_HOLD,		/* someone else set it: keep it until the light changes a lot */
};

struct dl_controller {
	struct dl_params p;
	double offset[DL_POINTS];
	struct dl_ambient ambient;
	struct dl_ramp ramp;
	double shown;		/* nits the screen is at (or heading through), <= 0 if unknown */
	double target;		/* nits the curve gives for the light */
	double automatic;	/* the last level this controller set */
	bool hold;
	double hold_lux;
};

/* Light changes by this factor end a hold. */
#define DL_HOLD_RATIO 3.0

void dl_controller_init(struct dl_controller *c, const struct dl_params *p,
			const double offset[DL_POINTS], double shown);

/* New settings: keep the light and the level, follow the new curve. */
void dl_controller_set_params(struct dl_controller *c, double t, const struct dl_params *p,
			      const double offset[DL_POINTS]);

/* Forget the light, as after a wake or the lid opening: the next sample is taken as it is. */
void dl_controller_restart(struct dl_controller *c);

/* Feed a sensor sample. Returns true if a transition started. */
bool dl_controller_sample(struct dl_controller *c, double t, double lux);

/* The level to show at time t while a transition runs; false when there is none. */
bool dl_controller_level(struct dl_controller *c, double t, double *nits);

/* Someone set the level to nits. Returns true when the learned offsets changed. */
bool dl_controller_adjusted(struct dl_controller *c, double t, double nits, enum dl_adjust how);

#endif
