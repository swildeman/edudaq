# EduDAQ
EduDAQ is simple data acquisition (DAQ) system for educational purposes that runs on an Arduino UNO.

Features:
* Sample Analog Input pins with an accurate (crystal oscillator controlled) rate up to 1000Hz (1 ms period) and down to once per 15 minutes.
* Configure/view acquisition/output settings through a simple Serial communication protocol (e.g. using the Arduino IDE Serial Monitor, or using MATLAB/Python in automated applications).
* Continous ("live view"), or triggered acquisition (trigger on signal passing a threshold, or on external pull-down).
* Special "graph-mode" to format the output for live streaming with the Arduino IDE Serial Plotter.
* Circular data buffer of up to 1200 samples to capture (part of) a signal _before_ the trigger (especially useful for fast signals).
* Up to 6 analog pins can be sampled "in parrallel" through multiplexing (the analog pins are sampled in quick succession with 1 ms between samples)
* Output the "raw" 10-bit ADC data or apply a custom 2nd order calibration polynomial.
* Special calibration mode for converting samples of a NTC thermistor to temperature readings.

# Command overview

# Getting started

You'll need:
* An Arduino UNO (The code might work on other devices (perhaps with some modifications), but it has been developed and tested on a UNO).
* [Arduino IDE](https://www.arduino.cc/en/software) to compile and upload sketches and communicate over the Serial port.
* A signal that varies between 0-5V (e.g. from a sensor) connected to Analog Input A0. As a test, you could simply use a wire from Arduino's GND, 3.3V, or 5V to manually create a "signal" on A0, or you could sample the onboard LED pin (pin 13) which is toggled by the DAQ after each acquisition. If you want to go fancy, you could connect a [potentiometer](https://makeabilitylab.github.io/physcomp/arduino/potentiometers.html#correct-potentiometer-based-analog-input-circuit-voltage-divider) between 0 and 5V and sample the central (sliding) contact as you turn the knob to make arbitrary signals:

<img src="https://github.com/swildeman/edudaq/assets/34604545/65b53ec0-e484-4833-8752-eb429ad49d0d" alt="Potentiometer connected to A0" width="400"/>

After you've [compiled and uploaded](https://docs.arduino.cc/software/ide-v2/tutorials/getting-started/ide-v2-uploading-a-sketch/) the EduDAQ.ino sketch to your Arduino [open the Serial Monitor](https://docs.arduino.cc/software/ide-v2/tutorials/ide-v2-serial-monitor/) from the Tools menu in Arduino IDE. Select 'Newline' and '115200 baud' and use the input field at the top of the Serial Monitor to send the message '**tl**' (short for '**t**rigger mode **l**ive'). If all went well, you should now see a continous stream of raw readings from Analog IN A0 (numbers between 0 and 1023). If you turn the potentiometer knob (or change the signal on A0 in some other way), you should see the number change.

screenshot

Explain how to change the period

Explain serial plotter

# Circular buffer and triggering

# Multiple signal channels

# Sensor calibration

## Thermometer calibration
