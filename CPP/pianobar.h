/*
 *  pianobar.h
 *  kblisten
 *
 *  Created by Andrew McPherson on 2/10/10.
 *  Copyright 2010 __MyCompanyName__. All rights reserved.
 *
 */

#define DEBUG_MESSAGES
#undef DEBUG_MESSAGES_EXTRA
#undef DEBUG_ALLOCATION
#define DEBUG_MESSAGE_SAMPLE_INTERVAL	8192

#define DEBUG_OSC
#define OSC_COUNTER_INTERVAL 50

#ifndef PIANOBAR_H
#define PIANOBAR_H

#include <iostream>
#include <cmath>
#include <vector>
#include <deque>
#include <set>
#include <map>
#include <fstream>
#include <pthread.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdarg.h>
#include <sys/stat.h>
#include "portaudio.h"
#include "lo/lo.h"
#ifdef __APPLE__
#include "pa_mac_core.h"
#endif
//#include "osc.h"

using namespace std;

#define DEBUG_CALIBRATION
#define DEBUG_STATES

// Mapping of data bins to MIDI note numbers for Moog Piano Bar.  Left to right represents pads "GRP1" to "GRP12" on
// scanner bar.  Top to bottom represents 18 successive values after a sync pulse.

const short kPianoBarMapping[18][12] = {						
	{24, 26, 41, 43, 52, 53, 69, 71, 81, 83, 98, 100},		
	{31, 33, 0,  0,  59, 60, 76, 0,  88, 89, 105, 107},
	{22, 25, 37, 39, 49, 51, 63, 66, 78, 80, 92, 94},
	{32, 34, 46, 0,  58, 61, 73, 75, 87, 90, 102, 104},
	{27, 30, 42, 44, 54, 56, 68, 70, 82, 85, 97, 99},
	{35, 36, 0,  0,  62, 64, 0,  0,  91, 93, 106, 108},
	{21, 23, 38, 40, 48, 50, 65, 67, 81, 83, 98, 100},
	{35, 36, 0,  0,  62, 64, 0,  0,  91, 93, 106, 108},
	{22, 25, 37, 39, 49, 51, 63, 66, 78, 80, 92, 94},
	{32, 34, 46, 0,  58, 61, 73, 75, 87, 90, 102, 104},
	{27, 30, 42, 44, 54, 56, 68, 70, 82, 85, 97, 99},	
	{35, 36, 0,  0,  62, 64, 0,  0,  91, 93, 106, 108},	
	{21, 23, 38, 40, 48, 50, 65, 67, 77, 79, 95, 96},
	{28, 29, 47, 45, 55, 57, 72, 74, 84, 86, 101, 103},
	{22, 25, 37, 39, 49, 51, 63, 66, 78, 80, 92, 94},
	{32, 34, 46, 0,  58, 61, 73, 75, 87, 90, 102, 104},
	{27, 30, 42, 44, 54, 56, 68, 70, 82, 85, 97, 99},
	{35, 36, 0,  0,  62, 64, 0,  0,  91, 93, 106, 108}};

// Signal identities: the white keys and black keys on the Piano Bar operate
// differently (white = reflectance, black = breakbeam).  What's more, those
// keys that repeat within a cycle may not have the same exact lighting amplitude
// each time, for which we may need to compensate.

enum {
	PB_NA = 0,		// Unknown / blank (not all signal bins in PB data represent actual keys)
	PB_W1 = 1,		// White keys
	PB_W2 = 2,
	PB_W3 = 3,
	PB_W4 = 4,
	PB_B1 = 5,		// Black keys
	PB_B2 = 6,
	PB_B3 = 7,
	PB_B4 = 8
};

enum {				// Key color
	K_W = 0,
	K_B = 1
};

