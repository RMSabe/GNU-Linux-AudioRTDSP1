# GNU-Linux-AudioRTDSP1
Real Time Audio Delay Digital Signal Processing for GNU Linux Based OS.

This code retrives the audio signal from headerless audio files (.raw). The audio signal should be in stereo 44100 Hz 16bit format. The code might not work properly using different parameters/resolutions.

This code requires the ALSA build resources to run. Run command "sudo apt install libasound2-dev" to download and install required resources.

Note that, when compiling the code, two used resources must be explicitly mentioned on the command line:
-lpthread
-lasound

Author: Rafael Sabe
Email: rafaelmsabe@gmail.com
