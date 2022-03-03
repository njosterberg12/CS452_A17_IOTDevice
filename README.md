# CS452_A17_IOTDevice

This is an IOT device I made for my RTOS class using an ESP32 and the Microsoft Azure Cloud.

The ESP32 collects Temperature and Humidity data from a temp/humidity sensor connected to the ESP32,
and sends that data to the cloud. There is a stepper motor that is also connected to the ESP32 that
is controlled by a dipswitch on the board. If the dipswitch is in the proper position, the stepper
motor will turn either CW or CCW or not move depending on which button is selected on the webpage.
The colored neopixel LED's change colors depending on which state the device is in.
