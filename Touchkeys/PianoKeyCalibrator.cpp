/*
 *  PianoKeyCalibrator.cpp
 *  keycontrol
 *
 *  Created by Andrew McPherson on 12/18/12.
 *  Copyright 2012 __MyCompanyName__. All rights reserved.
 *
 */

#include "PianoKeyCalibrator.h"

// Constructor
PianoKeyCalibrator::PianoKeyCalibrator(bool pressValueGoesDown, key_position* warpTable)
: status_(kPianoKeyNotCalibrated), prevStatus_(kPianoKeyNotCalibrated), history_(0),
  pressValueGoesDown_(pressValueGoesDown), warpTable_(warpTable) {}

// Destructor
PianoKeyCalibrator::~PianoKeyCalibrator() {
    if(history_ != 0)
        delete history_;
    
	// warpTable_ is passed in externally-- don't delete it
}

// Produce the calibrated value for a raw sample
key_position PianoKeyCalibrator::evaluate(int rawValue) {
	key_position calibratedValue, calibratedValueDenominator;

	calibrationMutex_.lock();
	
	switch(status_) {
		case kPianoKeyCalibrated:
			if(missing_value<int>::isMissing(quiescent_) ||
			   missing_value<int>::isMissing(press_)) {
				calibrationMutex_.unlock();
				return missing_value<key_position>::missing();
			}
			
			// Do the calculation either in integer or floating-point arithmetic
			calibratedValueDenominator = (key_position)(press_ - quiescent_);
			
			// Prevent divide-by-0 errors
			if(calibratedValueDenominator == 0)
				calibratedValue = missing_value<key_position>::missing();
			else {
                // Scale the value and clip it to a sensible range (for badly calibrated sensors)
				calibratedValue = (scale_key_position((rawValue - quiescent_))) / calibratedValueDenominator;
                if(calibratedValue < -0.5)
                    calibratedValue = -0.5;
                if(calibratedValue > 1.2)
                    calibratedValue = 1.2;
            }
			
			if(warpTable_ != 0) {
				// TODO: warping
			}
			calibrationMutex_.unlock();
			return calibratedValue;
		case kPianoKeyInCalibration:
			historyMutex_.lock();

			// Add the sample to the calibration buffer, and wait until we have enough samples to do anything
			history_->push_back(rawValue);
			if(history_->size() < kPianoKeyCalibrationPressLength) {
				historyMutex_.unlock();
				calibrationMutex_.unlock();
				return missing_value<key_position>::missing();
			}
            
			if(pressValueGoesDown_) {      // Pressed keys have a lower value than quiescent keys
				int currentAverage = averagePosition(kPianoKeyCalibrationPressLength);
                
				// Look for minimum overall value
				if(currentAverage < newPress_ || missing_value<int>::isMissing(newPress_)) {
					newPress_ = currentAverage;
                }
			}
			else {                          // Pressed keys have a higher value than quiescent keys
				int currentAverage = averagePosition(kPianoKeyCalibrationPressLength);
				
				// Look for maximum overall value
				if(currentAverage > newPress_ || missing_value<int>::isMissing(newPress_)) {
					newPress_ = currentAverage;
                }
			}
			
			// Don't return a value while calibrating
			historyMutex_.unlock();
			calibrationMutex_.unlock();
			return missing_value<key_position>::missing();
		case kPianoKeyNotCalibrated:	// Don't do anything
		default:
			calibrationMutex_.unlock();
			return missing_value<key_position>::missing();
	}
}

// Begin the calibrating process.
void PianoKeyCalibrator::calibrationStart() {
	if(status_ == kPianoKeyInCalibration)	// Throw away the old results if we're already in progress
		calibrationAbort();					// This will clear the slate
	
    historyMutex_.lock();
    if(history_ != 0)
        delete history_;
    history_ = new boost::circular_buffer<int>(kPianoKeyCalibrationBufferSize);
    historyMutex_.unlock();
    
	calibrationMutex_.lock();
    newPress_ = quiescent_ = missing_value<int>::missing();
	changeStatus(kPianoKeyInCalibration);
	calibrationMutex_.unlock();
}

// Finish calibrating and accept the new results. Returns true if
// calibration was successful; false if one or more values were missing
// or if insufficient range is available.