const short kPianoBarSignalTypes[18][12] = {
	{PB_W1, PB_W1, PB_W1, PB_W1, PB_W1, PB_W1, PB_W1, PB_W1, PB_W1, PB_W1, PB_W1, PB_W1},
	{PB_W1, PB_W1, PB_NA, PB_NA, PB_W1, PB_W1, PB_W1, PB_NA, PB_W1, PB_W1, PB_W1, PB_W1},
	{PB_B1, PB_B1, PB_B1, PB_B1, PB_B1, PB_B1, PB_B1, PB_B1, PB_B1, PB_B1, PB_B1, PB_B1},
	{PB_B1, PB_B1, PB_B1, PB_NA, PB_B1, PB_B1, PB_B1, PB_B1, PB_B1, PB_B1, PB_B1, PB_B1},
	{PB_B1, PB_B1, PB_B1, PB_B1, PB_B1, PB_B1, PB_B1, PB_B1, PB_B1, PB_B1, PB_B1, PB_B1},
	{PB_W1, PB_W1, PB_NA, PB_NA, PB_W1, PB_W1, PB_NA, PB_NA, PB_W1, PB_W1, PB_B1, PB_W1},
	{PB_W1, PB_W1, PB_W1, PB_W1, PB_W1, PB_W1, PB_W1, PB_W1, PB_W2, PB_W2, PB_W2, PB_W2},
	{PB_W2, PB_W2, PB_NA, PB_NA, PB_W2, PB_W2, PB_NA, PB_NA, PB_W2, PB_W2, PB_B2, PB_W2},
	{PB_B2, PB_B2, PB_B2, PB_B2, PB_B2, PB_B2, PB_B2, PB_B2, PB_B2, PB_B2, PB_B2, PB_B2},
	{PB_B2, PB_B2, PB_B2, PB_NA, PB_B2, PB_B2, PB_B2, PB_B2, PB_B2, PB_B2, PB_B2, PB_B2},
	{PB_B2, PB_B2, PB_B2, PB_B2, PB_B2, PB_B2, PB_B2, PB_B2, PB_B2, PB_B2, PB_B2, PB_B2},
	{PB_W3, PB_W3, PB_NA, PB_NA, PB_W3, PB_W3, PB_NA, PB_NA, PB_W3, PB_W3, PB_B3, PB_W3},
	{PB_W2, PB_W2, PB_W2, PB_W2, PB_W2, PB_W2, PB_W2, PB_W2, PB_W1, PB_W1, PB_W1, PB_W1},
	{PB_W1, PB_W1, PB_W1, PB_W1, PB_W1, PB_W1, PB_W1, PB_W1, PB_W1, PB_W1, PB_W1, PB_W1},
	{PB_B3, PB_B3, PB_B3, PB_B3, PB_B3, PB_B3, PB_B3, PB_B3, PB_B3, PB_B3, PB_B3, PB_B3},
	{PB_B3, PB_B3, PB_B3, PB_NA, PB_B3, PB_B3, PB_B3, PB_B3, PB_B3, PB_B3, PB_B3, PB_B3},
	{PB_B3, PB_B3, PB_B3, PB_B3, PB_B3, PB_B3, PB_B3, PB_B3, PB_B3, PB_B3, PB_B3, PB_B3},
	{PB_W4, PB_W4, PB_NA, PB_NA, PB_W4, PB_W4, PB_NA, PB_NA, PB_W4, PB_W4, PB_B4, PB_W4}};	

const short kPianoBarKeyColor[88] = {			 K_W, K_B, K_W,		// Octave 0
	K_W, K_B, K_W, K_B, K_W, K_W, K_B, K_W, K_B, K_W, K_B, K_W,		// Octave 1
	K_W, K_B, K_W, K_B, K_W, K_W, K_B, K_W, K_B, K_W, K_B, K_W,		// Octave 2
	K_W, K_B, K_W, K_B, K_W, K_W, K_B, K_W, K_B, K_W, K_B, K_W,		// Octave 3
	K_W, K_B, K_W, K_B, K_W, K_W, K_B, K_W, K_B, K_W, K_B, K_W,		// Octave 4
	K_W, K_B, K_W, K_B, K_W, K_W, K_B, K_W, K_B, K_W, K_B, K_W,		// Octave 5
	K_W, K_B, K_W, K_B, K_W, K_W, K_B, K_W, K_B, K_W, K_B, K_W,		// Octave 6
	K_W, K_B, K_W, K_B, K_W, K_W, K_B, K_W, K_B, K_W, K_B, K_W,		// Octave 7
	K_W };															// Octave 8

extern const char *kNoteNames[88];

#define NUM_WHITE_KEYS 52
#define NUM_BLACK_KEYS 36

#define DEFAULT_HISTORY_LENGTH (1.0)

#define GLOBAL_HISTORY_LENGTH (32768)	// About 3 seconds of audio

// Calibration status of the Piano Bar.  This compensates for variations in mechanical position, light level, etc.
enum {
	kPianoBarNotCalibrated = 0,
	kPianoBarCalibrated,
	kPianoBarInCalibration,
	kPianoBarCalibratedButNoUpdates,
	kPianoBarAutoCalibration
};

// Possible states for each key to be in.  Conventional MIDI representations only admit Idle, Press, Down, and Release
// (and sometimes Aftertouch) but we can track a variety of extended keyboard gestures.

enum {
	kKeyStateUnknown = 0,		// Will be in unknown state before calibration
	kKeyStateIdle,
	kKeyStatePretouch,
	kKeyStatePreVibrato,
	kKeyStateTap,
	kKeyStatePress,
	kKeyStateDown,
	kKeyStateAftertouch,
	kKeyStateAfterVibrato,
	kKeyStateRelease,
	kKeyStatePostRelease,
	kKeyStateDisabledByCalibrator,
	kKeyStateDisabledByUser,
	kKeyStateInitialActivity,
	kKeyStatePartialMax,
	kKeyStateRestrike,
	kKeyStatesLength
};

