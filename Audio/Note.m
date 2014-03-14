//
//  Note.m
//  PianoRoll
//
//  Created by Jeff Gregorio on 10/16/13.
//  Copyright (c) 2013 Jeff Gregorio. All rights reserved.
//

#import "Note.h"

@implementation Note

@synthesize noteNumber, positionHistory, positionDict, startTime;

- (id)initWithNoteNumber:(NSInteger)nn atTime:(NSString *)start {
    
    self = [super init];
    
    if (self) {
        
        noteNumber = nn;
        startTime = start;
        positionHistory = [[NSMutableArray alloc] init];
        positionDict = [[NSMutableDictionary alloc] init];
        
        NSLog(@"New Note with note number %ld began at time %@", noteNumber, startTime);
    }
    
    return self;
}

@end
