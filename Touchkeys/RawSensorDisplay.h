//
//  RawSensorDisplay.h
//  touchkeys
//
//  Created by Andrew McPherson on 26/02/2013.
//  Copyright (c) 2013 Andrew McPherson. All rights reserved.
//

#ifndef __touchkeys__RawSensorDisplay__
#define __touchkeys__RawSensorDisplay__

#include <iostream>
#include <vector>
#include <OpenGL/gl.h>
#include <boost/thread.hpp>
#include "PianoTypes.h"
#include "Node.h"
#include "OpenGLDisplayBase.h"


// This class uses OpenGL to draw a bar graph of key sensor data,
// for the purpose of debugging and validating individual keys

class RawSensorDisplay : public OpenGLDisplayBase {
	// Internal data structures and constants
private:
    // Display margins
    const float kDisplaySideMargin = 0.5;
    const float kDisplayBottomMargin = 0.5;
    const float kDisplayTopMargin = 0.5;
    
    // Size of bar graphs and spacing
    const float kDisplayBarWidth = 0.5;
    const float kDisplayBarSpacing = 0.25;
    const float kDisplayBarHeight = 10.0;
    
	typedef struct {
		float x;
		float y;
	} Point;
	
public:
	RawSensorDisplay();
	
	// Setup methods for display size and keyboard range
	void setDisplaySize(float width, float height);
    float aspectRatio() { return totalDisplayWidth_ / totalDisplayHeight_; }
	
	// Drawing methods
	bool needsRender() { return needsUpdate_; }
	void render();
	
	// Interaction methods
	void mouseDown(float x, float y);
	void mouseDragged(float x, float y);
	void mouseUp(float x, float y);
	void rightMouseDown(float x, float y);
	void rightMouseDragged(float x, float y);
	void rightMouseUp(float x, float y);
    
    // Data update methods
    void setDisplayData(std::vector<int> const& values);
    
private:
    // Convert mathematical XY coordinate space to drawing positions
    float graphToDisplayX(float x);
    float graphToDisplayY(float y);
    
	void refreshViewport();
	
	// Conversion from internal coordinate space to external pixel values and back
	Point screenToInternal(Point& inPoint);
	Point internalToScreen(Point& inPoint);
	
    
private:
	float displayPixelWidth_, displayPixelHeight_;	// Pixel resolution of the surrounding window
	float totalDisplayWidth_, totalDisplayHeight_;	// Size of the internal view (centered around origin)
    float yMin_, yMax_;                             // Range of the graph axes
    
	bool needsUpdate_;								// Whether the keyboard should be redrawn
	boost::mutex displayMutex_;						// Synchronize access between data and display threads
    std::vector<int> displayValues_;                // Values to display as a bar graph
};

#endif /* defined(__touchkeys__RawSensorDisplay__) */