enum {
	kPedalStateUnknown = 0,
	kPedalStateUp,
	kPedalStateDown,
	kPedalStateDisabledByCalibrator,
	kPedalStateDisabledByUser,
	kPedalStatesLength
};

// Status of piano bar data logging

enum {
	kRecordStatusOff = 0,		// Off (but can be enabled by user)
	kRecordStatusContinuous,	// Always record
	kRecordStatusOnActivity,	// Record when keys go active
	kRecordStatusDisabled,		// Disabled (can't be enabled by user)
	
	// ** These only for specific keys:
	kRecordStatusShouldStartRecording, // Open the file and record at the next opportunity
	kRecordStatusRecordingEvent,	// Currently recording an event
	kRecordStatusPostEvent			// Recording after the completion of an event
};

enum {							// Index of each pedal in the buffers
	kPedalDamper = 0,
	kPedalSostenuto = 1,
	kPedalUnaCorda = 2
};

enum {
	kFeatureTypePartialPress = 0,
	kFeatureTypeDown = 1,
	kFeatureTypeRelease = 2,
	kFeatureTypeAfter = 3
};

enum {
	kKeyPointTypeStart = 0,
	kKeyPointTypeMin,
	kKeyPointTypeMax
};

enum {							// MIDI messages
	MESSAGE_NOTEOFF = 0x80,
	MESSAGE_NOTEON = 0x90,
	MESSAGE_AFTERTOUCH_POLY = 0xA0,
	MESSAGE_CONTROL_CHANGE = 0xB0,
	MESSAGE_PROGRAM_CHANGE = 0xC0,
	MESSAGE_AFTERTOUCH_CHANNEL = 0xD0,
	MESSAGE_PITCHWHEEL = 0xE0,
	MESSAGE_SYSEX = 0xF0,
	MESSAGE_SYSEX_END = 0xF7,
	MESSAGE_ACTIVE_SENSE = 0xFE,
	MESSAGE_RESET = 0xFF
};

enum {							// Partial listing of MIDI controllers
	CONTROL_BANK_SELECT = 0,
	CONTROL_MODULATION_WHEEL = 1,
	CONTROL_VOLUME = 7,
	CONTROL_PATCH_CHANGER = 14,	// Piano bar patch-changing interface (deprecate this?)
	CONTROL_AUX_PEDAL = 15,		// Use this as an auxiliary pedal
	CONTROL_MRP_BASE = 16,		// Base of a range of controllers used by MRP signal routing hardware
	CONTROL_BANK_SELECT_LSB = 32,
	CONTROL_MODULATION_WHEEL_LSB = 33,
	CONTROL_VOLUME_LSB = 39,
	CONTROL_DAMPER_PEDAL = 64,
	CONTROL_SOSTENUTO_PEDAL = 66,
	CONTROL_SOFT_PEDAL = 67,
	CONTROL_ALL_SOUND_OFF = 120,
	CONTROL_ALL_CONTROLLERS_OFF = 121,
	CONTROL_LOCAL_KEYBOARD = 122,
	CONTROL_ALL_NOTES_OFF = 123,
	CONTROL_OMNI_OFF = 124,
	CONTROL_OMNI_ON = 125,
	CONTROL_MONO_OPERATION = 126,
	CONTROL_POLY_OPERATION = 127
};

#define PIANO_BAR_SAMPLE_RATE (10700)
#define PIANO_BAR_FRAME_SIZE (20)			// Bytes per frame (10 channels x 16 bits)
#define UNCALIBRATED (short)(0x7FFF)		// Initial value for calibration settings
#define OLD_VELOCITY_SCALER 64				// Amount by which we mulitply velocity results for better (int) resolution
#define VELOCITY_SCALER 25087				// Amount by which velocity history is scaled ((4096*65536)/PIANO_BAR_SAMPLE_RATE)
#define STATE_HISTORY_LENGTH 8				// How many old states to save for each key
#define KEY_MESSAGE_UPDATE_INTERVAL 36		// Send new data to the synth every 2 piano bar cycles
#define POST_PRESS_OSC_WAIT_TIME 535		// Wait 50ms after key down to send key data blob
#define POST_PRESS_FEATURE_WAIT_TIME 300	// Wait around 30ms before sending features
#define POST_PRESS_AFTER_FEATURE_WAIT_TIME 600 // Wait around 60ms before sending features
#define WARP_TABLE_SIZE 16384				// Size of the table mapping sensor reading to actual position

#define DATA_ERROR_MAX_MESSAGES 10
#define DATA_ERROR_MESSAGE_SPACING 21400	// 2 seconds

#define KEY_POSITION_DAMPER 1217			// FIXME: make these dynamic
#define KEY_POSITION_ESCAPEMENT 2657
#define KEY_POSITION_FELT_PAD 3713

#define NUM_OSC_FEATURES 16

