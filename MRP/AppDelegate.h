//
//  AppDelegate.h
//  MRP
//
//  Created by Jeff Gregorio on 12/12/13.
//  Copyright (c) 2013 Jeff Gregorio. All rights reserved.
//

#import <Cocoa/Cocoa.h>
#import <VVOSC/VVOSC.h>
#import "AudioOutput.h"
#import "portaudio.h"

#include <vector>
#include <string>

@interface AppDelegate : NSObject <NSApplicationDelegate> {
    
    /* Synthesis */
    AudioOutput *kObjectMainAudioOut;
//    Synth *kObjectMainSynth;
    
    /* === Keyboard Scanner === */
    std::vector<std::string> touchkeyDevicePaths_;
    
    /* === OSC === */
    OSCManager *kObjectOscManager;      // Mr. Manager
    OSCInPort  *kObjectOscInputPort;    // Input port
    
    NSDate *startTime;                  // Timestamp of OSC input enable
    
    /* === MIDI === */
    __weak NSButton *kObjectTouchkeysInputEnable;
    __weak NSPopUpButton *kObjectKeyboardScannerDeviceSelect;
    __weak NSPopUpButton *fuckingObject;
}

@property (weak) IBOutlet NSTabView *kObjectMainTabView;

/** === Input/Output Tab === **/
/* Switches */
@property (weak) IBOutlet NSButton *kObjectPianoBarInputEnable;
@property (weak) IBOutlet NSButton *kObjectTouchkeysInputEnable;
@property (weak) IBOutlet NSButton *kObjectOscInputEnable;
@property (weak) IBOutlet NSButton *kObjectOscThruEnable;
@property (weak) IBOutlet NSButton *kObjectMidiInputEnable;
@property (weak) IBOutlet NSButton *kObjectMidiThruEnable;

- (IBAction)enablePianoBarInput:(NSButton *)sender;
- (IBAction)enableOscInput:     (NSButton *)sender;
- (IBAction)enableOscThru:      (NSButton *)sender;
- (IBAction)enableMidiInput:    (NSButton *)sender;
- (IBAction)enableMidiThru:     (NSButton *)sender;

/* Text Fields */
@property (weak) IBOutlet NSTextField *kObjectOscInputPortField;
@property (weak) IBOutlet NSTextField *kObjectOscThruPortField;
@property (weak) IBOutlet NSTextField *kObjectOscThruServerIPField;
@property (weak) IBOutlet NSTextField *kObjectMidiInputChannelField;
@property (weak) IBOutlet NSTextField *kObjectMidiThruChannelField;

- (IBAction)changeOscInputPort:     (NSTextField *)sender;
- (IBAction)changeOscThruPort:      (NSTextField *)sender;
- (IBAction)changeOscThruServerIP:  (NSTextField *)sender;
- (IBAction)changeMidiInputChannel: (NSTextField *)sender;
- (IBAction)changeMidiThruChannel:  (NSTextField *)sender;

/* Pop-Ups */
@property (weak) IBOutlet NSPopUpButton *kObjectMidiInputDeviceSelect;
@property (weak) IBOutlet NSPopUpButton *kObjectMidiThruDeviceSelect;
@property (weak) IBOutlet NSPopUpButton *kObjectTouchkeyDeviceSelect;

- (IBAction)selectMidiInputDevice:  (NSPopUpButton *)sender;
- (IBAction)selectMidiThruDevice:   (NSPopUpButton *)sender;
- (IBAction)selectTouchkeyDevice:   (NSPopUpButton *)sender;

/** === Synth Tab === **/


/** === Log Tab === **/
@property (unsafe_unretained) IBOutlet NSTextView *kObjectLogView;
@property (weak) IBOutlet NSButton *kObjectLogEnable;
@property (weak) IBOutlet NSButton *kObjectLogSaveButton;

- (IBAction)enableLog:(id)sender;
- (IBAction)saveLog:(id)sender;


@end
