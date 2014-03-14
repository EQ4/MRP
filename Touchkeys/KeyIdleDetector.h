/*
 *  KeyIdleDetector.h
 *  keycontrol
 *
 *  Created by Andrew McPherson on 11/2/10.
 *  Copyright 2010 __MyCompanyName__. All rights reserved.
 *
 */

#ifndef KEYCONTROL_KEYIDLEDETECTOR_H
#define KEYCONTROL_KEYIDLEDETECTOR_H

#include "Node.h"
#include "Accumulator.h"
//#include "Trigger.h"
//#include "PianoKeyboard.h"
#include "PianoTypes.h"

#define kKeyIdleNumSamples 10
#define kDefaultKeyIdleThreshold (scale_key_position(0.05))

// Three states of idle detector
enum {
	kIdleDetectorIdle = 0,
	kIdleDetectorActive = 1,
	kIdleDetectorUnknown = 2
};

/*
 * KeyIdleDetector
 *
 * A Filter that looks for whether the key position has been flat over time, or is changing.
 * Uses this information to detect when a key has begun to move.
 *
 * This class contains a second Filter object, operating on the same data source, which is used
 * to maintain a running sum of the last N values.  The running sum is converted to an average
 * value, and the maximum deviation from the average is calculated.
 *
 */

class KeyIdleDetector : public Node<int> {
public:
	// ***** Constructors *****
	
	// Default constructor, taking an input and thresholds (position and timing) at which to detect "not idle"
	KeyIdleDetector(capacity_type capacity, Node<key_position>& keyBuffer, key_position positionThreshold, 
					key_position activityThreshold, int counterThreshold);
	
	// Copy constructor
	KeyIdleDetector(KeyIdleDetector const& obj);
	
	// ***** State Access *****
	
	// Determine whether the key is currently idle or not.
	int idleState() { return idleState_; }
	
	// Set the threshold at which a key is determined to be idle or not.
	key_position activityThreshold() { return activityThreshold_; }
	key_position positionThreshold() { return positionThreshold_; }
	void setActivityThreshold(key_position thresh) { activityThreshold_ = thresh; }
	void setPositionThreshold(key_position thresh) { positionThreshold_ = thresh; }
	
	// ***** Modifiers *****
	
	void clear();
	
	// ***** Evaluator *****
	
	// This method actually handles the quantification of key activity.  When it
	// exceeds a preset threshold, it sends a trigger
	
	void triggerReceived(TriggerSource* who, timestamp_type timestamp);
	
	// ***** Member Variables *****
	
	Node<key_position>& keyBuffer_;								// Raw key position data	
	Accumulator<key_position, kKeyIdleNumSamples> accumulator_;	// This class accumulates the last N key samples (to find an average)
	
    key_position keyIdleThreshold_;                             // Position below which we assume key is staying idle
    
	key_position activityThreshold_;							// How much key motion should take place to make key active
	key_position positionThreshold_;							// Position below which key can return to idle
	int numberOfFramesWithoutActivity_;                         // For how many samples have we been below the idle threshold?
    int noActivityCounterThreshold_;
	int idleState_;												// Currently idle?

};
 

#endif /* KEYCONTROL_KEYIDLEDETECTOR_H */