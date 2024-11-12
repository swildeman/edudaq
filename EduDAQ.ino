// EduDAQ - by Sander Wildeman 2024 (license: GPL-3.0)
//
// Information sources:
//  Analog measurements: https://meettechniek.info/embedded/arduino-analog.html
//  ADC reference: https://www.tspi.at/2021/07/15/atmegaavradc.html
//  Using Timers: https://deepbluembedded.com/arduino-timer-interrupts/
//  Timer1 settings: https://wolles-elektronikkiste.de/en/timer-and-pwm-part-2-16-bit-timer1
//  Using interrupts: https://www.gammon.com.au/interrupts
//  Pinout Arduino Uno: https://commons.wikimedia.org/wiki/File:Pinout_of_ARDUINO_Board_and_ATMega328PU.svg

//  Todo: ignore trigger events for at least preTrigSamp after an acqComplete to make sure we capture fresh samples each time

#include <errno.h> // needed for error handling of string to number conversions
#include <limits.h>

#define PRESCALER 64
#define PRESCALER_BITS (_BV(CS10) | _BV(CS11)) // prescaler 64

#define enableISRs() ADCSRA |= _BV(ADIE); EIMSK |= _BV(INT0); // enable DAQ related ISRs (ADC_vect and INT0)
#define disableISRs() ADCSRA &= ~_BV(ADIE); EIMSK &= ~_BV(INT0); // disable DAQ related ISRs

#define STROBE_PIN 9
//
// Initial configuration
//
struct {
  // Acquisition settings
  //
  // p sampPeriod
  // n nChannels
  // r adcRes
  //
  byte nChannels = 1;
  long sampPeriod = 100; // milliseconds
  char adcRes = 'L'; // Low L (Max = 5V), High H (Max = 1.1 V)
  
  // Trigger settings
  //
  // t [trigChannel] trigMode trigThresh  preTrigSamp acqDelay
  // s nSamples
  //
  byte trigChannel = 0; // 0 - 5
  char trigMode = 'e'; // external e, live l, falling \, rising /, crossing x
  int trigThresh = 500; // in raw 10-bit ADC units 0 - 1023
  int preTrigSamp = 5; // number of samples to keep before trigger event
  int acqDelay = 0; // number of periods to delay the acquisition (multiple of 1 ms)
  int nSamples = 10; 

  // Waveform generation settings
  //
  // w pwmOn pwmFreq pwmDuty
  //
  //
  bool pwmOn = false; // Y/N
  float pwmFreq = 1; // Hz
  float pwmDuty = 0.5;

  // Output settings
  //
  // s valSep 
  // g graphMode
  //
  char valSep = '\t'; // any character, t = tab, s = space
  bool graphMode = false; // Y/N - format data suitable for Serial Plotter
} cfg; //, input;

// buffer size based on initial configuration (make sure bufSize < maxBufSize)
int bufSize = cfg.nChannels*cfg.nSamples; // bufSize = nChannels*nSamples

const int maxBufSize = 1200;
const int maxInputStrLen = 64; // We are on a tight budget, make sure we don't go over it when a long input string is typed
const int maxAcqDelay = 2000;
const long maxSampPeriod = 900000L; // max(maxAcqDelay,maxBufSize)*maxSampPeriod should be < max(long type), otherwise the timestamp might not be properly calculated
const byte maxChannels = 6;
const byte LED_BIT = _BV(5); // inbuilt LED on PORTB

byte sampBuf[maxBufSize];      // High bits of 10-bit ADC samples
byte sampBufL[maxBufSize / 4]; // Low bits

char inputString[maxInputStrLen] = "";
bool inputComplete = false;

char sci_str[12];  // Buffer used for output formatting

int acqDelayCnt = 0;
unsigned long skipCnt = 0;

