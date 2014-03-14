//
//  AppDelegate.m
//  MRP
//
//  Created by Jeff Gregorio on 12/12/13.
//  Copyright (c) 2013 Jeff Gregorio. All rights reserved.
//

#import "AppDelegate.h"

@implementation AppDelegate

#pragma mark Application
/* App initialization */
- (void)applicationDidFinishLaunching:(NSNotification *)aNotification
{
    [self logDirectoryPathChanged:(id)self];
    [self calibrationDirectoryPathChanged:(id)self];
    
    /** == Touchkeys == **/
    keyboardController_ = new PianoKeyboard();
    touchkeyController_ = new TouchkeyDevice(*keyboardController_);
    touchkeyController_->setVerboseLevel(2);
    [self setAvailableTouchkeyDevices];
    
    /** == Keyboard Display == **/
    keyboardDisplay_ = new KeyboardDisplay();
    // Tell the OpenGLViews where to find their display classes
    [openGLview_ setDisplay:keyboardDisplay_];
    [NSTimer scheduledTimerWithTimeInterval:1.0/30.0 target:openGLview_ selector:@selector(updateIfNeeded) userInfo:nil repeats:YES];
    
    keyboardController_->setGUI(keyboardDisplay_);
    
//    /* Set the touchkey callbacks */
//    touchkeyController_->setCentroidCallback(touchkeyCentroidCallback, (__bridge void*) self);
//    touchkeyController_->setAnalogCallback(touchkeyAnalogCallback, (__bridge void*) self);
    
    /* Makes keyboard display with appear by default for an 88-key piano */
    [self setDisplayRangeFromMidiNote:21 toMidiNote:108];
    
    /** == MIDI == **/
    midiInputController_ = new MidiInputController(*keyboardController_);
    [self getAvailableMidiDevices];
    
    /** == GUI == **/
    [kObjectMainWindow setDelegate:self];
    /* Set window's size to its minimum */
    NSSize minWindowSize = [kObjectMainWindow minSize];
    CGRect mainWindowFrame = CGRectMake(200, 200, minWindowSize.width, minWindowSize.height);
    [kObjectMainWindow setFrame:mainWindowFrame display:YES];
    
    /* Default initial selected tabs */
    [kObjectMainTabView selectTabViewItemAtIndex:0];    // I/O tab
    [kObjectAuxTabView selectTabViewItemAtIndex:1];     // Log tab
    
    /* Make sure the I/O scroll view is scrolled to the top */
    NSScrollView *sv = [[[kObjectIOTabItem view] subviews] objectAtIndex:0];
    CGFloat maxYPoint = 290;
    [[sv contentView] scrollToPoint:NSMakePoint(0.0, maxYPoint)];
    [sv reflectScrolledClipView:[sv contentView]];
    
    /** == OSC Setup == **/
    oscTransmitter_ = new OscTransmitter();
    keyboardController_->setOscTransmitter(oscTransmitter_);
    
    kObjectOscManager = [[OSCManager alloc] init];
    [kObjectOscManager setDelegate:self];// Incoming OSC goes to receivedOSCMessage:
    
    /** == Synth == **/
    
    /** == PianoRoll == **/
}


- (void)applicationWillTerminate:(NSNotification *)note {
    
    /* Make sure we properly close the touchkey device and any open log file */
    if (touchkeyController_ != NULL) {
        
        touchkeyController_->closeDevice();
        
        if (touchkeyController_->isLogging())
            touchkeyController_->closeLogFile();
    }
}

#pragma mark Preferences
/* Update the directory where log files are stored */
- (IBAction)logDirectoryPathChanged:(id)sender {
    
    NSString *drField = [kObjectLogDirectoryField stringValue];
    
    /* Replace "~/" with "/Users/<UserName>/" */
    if ([drField hasPrefix:@"~/"]) {
    
        touchkeyLogDirectoryPath_ = [NSString stringWithFormat:@"/Users/%@/%@", NSUserName(), [drField substringFromIndex:2]];
    }
    
    else touchkeyLogDirectoryPath_ = [kObjectLogDirectoryField stringValue];
}

- (IBAction)logDirectoryBrowsePressed:(id)sender {
    
    /* Open a panel to choose directories only */
    NSOpenPanel *openPanel = [NSOpenPanel openPanel];
    [openPanel setAllowsMultipleSelection:NO];
    [openPanel setCanChooseDirectories:YES];
    [openPanel setCanChooseFiles:NO];
    [openPanel setResolvesAliases:YES];
	NSInteger result = [openPanel runModal];
	
    /* Set the chosen path in the text field */
	if (result == NSOKButton) {
		[kObjectLogDirectoryField setStringValue:[[openPanel URL] absoluteString]];
	}
    
    [self logDirectoryPathChanged:(id)self];
}

