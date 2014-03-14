//
//  TouchkeyVibratoMapping.cpp
//  touchkeys
//
//  Created by Andrew McPherson on 16/03/2013.
//  Copyright (c) 2013 Andrew McPherson. All rights reserved.
//

#include "TouchkeyVibratoMapping.h"
#include "MidiOutputController.h"
#include <vector>
#include <climits>
#include <cmath>

// Main constructor takes references/pointers from objects which keep track
// of touch location, continuous key position and the state detected from that
// position. The PianoKeyboard object is strictly required as it gives access to
// Scheduler and OSC methods. The others are optional since any given system may
// contain only one of continuous key position or touch sensitivity
TouchkeyVibratoMapping::TouchkeyVibratoMapping(PianoKeyboard &keyboard, int noteNumber, Node<KeyTouchFrame>* touchBuffer,
                       Node<key_position>* positionBuffer, KeyPositionTracker* positionTracker)
: Mapping(keyboard, noteNumber, touchBuffer, positionBuffer, positionTracker),
  rawDistance_(kDefaultFilterBufferLength),
  filteredDistance_(kDefaultFilterBufferLength, rawDistance_),
  noteIsOn_(false), vibratoState_(kStateInactive), foundFirstExtremum_(false),
  firstExtremumX_(0), firstExtremumY_(0), /*midiChannel_(kDefaultMIDIChannel),*/
  idOfCurrentTouch_(-1), lastExtremumTimestamp_(missing_value<timestamp_diff_type>::missing()),
  firstExtremumTimestamp_(missing_value<timestamp_diff_type>::missing()),
  lastX_(missing_value<float>::missing()), lastY_(missing_value<float>::missing()),
  lastTimestamp_(missing_value<timestamp_type>::missing()), lastProcessedIndex_(0),
  onsetThresholdX_(kDefaultVibratoThresholdX), onsetThresholdY_(kDefaultVibratoThresholdY),
  onsetRatioX_(kDefaultVibratoRatioX), onsetRatioY_(kDefaultVibratoRatioY),
  onsetTimeout_(kDefaultVibratoTimeout), vibratoPrescaler_(kDefaultVibratoPrescaler),
  vibratoRangeSemitones_(kDefaultVibratoRangeSemitones), lastPitchBendSemitones_(0),
  rampLength_(0), rampBeginTime_(missing_value<timestamp_type>::missing()),
  rampScaleValue_(0), lastCalculatedRampValue_(0), lastZeroCrossingInterval_(0),
  lastSampleWasPositive_(false), lastZeroCrossingTimestamp_(missing_value<timestamp_type>::missing())
{
    // Initialize the filter coefficients for filtered key velocity (used for vibrato detection)
    std::vector<double> bCoeffs, aCoeffs;
    designSecondOrderBandpass(bCoeffs, aCoeffs, 9.0, 0.707, 200.0);
    std::vector<float> bCf(bCoeffs.begin(), bCoeffs.end()), aCf(aCoeffs.begin(), aCoeffs.end());
    filteredDistance_.setCoefficients(bCf, aCf);
    filteredDistance_.setAutoCalculate(true);
    
    setOscController(&keyboard_);
    resetDetectionState();
}