// 'Volatile' tells the compiler that these values might be modified in ISR (interrupt) routines and 
// should therefore not be cached when used in main thread. Thus, values that are modified in ISR 
// and used outside ISR should be declared volatile.
// If any of the ints (2 byte values) is read in main program while an interrupt may modify them,
// the relevant interrupts should be temporarly disabled when performing these operations.
//volatile bool preventTrigFirstSamp = true;
volatile int trigIgnoreFirstNSamples = 1;
volatile int curPivot = 0;      // current pivot location in the circular buffer, this is where the NEXT sample will be written to
volatile byte curChannel = 0; // channel from which the NEXT sample will be read
volatile int trigPivot = 0;
volatile int startPivot = 0;
volatile int adcCur[maxChannels];
volatile bool trigger = false;
volatile bool acqStarted = false;
volatile bool acqComplete = false;
volatile bool sampAvail = false;


void setup() {
  Serial.begin(115200);

  cli(); // make sure no interrupts are triggered while we set things up

  //
  // set LED pin to output mode
  //
  DDRB |= LED_BIT;

  //
  // init circular buffers
  //
  clearBuf();

  //
  // set up external interrupt
  //
  EIMSK = 0;
  EICRA = _BV(ISC01); // trigger on falling edge INT0, Arduino Pin 2
  EIFR |= _BV(INTF0); // clear interrupt flag
  pinMode(2, INPUT_PULLUP); // enable internal pull-up, so that it can be triggered when pulled low

  //
  // Set up Timer0 for controlling acquisition rate (a 8 bit timer, counts from 0 to 255)
  // This frees up Timer1 to create e.g. PWM output for a function generator
  // Note: this will corrupt the Arduino functions delay(), delayMicroseconds(), micros() and millis(), but we don't use these anyway
  //
  byte cyclesPerMs = F_CPU / PRESCALER / 1000; // cyclesPerMs = fcpu / prescaler / 1000 (= 250 @ 16 MHz)
  TCCR0A = _BV(WGM01);      // configure timer to reset when TCNT0 reaches value in OCR1A (CTC Mode)
  TCCR0B = PRESCALER_BITS;  // set prescaler (determines base frequency)
  OCR0A = cyclesPerMs - 1;  // set timer count "TOP" where timer is reset (= sample period)
  TIMSK0 |= _BV(OCIE1A);    // enable interrupt when TCNT0 reaches OCR1A value (TIMER0_COMPA_vect is not really used, but needed to trigger ADC)
  TCNT0 = 0;                // reset Timer1 count

  //
  // set up AD converter for cont. acquisition timed by Timer1
  //
  ADMUX = 0;                          // read from analog pin A0
  ADMUX |= _BV(REFS0) | _BV(ADLAR);   // use Vcc = 5V as reference for AD conversion, left adjust
  ADCSRA = _BV(ADEN) | _BV(ADATE);    // ADC Enable, Auto Trigger Enable, Interrupt enable
  ADCSRA |= _BV(ADPS2) | _BV(ADPS1) | _BV(ADPS0); // set ADC Prescaler to 128 -> AD conversion rate = F_CPU/128/(13 ADC cycles) = 9.6 kHz
  ADCSRB = _BV(ADTS1) | _BV(ADTS0);   // set Auto Trigger source to Timer/Counter0 Compare Match A (i.e. when TCNT0 = OCR0A)
  ADCSRA |= _BV(ADIF);                // clear ADC interrupt flag (just to be sure)

  //
  // Set-up Timer 1 for fast PWM mode
  //
  TCCR1A = _BV(WGM11); // Clear on Match OCR1A, WGM: Fast PWM 14 (Top = ICR1)
  TCCR1B = _BV(WGM13) | _BV(WGM12);
  configPWMClock();
  if(cfg.pwmOn) pwmOn();

  //
  // Get going
  //
  enableISRs(); // enable ADC and Ext. Int. ISR handlers
  sei(); // set global interrupt flag (e.g. Timer0/1)
}

