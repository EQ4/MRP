//
//  DrawOSC.m
//  PianoRoll
//
//  Created by Jeff Gregorio on 10/16/13.
//  Copyright (c) 2013 Jeff Gregorio. All rights reserved.
//

#import "DrawOSC.h"

@implementation DrawOSC

- (id)initWithFrame:(NSRect)frameRect {
    
    if ((self = [super initWithFrame:frameRect]) == nil) {
        NSLog(@"Error initializing DrawOSC instance");
        return self;
    }
    
    drawBuffer_ = [[NSMutableArray alloc] init];
    timestamps_ = [[NSMutableArray alloc] init];
    
    return self;
}

- (void)drawRect:(NSRect)rect {

    /* Erase the background by drawing white */
    [[NSColor whiteColor] set];
    [NSBezierPath fillRect:rect];
    
    if (!shouldClear_) {
        
        /* Map key position to color */
        if (plotColorMap_) {
            
            /* Create the rectangle with constant height, width, and origin.y */
            NSRect drawingRect = NSZeroRect;
            drawingRect.size.width = 1;
            drawingRect.size.height = self.frame.size.height;
            drawingRect.origin.y = 0;
            
            CGFloat hue;
            
            for (int m = 0; m < drawBuffer_.count; ++m) {
                
                /* Map key positon to hue */
                hue = [[drawBuffer_ objectAtIndex:m] floatValue] / self.frame.size.height * 2/3;
                [[NSColor colorWithCalibratedHue:hue saturation:1 brightness:1 alpha:1] set];
                
                /* Shift one pixel to the right and draw */
                drawingRect.origin.x = m;
                NSRectFill(drawingRect);
            }
        }
        
        /* Plot position in cartesian coordinates */
        else {
            [[NSColor blueColor] set];
            for (int m = 0; m < drawBuffer_.count; ++m) {
                NSRect drawingRect = NSZeroRect;
                drawingRect.origin.x = m;
                drawingRect.origin.y = 0;
                drawingRect.size.width = 1;
                drawingRect.size.height = [[drawBuffer_ objectAtIndex:m] floatValue];
                NSRectFill(drawingRect);
            }
        }
    }
}

/* Plotting by index */
- (void)setDrawBuffer:(NSMutableArray *)buff {
    
    shouldClear_ = NO;
    int viewWidth = self.frame.size.width;
    int viewHeight = self.frame.size.height;
    
    /* Create linearly-spaced indices the size of the view width */
    float *indices = (float *)calloc(viewWidth, sizeof(float));
    [self linspace:0 max:buff.count-1 numElements:viewWidth array:indices];
    
    /* Linearly interpolate if we have fewer position points than pixels */
    if (buff.count < viewWidth) {
        for (int i = 0; i < viewWidth; ++i) {
            /* Nearest integer indices in buff */
            NSInteger x1 = floor(indices[i]);
            NSInteger x2 =  ceil(indices[i]);
            
            /* Values at those indices */
            float y1 = [[buff objectAtIndex:x1] floatValue];
            float y2 = [[buff objectAtIndex:x2] floatValue];
            
            /* Interpolation */
            float y3 = y1 + (y2-y1)*(indices[i]-x1);
            
            /* Scale to fit in the view */
            y3 *= viewHeight;
            
            /* Add to the drawBuffer */
            [drawBuffer_ insertObject:[NSNumber numberWithFloat:y3] atIndex:i];
        }
    }
    else {
    
    }

    [self setNeedsDisplay:YES];
}