/* Update the directory where touchkey calibration files are stored */
- (IBAction)calibrationDirectoryPathChanged:(id)sender {
    
    NSString *drField = [kObjectCalibrationDirectoryField stringValue];
    
    /* Replace "~/" with "/Users/<UserName>/" */
    if ([drField hasPrefix:@"~/"]) {
        
        touchkeyCalibrationDirectoryPath_ = [NSString stringWithFormat:@"/Users/%@/%@", NSUserName(), [drField substringFromIndex:2]];
    }
    
    else touchkeyCalibrationDirectoryPath_ = [kObjectCalibrationDirectoryField stringValue];
}

- (IBAction)calibrationDirectoryBrowsePressed:(id)sender {
    
    /* Open a panel to choose directories only */
    NSOpenPanel *openPanel = [NSOpenPanel openPanel];
    [openPanel setAllowsMultipleSelection:NO];
    [openPanel setCanChooseDirectories:YES];
    [openPanel setCanChooseFiles:NO];
    [openPanel setResolvesAliases:YES];
	NSInteger result = [openPanel runModal];
	
    /* Set the chosen path in the text field */
	if (result == NSOKButton) {
		[kObjectCalibrationDirectoryField setStringValue:[[openPanel URL] absoluteString]];
	}
    
    [self calibrationDirectoryPathChanged:(id)self];
}

- (NSSize)windowWillResize:(NSWindow *)sender toSize:(NSSize)frameSize {
    
//    /* Get the scroller position before resizing */
//    NSScrollView *sv = [[[kObjectIOTabItem view] subviews] objectAtIndex:0];
//    CGFloat p_scroller = [[sv verticalScroller] floatValue];
//    CGFloat visibleHeight = sv.contentView.frame.size.height;
//    NSView *eff = sv.contentView.documentView;
//    CGFloat contentHeight = eff.frame.size.height;
//    
//    CGFloat offScreenPixels = contentHeight - visibleHeight;
////    NSLog(@"offScreenPixels = %f", offScreenPixels);
//    
////    CGFloat pixelsAbove = offScreenPixels * p_scroller;
////    NSLog(@"pixelsAbove = %f", pixelsAbove);
    
    return frameSize;
}
/* Keeps any displayed scroll views containing menu items at their current scroll positions */
- (void)windowDidResize:(NSNotification *)notification {
    
//    if ([[kObjectMainTabView selectedTabViewItem] isEqual:kObjectIOTabItem]) {
//        
//        /* Get the scroller position before resizing */
//        NSScrollView *sv = [[[kObjectIOTabItem view] subviews] objectAtIndex:0];
//        CGFloat p_scroller = [[sv verticalScroller] floatValue];
//        CGFloat visibleHeight = sv.contentView.frame.size.height;
//        NSView *eff = sv.contentView.documentView;
//        CGFloat contentHeight = eff.frame.size.height;
//        
//        CGFloat offScreenPixels = contentHeight - visibleHeight;
//        CGFloat pixelsAbove = offScreenPixels * p_scroller;
//        
//        p_scroller = pixelsAbove / offScreenPixels;
//        NSLog(@"p_scroller = %f", p_scroller);
//        /* Set the scroller position */
//        sv.verticalScroller.floatValue = p_scroller;
//        [sv reflectScrolledClipView:[sv contentView]];
//        NSLog(@"p = %f", sv.verticalScroller.floatValue);
//    }
}

#pragma mark Touchkeys Input
- (IBAction)touchkeyEnableInput:(NSButton *)sender {
    
    NSLog(@"%s",__func__);
}

- (IBAction)touchkeySelectDevice:(NSPopUpButton *)sender; {
    
    NSLog(@"Selected device %ld", (long)sender.indexOfSelectedItem);
}

- (IBAction)touchkeyChangeLowestOctave:(id)sender {
    
    int startingOctave = (int)[kObjectTouchkeyLowestOctaveSelect indexOfSelectedItem];
    
    keyboardDisplay_->clearAllTouches();
    touchkeyController_->setLowestMidiNote(12*(startingOctave+1));
}

