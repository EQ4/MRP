/*
 *  Trigger.cpp
 *  keycontrol
 *
 *  Created by Andrew McPherson on 10/15/10.
 *  Copyright 2010 __MyCompanyName__. All rights reserved.
 *
 */

#include "Trigger.h"

#undef DEBUG_TRIGGERS

void TriggerSource::sendTrigger(timestamp_type timestamp) {
#ifdef DEBUG_TRIGGERS
    std::cerr << "sendTrigger (" << this << ")\n";
#endif
    
    if(triggerDestinationsModified_) {
        triggerSourceMutex_.lock();
        processAddRemoveQueue();
        triggerSourceMutex_.unlock();
    }
    
    std::set<TriggerDestination*>::iterator it = triggerDestinations_.begin();
	TriggerDestination* target;
	while(it != triggerDestinations_.end()) {	// Advance the iterator before sending the trigger
		target = *it;							// in case the triggerReceived routine causes the object to unregister
#ifdef DEBUG_TRIGGERS
        std::cerr << " --> " << target << std::endl;
#endif
		target->triggerReceived(this, timestamp);
        it++;
	}
}

void TriggerSource::addTriggerDestination(TriggerDestination* dest) { 
#ifdef DEBUG_TRIGGERS
    std::cerr << "addTriggerDestination (" << this << "): " << dest << "\n";
#endif
	if(dest == 0 || (void*)dest == (void*)this)
		return;
	triggerSourceMutex_.lock();
    // Make sure this trigger isn't already present
    if(triggerDestinations_.count(dest) == 0) {
        triggersToAdd_.insert(dest);
        triggerDestinationsModified_ = true;
    }
    // If the trigger is also slated to be removed, cancel that request
    if(triggersToRemove_.count(dest) != 0)
        triggersToRemove_.erase(dest);
	triggerSourceMutex_.unlock();
}

void TriggerSource::removeTriggerDestination(TriggerDestination* dest) {
#ifdef DEBUG_TRIGGERS
    std::cerr << "removeTriggerDestination (" << this << "): " << dest << "\n";
#endif
	triggerSourceMutex_.lock();
    // Check whether this trigger is actually present
    if(triggerDestinations_.count(dest) != 0) {
        triggersToRemove_.insert(dest);
        triggerDestinationsModified_ = true;
    }
    // If the trigger is also slated to be added, cancel that request
    if(triggersToAdd_.count(dest) != 0)
        triggersToAdd_.erase(dest);
	triggerSourceMutex_.unlock();
}	

void TriggerSource::clearTriggerDestinations() {
#ifdef DEBUG_TRIGGERS
    std::cerr << "clearTriggerDestinations (" << this << ")\n";
#endif
	triggerSourceMutex_.lock();
    processAddRemoveQueue();
	std::set<TriggerDestination*>::iterator it;
	for(it = triggerDestinations_.begin(); it != triggerDestinations_.end(); ++it)
		(*it)->triggerSourceDeleted(this);		
	triggerDestinations_.clear(); 
	triggerSourceMutex_.unlock();
}

// Process everything in the add and remove groups and transfer them
// into the main set of trigger destinations. Do this with mutex locked.
void TriggerSource::processAddRemoveQueue() {
#ifdef DEBUG_TRIGGERS
    std::cerr << "processAddRemoveQueue (" << this << ")\n";
#endif
    std::set<TriggerDestination*>::iterator it;
    for(it = triggersToAdd_.begin(); it != triggersToAdd_.end(); ++it) {
        triggerDestinations_.insert(*it);
#ifdef DEBUG_TRIGGERS
        std::cerr << " --> added " << *it << std::endl;
#endif
    }
    for(it = triggersToRemove_.begin(); it != triggersToRemove_.end(); ++it) {
        triggerDestinations_.erase(*it);
#ifdef DEBUG_TRIGGERS
        std::cerr << " --> removed " << *it << std::endl;
#endif
    }
    triggersToAdd_.clear();
    triggersToRemove_.clear();
    triggerDestinationsModified_ = false;
}