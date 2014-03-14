//
//  AppDelegate.h
//  MRP
//
//  Created by Jeff Gregorio on 12/12/13.
//  Copyright (c) 2013 Jeff Gregorio. All rights reserved.
//

#import <Cocoa/Cocoa.h>
#import <VVOSC/VVOSC.h>
//#import <NSPathUtilities.h>

#import "Note.h"
#import "DrawOSC.h" // Remove reference when PianoRollView is finished
#import "PianoRollView.h"

#import	"CustomOpenGLView.h"

#include <vector>
#include <string>

#undef check    // Reserved word in Obj C that conflicts with boost libraries
#include "TouchkeyDevice.h"
#include "MidiInputController.h"
#include "Osc.h"

@interface AppDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate> {
    
    /* === Preferences Menu === */
    IBOutlet NSWindow    *kObjectPreferencesWindow;
    IBOutlet NSTextField *kObjectLogDirectoryField;
    IBOutlet NSButton    *kObjectLogDirectoryBrowseButton;
    IBOutlet NSTextField *kObjectCalibrationDirectoryField;
    IBOutlet NSButton    *kObjectCalibrationDirectoryBrowseButton;
    NSString *touchkeyCalibrationDirectoryPath_;
    NSString *touchkeyLogDirectoryPath_;
    
    /* === Main Window === */
    IBOutlet NSWindow  *kObjectMainWindow;
    IBOutlet NSTabView *kObjectMainTabView;
    IBOutlet NSTabView *kObjectAuxTabView;
    IBOutlet NSTabViewItem *kObjectIOTabItem;
    
    /* === Touchkeys I/O Menu Items === */
    IBOutlet NSButton      *kObjectTouchkeyInputEnable;
    IBOutlet NSPopUpButton *kObjectTouchkeyDeviceSelect;
    IBOutlet NSPopUpButton *kObjectTouchkeyLowestOctaveSelect;
    IBOutlet NSButton      *kObjectTouchkeyStartButton;
    IBOutlet NSButton      *kObjectTouchkeyCalibrateButton;
    IBOutlet NSButton      *kObjectTouchkeyCalibrationLoadButton;
    IBOutlet NSButton      *kObjectTouchkeyCalibrationSaveButton;
    IBOutlet NSButton      *kObjectTouchkeyCalibrationClearButton;
    IBOutlet NSTextField   *kObjectTouchkeyCalibrationStatusField;
    IBOutlet NSTextField   *kObjectTouchkeyStatusField;
    
    IBOutlet CustomOpenGLView *openGLview_;     // OpenGL wrapper for keyboard display
    
    std::vector<std::string> touchkeyDevicePaths_;
    TouchkeyDevice  *touchkeyController_;
    PianoKeyboard   *keyboardController_;
    KeyboardDisplay *keyboardDisplay_;
    
    /* === OSC I/O Menu Items === */
    IBOutlet NSButton    *kObjectOscInputEnable;
    IBOutlet NSTextField *kObjectOscInputPortField;
    IBOutlet NSButton    *kObjectOscOutputEnable;
    IBOutlet NSTextField *kObjectOscOutputPortField;
    IBOutlet NSTextField *kObjectOscOutputServerIPField;
    OscTransmitter *oscTransmitter_;
    
    
    /* Replace with c++ oscController class */
    OSCManager *kObjectOscManager;      // Mr. Manager
    OSCInPort  *kObjectOscInputPort;    // Input port
    NSDate *startTime;                  // Timestamp of OSC input enable
    
    /* === MIDI I/O Menu Items === */
    IBOutlet NSButton      *kObjectMidiInputEnable;
    IBOutlet NSPopUpButton *kObjectMidiInputDeviceSelect;
    IBOutlet NSPopUpButton *kObjectMidiInputChannelSelect;
    IBOutlet NSButton      *kObjectMidiOutputEnable;
    IBOutlet NSPopUpButton *kObjectMidiOutputDeviceSelect;
    IBOutlet NSPopUpButton *kObjectMidiOutputChannelSelect;
    
    vector<int> midiDeviceIDs_;
    MidiInputController *midiInputController_;
    
    /* === Logging Tab View Items === */
    IBOutlet NSTabViewItem *kObjectLogTabItem;
    IBOutlet NSTextView *kObjectLogView;
    IBOutlet NSButton *kObjectLogEnable;
    IBOutlet NSButton *kObjectLogSaveButton;
    IBOutlet NSButton *kObjectLogClearButton;
    
    /* OSC Logging */
    NSPipe *logPipe;
    NSFileHandle *logPipeReadHandle;
    NSDictionary *logStringAttributes;
    
    /* Touchkey raw data logging */
    IBOutlet NSButton    *kObjectTouchkeyLogButton;
    IBOutlet NSTextField *kObjectTouchkeyLogFileField;
}

/* Preferences Menu Item Callbacks */
- (IBAction)logDirectoryPathChanged:            (id)sender;
- (IBAction)logDirectoryBrowsePressed:          (id)sender;
- (IBAction)calibrationDirectoryPathChanged:    (id)sender;
- (IBAction)calibrationDirectoryBrowsePressed:  (id)sender;

/* Touchkey I/O Menu Item Callbacks */
- (IBAction)touchkeyEnableInput:        (NSButton       *)sender;
- (IBAction)touchkeySelectDevice:       (NSPopUpButton  *)sender;
- (IBAction)touchkeyChangeLowestOctave: (id)sender; // Should be setable via MIDI
- (IBAction)touchkeyInputStart:         (NSButton *)sender;
- (IBAction)touchkeyCalibrateDevice:    (NSButton *)sender;
- (IBAction)touchkeyLoadCalibration:    (NSButton *)sender;
- (IBAction)touchkeySaveCalibration:    (NSButton *)sender;
- (IBAction)touchkeyClearCalibration:   (NSButton *)sender;

/* OSC I/O Menu Item Callbacks */
- (IBAction)oscInputEnable:     (NSButton       *)sender;
- (IBAction)oscInputPortChange: (NSTextField    *)sender;
- (IBAction)oscOutputEnable:    (NSButton       *)sender;
- (IBAction)oscOutputPortChange:(id)sender;

/* MIDI I/O Menu Item Callbacks */
- (IBAction)midiInputEnable:        (NSButton       *)sender;
- (IBAction)midiInputDeviceSelect:  (NSPopUpButton  *)sender;
- (IBAction)midiInputChannelSelect: (NSPopUpButton  *)sender;
- (IBAction)midiOutputEnable:       (NSButton       *)sender;
- (IBAction)midiOutputDeviceSelect: (NSPopUpButton  *)sender;
- (IBAction)midiOutputChannelSelect:(NSPopUpButton  *)sender;

/** === Log Tab Item Callbacks === **/
- (IBAction)logEnable:  (NSButton *)sender;
- (IBAction)logSave:    (NSButton *)sender;
- (IBAction)logClear:   (NSButton *)sender;
- (IBAction)touchkeyLogToggle:(NSButton *)sender;
// Append to log view methods
- (void)appendToLogQueue:  (NSString *)str;
- (void)handleNotification:(NSNotification *)notification;

@end