/* Plotting by timestamp */
- (void)setDrawDict:(NSMutableDictionary *)buff {
    
    shouldClear_ = NO;
    int viewWidth = self.frame.size.width;
    int viewHeight = self.frame.size.height;
    
    /* Separate dictionary into timestamp and position value arrays sorted by timestamp */
    timestamps_ = [NSMutableArray arrayWithArray:[[buff allKeys] sortedArrayUsingSelector:@selector(compare:)]];
    NSMutableArray *tempBuffer_ = [NSMutableArray arrayWithArray:[buff objectsForKeys:timestamps_ notFoundMarker:[NSNumber numberWithFloat:0]]];
    
    /* Create $viewWidth$ time values at which we will interpolate the position history */
    float *targetTimestamps = (float *)calloc(viewWidth, sizeof(float));
    [self linspace:[timestamps_[0] floatValue] max:[timestamps_[timestamps_.count-1] floatValue] numElements:viewWidth array:targetTimestamps];
    
    /* Linearly interpolate if we have fewer position points than pixels */
    if (buff.count < viewWidth) {
        for (int i = 0; i < viewWidth; ++i) {
            
            /* Find index of first timestamp larger than the target timestamp */
            /* TO DO: Make more efficient than linear search
                - Divide and conquer
                - Recursively search subarrays */
            int j = 0;
            while ([timestamps_[j] floatValue] <= targetTimestamps[i] && j < timestamps_.count-2) {
                ++j;
            }
            
            /* Nearest integer indices in buff */
            NSInteger x1 = j-1;
            NSInteger x2 = j;

            /* Values at those indices */
            float y1 = [tempBuffer_[x1] floatValue];
            float y2 = [tempBuffer_[x2] floatValue];
            
            /* Timestamps at those indices */
            float t1 = [timestamps_[x1] floatValue];
            float t2 = [timestamps_[x2] floatValue];
            
            /* Interpolation */
            float y3 = y1 + (y2-y1)*(targetTimestamps[i]-t1)/(t2-t1);
            
            /* Scale to fit in the view */
            y3 *= viewHeight;
            
            /* Add to the drawBuffer */
            [drawBuffer_ insertObject:[NSNumber numberWithFloat:y3] atIndex:i];
        }
    }
    else {
        
    }

    [self setNeedsDisplay:YES];
}

- (void)setPlotColormap:(BOOL)mode {
    
    plotColorMap_ = mode;
}

- (void)clearPlot {

    shouldClear_ = YES;
    [self setNeedsDisplay:YES];
}

/* Generate a linearly-spaced set of indices for sampling an incoming waveform */
-(void)linspace:(float)minVal max:(float)maxVal numElements:(int)size array:(float*)array {
    
    float step = (maxVal-minVal)/(size-1);
    array[0] = minVal;
    int i;
    for (i = 1;i<size-1;i++) {
        array[i] = array[i-1]+step;
    }
    array[size-1] = maxVal;
}

@end

/* Comparator used to sort the NSMutableDictionary by timestamp string's float value */
@implementation NSString (numericComparison)

- (NSComparisonResult) compareNumerically:(NSString *) other
{
    float myValue = [self floatValue];
    float otherValue = [other floatValue];
    if (myValue == otherValue) return NSOrderedSame;
    return (myValue < otherValue ? NSOrderedAscending : NSOrderedDescending);
}

@end

/* *************************************************** */
/* *************************************************** */
/* *************************************************** */

@implementation DrawMIDI

- (void)drawRect:(NSRect)rect {
    
    /* Erase the background by drawing white */
    [[NSColor whiteColor] set];
    [NSBezierPath fillRect:rect];
    
    if (!shouldClear_) {
        
        /* Map MIDI velocity to color */
        if (plotColorMap_) {
            [[NSColor colorWithCalibratedHue:(float)(midiVelocity_/127.0*2/3) saturation:1 brightness:1 alpha:1] set];
            [NSBezierPath fillRect:rect];
        }
        /* Plot MIDI in cartesian coordinates */
        else {
            /* Set the plot color */
            [[NSColor blueColor] set];
            
            /* Create rectangle with constant origin.y and width (one pixel) */
            NSRect drawingRect = NSZeroRect;
            drawingRect.origin.y = 0;
            drawingRect.size.width = 1;
            
            for (int m = 0; m < drawBuffer_.count; ++m) {
                
                /* Shift rectangle right one pixel and set the height from the position buffer; draw */
                drawingRect.origin.x = m;
                drawingRect.size.height = [[drawBuffer_ objectAtIndex:m] floatValue];
                NSRectFill(drawingRect);
            }
        }
    }
}

- (void)setDrawBuffer:(NSInteger *)midiVelocity {
    
    shouldClear_ = NO;
    midiVelocity_ = (int)midiVelocity;
    
    int viewWidth = self.frame.size.width;
    
    float scaledVelocity = ((int)midiVelocity / 127.0)*self.frame.size.height;
    NSNumber *sv = [NSNumber numberWithFloat:scaledVelocity];
    
    NSLog(@"Setting scaled velocity to %f", scaledVelocity);
    
    for (int i = 0; i < viewWidth; ++i) {
        [drawBuffer_ insertObject:sv atIndex:i];
    }
    
    [self setNeedsDisplay:YES];
}

@end
