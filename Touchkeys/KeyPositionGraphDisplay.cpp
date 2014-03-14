//
//  KeyPositionGraphDisplay.cpp
//  touchkeys
//
//  Created by Andrew McPherson on 28/01/2013.
//  Copyright (c) 2013 Andrew McPherson. All rights reserved.
//

#include "KeyPositionGraphDisplay.h"
#include <iostream>
#include <cmath>

KeyPositionGraphDisplay::KeyPositionGraphDisplay() :
totalDisplayWidth_(1.0), totalDisplayHeight_(1.0), displayPixelWidth_(1.0), displayPixelHeight_(1.0),
needsUpdate_(true), xMin_(0.0), xMax_(1.0), yMin_(-0.2), yMax_(1.2) {
	// Initialize OpenGL settings: 2D only
    
	//glMatrixMode(GL_PROJECTION);
	//glDisable(GL_DEPTH_TEST);
    
    pressStartTimestamp_ = pressFinishTimestamp_ = releaseStartTimestamp_ = releaseFinishTimestamp_ = missing_value<timestamp_type>::missing();
    pressStartPosition_ = pressFinishPosition_ = releaseStartPosition_ = releaseFinishPosition_ = missing_value<key_position>::missing();
    
    totalDisplayWidth_ = kDisplaySideMargin*2 + kDisplayGraphWidth;
    totalDisplayHeight_ = kDisplayTopMargin + kDisplayBottomMargin + kDisplayGraphHeight;
}

void KeyPositionGraphDisplay::setDisplaySize(float width, float height) {
	displayMutex_.lock();
	displayPixelWidth_ = width;
	displayPixelHeight_ = height;
	refreshViewport();
	displayMutex_.unlock();
}


// Render the keyboard display

void KeyPositionGraphDisplay::render() {
	// Start with a light gray background
	glClearColor(0.8, 0.8, 0.8, 1.0);
	glClear(GL_COLOR_BUFFER_BIT);
	glLoadIdentity();
    
    float invAspectRatio = totalDisplayWidth_ / totalDisplayHeight_; 
	float scaleValue = 2.0 / totalDisplayWidth_;
	
	glScalef(scaleValue, scaleValue * invAspectRatio, scaleValue);
	glTranslatef(-1.0 / scaleValue, -totalDisplayHeight_ / 2.0, 0);
	glTranslatef(kDisplaySideMargin, kDisplayBottomMargin, 0.0);
	
	displayMutex_.lock();
    
    // Draw the region where the graph will be plotted (white with black outline)
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glColor3f(1.0, 1.0, 1.0);
    
    glBegin(GL_POLYGON);
	glVertex2f(0, 0);
    glVertex2f(0, kDisplayGraphHeight);
	glVertex2f(kDisplayGraphWidth, kDisplayGraphHeight);
	glVertex2f(kDisplayGraphWidth, 0);
	glEnd();
    
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glColor3f(0.0, 0.0, 0.0);
    glLineWidth(1.0);
    
    glBegin(GL_POLYGON);
	glVertex2f(0, 0);
    glVertex2f(0, kDisplayGraphHeight);
	glVertex2f(kDisplayGraphWidth, kDisplayGraphHeight);
	glVertex2f(kDisplayGraphWidth, 0);
	glEnd();
    
    // Draw gray lines for the 0.0 and 1.0 key positions
    glColor3f(0.5, 0.5, 0.5);
    glBegin(GL_LINES);
    glVertex2f(0, graphToDisplayY(0.0));
    glVertex2f(kDisplayGraphWidth, graphToDisplayY(0.0));
    glEnd();
    glBegin(GL_LINES);
    glVertex2f(0, graphToDisplayY(1.0));
    glVertex2f(kDisplayGraphWidth, graphToDisplayY(1.0));
    glEnd();

    // Draw the graph of position over time (if data exists)
    if(!keyPositions_.empty() && !keyTimestamps_.empty()) {
        glColor3f(0.0, 0.0, 0.0);
        glBegin(GL_LINE_STRIP);
        for(int index = 0; index < keyPositions_.size() && index < keyTimestamps_.size(); index++) {
            glVertex2f(graphToDisplayX(keyTimestamps_[index]), graphToDisplayY(keyPositions_[index]));
        }
        glEnd();
    }
    
    // Draw the important features, if they exist
    if(!missing_value<timestamp_type>::isMissing(pressStartTimestamp_)) {
        glColor3f(0.0, 0.0, 1.0);
        glBegin(GL_LINES);
        glVertex2f(graphToDisplayX(pressStartTimestamp_), 0);
        glVertex2f(graphToDisplayX(pressStartTimestamp_), kDisplayGraphHeight);
        glEnd();
    }
    if(!missing_value<timestamp_type>::isMissing(pressFinishTimestamp_)) {
        glColor3f(1.0, 0.0, 0.0);
        glBegin(GL_LINES);
        glVertex2f(graphToDisplayX(pressFinishTimestamp_), 0);
        glVertex2f(graphToDisplayX(pressFinishTimestamp_), kDisplayGraphHeight);
        glEnd();
    }
    if(!missing_value<timestamp_type>::isMissing(releaseStartTimestamp_)) {
        glColor3f(0.0, 0.0, 1.0);
        glBegin(GL_LINES);
        glVertex2f(graphToDisplayX(releaseStartTimestamp_), 0);
        glVertex2f(graphToDisplayX(releaseStartTimestamp_), kDisplayGraphHeight);
        glEnd();
    }
    if(!missing_value<timestamp_type>::isMissing(releaseFinishTimestamp_)) {
        glColor3f(1.0, 0.0, 0.0);
        glBegin(GL_LINES);
        glVertex2f(graphToDisplayX(releaseFinishTimestamp_), 0);
        glVertex2f(graphToDisplayX(releaseFinishTimestamp_), kDisplayGraphHeight);
        glEnd();
    }
    
    needsUpdate_ = false;
	displayMutex_.unlock();
	
	glFlush();
}

