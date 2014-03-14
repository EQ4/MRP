MRP: Single GUI application with target functionality:

	- Multichannel audio synthesis for the Magnetic Resonator Piano
	- Control input provided by
		- 'Touchkey' devices including MRP keyboard scanner and Touchkeys multitouch 	key surfaces
		- MIDI controller
		- Open Sound Control (OSC) over network
	- Control data logging
	- High-resolution key movement feature visualization

- Implemented:
	- OSC
		- input
		- output
		- input logging
	- Touchkeys
		- Recognize device
		- User-defined callbacks
		- Raw data logging
	- Preferences menu
		- Logging path
		- Touchkey calibration path

- To Do:
	- OSC
		- OSC thru
		- OSC in to MIDI thru
		- advanced logging (only print specified messages)
	- MIDI
		- MIDI in
		- MIDI thru
		- MIDI in to OSC thru
	- Touchkeys
		- Touchkeys in to MIDI thru
	- Audio
		- Audio synthesis via AUGraph with selectable audio unit instruments
	- Visualization
		- One-octave piano roll view showing MIDI vs. key position resolution