void configPWMClock() {  
  long periodTimerCnts = F_CPU / cfg.pwmFreq + 0.5; // constrained between 2 and 4*16e6
  long ontimeTimerCnts = periodTimerCnts * cfg.pwmDuty + 0.5;

  // Auto-select prescaler from prescaler options = 1, 8, 64, 256, 1024

  unsigned int reqPrescaler = periodTimerCnts / (UINT_MAX + 1L);   // 16 bit counter -> max TCNT1 = UINT_MAX = 65535
  unsigned int pwmPrescaler;

  TCCR1B &= ~(_BV(CS10) | _BV(CS11) | _BV(CS12)); // clear previous clock bits

  // set new prescaler (clock select) bits
  if(reqPrescaler <= 1) {
    pwmPrescaler = 1;
    TCCR1B |= _BV(CS10); 
  }
  else if(reqPrescaler <=8) {
    pwmPrescaler = 8;
    TCCR1B |= _BV(CS11);
  }
  else if(reqPrescaler <=64) {
    pwmPrescaler = 64;
    TCCR1B |= _BV(CS10) | _BV(CS11);
  }
  else if(reqPrescaler <=256) {
    pwmPrescaler = 256;
    TCCR1B |= _BV(CS12);
  }
  else {
    pwmPrescaler = 1024;
    TCCR1B |= _BV(CS12) | _BV(CS10);
  }

  periodTimerCnts /= pwmPrescaler; 
  ontimeTimerCnts /= pwmPrescaler;
  
  ICR1 = periodTimerCnts - 1;
  OCR1A = constrain(ontimeTimerCnts - 1, 0, periodTimerCnts - 2);

  // todo: set cfg to actual pwmFreq, pwmDuty so that user can see it was modified
}


// Dummy COMPA interrupt handler to make sure ISR(ADC_vect) gets triggered according to ADC setup
// If needed we could later use the interrupt to make our own "millis()" function
EMPTY_INTERRUPT (TIMER0_COMPA_vect); 

// Triggered when ADC has succesfully read a value (i.e. ~13 ADC cycles @125kHz (fcpu/adc prescaler of 128) after COMPB trigger)
ISR(ADC_vect) {
  if (curChannel == 0) {
    if(skipCnt < cfg.sampPeriod - cfg.nChannels) {
      skipCnt++;
      return;
    }
    else {
      skipCnt = 0;
      PORTB ^= LED_BIT;
    }
  }

  // temporary store previous reading (for trigger calculation) and get the new ADC reading
  int adcPrev = adcCur[curChannel];
  adcCur[curChannel] = ADCL >> 6;
  adcCur[curChannel] |= ADCH << 2;

  // store the acquired ADC reading into the circular buffer
  toBuf(adcCur[curChannel], curPivot);

  if(curChannel == cfg.nChannels-1) sampAvail = true;

  if(trigIgnoreFirstNSamples > 0) {
    if(curChannel == cfg.nChannels-1) trigIgnoreFirstNSamples--;
  }
  else if(curChannel == cfg.trigChannel) {
    if (!trigger) {
      // check if we should trigger
      if ( ((cfg.trigMode == 'x' || cfg.trigMode == '\\') && adcPrev > cfg.trigThresh && adcCur[curChannel] <= cfg.trigThresh)
           || ((cfg.trigMode == 'x' || cfg.trigMode == '/') && adcPrev < cfg.trigThresh && adcCur[curChannel] >= cfg.trigThresh) )
      {
        setTrig();
      }
    }
  
    if (trigger) {
      if (acqDelayCnt >= cfg.acqDelay) {
        acqStarted = true;
      }
      else {
        acqDelayCnt++;
      }
    }
  }

  // set things up for the next function call
  curPivot = (curPivot + 1) % bufSize;

  // cycle through channels (multiplexing)
  ADMUX &= ~curChannel;
  curChannel = (curChannel + 1) % cfg.nChannels;
  ADMUX |= curChannel;
  
  if (trigger && acqStarted && curPivot == startPivot) { // we filled the buffer after a trigger
    acqComplete = true;
    disableISRs(); // dissable calls to interrupt handlers (till we have processed the data in the buffer)
  }
}

// External interrupt triggered when PIN 2 is pulled low (falling edge)
ISR(INT0_vect)
{
  if(cfg.trigMode == 'e') setTrig();
}

