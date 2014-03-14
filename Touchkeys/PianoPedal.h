/*
 *  PianoPedal.h
 *  keycontrol_cocoa
 *
 *  Created by Andrew McPherson on 6/14/11.
 *  Copyright 2011 __MyCompanyName__. All rights reserved.
 *
 */

#ifndef PIANO_PEDAL_H
#define PIANO_PEDAL_H

#include "Node.h"
#include "PianoTypes.h"

class PianoPedal : public Node<key_position> {
public:
	PianoPedal(int historyLength) : Node<key_position>(historyLength) {}
};

#endif /* PIANO_PEDAL_H */