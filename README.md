# EduDAQ
EduDAQ is simple data acquisition (DAQ) system for educational purposes that runs on an Arduino UNO.

Features:
* Sample Analog Input pins with an accurate (crystal oscillator controlled) rate up to 1000Hz (1 ms period) and down to once per 15 minutes.
* Configure/view acquisition/output settings via a simple Serial communication protocol (e.g. using the Arduino IDE Serial Monitor, or using MATLAB/Python/Excel Data Streamer in automated applications).
* Continous ("live view"), or triggered acquisition (trigger on signal passing a threshold, or on external pull-down).
* Special "graph-mode" to format the output for live streaming with Arduino IDE Serial Plotter.
* Circular data buffer of up to 1200 samples to capture (part of) a signal _before_ the trigger (especially useful for fast signals).
* Up to 6 Analog Input pins can be sampled "in parrallel" through multiplexing (the Analog pins are sampled in quick succession with 1 ms between samples)
* Output "raw" 10-bit ADC data or apply a custom 2nd order calibration polynomial.
* Special calibration mode for converting samples from a NTC thermistor to temperature readings.

# Command overview

|command|parameters|example|description|
|---|---|---|---|
|?||`?`|Print an overview of the current settings.|
|n|nSamples [nChannels]|`n 20 5`|Set the size of the [circular buffer](README.md#circular-buffer-and-triggering) to `nSamples` and sample `nChannels` anolog inputs starting from A0 (see [Multiple signals](README.md#multiple-signals)). `nSamples`x`nChannels` is limited to 1200.|
|...|||

# Getting started

You'll need:
* An Arduino UNO (The code might work on other devices (perhaps with some modifications), but it has been developed and tested on a UNO).
* [Arduino IDE](https://www.arduino.cc/en/software) to compile and upload sketches and communicate over the Serial port.
* A signal that varies between 0-5V (e.g. from a sensor) connected to Analog Input A0.

As a test, you could simply use a wire from Arduino's GND, 3.3V, or 5V to manually create a "signal" on A0, or you could sample the onboard LED pin (Pin 13) which is toggled after each acquisition. A bit more fancy: connect a [potentiometer](https://makeabilitylab.github.io/physcomp/arduino/potentiometers.html#correct-potentiometer-based-analog-input-circuit-voltage-divider) between 0 and 5V and sample the central (sliding) contact as you turn the knob to make arbitrary signals.

<img src="https://github.com/swildeman/edudaq/assets/34604545/65b53ec0-e484-4833-8752-eb429ad49d0d" alt="Potentiometer connected to A0" width="400" />

After you've [compiled and uploaded](https://docs.arduino.cc/software/ide-v2/tutorials/getting-started/ide-v2-uploading-a-sketch/) the EduDAQ.ino sketch to your Arduino, [open the Serial Monitor](https://docs.arduino.cc/software/ide-v2/tutorials/ide-v2-serial-monitor/) from the Tools menu in Arduino IDE. Select 'Newline' and '115200 baud' and use the input field at the top of the Serial Monitor to send the message '**tl**' (short for '**t**rigger mode **l**ive'). If all went well, you should now see a continous stream of raw readings from Analog IN A0 (numbers between 0 and 1023). If you turn the potentiometer knob (or change the signal on A0 in some other way), you should see the number change.

<img width="600" alt="live mode" src="https://github.com/swildeman/edudaq/assets/34604545/c3472671-04f7-47da-a9ea-e31256f8a938" />

The default sampling period is 500 ms (half a second). To change this, use the command `p [desired period in ms]`. For example, to set the period to 10 ms send the message `p 10`. If you are still in live mode, you should see the rate at which samples are printed increase. The onboard LED will also flicker more rapidly.

The output of EduDAQ is also compatible with Arduino IDE's Serial Plotter. Give it a try by closing the Serial Monitor and opening Serial Plotter from the Tools menu. The Arduino board will be reset when you do this, so you will have to reconfigure the live mode and the period by sending `t l p 10` using the message field at the bottom of the Serial Plotter. Make sure 'Newline' and '115200 baud' are selected as before. If all went well, you should now see a live graph of the signal on A0.

<img width="600" alt="serial plotter" src="https://github.com/swildeman/edudaq/assets/34604545/0df01505-3eb0-44de-a3c4-caeb9fb6d132" />

# Circular buffer and triggering

<img src="https://github.com/swildeman/edudaq/assets/34604545/171ab354-882b-43a8-9283-1e2c0bd636eb" alt="Circular Buffer" height="250"/>
<img src="https://github.com/swildeman/edudaq/assets/34604545/bbcc32d4-c20d-4307-8b80-80f8a89f91d4" alt="Trigger in Circular Buffer" height="250"/>

# Multiple signals

<img src="https://github.com/swildeman/edudaq/assets/34604545/ecd914d6-5e92-44fa-865c-a7fc077df04d" alt="Circular Buffer with Channel Multiplexing" height="250"/>

# Sensor calibration

## Thermometer calibration