// Arduino's pseudo-interrupt called on Arduino UNO at end of "loop()" function
void serialEvent() {
  while(Serial.available()) {
    char inChar = (char)Serial.read();
    if(inChar == '\n') {
      inputComplete = true;
    }
    else {
      int len = strlen(inputString);
      if(len < maxInputStrLen-1) {
        inputString[len] = inChar;
        inputString[len+1] = '\0';
      }
    }
  }
}


void loop() {
   if(inputComplete) { // process input data acquired in "serialEvent" function
      processInput();
   }
   
  if(cfg.trigMode == 'l' && sampAvail) { // live mode
    disableISRs();
    for(int c=0; c<cfg.nChannels; c++) {
      if(cfg.graphMode) { Serial.print(c); Serial.print(':'); }
      Serial.print(adcCur[c]);
      if(c < cfg.nChannels -1) Serial.print(cfg.valSep);
    }
    Serial.println();
    sampAvail = false;
    enableISRs();
  }
  
  if (acqComplete) { // acquisition complete after a trigger event
    long t = (-cfg.preTrigSamp+cfg.acqDelay)*cfg.sampPeriod;
    
    int c = 0;
    int i = startPivot;
    do {
      if(c == 0 && !cfg.graphMode) { Serial.print(t); Serial.print(cfg.valSep); }
      if(c > 0) Serial.print(cfg.valSep);
      if(cfg.graphMode) { Serial.print(c); Serial.print(':'); }

      int s = fromBuf(i);
      Serial.print(s);
      
      if(c == cfg.nChannels-1) {
        t += cfg.sampPeriod;
        Serial.println();
      }
      
      c = (c + 1) % cfg.nChannels;
      i = (i + 1) % bufSize;
    } while (i != startPivot);

    Serial.println();

    resetAcq();
    trigIgnoreFirstNSamples = cfg.preTrigSamp + 1; // make sure we don't get a corrupted time series because acquisition was temporarly halted
    enableISRs(); // continue filling the buffer when samples arrive
  }
}


// Macros for input parsing with error handling
char* endptr; 
#define strtol_or_cont(val, rest) \
          errno = 0; \
          val = strtol(rest,&endptr,10); \
          if(errno != 0 || rest == endptr) continue; \
          rest = endptr;

#define strtod_or_cont(val, rest) \
          errno = 0; \
          val = strtod(rest,&endptr); \
          if(errno != 0 || rest == endptr) continue; \
          rest = endptr;

