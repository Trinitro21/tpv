# tpv

Touch Point Visualizer

Draws your touches on the screen. Also implements edge swipe actions, mouse hiding on touch, and touch hold to rightclick.  
X11 only. You should probably also have a compositor running.

Dependencies are libx11, libxcomposite, libxi, libxfixes, libxtst and libevdev. Most desktops probably have these installed already.

To build on Ubuntu-based distributions:  
`sudo apt install libx11-dev libxcomposite-dev libxi-dev libxfixes-dev libxtst-dev libevdev-dev gcc`  
`/path/to/tpv.sh`

Usage:
`/path/to/tpv`

# Options

If the file exists, the program reads its settings from `~/.config/tpv`.  
The format is just the option name followed by the value, separated by a space.  
If a line does not start with one of the following options, it is ignored.  

* `device`  
	Device ID of your touchscreen. May differ between input methods.  
	If using libevdev, it's the number portion of `/dev/input/eventX`. If using `xinput2` for `inputmethod`, it's optional; if you don't set it, the program will catch touch events from all devices.  
	Default is -1.
* `fps`  
	Framerate of the program. Should be set to your display's refresh rate for minimal flicker.  
	Default is 60.
* `outputwindow`  
	Possibilities are `none`, `root` and `compositeoverlay`.  
	Specifies what window to draw the output on. Choosing `none` makes it not draw at all.  
	Default is `root`.
* `clearmethod`  
	Possibilities are `expose`, `cleararea`, and `exposeandcleararea`.  
	Specifies how the program should clear what it has drawn.  
	Default is `exposeandcleararea`.
* `inputmethod`  
	Possibilities are `libevdev` and `xinput2`.  
	If you choose `libevdev`, the program needs to be able to access `/dev/input/eventX`.  
	If you choose `xinput2`, pressure sensitivity won't be available.  
	Default is `xinput2`.
* `hidemouse`  
	Hides the mouse on touch input and shows it on mouse input.  
	Default is 1.
* `buttonlisten`  
	When `rightclickonhold` is activated, the mouse button press can cause the mouse to be shown. This option simply sets whether mouse buttons should be detected when `inputmethod` is `xinput2`.  
	Default is 1.
* `mousedevice`  
	The device to capture mouse events from. Only used if `hidemouse` is 1 and `inputmethod` is `libevdev`.  
	Default is -1.
* `rightclickonhold`  
	Whether the program should emulate rightclicks when one finger is held in place for more than the specified time interval.  
	Default is 1.
* `rightclicktime`  
	The time interval, in milliseconds, a touch has to be held for in order for a rightclick to be generated when the finger is lifted.  
	Default is 1000.
* `shapechangeifrightclick`  
	Whether the shape of the current touch should change if a rightclick will be triggered when the finger is lifted.  
	Default is 1.
* `fixedwidth`  
	Whether or not the point should use a fixed width or get touch size info from `ABS_MT_TOUCH_MAJOR`.  
	Pressure sensitivity is not available when using XInput2 for input.  
	Default is 1.
* `widthmult`  
	If fixedwidth is 0, how many pixels is one unit as reported by `ABS_MT_TOUCH_MAJOR`.  
	Default is 16.
* `width`  
	If fixedwidth is 1, how wide should the touch indicator be.  
	Default is 60.
* `shape`  
	Possibilities are `circle` and `square`.  
	Determines what shape the touch indicator should be.  
	Default is `circle`.
* `shapeaftertap`  
	Should the indicator still be visible after the touch moves out of the tap area.  
	Default is 0.
* `tapthreshold`  
	How much does the finger have to move for the touch to no longer be considered a tap.  
	Default is 30.
* `trail`  
	Should a trail be shown of the previous locations of each touch point.  
	Default is 1.
* `trailduringtap`  
	Should the trail be shown before the tap ends.  
	Default is 0.
* `trailstartdistance`  
	What distance should the trail start at.  
	Default is 0.
* `traillength`  
	How many frames behind the current touch should the trail display.  
	Default is 8.
* `traildispersion`  
	How many frames apart should the trail points be.  
	Default is 1.
* `trailisshape`  
	Should the trail be made up of shapes rather than lines.  
	Default is 0.
* `trailshape`  
	Possibilities are `circle` and `square`.  
	Determines what shape the trail points should be if trailshape is 1.  
	Default is `circle`.
* `edgeswipethreshold`  
	How far away from the edges of the screen a touch has to start to be considered an edge swipe.  
	Default is 1.
* `edgeswipeextrapolate`  
	If this is on, the start edge swipe check starts on the second frame of the touch and tries to extrapolate the finger position on the frame before the touch started.  
	This is more accurate and detects fast swipes more efficiently.  
	Default is 1.
* `edgetop`, `edgebottom`, `edgeleft`, `edgeright`  
	The command to run when sliding from the corresponding edge of the screen.  
	Default is an empty string.
* `edgetopend`, `edgebottomend`, `edgeleftend`, `edgerightend`  
	The command to run when a slide from the corresponding edge of the screen is lifted, so that the mouse position change doesn't interfere with the script.  
	Default is an empty string.

# Issues / To do

* While the maximum values for the touch coordinates under xinput2 seem to be always 65535 when using libinput as the x11 touchscreen driver, this is not always the case with other drivers like evdev. The application needs to programmatically find the maximum values for each device somehow.

* Some applications implement their own version of this application's rightclickonhold. Possibly there could be a blacklist of which applications should be excluded from the gesture.  
	This would require the application to figure out which window the cursor is currently over, get its application class, and compare that to a list.