- (IBAction)touchkeyInputStart:(NSButton *)sender {
    
    if(touchkeyController_->isAutoGathering()) {
		// Currently running.  Stop data collection and close the device.
		
		touchkeyController_->closeDevice();
		[kObjectTouchkeyStartButton setTitle: @"Start"];
		[kObjectTouchkeyStatusField setStringValue: @"Not Running"];
        
        [kObjectTouchkeyCalibrateButton setEnabled: NO];
		[kObjectTouchkeyCalibrationLoadButton setEnabled: NO];
		[kObjectTouchkeyCalibrationSaveButton setEnabled: NO];
		[kObjectTouchkeyCalibrationClearButton setEnabled: NO];
		[kObjectTouchkeyCalibrationClearButton setTitle: @"Clear"];
		[kObjectTouchkeyCalibrateButton setTitle: @"Calibrate"];
		[kObjectTouchkeyCalibrationStatusField setStringValue: @"Not Running"];
	}
	else {
        //printf("starting 1\n");
		// Currently not running.  Open the device and start data collection.
		// Use the current selection from the devices menu to find the device path.
		if([kObjectTouchkeyDeviceSelect numberOfItems] < 1)
			return;
		int selectedIndex = (int)[kObjectTouchkeyDeviceSelect indexOfSelectedItem];
		if(selectedIndex >= touchkeyDevicePaths_.size())
			return;
		
		// Make sure our MIDI mapping is current
		[self touchkeyChangeLowestOctave:self];
        
		// Try opening the device
		if(touchkeyController_->openDevice(touchkeyDevicePaths_[selectedIndex].c_str())) {
			// Device open successful.  First figure out whether this port is actually
			// a valid touchkey device
            
            printf("\t\tDevice = %s", touchkeyDevicePaths_[selectedIndex].c_str());
			
            [kObjectTouchkeyStatusField setStringValue: @"Waiting on device..."];
            
            int count = 0;
            while(1) {
                if(touchkeyController_->checkIfDevicePresent(250))
                    break;
                if(++count >= 20) {	// Try for 5 seconds before giving up.
                    [kObjectTouchkeyStatusField setStringValue: @"Failed to open"];
                    return;
                }
            }
            
            // Set the display to reflect the number of octaves present
            [self setDisplayRangeFromMidiNote:touchkeyController_->lowestKeyPresentMidiNote() toMidiNote:touchkeyController_->highestMidiNote()];
            
			// Set minimum size and scan rate before starting
            touchkeyController_->setKeyMinimumCentroidSize(-1, -1, 32);
            touchkeyController_->setScanInterval(1);
			
			// Now try starting data collection
			if(touchkeyController_->startAutoGathering()) {
				// Success!
                [kObjectTouchkeyStartButton setTitle: @"Stop"];
                [kObjectTouchkeyStatusField setStringValue: @"Running"];
                [kObjectTouchkeyCalibrateButton setEnabled: YES];
                [kObjectTouchkeyCalibrationLoadButton setEnabled: YES];
                [kObjectTouchkeyCalibrationSaveButton setEnabled: NO];
                [kObjectTouchkeyCalibrationClearButton setEnabled: NO];
                [kObjectTouchkeyCalibrationClearButton setTitle: @"Clear"];
                [kObjectTouchkeyCalibrationStatusField setStringValue: @"Not Calibrated"];
                
                
			}
			else {
				touchkeyController_->closeDevice();
				[kObjectTouchkeyStatusField setStringValue: @"Failed to start"];
			}
		}
		else {
			// Failed
			[kObjectTouchkeyStatusField setStringValue: @"Failed to open"];
		}
        
        NSString *pathToDefaultCal = [touchkeyCalibrationDirectoryPath_ stringByAppendingString:@"calibration.xml"];
        
        if (touchkeyController_->calibrationLoadFromFile([pathToDefaultCal cStringUsingEncoding:NSASCIIStringEncoding])) {
            
            [kObjectTouchkeyCalibrationStatusField setStringValue: @"Calibrated"];
            [kObjectTouchkeyCalibrationLoadButton setEnabled: YES];
            [kObjectTouchkeyCalibrationSaveButton setEnabled: YES];
            [kObjectTouchkeyCalibrationClearButton setEnabled: YES];
            [kObjectTouchkeyCalibrationClearButton setTitle: @"Clear"];
        }
        else {
            [kObjectTouchkeyCalibrationStatusField setStringValue: @"Unable to load"];
            [kObjectTouchkeyCalibrationSaveButton setEnabled: NO];
        }
    }
}

/* Begin touchkeyController's calibration sequence */
- (IBAction)touchkeyCalibrateDevice:(NSButton *)sender {
    
    if (!touchkeyController_->isOpen()) // Sanity check
        return;
	if (touchkeyController_->calibrationInProgress()) {
		// If a calibration is currently in progress, finish it
		touchkeyController_->calibrationFinish();
		
		[kObjectTouchkeyCalibrateButton setTitle: @"Recalibrate"];
		[kObjectTouchkeyCalibrationStatusField setStringValue: @"Calibrated"];
		[kObjectTouchkeyCalibrationLoadButton setEnabled: YES];
		[kObjectTouchkeyCalibrationSaveButton setEnabled: YES];
		[kObjectTouchkeyCalibrationClearButton setEnabled: YES];
		[kObjectTouchkeyCalibrationClearButton setTitle: @"Clear"];
	}
	else {
		// Calibration not in progress: start one ( (0,0) means all keys )
		touchkeyController_->calibrationStart(0);
		
		[kObjectTouchkeyCalibrateButton setTitle: @"Finish"];
		[kObjectTouchkeyCalibrationStatusField setStringValue: @"Calibrating"];
		[kObjectTouchkeyCalibrationLoadButton setEnabled: NO];
		[kObjectTouchkeyCalibrationSaveButton setEnabled: NO];
		[kObjectTouchkeyCalibrationClearButton setEnabled: YES];
		[kObjectTouchkeyCalibrationClearButton setTitle: @"Abort"];
	}
}

