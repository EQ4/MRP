/*
 *  MidiController.cpp
 *  keycontrol_cocoa
 *
 *  Created by Andrew McPherson on 6/8/11.
 *  Copyright 2011 __MyCompanyName__. All rights reserved.
 *
 */

#include "MidiInputController.h"
#include "MidiOutputController.h"

// Constructor

MidiInputController::MidiInputController(PianoKeyboard& keyboard) 
: keyboard_(keyboard), mode_(ModeOff), midiOutputController_(0)
{
	// Register for OSC messages from the internal keyboard source
	setOscController(&keyboard_);
    
    logFileCreated = false;
    loggingActive = false;
    
    // Start a thread by which we can generate timestamps
    eventScheduler_.start(0);
}

// ------------------------------------------------------
// create a new MIDI log file, ready to have data written to it
void MidiInputController::createLogFile(string midiLog_filename, string path)
{
    // indicate that we have created a log file (so we can close it later)
    logFileCreated = true;
    
    if (path.compare("") != 0)
    {
        path = path + "/";
    }
    
    midiLog_filename = path + midiLog_filename;
    
    char *fileName = (char*)midiLog_filename.c_str();
    
    // create output file
    midiLog.open (fileName, ios::out | ios::binary);
    midiLog.seekp(0);
}

// ------------------------------------------------------
// close the existing log file
void MidiInputController::closeLogFile()
{
    if (logFileCreated)
    {
        midiLog.close();
        logFileCreated = false;
    }
}

// ------------------------------------------------------
// start logging midi data
void MidiInputController::startLogging()
{
    loggingActive = true;
}

// ------------------------------------------------------
// stop logging midi data
void MidiInputController::stopLogging()
{
    loggingActive = false;
}

// Iterate through the available MIDI devices.  Return a vector containing
// indices and names for each device.  The index will later be passed back
// to indicate which device to open.

vector<pair<int, string> > MidiInputController::availableMidiDevices() {
	RtMidiIn rtMidiIn;
	
	vector<pair<int, string> > deviceList;
	
	try {
		int numDevices = rtMidiIn.getPortCount();
		
		for(int i = 0; i < numDevices; i++) {
			pair<int, string> p(i, rtMidiIn.getPortName(i));
			deviceList.push_back(p);
		}
	}
	catch(...) {
		deviceList.clear();
	}
	
	return deviceList;
}

// Enable a new MIDI port according to its index (returned from availableMidiDevices())
// Returns true on success.

bool MidiInputController::enablePort(int portNumber) {
	if(portNumber < 0)
		return false;
	
	try {
		MidiInputCallback *callback = new MidiInputCallback;
		RtMidiIn *rtMidiIn = new RtMidiIn;
		
		cout << "Enabling MIDI port " << portNumber << " (" << rtMidiIn->getPortName(portNumber) << ")\n";
		
		rtMidiIn->openPort(portNumber);				// Open the port
		rtMidiIn->ignoreTypes(true, true, true);	// Ignore sysex, timing, active sensing	
		
		callback->controller = this;
		callback->midiIn = rtMidiIn;
		callback->inputNumber = portNumber;
		
		rtMidiIn->setCallback(MidiInputController::rtMidiStaticCallback, callback);
		
		activePorts_[portNumber] = callback;
	}
	catch(...) {
		return false;
	}
	
	return true;
}

// Enable all current MIDI ports

bool MidiInputController::enableAllPorts() {
	bool enabledPort = false;
	vector<pair<int, string> > ports = availableMidiDevices();
	vector<pair<int, string> >::iterator it = ports.begin();
	
	while(it != ports.end()) {
		// Don't enable MIDI input from our own virtual output
		if(it->second != kMidiVirtualOutputName)
			enabledPort |= enablePort((it++)->first);
		else
			it++;
	}
	
	return enabledPort;
}

// Remove a specific MIDI input source and free associated memory

void MidiInputController::disablePort(int portNumber) {
	if(activePorts_.count(portNumber) <= 0)
		return;
	
	MidiInputCallback *callback = activePorts_[portNumber];	

	cout << "Disabling MIDI port " << portNumber << " (" << callback->midiIn->getPortName(portNumber) << ")\n";

	callback->midiIn->cancelCallback();
	delete callback->midiIn;
	delete callback;
	
	activePorts_.erase(portNumber);
}

// Remove all MIDI input sources and free associated memory

