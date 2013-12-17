/*
 *  pianobar.cpp
 *  kblisten
 *
 *  Created by Andrew McPherson on 2/10/10.
 *  Copyright 2010 __MyCompanyName__. All rights reserved.
 *
 */

#include "pianobar.h"
//#include "osc.h"

#define MIDI_HACK

const char *kNoteNames[88] = { "A0", "A#0", "B0",
	"C1", "C#1", "D1", "D#1", "E1", "F1", "F#1", "G1", "G#1", "A1", "A#1", "B1",
	"C2", "C#2", "D2", "D#2", "E2", "F2", "F#2", "G2", "G#2", "A2", "A#2", "B2",
	"C3", "C#3", "D3", "D#3", "E3", "F3", "F#3", "G3", "G#3", "A3", "A#3", "B3",
	"C4", "C#4", "D4", "D#4", "E4", "F4", "F#4", "G4", "G#4", "A4", "A#4", "B4",
	"C5", "C#5", "D5", "D#5", "E5", "F5", "F#5", "G5", "G#5", "A5", "A#5", "B5",
	"C6", "C#6", "D6", "D#6", "E6", "F6", "F#6", "G6", "G#6", "A6", "A#6", "B6",
	"C7", "C#7", "D7", "D#7", "E7", "F7", "F#7", "G7", "G#7", "A7", "A#7", "B7", "C8"};	

const char *kPedalNames[3] = { "Damper", "Sostenuto", "Una Corda" };

const char *kPedalShortNames[3] = { "damp", "sost", "uc" };

PianoBarController::PianoBarController()
{
	recordGlobalHistory_ = NULL;
	recordCalibrationNeedsSaving_ = false;
	recordEvents_ = false;
	recordRawStream_ = false;
	isInitialized_ = false;
	isRunning_ = false;
	inputStream_ = NULL;
	needsNoiseFloorCalibration_ = false;
	dataErrorCount_ = 0;
	dataErrorLastTimestamp_ = 0;
	sendFeatureMessages_ = false;
	sendMidiMessages_ = false;
	sendKeyPressData_ = false;
	lastCalibrationFile_ = "";	
	keyWarpWhite_ = keyWarpBlack_ = NULL;
	
	for(int i = 0; i < 88; i++)
	{
		keyHistory_[i] = NULL;
		keyHistoryRaw_[i] = NULL;
		keyHistoryTimestamps_[i] = NULL;
		keyVelocityHistory_[i] = NULL;
		keyStartValues_[i] = NULL;
		keyNoiseFloor_[i] = 0;
		
		for(int j = 0; j < 4; j++)
		{
			calibrationHistory_[i][j] = NULL;
			keyPositionModes_[i][j] = NULL;
		}
	}
	
	for(int i = 0; i < 3; i++)
	{
		pedalHistory_[i] = NULL;
		pedalHistoryRaw_[i] = NULL;
		pedalHistoryTimestamps_[i] = NULL;
		pedalPositionModes_[i] = NULL;
	}
	
	pthread_mutex_init(&audioMutex_, NULL);
	pthread_mutex_init(&calibrationMutex_, NULL);
	pthread_mutex_init(&eventLogMutex_, NULL);
}

#pragma mark Device Control

// Set all internal state variables to a beginning setting.  This is called by openDevice, but can
// also be called independently, for example to prepare to play back a file without a device attached
// Returns true on success.

bool PianoBarController::initialize()
{
	// Update the global variables
	
	isInitialized_ = true;
	isRunning_ = false;								// not until we call startDevice()
	calibrationStatus_ = kPianoBarNotCalibrated;
	currentTimeStamp_ = 0;
	
	// Allocate back-history buffers at a default size (might be changed by logging later)
	
	int defaultHistoryLengthBlack = (int)ceilf(DEFAULT_HISTORY_LENGTH * (float)PIANO_BAR_SAMPLE_RATE / 6.0);
	int defaultHistoryLengthWhite = (int)ceilf(DEFAULT_HISTORY_LENGTH * (float)PIANO_BAR_SAMPLE_RATE / 18.0);	
	int defaultPedalHistoryLength = (int)ceilf(DEFAULT_HISTORY_LENGTH * (float)PIANO_BAR_SAMPLE_RATE / 18.0);
	if(!allocateKeyHistoryBuffers(defaultHistoryLengthBlack, defaultHistoryLengthWhite, defaultPedalHistoryLength))
		return false;
	
	generateWarpTables();
	loadProcessingParameters();
	resetKeyStates();
	resetPedalStates();
	
	return true;
}

// Open and initialize the audio stream containing real-time (modified) Piano Bar input data.
// This will always come in the form of a 10.7kHz, 16-bit, 10-channel data stream.  
// historyInSeconds defines the amount of key history to save for each key.  This controls the
// size of the history buffers, which will be larger for the black keys (sampled at 1783Hz) than the
// white keys (594Hz).
// Return true on success.

// **** NOTE ****

// To work properly, this requires a modified portaudio library which converts float data to int16 by
// multiplying by 32768 (not 32767).  This is the only way (on Mac, at least) that the 16-bit input data
// comes out unchanged from the way it was transmitted via USB.

bool PianoBarController::openDevice(PaDeviceIndex inputDeviceNum, int bufferSize)
{
	const PaDeviceInfo *deviceInfo;
	PaStreamParameters inputParameters;
	PaError err;
	
	closeDevice();	// Close any existing stream
	
	deviceInfo = Pa_GetDeviceInfo(inputDeviceNum);
	if(deviceInfo == NULL)
	{
		cerr << "Unknown PianoBar device " << inputDeviceNum << ".  Try -l for list of devices.\n";
		return false;
	}		
	
	cout << "PianoBar Input Device:  " << deviceInfo->name << endl;	
	
	if(deviceInfo->maxInputChannels < 10)
	{
		cerr << "Invalid PianoBar device.  Device should support at least 10 channels.\n";
		return false;
	}
	
	inputParameters.device = inputDeviceNum;
	inputParameters.channelCount = 10;
	inputParameters.sampleFormat = paInt16;				// Use 16-bit int for input
	inputParameters.suggestedLatency = Pa_GetDeviceInfo(inputParameters.device)->defaultLowInputLatency;
	
	// If we're on Mac, make sure we set the hardware to the actual sample rate and don't rely on SRC
	if(deviceInfo->hostApi == paCoreAudio || deviceInfo->hostApi == 0)	// kludge for portaudio bug?
	{
		PaMacCoreStreamInfo macInfo;
		
		PaMacCore_SetupStreamInfo(&macInfo, paMacCoreChangeDeviceParameters | paMacCoreFailIfConversionRequired);
		inputParameters.hostApiSpecificStreamInfo = &macInfo;
	}
	else
		inputParameters.hostApiSpecificStreamInfo = NULL;
	
	// Check whether these parameters will work with the given sample rate
	err = Pa_IsFormatSupported(&inputParameters, NULL, PIANO_BAR_SAMPLE_RATE);
	if(err != paFormatIsSupported)
	{
		cerr << "Invalid PianoBar device.  Device should support 10.7kHz sample rate at 10 channels.\n";
		return false;
	}
	
	// Open the stream, passing our own (static) callback function
	// No dithering, no clipping: just the straight digital data!
	err = Pa_OpenStream(&inputStream_,
						&inputParameters,
						NULL,
						PIANO_BAR_SAMPLE_RATE,
						bufferSize,
						paDitherOff | paClipOff,
						staticAudioCallback,
						this);
	if(err != paNoError)
	{
		cerr << "Error opening PianoBar stream: " << Pa_GetErrorText(err) << endl;
		inputStream_ = NULL;
		return false;
	}
	
	if(!initialize())
		return false;
	
	return true;
}

// Tell the currently open stream to begin capturing data.  Returns true on success.

bool PianoBarController::startDevice()
{
	PaError err;
	
	if(!isInitialized_ || inputStream_ == NULL || isRunning_)
		return false;
	
	err = Pa_IsStreamActive(inputStream_);
	if(err > 0)
		return true;	// Stream is already running... nothing to do here ("success"?)
	else if(err < 0)
	{
		cerr << "Error in PianoBarController::startDevice(): " << Pa_GetErrorText(err) << endl;
		return false;
	}
	
	for(int i = 0; i < 88; i++)	// Start with a clean slate of key info any time the device has been stopped or started
		resetKeyInfo(i);
	
	if(needsNoiseFloorCalibration_)	// This will happen when the device has been calibrated, and we need to finish it
		calibrationStatus_ = kPianoBarCalibratedButNoUpdates;
	
	err = Pa_StartStream(inputStream_);
	
	if(err != paNoError)
	{
		cerr << "Error in PianoBarController::startDevice(): " << Pa_GetErrorText(err) << endl;
		return false;
	}
	
	isRunning_ = true;
	
	if(needsNoiseFloorCalibration_)
	{
		updateNoiseFloors();
		calibrationStatus_ = kPianoBarCalibrated;
		needsNoiseFloorCalibration_ = false;
	}
	
	return true;
}

// Stop a currently running stream.  Returns true on success.

bool PianoBarController::stopDevice()
{
	PaError err;
	
	if(!isInitialized_ || inputStream_ == NULL)
		return false;	
	
	err = Pa_IsStreamActive(inputStream_);
	if(err > 0)	// Stream is running
	{
		err = Pa_StopStream(inputStream_);
		isRunning_ = false;
		
		if(err != paNoError)
		{
			cerr << "Error in PianoBarController::stopDevice(): " << Pa_GetErrorText(err) << endl;
			return false;		
		}
	}	
	
	return true;
}

// Close any currently active audio stream.  This is assumed to succeed (nothing we can do otherwise).

void PianoBarController::closeDevice()
{
	PaError err;
	
	if(!isInitialized_ || inputStream_ == NULL)
		return;
	
	stopDevice();			// Stop the stream first
	stopAllRecordings();	// Now close all open files
	
	err = Pa_CloseStream(inputStream_);
	if(err != paNoError)
		cerr << "Warning: PianoBarController::closeDevice() failed: " << Pa_GetErrorText(err) << endl;
	
	if(keyWarpWhite_ != NULL)
	{
		free(keyWarpWhite_);
		keyWarpWhite_ = NULL;
	}
	if(keyWarpBlack_ != NULL)
	{
		free(keyWarpBlack_);
		keyWarpBlack_ = NULL;
	}
	
	inputStream_ = NULL;
	isInitialized_ = false;		// No longer initialized without a stream open
	for(int i = 0; i < 88; i++)
	{
		if(keyHistory_[i] != NULL)
		{
			free(keyHistory_[i]);	// Free up key history buffers
			free(keyHistoryRaw_[i]);
			free(keyHistoryTimestamps_[i]);
			free(keyVelocityHistory_[i]);
			free(keyStartValues_[i]);
			keyHistory_[i] = NULL;
			keyHistoryRaw_[i] = NULL;
			keyHistoryTimestamps_[i] = NULL;
			keyVelocityHistory_[i] = NULL;
			keyStartValues_[i] = NULL;
		}
		keyHistoryLength_[i] = 0;
		keyHistoryLengthWhite_ = keyHistoryLengthBlack_ = 0;
	}
	
	for(int i = 0; i < 3; i++)	// Free pedal history buffers
	{
		if(pedalHistory_[i] != NULL)
		{
			free(pedalHistory_[i]);
			free(pedalHistoryRaw_[i]);
			free(pedalHistoryTimestamps_[i]);
			pedalHistory_[i] = NULL;
			pedalHistoryRaw_[i] = NULL;
			pedalHistoryTimestamps_[i] = NULL;
		}
	}
}

#pragma mark File Input

// This method plays back the recording of raw Piano Bar data as if it were being received
// from the device.  It runs (hopefully) faster than real-time, and triggers all the same
// events and logging as the live data stream.
// Returns true on success

bool PianoBarController::playRawFile(const char *filename, pb_timestamp startTimestamp, vector<float> offsets)
{
	int fd, readCount = 0;
	short rawFrame[10];
	pb_timestamp rangeBegin = 0, rangeEnd = 0;
	
	if(isRunning_)
		return false;
	
	cout << "** Reading Piano Bar data from " << filename << " **\n";
	
	fd = open(filename, O_RDONLY);
	if(fd == -1)
		return false;
	
	for(int i = 0; i < 88; i++)	// Start with a clean slate of key info any time the device has been stopped or started
		resetKeyInfo(i);
	
	isRunning_ = true;
	
	if(offsets.size() >= 1)
	{
		rangeBegin = offsets[0]*PIANO_BAR_SAMPLE_RATE;
		lseek(fd, 20LL*rangeBegin, SEEK_SET);
		if(offsets.size () >= 2)
			rangeEnd = offsets[1]*PIANO_BAR_SAMPLE_RATE + startTimestamp;
	}
	
	currentTimeStamp_ = rangeBegin + startTimestamp;
	cout << "** Starting at timestamp " << currentTimeStamp_ << " **\n";	
	
	while(read(fd, rawFrame, 10*sizeof(short)) == 10*sizeof(short))
	{
		pthread_mutex_lock(&audioMutex_);
		processPianoBarFrame(rawFrame, readCount++);
		if(calibrationStatus_ == kPianoBarInCalibration || calibrationStatus_ == kPianoBarAutoCalibration)
			calibrationSamples_++;		
		pthread_mutex_unlock(&audioMutex_);
		
		if(currentTimeStamp_ > rangeEnd && rangeEnd != 0)
		{
			cout << "** Finished at timestamp " << currentTimeStamp_ << " **\n";
			break;
		}
	}
	
	close(fd);
	
	isRunning_ = false;
	
	cout << "** Read complete **\n";
	
	return true;
}


#pragma mark Calibration

// This method begins the calibration process, where raw values of key and pedal positions are recorded
// to determine the resting (quiescent) and pressed state of each key/pedal.  If quiescentOnly is set, the
// quiescent values are updated and calibration ends immediately; otherwise, calibration remains open until
// stopCalibration() is called, updating both quiescent and pressed values in the process.

// keysToCalibrate and/or pedalsToCalibrate can be NULL.  If both are NULL, all keys and pedals are calibrated.
// If keysToCalibrate is non-NULL and pedalsToCalibrate is NULL, only the given keys are calibrated (and vice-versa).
// If neither is NULL, both are selectively calibrated.

bool PianoBarController::startCalibration(vector<int> &keysToCalibrate, vector<int> &pedalsToCalibrate, bool quiescentOnly)
{
	int i, j, k;
	
	if(!isRunning_)
		return false;
	
	// This forces a wait for a previous calibration file write to finish
	pthread_mutex_lock(&calibrationMutex_);
	recordCalibrationNeedsSaving_ = false;		// No need to save this anymore, even if it hasn't already been saved
	if(recordEvents_)
	{
		pthread_mutex_lock(&eventLogMutex_);
		recordEventsFile_ << currentTimeStamp_ << " -1 calibrate_start\n";
		pthread_mutex_unlock(&eventLogMutex_);
	}
	
	// This tells the audio capture callback to start saving data into the calibration buffers
	pthread_mutex_lock(&audioMutex_);
	
	calibrationStatus_ = kPianoBarInCalibration;	
	
	// Calibration gesture involves pressing each key lightly, then exerting heavy pressure.  For black keys,
	// light level (i.e. input value) will go up when this happens; for white keys, it will go down.  We assume
	// the start of calibration represents the quiescent values (average over several data points).  Then watch
	// for any big changes (again using heavy averaging).
	
	// Completion of key press will be marked by velocity going to zero and staying there.  Capture the first N
	// data points following this event as "light" press, then the max/min value as "heavy" press.
	
	calibrationHistoryLength_ = 64;			// Picked arbitrarily...
	calibrationSamples_ = 0;
	
	if(keysToCalibrate.size() > 0)
		keysToCalibrate_ = new vector<int>(keysToCalibrate);
	else
		keysToCalibrate_ = NULL;
	if(pedalsToCalibrate.size() > 0)
		pedalsToCalibrate_ = new vector<int>(pedalsToCalibrate);
	else
		pedalsToCalibrate_ = NULL;	
	
	if(keysToCalibrate_ != NULL || pedalsToCalibrate_ == NULL)	// Perform this unless we're calibrating pedals only
	{
		for(i = 0; i < 88; i++)
		{
			for(j = 0; j < 4; j++)
			{
				calibrationHistory_[i][j] = new short[calibrationHistoryLength_];
				
				for(k = 0; k < calibrationHistoryLength_; k++)
					calibrationHistory_[i][j][k] = UNCALIBRATED;
				
				if(keysToCalibrate_ != NULL)
				{
					bool foundMatch = false;
					
					for(k = 0; k < keysToCalibrate_->size(); k++)
					{
						if((*keysToCalibrate_)[k] == i + 21)
						{
							foundMatch = true;
							break;
						}
					}
					
					if(!foundMatch)
						continue;
				}
				
				calibrationQuiescent_[i][j] = UNCALIBRATED;
				
				if(!quiescentOnly)
					calibrationFullPress_[i][j] = UNCALIBRATED;
			}
		}
	}
	
	if(keysToCalibrate_ == NULL || pedalsToCalibrate_ != NULL)	// Do this unless this is a keys-only calibration
	{
		for(i = 0; i < 3; i++)
		{
			if(pedalsToCalibrate_ != NULL)
			{
				bool foundMatch = false;
				
				for(k = 0; k < pedalsToCalibrate_->size(); k++)
				{
					if((*pedalsToCalibrate_)[k] == i)
					{
						foundMatch = true;
						break;
					}
				}
				
				if(!foundMatch)
					continue;
			}			
			
			pedalCalibrationQuiescent_[i] = UNCALIBRATED;
			if(!quiescentOnly)
			{
				pedalCalibrationFullPress_[i] = UNCALIBRATED;
				pedalMinPressRaw_[i] = UNCALIBRATED;
			}
		}
	}
	
	pthread_mutex_unlock(&audioMutex_);
	
	// Let run for a short time to fill the history buffers, then collect quiescent values for each key
	while(calibrationSamples_ < 18*64)	// 18 samples per cycle, 64 cycles fills all buffers
		usleep(100);
	
	// Now (with mutex locked to prevent data changing) set the quiescent values for each key to a running
	// average of the last several samples
	pthread_mutex_lock(&audioMutex_);
	
	if(keysToCalibrate_ != NULL || pedalsToCalibrate_ == NULL)	// Perform this unless we're calibrating pedals only
	{	
		for(i = 0; i < 88; i++)
		{
			if(keysToCalibrate_ != NULL)
			{
				bool foundMatch = false;
				
				for(k = 0; k < keysToCalibrate_->size(); k++)
				{
					if((*keysToCalibrate_)[k] == i + 21)
					{
						foundMatch = true;
						break;
					}
				}
				
				if(!foundMatch)
					continue;
			}
			
			for(j = 0; j < 4; j++)
			{
				// At this point, each history buffer will either have filled up with real data, or be entirely empty
				// (for unused cycle values, e.g. cycle 4 on many keys).  In the latter case, we should get an averaged
				// value of UNCALIBRATED which is what we want.
				
				calibrationQuiescent_[i][j] = (short)calibrationRunningAverage(i, j, 0, 32);
			}
		}
	}
	
	if(keysToCalibrate_ == NULL || pedalsToCalibrate_ != NULL)	// Do this unless this is a keys-only calibration
	{
		for(i = 0; i < 3; i++)
		{
			if(pedalsToCalibrate_ != NULL)
			{
				bool foundMatch = false;
				
				for(k = 0; k < pedalsToCalibrate_->size(); k++)
				{
					if((*pedalsToCalibrate_)[k] == i)
					{
						foundMatch = true;
						break;
					}
				}
				
				if(!foundMatch)
					continue;
			}			
			
			pedalCalibrationQuiescent_[i] = (short)pedalCalibrationRunningAverage(i, 0, 32);
		}
	}
	
	if(quiescentOnly)
	{
		calibrationStatus_ = kPianoBarCalibratedButNoUpdates;	// Don't pick up the other values in this case
		
		if(keysToCalibrate_ != NULL)
			delete keysToCalibrate_;
		
		// When finished, free the calibration buffers-- we use a combined buffering system for regular usage
		
		for(i = 0; i < 88; i++)
		{
			for(j = 0; j < 4; j++)
			{
				delete calibrationHistory_[i][j];
			}
			resetKeyInfo(i);
		}	
		
		cleanUpCalibrationValues();
		recordCalibrationNeedsSaving_ = true;
		recordCalibrationCompletionTime_ = currentTimeStamp_;
		if(recordEvents_)
		{
			pthread_mutex_lock(&eventLogMutex_);
			recordEventsFile_ << currentTimeStamp_ << " -1 calibrate_idle\n";
			pthread_mutex_unlock(&eventLogMutex_);
		}
	}
	
	pthread_mutex_unlock(&audioMutex_);
	
	pthread_mutex_unlock(&calibrationMutex_);
	
	if(quiescentOnly)
	{
		updateNoiseFloors();						// Calculate the quiescent noise on each key
		calibrationStatus_ = kPianoBarCalibrated;	// Now we're ready to go!
		return true;
	}
	
#ifdef DEBUG_CALIBRATION		// Print calibration values
	for(i = 0; i < 88; i++)
	{
		cout << "Note " << kNoteNames[i] << ": quiescent = (";
		if(calibrationQuiescent_[i][0] == UNCALIBRATED)
			cout << "---, ";
		else
			cout << calibrationQuiescent_[i][0] << ", ";
		if(calibrationQuiescent_[i][1] == UNCALIBRATED)
			cout << "---, ";
		else
			cout << calibrationQuiescent_[i][1] << ", ";
		if(calibrationQuiescent_[i][2] == UNCALIBRATED)
			cout << "---, ";
		else
			cout << calibrationQuiescent_[i][2] << ", ";
		if(calibrationQuiescent_[i][3] == UNCALIBRATED)
			cout << "---)\n";
		else
			cout << calibrationQuiescent_[i][3] << ")\n";		
	}
	
	for(i = 0; i < 3; i++)
	{
		cout << kPedalNames[i] << " pedal: quiescent = " << pedalCalibrationQuiescent_[i] << endl;
	}
#endif	
	
	return true;
}

