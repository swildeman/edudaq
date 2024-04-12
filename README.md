# EduDAQ
EduDAQ is simple data acquisition (DAQ) system for educational purposes that runs on an Arduino UNO.

Features:
* Sample Analog Input pins with accurate (crystal oscillator controlled) sample rate up to 1000Hz (period 1 ms) and down to once per 15 minutes.
* Configure/view acquisition/output settings through a simple Serial communication protocol (e.g. using the Arduino IDE Serial Monitor, or using MATLAB/Python in automated applications).
* Continous ("live view"), or triggered acquisition (trigger on signal passing a threshold, or on external pull-down).
* Special "graph-mode" to format the output for live streaming with the Arduino IDE Serial Plotter.
* Circular data buffer of up to 1200 samples to capture (part of) a signal _before_ the trigger (especially useful for fast signals).
* Up to 6 analog pins can be sampled "in parrallel" through multiplexing (the analog pins are sampled in quick succession with 1 ms between samples)
* Output the "raw" 10-bit ADC data or apply a custom 2nd order calibration polynomial.
* Special calibration mode for converting samples of a NTC thermistor to temperature readings.

# Getting started

You'll need:
* An Arduino UNO (The code might work on other devices (perhaps with some modifications), but it has been developed and tested on a UNO).
* [Arduino IDE](https://www.arduino.cc/en/software) (Or any other software to compile and upload sketches and communicate over a Serial port).
* A signal that varies between 0-5V (e.g. from a sensor) connected to Analog Input A0. As a test, you could simply use a wire from Arduino's GND, 3.3V, or 5V to manually create a "signal" on A0, or you could sample the onboard LED pin (pin 13) which is toggled by the DAQ after each acquisition. If you want to go fancy, you could connect a [potentiometer](https://makeabilitylab.github.io/physcomp/arduino/potentiometers.html#correct-potentiometer-based-analog-input-circuit-voltage-divider) between 0 and 5V and sample the central (sliding) contact as you turn the knob to make arbitrary signals.

# Circular buffer and triggering

# Thermometer calibration