void MidiInputController::disableAllPorts() {
	map<int, MidiInputCallback*>::iterator it;
	
	cout << "Disabling all MIDI ports\n";
	
	it = activePorts_.begin();
	
	while(it != activePorts_.end()) {
		it->second->midiIn->cancelCallback();	// disable port
		delete it->second->midiIn;				// free RtMidiIn
		delete it->second;						// free MidiInputCallback
		it++;
	}
	
	activePorts_.clear();
}

// Listen on a given MIDI channel; returns true on success

bool MidiInputController::enableChannel(int channelNumber) {
	activeChannels_.insert(channelNumber);
	return true;
}

// Listen on all MIDI channels; returns true on success

bool MidiInputController::enableAllChannels() {
	for(int i = 0; i < 16; i++)
		activeChannels_.insert(i);
	return true;
}

// Disable listening to a specific MIDI channel

void MidiInputController::disableChannel(int channelNumber) {
	activeChannels_.erase(channelNumber);
}

// Disable all MIDI channels

void MidiInputController::disableAllChanels() {
	activeChannels_.clear();
}

// Disable any currently active notes

void MidiInputController::allNotesOff() {
	
}

// Set the operating mode of the controller.  The mode determines the behavior in
// response to incoming MIDI data.

void MidiInputController::setModeOff() {
	allNotesOff();
	removeAllOscListeners();
	mode_ = ModeOff;
}

void MidiInputController::setModePassThrough() {
	allNotesOff();
	removeAllOscListeners();
	mode_ = ModePassThrough;
}

void MidiInputController::setModePolyphonic(int maxPolyphony) {
	// First turn off any notes in the current mode
	allNotesOff();
	removeAllOscListeners();
	
	// Register a callback for touchkey data.  When we get a note-on message,
	// we request this callback occur once touch data is available.  In this mode,
	// we know the eventual channel before any touch data ever occurs: thus, we
	// only listen to the MIDI onset itself, which happens after all the touch
	// data is sent out.
	addOscListener("/midi/noteon");

	mode_ = ModePolyphonic;
	
	retransmitMaxPolyphony_ = maxPolyphony;
	if(retransmitMaxPolyphony_ > 16)
		retransmitMaxPolyphony_ = 16;	// Limit polyphony to 16 (number of MIDI channels
	for(int i = 0; i < retransmitMaxPolyphony_; i++)
		retransmitChannelsAvailable_.insert(i);
	retransmitChannelForNote_.clear();
}

void MidiInputController::setModeChannelSelect(int switchType, int numDivisions, int defaultChannel) {
	// First turn off any notes in the current mode
	allNotesOff();
	removeAllOscListeners();
	
	// Register a callback for touchkey data.  When we get a note-on message,
	// we request this callback occur once touch data is available.  In this mode, we
	// need to know about the touch before we can decide the channel, so we listen to both
	// pre-onset and onset messages
	addOscListener("/touchkeys/preonset");	
	addOscListener("/midi/noteon");	
	
	mode_ = ModeChannelSelect;
	channelSelectSwitchType_ = switchType;
	channelSelectNumberOfDivisions_ = numDivisions;
	channelSelectDefaultChannel_ = defaultChannel;
	
	if(channelSelectNumberOfDivisions_ < 1)
		channelSelectNumberOfDivisions_ = 1;
	if(channelSelectDefaultChannel_ >= channelSelectNumberOfDivisions_)
		channelSelectDefaultChannel_ = channelSelectNumberOfDivisions_ - 1;
	retransmitChannelForNote_.clear();	
	channelSelectLastOnsetChannel_ = 0;
}

// This gets called every time MIDI data becomes available on any input controller.  deltaTime gives us
// the time since the last event on the same controller, message holds a 3-byte MIDI message, and inputNumber
// tells us the number of the device that triggered it.

