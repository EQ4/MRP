//
//  MIDIKeyPositionMapping.h
//  touchkeys
//
//  Created by Andrew McPherson on 11/02/2013.
//  Copyright (c) 2013 Andrew McPherson. All rights reserved.
//

#ifndef __touchkeys__MIDIKeyPositionMapping__
#define __touchkeys__MIDIKeyPositionMapping__

#include <map>
#include <boost/bind.hpp>
#include "KeyTouchFrame.h"
#include "KeyPositionTracker.h"
#include "PianoKeyboard.h"
#include "Mapping.h"

// This class handles the mapping from continuous key position to
// MIDI messages: note on, note off, aftertouch.

class MIDIKeyPositionMapping : public Mapping {
private:
    const int kDefaultMIDIChannel = 0;
    const float kDefaultAftertouchScaler = 127.0 / 0.03;   // Default aftertouch sensitivity: MIDI 127 = 0.03
    const float kMinimumAftertouchPosition = 0.99;         // Position at which aftertouch messages start
    const float kDefaultPercussivenessScaler = 1.0 / 300.0; // Default scaler from percussiveness feature to MIDI
    const key_velocity kPianoKeyVelocityForMaxMIDI = scale_key_velocity(40.0);           // Press velocity for MIDI 127
    const key_velocity kPianoKeyReleaseVelocityForMaxMIDI = scale_key_velocity(-50.0);   // Release velocity for MIDI 127
    
public:
	// ***** Constructors *****
	
	// Default constructor, passing the buffer on which to trigger
	MIDIKeyPositionMapping(PianoKeyboard &keyboard, int noteNumber, Node<KeyTouchFrame>* touchBuffer,
               Node<key_position>* positionBuffer, KeyPositionTracker* positionTracker);
	
	// Copy constructor
	MIDIKeyPositionMapping(MIDIKeyPositionMapping const& obj);
    
    // ***** Destructor *****
    
    ~MIDIKeyPositionMapping();
	
    // ***** Modifiers *****
    
    // Disable mappings from being sent
    void disengage();
	
    // Reset the state back initial values
	void reset();
    
    // Set the aftertouch sensitivity on continuous key position
    // 0 means no aftertouch, 1 means default sensitivity, upward
    // from there
    void setAftertouchSensitivity(float sensitivity);
    
    // Get or set the MIDI channel (0-15)
    int midiChannel() { return midiChannel_; }
    void setMIDIChannel(int ch) {
        if(ch >= 0 && ch < 16)
            midiChannel_ = ch;
    }
    
    // Get or set the MIDI channel for percussiveness messages
    int percussivenessMIDIChannel() { return midiPercussivenessChannel_; }
    void setPercussivenessMIDIChannel(int ch) {
        if(ch >= 0 && ch < 16)
            midiPercussivenessChannel_ = ch;
        else
            midiPercussivenessChannel_ = -1;
    }
    void disableMIDIPercussiveness() { midiPercussivenessChannel_ = -1; }
    
	// ***** Evaluators *****
	
    // This method receives triggers whenever events occur in the touch data or the
    // continuous key position (state changes only). It alters the behavior and scheduling
    // of the mapping but does not itself send OSC messages
	void triggerReceived(TriggerSource* who, timestamp_type timestamp);
	
    // This method handles the OSC message transmission. It should be run in the Scheduler
    // thread provided by PianoKeyboard.
    timestamp_type performMapping();
    
private:
    // ***** Private Methods *****

    void generateMidiNoteOn();
    void generateMidiNoteOff();
    void generateMidiPercussivenessNoteOn();
    
	// ***** Member Variables *****
    
    bool noteIsOn_;                             // Whether the MIDI note is active or not
    float aftertouchScaler_;                    // Scaler which affects aftertouch sensitivity
    int midiChannel_;                           // Channel on which to transmit MIDI messages
    int lastAftertouchValue_;                   // Value of the last aftertouch message
    int midiPercussivenessChannel_;             // Whether and where to transmit percussiveness messages
};


#endif /* defined(__touchkeys__MIDIKeyPositionMapping__) */