/* Open a panel to choose a calibration file */
- (IBAction)touchkeyLoadCalibration:(NSButton *)sender {
    
    // Sanity check
    if (!touchkeyController_->isOpen() || touchkeyController_->calibrationInProgress())
        return;
    
	NSOpenPanel *openPanel = [NSOpenPanel openPanel];
	[openPanel setAllowsMultipleSelection: NO];
	[openPanel setCanChooseDirectories: NO];
	NSInteger result = [openPanel runModal];
	
	if (result == NSOKButton) {
		NSString *file = [openPanel filename];
		
		// Try loading the file
		if (touchkeyController_->calibrationLoadFromFile([file cStringUsingEncoding:NSASCIIStringEncoding])) {
			[kObjectTouchkeyCalibrationStatusField setStringValue: @"Calibrated"];
			[kObjectTouchkeyCalibrationLoadButton setEnabled: YES];
			[kObjectTouchkeyCalibrationSaveButton setEnabled: YES];
			[kObjectTouchkeyCalibrationClearButton setEnabled: YES];
			[kObjectTouchkeyCalibrationClearButton setTitle: @"Clear"];
		}
		else {
			[kObjectTouchkeyCalibrationStatusField setStringValue: @"Unable to load"];
			[kObjectTouchkeyCalibrateButton setEnabled: NO];
		}
	}
}

/* Open a panel to save the current calibration to a file */
- (IBAction)touchkeySaveCalibration:(NSButton *)sender {
    
    if (!touchkeyController_->isOpen() || touchkeyController_->calibrationInProgress()
	   || !touchkeyController_->isCalibrated()) // Sanity check
        return;
    
	NSSavePanel *savePanel = [NSSavePanel savePanel];
	[savePanel setAllowedFileTypes: [NSArray arrayWithObject: @"xml"]];
	NSInteger result = [savePanel runModal];
	
	if (result == NSOKButton) {
		NSString *file = [savePanel filename];
		
		// Try saving the file
		if(touchkeyController_->calibrationSaveToFile([file cStringUsingEncoding:NSASCIIStringEncoding])) {
			[kObjectTouchkeyCalibrationStatusField setStringValue: @"Calibrated (saved)"];
		}
		else {
			[kObjectTouchkeyCalibrationStatusField setStringValue: @"Unable to save"];
		}
	}
}

/* Clear the current calibration */
- (IBAction)touchkeyClearCalibration:(NSButton *)sender {
    
    if (!touchkeyController_->isOpen()) // Sanity check
    return;
	if (touchkeyController_->calibrationInProgress()) {
		// Abort the current calibration
		touchkeyController_->calibrationAbort();
		
		[kObjectTouchkeyCalibrateButton setTitle: @"Clear"];
		
		// Now figure out whether there is still a calibration active
		if (touchkeyController_->isCalibrated()) {
			[kObjectTouchkeyCalibrateButton setEnabled: YES];
			[kObjectTouchkeyCalibrateButton setTitle: @"Reccalibrate"];
			[kObjectTouchkeyCalibrationLoadButton setEnabled: YES];
			[kObjectTouchkeyCalibrationSaveButton setEnabled: YES];
			[kObjectTouchkeyCalibrationClearButton setEnabled: YES];
			[kObjectTouchkeyCalibrationStatusField setStringValue: @"Calibrated"];
		}
		else {
			[kObjectTouchkeyCalibrateButton setEnabled: YES];
			[kObjectTouchkeyCalibrateButton setTitle: @"Calibrate"];
			[kObjectTouchkeyCalibrationLoadButton setEnabled: YES];
			[kObjectTouchkeyCalibrationSaveButton setEnabled: NO];
			[kObjectTouchkeyCalibrationClearButton setEnabled: NO];
			[kObjectTouchkeyCalibrationStatusField setStringValue: @"Not Calibrated"];
		}
	}
	else if (touchkeyController_->isCalibrated()) {
		touchkeyController_->calibrationClear();
		
		[kObjectTouchkeyCalibrateButton setEnabled: YES];
		[kObjectTouchkeyCalibrateButton setTitle: @"Calibrate"];
		[kObjectTouchkeyCalibrationLoadButton setEnabled: YES];
		[kObjectTouchkeyCalibrationSaveButton setEnabled: NO];
		[kObjectTouchkeyCalibrationClearButton setEnabled: NO];
		[kObjectTouchkeyCalibrationClearButton setTitle: @"Clear"];
		[kObjectTouchkeyCalibrationStatusField setStringValue: @"Not Calibrated"];
	}
	
	// If not calibrated, don't do anything (shouldn't happen)
}

