/*
 *  MidiOutputController.h
 *  keycontrol_cocoa
 *
 *  This class, based on top of RtMidi, handles sending MIDI messages
 *  to other programs or devices.
 *
 *  Created by Andrew McPherson on 6/10/11.
 *  Copyright 2011 __MyCompanyName__. All rights reserved.
 *
 */

#ifndef MIDI_OUTPUT_CONTROLLER_H
#define MIDI_OUTPUT_CONTROLLER_H

#define MIDI_OUTPUT_CONTROLLER_DEBUG_RAW

#include "MidiInputController.h"

const string kMidiVirtualOutputName = "keycontrol";

using namespace std;

class MidiOutputController {
public:
	
	// Constructor
	MidiOutputController();
	
	// Query available devices
	vector<pair<int, string> > availableMidiDevices();
	
	// Methods to connect/disconnect from a target port
	// (unlike MidiInputController, only one at a time)
	bool openPort(int portNumber);
	bool openVirtualPort();
	void closePort();
	
	bool isOpen() { return isOpen_; }
	
	// Send MIDI messages
	void sendNoteOn(unsigned char channel, unsigned char note, unsigned char velocity);
	void sendNoteOff(unsigned char channel, unsigned char note);
    void sendNoteOff(unsigned char channel, unsigned char note, unsigned char velocity);
	void sendControlChange(unsigned char channel, unsigned char control, unsigned char value);
	void sendProgramChange(unsigned char channel, unsigned char value);
	void sendAftertouchChannel(unsigned char channel, unsigned char value);
	void sendAftertouchPoly(unsigned char channel, unsigned char note, unsigned char value);
	void sendPitchWheel(unsigned char channel, unsigned int value);
	void sendReset();
	
	// Generic pre-formed messages
	void sendMessage(std::vector<unsigned char>* message);
	
	// Destructor
	~MidiOutputController() { closePort(); }
	
private:
	RtMidiOut midiOut_;	// Output instance from RtMidi
	bool isOpen_;			// Whether a port is currently open
};

#endif /* MIDI_OUTPUT_CONTROLLER_H */