void PianoBarController::stopCalibration()
{
	int i, j;
	bool calibrationHadErrors = false;
	
	pthread_mutex_lock(&audioMutex_);
	calibrationStatus_ = kPianoBarCalibratedButNoUpdates;
	
	if(keysToCalibrate_ != NULL)
		delete keysToCalibrate_;
	if(pedalsToCalibrate_ != NULL)
		delete pedalsToCalibrate_;	
	
	// When finished, free the calibration buffers-- we use a combined buffering system for regular usage
	
	for(i = 0; i < 88; i++)
	{
		for(j = 0; j < 4; j++)
		{
			delete calibrationHistory_[i][j];
			calibrationHistory_[i][j] = NULL;
		}
		resetKeyInfo(i);
	}	
	
	calibrationHadErrors = cleanUpCalibrationValues();

	if(recordEvents_)
	{
		pthread_mutex_lock(&eventLogMutex_);
		recordEventsFile_ << currentTimeStamp_ << " -1 calibrate_end\n";	
		pthread_mutex_unlock(&eventLogMutex_);
	}
	
	pthread_mutex_unlock(&audioMutex_);
	
	updateNoiseFloors();						// Calculate the quiescent noise on each key/pedal
	calibrationStatus_ = kPianoBarCalibrated;	// Now we're ready to go!
	
	recordCalibrationCompletionTime_ = currentTimeStamp_;	
	recordCalibrationNeedsSaving_ = true;
	
	if(calibrationHadErrors)
		cout << "\n*** One or more keys failed to calibrate.  Please run calibration again. ***\n";
}

// Reset the calibration values
void PianoBarController::clearCalibration()
{
	int i, j;
	
	pthread_mutex_lock(&calibrationMutex_);
	pthread_mutex_lock(&audioMutex_);

	calibrationStatus_ = kPianoBarNotCalibrated;
	
	for(i = 0; i < 88; i++)
	{
		for(j = 0; j < 4; j++)
		{
			calibrationQuiescent_[i][j] = calibrationFullPress_[i][j] = UNCALIBRATED;
		}
	}
	for(i = 0; i < 3; i++)
	{
		pedalCalibrationQuiescent_[i] = pedalCalibrationFullPress_[i] = pedalMinPressRaw_[i] = UNCALIBRATED;
	}

	pthread_mutex_unlock(&audioMutex_);	
	pthread_mutex_unlock(&calibrationMutex_);
}

// Take a snapshot of the current pedal position indicating the transition between down and up

void PianoBarController::calibratePedalMinPress(int pedal)
{
	short minPress;
	
	if(pedal < 0 || pedal > 2)
		return;
	
	pthread_mutex_lock(&audioMutex_);
	
	minPress = (short)pedalCalibrationRunningAverage(pedal, 0, 30);
	pedalMinPressRaw_[pedal] = minPress;
	
	if(calibrationStatus_ == kPianoBarCalibrated)
	{
		int calibratedValueInt = 4096*(int)(minPress - pedalCalibrationQuiescent_[pedal]);
		int calibratedValueDenominator = (pedalCalibrationFullPress_[pedal] - pedalCalibrationQuiescent_[pedal]);
		if(calibratedValueDenominator == 0)	// Prevent divide-by-0 errors
			calibratedValueDenominator = 1;
		calibratedValueInt /= calibratedValueDenominator;
		
		cout << kPedalNames[pedal] << " pedal press point calibrated to " << calibratedValueInt << " (raw " << minPress << ")\n";
		
		pedalMinPressRising_[pedal] = (calibratedValueInt < (4096-128) ? calibratedValueInt + 128 : calibratedValueInt);
		pedalMinPressFalling_[pedal] = (calibratedValueInt > 128 ? calibratedValueInt - 128 : calibratedValueInt);
	}
	
	pthread_mutex_unlock(&audioMutex_);
}

// Set whether the pedals should be used or not.  This is provided so in case the pedal sensor is not attached.

void PianoBarController::setUseKeysAndPedals(bool enableKeys, bool enableDamper, bool enableSost, bool enableUnaCorda)
{
	for(int key = 0; key < 88; key++)
	{
		if(enableKeys)
			changeKeyState(key, currentKeyState(key) == kKeyStateDisabledByCalibrator ? kKeyStateDisabledByCalibrator : kKeyStateUnknown);
		else
			changeKeyState(key, kKeyStateDisabledByUser);
	}
	
	if(enableDamper)
		changePedalState(kPedalDamper, kPedalStateUnknown);
	else
		changePedalState(kPedalDamper, kPedalStateDisabledByUser);
	if(enableSost)
		changePedalState(kPedalSostenuto, kPedalStateUnknown);
	else
		changePedalState(kPedalSostenuto, kPedalStateDisabledByUser);
	if(enableUnaCorda)
		changePedalState(kPedalUnaCorda, kPedalStateUnknown);
	else
		changePedalState(kPedalUnaCorda, kPedalStateDisabledByUser);
}

// Private helper function called from both stopCalibration() and loadCalibrationFromFile() which
// sanity-checks the resulting data and flags any problems

bool PianoBarController::cleanUpCalibrationValues()
{
	bool calibrationHadErrors = false;
	int i;
	
#ifdef DEBUG_CALIBRATION		// Print calibration values
	for(i = 0; i < 88; i++)
	{
		cout << "Note " << kNoteNames[i] << ": quiescent = (";
		if(calibrationQuiescent_[i][0] == UNCALIBRATED)
			cout << "---, ";
		else
			cout << calibrationQuiescent_[i][0] << ", ";
		if(calibrationQuiescent_[i][1] == UNCALIBRATED)
			cout << "---, ";
		else
			cout << calibrationQuiescent_[i][1] << ", ";
		if(calibrationQuiescent_[i][2] == UNCALIBRATED)
			cout << "---, ";
		else
			cout << calibrationQuiescent_[i][2] << ", ";
		if(calibrationQuiescent_[i][3] == UNCALIBRATED)
			cout << "---) press = (";
		else
			cout << calibrationQuiescent_[i][3] << ") press = (";
		if(calibrationFullPress_[i][0] == UNCALIBRATED)
			cout << "---, ";
		else
			cout << calibrationFullPress_[i][0] << ", ";
		if(calibrationFullPress_[i][1] == UNCALIBRATED)
			cout << "---, ";
		else
			cout << calibrationFullPress_[i][1] << ", ";
		if(calibrationFullPress_[i][2] == UNCALIBRATED)
			cout << "---, ";
		else
			cout << calibrationFullPress_[i][2] << ", ";
		if(calibrationFullPress_[i][3] == UNCALIBRATED)
			cout << "---)\n";
		else
			cout << calibrationFullPress_[i][3] << ")\n";		
	}
	
	for(i = 0; i < 3; i++)
	{
		cout << kPedalNames[i] << " pedal: quiescent = " << pedalCalibrationQuiescent_[i];
		cout << ", press = " << pedalCalibrationFullPress_[i] << endl;
	}
#endif
	
	resetKeyStates();
	resetPedalStates();
	
	// Check that we got decent values for cycle 0 for white keys and cycles 0-2 for black keys
	// Also print warnings if we can't read key pressure (this may depend on the piano)
	
	for(i = 0; i < 88; i++)
	{
		if(kPianoBarKeyColor[i] == K_W)	// white keys
		{
			if(calibrationQuiescent_[i][0] == UNCALIBRATED ||
			   calibrationFullPress_[i][0] == UNCALIBRATED)
			{
				cout << "ERROR: Key " << kNoteNames[i] << " did not properly calibrate.\n";
				changeKeyState(i, kKeyStateDisabledByCalibrator);
				calibrationHadErrors = true;
			}
			else if(abs(calibrationQuiescent_[i][0] - calibrationFullPress_[i][0]) < 8)
			{
				cout << "ERROR: Key " << kNoteNames[i] << " did not properly calibrate (not enough range).\n";
				changeKeyState(i, kKeyStateDisabledByCalibrator);
				calibrationHadErrors = true;				
			}
		}
		else							// black keys
		{
			if(calibrationQuiescent_[i][0] == UNCALIBRATED ||
			   calibrationFullPress_[i][0] == UNCALIBRATED ||
			   calibrationQuiescent_[i][1] == UNCALIBRATED ||
			   calibrationFullPress_[i][1] == UNCALIBRATED ||
			   calibrationQuiescent_[i][2] == UNCALIBRATED ||
			   calibrationFullPress_[i][2] == UNCALIBRATED)
			{
				cout << "ERROR: Key " << kNoteNames[i] << " did not properly calibrate.\n";
				changeKeyState(i, kKeyStateDisabledByCalibrator);
				calibrationHadErrors = true;
			}
			else if(abs(calibrationQuiescent_[i][0] - calibrationFullPress_[i][0]) < 8 ||
					abs(calibrationQuiescent_[i][1] - calibrationFullPress_[i][1]) < 8 ||
					abs(calibrationQuiescent_[i][2] - calibrationFullPress_[i][2]) < 8)
			{
				cout << "ERROR: Key " << kNoteNames[i] << " did not properly calibrate (not enough range).\n";
				changeKeyState(i, kKeyStateDisabledByCalibrator);
				calibrationHadErrors = true;				
			}			
		}
		
	
	}
	
	// Also check that we got decent values for the pedals
	
	for(i = 0; i < 3; i++)
	{
		if(pedalCalibrationQuiescent_[i] == UNCALIBRATED ||
		   pedalCalibrationFullPress_[i] == UNCALIBRATED)
		{
			cout << "ERROR: Pedal " << kPedalNames[i] << " did not properly calibrate.\n";
			changePedalState(i, kPedalStateDisabledByCalibrator);
			calibrationHadErrors = true;
		}
		else if(abs(pedalCalibrationQuiescent_[i] - pedalCalibrationFullPress_[i]) < 8)
		{
			cout << "ERROR: Pedal " << kPedalNames[i] << " did not properly calibrate (not enough range).\n";
			changePedalState(i, kPedalStateDisabledByCalibrator);
			calibrationHadErrors = true;			
		}
		else if(pedalMinPressRaw_[i] != UNCALIBRATED)
		{
			int calibratedValueInt = 4096*(int)(pedalMinPressRaw_[i] - pedalCalibrationQuiescent_[i]);
			int calibratedValueDenominator = (pedalCalibrationFullPress_[i] - pedalCalibrationQuiescent_[i]);
			if(calibratedValueDenominator == 0)	// Prevent divide-by-0 errors
				calibratedValueDenominator = 1;
			calibratedValueInt /= calibratedValueDenominator;
			
			pedalMinPressRising_[i] = (calibratedValueInt < (4096-128) ? calibratedValueInt + 128 : calibratedValueInt);
			pedalMinPressFalling_[i] = (calibratedValueInt > 128 ? calibratedValueInt - 128 : calibratedValueInt);		
		}
		else
		{
			// Set value to halfway point
			pedalMinPressRising_[i] = 2048 + 128;
			pedalMinPressFalling_[i] = 2048 - 128;
		}
	}
	
	return calibrationHadErrors;
}


bool PianoBarController::saveCalibrationToFile(string& filename)
{
	int i, j;
	
	if(calibrationStatus_ != kPianoBarCalibrated && calibrationStatus_ != kPianoBarCalibratedButNoUpdates)
		return false;
	
	try
	{
		// Save the calibration to a text file
		// Format: "[note#] [sequence] [quiescent] [press] [noise floor]"
		// There's only one noise floor per key, but write it for each sequence to keep formatting consistent
		
		ofstream outputFile;
		
		outputFile.open(filename.c_str(), ios::out);
		for(i = 0; i < 88; i++)
		{
			for(j = 0; j < 4; j++)
			{
				outputFile << i << " " << j << " " << calibrationQuiescent_[i][j] << " ";
				outputFile << calibrationFullPress_[i][j] << " " << keyNoiseFloor_[i] << endl;
			}
		}
		
		// Output pedal calibration.  We don't need all the fields above, but fill them in
		// so we have a consistent table of data.  Distinguish pedals from keys by adding 128
		// to the pedal number.
		// Format: "[pedal#] [0] [quiescent] [press] [threshold]"
		
		for(i = 0; i < 3; i++)
		{
			outputFile << i + 128 << " 0 " << pedalCalibrationQuiescent_[i] << " ";
			outputFile << pedalCalibrationFullPress_[i] << " " << pedalMinPressRaw_[i] << endl;
		}
		outputFile.close();
		lastCalibrationFile_ = filename;		
	}
	catch(...)
	{
		return false;
	}
	
	return true;
}

// Load calibration values from a file (created with saveCalibrationToFile).  Returns true on success.

bool PianoBarController::loadCalibrationFromFile(string& filename)
{
	int i, j;
	
	pthread_mutex_lock(&audioMutex_);
	// To begin with, clear all existing values (then let the file data fill them in).	
	calibrationStatus_ = kPianoBarNotCalibrated;
	
	for(i = 0; i < 88; i++)
	{
		for(j = 0; j < 4; j++)
		{
			calibrationQuiescent_[i][j] = calibrationFullPress_[i][j] = UNCALIBRATED;
		}
	}
	
	// Open the file and read the new values
	try
	{
		// Format: "[note#] [sequence] [quiescent] [press]"
		// Parse the calibration text table
		ifstream inputFile;
		int key, seqOffset, quiescent, press, noiseFloor;
		
		inputFile.open(filename.c_str(), ios::in);
		if(inputFile.fail())		// Failed to open file...
		{
			pthread_mutex_unlock(&audioMutex_);
			return false;
		}
		while(!inputFile.eof())
		{
			inputFile >> key;
			inputFile >> seqOffset;
			inputFile >> quiescent;
			inputFile >> press;
			inputFile >> noiseFloor;
			
			if(key >= 128 && key < 128 + 3)	// Values starting at 128 indicate pedals
			{
				pedalCalibrationQuiescent_[key - 128] = quiescent;
				pedalCalibrationFullPress_[key - 128] = press;
				pedalMinPressRaw_[key - 128] = noiseFloor;
				continue;
			}
			
			if(key < 0 || key > 87)
			{
				cerr << "loadCalibrationFromFile(): Invalid key " << key << endl;
				inputFile.close();
				pthread_mutex_unlock(&audioMutex_);
				return false;
			}
			if(seqOffset < 0 || seqOffset > 3)
			{
				cerr << "loadCalibrationFromFile(): Invalid offset " << seqOffset << " for key " << key << endl;
				inputFile.close();
				pthread_mutex_unlock(&audioMutex_);
				return false;				
			}
			
			calibrationQuiescent_[key][seqOffset] = quiescent;
			calibrationFullPress_[key][seqOffset] = press;
			if(seqOffset == 0)
				keyNoiseFloor_[key] = noiseFloor;
		}
		inputFile.close();		
		lastCalibrationFile_ = filename;		
	}
	catch(...)
	{
		pthread_mutex_unlock(&audioMutex_);
		return false;
	}
	
	cleanUpCalibrationValues();	// Ignore whether or not this has errors for purposes of return value...
	
	calibrationStatus_ = kPianoBarCalibrated;
	recordCalibrationNeedsSaving_ = true;
	recordCalibrationCompletionTime_ = currentTimeStamp_;	
	for(i = 0; i < 88; i++)
		resetKeyInfo(i);
	
	updateFlatnessTolerance();	// Update the flatness tolerance of each key based on its noise floor
	
	pthread_mutex_unlock(&audioMutex_);
	
	return true;
}

// Allocate resources and configure to auto-calibrate.  This is usually done with a recording after it has been made.

void PianoBarController::startAutoCalibration()
{
	int i,j,k;
	
	for(i = 0; i < 88; i++)
	{
		for(j = 0; j < 4; j++)
			keyPositionModes_[i][j] = new map<int,int>;
	}
	for(i = 0; i < 3; i++)
		pedalPositionModes_[i] = new map<int,int>;
	
	pthread_mutex_lock(&calibrationMutex_);
	pthread_mutex_lock(&audioMutex_);
	
	calibrationHistoryLength_ = 64;			// Picked arbitrarily...
	calibrationSamples_ = 0;

	keysToCalibrate_ = NULL;
	pedalsToCalibrate_ = NULL;	

	for(i = 0; i < 88; i++)
	{
		for(j = 0; j < 4; j++)
		{
			calibrationHistory_[i][j] = new short[calibrationHistoryLength_];
			
			for(k = 0; k < calibrationHistoryLength_; k++)
				calibrationHistory_[i][j][k] = UNCALIBRATED;

			calibrationQuiescent_[i][j] = UNCALIBRATED;
			calibrationFullPress_[i][j] = UNCALIBRATED;
		}
	}

	
	if(keysToCalibrate_ == NULL || pedalsToCalibrate_ != NULL)	// Do this unless this is a keys-only calibration
	{
		for(i = 0; i < 3; i++)
		{
			pedalCalibrationQuiescent_[i] = UNCALIBRATED;
			pedalCalibrationFullPress_[i] = UNCALIBRATED;
			pedalMinPressRaw_[i] = UNCALIBRATED;
		}
	}	
	
	calibrationStatus_ = kPianoBarAutoCalibration;
	
	pthread_mutex_unlock(&audioMutex_);
	pthread_mutex_unlock(&calibrationMutex_);
}

// Finish an auto-calibration session and release any allocated resources.

void PianoBarController::stopAutoCalibration()
{
	int i,j,modeValue,modeCount;
	map<int,int>::iterator it;
	
	if(calibrationStatus_ != kPianoBarAutoCalibration)
		return;
	
	pthread_mutex_lock(&calibrationMutex_);
	pthread_mutex_lock(&audioMutex_);
	
	// Find the quiescent position by which value was the most frequent
	
	for(i = 0; i < 88; i++)
	{
		for(j = 0; j < 4; j++)
		{
			if(keyPositionModes_[i][j]->size() == 0)
				continue;
			modeCount = 0;
			modeValue = 0;
			
			for(it = keyPositionModes_[i][j]->begin(); it != keyPositionModes_[i][j]->end(); it++)
			{
				if((*it).second > modeCount)
				{
					modeCount = (*it).second;
					modeValue = (*it).first;
				}
			}
			
			calibrationQuiescent_[i][j] = modeValue;
		}
	}
	
	for(i = 0; i < 3; i++)
	{
		if(pedalPositionModes_[i]->size() == 0)
			continue;
		modeCount = 0;
		modeValue = 0;
		
		for(it = pedalPositionModes_[i]->begin(); it != pedalPositionModes_[i]->end(); it++)
		{
			if((*it).second > modeCount)
			{
				modeCount = (*it).second;
				modeValue = (*it).first;
			}
		}
		
		pedalCalibrationQuiescent_[i] = modeValue;	
	}
	
	cleanUpCalibrationValues();
	calibrationStatus_ = kPianoBarCalibrated;
	
	pthread_mutex_unlock(&audioMutex_);
	pthread_mutex_unlock(&calibrationMutex_);	
	
	for(i = 0; i < 88; i++)
	{
		for(j = 0; j < 4; j++)
		{
			if(keyPositionModes_[i][j] != NULL)
			{
				delete keyPositionModes_[i][j];
				delete calibrationHistory_[i][j];
				keyPositionModes_[i][j] = NULL;
			}
		}
	}
	for(i = 0; i < 3; i++)
	{
		if(pedalPositionModes_[i] != NULL)
		{
			delete pedalPositionModes_[i];
			pedalPositionModes_[i] = NULL;
		}
	}
}

#pragma mark Data Logging

// Set the relevant information for logging Piano Bar data to file

// For continuous recording, pedals work identically to keys (recorded continuously)
// For onActivity recording, pedals are recorded whenever any key is active
//   i.e. stop recording when all keys have stopped, close file when all key files have closed
// Store 3 files, 1 for each pedal, no need to control independently