typedef unsigned long long pb_timestamp;				// Timestamp for each key data point
typedef long long pb_time_offset;						// Difference in timestamps
typedef void (*key_action)(int key, void *userData);	// Function that can be scheduled to run at a particular time

//class OscTransmitter;
//class OscReceiver;
//class OscHandler;

class PianoBarController
{
public:
	PianoBarController();
	
	bool initialize();
	bool openDevice(PaDeviceIndex inputDeviceNum, int bufferSize);
	bool startDevice();		// Start, stop, and close control the stream supplied by open.  start and stop begin
	bool stopDevice();		// and end data capture, where close releases everything allocated by open
	void closeDevice();		// These return true on success.

	bool playRawFile(const char *filename, pb_timestamp startTimestamp, vector<float> offsets);	
	
	// Call these after the device is running.  Calibrate specific PB data.
	bool startCalibration(vector<int> &keysToCalibrate, vector<int> &pedalsToCalibrate, bool quiescentOnly);
	void stopCalibration();
	void clearCalibration();
	void calibratePedalMinPress(int pedal);
	void setUseKeysAndPedals(bool enableKeys, bool enableDamper, bool enableSost, bool enableUnaCorda);
	
	bool saveCalibrationToFile(string& filename);	// Save calibration settings to a file
	bool loadCalibrationFromFile(string& filename);	// Load calibration settings from a file
	void startAutoCalibration();
	void stopAutoCalibration();
	
	bool isCalibrating() { return (calibrationStatus_ == kPianoBarInCalibration); }
	bool isInitialized() { return isInitialized_; }
	bool isRunning() { return isRunning_; }
	
	void printKeyStatus();									// Print the current status of each key
	void printIndividualKeyPosition(int key);				// Information on the current state of a key
	
	bool setRecordSettings(int recordStatus, vector<int> *recordKeys, const char *recordDirectory, bool recordPedals,
						   bool recordRaw, bool recordEvents, float recordPreTime, float recordPostTime, float recordSpacing);
	
	static int staticAudioCallback(const void *input, void *output, unsigned long frameCount, 
								   const PaStreamCallbackTimeInfo* timeInfo,
								   PaStreamCallbackFlags statusFlags, void *userData) {
		return ((PianoBarController *)userData)->audioCallback(input, output, frameCount, timeInfo, statusFlags);
	}	
	
	void *recordLoop();										// This loop handles the writing of key data to disk
	static void *staticRecordLoop(void *arg) {
		return ((PianoBarController *)arg)->recordLoop();
	}
	
	~PianoBarController();
	
private:
	void printKeyStatusHelper(int start, int length, int padSpaces);	// Helper for printKeyStatus()
	
	// audioCallback() does the real heavy lifting.  staticAudioCallback is just there to provide a hook
	// for portaudio into this object.
	
	int audioCallback(const void *input, void *output,
					  unsigned long frameCount,
					  const PaStreamCallbackTimeInfo* timeInfo,
					  PaStreamCallbackFlags statusFlags);
	void processKeyValue(short midiNote, short type, short value);
	void processPedalValue(int pedal, short value);
	
	void processPianoBarFrame(short *inData, int readCount);
	
	int lastKeyPosition(int key) { return keyHistory_[key][keyHistoryPosition_[key]]; }
	int lastPedalPosition(int pedal) { return pedalHistory_[pedal][pedalHistoryPosition_[pedal]]; }
	int rawRunningPositionAverage(int key, int offset, int length);
	int runningPositionAverage(int key, int offset, int length);
	int runningVelocityAverage(int key, int offset, int length);
	int runningAccelerationAverage(int key, int offset, int length);
	int runningPedalAverage(int pedal, int offset, int length);
	int peakAcceleration(int key, int offset, int distanceToSearch, int samplesToAverage, bool positive);
	int pedalCalibrationRunningAverage(int pedal, int offset, int length);
	int calibrationRunningAverage(int key, int seq, int offset, int length);
	void updateNoiseFloors();
	void updateFlatnessTolerance();
	int calculateKeyVelocity(int key, int index) {	// Calculate the instantaneous velocity based on position and timestamps
		int lastIndex = (index - 1 + keyHistoryLength_[key]) % keyHistoryLength_[key];
		int currentPosition = keyHistory_[key][index], lastPosition = keyHistory_[key][lastIndex];
		int currentTimestamp = (int)keyHistoryTimestamps_[key][index], lastTimestamp = keyHistoryTimestamps_[key][lastIndex];
		
		// Use integer math for speed.  Key positions are nominally 12-bit, 0 to 4096, but could conceivably range -8192 to 8192 (14 bits).
		// The fastest on most platforms is probably to give an extra 16 bits for the calculation before performing integer division
		// Resulting scale, compared to 0-1 position values: (pos_diff*4096*65536)/(time_diff*10700) ---> *25087
		
		int magnifiedDifference = (currentPosition - lastPosition) * 65536;	// 2^16
		int timeDifference = currentTimestamp - lastTimestamp;
		
		if(timeDifference == 0)
			timeDifference = 1;
		return magnifiedDifference / timeDifference;
	}
	
