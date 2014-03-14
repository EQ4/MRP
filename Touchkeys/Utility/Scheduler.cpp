/*
 *  Scheduler.cpp
 *  keycontrol
 *
 *  Created by Andrew McPherson on 10/20/10.
 *  Copyright 2010 __MyCompanyName__. All rights reserved.
 *
 */

#include "Scheduler.h"
#include <boost/date_time.hpp>
#undef DEBUG_SCHEDULER

using namespace boost::posix_time;
using std::cout;

// Start the thread handling the scheduling.  Pass it an initial timestamp.
void Scheduler::start(timestamp_type where) {
	if(isRunning_)
		return;
	thread_ = boost::thread(Scheduler::staticRunLoop, this, where);
}

// Stop the scheduler thread if it is currently running.  Events will remain
// in the queue unless explicitly cleared.
void Scheduler::stop() {
	if(!isRunning_)
		return;
	thread_.interrupt();
	thread_.join();
	isRunning_ = false;
}

// Return the current timestamp, relative to this class's start time.
timestamp_type Scheduler::currentTimestamp() {
	if(!isRunning_)
		return 0;
	return ptime_to_timestamp(microsec_clock::universal_time() - startTime_);
}

// Schedule a new event
void Scheduler::schedule(void *who, action func, timestamp_type timestamp) {
    bool newActionWillComeFirst = false;
    
	eventMutex_.lock();
    // Check if this timestamp will become the next thing in the queue
    if(events_.empty())
        newActionWillComeFirst = true;
    else if(timestamp < events_.begin()->first)
        newActionWillComeFirst = true;
    events_.insert(std::pair<timestamp_type,std::pair<void*, action> >
					(timestamp, std::pair<void*, action>(who, func)));
	eventMutex_.unlock();
	
	// Tell the thread to wake up and recheck its status if the
    // time of the next event has changed
    if(newActionWillComeFirst)
        eventCondition_.notify_all();
}

// Remove an existing event
void Scheduler::unschedule(void *who, timestamp_type timestamp) {
#ifdef DEBUG_SCHEDULER
    std::cerr << "Scheduler::unschedule: " << who << ", " << timestamp << std::endl;
#endif
    
	eventMutex_.lock();
	// Find all events with this timestamp, and remove only the ones matching the given source
	std::multimap<timestamp_type, std::pair<void*, action> >::iterator it;
    
    if(timestamp == 0) {
        // Remove all events from this source
        it = events_.begin();
        while(it != events_.end()) {
#ifdef DEBUG_SCHEDULER
            std::cerr << "| (" << it->first << ", " << it->second.first << ")\n";
#endif
            if(it->second.first == who) {
#ifdef DEBUG_SCHEDULER
                std::cerr << "--> erased " << it->first << ", " << it->second.first << ")\n";
#endif
                events_.erase(it++);
            }
            else
                it++;
        }
    }
    else {
        // Remove only a specific event from this source with the given timestmap
        it = events_.find(timestamp);
        while(it != events_.end()) {
            if(it->second.first == who) {
#ifdef DEBUG_SCHEDULER
                std::cerr << "--> erased " << it->first << ", " << it->second.first << ")\n";
#endif
                events_.erase(it++);
            }
            else
                it++;
        }
    }
	eventMutex_.unlock();
#ifdef DEBUG_SCHEDULER
    std::cerr << "Scheduler::unschedule: done\n";
#endif
	// No need to wake up the thread...
}

// Clear all events from the queue
void Scheduler::clear() {
	eventMutex_.lock();
	events_.clear();
	eventMutex_.unlock();
	
	// No need to signal the condition variable.  If the thread is waiting, it can keep waiting.
}

// This function runs in its own thread (this->thread_).  It looks for the next event
// in the queue.  When its time arrives, the event is executed and removed from the queue.
// When the queue is empty, or the next event has not arrived yet, the thread sleeps.

void Scheduler::runLoop(timestamp_type starting_timestamp) {
	
	// Find the start time, against which our offsets will be measured.
	startTime_ = microsec_clock::universal_time();	
	isRunning_ = true;
	
	try {
		// Start with the mutex locked.  The wait() methods will unlock it.
		//eventMutex_.lock();
		boost::unique_lock<boost::mutex> lock(eventMutex_);
		
		// This will run until the thread is interrupted (in the stop() method)
		// events_ is ordered by increasing timestamp, so the next event to execute is always the first item.
		while(true) {
			if(events_.empty())	{					// If there are no events in the queue, wait until we're signaled
				eventCondition_.wait(lock);	// that a new one comes in.  Unlock the mutex and wait.
			}
			else {
				timestamp_type t = events_.begin()->first;				// Find the timestamp of the first event
				ptime targetTime = startTime_ + timestamp_to_ptime(t);
				eventCondition_.timed_wait(lock, targetTime);	// Wait until that time arrives
			}
			
			// At this point, the mutex is locked.  We can change the contents of events_ without worrying about disrupting anything.
			
			if(events_.empty())				// Double check that we actually have an event to execute
				continue;
			if(currentTimestamp() < events_.begin()->first)
				continue;
            
            // Run the function that's stored, which takes no arguments and returns a timestamp
            // of the next time this particular function should run.
            std::multimap<timestamp_type, std::pair<void*, action> >::iterator it = events_.begin();
            action actionFunction = (it->second).second;
            timestamp_type testingTimestamp = it->first;
            void *who = it->second.first;
			timestamp_type timeOfNextEvent = actionFunction();
            
            // Remove the last event from the queue
            events_.erase(it);
			
            if(timeOfNextEvent > 0) {
                // Reschedule the same event for some (hopefully) future time.
                events_.insert(std::pair<timestamp_type,std::pair<void*, action> >
                               (timeOfNextEvent,
                                std::pair<void*, action>(who, actionFunction)));
            }
		}
	} catch(...) {				// When this thread is interrupted, it will generate an exception.  Make sure the mutex unlocks.
		eventMutex_.unlock();
	}
}