bool PianoBarController::setRecordSettings(int recordStatus, vector<int> *recordKeys, const char *recordDirectory, 
										   bool recordPedals, bool recordRaw, bool recordEvents, float recordPreTime, 
										   float recordPostTime, float recordSpacing)
{
	int i, fp;
	float adjustedRecordSpacing = recordSpacing;
	int maxBufferLengthWhite_, maxBufferLengthBlack_;
	
	if(!isInitialized_)		// Need an open device first
		return false;
	
	pthread_mutex_lock(&audioMutex_);	// Lock the mutex so that none of this changes in the middle of a callback
	
	// Stop the current operation.  Even if our new state looks the same as the old state, we may want a new set
	// of files to distinguish this event.
	
	stopAllRecordings();				// This disables each key and closes all files

	// Make the base directory if needed
	recordDirectory_ = recordDirectory;
	recordRawStream_ = recordRaw;

	// Now check the status of the key-specific recordings and make any necessary preparations
	
	if(recordStatus == kRecordStatusOnActivity || recordStatus == kRecordStatusContinuous || recordEvents || recordRawStream_)
	{
		DIR *dir = opendir(recordDirectory);
		
		// First, make the directory to hold recordings if it doesn't already exist
		if(dir != NULL)	
			closedir(dir);
		else if(mkdir(recordDirectory, 
					  S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) != 0)	// Failed to make the directory
		{
			pthread_mutex_unlock(&audioMutex_);
			cerr << "ERROR: could not create directory '" << recordDirectory << "' to hold recordings\n";
			return false;
		}
		
		// Save the calibration settings to a new file (assuming the system has been calibrated)
		
		if(recordCalibrationNeedsSaving_)
		{
			if(!recordSaveCalibration(recordCalibrationCompletionTime_))
			{
				pthread_mutex_unlock(&audioMutex_);
				cerr << "ERROR: could not save calibration data\n";
				return false;
			}
			recordCalibrationNeedsSaving_ = false;
		}
				
		// Open a file to hold descriptions of events and features
		if(!recordOpenNewEventsFile())
		{
			pthread_mutex_unlock(&audioMutex_);
			cerr << "ERROR: unable to open event recording file\n";
			return false;
		}
	}
	
	// If we record the raw stream, open the file now to hold that data
	
	if(recordRawStream_)
	{
		// Open a new file for recording
		
		recordGlobalHistoryFile_ = recordOpenNewKeyFile(-1, currentTimeStamp_);
		if(recordGlobalHistoryFile_ < 0)
		{
			cerr << "ERROR: unable to open file to save global recording data -- raw recording disabled\n";
			if(recordEventsFile_.is_open())
			{
				pthread_mutex_lock(&eventLogMutex_);
				recordEventsFile_.close();
				pthread_mutex_unlock(&eventLogMutex_);
			}
			recordEvents_ = false;
			recordRawStream_ = false;
		}
		else if(!allocateGlobalHistoryBuffer(GLOBAL_HISTORY_LENGTH)) // Allocate buffer for combined key history
		{
			cerr << "ERROR: unable to allocate buffer for global recording data -- raw recording disabled\n";
			stopAllRecordings();	// We can do this because we haven't opened any other recordings yets
		}			
	}	
	
	if(recordStatus == kRecordStatusContinuous)
	{
		if(recordKeys == NULL)
		{
			for(i = 0; i < 88; i++)
			{
				fp = recordOpenNewKeyFile(i, currentTimeStamp_);
				recordKeyStatus_[i] = kRecordStatusContinuous;	// All keys armed
				recordKeyFilePointer_[i] = fp;
				recordKeyHistoryOverflow_[i] = false;
				recordKeyHistoryIOPosition_[i] = keyHistoryPosition_[i];
			}
		}
		else
		{
			for(i = 0; i < recordKeys->size(); i++)
			{
				if((*recordKeys)[i] < 0 || (*recordKeys)[i] > 87)
					continue;
				fp = recordOpenNewKeyFile((*recordKeys)[i], currentTimeStamp_);
				recordKeyStatus_[(*recordKeys)[i]] = kRecordStatusContinuous;
				recordKeyFilePointer_[(*recordKeys)[i]] = fp;
				recordKeyHistoryOverflow_[(*recordKeys)[i]] = false;
				recordKeyHistoryIOPosition_[(*recordKeys)[i]] = keyHistoryPosition_[(*recordKeys)[i]];
			}
		}	
		
		if(recordPedals)
		{
			for(i = 0; i < 3; i++)
			{
				fp = recordOpenNewPedalFile(i, currentTimeStamp_);
				recordPedalStatus_[i] = kRecordStatusContinuous;
				recordPedalFilePointer_[i] = fp;
				recordPedalHistoryOverflow_[i] = false;
				recordPedalHistoryIOPosition_[i] = pedalHistoryPosition_[i];
			}
		}
	}
	else if(recordStatus == kRecordStatusOnActivity)
	{
		// Set up the internal buffers to be able to handle the demands of pre- and post-event recording time
		
		if(adjustedRecordSpacing < recordPostTime)		// Doesn't make any sense for these to overlap
			adjustedRecordSpacing = recordPostTime;
		
		// Calculate buffer lengths needed to capture all pre- and post-event data
		recordPreTimeWhite_ = (int)ceilf(recordPreTime * (float)PIANO_BAR_SAMPLE_RATE / 18.0);
		recordPreTimeBlack_ = (int)ceilf(recordPreTime * (float)PIANO_BAR_SAMPLE_RATE / 6.0);
		recordPostTimeWhite_ = (int)ceilf(recordPostTime * (float)PIANO_BAR_SAMPLE_RATE / 18.0);
		recordPostTimeBlack_ = (int)ceilf(recordPostTime * (float)PIANO_BAR_SAMPLE_RATE / 6.0);
		recordSpacingWhite_ = secondsToFrames(recordSpacing);
		recordSpacingBlack_ = secondsToFrames(recordSpacing);
		
		maxBufferLengthWhite_ = (recordSpacingWhite_ > recordPreTimeWhite_ ? recordSpacingWhite_ : recordPreTimeWhite_);
		maxBufferLengthBlack_ = (recordSpacingBlack_ > recordPreTimeBlack_ ? recordSpacingBlack_ : recordPreTimeBlack_);
		
		if(maxBufferLengthBlack_ > keyHistoryLengthBlack_ || maxBufferLengthWhite_ > keyHistoryLengthWhite_)
			allocateKeyHistoryBuffers(maxBufferLengthBlack_, maxBufferLengthWhite_, maxBufferLengthWhite_);	
		
		// Don't open any files for now, but arm the keys for recording if an event comes up
		if(recordKeys == NULL)
		{
			for(i = 0; i < 88; i++)
				recordKeyStatus_[i] = kRecordStatusOnActivity;	// All keys armed
		}
		else
		{
			for(i = 0; i < recordKeys->size(); i++)
			{
				if((*recordKeys)[i] < 0 || (*recordKeys)[i] > 87)
					continue;
				recordKeyStatus_[(*recordKeys)[i]] = kRecordStatusOnActivity;
			}
		}	
		recordNumberOfActiveKeys_ = 0;	
		
		if(recordPedals)
		{
			// Arm the pedals for recording if and when a key goes active
			
			for(i = 0; i < 3; i++)
				recordPedalStatus_[i] = kRecordStatusOnActivity;
		}
	}
	
	// Launch a thread to handle the file I/O if we'll be recording
	if(recordStatus == kRecordStatusOnActivity || recordStatus == kRecordStatusContinuous || recordRawStream_)
		launchRecordThread();	
		
	pthread_mutex_unlock(&audioMutex_);
	
	return true;
}

// This loop runs in its own thread, handling file I/O for key data and events

void* PianoBarController::recordLoop()
{
	int key, pedal, newIOPosition;
	int samplesToRecord;
	int recordStatus;
	
	while(!recordIOThreadFinishFlag_)
	{
		for(key = 0; key < 88; key++)
		{
			recordStatus = recordKeyStatus_[key];
			if(recordStatus == kRecordStatusShouldStartRecording)
				recordLoopBeginKeyRecording(key);

			// Save data to file if necessary
			if(recordStatus == kRecordStatusContinuous || recordStatus == kRecordStatusRecordingEvent ||
			   (recordStatus == kRecordStatusPostEvent && recordKeyRemainingSamples_[key] > 0))
			{
				// Preparations should happen atomically without interruption
				// by the callback, but the writes themselves can't be allowed to disrupt its execution
				
				pthread_mutex_lock(&audioMutex_);
				
				// In PostEvent state, we only record up to a specified point.  Otherwise, record all the currently
				// available data.
				
				if(recordKeyHistoryOverflow_[key])
				{
					if(recordStatus == kRecordStatusPostEvent)
						samplesToRecord = min(recordKeyRemainingSamples_[key], keyHistoryLength_[key]);
					else
						samplesToRecord = keyHistoryLength_[key];
					
					// The whole buffer needs to be saved.  We've already lost some data and might well end up losing some
					// more since the callback will keep running while we do this, but do our best to save what we can
					
					int bufLengthBytes = keyHistoryLength_[key] * sizeof(short);
					int startPosBytes = recordKeyHistoryIOPosition_[key] * sizeof(short);
					int totalBytes = samplesToRecord * sizeof(short);
					
					newIOPosition = (recordKeyHistoryIOPosition_[key] + samplesToRecord) % keyHistoryLength_[key];
					pthread_mutex_unlock(&audioMutex_);
					
					if(bufLengthBytes - startPosBytes < samplesToRecord)
					{
						// Record in two chunks
						
						write(recordKeyFilePointer_[key], ((uint8_t *)keyHistoryRaw_[key]) + startPosBytes, bufLengthBytes - startPosBytes);
						write(recordKeyFilePointer_[key], (void *)keyHistoryRaw_[key], totalBytes - (bufLengthBytes - startPosBytes));
					}
					else
					{
						// It all fits in one chunk
						
						write(recordKeyFilePointer_[key], ((uint8_t *)keyHistoryRaw_[key]) + startPosBytes, totalBytes);
					}
					
					recordKeyHistoryIOPosition_[key] = newIOPosition;
					recordKeyHistoryOverflow_[key] = false;
					
					cerr << "WARNING: buffer overflow saving key " << kNoteNames[key] << " (timestamp " << currentTimeStamp_ << ")\n";
				}
				else
				{
					if(recordStatus == kRecordStatusPostEvent)
						samplesToRecord = min(recordKeyRemainingSamples_[key], 
											  keyDistanceFromFeature(key, recordKeyHistoryIOPosition_[key], keyHistoryPosition_[key]));
					else
						samplesToRecord = keyDistanceFromFeature(key, recordKeyHistoryIOPosition_[key], keyHistoryPosition_[key]);
					
					if(recordKeyHistoryIOPosition_[key] < keyHistoryPosition_[key])
					{
						// Can save the buffer in one write command, since data lies contiguously with no wraparound
						
						int partialBytes1 = samplesToRecord*sizeof(short);
						newIOPosition = recordKeyHistoryIOPosition_[key] + samplesToRecord;
						
						pthread_mutex_unlock(&audioMutex_);
						
						write(recordKeyFilePointer_[key], 
							  ((uint8_t *)keyHistoryRaw_[key]) + recordKeyHistoryIOPosition_[key]*sizeof(short),
							  partialBytes1);
						
						recordKeyHistoryIOPosition_[key] = newIOPosition;
					}
					else if(recordKeyHistoryIOPosition_[key] > keyHistoryPosition_[key])
					{
						// Need two write commands, one from IOPosition to the end of the buffer, another from the beginning
						// of the buffer to the callback position
						
						int bufLengthBytes = keyHistoryLength_[key] * sizeof(short);
						int startPosBytes = recordKeyHistoryIOPosition_[key] * sizeof(short);
						int totalBytes = samplesToRecord * sizeof(short);
						
						newIOPosition = (recordKeyHistoryIOPosition_[key] + samplesToRecord) % keyHistoryLength_[key];
						pthread_mutex_unlock(&audioMutex_);
						
						if(bufLengthBytes - startPosBytes < samplesToRecord)
						{
							// Record in two chunks
							
							write(recordKeyFilePointer_[key], ((uint8_t *)keyHistoryRaw_[key]) + startPosBytes, bufLengthBytes - startPosBytes);
							write(recordKeyFilePointer_[key], (void *)keyHistoryRaw_[key], totalBytes - (bufLengthBytes - startPosBytes));
						}
						else
						{
							// It all fits in one chunk
							
							write(recordKeyFilePointer_[key], ((uint8_t *)keyHistoryRaw_[key]) + startPosBytes, totalBytes);
						}
						
						recordKeyHistoryIOPosition_[key] = newIOPosition;
					}
					else
						pthread_mutex_unlock(&audioMutex_);	// Nothing to do here
				}
				
				if(recordStatus == kRecordStatusPostEvent)
				{
					recordKeyRemainingSamples_[key] -= samplesToRecord;
				}
			}
			else if(recordStatus == kRecordStatusPostEvent && recordKeyRemainingSamples_[key] <= 0)
			{
				// We've recorded all the samples we need to into this file.  All that remains is to see
				// whether enough time has elapsed that we can close the file.  This depends on the key
				// record spacing: it's possible that if an event comes up post-recording but within the spacing
				// window, we'll want to continue recording where we left off.  That's easy enough since
				// recordKeyHistoryIOPosition is waiting at the end of the last samples recorded.
				
				if(currentTimeStamp_ > recordKeyCloseFileTimestamp_[key])
				{
					close(recordKeyFilePointer_[key]);
					openKeyPedalFilePointers_.erase(recordKeyFilePointer_[key]);
					recordKeyFilePointer_[key] = -1;
					
					recordKeyStatus_[key] = kRecordStatusOnActivity;	// Go back to waiting state
				}
			}
		}
		
		for(pedal = 0; pedal < 3; pedal++)
		{
			recordStatus = recordPedalStatus_[pedal];
			if(recordStatus == kRecordStatusShouldStartRecording)
				recordLoopBeginPedalRecording(pedal);
			
			// Save data to file if necessary
			if(recordStatus == kRecordStatusContinuous || recordStatus == kRecordStatusRecordingEvent ||
			   (recordStatus == kRecordStatusPostEvent && recordPedalRemainingSamples_[pedal] > 0))
			{
				// Preparations should happen atomically without interruption
				// by the callback, but the writes themselves can't be allowed to disrupt its execution
				
				pthread_mutex_lock(&audioMutex_);
				
				// In PostEvent state, we only record up to a specified point.  Otherwise, record all the currently
				// available data.
				
				if(recordPedalHistoryOverflow_[pedal])
				{
					if(recordStatus == kRecordStatusPostEvent)
						samplesToRecord = min(recordPedalRemainingSamples_[pedal], pedalHistoryLength_[pedal]);
					else
						samplesToRecord = pedalHistoryLength_[pedal];
					
					// The whole buffer needs to be saved.  We've already lost some data and might well end up losing some
					// more since the callback will keep running while we do this, but do our best to save what we can
					
					int bufLengthBytes = pedalHistoryLength_[pedal] * sizeof(short);
					int startPosBytes = recordPedalHistoryIOPosition_[pedal] * sizeof(short);
					int totalBytes = samplesToRecord * sizeof(short);
					
					newIOPosition = (recordPedalHistoryIOPosition_[pedal] + samplesToRecord) % pedalHistoryLength_[pedal];
					pthread_mutex_unlock(&audioMutex_);
					
					if(bufLengthBytes - startPosBytes < samplesToRecord)
					{
						// Record in two chunks
						
						write(recordPedalFilePointer_[pedal], ((uint8_t *)pedalHistoryRaw_[pedal]) + startPosBytes, bufLengthBytes - startPosBytes);
						write(recordPedalFilePointer_[pedal], (void *)pedalHistoryRaw_[pedal], totalBytes - (bufLengthBytes - startPosBytes));
					}
					else
					{
						// It all fits in one chunk
						
						write(recordPedalFilePointer_[pedal], ((uint8_t *)pedalHistoryRaw_[pedal]) + startPosBytes, totalBytes);
					}
					
					recordPedalHistoryIOPosition_[pedal] = newIOPosition;
					recordPedalHistoryOverflow_[pedal] = false;
					
					cerr << "WARNING: buffer overflow saving pedal " << kPedalNames[pedal] << " (timestamp " << currentTimeStamp_ << ")\n";
				}
				else
				{
					if(recordStatus == kRecordStatusPostEvent)
						samplesToRecord = min(recordPedalRemainingSamples_[pedal], 
											  pedalDistanceFromFeature(pedal, recordPedalHistoryIOPosition_[pedal], pedalHistoryPosition_[pedal]));
					else
						samplesToRecord = pedalDistanceFromFeature(pedal, recordPedalHistoryIOPosition_[pedal], pedalHistoryPosition_[pedal]);
					
					if(recordPedalHistoryIOPosition_[pedal] < pedalHistoryPosition_[pedal])
					{
						// Can save the buffer in one write command, since data lies contiguously with no wraparound
						
						int partialBytes1 = samplesToRecord*sizeof(short);
						newIOPosition = recordPedalHistoryIOPosition_[pedal] + samplesToRecord;
						
						pthread_mutex_unlock(&audioMutex_);
						
						write(recordPedalFilePointer_[pedal], 
							  ((uint8_t *)pedalHistoryRaw_[pedal]) + recordPedalHistoryIOPosition_[pedal]*sizeof(short),
							  partialBytes1);
						
						recordPedalHistoryIOPosition_[pedal] = newIOPosition;
					}
					else if(recordPedalHistoryIOPosition_[pedal] > pedalHistoryPosition_[pedal])
					{
						// Need two write commands, one from IOPosition to the end of the buffer, another from the beginning
						// of the buffer to the callback position
						
						int bufLengthBytes = pedalHistoryLength_[pedal] * sizeof(short);
						int startPosBytes = recordPedalHistoryIOPosition_[pedal] * sizeof(short);
						int totalBytes = samplesToRecord * sizeof(short);
						
						newIOPosition = (recordPedalHistoryIOPosition_[pedal] + samplesToRecord) % pedalHistoryLength_[pedal];
						pthread_mutex_unlock(&audioMutex_);
						
						if(bufLengthBytes - startPosBytes < samplesToRecord)
						{
							// Record in two chunks
							
							write(recordPedalFilePointer_[pedal], ((uint8_t *)pedalHistoryRaw_[pedal]) + startPosBytes, bufLengthBytes - startPosBytes);
							write(recordPedalFilePointer_[pedal], (void *)pedalHistoryRaw_[pedal], totalBytes - (bufLengthBytes - startPosBytes));
						}
						else
						{
							// It all fits in one chunk
							
							write(recordPedalFilePointer_[pedal], ((uint8_t *)pedalHistoryRaw_[pedal]) + startPosBytes, totalBytes);
						}
						
						recordPedalHistoryIOPosition_[pedal] = newIOPosition;
					}
					else
						pthread_mutex_unlock(&audioMutex_);	// Nothing to do here
				}
				
				if(recordStatus == kRecordStatusPostEvent)
				{
					recordPedalRemainingSamples_[pedal] -= samplesToRecord;
				}
			}
			else if(recordStatus == kRecordStatusPostEvent && recordPedalRemainingSamples_[pedal] <= 0)
			{
				// We've recorded all the samples we need to into this file.  All that remains is to see
				// whether enough time has elapsed that we can close the file.  This depends on the pedal
				// record spacing: it's possible that if an event comes up post-recording but within the spacing
				// window, we'll want to continue recording where we left off.  That's easy enough since
				// recordPedalHistoryIOPosition is waiting at the end of the last samples recorded.
				
				if(currentTimeStamp_ > recordPedalCloseFileTimestamp_[pedal])
				{
					close(recordPedalFilePointer_[pedal]);
					openKeyPedalFilePointers_.erase(recordPedalFilePointer_[pedal]);
					recordPedalFilePointer_[pedal] = -1;
					
					recordPedalStatus_[pedal] = kRecordStatusOnActivity;	// Go back to waiting state
				}
			}
		}		
		
		
		if(recordRawStream_)
		{
			// In this case, we dump the data from the Piano Bar device directly to disk with no further processing.
			
			pthread_mutex_lock(&audioMutex_);
			
			if(recordGlobalHistoryOverflow_)
			{
				// The whole buffer needs to be saved.  We've already lost some data and might well end up losing some
				// more since the callback will keep running while we do this, but do our best to save what we can
			
				int totalBytes = recordGlobalHistoryLength_ * PIANO_BAR_FRAME_SIZE;
				int partialBytes1 = recordGlobalHistoryIOPosition_ * PIANO_BAR_FRAME_SIZE;
				newIOPosition = recordGlobalHistoryCallbackPosition_; // This value might change during write
				
				pthread_mutex_unlock(&audioMutex_);
				
				// Yes, some of this data will get corrupted going to file, but ultimately there's not much we can do
				// when the buffer overflows.  The good news is that since each point is timestamped, there won't be
				// any confusion as to when the overflowed samples should have occurred (hopefully).
				
				// FIXME: handle buffer overflow better in the future?
				
				write(recordGlobalHistoryFile_, ((uint8_t *)recordGlobalHistory_) + partialBytes1, totalBytes - partialBytes1);
				write(recordGlobalHistoryFile_, (void *)recordGlobalHistory_, partialBytes1);
				
				recordGlobalHistoryIOPosition_ = newIOPosition;		// Set position to current pointer
				recordGlobalHistoryOverflow_ = false;
				
				cerr << "WARNING: buffer overflow saving global history (timestamp " << currentTimeStamp_ << ")\n";
			}
			else if(recordGlobalHistoryIOPosition_ < recordGlobalHistoryCallbackPosition_)
			{
				// Can save the buffer in one write command, since data lies contiguously with no wraparound
					
				int partialBytes1 = (recordGlobalHistoryCallbackPosition_ - recordGlobalHistoryIOPosition_)*PIANO_BAR_FRAME_SIZE;
				newIOPosition = recordGlobalHistoryCallbackPosition_;
				
				pthread_mutex_unlock(&audioMutex_);
				
				write(recordGlobalHistoryFile_, 
					  ((uint8_t *)recordGlobalHistory_) + recordGlobalHistoryIOPosition_*PIANO_BAR_FRAME_SIZE,
					  partialBytes1);
				
				recordGlobalHistoryIOPosition_ = newIOPosition;
			}
			else if(recordGlobalHistoryIOPosition_ > recordGlobalHistoryCallbackPosition_)
			{
				// Need two write commands, one from IOPosition to the end of the buffer, another from the beginning
				// of the buffer to the callback position

				int totalBytes = recordGlobalHistoryLength_ * PIANO_BAR_FRAME_SIZE;
				int partialBytes1 = recordGlobalHistoryIOPosition_ * PIANO_BAR_FRAME_SIZE;
				int partialBytes2 = recordGlobalHistoryCallbackPosition_ * PIANO_BAR_FRAME_SIZE;
				newIOPosition = recordGlobalHistoryCallbackPosition_;
				
				pthread_mutex_unlock(&audioMutex_);
				
				write(recordGlobalHistoryFile_, ((uint8_t *)recordGlobalHistory_) + partialBytes1, totalBytes - partialBytes1);
				write(recordGlobalHistoryFile_, (void *)recordGlobalHistory_, partialBytes2);
					
				recordGlobalHistoryIOPosition_ = newIOPosition;
			}
			else
				pthread_mutex_unlock(&audioMutex_);	// Nothing to do here

		}
		
		pthread_mutex_lock(&calibrationMutex_);
		
		if(recordCalibrationNeedsSaving_)
		{
			recordSaveCalibration(recordCalibrationCompletionTime_);
			recordCalibrationNeedsSaving_ = false;
		}
		
		pthread_mutex_unlock(&calibrationMutex_);
		
		usleep(50000);		// Wake up every 50ms
	}
	
	return NULL;
}

void PianoBarController::printKeyStatus()
{
	if(!isInitialized_)
	{
		cout << "Piano Bar not initialized.\n";
		return;
	}
	if(!isRunning_)
	{
		cout << "Piano Bar not running.\n";
		return;
	}
	switch(calibrationStatus_)
	{
		case kPianoBarNotCalibrated:
			cout << "Piano Bar not calibrated.\n";
			return;
		case kPianoBarInCalibration:
		case kPianoBarAutoCalibration:
			cout << "Piano Bar in calibration.\n";
			return;
		case kPianoBarCalibrated:
		case kPianoBarCalibratedButNoUpdates:
			cout << "Octave 0:                                                       A     A#    B\n";
			printKeyStatusHelper(0, 3, strlen("Octave 0:                                                       "));
			cout << "\nOctave 1: C     C#    D     D#    E     F     F#    G     G#    A     A#    B\n";
			printKeyStatusHelper(3, 12, strlen("Octave 1: "));
			cout << "\nOctave 2: C     C#    D     D#    E     F     F#    G     G#    A     A#    B\n";
			printKeyStatusHelper(15, 12, strlen("Octave 1: "));
			cout << "\nOctave 3: C     C#    D     D#    E     F     F#    G     G#    A     A#    B\n";
			printKeyStatusHelper(27, 12, strlen("Octave 1: "));
			cout << "\nOctave 4: C     C#    D     D#    E     F     F#    G     G#    A     A#    B\n";
			printKeyStatusHelper(39, 12, strlen("Octave 1: "));
			cout << "\nOctave 5: C     C#    D     D#    E     F     F#    G     G#    A     A#    B\n";
			printKeyStatusHelper(51, 12, strlen("Octave 1: "));
			cout << "\nOctave 6: C     C#    D     D#    E     F     F#    G     G#    A     A#    B\n";
			printKeyStatusHelper(63, 12, strlen("Octave 1: "));
			cout << "\nOctave 7: C     C#    D     D#    E     F     F#    G     G#    A     A#    B     C\n";
			printKeyStatusHelper(75, 13, strlen("Octave 1: "));
			
			cout << kPedalNames[0] << " pedal: " << runningPedalAverage(0, 0, 16) << endl;
			cout << kPedalNames[1] << " pedal: " << runningPedalAverage(1, 0, 16) << endl;
			cout << kPedalNames[2] << " pedal: " << runningPedalAverage(2, 0, 16) << endl;
			return;
		default:
			cout << "Piano Bar: Unknown calibration status.\n";
			return;
	}
}