void MidiInputController::rtMidiCallback(double deltaTime, vector<unsigned char> *message, int inputNumber)
{
	// RtMidi will give us one MIDI command per callback, which makes processing easier for us.

	if(message == 0)
		return;
	if(message->size() == 0)	// Ignore empty messages
		return;	
	
    // if logging is active
    if (loggingActive)
    {
        ////////////////////////////////////////////////////////
        ////////////////////////////////////////////////////////
        //////////////////// BEGIN LOGGING /////////////////////
        
        int midi_channel = (int)((*message)[0]);
        int midi_number = (int)((*message)[1]);
        int midi_velocity = (int)((*message)[2]);
        timestamp_type timestamp = eventScheduler_.currentTimestamp();
        
        midiLog.write ((char*)&timestamp, sizeof (timestamp_type));
        midiLog.write ((char*)&midi_channel, sizeof (int));
        midiLog.write ((char*)&midi_number, sizeof (int));
        midiLog.write ((char*)&midi_velocity, sizeof (int));
        
        ///////////////////// END LOGGING //////////////////////
        ////////////////////////////////////////////////////////
        ////////////////////////////////////////////////////////
    }
        
#ifdef MIDI_INPUT_CONTROLLER_DEBUG_RAW
	cout << "MIDI Input " << inputNumber << ": ";
	for(int debugPrint = 0; debugPrint < message->size(); debugPrint++)
		printf("%x ", (*message)[debugPrint]);
	cout << endl;
#endif /* MIDI_INPUT_CONTROLLER_DEBUG_RAW */

	if(!messageIsForActiveChannel(message))
		return;
	
    ////
    
	switch(mode_) {
		case ModePassThrough:
			modePassThroughHandler(deltaTime, message, inputNumber);
			break;
		case ModeMonophonic:
			modeMonophonicHandler(deltaTime, message, inputNumber);
			break;
		case ModePolyphonic:
			modePolyphonicHandler(deltaTime, message, inputNumber);
			break;
		case ModeChannelSelect:
			modeChannelSelectHandler(deltaTime, message, inputNumber);
			break;
		case ModeConstantControllers:
			modeConstantControllersHandler(deltaTime, message, inputNumber);
			break;
		case ModeOff:
		default:
			// Ignore message
			break;
	}
}

// OscHandler method which parses incoming OSC messages we've registered for.  In this case,
// we use OSC callbacks to find out about touch data for notes we want to trigger.

bool MidiInputController::oscHandlerMethod(const char *path, const char *types, int numValues, lo_arg **values, void *data) {
	if(mode_ == ModePolyphonic) {
		modePolyphonicNoteOnCallback(path, types, numValues, values);
	}
	else if(mode_ == ModeChannelSelect) {
		modeChannelSelectNoteOnCallback(path, types, numValues, values);
	}
	
	return true;
}

// Destructor.  Free any existing callbacks

MidiInputController::~MidiInputController() {
    if (logFileCreated)
    {
        midiLog.close();
    }
	disableAllPorts();
}

// Return whether this message matches an active channel we're listening to.  Global
// MIDI events (without channels) match, as do specific messages on any of the enabled
// channels.  Returns true if the message should be further processed, false if it should
// be ignored.

bool MidiInputController::messageIsForActiveChannel(vector<unsigned char> *message) {
	if(message == 0)
		return false;
	if(message->size() == 0)
		return false;
	if(((*message)[0] & 0xF0) == 0xF0)	// System messages all begin with 0xF...
		return true;
	
	int channel = (*message)[0] & 0x0F;
	
	return (activeChannels_.count(channel) > 0);
}

// Mode-specific MIDI handlers.  These methods handle incoming MIDI data according to the rules
// defined by a particular mode of operation.

// Pass-Through: Retransmit any input data to the output unmodified.
void MidiInputController::modePassThroughHandler(double deltaTime, 
												 vector<unsigned char> *message, int inputNumber) {
	if(midiOutputController_ == 0)
		return;
	midiOutputController_->sendMessage(message);
}

void MidiInputController::modeMonophonicHandler(double deltaTime, 
												vector<unsigned char> *message, int inputNumber) {
}