	void updateKeyState(int key);
	void updatePedalState(int pedal);
	int updateSumAndFlatness(int key);
	void sendKeyStateMessages();
	void sendKeyDownFeatures(int key, void *userData);							
	void sendKeyDownDetailedData(int key, void *userData);
	void sendKeyPartialPressFeatures(int key, void *userData);							
	void sendKeyPartialPressDetailedData(int key, void *userData);
	void sendKeyAfterFeatures(int key, void *userData);
	void sendKeyReleaseFeatures(int key, void *userData);
	void sendKeyPercussiveMidiOn(int key, void *userData);
	void sendKeyPercussiveMidiOff(int key, void *userData);
	
	static void staticSendKeyDownFeatures(int key, void *userData) {			// key_action function
		((PianoBarController *)userData)->sendKeyDownFeatures(key, NULL);
	}	
	static void staticSendKeyDownDetailedData(int key, void *userData) {		// key_action function
		((PianoBarController *)userData)->sendKeyDownDetailedData(key, NULL);
	}
	static void staticSendKeyPartialPressFeatures(int key, void *userData) {			// key_action function
		((PianoBarController *)userData)->sendKeyPartialPressFeatures(key, NULL);
	}	
	static void staticSendKeyPartialPressDetailedData(int key, void *userData) {		// key_action function
		((PianoBarController *)userData)->sendKeyPartialPressDetailedData(key, NULL);
	}
	static void staticSendKeyAfterFeatures(int key, void *userData) {			// key_action function
		((PianoBarController *)userData)->sendKeyAfterFeatures(key, NULL);
	}		
	static void staticSendKeyPercussiveMidiOn(int key, void *userData) {		// key_action function
		((PianoBarController *)userData)->sendKeyPercussiveMidiOn(key, NULL);
	}	
	static void staticSendKeyPercussiveMidiOff(int key, void *userData) {		// key_action function
		((PianoBarController *)userData)->sendKeyPercussiveMidiOff(key, NULL);
	}	
	
	int keyHistoryOffset(int key, int offset) {	// Circular buffer position relative to current
		return (keyHistoryPosition_[key] - offset + keyHistoryLength_[key]) % keyHistoryLength_[key];
	}
	int pedalHistoryOffset(int pedal, int offset) {	// Circular buffer position relative to current
		return (pedalHistoryPosition_[pedal] - offset + pedalHistoryLength_[pedal]) % pedalHistoryLength_[pedal];
	}	
	int keyDistanceFromFeature(int key, int pos1, int pos2) {	// How far away are pos1 and pos2 in the circular buffer?
		return (pos2 >= pos1 ? pos2-pos1 : pos2-pos1+keyHistoryLength_[key]);
	}
	int pedalDistanceFromFeature(int pedal, int pos1, int pos2) {	// How far away are pos1 and pos2 in the circular buffer?
		return (pos2 >= pos1 ? pos2-pos1 : pos2-pos1+pedalHistoryLength_[pedal]);
	}	
				
	void handleMultiKeyPitchBend(set<int> *keysToSkip);
	void handleMultiKeyHarmonicSweep(set<int> *keysToSkip);
	
	int currentKeyState(int key);								// Return current key state
	int currentPedalState(int pedal);
	int previousKeyState(int key);								// Return key state before this one
	int previousPedalState(int pedal);
	
	pb_timestamp framesInCurrentKeyState(int key);		// How long have we been in the current state?
	pb_timestamp framesInCurrentPedalState(int pedal);
	pb_timestamp timestampOfKeyStateChange(int key, int state);
	pb_timestamp timestampOfPedalStateChange(int pedal, int state);
	pb_timestamp secondsToFrames(double seconds) {	// Useful conversion methods
		return (pb_timestamp)(seconds * (double)PIANO_BAR_SAMPLE_RATE);
	}
	double framesToSeconds(pb_timestamp frames) {
		return ((double)frames / (double)PIANO_BAR_SAMPLE_RATE);
	}
	pb_timestamp timestampForKeyOffset(int key, int offset) {	// Return the timestamp of a previous sample
		if(key < 0 || key > 87)
			return 0;
		int loc = (keyHistoryPosition_[key] - offset + keyHistoryLength_[key]) % keyHistoryLength_[key];
		return keyHistoryTimestamps_[key][loc];
	}
	pb_timestamp timestampForPedalOffset(int pedal, int offset) {	// Return the timestamp of a previous sample
		if(pedal < 0 || pedal > 2)
			return 0;
		int loc = (pedalHistoryPosition_[pedal] - offset + pedalHistoryLength_[pedal]) % pedalHistoryLength_[pedal];
		return pedalHistoryTimestamps_[pedal][loc];
	}	
	int timestampToKeyOffset(int key, pb_timestamp timestamp) {	// How many samples ago was this timestamp?
		if(key < 0 || key > 87)
			return 0;
		int loc = keyHistoryPosition_[key], count = 0;
		while(keyHistoryTimestamps_[key][loc] > timestamp)			// TODO: pre-estimate...
		{
			loc = (loc - 1 + keyHistoryLength_[key]) % keyHistoryLength_[key];
			count++;
			
			if(count >= keyHistoryLength_[key])	// Hopefully nobody asks for something this big!
				return 0;
		}
		return count;
	}
	int timestampToPedalOffset(int pedal, pb_timestamp timestamp) {	// How many samples ago was this timestamp?
		if(pedal < 0 || pedal > 2)
			return 0;
		int loc = pedalHistoryPosition_[pedal], count = 0;
		while(pedalHistoryTimestamps_[pedal][loc] > timestamp)			// TODO: pre-estimate...
		{
			loc = (loc - 1 + pedalHistoryLength_[pedal]) % pedalHistoryLength_[pedal];
			count++;
			
			if(count >= pedalHistoryLength_[pedal])	// Hopefully nobody asks for something this big!
				return 0;
		}
		return count;
	}	

