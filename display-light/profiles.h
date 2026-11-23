/* SPDX-License-Identifier: MIT */
/*
 * The built-in brightness profiles and response settings, shared by the
 * service and the settings page (plain C, includable from C++).
 */
#ifndef DISPLAY_LIGHT_PROFILES_H
#define DISPLAY_LIGHT_PROFILES_H

/* The ambient levels the curve is given at, in lux: dark room, dim room, indoor lighting, bright indoor, daylight. */
#define DL_POINT_LUX 0, 10, 100, 500, 2000

/*
 * Nits at those levels. Balanced keeps a dark room around 25 nits, an evenly
 * lit room around 170 and reaches 500 in daylight; the other two sit about a
 * third of the perceptual range lower and higher. Levels above a panel's
 * maximum are capped to it.
 *
 * X(name, nits at each point)
 */
#define DL_PROFILES(X)                          \
	X(dimmer, 12, 30, 100, 210, 380)        \
	X(balanced, 25, 60, 170, 320, 500)      \
	X(brighter, 50, 110, 270, 440, 600)

/*
 * How readily the screen follows the light. The light has to move by the
 * given fraction (up, down) and stay there for the given time (s); the
 * transition then takes the given time (s). Dimming waits and moves more
 * slowly than brightening, since a screen that is briefly too bright is less
 * of a bother than one that dips whenever a shadow passes.
 *
 * X(name, up, down, wait up, wait down, time up, time down)
 */
#define DL_RESPONSES(X)                                 \
	X(calm, 0.60, 0.70, 5, 15, 3, 10)               \
	X(balanced, 0.35, 0.40, 3, 8, 2, 6)             \
	X(quick, 0.15, 0.20, 1, 3, 1, 3)

#endif