/* Populate the pop-up menu of available touchkey devices */
- (void)setAvailableTouchkeyDevices
{
	/* Clear all existing items */
	[kObjectTouchkeyDeviceSelect removeAllItems];
	
	NSString *dev = @"/dev";
	NSArray *dirContents = [[NSFileManager defaultManager] contentsOfDirectoryAtPath:dev error:nil];
	NSArray *touchkeyDevices = [dirContents filteredArrayUsingPredicate:
								[NSPredicate predicateWithFormat:@"self BEGINSWITH 'cu.usbmodem'"]];
	
    /* If we don't have any touchkey devices, disable the controls */
	if([touchkeyDevices count] == 0) {
		[kObjectTouchkeyDeviceSelect addItemWithTitle: @"No devices found"];
		[kObjectTouchkeyDeviceSelect            setEnabled: NO];
		[kObjectTouchkeyInputEnable             setEnabled: NO];
		[kObjectTouchkeyLowestOctaveSelect      setEnabled: NO];
		[kObjectTouchkeyStartButton             setEnabled: NO];
		[kObjectTouchkeyCalibrateButton         setEnabled: NO];
		[kObjectTouchkeyCalibrationLoadButton   setEnabled: NO];
        [kObjectTouchkeyCalibrationSaveButton   setEnabled: NO];
        [kObjectTouchkeyCalibrationClearButton  setEnabled: NO];
	}
    /* Otherwise, populate the drop-down menu with available devices */
	else {
		for(int i = 0; i < [touchkeyDevices count]; i++) {
			[kObjectTouchkeyDeviceSelect addItemWithTitle: [touchkeyDevices objectAtIndex: i]];
			touchkeyDevicePaths_.push_back([[dev stringByAppendingPathComponent:[touchkeyDevices objectAtIndex: i]]
                                            cStringUsingEncoding:NSASCIIStringEncoding]);
		}
		[kObjectTouchkeyDeviceSelect selectItemAtIndex:0];
		[kObjectTouchkeyDeviceSelect            setEnabled: YES];
		[kObjectTouchkeyInputEnable             setEnabled: YES];
		[kObjectTouchkeyLowestOctaveSelect      setEnabled: YES];
		[kObjectTouchkeyStartButton             setEnabled: YES];
		[kObjectTouchkeyCalibrateButton         setEnabled: YES];
		[kObjectTouchkeyCalibrationLoadButton   setEnabled: YES];
        [kObjectTouchkeyCalibrationSaveButton   setEnabled: YES];
        [kObjectTouchkeyCalibrationClearButton  setEnabled: YES];
	}
}

//#pragma mark Touchkey Callback
//
//void touchkeyCentroidCallback(timestamp_type timeStamp, int midiNote, KeyTouchFrame frame, void *userData) {
//    
//    AppDelegate *ad = (__bridge AppDelegate *)userData;
//    
//    /* Log the incoming frame data */
//    if (ad->kObjectLogEnable.state == NSOnState) {
//        NSString *txt = [NSString stringWithFormat:@"[t = %3.5f]: %d\n", timeStamp, midiNote];
//        [ad appendToLogQueue:txt];
//    }
//}
//
//#pragma mark MRP Scanner Callback
//
//void touchkeyAnalogCallback(timestamp_type timeStamp, int midiNote, float position, void *userData) {
//    
//    AppDelegate *ad = (__bridge AppDelegate *)userData;
//    
//    /* Log the incoming position data */
//    if (ad->kObjectTouchkeyLogButton.state == NSOnState) {
//        char str[64];
//        ad->touchkeyLogFile << "[t = " << timeStamp << "]: " << midiNote << ", " << position << endl;
//        fprintf(ad->touchkeyLogFile, "[t = %8.5f]: %3d, %8.5f\n", timeStamp, midiNote, position);
//        fprintf(ad->touchkeyLogFile, str);
//        
//        NSString *msg = [NSString stringWithFormat:@"[t = %8.5f]: %3d, %8.5f\n", timeStamp, midiNote, position];
//        [msg writeToFile:ad->touchkeyLogFilePath_ atomically:YES encoding:NSUTF8StringEncoding error:nil];
//        [ad appendToLogQueue:msg];
//        NSLog(@"%@", msg);
//    }
//}