	bool debugPrintGate(int key, pb_timestamp delay);	// Method that tells us whether to dump something to the console
	
	void changeKeyStateWithTimestamp(int key, int newState, pb_timestamp timestamp);
	void changeKeyState(int key, int state) { changeKeyStateWithTimestamp(key, state, currentTimeStamp_); }
	void changePedalStateWithTimestamp(int pedal, int newState, pb_timestamp timestamp);
	void changePedalState(int pedal, int state) { changePedalStateWithTimestamp(pedal, state, currentTimeStamp_); }
	
	bool cleanUpCalibrationValues();
	void resetKeyStates();
	void resetPedalStates();
	void resetKeyInfo(int key);
	void loadProcessingParameters();
	
	int whiteKeyAbove(int key) {		// Return the index of the next white key above this key
		if(key == 87) return 0;
		if(kPianoBarKeyColor[key+1] == K_B)
			return key+2;
		return key+1;
	}
	int whiteKeyBelow(int key) {		// Return the index of the next white key below this one
		if(key == 0) return 87;	// "circular" keyboard! (have to return SOMETHING for the bottom key)
		if(kPianoBarKeyColor[key-1] == K_B)
			return key-2;
		return key-1;
	}
	
	// Handy functions to pack and unpack raw values for saving in file
	unsigned short packRawValue(short value, short type) {
		return (value | type << 12);
	}
	void unpackRawValue(unsigned short packedValue, short *value, short *type) {
		*value = packedValue & 0x0FFF;			// Value stored as signed 12-bit integer
		if(*value > 2047)
			*value -= 4096;
		*type = (packedValue & 0xF000) >> 12;	// Type held in high 4 bits
	}
	
	void sendMidiNoteOn(int key);		// Send a MIDI note on message over OSC for the given key
	void sendMidiNoteOff(int key);		// Send a MIDI note off message over OSC for the given key
	
	int recordOpenNewKeyFile(int key, pb_timestamp timestamp);	// Open a new file for writing raw key position data
	int recordOpenNewPedalFile(int pedal, pb_timestamp timestamp); // Same thing for pedal data
	bool recordSaveCalibration(pb_timestamp timestamp);		// Save calibration data to a new file
	bool recordOpenNewEventsFile();		// Open a new file for writing events (key activity, features)
	bool launchRecordThread();			// Starts the thread that handles file I/O
	void stopRecordThread();			// Stops the thread controlling file I/O
	void stopAllRecordings();			// Stop all recordings in progress
	bool recordLoopBeginKeyRecording(int key);	// Set up a new file for event-driven recording
	bool recordLoopBeginPedalRecording(int pedal);
	
	bool allocateKeyHistoryBuffers(int blackLength, int whiteLength, int pedalLength);	// Allocate key history buffers
	bool allocateGlobalHistoryBuffer(int length);	// Allocate a combined buffer for all keys
	bool generateWarpTables();						// Generate non-linear mappings from sensor value to position
	int warpedValue(int key, int rawValue);			// Map a single sample
	
	// ****** State Variables *********
	
	bool isInitialized_;			// Whether the audio device has been initialized
	bool isRunning_;				// Whether the device is currently capturing data
	PaStream *inputStream_;			// Reference to the audio stream
	pthread_mutex_t audioMutex_;	// Mutex to synchronize between audio callbacks and user function calls
	pthread_mutex_t calibrationMutex_;			// Mutex to synchronize calibration calls with file I/O
	pthread_mutex_t eventLogMutex_;				// Mutex to ensure only one thread writes to the event log at once
	volatile pb_timestamp currentTimeStamp_;	// Current time in ADC frames (10.7kHz), relative to device start
	
