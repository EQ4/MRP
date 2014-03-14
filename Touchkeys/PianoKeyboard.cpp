/*
 *  PianoKeyboard.cpp
 *  keycontrol
 *
 *  Created by Andrew McPherson on 10/27/10.
 *  Copyright 2010 __MyCompanyName__. All rights reserved.
 *
 */

#include "PianoKeyboard.h"
#include "TouchkeyDevice.h"
#include "Mapping.h"
#include "MidiOutputcontroller.h"

// Constructor
PianoKeyboard::PianoKeyboard() 
: isInitialized_(false), isRunning_(false), isCalibrated_(false), calibrationInProgress_(false),
  lowestMidiNote_(0), highestMidiNote_(0), gui_(0), graphGui_ (0), oscTransmitter_(0), touchkeyDevice_(0),
  midiOutputController_(0) {
	  // Start a thread by which we can schedule future events
	  futureEventScheduler_.start(0);
      
      // XXX HACK
      // create output file
      testLog_.open ("/Users/apm/Desktop/test_pb_log.txt", ios::out | ios::binary);
      testLog_.seekp(0);
}

// Reset all keys and pedals to their default state.
void PianoKeyboard::reset() {
	// Clear any history in the source buffers
	std::vector<PianoKey*>::iterator itKey;
	std::vector<PianoPedal*>::iterator itPed;
	
	for(itKey = keys_.begin(); itKey != keys_.end(); itKey++)
		(*itKey)->reset();
	for(itPed = pedals_.begin(); itPed != pedals_.end(); itPed++)
		(*itPed)->clear();
}

// Provide a pointer to the graphical display class

void PianoKeyboard::setGUI(KeyboardDisplay* gui) {
	gui_ = gui;
	if(gui_ != 0) {
		gui_->setKeyboardRange(lowestMidiNote_, highestMidiNote_);
	}
}

// Set the range of the keyboard in terms of MIDI notes.  A standard
// 88-key keyboard has a range of 21-108, but other setups may differ.

void PianoKeyboard::setKeyboardRange(int lowest, int highest) {
	lowestMidiNote_ = lowest;
	highestMidiNote_ = highest;
	
	// Sanity checks: enforce 0-127 range, high >= low
	if(lowestMidiNote_ < 0)
		lowestMidiNote_ = 0;
	if(highestMidiNote_ < 0)
		highestMidiNote_ = 0;
	if(lowestMidiNote_ > 127)
		lowestMidiNote_ = 127;
	if(highestMidiNote_ > 127)
		highestMidiNote_ = 127;
	if(lowestMidiNote_ > highestMidiNote_)
		highestMidiNote_ = lowestMidiNote_;
	
	// Free the existing PianoKey objects
	for(std::vector<PianoKey*>::iterator it = keys_.begin(); it != keys_.end(); ++it)
		delete (*it);
	keys_.clear();
	
	// Rebuild the key list
	for(int i = lowestMidiNote_; i <= highestMidiNote_; i++)
		keys_.push_back(new PianoKey(*this, i, kDefaultKeyHistoryLength));
	
	if(gui_ != 0)
		gui_->setKeyboardRange(lowestMidiNote_, highestMidiNote_);
}

// Send a message by OSC (and potentially by other means depending on who's listening)

void PianoKeyboard::sendMessage(const char * path, const char * type, ...) {
	
    //cout << "sendMessage: " << path << endl;
    
	// Initialize variable argument list for reading
	va_list v;
	va_start(v, type);	
	
	// Make a new OSC message which we will use both internally and externally
	lo_message msg = lo_message_new();
	lo_message_add_varargs(msg, type, v);
	int argc = lo_message_get_argc(msg);
	lo_arg **argv = lo_message_get_argv(msg);
	
	// Internal handler lookup first
	// Lock the mutex so the list of listeners doesn't change midway through
	pthread_mutex_lock(&oscListenerMutex_);
	
	// Now remove the global prefix and compare the rest of the message to the registered handlers.
	std::multimap<std::string, OscHandler*>::iterator it;
	std::pair<std::multimap<std::string, OscHandler*>::iterator,std::multimap<std::string, OscHandler*>::iterator> ret;
	ret = noteListeners_.equal_range((std::string)path);
	
	it = ret.first;
	while(it != ret.second) {
		OscHandler *object = (*it++).second;

		object->oscHandlerMethod(path, type, argc, argv, 0);
	}
	
	pthread_mutex_unlock(&oscListenerMutex_);
    
	// Now send this message to any external OSC sources
	if(oscTransmitter_ != 0)
		oscTransmitter_->sendMessage(path, type, msg);
	
	lo_message_free(msg);	
	
	va_end(v);
}