// Print the current position and calibration information for a given key

void PianoBarController::printIndividualKeyPosition(int key)
{	
	int rawPosition, calPosition;
	
	if(key < 0 || key > 87)
	{
		cout << "Invalid key\n";
		return;
	}
	
	if(!isInitialized_)
	{
		cout << "Piano Bar not initialized.\n";
		return;
	}
	if(!isRunning_)
	{
		cout << "Piano Bar not running.\n";
		return;
	}
	switch(calibrationStatus_)
	{
		case kPianoBarNotCalibrated:
			rawPosition = rawRunningPositionAverage(key, 0, 100);
			cout << "Raw: " << rawPosition << " (not calibrated)\n";
			return;
		case kPianoBarInCalibration:
		case kPianoBarAutoCalibration:
			rawPosition = rawRunningPositionAverage(key, 0, 100);
			cout << "Raw: " << rawPosition << " (not calibrated)\n";
			return;
		case kPianoBarCalibrated:
		case kPianoBarCalibratedButNoUpdates:
			rawPosition = rawRunningPositionAverage(key, 0, 100);
			calPosition = runningPositionAverage(key, 0, 100);
			cout << "Raw: " << rawPosition << " Calibrated: " << calPosition << " Range (";
			cout << calibrationQuiescent_[key][0] << ", " << calibrationFullPress_[key][0] << ")\n";
			return;
		default:
			cout << "Piano Bar: Unknown calibration status.\n";
			return;
	}	
}

// Destructor needs to close the currently open device

PianoBarController::~PianoBarController()
{
	closeDevice();
	pthread_mutex_destroy(&audioMutex_);
	pthread_mutex_destroy(&calibrationMutex_);
	pthread_mutex_destroy(&eventLogMutex_);
}

#pragma mark --- Private Methods ---

// Private helper function prints state and position of <length> keys starting at <start>

void PianoBarController::printKeyStatusHelper(int start, int length, int padSpaces)
{
	int i;
	const char *shortStateNames[kKeyStatesLength] = {"Unk.  ", "Idle  ", "PreT  ", "PreV  ", "Tap   ",
		"Press ", "Down  ", "AftT  ", "AftV  ", "Rel   ", "PRel  ", "*D/A* ", "*D/A* ", "IAct  ", "PMax  ", "Rstrk "};
	
	// On the first line, print the state of each key
	
	for(i = 0; i < padSpaces; i++)
		cout << " ";
	for(i = start; i < start + length; i++)
		cout << shortStateNames[currentKeyState(i)];
	cout << endl;
	
	// On the second line, print the current position
	
	for(i = 0; i < padSpaces; i++)
		cout << " ";
	for(i = start; i < start + length; i++)
		printf("%-6d", runningPositionAverage(i, 0, (kPianoBarKeyColor[i] == K_W) ? 10 : 30));
	cout << endl;
}

#pragma mark Audio Input

// Callback from portaudio when new data is available.  In this function, pull it apart to separate out individual keys.

int PianoBarController::audioCallback(const void *input, void *output, 
									  unsigned long frameCount, const PaStreamCallbackTimeInfo* timeInfo,
									  PaStreamCallbackFlags statusFlags)
{
	int readCount;
	short *inData = (short *)input;
	
	pthread_mutex_lock(&audioMutex_);
	
	for(readCount = 0; readCount < frameCount; readCount++)
	{
		// First, save the raw stream if we're logging it to file
		
		if(recordRawStream_)
		{
			memcpy((uint8_t *)recordGlobalHistory_ + recordGlobalHistoryCallbackPosition_*PIANO_BAR_FRAME_SIZE, 
				   inData, PIANO_BAR_FRAME_SIZE);
			recordGlobalHistoryCallbackPosition_ = (recordGlobalHistoryCallbackPosition_ + 1) % recordGlobalHistoryLength_;
			if(recordGlobalHistoryCallbackPosition_ == recordGlobalHistoryIOPosition_)	// If we cath up, it's an overflow
				recordGlobalHistoryOverflow_ = true;
		}
		
		// Unpack data for this frame
		
		processPianoBarFrame(inData, readCount);
		
		inData += 10;	// Each frame is 10 samples of 16 bits
		
		if(calibrationStatus_ == kPianoBarInCalibration || calibrationStatus_ == kPianoBarAutoCalibration)
			calibrationSamples_++;
		
		if(currentTimeStamp_ % KEY_MESSAGE_UPDATE_INTERVAL == 0)	// Send key state messages at regular intervals
			sendKeyStateMessages();
	}
	
	pthread_mutex_unlock(&audioMutex_);
	
	return paContinue;
}

// Store a new value from the audio callback into the history buffer.  If we're in calibration, also examine
// the raw values for calibration purposes

void PianoBarController::processKeyValue(short midiNote, short type, short value)
{
	bool white, forCalibrationOnly = false;
	int key, calibratedValueInt, calibratedValueDenominator, seqOffset;
	short signedValue;
	
	// Though the Piano Bar reports multiple data points per sequence for some white keys, a consistent
	// approach across the keyboard is preferred.  Therefore, we'll only pay attention to the first white
	// key sample per sequence, and the first three black key samples (Bb7 samples 4 times).
	
	switch(type)
	{
		case PB_W1:
			white = true;
			seqOffset = 0;
			break;
		case PB_W2:
			if(calibrationStatus_ != kPianoBarInCalibration && calibrationStatus_ != kPianoBarAutoCalibration)
				return;
			forCalibrationOnly = true;
			white = true;
			seqOffset = 1;
			break;
		case PB_W3:
			if(calibrationStatus_ != kPianoBarInCalibration && calibrationStatus_ != kPianoBarAutoCalibration)
				return;		
			forCalibrationOnly = true;
			white = true;
			seqOffset = 2;
			break;
		case PB_W4:
			if(calibrationStatus_ != kPianoBarInCalibration && calibrationStatus_ != kPianoBarAutoCalibration)
				return;			
			forCalibrationOnly = true;
			white = true;
			seqOffset = 3;
			break;			
		case PB_B1:			
			white = false;
			seqOffset = 0;
			break;
		case PB_B2:
			white = false;
			seqOffset = 1;
			break;
		case PB_B3:
			white = false;
			seqOffset = 2;
			break;
		case PB_B4:
			if(calibrationStatus_ != kPianoBarInCalibration && calibrationStatus_ != kPianoBarAutoCalibration)
				return;			
			forCalibrationOnly = true;
			white = false;
			seqOffset = 3;
		case PB_NA:
		default:
			return;
	}
	
	if(midiNote < 21 || midiNote > 108)		// Shouldn't happen, but...
		return;								// can't do anything with something outside the piano range
	key = midiNote - 21;					// Convert to buffer index
	
	signedValue = (value > 2047 ? value - 4096 : value);	// Convert from signed 12-bit int to signed 16-bit int
	
	switch(calibrationStatus_)
	{
		case kPianoBarNotCalibrated:
			if(keyHistory_[key] == NULL)
				return;
			
			// Store the raw value, but no calibrated value (which means no processing will take place)
			
			keyHistoryPosition_[key] = (keyHistoryPosition_[key] + 1) % keyHistoryLength_[key];	
			if(keyHistoryPosition_[key] == recordKeyHistoryIOPosition_[key])	// This only matters if
				recordKeyHistoryOverflow_[key] = true;							// recording is enabled
			keyHistoryRaw_[key][keyHistoryPosition_[key]] = packRawValue(value, type);
			keyHistory_[key][keyHistoryPosition_[key]] = 0;
			keyHistoryTimestamps_[key][keyHistoryPosition_[key]] = currentTimeStamp_;
			keyVelocityHistory_[key][keyHistoryPosition_[key]] = 0;
			break;
		case kPianoBarCalibrated:	
		case kPianoBarCalibratedButNoUpdates:
			if(keyHistory_[key] == NULL)
				return;
			
			calibratedValueInt = 4096*(int)(signedValue - calibrationQuiescent_[key][seqOffset]);
			calibratedValueDenominator = (calibrationFullPress_[key][seqOffset] - calibrationQuiescent_[key][seqOffset]);
			if(calibratedValueDenominator == 0)	// Prevent divide-by-0 errors
				calibratedValueDenominator = 1;
			calibratedValueInt /= calibratedValueDenominator;

			if(abs(warpedValue(key, calibratedValueInt) - keyHistory_[key][keyHistoryPosition_[key]]) > 1000 && currentKeyState(key) != kKeyStateDisabledByCalibrator)
			{
				cout << " **** JUMP from " << keyHistory_[key][keyHistoryPosition_[key]] << " (ts " << keyHistoryTimestamps_[key][keyHistoryPosition_[key]];
				cout << ") to " << warpedValue(key, calibratedValueInt) << " (key " << key << " ts " << currentTimeStamp_ << ")\n";
			}
			
			keyHistoryPosition_[key] = (keyHistoryPosition_[key] + 1) % keyHistoryLength_[key];
			if(keyHistoryPosition_[key] == recordKeyHistoryIOPosition_[key])	// This only matters if
				recordKeyHistoryOverflow_[key] = true;							// recording is enabled			
			keyHistoryRaw_[key][keyHistoryPosition_[key]] = packRawValue(value, type);
			keyHistory_[key][keyHistoryPosition_[key]] = warpedValue(key, calibratedValueInt);
			keyHistoryTimestamps_[key][keyHistoryPosition_[key]] = currentTimeStamp_;
			if(currentKeyState(key) != kKeyStateIdle)
				keyVelocityHistory_[key][keyHistoryPosition_[key]] = calculateKeyVelocity(key, keyHistoryPosition_[key]);
			else
				keyVelocityHistory_[key][keyHistoryPosition_[key]] = UNCALIBRATED;	// Save this for later, only if we really need it
			
			//if(currentKeyState(key) != kKeyStateIdle && currentKeyState(key) != kKeyStateInitialActivity)
			//	cout << "**** " << key << " ** " << value << " ** " << calibratedValueInt << endl;
			

			
			if(calibrationStatus_ == kPianoBarCalibrated)
				updateKeyState(key);
			break;
		case kPianoBarInCalibration:
		case kPianoBarAutoCalibration:
			if(!forCalibrationOnly)
			{
				// Store the raw value, as in the uncalibrated setting
				
				keyHistoryPosition_[key] = (keyHistoryPosition_[key] + 1) % keyHistoryLength_[key];			
				if(keyHistoryPosition_[key] == recordKeyHistoryIOPosition_[key])	// This only matters if
					recordKeyHistoryOverflow_[key] = true;							// recording is enabled				
				keyHistoryRaw_[key][keyHistoryPosition_[key]] = packRawValue(value, type);
				keyHistory_[key][keyHistoryPosition_[key]] = 0;
				keyHistoryTimestamps_[key][keyHistoryPosition_[key]] = currentTimeStamp_;
				keyVelocityHistory_[key][keyHistoryPosition_[key]] = 0;
			}		
			
			if(calibrationHistory_[key][seqOffset] == NULL)
				break;
			calibrationHistoryPosition_[key][seqOffset] = (calibrationHistoryPosition_[key][seqOffset] + 1) % calibrationHistoryLength_;
			calibrationHistory_[key][seqOffset][calibrationHistoryPosition_[key][seqOffset]] = signedValue;
			
			if(calibrationStatus_ == kPianoBarAutoCalibration && keyPositionModes_[key][seqOffset] != NULL)
			{
				if(keyPositionModes_[key][seqOffset]->count(signedValue) == 0)
					(*keyPositionModes_[key][seqOffset])[signedValue] = 1;
				else
					(*keyPositionModes_[key][seqOffset])[signedValue] = (*keyPositionModes_[key][seqOffset])[signedValue] + 1;	
			}
			
			if(calibrationStatus_ == kPianoBarAutoCalibration && calibrationSamples_ < 18*64)
				break;
			
			// Quiescent levels have been set when calibration began.  Watch for significant changes to set key press levels
			// Check for end of key press by comparing last M samples against the N samples before that.  If they
			// (approximately) match, the key press is done.
			
			if(calibrationQuiescent_[key][seqOffset] == UNCALIBRATED && calibrationStatus_ == kPianoBarInCalibration)	// Don't go any further until we've set quiescent value
				break;
			
			if(keysToCalibrate_ != NULL)
			{
				bool foundMatch = false;
				int k;
				
				for(k = 0; k < keysToCalibrate_->size(); k++)
				{
					if((*keysToCalibrate_)[k] == key + 21)
					{
						foundMatch = true;
						break;
					}
				}
				
				if(!foundMatch)
					break;
			}
			
			if(white)
			{
				int calibrationLength = (calibrationStatus_ == kPianoBarInCalibration ? 10 : 30);	// Longer window for auto-calibration
				int currentAverage = calibrationRunningAverage(key, seqOffset, 0, calibrationLength);

				// Look for minimum overall value
				if(currentAverage < (int)calibrationFullPress_[key][seqOffset] ||
				   calibrationFullPress_[key][seqOffset] == UNCALIBRATED)
					calibrationFullPress_[key][seqOffset] = (short)currentAverage;							
			}
			else // black
			{
				int calibrationLength = (calibrationStatus_ == kPianoBarInCalibration ? 10 : 30);	// Longer window for auto-calibration
				int currentAverage = calibrationRunningAverage(key, seqOffset, 0, calibrationLength);

				// Heavy press is the maximum overall value
				if(currentAverage > (int)calibrationFullPress_[key][seqOffset] ||
				   calibrationFullPress_[key][seqOffset] == UNCALIBRATED)
					calibrationFullPress_[key][seqOffset] = (short)currentAverage;
			}
			break;
	}
}

void PianoBarController::processPedalValue(int pedal, short value)
{
	int calibratedValueInt, calibratedValueDenominator, currentAverage;
	int k;

	if(pedal < 0 || pedal > 2)				// Shouldn't happen....
		return;
	
	switch(calibrationStatus_)
	{
		case kPianoBarNotCalibrated:
			if(pedalHistory_[pedal] == NULL)
				return;
			
			// Store the raw value, but no calibrated value (which means no processing will take place)
			
			pedalHistoryPosition_[pedal] = (pedalHistoryPosition_[pedal] + 1) % pedalHistoryLength_[pedal];	
			if(pedalHistoryPosition_[pedal] == recordPedalHistoryIOPosition_[pedal])	// This only matters if
				recordPedalHistoryOverflow_[pedal] = true;								// recording is enabled			
			pedalHistoryRaw_[pedal][pedalHistoryPosition_[pedal]] = value;
			pedalHistory_[pedal][pedalHistoryPosition_[pedal]] = 0;
			pedalHistoryTimestamps_[pedal][pedalHistoryPosition_[pedal]] = currentTimeStamp_;
			break;
		case kPianoBarCalibrated:	
		case kPianoBarCalibratedButNoUpdates:
			if(pedalHistory_[pedal] == NULL)
				return;
			
			calibratedValueInt = 4096*(int)(value - pedalCalibrationQuiescent_[pedal]);
			calibratedValueDenominator = (pedalCalibrationFullPress_[pedal] - pedalCalibrationQuiescent_[pedal]);
			if(calibratedValueDenominator == 0)	// Prevent divide-by-0 errors
				calibratedValueDenominator = 1;
			calibratedValueInt /= calibratedValueDenominator;
			
			pedalHistoryPosition_[pedal] = (pedalHistoryPosition_[pedal] + 1) % pedalHistoryLength_[pedal];
			if(pedalHistoryPosition_[pedal] == recordPedalHistoryIOPosition_[pedal])	// This only matters if
				recordPedalHistoryOverflow_[pedal] = true;								// recording is enabled	
			pedalHistoryRaw_[pedal][pedalHistoryPosition_[pedal]] = value;
			pedalHistory_[pedal][pedalHistoryPosition_[pedal]] = calibratedValueInt;
			pedalHistoryTimestamps_[pedal][pedalHistoryPosition_[pedal]] = currentTimeStamp_;
			
			if(calibrationStatus_ == kPianoBarCalibrated)
				updatePedalState(pedal);
			break;
		case kPianoBarInCalibration:
		case kPianoBarAutoCalibration:
			if(pedalHistory_[pedal] == NULL)
				return;
			
			// Store the raw value, as in the uncalibrated setting
			
			pedalHistoryPosition_[pedal] = (pedalHistoryPosition_[pedal] + 1) % pedalHistoryLength_[pedal];	
			if(pedalHistoryPosition_[pedal] == recordPedalHistoryIOPosition_[pedal])	// This only matters if
				recordPedalHistoryOverflow_[pedal] = true;								// recording is enabled	
			pedalHistoryRaw_[pedal][pedalHistoryPosition_[pedal]] = value;
			pedalHistory_[pedal][pedalHistoryPosition_[pedal]] = 0;
			pedalHistoryTimestamps_[pedal][pedalHistoryPosition_[pedal]] = currentTimeStamp_;

			if(calibrationStatus_ == kPianoBarAutoCalibration && pedalPositionModes_[pedal] != NULL)
			{
				if(pedalPositionModes_[pedal]->count(value) == 0)
					(*pedalPositionModes_[pedal])[value] = 1;
				else
					(*pedalPositionModes_[pedal])[value] = (*pedalPositionModes_[pedal])[value] + 1;	
			}
			
			if(calibrationStatus_ == kPianoBarAutoCalibration && calibrationSamples_ < 18*64)
				break;			
			if(pedalCalibrationQuiescent_[pedal] == UNCALIBRATED && calibrationStatus_ == kPianoBarInCalibration)	// Don't go any further until we've set quiescent value
				break;
			if(pedalsToCalibrate_ != NULL)
			{
				if(pedalsToCalibrate_ != NULL)
				{
					bool foundMatch = false;
					
					for(k = 0; k < pedalsToCalibrate_->size(); k++)
					{
						if((*pedalsToCalibrate_)[k] == pedal)
						{
							foundMatch = true;
							break;
						}
					}
					
					if(!foundMatch)
						break;
				}				
			}
			else if(keysToCalibrate_ != NULL)	// Don't calibrate pedals if there are specific keys to calibrate
				break;
			
			currentAverage = pedalCalibrationRunningAverage(pedal, 0, 50);	// Pressed pedal means farther from sensor,
			if(currentAverage < (int)pedalCalibrationFullPress_[pedal])		// means reflected value is smaller
				pedalCalibrationFullPress_[pedal] = (short)currentAverage;
			break;
	}
}


// Process one frame of Piano Bar data, whether it comes from file or audio.
// Updates internal state variables (i.e. only one copy of this at a time)

void PianoBarController::processPianoBarFrame(short *inData, int readCount)
{
	int sequence;
	short key, value, type;
	
	currentTimeStamp_++;
	
	// data format: 10 channels at 16 bits each
	// ch0: flags
	//		15-12: reserved
	//		11-8: sequence number, for skip detection
	//		7-6: reserved
	//		5-1: sequence counter-- holds values 0-17
	//		0: data valid test, should always be 0
	// ch1-9: (144 bits)
	//		packed 12-bit little-endian values of 12 channels
	
	if(inData[0] & 0x0001)
	{
		if(currentTimeStamp_ > dataErrorLastTimestamp_ + DATA_ERROR_MESSAGE_SPACING)
			dataErrorCount_ = 0;
		if(dataErrorCount_ < DATA_ERROR_MAX_MESSAGES)
		{
			cerr << "PianoBarController warning: parity error in PianoBar data (data = " << inData[0];
			cerr << " count = " << readCount << ")\n";
			dataErrorCount_++;
			dataErrorLastTimestamp_ = currentTimeStamp_;				
		}
		
		// Just don't increment the history.  This will lead to some weird time stretching, but
		// it's the easiest option for now.
		return;					
	}
	
	sequence = (int)(inData[0] & 0x003E) >> 1;
	
	if(sequence > 17 || sequence < 0)
	{
		if(currentTimeStamp_ > dataErrorLastTimestamp_ + DATA_ERROR_MESSAGE_SPACING)
			dataErrorCount_ = 0;
		if(dataErrorCount_ < DATA_ERROR_MAX_MESSAGES)
		{
			cerr << "PianoBarController warning: sequence " << sequence << " out of range (data = ";
			cerr << inData[0] << " count = " << readCount << ")\n";
			dataErrorCount_++;
			dataErrorLastTimestamp_ = currentTimeStamp_;
		}
		return;
	}
	
	// Retrieve key numbers (0 for unused slots), types (white/black, cycle number), and value
	//   (signed 12-bit, -2048 to 2047) for each group
	// Piano Bar groups are interleaved between ADCs.  Order: 1 2 5 6 9 10 3 4 7 8 11 12
	//   This is because the second two groups of each board (3,4,7,8,11,12) are offset in phase by half
	//   a cycle with respect to the first two groups.
	
	key = kPianoBarMapping[sequence][0];
	type = kPianoBarSignalTypes[sequence][0];
	value = (inData[1] & 0x00FF) + ((inData[1] & 0xF000) >> 4);
	processKeyValue(key, type, value);
	
	key = kPianoBarMapping[sequence][1];
	type = kPianoBarSignalTypes[sequence][1];
	value = ((inData[1] & 0x0F00) >> 4) + ((inData[2] & 0x00F0) >> 4) + ((inData[2] & 0x000F) << 8);
	processKeyValue(key, type, value);
	
	key = kPianoBarMapping[sequence][4];
	type = kPianoBarSignalTypes[sequence][4];
	value = ((inData[2] & 0xFF00) >> 8) + ((inData[3] & 0x00F0) << 4);
	processKeyValue(key, type, value);
	
	key = kPianoBarMapping[sequence][5];
	type = kPianoBarSignalTypes[sequence][5];
	value = ((inData[3] & 0x000F) << 4) + ((inData[3] & 0xF000) >> 12) + (inData[3] & 0x0F00);
	processKeyValue(key, type, value);
	
	key = kPianoBarMapping[sequence][8];
	type = kPianoBarSignalTypes[sequence][8];
	value = (inData[4] & 0x00FF) + ((inData[4] & 0xF000) >> 4);
	processKeyValue(key, type, value);
	
	key = kPianoBarMapping[sequence][9];
	type = kPianoBarSignalTypes[sequence][9];
	value = ((inData[4] & 0x0F00) >> 4) + ((inData[5] & 0x00F0) >> 4) + ((inData[5] & 0x000F) << 8);
	processKeyValue(key, type, value);
	
	key = kPianoBarMapping[sequence][2];
	type = kPianoBarSignalTypes[sequence][2];
	value = ((inData[5] & 0xFF00) >> 8) + ((inData[6] & 0x00F0) << 4);
	processKeyValue(key, type, value);
	
	key = kPianoBarMapping[sequence][3];
	type = kPianoBarSignalTypes[sequence][3];
	value = ((inData[6] & 0x000F) << 4) + ((inData[6] & 0xF000) >> 12) + (inData[6] & 0x0F00);		
	processKeyValue(key, type, value);
	
	key = kPianoBarMapping[sequence][6];
	type = kPianoBarSignalTypes[sequence][6];
	value = (inData[7] & 0x00FF) + ((inData[7] & 0xF000) >> 4);
	processKeyValue(key, type, value);
	
	key = kPianoBarMapping[sequence][7];
	type = kPianoBarSignalTypes[sequence][7];
	value = ((inData[7] & 0x0F00) >> 4) + ((inData[8] & 0x00F0) >> 4) + ((inData[8] & 0x000F) << 8);
	processKeyValue(key, type, value);
	
	key = kPianoBarMapping[sequence][10];
	type = kPianoBarSignalTypes[sequence][10];
	value = ((inData[8] & 0xFF00) >> 8) + ((inData[9] & 0x00F0) << 4);
	processKeyValue(key, type, value);
	
	key = kPianoBarMapping[sequence][11];
	type = kPianoBarSignalTypes[sequence][11];
	value = ((inData[9] & 0x000F) << 4) + ((inData[9] & 0xF000) >> 12) + (inData[9] & 0x0F00);						
	processKeyValue(key, type, value);	
	
	// Load pedal data for select frames
	// Pedal values are 10 bits, stored in the high 10 bits of channel 0
	
	switch(sequence)
	{
		case 0:			// Damper pedal
			processPedalValue(kPedalDamper, (unsigned short)inData[0] >> 6);
			break;
		case 6:			// Sostenuto pedal
			processPedalValue(kPedalSostenuto, (unsigned short)inData[0] >> 6);
			break;
		case 12:		// Una corda pedal
			processPedalValue(kPedalUnaCorda, (unsigned short)inData[0] >> 6);
			break;
		default:		// Don't use the rest of the values
			break;
	}
}

