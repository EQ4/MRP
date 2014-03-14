//
//  TouchkeyVibratoMapping.h
//  touchkeys
//
//  Created by Andrew McPherson on 16/03/2013.
//  Copyright (c) 2013 Andrew McPherson. All rights reserved.
//

#ifndef __touchkeys__TouchkeyVibratoMapping__
#define __touchkeys__TouchkeyVibratoMapping__


#include <map>
#include <boost/bind.hpp>
#include "KeyTouchFrame.h"
#include "KeyPositionTracker.h"
#include "PianoKeyboard.h"
#include "Mapping.h"
#include "IIRFilter.h"

// This class handles the detection and mapping of vibrato gestures
// based on Touchkey data. It outputs MIDI or OSC messages that
// can be used to affect the pitch of the active note.

class TouchkeyVibratoMapping : public Mapping, public OscHandler {
private:
    // Useful constants for mapping MRP messages
    const int kDefaultMIDIChannel = 0;
    const int kDefaultFilterBufferLength = 30;
    
    const float kDefaultVibratoThresholdX = 0.05;
    const float kDefaultVibratoRatioX = 0.3;
    const float kDefaultVibratoThresholdY = 0.02;
    const float kDefaultVibratoRatioY = 0.8;
    const timestamp_diff_type kDefaultVibratoTimeout = microseconds_to_timestamp(400000); // 0.4s
    const float kDefaultVibratoPrescaler = 4.0;
    const float kDefaultVibratoRangeSemitones = 1.25;
    
    const timestamp_diff_type kZeroCrossingMinimumTime = microseconds_to_timestamp(50000); // 50ms
    const timestamp_diff_type kMinimumOnsetTime = microseconds_to_timestamp(30000); // 30ms
    const timestamp_diff_type kMaximumOnsetTime = microseconds_to_timestamp(300000); // 300ms
    const timestamp_diff_type kMinimumReleaseTime = microseconds_to_timestamp(30000); // 30ms
    const timestamp_diff_type kMaximumReleaseTime = microseconds_to_timestamp(300000); // 300ms
    
    enum {
        kStateInactive = 0,
        kStateSwitchingOn,
        kStateActive,
        kStateSwitchingOff
    };
    
public:
	// ***** Constructors *****
	
	// Default constructor, passing the buffer on which to trigger
	TouchkeyVibratoMapping(PianoKeyboard &keyboard, int noteNumber, Node<KeyTouchFrame>* touchBuffer,
               Node<key_position>* positionBuffer, KeyPositionTracker* positionTracker);
	
	// Copy constructor
	TouchkeyVibratoMapping(TouchkeyVibratoMapping const& obj);
    
    // ***** Destructor *****
    
    ~TouchkeyVibratoMapping();
	
    // ***** Modifiers *****
    
    // Enable mappings to be sent
    void engage();
    
    // Disable mappings from being sent
    void disengage();
	
    // Reset the state back initial values
	void reset();
    
    // Get or set the MIDI channel (0-15)
    /*int midiChannel() { return midiChannel_; }
    void setMIDIChannel(int ch) {
        if(ch >= 0 && ch < 16)
            midiChannel_ = ch;
    }*/
    
	// ***** Evaluators *****
    
    // OSC Handler Method: called by PianoKeyboard (or other OSC source)
	bool oscHandlerMethod(const char *path, const char *types, int numValues, lo_arg **values, void *data);
	
    // This method receives triggers whenever events occur in the touch data or the
    // continuous key position (state changes only). It alters the behavior and scheduling
    // of the mapping but does not itself send OSC messages
	void triggerReceived(TriggerSource* who, timestamp_type timestamp);
	
    // This method handles the OSC message transmission. It should be run in the Scheduler
    // thread provided by PianoKeyboard.
    timestamp_type performMapping();
    
private:
    // ***** Private Methods *****
    void changeStateSwitchingOn(timestamp_type timestamp);
    void changeStateActive(timestamp_type timestamp);
    void changeStateSwitchingOff(timestamp_type timestamp);
    void changeStateInactive(timestamp_type timestamp);

    void resetDetectionState();
    void clearBuffers();
    
    void sendVibratoMessage(float pitchBendSemitones);
    
	// ***** Member Variables *****
    
    bool noteIsOn_;                             // Whether the MIDI note is active or not
    int vibratoState_;                          // Whether a vibrato gesture is currently detected
    //int midiChannel_;                           // Channel on which to transmit MIDI messages
    
    timestamp_type rampBeginTime_;              // If in a switching state, when does the transition begin?
    float rampScaleValue_;                      // If in a switching state, what is the end point of the ramp?
    timestamp_diff_type rampLength_;            // If in a switching state, how long is the transition?
    float lastCalculatedRampValue_;             // Value of the ramp that was last calculated
    
    float onsetThresholdX_, onsetThresholdY_;   // Thresholds for detecting vibrato (first extremum)
    float onsetRatioX_, onsetRatioY_;           // Thresholds for detection vibrato (second extremum)
    timestamp_diff_type onsetTimeout_;          // Timeout between first and second extrema
    
    float onsetLocationX_, onsetLocationY_;     // Where the touch began at MIDI note on
    float lastX_, lastY_;                       // Where the touch was at the last frame we received
    int idOfCurrentTouch_;                      // Which touch ID we're currently following
    timestamp_type lastTimestamp_;              // When the last data point arrived
    Node<float>::size_type lastProcessedIndex_; // Index of the last filtered position sample we've handled
    
    timestamp_type lastZeroCrossingTimestamp_;  // Timestamp of the last zero crossing
    timestamp_diff_type lastZeroCrossingInterval_;   // Interval between the last two zero-crossings of filtered distance
    bool lastSampleWasPositive_;                // Whether the last sample was > 0
    
    bool foundFirstExtremum_;                   // Whether the first extremum has occurred
    float firstExtremumX_, firstExtremumY_;     // Where the first extremum occurred
    timestamp_type firstExtremumTimestamp_;     // Where the first extremum occurred
    timestamp_type lastExtremumTimestamp_;      // When the most recent extremum occurred
    
    float vibratoPrescaler_;                    // Parameter controlling prescaler before nonlinear scaling
    float vibratoRangeSemitones_;               // Amount of pitch bend in one direction at maximum
    
    float lastPitchBendSemitones_;              // The last pitch bend value we sent out
    
    Node<float> rawDistance_;                   // Distance from onset location
    IIRFilter<float> filteredDistance_;         // Bandpass filtered finger motion
};

#endif /* defined(__touchkeys__TouchkeyVibratoMapping__) */
