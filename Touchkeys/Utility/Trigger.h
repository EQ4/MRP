/*
 *  Trigger.h
 *  keycontrol
 *
 *  Created by Andrew McPherson on 10/14/10.
 *  Copyright 2010 __MyCompanyName__. All rights reserved.
 *
 */

#ifndef KEYCONTROL_TRIGGER_H
#define KEYCONTROL_TRIGGER_H

#include <iostream>
#include <set>
#include <boost/thread.hpp>
#include "Types.h"

class TriggerDestination;

/*
 * TriggerSource
 *
 * Provides a set of routines for an object that sends triggers with an associated timestamp.  All Node
 * objects inherit from Trigger, but other objects may use these routines as well.
 */

class TriggerSource {
	friend class TriggerDestination;
protected:
	// Send a trigger event out to all our registered listeners.  The type of the accompanying
	// data will be set by the template of the subclass.
	void sendTrigger(timestamp_type timestamp);
	
public:
	// ***** Constructor *****
	
	TriggerSource() {}	// No instantiating this class directly!
	
	// ***** Destructor *****
	
	~TriggerSource() { clearTriggerDestinations(); }	
	
	// ***** Connection Management *****
	
	bool hasTriggerDestinations() { return triggerDestinations_.size() > 0; }

private:
	// For internal use or use by friend class NodeBase only
	
	// These methods manage the list of objects to whom the triggers should be sent.  All objects
	// will inherit from the Triggerable base class.  These shouldn't be called by the user directly;
	// rather, they're called by Triggerable when it registers itself.
	
	void addTriggerDestination(TriggerDestination* dest);
	void removeTriggerDestination(TriggerDestination* dest);
	void clearTriggerDestinations();
    
    // When sources are added or removed, they are first stored in separate locations to be updated
    // prior to each new call of sendTrigger(). This way, destinations which are updated from functions
    // called from sendTrigger() do not render the set inconsistent in the middle.
    void processAddRemoveQueue();
	
private:
	std::set<TriggerDestination*> triggerDestinations_;
    std::set<TriggerDestination*> triggersToAdd_;
    std::set<TriggerDestination*> triggersToRemove_;
    bool triggerDestinationsModified_;
	boost::mutex triggerSourceMutex_;
};

/*
 * TriggerDestination
 *
 * This class accepts a Trigger event.  Designed to be inherited by more complex objects.
 */

class TriggerDestination {
	friend class TriggerSource;
public:
	// ***** Constructors *****
	
	TriggerDestination() {}
	TriggerDestination(TriggerDestination const& obj) : registeredTriggerSources_(obj.registeredTriggerSources_) {}
	
	// This defines what we actually do when a trigger is received.  It should be implemented
	// by the subclass.
	virtual void triggerReceived(TriggerSource* who, timestamp_type timestamp) { /*std::cout << "     received this = " << this << " who = " << who << std::endl;*/ }
	
	// These methods register and unregister sources of triggers.
	
	void registerForTrigger(TriggerSource* src) {
		//std::cout<<"registerForTrigger: this = " << this << " src = " << src << std::endl;
		
		if(src == 0 || (void*)src == (void*)this)
			return;
		triggerDestMutex_.lock();
		src->addTriggerDestination(this);
		registeredTriggerSources_.insert(src);
		triggerDestMutex_.unlock();
	}
	
	void unregisterForTrigger(TriggerSource* src) {
		if(src == 0 || (void*)src == (void*)this)
			return;
		triggerDestMutex_.lock();
		src->removeTriggerDestination(this);
		registeredTriggerSources_.erase(src);
		triggerDestMutex_.unlock();
	}
	
	void clearTriggers() {
		triggerDestMutex_.lock();
		std::set<TriggerSource*>::iterator it;
		for(it = registeredTriggerSources_.begin(); it != registeredTriggerSources_.end(); it++)
			(*it)->removeTriggerDestination(this);
		registeredTriggerSources_.clear();
		triggerDestMutex_.unlock();
	}
	
protected:
	// This method is called by a TriggerBase object when it is deleted, so that we know not
	// to contact it later when this object is deleted.  This is different than unregisterForTrigger()
	// because it only removes the reference and does not call the TriggerBase object (which would create
	// an infinite loop).
	
	void triggerSourceDeleted(TriggerSource* src) { registeredTriggerSources_.erase(src); }
	
public:
	// ***** Destructor *****
	//
	// Remove all trigger sources before this object goes away
	
	~TriggerDestination() { clearTriggers(); }
	
private:
	// Keep an internal registry of who we've asked to send us triggers.  It's important to keep
	// a list of these so that when this object is destroyed, all triggers are automatically unregistered.
	std::set<TriggerSource*> registeredTriggerSources_;
	boost::mutex triggerDestMutex_;
};



#endif /* KEYCONTROL_TRIGGER_H */