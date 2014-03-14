/*
 *  KeyTouchFrame.h
 *  keycontrol_cocoa
 *
 *  Created by Andrew McPherson on 6/14/11.
 *  Copyright 2011 __MyCompanyName__. All rights reserved.
 *
 */

#ifndef KEY_TOUCH_FRAME_H
#define KEY_TOUCH_FRAME_H

#define kWhiteFrontBackCutoff (6.5/19.0)	// Border between 2- and 1-dimensional sensing regions

// This class holds one frame of key touch data, both raw values and unique ID numbers
// that help connect one frame to the next

class KeyTouchFrame {
public:
	KeyTouchFrame() : count(0), locH(-1.0), nextId(0), white(true) {
		for(int i = 0; i < 3; i++) {
			ids[i] = -1;
			locs[i] = -1.0;
			sizes[i] = 0.0;
		}
	}
	
	KeyTouchFrame(int newCount, float* newLocs, float *newSizes, float newLocH, bool newWhite)
	: count(newCount), locH(newLocH), white(newWhite), nextId(0) {
		for(int i = 0; i < count; i++) {
			ids[i] = -1;
			locs[i] = newLocs[i];
			sizes[i] = newSizes[i];
		}
		for(int i = count; i < 3; i++) {
			ids[i] = -1;
			locs[i] = -1.0;
			sizes[i] = 0.0;
		}
	}
	
	KeyTouchFrame(const KeyTouchFrame& copy) : count(copy.count), locH(copy.locH), nextId(copy.nextId), white(copy.white) {
		for(int i = 0; i < 3; i++) {
			ids[i] = copy.ids[i];
			locs[i] = copy.locs[i];
			sizes[i] = copy.sizes[i];
		}
	}
	
	// Horizontal location only makes sense in the front part of the key.  Return the
	// value if applicable, otherwise -1.
	float horizontal(int index) const {
		//if(!white)
		//	return -1.0;
		if(index >= count || index > 2 || index < 0)
			return -1.0;
		//if(locs[index] < kWhiteFrontBackCutoff) // FIXME: need better hardware-dependent solution to this
			return locH;
		//return -1.0;
	}
	
	int count;			// Number of active touches (0-3)
	int ids[3];			// Unique ID numbers for current touches
	float locs[3];		// Vertical location of current touches
	float sizes[3];		// Contact area of current touches
	float locH;			// Horizontal location (white keys only)
	int nextId;			// ID number to be used for the next new touch added
	bool white;			// Whether this is a white key
};

#endif /* KEY_TOUCH_FRAME_H */