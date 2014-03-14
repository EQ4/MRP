//
//  Mapping.cpp
//  touchkeys
//
//  Created by Andrew McPherson on 04/02/2013.
//  Copyright (c) 2013 Andrew McPherson. All rights reserved.
//

#include "Mapping.h"

// Main constructor takes references/pointers from objects which keep track
// of touch location, continuous key position and the state detected from that
// position. The PianoKeyboard object is strictly required as it gives access to
// Scheduler and OSC methods. The others are optional since any given system may
// contain only one of continuous key position or touch sensitivity
Mapping::Mapping(PianoKeyboard &keyboard, int noteNumber, Node<KeyTouchFrame>* touchBuffer,
                       Node<key_position>* positionBuffer, KeyPositionTracker* positionTracker)
: keyboard_(keyboard), noteNumber_(noteNumber), touchBuffer_(touchBuffer),
positionBuffer_(positionBuffer), positionTracker_(positionTracker), engaged_(false),
nextScheduledTimestamp_(0), updateInterval_(kDefaultUpdateInterval)
{
    // Create a statically bound call to the performMapping() method that
    // we use each time we schedule a new mapping
    mappingAction_ = boost::bind(&Mapping::performMapping, this);
}

// Copy constructor
Mapping::Mapping(Mapping const& obj) : keyboard_(obj.keyboard_), noteNumber_(obj.noteNumber_),
touchBuffer_(obj.touchBuffer_), positionBuffer_(obj.positionBuffer_), positionTracker_(obj.positionTracker_),
engaged_(obj.engaged_), nextScheduledTimestamp_(obj.nextScheduledTimestamp_),
updateInterval_(obj.updateInterval_)
{
    // Create a statically bound call to the performMapping() method that
    // we use each time we schedule a new mapping
    mappingAction_ = boost::bind(&Mapping::performMapping, this);
    
    // Register ourself if already engaged since the scheduler won't have a copy of this object
    if(engaged_)
        keyboard_.scheduleEvent(this, mappingAction_, keyboard_.schedulerCurrentTimestamp());
}

// Destructor. IMPORTANT NOTE: any derived class of Mapping() needs to call disengage() in its
// own destructor. It can't be called here, or there is a risk that the scheduled action will be
// called between the destruction of the derived class and the destruction of Mapping. This
// will result in a pure virtual function call and a crash.
Mapping::~Mapping() {
    //std::cerr << "~Mapping(): " << this << std::endl;
}

// Turn on mapping of data. Register for a callback and set a flag so
// we continue to receive updates
void Mapping::engage() {
    engaged_ = true;
    
    // Register for trigger updates from touch data and state updates if either one is present.
    // Don't register for triggers on each new key sample
    if(touchBuffer_ != 0)
        registerForTrigger(touchBuffer_);
    if(positionTracker_ != 0)
        registerForTrigger(positionTracker_);
    nextScheduledTimestamp_ = keyboard_.schedulerCurrentTimestamp();
    keyboard_.scheduleEvent(this, mappingAction_, nextScheduledTimestamp_);
}

// Turn off mapping of data. Remove our callback from the scheduler
void Mapping::disengage() {
    //std::cerr << "Mapping::disengage(): " << this << std::endl;
    
    engaged_ = false;
    keyboard_.unscheduleEvent(this/*, nextScheduledTimestamp_*/);
    
    // Unregister for updates from touch data
    if(touchBuffer_ != 0)
        unregisterForTrigger(touchBuffer_);
    if(positionTracker_ != 0)
        unregisterForTrigger(positionTracker_);
    //std::cerr << "Mapping::disengage(): done\n";
}

// Reset state back to defaults
void Mapping::reset() {
    updateInterval_ = kDefaultUpdateInterval;
}