// Copy constructor
TouchkeyVibratoMapping::TouchkeyVibratoMapping(TouchkeyVibratoMapping const& obj)
: Mapping(obj), noteIsOn_(obj.noteIsOn_), vibratoState_(obj.vibratoState_),
  rawDistance_(obj.rawDistance_),
  filteredDistance_(obj.filteredDistance_), /*midiChannel_(obj.midiChannel_),*/
  foundFirstExtremum_(obj.foundFirstExtremum_), onsetLocationX_(obj.onsetLocationX_),
  onsetLocationY_(obj.onsetLocationY_), idOfCurrentTouch_(obj.idOfCurrentTouch_),
  firstExtremumTimestamp_(obj.firstExtremumTimestamp_),
  lastExtremumTimestamp_(obj.lastExtremumTimestamp_), firstExtremumX_(obj.firstExtremumX_),
  firstExtremumY_(obj.firstExtremumY_), lastX_(obj.lastX_), lastY_(obj.lastY_),
  lastTimestamp_(obj.lastTimestamp_), lastProcessedIndex_(obj.lastProcessedIndex_),
  onsetThresholdX_(obj.onsetThresholdX_), onsetThresholdY_(obj.onsetThresholdY_),
  onsetRatioX_(obj.onsetRatioX_), onsetRatioY_(obj.onsetRatioY_),
  onsetTimeout_(obj.onsetTimeout_), vibratoPrescaler_(kDefaultVibratoPrescaler),
  vibratoRangeSemitones_(obj.vibratoRangeSemitones_), rampBeginTime_(obj.rampBeginTime_),
  lastPitchBendSemitones_(obj.lastPitchBendSemitones_), rampLength_(obj.rampLength_),
  rampScaleValue_(obj.rampScaleValue_), lastCalculatedRampValue_(obj.lastCalculatedRampValue_),
  lastZeroCrossingInterval_(obj.lastZeroCrossingInterval_),
  lastSampleWasPositive_(obj.lastSampleWasPositive_), lastZeroCrossingTimestamp_(obj.lastZeroCrossingTimestamp_)
{
    setOscController(&keyboard_);    
}

TouchkeyVibratoMapping::~TouchkeyVibratoMapping() {
    try {
        disengage();
    }
    catch(...) {
        std::cerr << "~TouchkeyVibratoMapping(): exception during disengage()\n";
    }
}

// Turn on mapping of data.
void TouchkeyVibratoMapping::engage() {
    Mapping::engage();
    
    // Register for OSC callbacks on MIDI note on/off
    addOscListener("/midi/noteon");
	addOscListener("/midi/noteoff");
}

// Turn off mapping of data. Remove our callback from the scheduler
void TouchkeyVibratoMapping::disengage() {
    // Remove OSC listeners first
    removeOscListener("/midi/noteon");
	removeOscListener("/midi/noteoff");
    
    // Send 0 vibrato message
    sendVibratoMessage(0.0);
    
    Mapping::disengage();
    
    if(noteIsOn_) {
        // TODO
    }
    noteIsOn_ = false;
}

// Reset state back to defaults
void TouchkeyVibratoMapping::reset() {
    Mapping::reset();
    sendVibratoMessage(0.0);
    noteIsOn_ = false;
}

// OSC handler method. Called from PianoKeyboard when MIDI data comes in.
bool TouchkeyVibratoMapping::oscHandlerMethod(const char *path, const char *types, int numValues, lo_arg **values, void *data) {
    if(!strcmp(path, "/midi/noteon") && !noteIsOn_ && numValues >= 1) {
        if(types[0] == 'i' && values[0]->i == noteNumber_) {
            // MIDI note has gone on. Set the starting location to be most recent
            // location. It's possible there has been no touch data before this,
            // in which case lastX and lastY will hold missing values.
            onsetLocationX_ = lastX_;
            onsetLocationY_ = lastY_;
            if(!missing_value<float>::isMissing(onsetLocationY_)) {
                // Already have touch data. Clear the buffer here.
                // Clear buffer and start with 0 distance for this point
                clearBuffers();
                
                std::cout << "MIDI on: starting at (" << onsetLocationX_ << ", " << onsetLocationY_ << ")\n";
            }
            else
                std::cout << "MIDI on but no touch\n";
            
            noteIsOn_ = true;
            return true;
        }
    }
    else if(!strcmp(path, "/midi/noteoff") && noteIsOn_ && numValues >= 1) {
        if(types[0] == 'i' && values[0]->i == noteNumber_) {
            // MIDI note goes off. Stop tracking vibrato gesture
            noteIsOn_ = false;
            if(vibratoState_ == kStateActive || vibratoState_ == kStateSwitchingOn) {
                changeStateSwitchingOff(keyboard_.schedulerCurrentTimestamp());
            }
            // lastX_ = lastY_ = missing_value<float>::missing();
            // idOfCurrentTouch_ = -1;
            std::cout << "MIDI off\n";
            return true;
        }
    }
    
    return false;
}