#pragma mark Motion Analysis

// Return the average of the last N points for a particular key using the raw value
// key = 0 to 87 (not MIDI note number)

int PianoBarController::rawRunningPositionAverage(int key, int offset, int length)
{
	int sum = 0, loc;
	int i;
	short rawValue, unpackedValue, type;
	
	if(length == 0)
		return 0;
	
	loc = (keyHistoryPosition_[key] - offset - length + keyHistoryLength_[key]) % keyHistoryLength_[key];
	
	for(i = 0; i < length; i++)
	{
		rawValue = keyHistoryRaw_[key][loc];
		unpackRawValue(rawValue, &unpackedValue, &type);
		sum += (int)unpackedValue;
		loc = (loc + 1) % keyHistoryLength_[key];
	}
	
	return sum / length;
}

// Return the average of the last N points for a particular key
// key = 0 to 87 (not MIDI note number)

int PianoBarController::runningPositionAverage(int key, int offset, int length)
{
	int sum = 0, loc;
	int i;
	
	if(length == 0)
		return 0;
	
	loc = (keyHistoryPosition_[key] - offset - length + keyHistoryLength_[key]) % keyHistoryLength_[key];
	
	for(i = 0; i < length; i++)
	{
		sum += keyHistory_[key][loc];
		loc = (loc + 1) % keyHistoryLength_[key];
	}
	
	return sum / length;
}

// Return the average velocity over the last N points for a particular key
// key = 0 to 87
// Since we're doing this all with integer math and velocity numbers can be pretty
// small, multiply the result by a scaler so we get better resolution

int PianoBarController::runningVelocityAverage(int key, int offset, int length)
{	
	int scaler;
	
	// Velocity is the first difference (more-or-less).  And the handy thing about summing
	// a string of first differences is that they telescope.
	
	if(length == 0)
		return 0;
	
	int start = (keyHistoryPosition_[key] - offset - length + keyHistoryLength_[key]) % keyHistoryLength_[key];
	int finish = (keyHistoryPosition_[key] - offset + keyHistoryLength_[key]) % keyHistoryLength_[key];
	
	// Black keys sample three times as frequently, so we need to compensate for that in the scaling of the result
	if(kPianoBarKeyColor[key] == K_B)
		scaler = OLD_VELOCITY_SCALER * 3;
	else
		scaler = OLD_VELOCITY_SCALER;
	
	if(length > 1)
		return scaler*(keyHistory_[key][finish] - keyHistory_[key][start]) / length;
	return scaler*(keyHistory_[key][finish] - keyHistory_[key][start]);
}

// Return the average acceleration over the last N points for a particular key
// key = 0 to 87
// Since we're doing this all with integer math and velocity numbers can be pretty
// small, multiply the result by a scaler so we get better resolution

int PianoBarController::runningAccelerationAverage(int key, int offset, int length)
{	
	int scaler;
	
	// Velocity is the first difference (more-or-less).  And the handy thing about summing
	// a string of first differences is that they telescope.
	
	if(length == 0)
		return 0;
	
	int start = runningVelocityAverage(key, offset + length, 1);
	int finish = runningVelocityAverage(key, offset, 1);
	
	// Black keys sample three times as frequently, so we need to compensate for that in the scaling of the result
	if(kPianoBarKeyColor[key] == K_B)
		scaler = 3;
	else
		scaler = 1;
	
	if(length > 1)
		return scaler*(finish - start) / length;
	return scaler*(finish - start);
}

// Return the average of the last N points for a particular pedal

int PianoBarController::runningPedalAverage(int pedal, int offset, int length)
{
	int sum = 0, loc;
	int i;
	
	if(length == 0)
		return 0;
	
	loc = (pedalHistoryPosition_[pedal] - offset - length + pedalHistoryLength_[pedal]) % pedalHistoryLength_[pedal];
	
	for(i = 0; i < length; i++)
	{
		sum += pedalHistory_[pedal][loc];
		loc = (loc + 1) % pedalHistoryLength_[pedal];
	}
	
	return sum / length;
}

// Return the average of the last N points for a particular pedal's raw value (for calibration)

int PianoBarController::pedalCalibrationRunningAverage(int pedal, int offset, int length)
{
	int sum = 0, loc;
	int i;
	
	if(length == 0)
		return 0;
	
	loc = (pedalHistoryPosition_[pedal] - offset - length + pedalHistoryLength_[pedal]) % pedalHistoryLength_[pedal];
	
	for(i = 0; i < length; i++)
	{
		sum += (int)pedalHistoryRaw_[pedal][loc];
		loc = (loc + 1) % pedalHistoryLength_[pedal];
	}
	
	return sum / length;
}

// Return the average of the last N points for a particular key, with a particular place within
// the cycle.
// key = 0 to 87 (not MIDI note number)
// offset = number of samples back to go

int PianoBarController::calibrationRunningAverage(int key, int seq, int offset, int length)
{
	int sum = 0, loc, count = 0;;
	int i;
	
	loc = (calibrationHistoryPosition_[key][seq] - offset - length + calibrationHistoryLength_) % calibrationHistoryLength_;
	
	for(i = 0; i < length; i++)
	{
		if(calibrationHistory_[key][seq][loc] != UNCALIBRATED)
		{
			sum += calibrationHistory_[key][seq][loc];
			count++;
		}
		loc = (loc + 1) % calibrationHistoryLength_;
	}
	
	if(count == 0)
		return UNCALIBRATED;
	else if(count < length)
		cout << "Warning: running average for key " << kNoteNames[key] << " seq " << seq << " encountered " << length - count << " uncalibrated values.\n";
	
	return sum / count;
}

// After new calibration settings are loaded, we need to find the amount of noise on our measurements, to avoid
// spontaneously triggering events based on random variations.  Noise is a function of a number of factors, including
// external light sources, analog electrical noise in the Piano Bar, EM pickup on the cables, and (on the black keys)
// calibration mismatches between different cycles.

// Make sure this is only called when the audioMutex is unlocked

void PianoBarController::updateNoiseFloors()
{
	int key, j, startPositions[88], avg, alternateNoiseCalc, alternateNoiseCalcDenom;
	
	if(!isRunning_)
	{
		needsNoiseFloorCalibration_ = true;	// Can't do anything until the device has been started!
		return;
	}
	
	// This code isn't exactly a marvel of efficiency, but it only happens once in a while so I don't think
	// it should matter too much.
	
	for(key = 0; key < 88; key++)
	{
		keyNoiseFloor_[key] = 0;
		startPositions[key] = keyHistoryPosition_[key];
	}
	
	// Wait until we get enough samples in each buffer to make the calculations.  
	
	for(key = 0; key < 88; key++)
	{
		// We'll end up waiting on the first couple keys, at which point the rest should have enough data
		// to run the calculations right away
		
		while(keyDistanceFromFeature(key, startPositions[key], keyHistoryPosition_[key]) < 64)
			usleep(100);
	}
	
	pthread_mutex_lock(&audioMutex_);	// Lock this so the samples don't change out from under us!

	// The current calculation is only a short sampling (~100ms) of the key position.  There's another way to
	// calculate noise floor, and let's try both and take the higher of the two.  We can figure that the raw
	// samples might vary by 1 in either direction, so at worst, the noise floor might be the calibrated result
	// of a deviation of 2.  Experimentally, this is a bigger deal on white keys because they have a relatively
	// restricted input range.
	
	for(key = 0; key < 88; key++)
	{
		avg = runningPositionAverage(key, 0, 64);		// Average the last 32 samples
		for(j = 0; j < 64; j++)						// Go back through the samples and find the max deviation
		{
			int thisSample = keyHistory_[key][keyHistoryOffset(key, j)];
			if(abs(thisSample - avg) > keyNoiseFloor_[key])
				keyNoiseFloor_[key] = abs(thisSample - avg);
		}
		
		alternateNoiseCalcDenom = abs(calibrationFullPress_[key][0] - calibrationQuiescent_[key][0]);
		if(alternateNoiseCalcDenom == 0)
			alternateNoiseCalcDenom = 1;
		alternateNoiseCalc = 8192/alternateNoiseCalcDenom;
		if(alternateNoiseCalc > keyNoiseFloor_[key])
			keyNoiseFloor_[key] = alternateNoiseCalc;
		
#ifdef DEBUG_MESSAGES
		cout << "Key " << kNoteNames[key] << " noise floor: " << keyNoiseFloor_[key] << endl;
#endif
	}
	
	// Finally, check the flatness tolerance of each key.  If it's lower than twice the noise floor,
	// push it up.  (It's not specified whether updateNoiseFloors() or loadProcessingParameters() is
	// called first.
	
	updateFlatnessTolerance();

	pthread_mutex_unlock(&audioMutex_);
}

// Given current noise floors, update the flatness tolerance parameter for each key.

void PianoBarController::updateFlatnessTolerance()
{
	// For now, these are hard-coded.  Eventually, they'll go in a loadable file
	
	int baseFlatnessTolerance = (int)(0.005*4096.0);
	
	// A key must deviate by more than twice the noise floor before it's declared active
	
	for(int key = 0; key < 88; key++)
	{
		if(baseFlatnessTolerance > keyNoiseFloor_[key]*2)
			parameterFlatnessTolerance_[key] = baseFlatnessTolerance;
		else
			parameterFlatnessTolerance_[key] = keyNoiseFloor_[key]*2;
	}	
}

#pragma mark State Machine

// In this function we examine the recent key position data to determine the state of each key.
// We'll potentially look at position, velocity (1st diff), and acceleration (2nd diff) to figure this out.

// Allowable state transitions:
//	 Unknown -->	Idle
//   Idle    -->    InitialActivity
//   InitialActivity --> PartialMax, Down, Idle
//   PartialMax -->  Down, Idle
//   Down    -->    Release
//   Release -->    PostRelease, (Idle)
//   PostRelease --> Idle, Restrike

void PianoBarController::updateKeyState(int key)
{
	int flatCount, triggerThreshold;
	int keyState = currentKeyState(key);
	int currentPosition = keyHistoryPosition_[key];
	int currentValue = keyHistory_[key][currentPosition];
	
	if(keyState == kKeyStateDisabledByUser || keyState == kKeyStateDisabledByCalibrator)
		return;
	
	flatCount = updateSumAndFlatness(key);
	if(flatCount < 0)						// Not enough samples yet to determine flatness
		return;
	
	if(keyState == kKeyStateIdle)
	{
		// In the idle state, it's assumed the key remains essentially flat.  Watch for signs of motion
		// and if they are significant enough, transition to the InitialActivity state where more processing
		// is done
		
		if(flatCount == 0)
		{
			int count = 0, pos = currentPosition;
			
			while(count < 30)	// Search backwards for last time key velocity was below a certain threshold.  Set a cap on how far back to search
			{
				if(keyVelocityHistory_[key][pos] == UNCALIBRATED)	// Fill in any missing data
					keyVelocityHistory_[key][pos] = calculateKeyVelocity(key, pos);
				
				if(keyVelocityHistory_[key][pos] < parameterKeyStartMinimumVelocity_)
					break;
				count++;
				pos = (pos - 1 + keyHistoryLength_[key]) % keyHistoryLength_[key];
			}
			
			keyInfo_[key].currentStartPosition = pos;
			keyInfo_[key].currentStartValue = keyInfo_[key].lastKeyPointValue = keyHistory_[key][pos];

			keyInfo_[key].currentMinValue = keyInfo_[key].currentMaxValue = currentValue;
			keyInfo_[key].currentMinPosition = keyInfo_[key].currentMaxPosition = currentPosition;
			
			keyStartValuesPosition_[key] = (keyStartValuesPosition_[key] + 1) % keyStartValuesLength_[key];
			keyStartValues_[key][keyStartValuesPosition_[key]] = currentValue;
			keyInfo_[key].startValuesSum += currentValue;
			if(keyInfo_[key].startValuesSumCurrentLength < keyInfo_[key].startValuesSumMaxLength)
				keyInfo_[key].startValuesSumCurrentLength++;
			else
			{
				int oldStartPos = keyStartValuesPosition_[key] - keyInfo_[key].startValuesSumCurrentLength;
				if(oldStartPos < 0)
					oldStartPos += keyStartValuesLength_[key];
				
				keyInfo_[key].startValuesSum -= keyStartValues_[key][oldStartPos];
			}
			
			keyInfo_[key].recentKeyPoints.clear();  // Initialize the current key points
			keyInfo_[key].recentKeyPoints.push_back((keyPointHistory){currentValue, currentTimeStamp_, kKeyPointTypeStart});
			
			cout << "Key " << kNoteNames[key] << " maxVariation = " << keyInfo_[key].maxVariation << endl << "   ";
			for(int i = 0; i < keyInfo_[key].runningSumCurrentLength; i++)
			{
				cout << keyHistory_[key][keyHistoryOffset(key, i)] << " ";
			}
			cout << endl;
			
#ifdef MIDI_HACK						
//			if(sendMidiMessages_ && oscTransmitter_ != NULL)	// InitialActivity -> Down
//				sendMidiNoteOn(key);
#endif
			changeKeyState(key, kKeyStateInitialActivity);	// Key goes active
		}
	}
	else if(keyState == kKeyStateUnknown)
	{
		if(flatCount > parameterFlatnessIdleLength_ && 
		   (currentValue < parameterGuaranteedIdlePosition_ ||
		   (currentValue*keyInfo_[key].startValuesSumCurrentLength - keyInfo_[key].startValuesSum < parameterIdlePositionOffset_)))
			changeKeyState(key, kKeyStateIdle);
	}
	else	// All active states.  Here we may eventually break out several states
	{
		// Check first if the key has gone idle.  Notice that if key position is significantly BELOW where we expect,
		// it's still allowed to go idle
		
		if(flatCount > parameterFlatnessIdleLength_ && 
		   (currentValue < parameterGuaranteedIdlePosition_ ||
		   (currentValue*keyInfo_[key].startValuesSumCurrentLength - keyInfo_[key].startValuesSum < parameterIdlePositionOffset_)))
		{
			// If we are recording this key for the duration of its activity, it needs to be told when to stop
			if(recordKeyStatus_[key] == kRecordStatusRecordingEvent || recordKeyStatus_[key] == kRecordStatusShouldStartRecording)
			{
				// The number of samples remaining reflects not only the post-record time but also how many samples already
				// waiting to be saved
				
				if(kPianoBarKeyColor[key] == K_W)
				{
					recordKeyRemainingSamples_[key] = recordPostTimeWhite_ + keyDistanceFromFeature(key, recordKeyHistoryIOPosition_[key], keyHistoryPosition_[key]);
					recordKeyCloseFileTimestamp_[key] = currentTimeStamp_ + recordSpacingWhite_;					
				}
				else
				{
					recordKeyRemainingSamples_[key] = recordPostTimeBlack_ + keyDistanceFromFeature(key, recordKeyHistoryIOPosition_[key], keyHistoryPosition_[key]);
					recordKeyCloseFileTimestamp_[key] = currentTimeStamp_ + recordSpacingBlack_;
				}
				recordKeyStatus_[key] = kRecordStatusPostEvent;
				// If this is the last active key, tell the pedals to finish recording
				recordNumberOfActiveKeys_--;
				if(recordNumberOfActiveKeys_ <= 0)
				{
					recordNumberOfActiveKeys_ = 0;		// Just in case...
					for(int pedal = 0; pedal < 3; pedal++)
						recordPedalStatus_[pedal] = kRecordStatusPostEvent;
				}
			}
			
			if(currentKeyState(key) == kKeyStateRelease)	// Make sure key-release features are sent if we skipped the Post-Release state
				sendKeyReleaseFeatures(key, NULL);
//			if(sendMidiMessages_ && oscTransmitter_ != NULL)
//				sendKeyPercussiveMidiOff(key, NULL);
#ifdef MIDI_HACK
//			if(sendMidiMessages_ && oscTransmitter_ != NULL)	// Down -> Release
//			{
//				sendMidiNoteOff(key);
//			}
#endif
			changeKeyState(key, kKeyStateIdle);
		}
		else
		{
			// Look for min and max features
			
			if(currentValue > keyInfo_[key].currentMaxValue)
			{
				keyInfo_[key].currentMaxValue = currentValue;
				keyInfo_[key].currentMaxPosition = currentPosition;
				keyInfo_[key].recentKeyPoints.push_back((keyPointHistory){currentValue, currentTimeStamp_, kKeyPointTypeMax});
				
				if(currentKeyState(key) == kKeyStatePartialMax)
					changeKeyState(key, kKeyStateInitialActivity);	// If we exceed the previous partial maximum, reset so we can trigger another later
//				if(currentValue > 2048 && sendMidiMessages_ && oscTransmitter_ != NULL)
//					sendKeyPercussiveMidiOn(key, NULL);
			}
			if(currentValue < keyInfo_[key].currentMinValue)
			{
				keyInfo_[key].currentMinValue = currentValue;
				keyInfo_[key].currentMinPosition = currentPosition;
				keyInfo_[key].recentKeyPoints.push_back((keyPointHistory){currentValue, currentTimeStamp_, kKeyPointTypeMin});				
			}
			
			if(abs(keyInfo_[key].currentMaxValue - keyInfo_[key].lastKeyPointValue) >= parameterMaxMinValueSpacing_)
			{
				// Implement a sliding threshold that gets lower the farther away from the maximum we get
				triggerThreshold = parameterMaxMinValueSpacing_ / 
					(keyDistanceFromFeature(key, currentPosition, keyInfo_[key].currentMaxPosition) + 1);
				
				// Found maximum?
				if(currentValue < keyInfo_[key].currentMaxValue - triggerThreshold)
				{
					// Here we've detected a maximum value.  What we do with it depends on the current state.
					
					if(recordEvents_)			// Log it to file, if enabled
					{
						pthread_mutex_lock(&eventLogMutex_);
						recordEventsFile_ << currentTimeStamp_ << " " << key << " max ";
						recordEventsFile_ << keyHistoryTimestamps_[key][keyInfo_[key].currentMaxPosition] << " ";
						recordEventsFile_ << keyInfo_[key].currentMaxValue << endl;
						pthread_mutex_unlock(&eventLogMutex_);
					}
					
					keyInfo_[key].lastKeyPointValue = keyInfo_[key].currentMaxValue;
					
					if(keyState == kKeyStateInitialActivity && keyInfo_[key].currentMaxValue > parameterFirstMaxHeight_)
					{
						// First of all, start logging data for this key if it's set to OnActivity.  We didn't log up
						// to now to avoid catching momentary glitches that didn't produce an event.  But retroactively
						// record from before the start position.
						
						if(recordKeyStatus_[key] == kRecordStatusPostEvent)
						{
							// Here, we're waiting to wrap up a recording but we've just become active again so cancel
							// our plans to stop
							
							recordKeyStatus_[key] == kRecordStatusRecordingEvent;
						}
						if(recordKeyStatus_[key] == kRecordStatusOnActivity)
						{
							if(kPianoBarKeyColor[key] == K_W)
								recordKeyHistoryIOPosition_[key] = keyInfo_[key].currentStartPosition - recordPreTimeWhite_;
							else
								recordKeyHistoryIOPosition_[key] = keyInfo_[key].currentStartPosition - recordPreTimeBlack_;
							if(recordKeyHistoryIOPosition_[key] < 0)
								recordKeyHistoryIOPosition_[key] += keyHistoryLength_[key];
							
							recordKeyStatus_[key] = kRecordStatusShouldStartRecording;

							// Start the pedals recording if they weren't previously
							recordNumberOfActiveKeys_++;
							if(recordNumberOfActiveKeys_ == 1)
							{
								for(int pedal = 0; pedal < 3; pedal++)
									recordPedalStatus_[pedal] = kRecordStatusShouldStartRecording;
							}
						}
						
	
						if(keyInfo_[key].currentMaxValue > parameterKeyPressValue_)
						{
							keyInfo_[key].pressValue = keyInfo_[key].currentMaxValue;
							keyInfo_[key].pressPosition = keyInfo_[key].currentMaxPosition;
							
							// Prime the key release information.  These are updated continuously until the key actually releases
							keyInfo_[key].releaseValue = currentValue;
							keyInfo_[key].releasePosition = currentPosition;
							
#ifndef MIDI_HACK
							if(sendMidiMessages_ && oscTransmitter_ != NULL)	// InitialActivity -> Down
								sendMidiNoteOn(key);
#endif
							changeKeyStateWithTimestamp(key, kKeyStateDown, keyHistoryTimestamps_[key][keyInfo_[key].currentMaxPosition]);
							keyScheduledActions_[key].insert(pair<pb_timestamp, key_action>(currentTimeStamp_+POST_PRESS_FEATURE_WAIT_TIME,staticSendKeyDownFeatures));						
							keyScheduledActions_[key].insert(pair<pb_timestamp, key_action>(currentTimeStamp_+POST_PRESS_AFTER_FEATURE_WAIT_TIME,staticSendKeyAfterFeatures));						
							if(sendKeyPressData_)
								keyScheduledActions_[key].insert(pair<pb_timestamp, key_action>(currentTimeStamp_+POST_PRESS_OSC_WAIT_TIME,staticSendKeyDownDetailedData));
							
						}
						else	// Key found a maximum, but it's short of a full press
						{
							sendKeyPartialPressFeatures(key, NULL);	// Send features right away -- do this BEFORE state change
							if(sendKeyPressData_)
								keyScheduledActions_[key].insert(pair<pb_timestamp, key_action>(currentTimeStamp_+POST_PRESS_OSC_WAIT_TIME,staticSendKeyPartialPressDetailedData));						
//							if(sendMidiMessages_ && oscTransmitter_ != NULL)
//								sendKeyPercussiveMidiOn(key, NULL);
						
							changeKeyStateWithTimestamp(key, kKeyStatePartialMax, keyHistoryTimestamps_[key][keyInfo_[key].currentMaxPosition]);
						}
					}
					else if(keyInfo_[key].currentMaxValue > parameterKeyRestrikeMinValue_ && keyState == kKeyStatePostRelease)
					{
						// The beginning of this action will be the last minimum value
						
						keyInfo_[key].currentStartValue = keyInfo_[key].currentMinValue;
						keyInfo_[key].currentStartPosition = keyInfo_[key].currentMinPosition;
						
						if(keyInfo_[key].currentMaxValue > parameterKeyPressValue_)
						{
							keyInfo_[key].pressValue = keyInfo_[key].currentMaxValue;
							keyInfo_[key].pressPosition = keyInfo_[key].currentMaxPosition;
							
							// Prime the key release information.  These are updated continuously until the key actually releases
							keyInfo_[key].releaseValue = currentValue;
							keyInfo_[key].releasePosition = currentPosition;
#ifndef MIDI_HACK							
							// Go back to Down state
							if(sendMidiMessages_ && oscTransmitter_ != NULL)	// PostRelease -> Down
								sendMidiNoteOn(key);
#endif
							changeKeyStateWithTimestamp(key, kKeyStateDown, keyHistoryTimestamps_[key][keyInfo_[key].currentMaxPosition]);
							keyScheduledActions_[key].insert(pair<pb_timestamp, key_action>(currentTimeStamp_+POST_PRESS_FEATURE_WAIT_TIME,staticSendKeyDownFeatures));						
							keyScheduledActions_[key].insert(pair<pb_timestamp, key_action>(currentTimeStamp_+POST_PRESS_AFTER_FEATURE_WAIT_TIME,staticSendKeyAfterFeatures));	
							if(sendKeyPressData_)
								keyScheduledActions_[key].insert(pair<pb_timestamp, key_action>(currentTimeStamp_+POST_PRESS_OSC_WAIT_TIME,staticSendKeyDownDetailedData));						
						}
						else
						{
							sendKeyPartialPressFeatures(key, NULL);	// Send features right away -- BEFORE state change
							if(sendKeyPressData_)
								keyScheduledActions_[key].insert(pair<pb_timestamp, key_action>(currentTimeStamp_+POST_PRESS_OSC_WAIT_TIME,staticSendKeyPartialPressDetailedData));						
							
							changeKeyStateWithTimestamp(key, kKeyStatePartialMax, keyHistoryTimestamps_[key][keyInfo_[key].currentMaxPosition]);
						}
					}
					else if(keyInfo_[key].currentMaxValue > parameterKeyPressValue_ && keyState != kKeyStateDown)
					{
						keyInfo_[key].pressValue = keyInfo_[key].currentMaxValue;
						keyInfo_[key].pressPosition = keyInfo_[key].currentMaxPosition;
						
						// Prime the key release information.  These are updated continuously until the key actually releases
						keyInfo_[key].releaseValue = currentValue;
						keyInfo_[key].releasePosition = currentPosition;	
						
//						if(sendMidiMessages_ && oscTransmitter_ != NULL)	// Pretouch -> Down
//							sendMidiNoteOn(key);
						changeKeyStateWithTimestamp(key, kKeyStateDown, keyHistoryTimestamps_[key][keyInfo_[key].currentMaxPosition]);
						keyScheduledActions_[key].insert(pair<pb_timestamp, key_action>(currentTimeStamp_+POST_PRESS_FEATURE_WAIT_TIME,staticSendKeyDownFeatures));
						keyScheduledActions_[key].insert(pair<pb_timestamp, key_action>(currentTimeStamp_+POST_PRESS_AFTER_FEATURE_WAIT_TIME,staticSendKeyAfterFeatures));	
						if(sendKeyPressData_)
							keyScheduledActions_[key].insert(pair<pb_timestamp, key_action>(currentTimeStamp_+POST_PRESS_OSC_WAIT_TIME,staticSendKeyDownDetailedData));						
					}
					
					// Reinitialize minimum values for the next search
					keyInfo_[key].currentMinValue = currentValue;
					keyInfo_[key].currentMinPosition = currentPosition;					
				}
			}
			// Found minimum?
			if(abs(keyInfo_[key].currentMinValue - keyInfo_[key].lastKeyPointValue) >= parameterMaxMinValueSpacing_)
			{
				// Implement a sliding threshold that gets lower the farther away from the maximum we get
				triggerThreshold = parameterMaxMinValueSpacing_ / 
					(keyDistanceFromFeature(key, currentPosition, keyInfo_[key].currentMinPosition) + 1);
				if(currentValue > keyInfo_[key].currentMinValue + triggerThreshold)
				{
					// Here we've detected a minimum value.  What we do with it depends on the current state.
					if(recordEvents_)			// Log it to file, if enabled
					{
						pthread_mutex_lock(&eventLogMutex_);
						recordEventsFile_ << currentTimeStamp_ << " " << key << " min ";
						recordEventsFile_ << keyHistoryTimestamps_[key][keyInfo_[key].currentMinPosition] << " ";
						recordEventsFile_ << keyInfo_[key].currentMinValue << endl;
						pthread_mutex_unlock(&eventLogMutex_);
					}
					
					keyInfo_[key].lastKeyPointValue = keyInfo_[key].currentMinValue;
					keyInfo_[key].currentMaxValue = currentValue;
					keyInfo_[key].currentMaxPosition = currentPosition;				
					
					if(keyState == kKeyStateRelease && keyInfo_[key].currentMinValue < parameterKeyReleaseFinishValue_)
					{
						// Send features of key release now: we've got all the relevant data of the release itself, and what comes
						// after this will just be bounces before the key settles
						
						sendKeyReleaseFeatures(key, NULL);
//						if(sendMidiMessages_ && oscTransmitter_ != NULL)	// Down -> Release
//						{
//							sendMidiNoteOff(key);
//							sendKeyPercussiveMidiOff(key, NULL);
//						}
						
						// Transition from release to post-release tail (not idle yet but probably just some post-release
						// key vibrations).
						
						changeKeyStateWithTimestamp(key, kKeyStatePostRelease, keyHistoryTimestamps_[key][keyInfo_[key].currentMinPosition]);
					}
				}
			}			
			
			// Look for key release

			if(keyState == kKeyStateDown)
			{
				if(currentValue < parameterKeyReleaseStartValue_)
				{
					// the current values of .releaseValue and .releasePosition tell us where the release started
					
					changeKeyStateWithTimestamp(key, kKeyStateRelease, keyHistoryTimestamps_[key][keyInfo_[key].releasePosition]);
				}
				else if(flatCount != 0)
				{
					// The point of release is defined to be the last point at which the key was flat before the release
					// event occurred.
					
					keyInfo_[key].releaseValue = currentValue;
					keyInfo_[key].releasePosition = currentPosition;	
				}
			}
		}
	}
	
	// Once we've finished updating the state, see whether there are any actions that need to take place.
	
	while(keyScheduledActions_[key].size() > 0)
	{
		// The map sorts actions in increasing order of timestamp, so just take the first one.
		
		multimap<pb_timestamp, key_action>::iterator it = keyScheduledActions_[key].begin();
		
		if((*it).first > currentTimeStamp_)	// Not time to execute this particular action yet
			break;
		
		cout << "Found action for key " << key << " with timestamp " << (*it).first << endl;
		((*it).second)(key, (void *)this);	// TODO: userData
		keyScheduledActions_[key].erase(it);
	}
}