// Mouse interaction methods

void KeyPositionGraphDisplay::mouseDown(float x, float y) {
	//Point mousePoint = {x, y};
	//Point scaledPoint = screenToInternal(mousePoint);
	
	//needsUpdate_ = true;
}

void KeyPositionGraphDisplay::mouseDragged(float x, float y) {
	//Point mousePoint = {x, y};
	//Point scaledPoint = screenToInternal(mousePoint);

    //needsUpdate_ = true;
}

void KeyPositionGraphDisplay::mouseUp(float x, float y) {
	//Point mousePoint = {x, y};
	//Point scaledPoint = screenToInternal(mousePoint);
	
	//needsUpdate_ = true;
}

void KeyPositionGraphDisplay::rightMouseDown(float x, float y) {
	//Point mousePoint = {x, y};
	//Point scaledPoint = screenToInternal(mousePoint);
	
	//needsUpdate_ = true;
}

void KeyPositionGraphDisplay::rightMouseDragged(float x, float y) {
	//Point mousePoint = {x, y};
	//Point scaledPoint = screenToInternal(mousePoint);
}

void KeyPositionGraphDisplay::rightMouseUp(float x, float y) {
	//Point mousePoint = {x, y};
	//Point scaledPoint = screenToInternal(mousePoint);
}

float KeyPositionGraphDisplay::graphToDisplayX(float x) {
    return kDisplayGraphWidth*(x - xMin_)/(xMax_ - xMin_);
}

float KeyPositionGraphDisplay::graphToDisplayY(float y) {
    return kDisplayGraphHeight*(y - yMin_)/(yMax_ - yMin_);
}

void KeyPositionGraphDisplay::refreshViewport() {
	glViewport(0, 0, displayPixelWidth_, displayPixelHeight_);
}

// Copy data from the circular buffer to our internal buffer for 
void KeyPositionGraphDisplay::copyKeyDataFromBuffer(Node<key_position>& keyBuffer,
                                                    const Node<key_position>::size_type startIndex,
                                                    const Node<key_position>::size_type endIndex) {
    // Clear existing information
    keyPositions_.clear();
    keyTimestamps_.clear();
    
    Node<key_position>::size_type index = startIndex;
    if(index < keyBuffer.beginIndex())
        index = keyBuffer.beginIndex();
    
    // Iterate through the buffer, copying positions and timestamps
    while(index < endIndex && index < keyBuffer.endIndex()) {
        keyPositions_.push_back(keyBuffer[index]);
        keyTimestamps_.push_back(keyBuffer.timestampAt(index));
        index++;
    }
    
    // Scale the axes according to what's being displayed. The Y axis should stay
    // more or less constant (for now), but X will change depending on timestamps
    xMin_ = keyTimestamps_.front();
    xMax_ = keyTimestamps_.back();
    yMin_ = -0.2;
    yMax_ = 1.2;
    needsUpdate_ = true;
}

// Conversion from internal coordinate space to external pixel values and back

// Pixel values go from 0,0 (lower left) to displayPixelWidth_, displayPixelHeight_ (upper right)
// Internal values go from -totalDisplayWidth_/2, -totalDisplayHeight_/2 (lower left)
//   to totalDisplayWidth_/2, totalDisplayHeight_/2 (upper right)

// Pixel value in --> OpenGL value out
KeyPositionGraphDisplay::Point KeyPositionGraphDisplay::screenToInternal(Point& inPoint) {
	Point out;
	
	out.x = -totalDisplayWidth_*0.5 + (inPoint.x/displayPixelWidth_) * totalDisplayWidth_;
	out.y = -totalDisplayHeight_*0.5 + (inPoint.y/displayPixelHeight_) * totalDisplayHeight_;
	
	return out;
}

// OpenGL value in --> Pixel value out
KeyPositionGraphDisplay::Point KeyPositionGraphDisplay::internalToScreen(Point& inPoint) {
	Point out;
	
	out.x = ((inPoint.x + totalDisplayWidth_*0.5)/totalDisplayWidth_) * displayPixelWidth_;
	out.y = ((inPoint.y + totalDisplayHeight_*0.5)/totalDisplayHeight_) * displayPixelHeight_;
    
	return out;
}