// Polyphonic: Each incoming note gets its own unique MIDI channel so its controllers
// can be manipulated separately (e.g. by touchkey data).  Keep track of available channels
// and currently active notes.
void MidiInputController::modePolyphonicHandler(double deltaTime, 
												vector<unsigned char> *message, int inputNumber) {
	
	unsigned char command = (*message)[0];
	
	if(command == kMidiMessageReset) {
		// Reset state and pass along to all relevant channels
		
		retransmitChannelForNote_.clear();	// Clear current note information
		retransmitChannelsAvailable_.clear();		
		for(int i = 0; i < retransmitMaxPolyphony_; i++) {
			retransmitChannelsAvailable_.insert(i);
		}
		if(midiOutputController_ != 0)
			midiOutputController_->sendReset();		
	}
	else if(command < 0xF0) {
		// Commands below 0xF0 are channel-specific
		
		unsigned int filteredCommand = (unsigned int)(command & 0xF0);
		
		if(filteredCommand == kMidiMessageNoteOn) {
			// Note On message can mean three things:
			//   (1) a new note, for which we should allocate a new channel
			//   (2) a retrigger of an existing note, for which we should preserve the same channel
			//   (3) a note off (velocity 0), for which we should free the channel
			
			if(message->size() < 3)		// Check for degenerate messages
				return;
			if((*message)[2] == 0)		// Note Off (case 3)
				modePolyphonicNoteOff((*message)[1]);
			else if(retransmitChannelForNote_.count((*message)[1]) > 0) {
				// Case (2)-- retrigger an existing note
				if(midiOutputController_ != 0) {
					midiOutputController_->sendNoteOn(retransmitChannelForNote_[(*message)[1]], 
													  (*message)[1], (*message)[2]);
				}
			}
			else {
				// New note
				modePolyphonicNoteOn((*message)[1], (*message)[2]);
			}
		}
		else if(filteredCommand == kMidiMessageNoteOff) {
			if(message->size() < 2)	// Check for degenerate messages
				return;				// (we don't need the third byte in this case)
			modePolyphonicNoteOff((*message)[1]);
		}
		else if(filteredCommand == kMidiMessageControlChange) {
			// Control changes should be resent to all potentially active channels
			// This is true even for special messages like all Notes Off
			
			if(message->size() < 3)		// Check for degenerate messages
				return;
			
			if((*message)[1] == kMidiControlAllNotesOff) {
				retransmitChannelForNote_.clear();	// Clear current note information
				retransmitChannelsAvailable_.clear();
				for(int i = 0; i < retransmitMaxPolyphony_; i++)
					retransmitChannelsAvailable_.insert(i);
			}
			
			// All other control changes
			if(midiOutputController_ != 0) {
				for(int i = 0; i < retransmitMaxPolyphony_; i++) {
					midiOutputController_->sendControlChange(i, (*message)[1], (*message)[2]);
				}
			}
		}
		else if(filteredCommand == kMidiMessageProgramChange) {
			// Program changes should be resent to all potentially active channels
			if(message->size() < 2)		// Check for degenerate messages
				return;
			if(midiOutputController_ != 0) {
				for(int i = 0; i < retransmitMaxPolyphony_; i++) {
					midiOutputController_->sendProgramChange(i, (*message)[1]);
				}
			}			
		}
		else if(filteredCommand == kMidiMessagePitchWheel) {
			// Pitch wheel should be resent to all potentially active channels.
			// However, if another process (e.g. touchkeys) is already controlling
			// the pitch wheel on a particular note, we should add that value
			// rather than overriding it, which will produce unusual results.
			
			// TODO: pitch wheel
		}
		else if(filteredCommand == kMidiMessageAftertouchPoly) {
			// Polyphonic aftertouch should be directed to the channel allocated for
			// the particular note.
			if(message->size() < 3)		// Check for degenerate messages
				return;			
			if(retransmitChannelForNote_.count((*message)[1]) > 0) {		
				int retransmitChannel = retransmitChannelForNote_[(*message)[1]];
				if(midiOutputController_ != 0) {
					midiOutputController_->sendAftertouchPoly(retransmitChannel, (*message)[1], (*message)[2]);
				}
			}
		}
		else if(filteredCommand == kMidiMessageAftertouchChannel) {
			// Channel aftertouch covers all notes and should be sent to all currently
			// active channels.  Don't send to channels that don't have a note currently playing.
			
			if(message->size() < 2)		// Check for degenerate messages
				return;
			if(midiOutputController_ != 0) {
				for(map<int, int>::iterator it = retransmitChannelForNote_.begin(); 
					it != retransmitChannelForNote_.end(); ++it) {
					midiOutputController_->sendAftertouchChannel(it->second, (*message)[1]);
				}
			}
		}
	}	
}