// Trigger method. This receives updates from the TouchKey data or from state changes in
// the continuous key position (KeyPositionTracker). It will potentially change the scheduled
// behavior of future mapping calls, but the actual OSC messages should be transmitted in a different
// thread.
void TouchkeyVibratoMapping::triggerReceived(TriggerSource* who, timestamp_type timestamp) {
    if(who == 0)
        return;
    
    if(who == touchBuffer_) {
        if(!touchBuffer_->empty()) {
            // New touch data is available. Find the distance from the onset location.
            KeyTouchFrame frame = touchBuffer_->latest();
            lastTimestamp_ = timestamp;
            
            if(frame.count == 0) {
                // No touches. Last values are "missing", and we're not tracking any
                // particular touch ID
                lastX_ = lastY_ = missing_value<float>::missing();
                idOfCurrentTouch_ = -1;
                
                std::cout << "Touch off\n";
            }
            else {
                // At least one touch. Check if we are already tracking an ID and, if so,
                // use its coordinates. Otherwise grab the lowest current ID.
                
                bool foundCurrentTouch = false;
                
                if(idOfCurrentTouch_ >= 0) {
                    for(int i = 0; i < frame.count; i++) {
                        if(frame.ids[i] == idOfCurrentTouch_) {
                            lastY_ = frame.locs[i];
                            if(frame.locH < 0)
                                lastX_ = missing_value<float>::missing();
                            else
                                lastX_ = frame.locH;
                            foundCurrentTouch = true;
                            break;
                        }
                    }
                }
                
                if(!foundCurrentTouch) {
                    // Assign a new touch to be tracked
                    int lowestRemainingId = INT_MAX;
                    int lowestIndex = 0;
                    
                    for(int i = 0; i < frame.count; i++) {
                        if(frame.ids[i] < lowestRemainingId) {
                            lowestRemainingId = frame.ids[i];
                            lowestIndex = i;
                        }
                    }
                    
                    idOfCurrentTouch_ = lowestRemainingId;
                    lastY_ = frame.locs[lowestIndex];
                    if(frame.locH < 0)
                        lastX_ = missing_value<float>::missing();
                    else
                        lastX_ = frame.locH;
                    
                    std::cout << "Previous touch stopped; now ID " << idOfCurrentTouch_ << " at (" << lastX_ << ", " << lastY_ << ")\n";
                }
                
                // Now we have an X and (maybe) a Y coordinate for the most recent touch.
                // Check whether we have an initial location (if the note is active).
                if(noteIsOn_) {
                    if(missing_value<float>::isMissing(onsetLocationY_) ||
                       !foundCurrentTouch) {
                        // Note is on but touch hasn't yet arrived --> this touch becomes
                        // our onset location. Alternatively, the current touch is a different
                        // ID from the previous one.
                        onsetLocationY_ = lastY_;
                        onsetLocationX_ = lastX_;
                        
                        // Clear buffer and start with 0 distance for this point
                        clearBuffers();
                        
                        std::cout << "Starting at (" << onsetLocationX_ << ", " << onsetLocationY_ << ")\n";
                    }
                    else {
                        float distance = 0.0;
                        
                        // Note is on and a start location exists. Calculate distance between
                        // start location and the current point.
                        
                        if(missing_value<float>::isMissing(onsetLocationX_) &&
                           !missing_value<float>::isMissing(lastX_)) {
                            // No X location indicated for onset but we have one now.
                            // Update the onset X location.
                            onsetLocationX_ = lastX_;
                            std::cout << "Found first X location at " << onsetLocationX_ << std::endl;
                        }
                        
                        
                        if(missing_value<float>::isMissing(lastX_) ||
                           missing_value<float>::isMissing(onsetLocationX_)) {
                            // If no X value is available on the current touch, calculate the distance
                            // based on Y only. TODO: check whether we should do this by keeping the
                            // last X value we recorded.
                            
                            //distance = fabsf(lastY_ - onsetLocationY_);
                            distance = lastY_ - onsetLocationY_;
                        }
                        else {
                            // Euclidean distance between points
                            //distance = sqrtf((lastY_ - onsetLocationY_) * (lastY_ - onsetLocationY_) +
                            //                 (lastX_ - onsetLocationX_) * (lastX_ - onsetLocationX_));
                            distance = lastX_ - onsetLocationX_;
                        }
                        
                        // Insert raw distance into the buffer. Bandpass filter calculates the next
                        // sample automatically. The rest of the processing takes place in the dedicated
                        // thread so as not to slow down commmunication with the hardware.
                        rawDistance_.insert(distance, timestamp);
                           
                        // Move the current scheduled event up to the present time.
                        // FIXME: this may be more inefficient than just doing everything in the current thread!
                        keyboard_.unscheduleEvent(this);
                        keyboard_.scheduleEvent(this, mappingAction_, keyboard_.schedulerCurrentTimestamp());
                        
                        //std::cout << "Raw distance " << distance << " filtered " << filteredDistance_.latest() << std::endl;
                    }
                }
            }
        }
    }
}