void processInput() {
  disableISRs(); // make sure values are not modified/used in ISRs while we are reading/modifying them here

  // these values will be updated in a special order after all input is read, as they are constrained by other values 
  long nSamples = cfg.nSamples;
  long preTrigSamp = cfg.preTrigSamp;
  long sampPeriod = cfg.sampPeriod;

  bool reqResetBuf = false;
  bool reqSettingsPrint = false;
  bool reqResUpdate = false;
  bool reqPWMToggle = false;
  bool reqPWMConfig = false;
  
  char* rest = inputString; // pointer to start of remaining unparsed string
  
  while(rest[0] != '\0') {
    char prefix = tolower(rest[0]); // read (potential) control character

    if(prefix == '?') {
      reqSettingsPrint = true; 
      rest++;
      continue;
    }

    rest++; while(isspace(rest[0])) rest++; if(rest[0] == '\0') break;
    
    if(prefix == 't') { // [trigChannel] trigMode trigThresh preTrigSamp acqDelay
      long val;
      
      errno = 0; // try reading first value as a channel number, otherwise continue with other interpretations
      val = strtol(rest,&endptr,10);
      if(errno == 0 && rest != endptr && val >= 0 && val < maxChannels) {
        cfg.trigChannel = val;
        rest = endptr;
      }
      
      while(isspace(rest[0])) rest++; if(rest[0] == '\0') break;
      
      char mode = tolower(rest[0]);
      switch(mode) {
        case 'x': case '\\': case '/': case 'e': case 'l': // x, e, and l should not be used as prefix for another command
          cfg.trigMode = mode; rest++; break;
      }
      strtol_or_cont(val, rest); cfg.trigThresh = constrain(val,0,1023);
      strtol_or_cont(val, rest); preTrigSamp = val; // constraints dealth with later
      strtol_or_cont(val, rest); cfg.acqDelay = constrain(val,0,maxAcqDelay);
    }
    else if(prefix == 'v') { // value separator
      char sep = rest[0];
      switch(sep) {
        case 't': cfg.valSep = '\t'; rest++; break;
        case 's': cfg.valSep = ' '; rest++; break;
        default: cfg.valSep = sep; rest++;
      }
    }
    else if(prefix == 'r') {
      char res = toupper(rest[0]);
      switch(res) {
        case 'H': case 'L':
          if(cfg.adcRes != res) { 
            cfg.adcRes = res;
            reqResUpdate = true;
          }
          rest++;
          break;
      }
    }
    else if(prefix == 'g') {
      char yn = toupper(rest[0]);
      switch(yn) {
        case 'Y': cfg.graphMode = true; rest++; break;
        case 'N': cfg.graphMode = false; rest++; break;
      }
    }
    else if(prefix == 'w') {
      bool pwmOn = cfg.pwmOn;
      
      char yn = toupper(rest[0]);
      switch(yn) {
        case 'Y': pwmOn = true; rest++; break;
        case 'N': pwmOn = false; rest++; break;
      }
      
      if(pwmOn != cfg.pwmOn) { 
        reqPWMToggle = true;
        cfg.pwmOn = pwmOn;
      }
          
      float fd;
          
      strtod_or_cont(fd, rest); // freq
      fd = constrain(fd, 0.25, 8.0e6);
      if(fd != cfg.pwmFreq) { cfg.pwmFreq = fd; reqPWMConfig = true; }
          
      strtod_or_cont(fd, rest); // duty
      fd = constrain(fd, 0.0, 1.0);
      if(fd != cfg.pwmDuty) { cfg.pwmDuty = fd; reqPWMConfig = true; }
    }
    else {
      long val;
      strtol_or_cont(val, rest);
      
      switch(prefix) {
        case 's':
          nSamples = val; break; // dealth with later to handle interdep constrains
        case 'n':
          val = constrain(val,1,maxChannels); 
          if(val != cfg.nChannels) reqResetBuf = true;
          cfg.nChannels = val; // constraints dealth with later
          break;
        case 'p': sampPeriod = val; break;
      }
    }
  }
  inputString[0] = '\0'; // reset the input buffer so that we can receive new input

  // deal with values that need a specific order due to interdependent constraints
  nSamples = constrain(nSamples, 2, maxBufSize/cfg.nChannels);
  if(nSamples != cfg.nSamples) reqResetBuf = true;
  cfg.nSamples = nSamples;
  cfg.preTrigSamp = constrain(preTrigSamp, 0, cfg.nSamples-1);
  cfg.sampPeriod = constrain(sampPeriod, cfg.nChannels, maxSampPeriod);

  if(reqResetBuf) resetBuf();
  if(reqResUpdate) {
    switch(cfg.adcRes) {
      case 'L': ADMUX &= ~_BV(REFS1); break;
      case 'H': ADMUX |= _BV(REFS1); break;
    }
//    preventTrigFirstSamp = true;
    trigIgnoreFirstNSamples = 1;
  }
  if(reqPWMConfig) {
    configPWMClock();
  }
  if(reqPWMToggle) {
    if(cfg.pwmOn) pwmOn();
    else pwmOff();
  }
  if(reqSettingsPrint) printSettings();
  
  resetAcq(); // always reset the acquisition (i.e. wait for a new trigger) when parameters are modified
  inputComplete = false; // ready to receive a new input string
  enableISRs();
}