	int dataErrorCount_;						// How many error messages we've printed (used as a gating device)
	pb_timestamp dataErrorLastTimestamp_;		// Timestamp of the last printed data error message
	
	bool sendFeatureMessages_;					// If true, send features over OSC
	bool sendMidiMessages_;						// If true, send MIDI messages over OSC
	int midiMessageChannel_;					// Channel (0-15) on which to send MIDI messages
	bool sendKeyPressData_;						// Whether to send a blob of data with the details of each keypress
	
	// ****** Calibration ******
	
	int calibrationStatus_;			// Whether the Piano Bar has been calibrated or not
	
	// For right now, we store buffers that are larger than technically necessary: we only use 1 cycle point
	// per white key, 3 per black key, but conceivably we might want to use up to 4 cycle points for keys that support it.
	
	short calibrationQuiescent_[88][4];			// Resting state of each key
	short calibrationFullPress_[88][4];			// "Pressed" state for each key
	
	short *calibrationHistory_[88][4];			// Special buffer allocated only during calibration time which keeps
												// each sample within a sequence separate
	int calibrationHistoryPosition_[88][4];		// Position within each buffer
	int calibrationHistoryLength_;				// Same length for each buffer
	vector<int> *keysToCalibrate_;				// Which keys we're currently calibrating (NULL for all)
	vector<int> *pedalsToCalibrate_;			// Which pedals we're currently calibrating (NULL for all)
	volatile unsigned int calibrationSamples_;	// How many raw samples we've taken
	
	int keyNoiseFloor_[88];						// Calculated post-calibration; reflects the noise on a resting key
	bool needsNoiseFloorCalibration_;			// This flag is set when calibration is requested before the device is running
	string lastCalibrationFile_;				// Filename of the last file we saved or loaded for calibration
	
	map<int, int> *keyPositionModes_[88][4];	// Most common position of each key is taken to be its idle value
	map<int, int> *pedalPositionModes_[3];
	
	int *keyWarpWhite_, *keyWarpBlack_;			// Correction lookup table for the non-linearity of the sensor: allocated to 16k (-8192 to 8191)
	
	// ******* Key Position History *******
	
	int *keyHistory_[88];						// History of each key position, allocated dynamically
	unsigned short *keyHistoryRaw_[88];			// History of each key position in its original form
	pb_timestamp *keyHistoryTimestamps_[88];	// Specific time stamps for each point in the key history
	int *keyVelocityHistory_[88];				// Velocity of each key
	int keyHistoryLength_[88];					// History length of each specific key
	volatile int keyHistoryPosition_[88];		// Where we are within each buffer
	int keyHistoryLengthWhite_, keyHistoryLengthBlack_;	// General history length for white and black keys
	
	int *keyStartValues_[88];					// Values of last key start events
	int keyStartValuesPosition_[88];
	int keyStartValuesLength_[88];				
	
	pb_timestamp debugLastPrintTimestamp_[88];	// For debugging purposes, the last time a message was printed
	multimap<pb_timestamp, key_action> keyScheduledActions_[88];	// Functions scheduled to run in the future
	
	// ******* Key State History ********
	
	typedef struct {
		int state;								// see enum { kKeyState... } above
		pb_timestamp timestamp;			// Timestamp measured in ADC samples [10.7kHz]
	} stateHistory;
	
	typedef struct {
		int value;
		pb_timestamp timestamp;
		int type;
	} keyPointHistory;
	
	deque<stateHistory> keyStateHistory_[88];	// History of each key state
	
	// ******** Higher-Level Key Parameters *********
	
	typedef struct {
		int runningSum;						// sum of last N points (i.e. mean * N)
		int runningSumMaxLength;			// the value of N above
		int runningSumCurrentLength;		// How many values are actually part of the sum right now (transient condition)
		int startValuesSum;					// sum of the last N start values (to calculate returning quiescent position)
		int startValuesSumMaxLength;
		int startValuesSumCurrentLength;
		
		int maxVariation;					// The maximum deviation from mean of the last group of samples
		int flatCounter;					// how many successive samples have been "flat" (minimal change)
		int currentStartValue;				// values and positions of several key points for active keys
		int currentStartPosition;
		int currentMinValue;				
		int currentMinPosition;
		int currentMaxValue;
		int currentMaxPosition;
		int lastKeyPointValue;				// the value of the last important point {start, max, min}
		
		deque<keyPointHistory> recentKeyPoints; // the minima and maxima since the key started
		bool sentPercussiveMidiOn;			// HACK: whether we've sent the percussive MIDI event
		
		int pressValue;						// the value at the maximum corresponding to the end of the key press motion
		int pressPosition;					// the location in the buffer of this event (note: not the timestamp)
		int releaseValue;					// the value the key held right before release
		int releasePosition;				// the location in the buffer of the release corner
	} keyParameters;
	
	keyParameters keyInfo_[88];	
	