// Mapping method. This actually does the real work of sending OSC data in response to the
// latest information from the touch sensors or continuous key angle
timestamp_type TouchkeyVibratoMapping::performMapping() {
    timestamp_type currentTimestamp = keyboard_.schedulerCurrentTimestamp();
    bool newSamplePresent = false;

    // Go through the filtered distance samples that are remaining to process.
    if(lastProcessedIndex_ < filteredDistance_.beginIndex() + 1) {
        // Fell off the beginning of the position buffer. Skip to the samples we have
        // (shouldn't happen except in cases of exceptional system load, and not too
        // consequential if it does happen).
        lastProcessedIndex_ = filteredDistance_.beginIndex() + 1;
    }
    
    while(lastProcessedIndex_ < filteredDistance_.endIndex()) {
        float distance = filteredDistance_[lastProcessedIndex_];
        timestamp_type timestamp = filteredDistance_.timestampAt(lastProcessedIndex_);
        newSamplePresent = true;
        
        if((distance > 0 && !lastSampleWasPositive_) ||
           (distance < 0 && lastSampleWasPositive_)) {
              // Found a zero crossing: save it if we're active or have at least found the
              // first extremum
               if(!missing_value<timestamp_type>::isMissing(lastZeroCrossingTimestamp_) &&
                  (timestamp - lastZeroCrossingTimestamp_ > kZeroCrossingMinimumTime)) {
                   if(vibratoState_ == kStateActive || vibratoState_ == kStateSwitchingOn ||
                      foundFirstExtremum_) {
                       lastZeroCrossingInterval_ = timestamp - lastZeroCrossingTimestamp_;
                       std::cout << "Zero crossing interval " << lastZeroCrossingInterval_ << std::endl;
                   }
               }
               lastZeroCrossingTimestamp_ = timestamp;
        }
        lastSampleWasPositive_ = (distance > 0);
        
        // If not currently engaged, check for the pattern of side-to-side motion that
        // begins a vibrato gesture.
        if(vibratoState_ == kStateInactive || vibratoState_ == kStateSwitchingOff) {
            if(foundFirstExtremum_) {
                // Already found first extremum. Look for second extremum in the opposite
                // direction of the given ratio from the original.
                if((firstExtremumX_ > 0 && distance < 0) ||
                   (firstExtremumX_ < 0 && distance > 0)) {
                    if(fabsf(distance) >= fabsf(firstExtremumX_) * onsetRatioX_) {
                        std::cout << "Found second extremum at " << distance << ", TS " << timestamp << std::endl;
                        changeStateSwitchingOn(timestamp);
                    }
                }
                else if(timestamp - lastExtremumTimestamp_ > onsetTimeout_) {
                    std::cout << "Onset timeout at " << timestamp << endl;
                    resetDetectionState();
                }
            }
            else {
                if(fabsf(distance) >= onsetThresholdX_) {
                    // TODO: differentiate X/Y here
                    if(missing_value<float>::isMissing(firstExtremumX_) ||
                       fabsf(distance) > fabsf(firstExtremumX_)) {
                        firstExtremumX_ = distance;
                        lastExtremumTimestamp_ = timestamp;
                        std::cout << "First extremum candidate at " << firstExtremumX_ << ", TS " << lastExtremumTimestamp_ << std::endl;
                    }
                }
                else if(!missing_value<float>::isMissing(firstExtremumX_) &&
                        fabsf(firstExtremumX_) > onsetThresholdX_) {
                    // We must have found the first extremum since its maximum value is
                    // above the threshold, and we must have moved away from it since we are
                    // now below the threshold. Next step will be to look for extremum in
                    // opposite direction. Save the timestamp of this location in case
                    // another extremum is found later.
                    firstExtremumTimestamp_ = lastExtremumTimestamp_;
                    foundFirstExtremum_ = true;
                    std::cout << "Found first extremum at " << firstExtremumX_ << ", TS " << lastExtremumTimestamp_ << std::endl;
                }
            }
        }
        else {
            // Currently engaged. Look for timeout, defined as the finger staying below the lower (ratio-adjusted) threshold.
            if(fabsf(distance) >= onsetThresholdX_ * onsetRatioX_)
                lastExtremumTimestamp_ = timestamp;
            if(timestamp - lastExtremumTimestamp_ > onsetTimeout_) {
                std::cout << "Vibrato timeout at " << timestamp << " (last was " << lastExtremumTimestamp_ << ")" << endl;
                changeStateSwitchingOff(timestamp);
            }
        }
        
        lastProcessedIndex_++;
    }
    
    // Having processed every sample individually for detection, send a pitch bend message based on the most
    // recent one (no sense in sending multiple pitch bend messages simultaneously).
    if(newSamplePresent && vibratoState_ != kStateInactive) {
        float distance = filteredDistance_.latest();
        float scale = 1.0;
        
        if(vibratoState_ == kStateSwitchingOn) {
            // Switching on state gradually scales vibrato depth from 0 to
            // its final value over a specified switch-on time.
            if(rampLength_ <= 0 || (currentTimestamp - rampBeginTime_ >= rampLength_)) {
                scale = 1.0;
                changeStateActive(currentTimestamp);
                std::cout << "Vibrato switch on finished, going to Active\n";
            }
            else {
                lastCalculatedRampValue_ = rampScaleValue_ * (float)(currentTimestamp - rampBeginTime_)/(float)rampLength_;
                scale = lastCalculatedRampValue_;
                //std::cout << "Vibrato scale " << scale << ", TS " << currentTimestamp - rampBeginTime_ << std::endl;
            }
        }
        else if(vibratoState_ == kStateSwitchingOff) {
            // Switching off state gradually scales vibrato depth from full
            // value to 0 over a specified switch-off time.
            if(rampLength_ <= 0 || (currentTimestamp - rampBeginTime_ >= rampLength_)) {
                scale = 0.0;
                changeStateInactive(currentTimestamp);
                std::cout << "Vibrato switch off finished, going to Inactive\n";
            }
            else {
                lastCalculatedRampValue_ = rampScaleValue_ * (1.0 - (float)(currentTimestamp - rampBeginTime_)/(float)rampLength_);
                scale = lastCalculatedRampValue_;
                //std::cout << "Vibrato scale " << scale << ", TS " << currentTimestamp - rampBeginTime_ << std::endl;
            }
        }
        
        // Calculate pitch bend based on current distance, with a non-linear scaling to accentuate
        // smaller motions.
        float pitchBendSemitones = vibratoRangeSemitones_ * tanhf(vibratoPrescaler_ * scale * distance);
        
        sendVibratoMessage(pitchBendSemitones);
        lastPitchBendSemitones_ = pitchBendSemitones;
    }
    
    // We may have arrived here without a new touch, just based on timing. Check for timeouts and process
    // any release in progress.
    if(!newSamplePresent) {
        if(vibratoState_ == kStateSwitchingOff) {
            // No new information in the distance buffer, but we do need to gradually reduce the pitch bend to zero
            if(rampLength_ <= 0 || (currentTimestamp - rampBeginTime_ >= rampLength_)) {
                sendVibratoMessage(0.0);
                lastPitchBendSemitones_ = 0;
                changeStateInactive(currentTimestamp);
                std::cout << "Vibrato switch off finished, going to Inactive\n";
            }
            else {
                // Still in the middle of the ramp. Calculate its current value based on the last one
                // that actually had a touch data point (lastPitchBendSemitones_).
                
                lastCalculatedRampValue_ = rampScaleValue_ * (1.0 - (float)(currentTimestamp - rampBeginTime_)/(float)rampLength_);
                float pitchBendSemitones = lastPitchBendSemitones_ * lastCalculatedRampValue_;
                
                sendVibratoMessage(pitchBendSemitones);
            }
        }
        else if(vibratoState_ != kStateInactive) {
            // Might still be active but with no data coming in. We need to look for a timeout here too.
            if(currentTimestamp - lastExtremumTimestamp_ > onsetTimeout_) {
                std::cout << "Vibrato timeout at " << currentTimestamp << " (2; last was " << lastExtremumTimestamp_ << ")" << endl;
                changeStateSwitchingOff(currentTimestamp);
            }
        }
    }
    
    // Register for the next update by returning its timestamp
    nextScheduledTimestamp_ = currentTimestamp + updateInterval_;
    return nextScheduledTimestamp_;
}