bool PianoKeyCalibrator::calibrationFinish() {
    bool updatedCalibration = false;
    int oldQuiescent = quiescent_;
    
	if(status_ != kPianoKeyInCalibration)
		return false;
    
	calibrationMutex_.lock();
    
    // Check that we were successfully able to update the quiescent value
    // (should always be the case but this is a sanity check)
	bool updatedQuiescent = internalUpdateQuiescent();
    
    if(updatedQuiescent && abs(newPress_ - quiescent_) >= kPianoKeyCalibrationMinimumRange) {
        press_ = newPress_;
        changeStatus(kPianoKeyCalibrated);
        updatedCalibration = true;
    }
    else {
        quiescent_ = oldQuiescent;
        
        if(prevStatus_ == kPianoKeyCalibrated) {	// There may or may not have been valid data in press_ and quiescent_ before, depending on whether
            changeStatus(kPianoKeyCalibrated);      // they were previously calibrated.
        }
        else {
            changeStatus(kPianoKeyNotCalibrated);
        }
    }
    
    cleanup();
	calibrationMutex_.unlock();
    return updatedCalibration;
}

// Finish calibrating without saving results
void PianoKeyCalibrator::calibrationAbort() {
	calibrationMutex_.lock();
	cleanup();
	if(prevStatus_ == kPianoKeyCalibrated) {	// There may or may not have been valid data in press_ and quiescent_ before, depending on whether
		changeStatus(kPianoKeyCalibrated);	// they were previously calibrated.
	}
	else {
		changeStatus(kPianoKeyNotCalibrated);
	}
	calibrationMutex_.unlock();
}

// Clear the existing calibration, reverting to an uncalibrated state
void PianoKeyCalibrator::calibrationClear() {
	if(status_ == kPianoKeyInCalibration)
		calibrationAbort();
	calibrationMutex_.lock();
	status_ = prevStatus_ = kPianoKeyNotCalibrated;
	calibrationMutex_.unlock();
}

// Generate new quiescent values without changing the press values
void PianoKeyCalibrator::calibrationUpdateQuiescent() {
	calibrationStart();
	usleep(250000);			// Wait 0.25 seconds for data to collect
	internalUpdateQuiescent();
	calibrationAbort();
}

// Load calibration data from an XML string
void PianoKeyCalibrator::loadFromXml(TiXmlElement* baseElement) {
	// Abort any calibration in progress and reset to default values
	if(status_ == kPianoKeyInCalibration)
		calibrationAbort();
	calibrationClear();
	
	TiXmlElement *calibrationElement = baseElement->FirstChildElement("Calibration");
	
	if(calibrationElement != NULL) {
        int quiescent, press;
        
        if(calibrationElement->QueryIntAttribute("quiescent", &quiescent) == TIXML_SUCCESS) {
            if(calibrationElement->QueryIntAttribute("press", &press) == TIXML_SUCCESS) {
                // Found both values: update our state accordingly
                quiescent_ = quiescent;
                press_ = press;
                changeStatus(kPianoKeyCalibrated);
            }
        }
	}
}

// Saves calibration data within the provided XML Element.  Child elements
// will be added for each sequence.  Returns true if valid data was saved.
bool PianoKeyCalibrator::saveToXml(TiXmlElement *baseElement) {
	if(status_ != kPianoKeyCalibrated)
		return false;

    TiXmlElement newElement("Calibration");
    newElement.SetAttribute("quiescent", quiescent_);
    newElement.SetAttribute("press", press_);
    
    if(baseElement->InsertEndChild(newElement) == NULL)
        return false;

	return true;
}

// ***** Internal Methods *****

// Internal method to clean up after a calibration session.
void PianoKeyCalibrator::cleanup() {
	historyMutex_.lock();
    if(history_ != 0)
        delete history_;
    history_ = 0;
	historyMutex_.unlock();
    newPress_ = missing_value<int>::missing();
}

// This internal method actually calculates the new quiescent values.  Used by calibrationUpdateQuiescent()
// and calibrationFinish(). Returns true if successful.
bool PianoKeyCalibrator::internalUpdateQuiescent() {
	historyMutex_.lock();
    if(history_ == 0) {
        historyMutex_.unlock();
        return false;
    }
    if(history_->size() < kPianoKeyCalibrationPressLength) {
        historyMutex_.unlock();
        return false;
    }
    quiescent_  = averagePosition(kPianoKeyCalibrationBufferSize);
	historyMutex_.unlock();
    return true;
}

// Get the average position of several samples in the buffer. 
int PianoKeyCalibrator::averagePosition(int length) {
	boost::circular_buffer<int>::reverse_iterator rit = history_->rbegin();
	int count = 0, sum = 0;
	
	while(rit != history_->rend() && count < length) {
		sum += *rit++;
		count++;
	}
	
	if(count == 0) {
		return missing_value<int>::missing();
    }
    
    return (int)(sum / count);
}