- (void)setDisplayRangeFromMidiNote:(int)lowestMidiNote toMidiNote:(int)highestMidiNote {
    
	NSSize ratio;
	NSRect currentFrame = [openGLview_ frame];
    
    if(highestMidiNote < lowestMidiNote + 12) // Enforce sanity: one octave minimum
        highestMidiNote = lowestMidiNote + 12;
	
    keyboardDisplay_->setKeyboardRange(lowestMidiNote, highestMidiNote);
	ratio.width = keyboardDisplay_->keyboardAspectRatio();
	//ratio.width = [openGLview setKeyboardRangeFrom:lowestMidiNote to:highestMidiNote];
	ratio.height = 1.0;
	
	currentFrame.size.height = currentFrame.size.width / ratio.width;
//	[openGLview_ setFrame: currentFrame display: NO];
//	[openGLview_ setAspectRatio: ratio];
}

#pragma mark OSC Input
/* Start or stop the OSC server thread */
- (IBAction)oscInputEnable:(NSButton *)sender {
    
    /* Get the port number from the text field, set it */
    int pn = (int)[kObjectOscInputPortField integerValue];
    
    if (sender.state == NSOnState) {
        startTime = [[NSDate alloc] init];
        kObjectOscInputPort = [kObjectOscManager createNewInputForPort:pn];
        NSLog(@"Receiving OSC input on port %d", kObjectOscInputPort.port);

    }
    else {
        NSLog(@"Closing OSC input port %d", kObjectOscInputPort.port);
        [kObjectOscManager deleteAllInputs];
    }
}

/* Update the port for incoming OSC messages */
- (IBAction)oscInputPortChange:(NSTextField *)sender {
    
    int pn = (int)[kObjectOscInputPortField integerValue];
    
    /* Change the port number */
    if (kObjectOscInputEnable.state == NSOnState) {
        NSLog(@"Closing OSC input port %d", kObjectOscInputPort.port);
        [kObjectOscInputPort setPort:pn];
        NSLog(@"Receiving OSC on port %d", kObjectOscInputPort.port);
    }
    
    // If OSC input is not enabled, nothing needs to be done
}

/* OSC Callback */
- (void)receivedOSCMessage:(OSCMessage *)msg {
    
    /* Time since we started logging OSC */
    double ts = -[startTime timeIntervalSinceNow];
    
    /* Print this message to the log if enabled */
    [self displayRawOsc:msg atTimestamp:ts NSLog:NO];
    
    if ([[msg address] isEqualToString:@"/mrp/midi"]) {
        NSLog(@"/mrp/midi");
        
        /* Decode message into native C types to pass to the synth */
    }
    
    else if ([[msg address] isEqualToString:@"/mrp/quality/intensity"]) {
        NSLog(@"/mrp/quality/intensity");
        
        /* Activate a key on the keyboard display */
        int midiNote = [[msg.valueArray objectAtIndex:1] intValue];
        float value =  [[msg.valueArray objectAtIndex:2] floatValue];
        keyboardDisplay_->setAnalogValueForKey(midiNote, value);
    }
}

/* Display arbitrary OSC input messages at a given timestamp */
/** Arg1: message to print
 Arg2: timestamp (-1 to omit)
 Arg3: write to NSLog for debugging
 **/
- (void)displayRawOsc:(OSCMessage *)msg atTimestamp:(double)ts NSLog:(BOOL)doLog {
    
    /* Don't bother assembling the string if we're not printing to the log view or the NSLog */
    if (kObjectLogEnable.state == NSOffState && !doLog)
        return;
    
    NSString *txt = [[NSString alloc] init];
    
    /* Add the timestamp first if it exists */
    if (ts > 0) {
        txt = [txt stringByAppendingString:[NSString stringWithFormat:@"[t = %3.5f]: ", ts]];
    }
    
    /* Add the OSC path */
	txt = [txt stringByAppendingString:[msg address]];
    
    /* If there's more than one value, OSCMessage stores them in NSMutableArray 'valueArray' */
    if (msg.valueCount > 1) {
        
        NSMutableArray *vals = [msg valueArray];
        
        /* Append the string representation for all aruments */
        for (int i = 0; i < msg.valueCount; i++) {
            txt = [txt stringByAppendingString:[self stringForValue:[vals objectAtIndex:i]]];
        }
    }
    
    /* Otherwise, OSCMessage stores the only value in member 'value' */
    else {
        /* Append the string representation for this arument */
        txt = [txt stringByAppendingString:[self stringForValue:[msg value]]];
    }
	
    /* Write formatted strings to NSLog */
    if (doLog)  NSLog(@"%@", txt);
    
    /* Write formatted strings to the log view */
    if (kObjectLogEnable.state == NSOnState) {
        txt = [txt stringByAppendingString:@"\n"];
        [self appendToLogQueue:txt];
    }
}

