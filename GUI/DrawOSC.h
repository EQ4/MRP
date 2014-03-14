//
//  DrawOSC.h
//  PianoRoll
//
//  Created by Jeff Gregorio on 10/16/13.
//  Copyright (c) 2013 Jeff Gregorio. All rights reserved.
//

#import <Foundation/Foundation.h>
#import <Cocoa/Cocoa.h>

@interface DrawOSC : NSView {
    
    NSMutableArray *drawBuffer_;
    NSMutableArray *timestamps_;
    
    BOOL plotColorMap_;
    BOOL shouldClear_;
}

/* To DO: have single setDrawBuffer method that branches depending on the argument type */
- (void)setDrawBuffer:(NSMutableArray *)buff;
- (void)setDrawDict:(NSMutableDictionary *)buff;
- (void)clearPlot;
- (void)drawRect:(NSRect)rect;
- (void)setPlotColormap:(BOOL)mode;

@end

@interface DrawMIDI:DrawOSC {
    
    int midiVelocity_;
}

- (void)setDrawBuffer:(NSInteger *)midiVelocity;
- (void)drawRect:(NSRect)rect;

@end
