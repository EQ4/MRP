/*
 *  osc.cpp
 *  touchkeys
 *
 *  Created by Andrew McPherson on 11/14/09.
 *  Copyright 2009 __MyCompanyName__. All rights reserved.
 *
 */

#include "osc.h"

#pragma mark OscHandler

OscHandler::~OscHandler()
{
	if(oscController_ != NULL)	// Remove (individually) each listener
	{
		set<string>::iterator it;
		
		for(it = oscListenerPaths_.begin(); it != oscListenerPaths_.end(); ++it)
		{
			cout << "Deleting path " << *it << endl;
			
			string pathToRemove = *it;
			oscController_->removeListener(pathToRemove, this);
		}
	}
}

#pragma mark -- Private Methods

// Call this internal method to add a listener to the OSC controller.  Returns true on success.

bool OscHandler::addOscListener(const string& path)
{
	if(oscController_ == NULL)
		return false;
	if(oscListenerPaths_.count(path) > 0)
		return false;
	oscListenerPaths_.insert(path);
	oscController_->addListener(path, this);
	return true;
}

bool OscHandler::removeOscListener(const string& path)
{
	if(oscController_ == NULL)
		return false;
	if(oscListenerPaths_.count(path) == 0)
		return false;
	oscController_->removeListener(path, this);
	oscListenerPaths_.erase(path);
	return true;
}

bool OscHandler::removeAllOscListeners()
{
	if(oscController_ == NULL)
		return false;
	set<string>::iterator it = oscListenerPaths_.begin();
	
	while(it != oscListenerPaths_.end()) {
		removeOscListener(*it++);
	}
	
	return true;
}

#pragma mark OscMessageSource

// Adds a specific object listening for a specific OSC message.  The object will be
// added to the internal map from strings to objects.  All messages are preceded by
// a global prefix (typically "/mrp").  Returns true on success.

bool OscMessageSource::addListener(const string& path, OscHandler *object)
{
	if(object == NULL)
		return false;
	
	pthread_mutex_lock(&oscListenerMutex_);
	noteListeners_.insert(pair<string, OscHandler*>(path, object));
	pthread_mutex_unlock(&oscListenerMutex_);
	
#ifdef DEBUG_MESSAGES
	cout << "Added OSC listener to path '" << path << "'\n";
#endif
	
	return true;
}

// Removes a specific object from listening to a specific OSC message.
// Returns true if at least one path was removed.

bool OscMessageSource::removeListener(const string& path, OscHandler *object)
{
	if(object == NULL)
		return false;
	
	bool removedAny = false;
	
	pthread_mutex_lock(&oscListenerMutex_);	// Lock the mutex so no incoming messages happen in the middle
	
	multimap<string, OscHandler*>::iterator it;
	pair<multimap<string, OscHandler*>::iterator,multimap<string, OscHandler*>::iterator> ret;
	
	// Every time we remove an element from the multimap, the iterator is potentially corrupted.  Realistically
	// there should never be more than one entry with the same object and same path (we check this on insertion).
	
	ret = noteListeners_.equal_range(path);
	
	it = ret.first;
	while(it != ret.second)
	{
		if(it->second == object)
		{
			noteListeners_.erase(it++);
			removedAny = true;
			break;
		}
		else
			++it;
	}
	
	pthread_mutex_unlock(&oscListenerMutex_);
	
#ifdef DEBUG_MESSAGES	
	if(removedAny)
		cout << "Removed OSC listener from path '" << path << "'\n";	
	else
		cout << "Removal failed to find OSC listener on path '" << path << "'\n";
#endif
	
	return removedAny;
}

// Removes an object from all OSC messages it was listening to.  Returns true if object
// was found and removed.

bool OscMessageSource::removeListener(OscHandler *object)
{
	if(object == NULL)
		return false;
	
	bool removedAny = false;
	
	pthread_mutex_lock(&oscListenerMutex_);	// Lock the mutex so no incoming messages happen in the middle
	
	multimap<string, OscHandler*>::iterator it;

	// Every time we remove an element from the multimap, the iterator is potentially corrupted.  Realistically
	// there should never be more than one entry with the same object and same path (we check this on insertion).
	
	it = noteListeners_.begin();
	while(it != noteListeners_.end())
	{
		if(it->second == object)
		{
			noteListeners_.erase(it++);
			removedAny = true;
			break;
		}
		else
			++it;
	}
	
	pthread_mutex_unlock(&oscListenerMutex_);
	
#ifdef DEBUG_MESSAGES	
	if(removedAny)
		cout << "Removed OSC listener from all paths\n";	
	else
		cout << "Removal failed to find OSC listener on any path\n";
#endif
	
	return removedAny;
}

#pragma mark OscReceiver

// OscReceiver::handler()
// The main handler method for incoming OSC messages.  From here, we farm out the processing depending
// on the path.  In general all our paths should start with /mrp.  Return 0 if the message has been
// adequately handled, 1 otherwise (so the server can look for other functions to pass it to).