// Change number of pedals

void PianoKeyboard::setNumberOfPedals(int number) {
	numberOfPedals_ = number;
	if(numberOfPedals_ < 0)
		numberOfPedals_ = 0;
	if(numberOfPedals_ > 127)
		numberOfPedals_ = 127;
	
	// Free the existing PianoPedal objects
	for(std::vector<PianoPedal*>::iterator it = pedals_.begin(); it != pedals_.end(); ++it)
		delete (*it);
	pedals_.clear();
	
	// Rebuild the list of pedals
	for(int i = 0; i < numberOfPedals_; i++)
		pedals_.push_back(new PianoPedal(kDefaultPedalHistoryLength));
}

// Set color of RGB LED for a given key. note indicates the MIDI
// note number of the key, and color can be specified in one of two
// formats.
void PianoKeyboard::setKeyLEDColorRGB(const int note, const float red, const float green, const float blue) {
    if(touchkeyDevice_ != 0) {
        touchkeyDevice_->rgbledSetColor(note, red, green, blue);
    }
}

void PianoKeyboard::setKeyLEDColorHSV(const int note, const float hue, const float saturation, const float value) {
    if(touchkeyDevice_ != 0) {
        touchkeyDevice_->rgbledSetColorHSV(note, hue, saturation, value);
    }
}

void PianoKeyboard::setAllKeyLEDsOff() {
    if(touchkeyDevice_ != 0) {
        touchkeyDevice_->rgbledAllOff();
    }
}

// ***** Mapping Methods *****

// Add a new mapping identified by a MIDI note and an owner
void PianoKeyboard::addMapping(int noteNumber, Mapping* mapping) {
    removeMapping(noteNumber);  // Free any mapping that's already present on this note
    mappings_[noteNumber] = mapping;
}

// Remove an existing mapping identified by owner
void PianoKeyboard::removeMapping(int noteNumber) {
    if(mappings_.count(noteNumber) == 0)
        return;
    Mapping* mapping = mappings_[noteNumber];
    delete mapping;
    mappings_.erase(noteNumber);
}

// Return a specific mapping by owner and note number
Mapping* PianoKeyboard::mapping(int noteNumber) {
    if(mappings_.count(noteNumber) == 0)
        return 0;
    return mappings_[noteNumber];
    
    return 0;
}

// Return a list of all MIDI notes with active mappings. Some may have more than
// one but we only want the list of active notes
std::vector<int> PianoKeyboard::activeMappings() {
    std::vector<int> keys;
    std::map<int, Mapping*>::iterator it = mappings_.begin();
    while(it != mappings_.end()) {
        int nextKey = (it++)->first;
        keys.push_back(nextKey);
    }
    return keys;
}

void PianoKeyboard::clearMappings() {
    std::map<int, Mapping*>::iterator it = mappings_.begin();
    
    while(it != mappings_.end()) {
        // Delete everybody in the container
        Mapping *mapping = it->second;
        delete mapping;
    }
    
    // Now clear the container
    mappings_.clear();
}


// Destructor

PianoKeyboard::~PianoKeyboard() {
    // Remove all mappings
    clearMappings();
    
	// Delete any keys and pedals we've allocated
	for(std::vector<PianoKey*>::iterator it = keys_.begin(); it != keys_.end(); ++it)
		delete (*it);
	for(std::vector<PianoPedal*>::iterator it = pedals_.begin(); it != pedals_.end(); ++it)
		delete (*it);	
}