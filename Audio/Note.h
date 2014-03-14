//
//  Note.h
//  PianoRoll
//
//  Created by Jeff Gregorio on 10/16/13.
//  Copyright (c) 2013 Jeff Gregorio. All rights reserved.
//

#import <Foundation/Foundation.h>

@interface Note : NSObject

@property (nonatomic, assign) NSInteger noteNumber;
@property (nonatomic, retain) NSMutableArray *positionHistory;
@property (nonatomic, retain) NSString *startTime;

@property (nonatomic, retain) NSMutableDictionary *positionDict;

- (id)initWithNoteNumber:(NSInteger)nn atTime:(NSString*)start;

@end
    
