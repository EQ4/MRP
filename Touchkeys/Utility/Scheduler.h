/*
 *  Scheduler.h
 *  keycontrol
 *
 *  Created by Andrew McPherson on 10/20/10.
 *  Copyright 2010 __MyCompanyName__. All rights reserved.
 *
 */

#ifndef KEYCONTROL_SCHEDULER_H
#define KEYCONTROL_SCHEDULER_H

#include <iostream>
#include <map>
#include <boost/thread.hpp>
#include <boost/bind.hpp>
#include <boost/function.hpp>
#include "Types.h"

/*
 * Scheduler
 *
 * This class allows function calls to be scheduled for arbitrary points in the future.
 * It maintains a list of future events, ordered by timestamp.  A dedicated thread scans the
 * list, and when it is time for an event to occur, the thread wakes up, executes it, deletes
 * it from the list, and goes back to sleep.
 */

class Scheduler {
public:	
	typedef boost::function<timestamp_type ()> action;
	
public:	
	// ***** Constructor *****
	//
	// Note: This class is not copy-constructable.
	
	Scheduler() : isRunning_(false) {}
	
	// ***** Destructor *****
	
	~Scheduler() { stop(); }
	
	// ***** Timer Methods *****
	//
	// These start and stop the thread that handles the scheduling of events.
	
	void start(timestamp_type where = 0);
	void stop();
	
	bool isRunning() { return isRunning_; }
	timestamp_type currentTimestamp();
	
	// ***** Event Management Methods *****
	//
	// This interface provides the ability to schedule and unschedule events for
	// future times.
	
	void schedule(void *who, action func, timestamp_type timestamp);
	void unschedule(void *who, timestamp_type timestamp = 0);
	void clear();
	
	static void staticRunLoop(Scheduler* sch, timestamp_type starting_timestamp) { sch->runLoop(starting_timestamp); }
	
private:
	void runLoop(timestamp_type starting_timestamp);

	// These variables keep track of the status of the separate thread running the events
	boost::thread thread_;
	boost::condition_variable eventCondition_;
	boost::mutex eventMutex_;
	bool isRunning_;
	
	// Collection of future events to execute
	boost::posix_time::ptime startTime_;
	std::multimap<timestamp_type, std::pair<void*, action> > events_;
};


#endif /* KEYCONTROL_SCHEDULER_H */