// Internal state-change methods, which keep the state variables in sync
void TouchkeyVibratoMapping::changeStateSwitchingOn(timestamp_type timestamp) {
    // Go to SwitchingOn state, which brings the vibrato value gradually up to full amplitude
    
    // TODO: need to start from a non-zero value if SwitchingOff
    rampScaleValue_ = 1.0;
    rampBeginTime_ = timestamp;
    rampLength_ = 0.0;
    // Interval between peak and zero crossing will be a quarter of a cycle.
    // From this, figure out how much longer we have to go to get to the next
    // peak if the rate remains the same.
    if(!missing_value<timestamp_type>::isMissing(lastZeroCrossingTimestamp_) &&
       !missing_value<timestamp_type>::isMissing(firstExtremumTimestamp_)) {
        timestamp_type estimatedPeakTimestamp = lastZeroCrossingTimestamp_ + (lastZeroCrossingTimestamp_ - firstExtremumTimestamp_);
        rampLength_ = estimatedPeakTimestamp - timestamp;
        if(rampLength_ < kMinimumOnsetTime)
            rampLength_ = kMinimumOnsetTime;
        if(rampLength_ > kMaximumOnsetTime)
            rampLength_ = kMaximumOnsetTime;
        std::cout << "Switching on with ramp length " << rampLength_ << " (peak " << firstExtremumTimestamp_ << ", zero " << lastZeroCrossingTimestamp_ << ")" << std::endl;
    }
    
    vibratoState_ = kStateSwitchingOn;    
}