// Examine recent pedal data to determine whether any given pedal should be considered "pressed"
// Pretty simple for now: just see whether the pedal is above or below a given threshold for being
// pressed.

void PianoBarController::updatePedalState(int pedal)
{
	int pedalState = currentPedalState(pedal);
	
	switch(pedalState)
	{
		case kPedalStateUp:
			if(runningPedalAverage(pedal, 0, 3) >= pedalMinPressRising_[pedal])
			{
				changePedalState(pedal, kPedalStateDown);
//				if(sendMidiMessages_ && oscTransmitter_ != NULL)
//				{
//					switch(pedal)
//					{
//						case kPedalDamper:
//							oscTransmitter_->sendMidiMessage(MESSAGE_CONTROL_CHANGE | midiMessageChannel_, CONTROL_DAMPER_PEDAL, 127);
//							break;
//						case kPedalSostenuto:
//							oscTransmitter_->sendMidiMessage(MESSAGE_CONTROL_CHANGE | midiMessageChannel_, CONTROL_SOSTENUTO_PEDAL, 127);
//							break;
//						case kPedalUnaCorda:
//							oscTransmitter_->sendMidiMessage(MESSAGE_CONTROL_CHANGE | midiMessageChannel_, CONTROL_SOFT_PEDAL, 127);
//							break;
//					}
//				}
			}
			break;
		case kPedalStateDown:
		case kPedalStateUnknown:
			if(runningPedalAverage(pedal, 0, 3) < pedalMinPressFalling_[pedal])
			{
				changePedalState(pedal, kPedalStateUp);			
//				if(sendMidiMessages_ && oscTransmitter_ != NULL)
//				{
//					switch(pedal)
//					{
//						case kPedalDamper:
//							oscTransmitter_->sendMidiMessage(MESSAGE_CONTROL_CHANGE | midiMessageChannel_, CONTROL_DAMPER_PEDAL, 0);
//							break;
//						case kPedalSostenuto:
//							oscTransmitter_->sendMidiMessage(MESSAGE_CONTROL_CHANGE | midiMessageChannel_, CONTROL_SOSTENUTO_PEDAL, 0);
//							break;
//						case kPedalUnaCorda:
//							oscTransmitter_->sendMidiMessage(MESSAGE_CONTROL_CHANGE | midiMessageChannel_, CONTROL_SOFT_PEDAL, 0);
//							break;
//					}
//				}			
			}
			break;
		default:
			// Disabled
			break;
	}
}

// Update the flatness counter for any given key, returning its value unless there are not enough samples
// to calculate yet, in which case it returns -1.

int PianoBarController::updateSumAndFlatness(int key)
{
	// Update the current total
	
	keyInfo_[key].runningSum += keyHistory_[key][keyHistoryPosition_[key]];
	if(keyInfo_[key].runningSumCurrentLength < keyInfo_[key].runningSumMaxLength)
	{
		keyInfo_[key].runningSumCurrentLength++;
		return -1;	// Not enough info yet to calculate a flatness
	}
	else
	{
		keyInfo_[key].runningSum -= keyHistory_[key][keyHistoryOffset(key, keyInfo_[key].runningSumMaxLength)];
		
		// Find maximum deviation from average as a metric of flatness.  runningSum is the average value times
		// the number of samples so work in that scale until we're finished.
		
		keyInfo_[key].maxVariation = 0;
		for(int i = 0; i < keyInfo_[key].runningSumMaxLength; i++)
		{
			int thisSample = keyHistory_[key][keyHistoryOffset(key, i)]*keyInfo_[key].runningSumCurrentLength;
			if(abs(thisSample - keyInfo_[key].runningSum) > keyInfo_[key].maxVariation)
				keyInfo_[key].maxVariation = abs(thisSample - keyInfo_[key].runningSum);
		}
		keyInfo_[key].maxVariation /= keyInfo_[key].runningSumCurrentLength;
		
		if(keyInfo_[key].maxVariation <= parameterFlatnessTolerance_[key])
			keyInfo_[key].flatCounter++;
		else
			keyInfo_[key].flatCounter = 0;
	}	
	
	return keyInfo_[key].flatCounter;
}

// Transmit real-time information on the state and position of each key, which will be read by the synth
// process.

void PianoBarController::sendKeyStateMessages()
{
	// TODO: send a big ol' OSC packet with states and positions of every non-idle key
	
	// Construct an OSC blob with a sequence of 4-byte values
	//   Byte 1: Key # (0-87) -- eventually, pedals can be 128-130
	//   Byte 2: State ID -- maintain the same enumerated list in transmitter and receiver
	//   Bytes 3-4: Current position (signed 16-bit value)
	//
	// In general, keys in the Idle state are not included in this list, except for the
	// first time they transition to Idle so the receiving process knows to shut them off.
	// (Should we broadcast idle messages every N ms to avoid stuck notes in the event of dropped packets?)
	
	// HACK: simple weight/intensity mapping for now
	
//	if(!sendMidiMessages_ || oscTransmitter_ == NULL)
//		return;
	
	int key;
	float intensity;
	for(key = 0; key < 88; key++)
	{
#ifndef MIDI_HACK
		if(currentKeyState(key) != kKeyStateDown)
			continue;		
		intensity = ((float)runningPositionAverage(key, 0, 10)/4096.0 - 0.8)*7.0;
#else
		if(currentKeyState(key) == kKeyStateIdle)
			continue;
		intensity = ((float)runningPositionAverage(key, 0, 10)*1.1)/4096.0;
#endif
		if(intensity < 0.0)
			intensity = 0.0;
		if(intensity > 2.0)
			intensity = 2.0;
		
//		oscTransmitter_->sendQualityUpdate((char *)"intensity", midiMessageChannel_, key, intensity);
	}
}

// Look for a pitch-bend action: a key in the Down or Aftertouch position
// whose neighboring white key changed to Pretouch AFTER the key press
// Bend the frequency of the "down" note and give it a richer spectrum,
// while disabling individual sounding for the neighboring note

void PianoBarController::handleMultiKeyPitchBend(set<int> *keysToSkip)
{
	/*int centerKey, whiteBelow, whiteAbove;
	int centerKeyState;
	double pitch;
	
	for(centerKey = 0; centerKey < 88; centerKey++)
	{
		centerKeyState = currentKeyState(centerKey);
		if(centerKeyState != kKeyStateDown && centerKeyState != kKeyStateAftertouch && centerKeyState != kKeyStateAfterVibrato)
			continue;
		
		whiteAbove = whiteKeyAbove(centerKey);
		whiteBelow = whiteKeyBelow(centerKey);
	}*/
}

// Look for a pretouch action one octave above a held note-- use it to sweep up and down the
// harmonic series of the center note

void PianoBarController::handleMultiKeyHarmonicSweep(set<int> *keysToSkip)
{
	/*int centerKey, octaveAbove;
	int centerKeyState;
	double harmonic;
	
	for(centerKey = 0; centerKey < 76; centerKey++)	// This doesn't make sense for the top octave of keys
	{
		centerKeyState = currentKeyState(centerKey);
		if(centerKeyState != kKeyStateDown && centerKeyState != kKeyStateAftertouch && centerKeyState != kKeyStateAfterVibrato)
			continue;
		
		octaveAbove = centerKey + 12;
		if(kPianoBarKeyColor[octaveAbove] != K_W)	// Use a major seventh above in the case of black keys
			octaveAbove--;
	}	*/
}

// Return the current state of a given key

int PianoBarController::currentKeyState(int key)
{
	if(keyStateHistory_[key].size() == 0)
		return kKeyStateUnknown;
	return keyStateHistory_[key].back().state;
}

// Return the current state of a given pedal

int PianoBarController::currentPedalState(int pedal)
{
	if(pedalStateHistory_[pedal].size() == 0)
		return kPedalStateUnknown;
	return pedalStateHistory_[pedal].back().state;
}

// Return the previous state of a given key

int PianoBarController::previousKeyState(int key)
{
	deque<stateHistory>::reverse_iterator rit;
	
	if(keyStateHistory_[key].size() < 2)
		return kKeyStateUnknown;
	
	rit = keyStateHistory_[key].rbegin();
	return (++rit)->state;
}

// Return the previous state of a given pedal

int PianoBarController::previousPedalState(int pedal)
{
	deque<stateHistory>::reverse_iterator rit;
	
	if(pedalStateHistory_[pedal].size() < 2)
		return kPedalStateUnknown;
	
	rit = pedalStateHistory_[pedal].rbegin();
	return (++rit)->state;
}

// Find out how long (in audio frames) the key has been in its current state

pb_timestamp PianoBarController::framesInCurrentKeyState(int key)
{
	pb_timestamp lastTimestamp;
	
	if(keyStateHistory_[key].size() < 2)
		return 0;
	
	deque<stateHistory>::reverse_iterator rit;
	rit = keyStateHistory_[key].rbegin();
	lastTimestamp = (++rit)->timestamp;	
	
	return (currentTimeStamp_ - lastTimestamp);
}

pb_timestamp PianoBarController::framesInCurrentPedalState(int pedal)
{
	pb_timestamp lastTimestamp;
	
	if(pedalStateHistory_[pedal].size() < 2)
		return 0;
	
	deque<stateHistory>::reverse_iterator rit;
	rit = pedalStateHistory_[pedal].rbegin();
	lastTimestamp = (++rit)->timestamp;	
	
	return (currentTimeStamp_ - lastTimestamp);
}

// Find the timestamp of when the key last changed to the indicated state
// Returns 0 if state not found

pb_timestamp PianoBarController::timestampOfKeyStateChange(int key, int state)
{
	deque<stateHistory>::reverse_iterator rit;
	
	rit = keyStateHistory_[key].rbegin();
	
	while(rit != keyStateHistory_[key].rend())
	{
		if(rit->state == state)
			return rit->timestamp;
		rit++;
	}
	
	return 0;
}

pb_timestamp PianoBarController::timestampOfPedalStateChange(int pedal, int state)
{
	deque<stateHistory>::reverse_iterator rit;
	
	rit = pedalStateHistory_[pedal].rbegin();
	
	while(rit != pedalStateHistory_[pedal].rend())
	{
		if(rit->state == state)
			return rit->timestamp;
		rit++;
	}
	
	return 0;
}

#ifdef DEBUG_STATES

const char *kKeyStateNames[kKeyStatesLength] = {"Unknown", "Idle", "Pretouch", "PreVibrato",
	"Tap", "Press", "Down", "Aftertouch", "AfterVibrato", "Release", "PostRelease", "Disabled",
	"Disabled", "InitialActivity", "PartialMax", "Restrike"};

const char *kPedalStateNames[kPedalStatesLength] = {"Unknown", "Up", "Down", "Disabled"};

#endif

void PianoBarController::changeKeyStateWithTimestamp(int key, int newState, pb_timestamp timestamp)
{
#ifdef DEBUG_STATES
	cout << "Key " << kNoteNames[key] << ": " << kKeyStateNames[currentKeyState(key)] << " --> " << kKeyStateNames[newState];
	cout << "        (pos " << lastKeyPosition(key) << ")\n";
#endif
	
	if(recordEvents_)
	{
		pthread_mutex_lock(&eventLogMutex_);
		recordEventsFile_ << currentTimeStamp_ << " " << key << " state " << timestamp << " " << kKeyStateNames[newState] << endl;
		pthread_mutex_unlock(&eventLogMutex_);
	}
	
	stateHistory st;
	//int prevState = currentKeyState(key);
	
	st.state = newState;
	st.timestamp = timestamp;
	
	keyStateHistory_[key].push_back(st);
	while(keyStateHistory_[key].size() > STATE_HISTORY_LENGTH)
		keyStateHistory_[key].pop_front();
}

void PianoBarController::changePedalStateWithTimestamp(int pedal, int newState, pb_timestamp timestamp)
{
#ifdef DEBUG_STATES
	cout << kPedalNames[pedal] << " pedal: " << kPedalStateNames[currentPedalState(pedal)] << " --> " << kPedalStateNames[newState];
	cout << "        (pos " << lastPedalPosition(pedal) << ")\n";
#endif
	
	if(recordEvents_)
	{
		pthread_mutex_lock(&eventLogMutex_);
		recordEventsFile_ << currentTimeStamp_ << " " << pedal << " pstate " << timestamp << " " << kPedalStateNames[newState] << endl;
		pthread_mutex_unlock(&eventLogMutex_);
	}
	
	stateHistory st;
	
	st.state = newState;
	st.timestamp = timestamp;
	
	pedalStateHistory_[pedal].push_back(st);
	while(pedalStateHistory_[pedal].size() > STATE_HISTORY_LENGTH)
		pedalStateHistory_[pedal].pop_front();
}

// Reset all key states to Unknown

void PianoBarController::resetKeyStates()
{
	for(int i = 0; i < 88; i++)
	{
		bool wasUserDisabled = (currentKeyState(i) == kKeyStateDisabledByUser);
		stateHistory sh;
		
		keyStateHistory_[i].clear();
		
		sh.state = (wasUserDisabled ? kKeyStateDisabledByUser : kKeyStateUnknown);
		sh.timestamp = currentTimeStamp_;
		keyStateHistory_[i].push_back(sh);
	}
}

