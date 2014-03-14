//
//  AudioOutput.m
//  MRP
//
//  Created by Jeff Gregorio on 12/16/13.
//  Copyright (c) 2013 Jeff Gregorio. All rights reserved.
//

#import "AudioOutput.h"

@implementation AudioOutput

//@synthesize delegate;

- (id)init {
    
    self = [super init];
    
//    OSStatus status;
    
//    status = NewAUGraph(kObjectMainGraph);
    
    
    [self listAudioComponents];
    
    
    
    return self;
}

- (void)listAudioComponents {
    
//    AudioComponentDescription desc = (AudioComponentDescription) {.componentType = "aumu",                                                                                                                          .componentSubType = NULL,
//                                                                  .componentManufacturer = NULL,
//                                                                  .componentFlags = 0,
//                                                                  .componentFlagsMask = 0};
    
//    AudioComponentDescription desc = (AudioComponentDescription) {'aumu', 'samp', 'appl', 0, 0};
//    UInt32 count = AudioComponentCount(&desc);
//    
//    AudioComponent audComp = NULL;
//    
//    
//    AudioComponentFindNext(audComp, &desc);
//    
//    CFStringRef name;
//    
//    AudioComponentCopyName(audComp, &name);
//    
//    NSLog(@"count = %d", count);
//    
//    NSLog(@"name = %@", name);
    
}


@end
