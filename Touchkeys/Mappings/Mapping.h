//
//  Mapping.h
//  touchkeys
//
//  Created by Andrew McPherson on 04/02/2013.
//  Copyright (c) 2013 Andrew McPherson. All rights reserved.
//

#ifndef touchkeys_Mapping_h
#define touchkeys_Mapping_h


#include <map>
#include <boost/bind.hpp>
#include "KeyTouchFrame.h"
#include "KeyPositionTracker.h"
#include "PianoKeyboard.h"

// This virtual base class defines a mapping from keyboard data to OSC or
// other output information. Specific behavior is implemented by subclasses.

class Mapping : public TriggerDestination {
protected:
    // Default frequency of mapping data, in the absence of other triggers
    const timestamp_diff_type kDefaultUpdateInterval = microseconds_to_timestamp(5500);
    
public:
	// ***** Constructors *****
	
	// Default constructor, passing the buffer on which to trigger
    Mapping(PianoKeyboard &keyboard, int noteNumber, Node<KeyTouchFrame>* touchBuffer,
                       Node<key_position>* positionBuffer, KeyPositionTracker* positionTracker);
	
	// Copy constructor
    Mapping(Mapping const& obj);
    
    // ***** Destructor *****
    
    virtual ~Mapping();
	
    // ***** Modifiers *****
    
    // Enable mappings to be sent
    virtual void engage();
    
    // Disable mappings from being sent
    virtual void disengage();
	
    // Reset the state back initial values
	virtual void reset();
    
    // Set the interval between mapping actions
    virtual void setUpdateInterval(timestamp_diff_type interval) {
        if(interval <= 0)
            return;
        updateInterval_ = interval;
    }

	// ***** Evaluators *****
	// These are the main mapping functions, and they need to be implemented
    // specifically in any subclass.
    
    // This method receives triggers whenever events occur in the touch data or the
    // continuous key position (state changes only).
	virtual void triggerReceived(TriggerSource* who, timestamp_type timestamp) = 0;
	
    // This method is run periodically the Scheduler provided by PianoKeyboard and
    // handles the actual work of performing the mapping.
    virtual timestamp_type performMapping() = 0;
    
protected:
    
	// ***** Member Variables *****
	
    PianoKeyboard& keyboard_;                   // Reference to the main keyboard controller
    int noteNumber_;                            // MIDI note number for this key
    Node<KeyTouchFrame>* touchBuffer_;          // Key touch location history
	Node<key_position>* positionBuffer_;		// Raw key position data
    KeyPositionTracker* positionTracker_;       // Object which manages states of key
    
    bool engaged_;                              // Whether we're actively mapping
    timestamp_diff_type updateInterval_;        // How long between mapping calls
    timestamp_type nextScheduledTimestamp_;     // When we've asked for the next callback
    Scheduler::action mappingAction_;           // Action function which calls performMapping()
};


#endif
