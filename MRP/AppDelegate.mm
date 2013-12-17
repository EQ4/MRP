//
//  AppDelegate.m
//  MRP
//
//  Created by Jeff Gregorio on 12/12/13.
//  Copyright (c) 2013 Jeff Gregorio. All rights reserved.
//

#import "AppDelegate.h"

@implementation AppDelegate

@synthesize kObjectTouchkeysInputEnable;

@synthesize kObjectMainTabView;
@synthesize kObjectPianoBarInputEnable;
@synthesize kObjectOscInputEnable;
@synthesize kObjectOscThruEnable;
@synthesize kObjectMidiInputEnable;
@synthesize kObjectMidiThruEnable;
@synthesize kObjectOscInputPortField;
@synthesize kObjectOscThruPortField;
@synthesize kObjectOscThruServerIPField;
@synthesize kObjectMidiInputChannelField;
@synthesize kObjectMidiThruChannelField;
@synthesize kObjectMidiInputDeviceSelect;
@synthesize kObjectMidiThruDeviceSelect;
@synthesize kObjectTouchkeyDeviceSelect;
@synthesize kObjectLogView;
@synthesize kObjectLogEnable;
@synthesize kObjectLogSaveButton;

/* App initialization */
- (void)applicationDidFinishLaunching:(NSNotification *)aNotification
{
    /** == MRP Keyboard Sensor == **/
    /* Query audio devices to add to the drop-down menu */
//    [self getKeyboardScannerDevices];
    [self setAvailableTouchkeyDevices];
    
    /** == OSC Setup == **/
    kObjectOscManager = [[OSCManager alloc] init];
    [kObjectOscManager setDelegate:self];// Incoming OSC goes to receivedOSCMessage:
    
    /** == Synth == **/
    kObjectMainAudioOut = [[AudioOutput alloc] init];
    
    /** == GUI == **/
    /* Default initial selected tab */
    [kObjectMainTabView selectTabViewItemAtIndex:0];
}

- (IBAction)enablePianoBarInput:(NSButton *)sender {
    
    NSLog(@"%s",__func__);
}

/* Start or stop the OSC server thread */
- (IBAction)enableOscInput:(NSButton *)sender {
    
    /* Get the port number from the text field, set it */
    int pn = (int)[kObjectOscInputPortField integerValue];
    
    if (sender.state == NSOnState) {
        kObjectOscInputPort = [kObjectOscManager createNewInputForPort:pn];
        NSLog(@"Receiving OSC input on port %d", kObjectOscInputPort.port);

    }
    else {
        NSLog(@"Closing OSC input port %d", kObjectOscInputPort.port);
        [kObjectOscManager deleteAllInputs];
    }
}
- (IBAction)enableOscThru:(NSButton *)sender {
    
    NSLog(@"%s",__func__);
}
- (IBAction)enableMidiInput:(NSButton *)sender {
    
    NSLog(@"%s",__func__);
}
- (IBAction)enableMidiThru:(NSButton *)sender {
    
    NSLog(@"%s",__func__);
}
- (IBAction)changeOscInputPort:(NSTextField *)sender {
    
    int pn = (int)[kObjectOscInputPortField integerValue];
    
    /* Change the port number */
    if (kObjectOscInputEnable.state == NSOnState) {
        NSLog(@"Closing OSC input port %d", kObjectOscInputPort.port);
        [kObjectOscInputPort setPort:pn];
        NSLog(@"Receiving OSC on port %d", kObjectOscInputPort.port);
    }
    
    // If OSC input is not enabled, nothing needs to be done
}
- (IBAction)changeOscThruPort:(NSTextField *)sender {
    
    NSLog(@"%s",__func__);
}
- (IBAction)changeOscThruServerIP:(NSTextField *)sender {
    
    NSLog(@"%s",__func__);
}
- (IBAction)changeMidiInputChannel:(NSTextField *)sender {
    
    NSLog(@"%s",__func__);
}
- (IBAction)changeMidiThruChannel:(NSTextField *)sender {
    
    NSLog(@"%s",__func__);
}
- (IBAction)selectMidiInputDevice:(NSPopUpButton *)sender {
    
    NSLog(@"%s",__func__);
}
- (IBAction)selectMidiThruDevice:(NSPopUpButton *)sender {
    
    NSLog(@"%s",__func__);
}

