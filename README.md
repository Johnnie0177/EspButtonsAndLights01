# EspButtonsAndLights01
FreeRtos program run on an esp32 device.  Gpio pins 2, 3, and 4 drive leds to display a 0..7 counter.  Gpio pin 5 listens for push button presses and updates the counter.  Single click to increment, double click to decrement, long press to animate the leds then reset the count upon release.

This program completes an assignment for a firmware class:<br>
&nbsp; &nbsp; https://www.ucsc-extension.edu/courses/embedded-firmware-essentials

It is based largely on examples provided at:<br>
&nbsp; &nbsp; https://github.com/aircable/EFE_projects

Note: this program was cobbled together in phases for a class.  It demonstrates some basics: looping tasks, isr handler, gpio input and output, time measurement.  It does not demonstrate good design, don't judge me.
