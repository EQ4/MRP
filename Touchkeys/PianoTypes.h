/*
 *  PianoTypes.h
 *  keycontrol
 *
 *  Created by Andrew McPherson on 1/26/11.
 *  Copyright 2011 __MyCompanyName__. All rights reserved.
 *
 */

#ifndef KEYCONTROL_PIANO_TYPES_H
#define KEYCONTROL_PIANO_TYPES_H

#include "Types.h"

#undef FIXED_POINT_PIANO_SAMPLES

// Data types.  Allow for floating-point (more flexible) or fixed-point (faster) arithmetic
// on piano key positions
#ifdef FIXED_POINT_PIANO_SAMPLES
typedef int key_position;
typedef int key_velocity;
#define scale_key_position(x) 4096*(key_position)(x)
#define key_position_to_float(x) ((float)x/4096.0)
#define key_abs(x) abs(x)
#define calculate_key_velocity(dpos, dt) (key_velocity)((65536*dpos)/(key_position)dt)
#define scale_key_velocity(x) (65536/4096)*(key_velocity)(x) // FIXME: TEST THIS!
#else
typedef float key_position;
typedef float key_velocity;
#define scale_key_position(x) (key_position)(x)
#define key_position_to_float(x) (x)
#define key_abs(x) fabsf(x)
#define calculate_key_velocity(dpos, dt) (key_velocity)(dpos/(key_position)dt)
#define scale_key_velocity(x) (key_velocity)(x)
#endif /* FIXED_POINT_PIANO_SAMPLES */

#endif /* KEYCONTROL_PIANO_TYPES_H */