/* Convert an OSCValue type to a string representation */
- (NSString *)stringForValue:(OSCValue *)val {
    
    NSString *txt = [[NSString alloc] init];
    
    NSString *strVal;
    int intVal;
    long long llVal;
    double dubVal;
    float floatVal;
    char charVal;
    
    switch (val.type) {
            
        case OSCValInt:
            intVal = [val intValue];
            txt = [txt stringByAppendingString:[NSString stringWithFormat:@" %d", intVal]];
            break;
            
        case OSCVal64Int:
            llVal = [val longLongValue];
            txt = [txt stringByAppendingString:[NSString stringWithFormat:@" %lld", llVal]];
            break;
            
        case OSCValDouble:
            dubVal = [val doubleValue];
            txt = [txt stringByAppendingString:[NSString stringWithFormat:@" %1.5f", dubVal]];
            break;
            
        case OSCValFloat:
            floatVal = [val floatValue];
            txt = [txt stringByAppendingString:[NSString stringWithFormat:@" %1.5f", floatVal]];
            break;
            
        case OSCValBool:
            if ([val boolValue] == YES) {
                txt = [txt stringByAppendingString:@"T"];
            }
            else
                txt = [txt stringByAppendingString:@"F"];
            break;
            
        case OSCValChar:
            charVal = [val charValue];
            txt = [txt stringByAppendingString:[NSString stringWithFormat:@" %c", charVal]];
            break;
            
        case OSCValString:
            strVal = [val stringValue];
            txt = [txt stringByAppendingString:strVal];
            break;
            
        case OSCValNil:
            txt = [txt stringByAppendingString:@"nil"];
            break;
            
            /* To Do: */
        case OSCValArray:
        case OSCValBlob:
        case OSCValColor:
        case OSCValInfinity:
        case OSCValMIDI:
        case OSCValSMPTE:
        case OSCValTimeTag:
            break;
    }
    
    return txt;
}


#pragma mark MIDI Input
- (IBAction)midiInputEnable:(NSButton *)sender {
    
    NSLog(@"%s",__func__);
}

- (IBAction)midiInputDeviceSelect:(NSPopUpButton *)sender {
    
    NSLog(@"%s",__func__);
}

- (IBAction)midiInputChannelSelect:(NSPopUpButton *)sender {
    
    NSLog(@"%s",__func__);
}

- (void)getAvailableMidiDevices {
    
    int nDevs = 0;
    
    midiDeviceIDs_.clear();
    
    /* Get the list of devices */
    vector<pair<int, string>> devices = midiInputController_->availableMidiDevices();
    vector<pair<int, string>>::iterator it;
    
    /* Populate the pop-up button with the device names */
    for (it = devices.begin(); it != devices.end(); ++it) {
        midiDeviceIDs_.push_back(it->first);
        [kObjectMidiInputDeviceSelect addItemWithTitle:[NSString stringWithCString:it->second.c_str() encoding:NSASCIIStringEncoding]];
        
        nDevs++;
    }
    
    if (nDevs != 0) {
        [kObjectMidiInputDeviceSelect addItemWithTitle:@"Select Device"];
        [kObjectMidiInputDeviceSelect setEnabled:YES];
        [kObjectMidiInputChannelSelect setEnabled:YES];
        [kObjectMidiInputEnable setEnabled:YES];
    }
    else {
        [kObjectMidiInputDeviceSelect addItemWithTitle:@"No Devices Found"];
        [kObjectMidiInputDeviceSelect setEnabled:NO];
        [kObjectMidiInputChannelSelect setEnabled:NO];
        [kObjectMidiInputEnable setEnabled:NO];
    }
}

- (void)midiDeviceChanged {
    
    /* Sanity check */
    if (midiDeviceIDs_.size() == 0)
        return;
    
    /* Get device index from pop-up button */
    int chosenDevice = (int)[kObjectMidiInputDeviceSelect indexOfSelectedItem];
    
    midiInputController_->disableAllPorts();
    
    /* Open port on selected device if one was selected */
    if (chosenDevice != 0)
        midiInputController_->enablePort(midiDeviceIDs_[chosenDevice-1]);
    
    [self midiInputChannelChanged];
}

- (void)midiInputChannelChanged {
    
    int chosenChannel = (int)[kObjectMidiInputChannelSelect indexOfSelectedItem];
    
    midiInputController_->disableAllChanels();
    midiInputController_->enableChannel(chosenChannel);
    
}


