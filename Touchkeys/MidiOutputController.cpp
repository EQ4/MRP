/*
 *  MidiOutputController.cpp
 *  keycontrol_cocoa
 *
 *  Created by Andrew McPherson on 6/10/11.
 *  Copyright 2011 __MyCompanyName__. All rights reserved.
 *
 */

#include "MidiOutputController.h"

// Constructor
MidiOutputController::MidiOutputController() : isOpen_(false)
{
}

// Iterate through the available MIDI devices.  Return a vector containing
// indices and names for each device.  The index will later be passed back
// to indicate which device to open.

vector<pair<int, string> > MidiOutputController::availableMidiDevices() {
	vector<pair<int, string> > deviceList;
	
	try {
		int numDevices = midiOut_.getPortCount();
		
		for(int i = 0; i < numDevices; i++) {
			pair<int, string> p(i, midiOut_.getPortName(i));
			deviceList.push_back(p);
		}
	}
	catch(...) {
		deviceList.clear();
	}
	
	return deviceList;
}

// Open a new MIDI output port, given an index related to the list from
// availableMidiDevices().  Returns true on success.

bool MidiOutputController::openPort(int portNumber) {
	// Close any previously open port
	if(isOpen_)
		closePort();
	try{
		midiOut_.openPort(portNumber);
		isOpen_ = true;
	}
	catch(...) {
		return false;
	}

	return true;
}

// Open a virtual MIDI port that other applications can connect to.
// Returns true on success.

bool MidiOutputController::openVirtualPort() {
	// Close any previously open port
	if(isOpen_)
		closePort();
	try{
		midiOut_.openVirtualPort("keycontrol");
		isOpen_ = true;
	}
	catch(...) {
		return false;
	}
	
	return true;
}

// Close a currently open MIDI port
void MidiOutputController::closePort() {
	try {
		isOpen_ = false;
		midiOut_.closePort();
	}
	catch(...) {}	
}

// Send a MIDI Note On message
void MidiOutputController::sendNoteOn(unsigned char channel, unsigned char note, unsigned char velocity) {
	vector<unsigned char> message;
	
	message.push_back((channel & 0x0F) | kMidiMessageNoteOn);
	message.push_back(note & 0x7F);
	message.push_back(velocity & 0x7F);
	
	sendMessage(&message);
}

// Send a MIDI Note Off message
void MidiOutputController::sendNoteOff(unsigned char channel, unsigned char note) {
	vector<unsigned char> message;
	
	message.push_back((channel & 0x0F) | kMidiMessageNoteOn);
	message.push_back(note & 0x7F);
	message.push_back(0);
	
	sendMessage(&message);
}

// Send a MIDI Note Off message; second version supporting release velocity
void MidiOutputController::sendNoteOff(unsigned char channel, unsigned char note, unsigned char velocity) {
	vector<unsigned char> message;
	
	message.push_back((channel & 0x0F) | kMidiMessageNoteOff);
	message.push_back(note & 0x7F);
	message.push_back(velocity & 0x7F);
	
	sendMessage(&message);
}

// Send a MIDI Control Change message
void MidiOutputController::sendControlChange(unsigned char channel, unsigned char control, unsigned char value) {
	vector<unsigned char> message;
	
	message.push_back((channel & 0x0F) | kMidiMessageControlChange);
	message.push_back(control & 0x7F);
	message.push_back(value & 0x7F);
	
	sendMessage(&message);	
}

// Send a MIDI Program Change message
void MidiOutputController::sendProgramChange(unsigned char channel, unsigned char value) {
	vector<unsigned char> message;
	
	message.push_back((channel & 0x0F) | kMidiMessageProgramChange);
	message.push_back(value & 0x7F);
	
	sendMessage(&message);		
}

// Send a Channel Aftertouch message
void MidiOutputController::sendAftertouchChannel(unsigned char channel, unsigned char value) {
	vector<unsigned char> message;
	
	message.push_back((channel & 0x0F) | kMidiMessageAftertouchChannel);
	message.push_back(value & 0x7F);
	
	sendMessage(&message);		
}

// Send a Polyphonic Aftertouch message
void MidiOutputController::sendAftertouchPoly(unsigned char channel, unsigned char note, unsigned char value) {
	vector<unsigned char> message;
	
	message.push_back((channel & 0x0F) | kMidiMessageAftertouchPoly);
	message.push_back(note & 0x7F);
	message.push_back(value & 0x7F);
	
	sendMessage(&message);	
}

// Send a Pitch Wheel message
void MidiOutputController::sendPitchWheel(unsigned char channel, unsigned int value) {
	vector<unsigned char> message;
	
	message.push_back((channel & 0x0F) | kMidiMessagePitchWheel);
	message.push_back(value & 0x7F);
	message.push_back((value >> 7) & 0x7F);
	
	sendMessage(&message);		
}

// Send a MIDI system reset message
void MidiOutputController::sendReset() {
	vector<unsigned char> message;
	
	message.push_back(kMidiMessageReset);
	sendMessage(&message);
}

// Send a generic MIDI message (pre-formatted data)
void MidiOutputController::sendMessage(std::vector<unsigned char>* message) {
	if(message == 0 || !isOpen_)
		return;
	
#ifdef MIDI_OUTPUT_CONTROLLER_DEBUG_RAW
	cout << "MIDI Output: ";
	for(int debugPrint = 0; debugPrint < message->size(); debugPrint++)
		printf("%x ", (*message)[debugPrint]);
	cout << endl;
#endif /* MIDI_OUTPUT_CONTROLLER_DEBUG_RAW */
	
	midiOut_.sendMessage(message);
}