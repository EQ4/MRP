/*
 *  TimestampSynchronizer.cpp
 *  keycontrol_cocoa
 *
 *  Created by Andrew McPherson on 6/15/11.
 *  Copyright 2011 __MyCompanyName__. All rights reserved.
 *
 */

#include "TimestampSynchronizer.h"

// Constructor
TimestampSynchronizer::TimestampSynchronizer()
: history_(kTimestampSynchronizerHistoryLength), nominalSampleInterval_(0), currentSampleInterval_(0),
startingTimestamp_(0), startingClockTime_(boost::posix_time::microsec_clock::universal_time()), frameModulus_(0),
bufferLengthCounter_(0)
{
}

// Clear the accumulated timestamp history and reset the current
// value to its nominal "expected" value.  Also (re-)establish
// the relationship between system clock time and output timestamp.
// If multiple streams are to be synchronized, they should be
// initialized with the same values

void TimestampSynchronizer::initialize(boost::posix_time::ptime clockTime, 
									   timestamp_type startingTimestamp) {
	history_.clear();
	currentSampleInterval_ = nominalSampleInterval_;
	startingClockTime_ = clockTime;
	startingTimestamp_ = startingTimestamp;
	
	//cout << "initialize(): startingTimestamp = " << startingTimestamp_ << ", interval = " << nominalSampleInterval_ << endl;
}

// Given a frame number, calculate a current timestamp
timestamp_type TimestampSynchronizer::synchronizedTimestamp(int rawFrameNumber) {
	using namespace boost::posix_time;
	
	// Calculate the current system clock-related timestamp
	timestamp_type clockTime = startingTimestamp_ + ptime_to_timestamp(microsec_clock::universal_time() - startingClockTime_);	
	timestamp_type frameTime;

	// Retrieve the timestamp of the previous frame
	// Need at least 2 samples in the buffer for the calculations that follow
	if(history_.empty()) {
		frameTime = clockTime;
	}
	else if(history_.size() < 2) {
		// One sample in buffer: make sure the new sample is new before
		// storing it in the buffer.
		
		int lastFrame = history_.latest().first;
		
		frameTime = clockTime;
		
		if(lastFrame == rawFrameNumber) // Don't reprocess identical frames
			return frameTime;		
	}
	else {
		int totalHistoryFrames;		
		int lastFrame = history_.latest().first;
		frameTime = history_.latest().second;
		
		if(lastFrame == rawFrameNumber) // Don't reprocess identical frames
			return frameTime;
			
		if(frameModulus_ == 0) {
			// No modulus, just compare the raw frame number to the last frame number
			frameTime += currentSampleInterval_ * (timestamp_type)(rawFrameNumber - lastFrame);
			
			totalHistoryFrames = (history_.latest().first - history_.earliest().first);
			if(totalHistoryFrames <= 0) {
				cout << "Warning: TimestampSynchronizer history buffer has a difference of " << totalHistoryFrames << " frames.\n";
				cout << "Size = " << history_.size() << " first = " << history_.earliest().first << " last = " << history_.latest().first << endl;
				totalHistoryFrames = 1;
			}
		}
		else {
			// Use mod arithmetic to handle wraparounds in the frame number
			frameTime += currentSampleInterval_ * (timestamp_type)((rawFrameNumber + frameModulus_ - lastFrame) % frameModulus_);
			
			totalHistoryFrames = (history_.latest().first - history_.earliest().first + frameModulus_) % frameModulus_;
			if(totalHistoryFrames <= 0) {
				cout << "Warning: TimestampSynchronizer history buffer has a difference of " << totalHistoryFrames << " frames.\n";
				cout << "Size = " << history_.size() << " first = " << history_.earliest().first << " last = " << history_.latest().first << endl;

				totalHistoryFrames = 1;
			}			
		}
		
		// Recalculate the nominal sample interval by examining the difference in times
		// between first and last frames in the buffer.
		
		currentSampleInterval_ = (history_.latestTimestamp() - history_.earliestTimestamp()) / (timestamp_diff_type)totalHistoryFrames;
		
		// The frame time was just incremented by the current sample period.  Check whether
		// this puts the frame time ahead of the clock time.  Don't allow the frame time to get
		// ahead of the system clock (this will also push future frame timestamps back).
		
		if(frameTime > clockTime) {
			//cout << "CLIP " << 100.0 * (frameTime - clockTime) / currentSampleInterval_ << "%: frame=" << frameTime << " to clock=" << clockTime << endl;
			frameTime = clockTime;			
		}
		
		bufferLengthCounter_++;
		
		if(bufferLengthCounter_ >= kTimestampSynchronizerHistoryLength) {
			timestamp_diff_type currentLatency = clockTime - frameTime;
			timestamp_diff_type maxLatency = 0, minLatency = 1000000.0;
			
			Node<pair<int, timestamp_type> >::iterator it;
			
			for(it = history_.begin(); it != history_.end(); ++it) {
				timestamp_diff_type l = (it.timestamp() - it->second);
				if(l > maxLatency)
					maxLatency = l;
				if(l < minLatency)
					minLatency = l;
			}
			
			//cout << "frame " << rawFrameNumber << ": rate = " << currentSampleInterval_ << " clock = " << clockTime << " frame = " << frameTime << " latency = " 
			//	<< currentLatency << " max = " << maxLatency << " min = " << minLatency << endl;
			
			timestamp_diff_type targetMinLatency = (maxLatency - minLatency) * 2.0 / sqrt(kTimestampSynchronizerHistoryLength);
			
			/*if(minLatency > targetMinLatency) {
				cout << "ADDING " << 50.0 * (minLatency - targetMinLatency) / (currentSampleInterval_) << "%: (target " << targetMinLatency << ")\n";
				frameTime += (minLatency - targetMinLatency) / 2.0;
			}*/
			//frameTime += minLatency / 4.0;
			
			bufferLengthCounter_ = 0;
		}
	}
	
	// Insert the new frame time and clock times into the buffer
	history_.insert(pair<int, timestamp_type>(rawFrameNumber, frameTime), clockTime);

	// The timestamp we return is associated with the frame, not the clock (which is potentially much
	// higher jitter)
	return frameTime;
}
