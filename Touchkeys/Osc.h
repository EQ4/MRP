/*
 *  osc.h
 *  touchkeys
 *
 *  Created by Andrew McPherson on 3/12/10.
 *  Copyright 2010 __MyCompanyName__. All rights reserved.
 *
 */

#ifndef OSC_H
#define OSC_H

#include <iostream>
#include <iomanip>
#include <fstream>
#include <set>
#include <map>
#include <string>
#include <vector>
#include <set>
#include <pthread.h>
#include "lo/lo.h"

using namespace std;

class OscMessageSource;

// This is an abstract base class implementing a single function oscHandlerMethod().  Objects that
// want to register to receive OSC messages should inherit from OscHandler.  Notice that all listener
// add/remove methods are private or protected.  The subclass of OscHandler should add any relevant 
// listeners, or optionally expose a public interface to add listeners.  (Never call the methods in
// OscMessageSource externally.)

class OscHandler
{
public:
	OscHandler() : oscController_(NULL) {}
	
	// The OSC controller will call this method when it gets a matching message that's been registered
	virtual bool oscHandlerMethod(const char *path, const char *types, int numValues, lo_arg **values, void *data) = 0;
	void setOscController(OscMessageSource *c) { oscController_ = c; }
	
	~OscHandler();	// In the destructor, remove all OSC listeners
protected:
	bool addOscListener(const string& path);
	bool removeOscListener(const string& path);
	bool removeAllOscListeners();
	
	OscMessageSource *oscController_;
	set<string> oscListenerPaths_;
};

// Base class for anything that acts as a source of OSC messages.  Could be
// received externally or internally generated.

class OscMessageSource
{
	friend class OscHandler;
	
public:
	OscMessageSource() {
		pthread_mutex_init(&oscListenerMutex_, NULL);		
	}
	
protected:
	bool addListener(const string& path, OscHandler *object);		// Add a listener object for a specific path
	bool removeListener(const string& path, OscHandler *object);	// Remove a listener object	from a specific path
	bool removeListener(OscHandler *object);						// Remove a listener object from all paths
	
	pthread_mutex_t oscListenerMutex_;		// This mutex protects the OSC listener table from being modified mid-message
	
	multimap<string, OscHandler*> noteListeners_;	// Map from OSC path name to handler (possibly multiple handlers per object)	
};

// This class specifically implements OSC messages coming from external sources

class OscReceiver : public OscMessageSource
{
public:
	OscReceiver(lo_server_thread thread, const char *prefix) {
		oscServerThread_ = thread;
		globalPrefix_.assign(prefix);
		useThru_ = false;
		lo_server_thread_add_method(thread, NULL, NULL, OscReceiver::staticHandler, (void *)this);
	}	
	
	void setThruAddress(lo_address thruAddr, const char *prefix) {
		thruAddress_ = thruAddr;
		thruPrefix_.assign(prefix);
		useThru_ = true;
	}
	
	// staticHandler() is called by liblo with new OSC messages.  Its only function is to pass control
	// to the object-specific handler method, which has access to all internal variables.
	
	int handler(const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *data);
	static int staticHandler(const char *path, const char *types, lo_arg **argv, int argc, lo_message msg, void *userData) {
		return ((OscReceiver *)userData)->handler(path, types, argv, argc, msg, userData);
	}	
	
	~OscReceiver() {
		lo_server_thread_del_method(oscServerThread_, NULL, NULL);
		pthread_mutex_destroy(&oscListenerMutex_);
	}	
	
private:
	lo_server_thread oscServerThread_;		// Thread that handles received OSC messages
	
	// OSC thru
	bool useThru_;							// Whether or not we retransmit any messages
	lo_address thruAddress_;				// Address to which we retransmit
	string thruPrefix_;						// Prefix that must be matched to be retransmitted
	
	// State variables
	string globalPrefix_;					// Prefix for all OSC paths	
};

class OscTransmitter
{
public:
	OscTransmitter() : debugMessages_(false) {}
	
	// Add and remove addresses to send to
	int addAddress(const char * host, const char * port, int proto = LO_UDP);
	void removeAddress(int index);
	void clearAddresses();
	
	void sendMessage(const char * path, const char * type, ...);
	void sendMessage(const char * path, const char * type, const lo_message& message);
	void sendByteArray(const char * path, const unsigned char * data, int length);
	
	void setDebugMessages(bool debug) { debugMessages_ = debug; }
	
	~OscTransmitter();
	
private:
	vector<lo_address> addresses_;

	bool debugMessages_;
};

#endif // OSC_H