// Reset all pedal states to unknown, unless the pedals were previously disabled by the user,
// in which case they should stay that way.

void PianoBarController::resetPedalStates()
{
	for(int i = 0; i < 3; i++)
	{
		bool wasUserDisabled = (currentPedalState(i) == kPedalStateDisabledByUser);
		
		stateHistory sh;
		
		pedalStateHistory_[i].clear();
		
		sh.state = (wasUserDisabled ? kPedalStateDisabledByUser : kPedalStateUnknown);
		sh.timestamp = currentTimeStamp_;
		pedalStateHistory_[i].push_back(sh);
	}
}

// Load values for the constants we use to process key states

void PianoBarController::loadProcessingParameters()
{
	// Check the flatness tolerance against the noise floor of each key.  At minimum
	// the flatness tolerance should be 2 times the noise floor
	updateFlatnessTolerance();
	
	parameterFlatnessIdleLength_ = 20;
	parameterMaxMinValueSpacing_ = (int)(0.02*4096.0);
	parameterFirstMaxHeight_ = (int)(0.075*4096.0);
	parameterIdlePositionOffset_ = (int)(0.05*4096.0);
	parameterGuaranteedIdlePosition_ = (int)(0.05*4096.0);
	parameterKeyPressValue_ = (int)(0.85*4096.0);
	parameterKeyReleaseStartValue_ = (int)(0.75*4096.0);
	parameterKeyReleaseFinishValue_ = (int)(0.1*4096.0);
	parameterKeyRestrikeMinValue_ = (int)(0.6*4096.0);
	parameterDefaultSumLength_ = 10;
	parameterDefaultStartSumLength_ = 5;
	parameterKeyStartMinimumVelocity_ = VELOCITY_SCALER*0.5;
}

void PianoBarController::resetKeyInfo(int key)
{
	keyInfo_[key].runningSum = 0;
	keyInfo_[key].runningSumMaxLength = parameterDefaultSumLength_;
	keyInfo_[key].runningSumCurrentLength = 0;
	keyInfo_[key].startValuesSum = 0;
	keyInfo_[key].startValuesSumMaxLength = parameterDefaultStartSumLength_;
	keyInfo_[key].startValuesSumCurrentLength = 0;
	keyInfo_[key].maxVariation = 0;
	keyInfo_[key].flatCounter = 0;
	keyInfo_[key].currentStartValue = 0;
	keyInfo_[key].currentStartPosition = 0;
	keyInfo_[key].currentMinValue = 0;
	keyInfo_[key].currentMinPosition = 0;
	keyInfo_[key].currentMaxValue = 0;
	keyInfo_[key].currentMaxPosition = 0;
	keyInfo_[key].lastKeyPointValue = 0;
	keyInfo_[key].pressValue = 0;
	keyInfo_[key].pressPosition = 0;
	keyInfo_[key].releaseValue = 0;
	keyInfo_[key].releasePosition = 0;
	keyInfo_[key].recentKeyPoints.clear();
	keyInfo_[key].sentPercussiveMidiOn = false;
	
	if(keyStartValues_[key] != NULL)
		free(keyStartValues_[key]);
	keyStartValuesLength_[key] = (parameterDefaultStartSumLength_ + 2);
	keyStartValues_[key] = (int *)malloc(keyStartValuesLength_[key]*sizeof(int));	// A couple extra for safety
	keyStartValuesPosition_[key] = 0;
	
	keyScheduledActions_[key].clear();
}

// Return true only at specified intervals to limit the speed of debugging print messages

bool PianoBarController::debugPrintGate(int key, pb_timestamp delay)
{
	if(currentTimeStamp_ > debugLastPrintTimestamp_[key] + delay)
	{
		debugLastPrintTimestamp_[key] = currentTimeStamp_;
		return true;
	}
	return false;
}

#pragma mark OSC and MIDI

// Sends via OSC a collection of features reflecting the current key press from the beginning of its
// activity to its first maximum value
//
// **NOTE** Make sure to change NUM_OSC_FEATURES if new features are added

void PianoBarController::sendKeyDownFeatures(int key, void *userData)
{
	vector<float> features;
	int i, j;
	
//	if(!sendFeatureMessages_ || oscTransmitter_ == NULL)
//		return;
	
	// Feature 0: type
	
	features.push_back(kFeatureTypeDown);	
	
	// Feature 1: key number
	
	features.push_back((float)key);
	
	// Feature 2: start time (low 20 bits), for unique ID purposes (IEEE754 floats have 23 bits available for the base)
	
	features.push_back((float)(keyHistoryTimestamps_[key][keyInfo_[key].currentStartPosition] & 0x000FFFFF));
	
	// Feature 3: key down time (low 20 bits)
	
	features.push_back((float)(keyHistoryTimestamps_[key][keyInfo_[key].pressPosition] & 0x000FFFFF));
	
	// Feature 4: velocity at escapement time
	
	int escapementPosition = keyInfo_[key].currentStartPosition;
	int escapementVelocity = 0;
	
	while(escapementPosition != keyInfo_[key].pressPosition)
	{
		if(keyHistory_[key][escapementPosition] >= KEY_POSITION_ESCAPEMENT)
		{
			// Found the position, now average the 5 points in this neighborhood to find the velocity
			
			escapementVelocity = (keyVelocityHistory_[key][(escapementPosition - 2 + keyHistoryLength_[key])%keyHistoryLength_[key]] +
								  keyVelocityHistory_[key][(escapementPosition - 1 + keyHistoryLength_[key])%keyHistoryLength_[key]] +
								  keyVelocityHistory_[key][escapementPosition] +
								  keyVelocityHistory_[key][(escapementPosition + 1)%keyHistoryLength_[key]] +
								  keyVelocityHistory_[key][(escapementPosition + 2)%keyHistoryLength_[key]])/5;
			break;
		}
		escapementPosition = (escapementPosition + 1) % keyHistoryLength_[key];
	}
	
	//cout << "start " << keyInfo_[key].currentStartPosition << " esc " << escapementPosition << " press " << keyInfo_[key].pressPosition << " vel " << escapementVelocity << endl;
	features.push_back((float)escapementVelocity/(float)VELOCITY_SCALER);
	
	// Features 5-10: percussiveness metrics
	
	// Examine the velocity data looking for an initial maximum
	
	int maxVal = 0, maxPos = keyInfo_[key].currentStartPosition;
	int largestDiff = 0, largestDiffPos = keyInfo_[key].currentStartPosition;
	int maxAtLargestDiff = 0, maxPosAtLargestDiff = keyInfo_[key].currentStartPosition;
	i = keyInfo_[key].currentStartPosition;
	j = 0;
	
	while(i != keyInfo_[key].pressPosition)
	{
		if(keyVelocityHistory_[key][i] == UNCALIBRATED)	// Calculate velocity on an as-needed basis
			keyVelocityHistory_[key][i] = calculateKeyVelocity(key, i);
		if(keyVelocityHistory_[key][i] > maxVal)
		{
			maxVal = keyVelocityHistory_[key][i];
			maxPos = i;
		}
		if(maxVal - keyVelocityHistory_[key][i] > largestDiff)
		{
			largestDiff = maxVal - keyVelocityHistory_[key][i];
			largestDiffPos = i;
			maxAtLargestDiff = maxVal;
			maxPosAtLargestDiff = maxPos;
		}
		
		if(keyHistory_[key][i] >= KEY_POSITION_DAMPER && j >= 3)	// Stop when we get to the part of the stroke where the damper engages
			break;													//  but always consider at least 4 points (for very fast presses)
		
		i = (i + 1) % keyHistoryLength_[key];
		j++;
	}
	
	float preArea = 0, postArea = 0;
	
	for(i = keyInfo_[key].currentStartPosition; i != maxPosAtLargestDiff; i = (i + 1) % keyHistoryLength_[key])
	{
		if(keyVelocityHistory_[key][i] == UNCALIBRATED)
			keyVelocityHistory_[key][i] = calculateKeyVelocity(key, i);		
		preArea += (float)keyVelocityHistory_[key][i] / (float)maxAtLargestDiff;
	}
	for(i = maxPosAtLargestDiff; i != largestDiffPos; i = (i + 1) % keyHistoryLength_[key])
	{
		if(keyVelocityHistory_[key][i] == UNCALIBRATED)
			keyVelocityHistory_[key][i] = calculateKeyVelocity(key, i);		
		postArea += (float)(maxAtLargestDiff - keyVelocityHistory_[key][i]) / (float)largestDiff;
	}

	features.push_back((float)maxAtLargestDiff/(float)VELOCITY_SCALER);
	features.push_back((float)largestDiff/(float)VELOCITY_SCALER);
	features.push_back((float)((maxPosAtLargestDiff - keyInfo_[key].currentStartPosition + keyHistoryLength_[key]) % keyHistoryLength_[key]));
	features.push_back((float)((largestDiffPos - maxPosAtLargestDiff + keyHistoryLength_[key]) % keyHistoryLength_[key]));
	features.push_back(preArea);
	features.push_back(postArea);
	
	// Feature 11: position at key down (usually the deepest point)
	
	features.push_back((float)keyInfo_[key].pressValue / 4096.0);
	
	// Features 12-15: average positions and slopes for two overlapping segments following the key press
	
	float afterVal1 = 0, afterSlope1 = 0, afterVal2 = 0, afterSlope2 = 0;
	int afterPos1begin = (keyInfo_[key].pressPosition + 3) % keyHistoryLength_[key];
	int afterPos1end = (keyInfo_[key].pressPosition + 10) % keyHistoryLength_[key];
	int afterPos2begin = (keyInfo_[key].pressPosition + 7) % keyHistoryLength_[key];
	int afterPos2end = (keyInfo_[key].pressPosition + 14) % keyHistoryLength_[key];	
	
	for(i = afterPos1begin; i != afterPos1end; i = (i + 1) % keyHistoryLength_[key])
	{
		if(keyVelocityHistory_[key][i] == UNCALIBRATED)
			keyVelocityHistory_[key][i] = calculateKeyVelocity(key, i);				
		afterVal1 += (float)keyHistory_[key][i] / (8.0*4096.0);
		afterSlope1 += (float)keyVelocityHistory_[key][i] / ((float)VELOCITY_SCALER*8.0);
	}
	for(i = afterPos2begin; i != afterPos2end; i = (i + 1) % keyHistoryLength_[key])
	{
		if(keyVelocityHistory_[key][i] == UNCALIBRATED)
			keyVelocityHistory_[key][i] = calculateKeyVelocity(key, i);				
		afterVal2 += (float)keyHistory_[key][i] / (8.0*4096.0);
		afterSlope2 += (float)keyVelocityHistory_[key][i] / ((float)VELOCITY_SCALER*8.0);
	}
	
	features.push_back(afterVal1);
	features.push_back(afterSlope1);
	features.push_back(afterVal2);
	features.push_back(afterSlope2);
	
	// Fill up any remaining space with zeros for consistency
	
	while(features.size() < NUM_OSC_FEATURES)
		features.push_back(0.0);	
	
//	pthread_mutex_lock(&eventLogMutex_);
//	if(recordEvents_)
//		oscTransmitter_->sendFeatures(key, features, &recordEventsFile_, currentTimeStamp_);
//	else
//		oscTransmitter_->sendFeatures(key, features, NULL, currentTimeStamp_);
//	pthread_mutex_unlock(&eventLogMutex_);
}

// Send the history of this key from its initial position to its full press value, with a bit of padding on either side.

void PianoBarController::sendKeyDownDetailedData(int key, void *userData)
{
	int startIndex = (keyInfo_[key].currentStartPosition + keyHistoryLength_[key] - 60) % keyHistoryLength_[key];	// where to start sending data
	int length = (keyHistoryPosition_[key] - startIndex + keyHistoryLength_[key]) % keyHistoryLength_[key];
	int keyStartOffset = (keyInfo_[key].currentStartPosition - startIndex + keyHistoryLength_[key]) % keyHistoryLength_[key];	// where within the buffer the "start" occurs
	int keyDownOffset = (keyInfo_[key].pressPosition - startIndex + keyHistoryLength_[key]) % keyHistoryLength_[key];			// where within the buffer the "down" occurs
	
//	if(oscTransmitter_ != NULL)
//		oscTransmitter_->sendKeyDataBlob(key, keyHistory_[key], keyHistoryTimestamps_[key], keyHistoryLength_[key], startIndex, length, keyStartOffset, keyDownOffset);
}

// Send a collection of features for a press that didn't make it all the way to key-down state.  It's possible that a key-down press
// will shortly follow this call, in which case the shared start time will identify them as being the same.

void PianoBarController::sendKeyPartialPressFeatures(int key, void *userData)
{
	vector<float> features;
	int i, j;
	deque<keyPointHistory>::reverse_iterator it;
	
//	if(!sendFeatureMessages_ || oscTransmitter_ == NULL)
//		return;
	
	// This is called every time a maximum is found.  If the key is sharply tapped, it will tend to oscillate several times.  Every time we
	// find a new, higher maximum, a new set of features should be sent, but later oscillations can and should be ignored.  That way the last
	// word is always the biggest excursion of the key.
	
	it = keyInfo_[key].recentKeyPoints.rbegin();
	while(it != keyInfo_[key].recentKeyPoints.rend())
	{
		if(it->value > keyInfo_[key].currentMaxValue)
			return;
		it++;
	}
	
	// Feature 0: type
	
	features.push_back(kFeatureTypePartialPress);
	
	// Feature 1: key number
	
	features.push_back((float)key);
	
	// Feature 2: start time (low 20 bits), for unique ID purposes (IEEE754 floats have 23 bits available for the base)
	
	features.push_back((float)(keyHistoryTimestamps_[key][keyInfo_[key].currentStartPosition] & 0x000FFFFF));
	
	// Feature 3: max position (low 20 bits)
	
	features.push_back((float)(keyHistoryTimestamps_[key][keyInfo_[key].currentMaxPosition] & 0x000FFFFF));
	
	// *** From here on out, the features may be different between Down and PartialPress cases.  But they should be the same length.
	
	// Feature 4: maximum velocity achieved during this period
	
	int maxVelocity = 0;
	
	for(i = keyInfo_[key].currentStartPosition; i != keyHistoryPosition_[key]; i = (i + 1) % keyHistoryLength_[key])
	{
		if(keyVelocityHistory_[key][i] == UNCALIBRATED)	// Calculate velocity on an as-needed basis
			keyVelocityHistory_[key][i] = calculateKeyVelocity(key, i);		
		if(keyVelocityHistory_[key][i] > maxVelocity)
			maxVelocity = keyVelocityHistory_[key][i];
	}
	
	features.push_back((float)maxVelocity/(float)VELOCITY_SCALER);
	
	// Features 5-10: percussiveness metrics, identically to Down case
	
	// Examine the velocity data looking for an initial maximum
	
	int maxVal = 0, maxPos = keyInfo_[key].currentStartPosition;
	int largestDiff = 0, largestDiffPos = keyInfo_[key].currentStartPosition;
	int maxAtLargestDiff = 0, maxPosAtLargestDiff = keyInfo_[key].currentStartPosition;
	i = keyInfo_[key].currentStartPosition;
	j = 0;
	
	while(i != keyHistoryPosition_[key])
	{
		if(keyVelocityHistory_[key][i] == UNCALIBRATED)	// Calculate velocity on an as-needed basis
			keyVelocityHistory_[key][i] = calculateKeyVelocity(key, i);
		if(keyVelocityHistory_[key][i] > maxVal)
		{
			maxVal = keyVelocityHistory_[key][i];
			maxPos = i;
		}
		if(maxVal - keyVelocityHistory_[key][i] > largestDiff)
		{
			largestDiff = maxVal - keyVelocityHistory_[key][i];
			largestDiffPos = i;
			maxAtLargestDiff = maxVal;
			maxPosAtLargestDiff = maxPos;
		}
		
		if(keyHistory_[key][i] >= KEY_POSITION_DAMPER && j >= 3)	// Stop when we get to the part of the stroke where the damper engages
			break;													//  but always consider at least 4 points (for very fast presses)
		
		i = (i + 1) % keyHistoryLength_[key];
		j++;
	}
	
	float preArea = 0, postArea = 0;
	
	for(i = keyInfo_[key].currentStartPosition; i != maxPosAtLargestDiff; i = (i + 1) % keyHistoryLength_[key])
	{
		if(keyVelocityHistory_[key][i] == UNCALIBRATED)
			keyVelocityHistory_[key][i] = calculateKeyVelocity(key, i);		
		preArea += (float)keyVelocityHistory_[key][i] / (float)maxAtLargestDiff;
	}
	for(i = maxPosAtLargestDiff; i != largestDiffPos; i = (i + 1) % keyHistoryLength_[key])
	{
		if(keyVelocityHistory_[key][i] == UNCALIBRATED)
			keyVelocityHistory_[key][i] = calculateKeyVelocity(key, i);		
		postArea += (float)(maxAtLargestDiff - keyVelocityHistory_[key][i]) / (float)largestDiff;
	}
	
	features.push_back((float)maxAtLargestDiff/(float)VELOCITY_SCALER);
	features.push_back((float)largestDiff/(float)VELOCITY_SCALER);
	features.push_back((float)((maxPosAtLargestDiff - keyInfo_[key].currentStartPosition + keyHistoryLength_[key]) % keyHistoryLength_[key]));
	features.push_back((float)((largestDiffPos - maxPosAtLargestDiff + keyHistoryLength_[key]) % keyHistoryLength_[key]));
	features.push_back(preArea);
	features.push_back(postArea);
	
	// Feature 11: deepest point reached so far
	
	features.push_back((float)keyInfo_[key].currentMaxValue / 4096.0);
	
	// Fill up any remaining space with zeros for consistency
	
	while(features.size() < NUM_OSC_FEATURES)
		features.push_back(0.0);
	
//	pthread_mutex_lock(&eventLogMutex_);
//	if(recordEvents_)
//		oscTransmitter_->sendFeatures(key, features, &recordEventsFile_, currentTimeStamp_);
//	else
//		oscTransmitter_->sendFeatures(key, features, NULL, currentTimeStamp_);
//	pthread_mutex_unlock(&eventLogMutex_);	
}

// Send the history of this key from its initial position to the present

void PianoBarController::sendKeyPartialPressDetailedData(int key, void *userData)
{
	int startIndex = (keyInfo_[key].currentStartPosition + keyHistoryLength_[key] - 60) % keyHistoryLength_[key];	// where to start sending data
	int length = (keyHistoryPosition_[key] - startIndex + keyHistoryLength_[key]) % keyHistoryLength_[key];
	int keyStartOffset = (keyInfo_[key].currentStartPosition - startIndex + keyHistoryLength_[key]) % keyHistoryLength_[key];	// where within the buffer the "start" occurs	
//	if(oscTransmitter_ != NULL)
//		oscTransmitter_->sendKeyDataBlob(key, keyHistory_[key], keyHistoryTimestamps_[key], keyHistoryLength_[key], startIndex, length, keyStartOffset, 0);	
}

// Send features for key release

void PianoBarController::sendKeyReleaseFeatures(int key, void *userData)
{
	vector<float> features;
	int i;
	
//	if(!sendFeatureMessages_ || oscTransmitter_ == NULL)
//		return;
	
	
	// Feature 0: type
	
	features.push_back(kFeatureTypeRelease);
	
	// Feature 1: key number
	
	features.push_back((float)key);
	
	// Feature 2: release time (low 20 bits), for unique ID purposes (IEEE754 floats have 23 bits available for the base)
	
	features.push_back((float)(keyHistoryTimestamps_[key][keyInfo_[key].releasePosition] & 0x000FFFFF));
	
	// Feature 3: release time again, just to maintain compatibility with other feature types
	
	features.push_back((float)(keyHistoryTimestamps_[key][keyInfo_[key].releasePosition] & 0x000FFFFF));	
	
	// *** From here on out, the features may be different between cases.  But they should be the same length.
	
	// Feature 4: minimum velocity achieved during release
	
	int minVelocity = 0;
	
	for(i = keyInfo_[key].releasePosition; i != keyHistoryPosition_[key]; i = (i + 1) % keyHistoryLength_[key])
	{
		if(keyVelocityHistory_[key][i] == UNCALIBRATED)	// Calculate velocity on an as-needed basis
			keyVelocityHistory_[key][i] = calculateKeyVelocity(key, i);		
		if(keyVelocityHistory_[key][i] < minVelocity)
			minVelocity = keyVelocityHistory_[key][i];
	}
	
	features.push_back((float)minVelocity/(float)VELOCITY_SCALER);
	
	// Feature 5: position at time release began
	
	features.push_back((float)keyInfo_[key].releaseValue / 4096.0);
	
	
	// Fill up any remaining space with zeros for consistency
	
	while(features.size() < NUM_OSC_FEATURES)
		features.push_back(0.0);
	
//	pthread_mutex_lock(&eventLogMutex_);
//	if(recordEvents_)
//		oscTransmitter_->sendFeatures(key, features, &recordEventsFile_, currentTimeStamp_);
//	else
//		oscTransmitter_->sendFeatures(key, features, NULL, currentTimeStamp_);
//	pthread_mutex_unlock(&eventLogMutex_);	
	
}

// Send an OSC packet describing the state of the key after it's down.  This could be done repeatedly,
// or just once somewhat after press, so that the key press data can get out sooner.