	int parameterFlatnessTolerance_[88];	// How much deviation is allowed for something to be deemed "flat"
	int parameterFlatnessIdleLength_;		// How long it should be flat before being declared "idle"
	int parameterMaxMinValueSpacing_;		// How far apart in value (not in time) successive maxima and minima must be
	int parameterFirstMaxHeight_;			// How high the first maximum has to be to be considered above the noise floor
	int parameterIdlePositionOffset_;		// How far away in position the key can be from the last start value and still be
											//  idle when it goes flat for sufficient time.
	int parameterGuaranteedIdlePosition_;	// A flat key below this point is always idle, regardless of previous start values
	int parameterKeyPressValue_;			// How far down the key should go before it's deemed "pressed"
	int parameterKeyReleaseStartValue_;		// Once the key is pressed, how far does it go before release begins?
	int parameterKeyReleaseFinishValue_;	// ...and where does the main part of release end?
	int parameterKeyRestrikeMinValue_;		// Minimum value for a new key press to be detected rapidly following a release
	int parameterDefaultSumLength_;			// How many points, by default, to average when computing flatness
	int parameterDefaultStartSumLength_;	// How many start values, by default, to average when computing idle position
	int parameterKeyStartMinimumVelocity_;	// Velocity below which the key start is judged to have not occurred-- for finding press beginnings
	
	// ******* Pedal Calibration ********
	
	short pedalCalibrationQuiescent_[3];
	short pedalCalibrationFullPress_[3];			// Resting and pressed values of each pedal
	short pedalMinPressRaw_[3];						// Minimum press for each pedal to be considered "down"
	int pedalMinPressRising_[3];					// The calibrated value of the above, with hysteresis
	int pedalMinPressFalling_[3];
	
	// ******* Pedal State *********
	
	int *pedalHistory_[3];							// History of each (calibrated) pedal position
	short *pedalHistoryRaw_[3];						// The raw values of each pedal
	pb_timestamp *pedalHistoryTimestamps_[3];		// Timestamps for each collected value
	volatile int pedalHistoryPosition_[3];			// Position within the circular buffer
	int pedalHistoryLength_[3];						// Length of the above buffers
	
	deque<stateHistory> pedalStateHistory_[3];		// History of pedal states
	
	// ******* Data Logging *********
	
	bool recordRawStream_;							// Whether we should record the raw unprocessed Piano Bar data
	int recordPreTimeWhite_, recordPreTimeBlack_;	// How many samples before an event to begin capturing
	int recordPostTimeWhite_, recordPostTimeBlack_;	// How many samples after an event to continue capturing
	pb_timestamp recordSpacingWhite_, recordSpacingBlack_;	// Time to require between two events before a new file is created
	
	int recordGlobalHistoryFile_;					// File descriptor to write combined data
	void *recordGlobalHistory_;						// Circular buffer holding the combined key history
	int recordGlobalHistoryLength_;
	int recordGlobalHistoryCallbackPosition_;		// Where in the buffer the callback should store current values
	int recordGlobalHistoryIOPosition_;				// Where in the buffer the I/O loop has last written to disk
	bool recordGlobalHistoryOverflow_;				// Set if CallbackPosition moves past IOPosition, indicating buffer overflow
	
	int recordNumberOfActiveKeys_;					// How many keys are currently recording (in the OnActivity mode)
	int recordKeyStatus_[88];						// What state each key is in with respect to recording
	int recordPedalStatus_[3];						// What state each pedal is in with respect to recording
	int recordKeyFilePointer_[88];					// Where the logging information of this key should be saved to
	int recordPedalFilePointer_[3];					// Analogous file points for pedal data
	int recordKeyHistoryIOPosition_[88];			// The pointer to where we last wrote the history to a file
	int recordPedalHistoryIOPosition_[3];			// Similiarly, pointer of where we wrote last pedal history
	bool recordKeyHistoryOverflow_[88];				// Set if the audio callback position moves past the IO position
	bool recordPedalHistoryOverflow_[3];
	int recordKeyRemainingSamples_[88];				// Position of when we should stop recording, in PostEvent state
	int recordPedalRemainingSamples_[3];
	pb_timestamp recordKeyCloseFileTimestamp_[88];	// Beyond what timestamp the file should be closed
	pb_timestamp recordPedalCloseFileTimestamp_[3];	
	set<int> openKeyPedalFilePointers_;				// List of each currently open file pointer, for cleanup purposes
	ofstream recordEventsFile_;						// Output stream to log events
	bool recordEvents_;								// Whether to record events to the above stream
	bool recordCalibrationNeedsSaving_;				// Whether we should save the current calibration data
	pb_timestamp recordCalibrationCompletionTime_;	// When the calibration finished

	pthread_t recordIOThread_;						// Thread handling file I/O
	volatile bool recordIOThreadFinishFlag_;		// When this goes true, thread should flush all data and exit
	
	const char *recordDirectory_;					// The base directory in which recordings take place
};

#endif