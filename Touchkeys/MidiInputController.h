/*
 *  MidiController.h
 *  keycontrol_cocoa
 *
 *  This class handles interaction with MIDI devices: querying available devices
 *  and processing incoming data.  It is based on top of the RtMidi C++ library.
 *
 *  Created by Andrew McPherson on 6/8/11.
 *  Copyright 2011 __MyCompanyName__. All rights reserved.
 *
 */

#ifndef MIDI_INPUT_CONTROLLER_H
#define MIDI_INPUT_CONTROLLER_H

#define MIDI_INPUT_CONTROLLER_DEBUG_RAW

#include <iostream>
#include <vector>
#include <map>
#include <set>
#include "RtMidi.h"
#include "PianoKeyboard.h"
#include "Osc.h"

using namespace std;

class MidiOutputController;

// MIDI standard messages

enum {
	kMidiMessageNoteOff = 0x80,
	kMidiMessageNoteOn = 0x90,
	kMidiMessageAftertouchPoly = 0xA0,
	kMidiMessageControlChange = 0xB0,
	kMidiMessageProgramChange = 0xC0,
	kMidiMessageAftertouchChannel = 0xD0,
	kMidiMessagePitchWheel = 0xE0,
	kMidiMessageSysex = 0xF0,
	kMidiMessageSysexEnd = 0xF7,
	kMidiMessageActiveSense = 0xFE,
	kMidiMessageReset = 0xFF
};

enum {
	kMidiControlAllSoundOff = 120,
	kMidiControlAllControllersOff = 121,
	kMidiControlLocalControl = 122,
	kMidiControlAllNotesOff = 123
};

class MidiInputController : public OscHandler {
public:
	typedef struct {
		MidiInputController *controller;	// The specific object to which this message should be routed
		RtMidiIn *midiIn;					// The object which actually handles the MIDI input
		int inputNumber;					// An index indicating which input this came from in the user-defined order
	} MidiInputCallback;
	
	// Operating modes for MIDI input
	enum {
		ModeOff = 0,
		ModePassThrough,
		ModeMonophonic,
		ModePolyphonic,
		ModeChannelSelect,
		ModeConstantControllers
	};
	
	// Switch types for Channel Select mode
	enum {
		ChannelSelectSwitchTypeUnknown = 0,
		ChannelSelectSwitchTypeLocation,
		ChannelSelectSwitchTypeSize,
		ChannelSelectSwitchTypeNumTouches,
		ChannelSelectSwitchTypeAngle
	};
	
public:
	// Constructor
	MidiInputController(PianoKeyboard& keyboard);

	
	// Query available devices
	vector<pair<int, string> > availableMidiDevices();
	
	// Add/Remove MIDI input ports;
	// Enable methods return true on success (at least one port enabled) 
	bool enablePort(int portNumber);
	bool enableAllPorts();
	void disablePort(int portNumber);
	void disableAllPorts();
	
	// Set which channels we listen to
	bool enableChannel(int channelNumber);
	bool enableAllChannels();
	void disableChannel(int channelNumber);
	void disableAllChanels();
	
	// Set/query the output controller
	MidiOutputController* midiOutputController() { return midiOutputController_; }
	void setMidiOutputController(MidiOutputController* ct) { midiOutputController_ = ct; }
	
	// All Notes Off: can be sent by MIDI or controlled programmatically
	void allNotesOff();
	
	// Change or query the operating mode of the controller
	int mode() { return mode_; }
	void setModeOff();
	void setModePassThrough();
	void setModePolyphonic(int maxPolyphony);
	void setModeChannelSelect(int switchType, int numDivisions, int defaultChannel);
	
	// The static callback below is needed to interface with RtMidi;
	// It passes control off to the instance-specific function
	void rtMidiCallback(double deltaTime, vector<unsigned char> *message, int inputNumber);	// Instance-specific callback	
	static void rtMidiStaticCallback(double deltaTime, vector<unsigned char> *message, void *userData)
	{
		MidiInputCallback *s = (MidiInputCallback *)userData;
		(s->controller)->rtMidiCallback(deltaTime, message, s->inputNumber);
	}
	
	// OSC method: used to get touch callback data from the keyboard
	bool oscHandlerMethod(const char *path, const char *types, int numValues, lo_arg **values, void *data);
    
    // for logging
    void createLogFile(string midiLog_filename, string path);
    void closeLogFile();
    void startLogging();
    void stopLogging();
    
    bool logFileCreated;
    bool loggingActive;


	// Destructor
	~MidiInputController();
	
private:
	// Filtering by channel: return whether this message concerns one of the active channels
	// we're listening to.
	bool messageIsForActiveChannel(vector<unsigned char> *message);
	
	// Mode-specific MIDI input handlers
	void modePassThroughHandler(double deltaTime, vector<unsigned char> *message, int inputNumber);	
	void modeMonophonicHandler(double deltaTime, vector<unsigned char> *message, int inputNumber);

	void modePolyphonicHandler(double deltaTime, vector<unsigned char> *message, int inputNumber);
	void modePolyphonicNoteOn(unsigned char note, unsigned char velocity);
	void modePolyphonicNoteOff(unsigned char note);
	void modePolyphonicNoteOnCallback(const char *path, const char *types, int numValues, lo_arg **values);
	
	void modeChannelSelectHandler(double deltaTime, vector<unsigned char> *message, int inputNumber);
	void modeChannelSelectNoteOn(unsigned char note, unsigned char velocity);
	void modeChannelSelectNoteOff(unsigned char note);
	void modeChannelSelectNoteOnCallback(const char *path, const char *types, int numValues, lo_arg **values);
	
	void modeConstantControllersHandler(double deltaTime, vector<unsigned char> *message, int inputNumber);	
	
	// ***** Member Variables *****
	
	PianoKeyboard& keyboard_;						// Reference to main keyboard data
	
	map<int, MidiInputCallback*> activePorts_;		// Sources of MIDI data
	set<int> activeChannels_;						// MIDI channels we listen to
	MidiOutputController *midiOutputController_;	// Destination for MIDI output
	
	// Current operating mode of the controller
	int mode_;
	
	// Mapping between input notes and output channels.  Depending on the mode of operation,
	// each note may be rebroadcast on its own MIDI channel.  Need to keep track of what goes where.
	// key is MIDI note #, value is output channel (0-15)
	map<int, int> retransmitChannelForNote_;
	set<int> retransmitChannelsAvailable_;
	int retransmitMaxPolyphony_;
	
	// Parameters for Channel Select mode of operation
	int channelSelectSwitchType_;
	int channelSelectNumberOfDivisions_;
	int channelSelectDefaultChannel_;
	int channelSelectLastOnsetChannel_;
    
    
    // for logging
    ofstream midiLog;
    
    // for generating timestamps
    Scheduler eventScheduler_;
};

#endif /* MIDI_INPUT_CONTROLLER_H */