void TouchkeyVibratoMapping::changeStateSwitchingOff(timestamp_type timestamp) {
    // Go to SwitchingOff state, which brings the vibrato value gradually down to 0
    
    if(vibratoState_ == kStateSwitchingOn) {
        // Might already be in the midst of a ramp up. Start from its current value
        rampScaleValue_ = lastCalculatedRampValue_;
    }
    else
        rampScaleValue_ = 1.0;
    
    rampBeginTime_ = timestamp;
    rampLength_ = lastZeroCrossingInterval_;
    if(rampLength_ < kMinimumReleaseTime)
        rampLength_ = kMinimumReleaseTime;
    if(rampLength_ > kMaximumReleaseTime)
        rampLength_ = kMaximumReleaseTime;
    
    std::cout << "Switching off with ramp length " << rampLength_ << std::endl;
    
    resetDetectionState();
    vibratoState_ = kStateSwitchingOff;
}

void TouchkeyVibratoMapping::changeStateActive(timestamp_type timestamp) {
    vibratoState_ = kStateActive;
}

void TouchkeyVibratoMapping::changeStateInactive(timestamp_type timestamp) {
    vibratoState_ = kStateInactive;
}

// Reset variables involved in detecting a vibrato gesture
void TouchkeyVibratoMapping::resetDetectionState() {
    foundFirstExtremum_ = false;
    firstExtremumX_ = firstExtremumY_ = 0.0;
    lastExtremumTimestamp_ = firstExtremumTimestamp_ = lastZeroCrossingTimestamp_ = missing_value<timestamp_type>::missing();
}

// Clear the buffers that hold distance measurements
void TouchkeyVibratoMapping::clearBuffers() {
    rawDistance_.clear();
    filteredDistance_.clear();
    rawDistance_.insert(0.0, lastTimestamp_);
    lastProcessedIndex_ = 0;
}

// Send the vibrato message of a given number of a semitones. Send by OSC,
// which can be mapped to MIDI CC externally
void TouchkeyVibratoMapping::sendVibratoMessage(float pitchBendSemitones) {
    keyboard_.sendMessage("/touchkeys/vibrato", "if", noteNumber_, pitchBendSemitones, LO_ARGS_END);
}