// Handle note on message in polyphonic mode.  Allocate a new channel
// for this note and rebroadcast it.
void MidiInputController::modePolyphonicNoteOn(unsigned char note, unsigned char velocity) {
	if(retransmitChannelsAvailable_.size() == 0) {
		// No channels available.  Print a warning and finish
		cout << "No MIDI output channel available for note " << (int)note << endl;
		return;
	}
	
	// Request the first available channel
	int newChannel = *retransmitChannelsAvailable_.begin();
	retransmitChannelsAvailable_.erase(newChannel);
	retransmitChannelForNote_[note] = newChannel;
	
	if(keyboard_.key(note) != 0) {
		keyboard_.key(note)->midiNoteOn(velocity, newChannel, keyboard_.schedulerCurrentTimestamp());
	}
	
	// The above function will cause a callback to be generated, which in turn will generate
	// the Note On message.
}

// Handle note off message in polyphonic mode.  Release any channel
// associated with this note.
void MidiInputController::modePolyphonicNoteOff(unsigned char note) {
	// If no channel associated with this note, ignore it
	if(retransmitChannelForNote_.count(note) == 0)
		return;
	
	if(keyboard_.key(note) != 0) {
		keyboard_.key(note)->midiNoteOff(keyboard_.schedulerCurrentTimestamp());
	}
	
	// Send a Note Off message to the appropriate channel
	if(midiOutputController_ != 0) {
		midiOutputController_->sendNoteOff(retransmitChannelForNote_[note], note);
	}
	
	// Now release the channel mapping associated with this note
	retransmitChannelsAvailable_.insert(retransmitChannelForNote_[note]);
	retransmitChannelForNote_.erase(note);
}

// Callback function after we request a note on.  PianoKey class will respond
// with touch data (if available within a specified timeout), or with a frame
// indicating an absence of touch data.  Once we receive this, we can send the
// MIDI note on message.

void MidiInputController::modePolyphonicNoteOnCallback(const char *path, const char *types, int numValues, lo_arg **values) {
	if(numValues < 3)	// Sanity check: first 3 values hold MIDI information
		return;
	if(types[0] != 'i' || types[1] != 'i' || types[2] != 'i')
		return;
	
	int midiNote = values[0]->i;
	int midiChannel = values[1]->i;
	int midiVelocity = values[2]->i;
	
	if(midiNote < 0 || midiNote > 127)
		return;	
	
	// Send the Note On message to the correct channel
	if(midiOutputController_ != 0) {
		midiOutputController_->sendNoteOn(midiChannel, midiNote, midiVelocity);
	}	
}