void PianoBarController::sendKeyAfterFeatures(int key, void *userData)
{
	vector<float> features;
	int i, j;
	
//	if(!sendFeatureMessages_ || oscTransmitter_ == NULL)
//		return;
	
	// Feature 0: type
	
	features.push_back(kFeatureTypeAfter);	
	
	// Feature 1: key number
	
	features.push_back((float)key);
	
	// Feature 2: press time (low 20 bits), for unique ID purposes (IEEE754 floats have 23 bits available for the base)
	
	features.push_back((float)(keyHistoryTimestamps_[key][keyInfo_[key].pressPosition] & 0x000FFFFF));
	
	// Feature 3: current time (low 20 bits)
	
	features.push_back((float)(keyHistoryTimestamps_[key][keyHistoryPosition_[key]] & 0x000FFFFF));
	
	// Feature 4: velocity at escapement time
	
	int escapementPosition = keyInfo_[key].currentStartPosition;
	int escapementVelocity = 0;
	
	while(escapementPosition != keyInfo_[key].pressPosition)
	{
		if(keyHistory_[key][escapementPosition] >= KEY_POSITION_ESCAPEMENT)
		{
			// Found the position, now average the 5 points in this neighborhood to find the velocity
			
			escapementVelocity = (keyVelocityHistory_[key][(escapementPosition - 2 + keyHistoryLength_[key])%keyHistoryLength_[key]] +
								  keyVelocityHistory_[key][(escapementPosition - 1 + keyHistoryLength_[key])%keyHistoryLength_[key]] +
								  keyVelocityHistory_[key][escapementPosition] +
								  keyVelocityHistory_[key][(escapementPosition + 1)%keyHistoryLength_[key]] +
								  keyVelocityHistory_[key][(escapementPosition + 2)%keyHistoryLength_[key]])/5;
			break;
		}
		escapementPosition = (escapementPosition + 1) % keyHistoryLength_[key];
	}
	
	//cout << "start " << keyInfo_[key].currentStartPosition << " esc " << escapementPosition << " press " << keyInfo_[key].pressPosition << " vel " << escapementVelocity << endl;
	features.push_back((float)escapementVelocity/(float)VELOCITY_SCALER);
	
	// Features 5-6: Average position and velocity of the most recent 8 points (13.3ms)
	// Features 7-8: Average position and velocity of the most recent 24 points (40ms)
	
	float afterVal1 = 0, afterSlope1 = 0, afterVal2 = 0, afterSlope2 = 0;
	int afterPos1begin = (keyHistoryPosition_[key] - 7 + keyHistoryLength_[key]) % keyHistoryLength_[key];
	int afterPos1end = (keyHistoryPosition_[key] + 1) % keyHistoryLength_[key]; // up to and including current value
	int afterPos2begin = (keyHistoryPosition_[key] - 23 + keyHistoryLength_[key]) % keyHistoryLength_[key];
	int afterPos2end = (keyHistoryPosition_[key] + 1) % keyHistoryLength_[key];
	
	for(i = afterPos1begin; i != afterPos1end; i = (i + 1) % keyHistoryLength_[key])
	{
		if(keyVelocityHistory_[key][i] == UNCALIBRATED)
			keyVelocityHistory_[key][i] = calculateKeyVelocity(key, i);				
		afterVal1 += (float)keyHistory_[key][i] / (8.0*4096.0);
		afterSlope1 += (float)keyVelocityHistory_[key][i] / ((float)VELOCITY_SCALER*8.0);
	}
	for(i = afterPos2begin; i != afterPos2end; i = (i + 1) % keyHistoryLength_[key])
	{
		if(keyVelocityHistory_[key][i] == UNCALIBRATED)
			keyVelocityHistory_[key][i] = calculateKeyVelocity(key, i);				
		afterVal2 += (float)keyHistory_[key][i] / (24.0*4096.0);
		afterSlope2 += (float)keyVelocityHistory_[key][i] / ((float)VELOCITY_SCALER*24.0);
	}
	
	features.push_back(afterVal1);
	features.push_back(afterSlope1);
	features.push_back(afterVal2);
	features.push_back(afterSlope2);
	
	
	
	// Feature 9: shallowest and deepest points since key down
	
	int shallowPosition = keyInfo_[key].pressValue;
	int deepPosition = keyInfo_[key].pressValue;
	
	for(i = keyInfo_[key].pressPosition; i != keyHistoryPosition_[key]; i = (i + 1) % keyHistoryLength_[key])
	{
		if(keyHistory_[key][i] < shallowPosition)
			shallowPosition = keyHistory_[key][i];
		if(keyHistory_[key][i] > deepPosition)
			deepPosition = keyHistory_[key][i];
	}
	
	features.push_back((float)shallowPosition / 4096.0);
	features.push_back((float)deepPosition / 4096.0);
	
	// Feature 11: press position (located in this slot for compatibility with other feature packets)
	
	features.push_back((float)keyInfo_[key].pressValue / 4096.0);
	
	// Fill up any remaining space with zeros for consistency
	
	while(features.size() < NUM_OSC_FEATURES)
		features.push_back(0.0);	
	
//	pthread_mutex_lock(&eventLogMutex_);
//	if(recordEvents_)
//		oscTransmitter_->sendFeatures(key, features, &recordEventsFile_, currentTimeStamp_);
//	else
//		oscTransmitter_->sendFeatures(key, features, NULL, currentTimeStamp_);
//	pthread_mutex_unlock(&eventLogMutex_);
}


// Send a MIDI event whose velocity corresponds to the percussiveness of the key-stroke

void PianoBarController::sendKeyPercussiveMidiOn(int key, void *userData)
{
//	if(oscTransmitter_ == NULL || key < 0 || key > 87)
//		return;	
	if(keyInfo_[key].sentPercussiveMidiOn)	// Send once only
		return;
	
	// Key percussiveness metrics, identical to the feature extraction later (yes, this is inefficient... clean it all up eventually.)
	// Examine the velocity data looking for an initial maximum
	
	int maxVal = 0, maxPos = keyInfo_[key].currentStartPosition;
	int largestDiff = 0, largestDiffPos = keyInfo_[key].currentStartPosition;
	int maxAtLargestDiff = 0, maxPosAtLargestDiff = keyInfo_[key].currentStartPosition;
	int i = keyInfo_[key].currentStartPosition;
	int j = 0;
	
	while(i != keyHistoryPosition_[key])
	{
		if(keyVelocityHistory_[key][i] == UNCALIBRATED)	// Calculate velocity on an as-needed basis
			keyVelocityHistory_[key][i] = calculateKeyVelocity(key, i);
		if(keyVelocityHistory_[key][i] > maxVal)
		{
			maxVal = keyVelocityHistory_[key][i];
			maxPos = i;
		}
		if(maxVal - keyVelocityHistory_[key][i] > largestDiff)
		{
			largestDiff = maxVal - keyVelocityHistory_[key][i];
			largestDiffPos = i;
			maxAtLargestDiff = maxVal;
			maxPosAtLargestDiff = maxPos;
		}
		
		if(keyHistory_[key][i] >= KEY_POSITION_DAMPER && j >= 3)	// Stop when we get to the part of the stroke where the damper engages
			break;													//  but always consider at least 4 points (for very fast presses)
		
		i = (i + 1) % keyHistoryLength_[key];
		j++;
	}
	
	float preArea = 0, postArea = 0;
	
	for(i = keyInfo_[key].currentStartPosition; i != maxPosAtLargestDiff; i = (i + 1) % keyHistoryLength_[key])
	{
		if(keyVelocityHistory_[key][i] == UNCALIBRATED)
			keyVelocityHistory_[key][i] = calculateKeyVelocity(key, i);		
		preArea += (float)keyVelocityHistory_[key][i] / (float)maxAtLargestDiff;
	}
	for(i = maxPosAtLargestDiff; i != largestDiffPos; i = (i + 1) % keyHistoryLength_[key])
	{
		if(keyVelocityHistory_[key][i] == UNCALIBRATED)
			keyVelocityHistory_[key][i] = calculateKeyVelocity(key, i);		
		postArea += (float)(maxAtLargestDiff - keyVelocityHistory_[key][i]) / (float)largestDiff;
	}
	
	// maxAtLargestDiff, largestDiff hold the percussiveness metrics.  Fashion a MIDI velocity out of them
	
	int midiVelocity = 0, midiNote = 51; // ride cymbal
	
	if(maxAtLargestDiff > 12*VELOCITY_SCALER)
	{
		midiVelocity = maxAtLargestDiff*2/VELOCITY_SCALER;
		if(midiVelocity > 127)
			midiVelocity = 127;
		//if(midiVelocity > 110)
		//	midiNote = 59;
	}
	
	cout << "MIDI PERC: " << midiVelocity << endl;
	
//	if(midiVelocity > 0)
//	{
//		// Use ride cymbal sound
//		oscTransmitter_->sendMidiMessage(MESSAGE_NOTEON | 9, midiNote, midiVelocity);
//	}
	
	keyInfo_[key].sentPercussiveMidiOn = true;
}

// Turn off the percussive MIDI note

void PianoBarController::sendKeyPercussiveMidiOff(int key, void *userData)
{
//	if(oscTransmitter_ == NULL || key < 0 || key > 87)
//		return;	
	if(keyInfo_[key].sentPercussiveMidiOn == false)
		return;
	
	cout << "MIDI PERC: OFF\n";
	
//	oscTransmitter_->sendMidiMessage(MESSAGE_NOTEOFF | 9, 51, 0);
	keyInfo_[key].sentPercussiveMidiOn = false;
}

// Send a MIDI note on message for the given key.  Figure out the velocity by looking at its
// history buffer and taking the maximum velocity

void PianoBarController::sendMidiNoteOn(int key)
{
//	if(oscTransmitter_ == NULL || key < 0 || key > 87)
//		return;
	
	// FIXME: this is a pretty crude velocity metric, and doesn't consider white vs. black
	
	int index = keyInfo_[key].currentStartPosition;
	int maxVelocity = 0, currentVelocity, midiVelocity;
	
	while(index != keyHistoryPosition_[key])
	{
		// Look for velocity in 4-sample chunks for slightly better noise performance
		
		currentVelocity = keyHistory_[key][index] - keyHistory_[key][keyHistoryOffset(key, 4)];
		if(currentVelocity > maxVelocity)
			maxVelocity = currentVelocity;
		
		index++;
		if(index >= keyHistoryLength_[key])
			index = 0;
	}
	
	midiVelocity = currentVelocity >> 2;
	if(midiVelocity > 127)
		midiVelocity = 127;
	if(midiVelocity == 0)
		midiVelocity = 1;	// Need at least 1 to be a Note On and not Note Off message
	
//	oscTransmitter_->sendMidiMessage(MESSAGE_NOTEON | midiMessageChannel_, key + 21, midiVelocity);	
}

// Send a MIDI note off message for the given key.  Note off is not velocity-sensitive (though it could be).

void PianoBarController::sendMidiNoteOff(int key)
{
//	if(oscTransmitter_ == NULL)
//		return;
//	
//	oscTransmitter_->sendMidiMessage(MESSAGE_NOTEOFF | midiMessageChannel_, key + 21, 0);
}

#pragma mark Data Logging


// Opens a new file for recording a given key (-1 means record all keys in the same file, raw)
// File name is autogenerated from the current timestamp

int PianoBarController::recordOpenNewKeyFile(int key, pb_timestamp timestamp)
{
	int fd;
	char filename[64];
	
	if(key > 87)		// Huh?
		return -1;
	
	// Construct the path name and open the file
	snprintf(filename, 64, "%s/key_%s_%llu", recordDirectory_, (key < 0 ? "raw" : kNoteNames[key]), timestamp);
	
	//cout << filename << endl;
	
	fd = open(filename, O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	
	openKeyPedalFilePointers_.insert(fd);
	
	return fd;
}

// Opens a new file for recording a given pedal
// File name is autogenerated from the current timestamp

int PianoBarController::recordOpenNewPedalFile(int pedal, pb_timestamp timestamp)
{
	int fd;
	char filename[64];
	
	if(pedal < 0 || pedal > 2)		// Huh?
		return -1;
	
	// Construct the path name and open the file
	snprintf(filename, 64, "%s/pedal_%s_%llu", recordDirectory_, kPedalShortNames[pedal], timestamp);
	
	//cout << filename << endl;
	
	fd = open(filename, O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	
	openKeyPedalFilePointers_.insert(fd);
	
	return fd;
}

// Save the calibration settings to the current directory labeled with a current timestamp
// Returns true on success

bool PianoBarController::recordSaveCalibration(pb_timestamp timestamp)
{
	char filename[64];
	string *filenameStr;
	bool result;
	
	if(calibrationStatus_ != kPianoBarCalibrated && calibrationStatus_ != kPianoBarCalibratedButNoUpdates)
		return false;

	// Construct the path name and open the file
	snprintf(filename, 64, "%s/calibration_%llu.txt", recordDirectory_, timestamp);
	
	filenameStr = new string(filename);
	result = saveCalibrationToFile(*filenameStr);
	delete filenameStr;
	
	return result;
}

// Open a file for writing that holds current events

bool PianoBarController::recordOpenNewEventsFile()
{
	char filename[64];
	
	// Construct the path name and open the file
	snprintf(filename, 64, "%s/events_%llu", recordDirectory_, currentTimeStamp_);
	
	try 
	{
		recordEventsFile_.open(filename, ios_base::out);
	}
	catch(...)
	{
		return false;
	}
	
	recordEvents_ = true;
	return true;
}

// Start a new background thread that watches the buffers and dumps the contents to file periodically.
// We don't want to do file I/O during audio callback time, and it's more efficient to save data in
// larger blocks than to update every sample.  Returns true on success.

bool PianoBarController::launchRecordThread()
{
	recordIOThreadFinishFlag_ = false;
	if(pthread_create(&recordIOThread_, NULL, staticRecordLoop, this) != 0)
		return false;
	return true;
}

// Tells record thread to exit, blocking until it returns

void PianoBarController::stopRecordThread()
{
	recordIOThreadFinishFlag_ = true;	// Tell the thread to finish up
	
	pthread_join(recordIOThread_, NULL);
}

// Stop all currently running recordings and close all files.  It's assumed this happens while
// the audio mutex is locked, and not during the callback thread, so be careful with where this function is called!

void PianoBarController::stopAllRecordings()
{
	int i;
	set<int>::iterator it;
	
	stopRecordThread();
	
	for(it = openKeyPedalFilePointers_.begin(); it != openKeyPedalFilePointers_.end(); it++)
		close(*it);				// This includes the global history file, if open
	openKeyPedalFilePointers_.clear();
	if(recordEventsFile_.is_open())
	{
		pthread_mutex_lock(&eventLogMutex_);
		recordEventsFile_.close();
		pthread_mutex_unlock(&eventLogMutex_);
	}
	recordEvents_ = false;
	recordGlobalHistoryFile_ = -1;
	if(recordGlobalHistory_ != NULL)
	{
		free(recordGlobalHistory_);
		recordGlobalHistory_ = NULL;
		recordGlobalHistoryLength_ = 0;
	}
	recordRawStream_ = false;
	for(i = 0; i < 88; i++)
	{
		recordKeyStatus_[i] = kRecordStatusDisabled;
		recordKeyFilePointer_[i] = -1;
	}
	recordNumberOfActiveKeys_ = 0;
	for(i = 0; i < 3; i++)
	{
		recordPedalStatus_[i] = kRecordStatusDisabled;
		recordPedalFilePointer_[i] = -1;
	}
}

// This function, as the name suggests, is called from the recordLoop thread, and it opens a new file
// to hold data for this individual key.  This will be called when recordKeyStatus_ is set to ShouldStartRecording.
// At the time that assignment is made, recordKeyHistoryPosition will be set to the first sample that needs to be
// saved.

bool PianoBarController::recordLoopBeginKeyRecording(int key)
{
	int fd = recordOpenNewKeyFile(key, keyHistoryTimestamps_[key][recordKeyHistoryIOPosition_[key]]);
	
	if(fd == -1)
	{
		cerr << "ERROR: Unable to open file for recording (key " << key << ", timestamp ";
		cerr << keyHistoryTimestamps_[key][recordKeyHistoryIOPosition_[key]] << ")\n";
		return false;
	}
	
	recordKeyFilePointer_[key] = fd;
	recordKeyHistoryOverflow_[key] = false;
	recordKeyStatus_[key] = kRecordStatusRecordingEvent;
	
	return true;
}

bool PianoBarController::recordLoopBeginPedalRecording(int pedal)
{
	int fd = recordOpenNewPedalFile(pedal, pedalHistoryTimestamps_[pedal][recordPedalHistoryIOPosition_[pedal]]);
	
	if(fd == -1)
	{
		cerr << "ERROR: Unable to open file for recording (pedal " << pedal << ", timestamp ";
		cerr << pedalHistoryTimestamps_[pedal][recordPedalHistoryIOPosition_[pedal]] << ")\n";
		return false;
	}
	
	recordPedalFilePointer_[pedal] = fd;
	recordPedalHistoryOverflow_[pedal] = false;
	recordPedalStatus_[pedal] = kRecordStatusRecordingEvent;
	
	return true;
}

#pragma mark Buffer Allocation

// Allocate back-history buffers for each key according to the given length.  This is called in open()
// and possibly again when the recording settings are changed.

bool PianoBarController::allocateKeyHistoryBuffers(int blackLength, int whiteLength, int pedalLength)
{
	for(int i = 0; i < 88; i++)
	{
		if(kPianoBarKeyColor[i] == K_B)
		{
			keyHistory_[i] = (int *)realloc(keyHistory_[i], blackLength*sizeof(int));
			keyHistoryRaw_[i] = (unsigned short *)realloc(keyHistoryRaw_[i], blackLength*sizeof(short));
			keyHistoryTimestamps_[i] = (pb_timestamp *)realloc(keyHistoryTimestamps_[i], blackLength*sizeof(pb_timestamp));
			keyVelocityHistory_[i] = (int *)realloc(keyVelocityHistory_[i], blackLength*sizeof(int));
			keyHistoryLength_[i] = blackLength;
			
			if(keyHistory_[i] == NULL || keyHistoryRaw_[i] == NULL || keyHistoryTimestamps_[i] == NULL || keyVelocityHistory_[i] == NULL)
				return false;
			bzero(keyHistory_[i], blackLength*sizeof(int));
			bzero(keyHistoryRaw_[i], blackLength*sizeof(short));
		}
		else
		{
			keyHistory_[i] = (int *)realloc(keyHistory_[i], whiteLength*sizeof(int));
			keyHistoryRaw_[i] = (unsigned short *)realloc(keyHistoryRaw_[i], whiteLength*sizeof(short));
			keyHistoryTimestamps_[i] = (pb_timestamp *)realloc(keyHistoryTimestamps_[i], whiteLength*sizeof(pb_timestamp));
			keyVelocityHistory_[i] = (int *)realloc(keyVelocityHistory_[i], whiteLength*sizeof(int));			
			keyHistoryLength_[i] = whiteLength;
			
			if(keyHistory_[i] == NULL || keyHistoryRaw_[i] == NULL || keyHistoryTimestamps_[i] == NULL || keyVelocityHistory_[i] == NULL)
				return false;
			bzero(keyHistory_[i], whiteLength*sizeof(int));
			bzero(keyHistoryRaw_[i], whiteLength*sizeof(short));
		}
		keyHistoryPosition_[i] = 0;
		debugLastPrintTimestamp_[i] = 0;
	}	
	
	for(int i = 0; i < 3; i++)	// Allocate pedal buffers
	{
		pedalHistory_[i] = (int *)realloc(pedalHistory_[i], pedalLength*sizeof(int));
		pedalHistoryRaw_[i] = (short *)realloc(pedalHistoryRaw_[i], pedalLength*sizeof(short));
		pedalHistoryTimestamps_[i] = (pb_timestamp *)realloc(pedalHistoryTimestamps_[i], pedalLength*sizeof(pb_timestamp));
		pedalHistoryLength_[i] = pedalLength;
		
		if(pedalHistory_[i] == NULL || pedalHistoryRaw_[i] == NULL || pedalHistoryTimestamps_[i] == NULL)
			return false;
		bzero(pedalHistory_[i], pedalLength*sizeof(int));
		bzero(pedalHistoryRaw_[i], pedalLength*sizeof(short));
		pedalHistoryPosition_[i] = 0;
	}
	
	keyHistoryLengthBlack_ = blackLength;
	keyHistoryLengthWhite_ = whiteLength;
	
	return true;
}

// Allocate the singular buffer that holds combined data for each key.  This is used for continuous recording.

bool PianoBarController::allocateGlobalHistoryBuffer(int length)
{
	if(recordGlobalHistory_ != NULL)
		free(recordGlobalHistory_);
	
	recordGlobalHistory_ = malloc(length*PIANO_BAR_FRAME_SIZE);
	
	if(recordGlobalHistory_ == NULL)
		return false;
	recordGlobalHistoryLength_ = length;
	recordGlobalHistoryCallbackPosition_ = recordGlobalHistoryIOPosition_ = 0;
	recordGlobalHistoryOverflow_ = false;
	
	return true;
}

// Generates the non-linear warping tables to convert sensor readings to real position
// TODO: make this file-loadable

bool PianoBarController::generateWarpTables()
{
	if(keyWarpBlack_ != NULL)
		free(keyWarpBlack_);
	if(keyWarpWhite_ != NULL)
		free(keyWarpWhite_);
	
	keyWarpBlack_ = (int *)malloc(WARP_TABLE_SIZE*sizeof(int));
	keyWarpWhite_ = (int *)malloc(WARP_TABLE_SIZE*sizeof(int));
	
	if(keyWarpBlack_ == NULL || keyWarpWhite_ == NULL)
		return false;
	
	double whiteKeyCoefficients[3] = {0.0607757110, 0.4473373159, 0.4966096444};	// quadratic fit
	double inputValue, warpedValue;
	int i, offset = -WARP_TABLE_SIZE/2;
	
	for(i = 0; i < WARP_TABLE_SIZE; i++)
	{
		keyWarpBlack_[i] = i + offset;	// No warping for black keys for now
		
		inputValue = (double)(i + offset) / 4096.0;
		warpedValue = (-whiteKeyCoefficients[1] + sqrt(whiteKeyCoefficients[1]*whiteKeyCoefficients[1] - 
													   4*whiteKeyCoefficients[0]*(whiteKeyCoefficients[2] - (1.0/(2.0 - inputValue))))) / (2.0*whiteKeyCoefficients[0]);
		keyWarpWhite_[i] = (int)(warpedValue*4096.0);
	}
	
	return true;
}

// Returns the adjusted value for this sample

int PianoBarController::warpedValue(int key, int rawValue)
{
	int index;
	
	if(kPianoBarKeyColor[key] == K_W)
	{
		if(keyWarpWhite_ == NULL)
			return rawValue;
		index = rawValue + WARP_TABLE_SIZE/2;
		if(index < 0)
			index = 0;
		if(index >= WARP_TABLE_SIZE)
			index = WARP_TABLE_SIZE;
		return keyWarpWhite_[index];
	}
	else
	{
		if(keyWarpBlack_ == NULL)
			return rawValue;
		index = rawValue + WARP_TABLE_SIZE/2;
		if(index < 0)
			index = 0;
		if(index >= WARP_TABLE_SIZE)
			index = WARP_TABLE_SIZE;
		return keyWarpBlack_[index];		
	}
}