#pragma mark OSC Output
- (IBAction)oscOutputEnable:(NSButton *)sender {
    
    bool enabled = ([kObjectOscOutputEnable state] == NSOnState);
    
	[kObjectOscOutputServerIPField setEnabled: enabled];
	[kObjectOscOutputPortField setEnabled: enabled];
	
	if(enabled) {
		[self oscOutputPortChange:(id)self];
	}
	else {
		oscTransmitter_->clearAddresses();
	}
}

/* Update the host/ip address for OSC output */
- (IBAction)oscOutputPortChange:(id)sender {
    
    const char *port = [[kObjectOscOutputPortField stringValue] cStringUsingEncoding:NSASCIIStringEncoding];
    const char *adds = [[kObjectOscOutputServerIPField stringValue] cStringUsingEncoding:NSASCIIStringEncoding];
	
	oscTransmitter_->clearAddresses();
	if(oscTransmitter_->addAddress(adds, port) < 0) {
		// TODO: if adding this address fails, update status appropriately
	}
}

#pragma mark MIDI Output
- (IBAction)midiOutputEnable:(NSButton *)sender {
    
    NSLog(@"%s",__func__);
}

- (IBAction)midiOutputDeviceSelect:(NSPopUpButton *)sender {
    
    NSLog(@"%s",__func__);
}

- (IBAction)midiOutputChannelSelect:(NSPopUpButton *)sender {
    
    NSLog(@"%s",__func__);
}

#pragma mark Log

- (IBAction)logEnable:(NSButton *)sender {
    
    NSLog(@"%s",__func__);
}

/* Write contents of log view to plain text file */
- (IBAction)logSave:(NSButton *)sender {
    
    NSLog(@"%s",__func__);
    
    NSSavePanel *savePanel = [NSSavePanel savePanel];
	[savePanel setAllowedFileTypes: [NSArray arrayWithObject: @"txt"]];
	NSInteger result = [savePanel runModal];
	
	if(result == NSOKButton) {
		NSString *file = [[savePanel URL] path];
        
        NSLog(@"%@", file);
		
        [[kObjectLogView string] writeToFile:file atomically:YES
                                    encoding:NSASCIIStringEncoding error:nil];
	}
}

- (IBAction)logClear:(NSButton *)sender {
    
    NSLog(@"%s",__func__);
    
    [kObjectLogView setString:@""];
}

/* Open/close the touchkey log file */
- (IBAction)touchkeyLogToggle:(NSButton *)sender {
    
    /* Create the log file and tell the touckeyController to start logging */
    if ([kObjectTouchkeyLogButton state] == NSOnState) {
        [kObjectTouchkeyLogButton setTitle:@"Stop"];
        
        NSString *logFileName = [NSString stringWithFormat:@"%@.txt", [kObjectTouchkeyLogFileField stringValue]];
        NSString *logDirPath = [touchkeyLogDirectoryPath_ stringByAppendingString:@"Touchkeys/"];
        
        touchkeyController_->createLogFile([logFileName cStringUsingEncoding:NSASCIIStringEncoding], [logDirPath cStringUsingEncoding:NSASCIIStringEncoding]);
        
        touchkeyController_->startLogging();
    }
    
    /* Tell the touchkey controller to stop logging and close the file */
    else if ([kObjectTouchkeyLogButton state] == NSOffState) {
        [kObjectTouchkeyLogButton setTitle:@"Start"];
        touchkeyController_->stopLogging();
        touchkeyController_->closeLogFile();
    }
}

#pragma mark Utility
/* Add text to the log view and scroll */
- (void)appendToLogQueue:(NSString *)txt {
    
    dispatch_async(dispatch_get_main_queue(), ^{
        
        NSDictionary *attributes = [NSDictionary dictionaryWithObject:[NSColor greenColor] forKey:NSForegroundColorAttributeName];
        
        NSAttributedString* attr = [[NSAttributedString alloc] initWithString:txt attributes:attributes];
        
        [[kObjectLogView textStorage] appendAttributedString:attr];
        [kObjectLogView scrollRangeToVisible:NSMakeRange([[kObjectLogView string] length], 0)];
    });
}

/* Handle incoming log messages (redirected from stdout) */
- (void)handleNotification:(NSNotification *)notification
{
    NSDictionary *stringAttributes = [NSDictionary dictionaryWithObject:[NSColor greenColor] forKey:NSForegroundColorAttributeName];
    
    NSAttributedString *str = [[NSAttributedString alloc] initWithString:[[NSString alloc] initWithData:[[notification userInfo] objectForKey: NSFileHandleNotificationDataItem]encoding: NSASCIIStringEncoding] attributes: stringAttributes];
    
	[[kObjectLogView textStorage] appendAttributedString:str];
    [kObjectLogView scrollRangeToVisible:NSMakeRange([[kObjectLogView string] length], 0)];
}

@end