- (IBAction)selectKeyboardScannerDevice:(NSPopUpButton *)sender {
    
    NSLog(@"Selected device %ld", (long)sender.indexOfSelectedItem);
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
        [self logtext:txt];
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

/* Add text to the log view and scroll */
- (void)logtext:(NSString *)txt {
    
    dispatch_async(dispatch_get_main_queue(), ^{
        
        NSDictionary *attributes = [NSDictionary dictionaryWithObject:[NSColor greenColor] forKey:NSForegroundColorAttributeName];
        
        NSAttributedString* attr = [[NSAttributedString alloc] initWithString:txt attributes:attributes];
        
        [[kObjectLogView textStorage] appendAttributedString:attr];
        [kObjectLogView scrollRangeToVisible:NSMakeRange([[kObjectLogView string] length], 0)];
    });
}


- (IBAction)enableLog:(id)sender {
    
    NSLog(@"%s",__func__);
}

/* Write contents of log view to plain text file */
- (IBAction)saveLog:(id)sender {
    
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

- (void)setAvailableTouchkeyDevices
{
	// The location of the touchkey device is platform-dependent anyway, so we'll put the directory
	// listing code here rather than in TouchkeyDevice.cpp
	
	// Clear all existing items
	[kObjectTouchkeyDeviceSelect removeAllItems];
	
	NSString *dev = @"/dev";
	NSArray *dirContents = [[NSFileManager defaultManager] contentsOfDirectoryAtPath:dev error:nil];
	NSArray *touchkeyDevices = [dirContents filteredArrayUsingPredicate:
								[NSPredicate predicateWithFormat:@"self BEGINSWITH 'cu.usbmodem'"]];
	
	if([touchkeyDevices count] == 0) {
		[kObjectTouchkeyDeviceSelect addItemWithTitle: @"No devices found"];
		[kObjectTouchkeyDeviceSelect setEnabled: NO];
//		[touchkeyScanRateButton setEnabled: NO];
//		[touchkeySensitivityField setEnabled: NO];
//		[touchkeyMinimumSizeField setEnabled: NO];
//		[touchkeyStartStopButton setEnabled: NO];
//		[touchkeyOctaveButton setEnabled: NO];
	}
	else {
		for(int i = 0; i < [touchkeyDevices count]; i++) {
			[kObjectTouchkeyDeviceSelect addItemWithTitle: [touchkeyDevices objectAtIndex: i]];
			touchkeyDevicePaths_.push_back([[dev stringByAppendingPathComponent:[touchkeyDevices objectAtIndex: i]]
										   cStringUsingEncoding:NSASCIIStringEncoding]);
		}
		[kObjectTouchkeyDeviceSelect selectItemAtIndex:0];
		[kObjectTouchkeyDeviceSelect setEnabled: YES];
//		[touchkeyScanRateButton setEnabled: YES];
//		[touchkeySensitivityField setEnabled: YES];
//		[touchkeyMinimumSizeField setEnabled: YES];
//		[touchkeyStartStopButton setEnabled: YES];
//		[touchkeyOctaveButton setEnabled: YES];
	}
}

- (void)getAudioDevices {
    
    const PaDeviceInfo *devInfo;
    
    Pa_Initialize();
    
    int numDevices = Pa_GetDeviceCount();
    
    for (int i = 0; i < numDevices; i++) {
        
        devInfo = Pa_GetDeviceInfo(i);
        
        NSLog(@"Device %d: %s (%s)", i, devInfo->name, Pa_GetHostApiInfo(devInfo->hostApi)->name);
        
        NSLog(@"\tmax inputs: %d, max outputs: %d", devInfo->maxInputChannels, devInfo->maxOutputChannels);
        
        [kObjectKeyboardScannerDeviceSelect addItemWithTitle:[NSString stringWithFormat:@"%s", devInfo->name]];
    }
    
    Pa_Terminate();
}

@end













