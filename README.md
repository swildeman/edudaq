# EduDAQ
EduDAQ is simple data acquisition (DAQ) system for educational purposes that runs on an Arduino UNO. [Get started!](#getting-started)

Features:
* Sample Arduino analog pins with an accurate sampling period anywhere between 1 ms and 15 minutes.
* Change acquisition parameters via a simple Serial communication protocol (using the Arduino IDE Serial Monitor, or using MATLAB/Python/Excel Data Streamer in automated applications).
* Support for both continous acquisition ("live view") and triggered acquisition (trigger on signal passing a threshold, or on external pull-down).
* Special "graph-mode" to format the output for live preview with Arduino IDE Serial Plotter.
* Circular data buffer of up to 1200 samples to capture (part of) a signal _before_ the trigger (especially useful for fast signals).
* Up to 6 Analog Input pins can be sampled "in parallel" through multiplexing (the Analog pins are sampled in quick succession with 1 ms between samples)

# Command overview

|command|parameters|example&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;|description|
|:---|:---|:---|:---|
|?||`?`|Print an overview of the current settings.|
|p|sampPeriod|`p 10`|Set the sampling period in ms (between 1 ms and 900000 ms (15 min))|
|n|nChannels|`n 5`|Set the number of anolog inputs to sample from (see [Multiple signals](#multiple-signals)). `nSamples`x`nChannels` is limited to 1200.|
|r|adcRes|`r H`|Select the internal voltage reference for the Analog to Digital Converter (ADC), i.e. the voltage that corresponds to 1024. L = 5V (low resolution), H = 1.1V (high resolution)|
|t| [trigChannel] trigMode [trigThresh] [preTrigSamp] [acqDelay] | `t / 400`  | Set the trigger mode to `trigMode` (live = l, external = e, rising edge = /, falling edge = \\, crossing = x). For /,\\, and x modes, `trigThresh` sets the raw ADC value at which the signal trigger is fired (upon the signal crossing this value). `trigChannel` specifies the channel (0-5) used for the signal trigger (default = 0). `preTrigSamp` specifies how many samples before the trigger should be kept (can be between 0 and `nSamples-1`) and `acqDelay` specifies an optional acquisition delay (in number of periods) after the trigger (0 - 2000 sampling periods).|
|s|nSamples|`s 200`|Set the size of the [circular buffer](#circular-buffer-and-triggering) to `nSamples`|
|w|pwmOn [pwmFreq] [pwmDuty]| w Y 0.5 0.25 | Turn on (`pwmOn` = Y) or off (`pwmOn` = N) the square wave generator on PIN 9. The frequency and duty cycle of the square wave are controlled through `pwmFreq` and `pwmDuty`|
|s|valSep| `s ,`| Set the value separator in the output to `valSep` (can be any character; t = tab, s = space)|
|g|graphMode| `g Y`| When `graphMode` (Y/N) is set to Y(es) the output is formatted for Serial Plotter: timestamps are ommitted and channel labels are added.|

# Getting started

You'll need:
* An Arduino UNO (The code might work on other devices (perhaps with some modifications), but it has been developed and tested on a UNO).
* [Arduino IDE](https://www.arduino.cc/en/software) to compile and upload sketches and communicate over the Serial port.
* A signal that varies between 0-5V (e.g. from a sensor) connected to Analog Input A0.

As a test, you could simply use a wire from Arduino's GND, 3.3V, or 5V to manually create a "signal" on A0, or you could sample the onboard LED pin (Pin 13) which is toggled after each acquisition. A bit more fancy: connect a [potentiometer](https://makeabilitylab.github.io/physcomp/arduino/potentiometers.html#correct-potentiometer-based-analog-input-circuit-voltage-divider) between 0 and 5V and sample the central (sliding) contact and turn the knob to make arbitrary signals.

<img src="https://github.com/swildeman/edudaq/assets/34604545/65b53ec0-e484-4833-8752-eb429ad49d0d" alt="Potentiometer connected to A0" width="400">

After you've [compiled and uploaded](https://docs.arduino.cc/software/ide-v2/tutorials/getting-started/ide-v2-uploading-a-sketch/) the EduDAQ.ino sketch to your Arduino, [open the Serial Monitor](https://docs.arduino.cc/software/ide-v2/tutorials/ide-v2-serial-monitor/) from the Tools menu in Arduino IDE. Select 'Newline' and '115200 baud' and use the input field at the top of the Serial Monitor to send the message '**tl**' (short for '**t**rigger mode **l**ive'). If all went well, you should now see a continous stream of raw readings from Analog IN A0 (numbers between 0 and 1023). If you turn the potentiometer knob (or change the signal on A0 in some other way), you should see the number change.

<img width="600" alt="live mode" src="https://github.com/swildeman/edudaq/assets/34604545/c3472671-04f7-47da-a9ea-e31256f8a938">

The default sampling period is 500 ms (half a second). To change this, use the command `p [desired period in ms]`. For example, to set the period to 10 ms send the message `p 10`. If you are still in live mode, you should see the rate at which samples are printed increase. The onboard LED will also flicker more rapidly.

The output of EduDAQ is also compatible with Arduino IDE's Serial Plotter. Give it a try by closing the Serial Monitor and opening Serial Plotter from the Tools menu. The Arduino board will be reset when you do this, so you will have to reconfigure the live mode and the period by sending `t l p 10` using the message field at the bottom of the Serial Plotter. Make sure 'Newline' and '115200 baud' are selected as before. If all went well, you should now see a live graph of the signal on A0.

<img width="600" alt="serial plotter" src="https://github.com/swildeman/edudaq/assets/34604545/0df01505-3eb0-44de-a3c4-caeb9fb6d132">

# Circular buffer and triggering

<img src="https://github.com/swildeman/edudaq/assets/34604545/171ab354-882b-43a8-9283-1e2c0bd636eb" alt="Circular Buffer" height="250">
<img src="https://github.com/swildeman/edudaq/assets/34604545/89b7d0c0-e6ad-4f18-84dd-7ebd7818308a" alt="Trigger in Circular Buffer" height="250">

## Signal trigger

`s10 p10 t / 200 5`

<img width="600" alt="Signal Trigger" src="https://github.com/swildeman/edudaq/assets/34604545/ff3e7eac-5540-4884-9480-3aadaa58c1a5">

`n500 p1 g1 t / 200 100`

<img width="600" alt="Signal Trigger Serial Plot" src="https://github.com/swildeman/edudaq/assets/34604545/715485bc-f841-49b8-896b-98d309c2e228">

## External trigger

`t e`

<img width="600" alt="External trigger" src="https://github.com/swildeman/edudaq/assets/34604545/c8e0c2e5-b994-4b08-9147-3f2304f8b955">

# Multiple signals

<img src="https://github.com/swildeman/edudaq/assets/34604545/779bb4ad-7cd2-47ae-9f76-649faf1fd4bc" alt="Two Sensor Layout" width="400">

<img src="https://github.com/swildeman/edudaq/assets/34604545/ecd914d6-5e92-44fa-865c-a7fc077df04d" alt="Circular Buffer with Channel Multiplexing" height="250">

`n2 s500 g1`

<img width="600" alt="Two channels" src="https://github.com/swildeman/edudaq/assets/34604545/e753f8d9-7fa1-444c-bce1-fb38ae19e9fe">


# Sensor calibration

## Thermometer calibration