// Channel Select: In this mode, the incoming MIDI message is redirected to one of
// several channels depending on the state of a variable, for example the touch location
// or number of touches.
void MidiInputController::modeChannelSelectHandler(double deltaTime, 
												   vector<unsigned char> *message, int inputNumber) {
	unsigned char command = (*message)[0];
	
	if(command == kMidiMessageReset) {
		// Reset state and pass along to all relevant channels
		
		retransmitChannelForNote_.clear();	// Clear current note information
		if(midiOutputController_ != 0)
			midiOutputController_->sendReset();	
	}
	else if(command < 0xF0) {
		// Commands below 0xF0 are channel-specific
		
		unsigned int filteredCommand = (unsigned int)(command & 0xF0);
		
		if(filteredCommand == kMidiMessageNoteOn) {
			// Note On message can mean either a new note onset or a note off (velocity 0)
			
			if(message->size() < 3)		// Check for degenerate messages
				return;
			if((*message)[2] == 0)		// Note Off (case 3)
				modeChannelSelectNoteOff((*message)[1]);
			else {
				// New note onset.  Decide where it should go based on selection criteria.
				modeChannelSelectNoteOn((*message)[1], (*message)[2]);
			}
		}
		else if(filteredCommand == kMidiMessageNoteOff) {
			if(message->size() < 2)	// Check for degenerate messages
				return;				// (we don't need the third byte in this case)
			modeChannelSelectNoteOff((*message)[1]);
		}
		else if(filteredCommand == kMidiMessageControlChange) {
			// Control changes should be resent to all potentially active channels
			// This is true even for special cases like All Notes Off

			if(message->size() < 3)		// Check for degenerate messages
				return;
			
			if((*message)[1] == kMidiControlAllNotesOff) {
				retransmitChannelForNote_.clear();	// Clear current note information
			}
			
			// All other control changes
			if(midiOutputController_ != 0) {
				for(int i = 0; i < channelSelectNumberOfDivisions_; i++) {
					midiOutputController_->sendControlChange(i, (*message)[1], (*message)[2]);
				}
			}
		}
		else if(filteredCommand == kMidiMessageProgramChange) {
			// It is expected that different channels maintain different programs.
			// Thus, program change messages should affect one channel only, which
			// we define to be the channel of the last note onset.
			
			if(message->size() < 2)		// Check for degenerate messages
				return;
			if(midiOutputController_ != 0) {
				midiOutputController_->sendProgramChange(channelSelectLastOnsetChannel_, (*message)[1]);
			}			
		}
		else if(filteredCommand == kMidiMessagePitchWheel) {
			// Pitch wheel should be resent to all potentially active channels.

			if(message->size() < 3)		// Check for degenerate messages
				return;
			unsigned int pitchValue = (unsigned int)(*message)[2] << 7 + (unsigned int)(*message)[1];
			if(midiOutputController_ != 0) {
				for(int i = 0; i < channelSelectNumberOfDivisions_; i++) {
					midiOutputController_->sendPitchWheel(i, pitchValue);
				}	
			}
		}
		else if(filteredCommand == kMidiMessageAftertouchPoly) {
			// Polyphonic aftertouch should be directed to the channel allocated for
			// the particular note.
			if(message->size() < 3)		// Check for degenerate messages
				return;			
			if(retransmitChannelForNote_.count((*message)[1]) > 0) {		
				int retransmitChannel = retransmitChannelForNote_[(*message)[1]];
				if(midiOutputController_ != 0) {
					midiOutputController_->sendAftertouchPoly(retransmitChannel, (*message)[1], (*message)[2]);
				}
			}
		}
		else if(filteredCommand == kMidiMessageAftertouchChannel) {
			// Channel aftertouch covers all notes and should be sent to all currently
			// active channels.  Don't send to channels that don't have a note currently playing.
			
			if(message->size() < 2)		// Check for degenerate messages
				return;
			if(midiOutputController_ != 0) {
				// A given channel might have more than one active note, but we don't want to
				// send it multiple copies of the message.
				set<bool> sentToChannel;
				for(map<int, int>::iterator it = retransmitChannelForNote_.begin(); 
						it != retransmitChannelForNote_.end(); ++it) {
					if(sentToChannel.count(it->second) == 0) {
						midiOutputController_->sendAftertouchChannel(it->second, (*message)[1]);
						sentToChannel.insert(it->second);
					}
				}
			}
		}
	}	
}

// Handle note on message in channel select mode.  The note will be sent to one of
// several channels, depending on the state of a predetermined selection criteria.
void MidiInputController::modeChannelSelectNoteOn(unsigned char note, unsigned char velocity) {
	// For now, rebroadcast this note on the default channel.  This will be updated once the
	// callback occurs.
	retransmitChannelForNote_[note] = channelSelectLastOnsetChannel_ = channelSelectDefaultChannel_;
	
	if(keyboard_.key(note) != 0) {
		keyboard_.key(note)->midiNoteOn(velocity, channelSelectLastOnsetChannel_, keyboard_.schedulerCurrentTimestamp());
	}

	// The above function will generate a callback on which basis we can assign a 
	// channel and eventually retransmit the note.
}

// Handle note off message in channel select mode.
void MidiInputController::modeChannelSelectNoteOff(unsigned char note) {
	// If no channel associated with this note, ignore it
	if(retransmitChannelForNote_.count(note) == 0)
		return;
	
	if(keyboard_.key(note) != 0) {
		keyboard_.key(note)->midiNoteOff(keyboard_.schedulerCurrentTimestamp());
	}
	
	// Send a Note Off message to the appropriate channel
	if(midiOutputController_ != 0) {
		midiOutputController_->sendNoteOff(retransmitChannelForNote_[note], note);
	}
	
	// Now release the channel mapping associated with this note
	retransmitChannelForNote_.erase(note);
}

// Callback function after we request a note on.  PianoKey class will respond
// with touch data (if available within a specified timeout), or with a frame
// indicating an absence of touch data.  We will use this to decide what channel
// the MIDI note should be directed to, at which point we can send controller values
// before the Note On message