int OscReceiver::handler(const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *data)
{
	bool matched = false;
	
	string pathString(path);	
	
	if(useThru_)
	{
		// Rebroadcast any matching messages
		
		if(!pathString.compare(0, thruPrefix_.length(), thruPrefix_))
			lo_send_message(thruAddress_, path, msg);
	}
	
	// Check if the incoming message matches the global prefix for this program.  If not, discard it.
	if(pathString.compare(0, globalPrefix_.length(), globalPrefix_))
	{
		cout << "OSC message '" << path << "' received\n";
		return 1;
	}
	
	// Lock the mutex so the list of listeners doesn't change midway through
	pthread_mutex_lock(&oscListenerMutex_);
	
	// Now remove the global prefix and compare the rest of the message to the registered handlers.
	multimap<string, OscHandler*>::iterator it;
	pair<multimap<string, OscHandler*>::iterator,multimap<string, OscHandler*>::iterator> ret;
	string truncatedPath = pathString.substr(globalPrefix_.length(), 
											 pathString.length() - globalPrefix_.length());
	ret = noteListeners_.equal_range(truncatedPath);
	
	it = ret.first;
	while(it != ret.second)
	{
		OscHandler *object = (*it++).second;
		
#ifdef DEBUG_MESSAGES
		cout << "Matched OSC path '" << path << "' to handler " << object << endl;
#endif
		object->oscHandlerMethod(truncatedPath.c_str(), types, argc, argv, data);
		matched = true;
	}
	
	pthread_mutex_unlock(&oscListenerMutex_);
	
	if(matched)		// This message has been handled
		return 0;
	
	printf("Unhandled OSC path: <%s>\n", path);
	
#ifdef DEBUG_MESSAGES
    for (int i=0; i<argc; i++) {
		printf("arg %d '%c' ", i, types[i]);
		lo_arg_pp((lo_type)types[i], argv[i]);
		printf("\n");
    }
#endif
	
    return 1;
}

#pragma mark OscTransmitter

// Add a new transmit address.  Returns the index of the new address.

int OscTransmitter::addAddress(const char * host, const char * port, int proto)
{
	lo_address addr = lo_address_new_with_proto(proto, host, port);
	
	if(addr == 0)
		return -1;
	addresses_.push_back(addr);
	
	return (int)addresses_.size() - 1;
}

// Delete a current transmit address

void OscTransmitter::removeAddress(int index)
{
	if(index >= addresses_.size() || index < 0)
		return;
	addresses_.erase(addresses_.begin() + index);
}

// Delete all destination addresses

void OscTransmitter::clearAddresses()
{
	vector<lo_address>::iterator it = addresses_.begin();
	
	while(it != addresses_.end()) {
		lo_address_free(*it++);
	}
	
	addresses_.clear();
}

void OscTransmitter::sendMessage(const char * path, const char * type, ...)
{
	va_list v;
	
	va_start(v, type);
	lo_message msg = lo_message_new();
	lo_message_add_varargs(msg, type, v);

	/*if(debugMessages_) {
		cout << path << " " << type << ": ";
		
		lo_arg **args = lo_message_get_argv(msg);
		
		for(int i = 0; i < lo_message_get_argc(msg); i++) {
			switch(type[i]) {
				case 'i':
					cout << args[i]->i << " ";
					break;
				case 'f':
					cout << args[i]->f << " ";
					break;
				default:
					cout << "? ";
			}
		}
		
		cout << endl;
		//lo_message_pp(msg);
	}*/
	
	sendMessage(path, type, msg);

	lo_message_free(msg);
	va_end(v);
}

void OscTransmitter::sendMessage(const char * path, const char * type, const lo_message& message)
{
    if(debugMessages_) {
        cout << path << " " << type << " ";

        int argc = lo_message_get_argc(message);
        lo_arg **argv = lo_message_get_argv(message);
        for (int i=0; i<argc; i++) {
            lo_arg_pp((lo_type)type[i], argv[i]);
            cout << " ";
        }
        cout << endl;
    }
    
	// Send message to everyone who's currently listening
	for(vector<lo_address>::iterator it = addresses_.begin(); it != addresses_.end(); it++) {
		lo_send_message(*it, path, message);
	}
}

// Send an array of bytes as an OSC message.  Bytes will be sent as a blob.

void OscTransmitter::sendByteArray(const char * path, const unsigned char * data, int length)
{
	if(length == 0)
		return;
	
	lo_blob b = lo_blob_new(length, data);
	
	lo_message msg = lo_message_new();
	lo_message_add_blob(msg, b);
	
	if(debugMessages_) {
		cout << path << " ";
		lo_message_pp(msg);
	}
	
	// Send message to everyone who's currently listening
	for(vector<lo_address>::iterator it = addresses_.begin(); it != addresses_.end(); it++) {
		lo_send_message(*it, path, msg);
	}	
	
	lo_blob_free(b);
}

OscTransmitter::~OscTransmitter()
{
	clearAddresses();
}