void printSettings() {
  Serial.println(); // acquisition settings
  Serial.print('-');Serial.print('-');Serial.println('-');
  Serial.print('p'); Serial.print(':'); Serial.println(cfg.sampPeriod);
  Serial.print('n'); Serial.print(':'); Serial.println(cfg.nChannels);
  Serial.print('r'); Serial.print(':'); Serial.println(cfg.adcRes);
  Serial.println(); // trigger settings
  Serial.print('t'); Serial.print(':');
    Serial.print(cfg.trigChannel); Serial.print(' ');
    Serial.print(cfg.trigMode); Serial.print(' ');
    Serial.print(cfg.trigThresh); Serial.print(' ');
    Serial.print(cfg.preTrigSamp); Serial.print(' ');
    Serial.println(cfg.acqDelay);
  Serial.print('s'); Serial.print(':'); Serial.println(cfg.nSamples);
  Serial.println(); // waveform output settings
  Serial.print('w'); Serial.print(':'); 
    cfg.pwmOn ? Serial.print('Y') : Serial.print('N'); Serial.print(' '); 
    sci_print(cfg.pwmFreq); Serial.print(' ');
    sci_print(cfg.pwmDuty); Serial.println();
  Serial.println(); // output formatting settings
  Serial.print('v'); Serial.print(':'); 
  switch(cfg.valSep) {
    case '\t': Serial.println('t'); break;
    case ' ': Serial.println('s'); break;
    default: Serial.println(cfg.valSep); break;
  }
  Serial.print('g'); Serial.print(':'); 
  cfg.graphMode ? Serial.println('Y') : Serial.println('N');
  Serial.println();
}

void toBuf(int sample, int i) { // helper function to "pack" 10-bit ADC values in the 8-bit byte buffers.
  int li = i / 4;
  int shift = (i % 4) * 2;

  sampBufL[li] &= ~(B000011 << shift);
  sampBufL[li] |= (sample & B000011) << shift; // pack the last two (low) bits of 10bit ADC sample
  sampBuf[i] = sample >> 2;
}

int fromBuf(int i) { // helper function to "unpack" 10-bit ADC values from the 8-bit byte buffers.
  int li = i / 4;
  int shift = (i % 4) * 2;

  int sample = sampBuf[i] << 2;
  sample |= (sampBufL[li] >> shift) & B000011;

  return sample;
}

void clearBuf() {
  for (int i = 0; i < maxBufSize; i++) sampBuf[i] = 0;
  for (int i = 0; i < maxBufSize / 4; i++) sampBufL[i] = 0;
}

void resetBuf() {
  clearBuf();
  bufSize = cfg.nChannels*cfg.nSamples;
  curPivot = 0;
//  preventTrigFirstSamp = true;
  trigIgnoreFirstNSamples = 1;
}

void setTrig() {
  if(trigger) return; // ignore any new triggers until we have dealt with the current one
  
  trigger = true;
  trigPivot = (curPivot + cfg.acqDelay) % bufSize; // modulo calculation because buffer is circular
  
  startPivot = trigPivot - cfg.trigChannel - cfg.preTrigSamp*cfg.nChannels;
  while (startPivot < 0) startPivot += bufSize; // modulo calculation because buffer is circular
}

void resetTrig() {
  trigger = false;
  acqDelayCnt = 0;
}

void resetAcq() {
  resetTrig();
  acqStarted = false;
  acqComplete = false;
}

void pwmOn() {
  pinMode(STROBE_PIN, OUTPUT);
  TCCR1A |= _BV(COM1A1); // turn on PWM on A1 (pin 9)
}

void pwmOff() {
  TCCR1A &= ~_BV(COM1A1); // turn off PWM
  pinMode(STROBE_PIN, INPUT);
}

// print values with 5 significant figures (more is not really warranted by 1024 bit ADC)
void sci_print(const float val) {
  if(abs(val) < 0.1 || abs(val) >= 10000) {
    dtostre(val, sci_str, 4, DTOSTR_UPPERCASE);
    Serial.print(sci_str);
   }
   else {
    Serial.print(val, 5 - ndigs(val));
   }
}

int ndigs(int val) {
  int n = 1;
  while(val /= 10) n++;
  return n;
}