void MidiInputController::modeChannelSelectNoteOnCallback(const char *path, const char *types, int numValues, lo_arg **values) {
	// We get two kinds of messages here: /touchkeys/preonset and /midi/noteon
	// Both start with the same 3 fields (MIDI note, channel, velocity)
	// We treat them differently: preonset determines our channel, noteon actually
	// causes the MIDI note on to be sent.  Between these two messages, controller values
	// might be updated if any mappings are present.
	
	if(numValues < 3)	// Sanity check: first 3 values hold MIDI information
		return;
	if(types[0] != 'i' || types[1] != 'i' || types[2] != 'i')
		return;
	
	int midiNote = values[0]->i;
	int midiVelocity = values[2]->i;
	
	if(midiNote < 0 || midiNote > 127)
		return;
	
	// In this mode, the supposed MIDI channel doesn't tell us anything because we didn't
	// know it when we started the callback process.
	
	if(!strcmp(path, "/midi/noteon") && midiOutputController_ != 0) {
		// Send the Note On message to the correct channel
		midiOutputController_->sendNoteOn(retransmitChannelForNote_[midiNote], midiNote, midiVelocity);
	}		
	else if(numValues >= 15) {
		// Checking numValues is a quicker sanity check that comparing the whole string,
		// and in any case this will prevent us from crashing if a malformed message comes through.

		if(channelSelectSwitchType_ == ChannelSelectSwitchTypeLocation) {
			// Look for the location of the first touch on this key
			int numTouches = values[3]->i;
			
			cout << "numTouches = " << numTouches << endl;
			if(numTouches > 0) {
				int firstTouch = values[4]->i;
				cout << "firstTouch = " << firstTouch << endl;
				if(firstTouch < 0 || firstTouch > 2)
					channelSelectLastOnsetChannel_ = channelSelectDefaultChannel_;	
				else {
					// Get vertical location corresponding to first touch
					float location = values[6 + 3*firstTouch]->f;
					float normalizedLocation = location * (float)channelSelectNumberOfDivisions_;
					
					cout << "location = " << location << " norm = " << normalizedLocation << endl;
					
					// Round down to get the channel number to send to
					channelSelectLastOnsetChannel_ = (int)floorf(normalizedLocation);
				}
			}
			else
				channelSelectLastOnsetChannel_ = channelSelectDefaultChannel_;		
		}
		else if(channelSelectSwitchType_ == ChannelSelectSwitchTypeSize) {
			// Look for the size of the first touch on this key
			int numTouches = values[3]->i;
			if(numTouches > 0) {
				int firstTouch = values[4]->i;
				if(firstTouch < 0 || firstTouch > 2)
					channelSelectLastOnsetChannel_ = channelSelectDefaultChannel_;	
				else {
					// Get vertical location corresponding to first touch
					float size = values[7 + 3*firstTouch]->f;
					float normalizedSize = size * (float)channelSelectNumberOfDivisions_;
					
					// Round down to get the channel number to send to
					channelSelectLastOnsetChannel_ = (int)floorf(normalizedSize);
				}
			}
			else
				channelSelectLastOnsetChannel_ = channelSelectDefaultChannel_;					
		}
		else if(channelSelectSwitchType_ == ChannelSelectSwitchTypeNumTouches) {
			// Select based on the number of touches (at most 3 divisions)
			int numTouches = values[3]->i;
			int actualMaxDivisions = min(channelSelectNumberOfDivisions_, 3);
			
			if(numTouches <= 0) // No apparent touch-- use default
				channelSelectLastOnsetChannel_ = channelSelectDefaultChannel_;
			else {
				// Select channel based on number of touches: 1 touch --> 0, 2 touches --> 1, 3 touches --> 2
				// unless there are fewer divisions, in which case limit the maximum channel number
				channelSelectLastOnsetChannel_ = min(numTouches - 1, actualMaxDivisions - 1);
			}
		}
		else if(channelSelectSwitchType_ == ChannelSelectSwitchTypeAngle) {
			// TODO: onset angle
		}
		else {
			// Unknown switch type
			channelSelectLastOnsetChannel_ = channelSelectDefaultChannel_;	
		}
	
		// Store the channel for this note; later we will get another message to
		// send the Note On message itself
		retransmitChannelForNote_[midiNote] = channelSelectLastOnsetChannel_;	
		
		// Let the key know about its new channel, for the purpose of future control-change messages
		if(keyboard_.key(midiNote) != 0)
			keyboard_.key(midiNote)->changeMidiChannel(channelSelectLastOnsetChannel_);
	}
}

void MidiInputController::modeConstantControllersHandler(double deltaTime, 
														 vector<unsigned char> *message, int inputNumber) {
}
