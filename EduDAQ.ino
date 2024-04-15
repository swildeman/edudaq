// EduDAQ - by Sander Wildeman 2024 (license: GPL-3.0)
//
// Information sources:
//  Analog measurements: https://meettechniek.info/embedded/arduino-analog.html
//  ADC reference: https://www.tspi.at/2021/07/15/atmegaavradc.html
//  Using Timers: https://deepbluembedded.com/arduino-timer-interrupts/
//  Timer1 settings: https://wolles-elektronikkiste.de/en/timer-and-pwm-part-2-16-bit-timer1
//  Using interrupts: https://www.gammon.com.au/interrupts
//  Pinout Arduino Uno: https://commons.wikimedia.org/wiki/File:Pinout_of_ARDUINO_Board_and_ATMega328PU.svg

#include <errno.h> // needed for error handling of string to number conversions

#define PRESCALER 64
#define PRESCALER_BITS (_BV(CS10) | _BV(CS11)) // Timer1 prescaler 64

#define enableISRs() ADCSRA |= _BV(ADIE); EIMSK |= _BV(INT0); // enable DAQ related ISRs (ADC_vect and INT0)
#define disableISRs() ADCSRA &= ~_BV(ADIE); EIMSK &= ~_BV(INT0); // disable DAQ related ISRs

//
// Initial configuration
//
struct {
  // Sampling settings
  //
  // Command: n nSamples nChannels
  // Command: p sampPeriod
  //
  int nSamples = 10; 
  int8_t nChannels = 1;
  long sampPeriod = 500; // milliseconds
  
  // Trigger settings
  //
  // Command: t trigMode trigThresh trigChannel preTrigSamp acqDelay
  //
  char trigMode = 'e'; // external e, live l, falling \, rising /, crossing x
  int8_t trigChannel = 0; // 0 - 5
  int trigThresh = 500; // in raw 10-bit ADC units 0 - 1023
  int preTrigSamp = 5; // number of samples to keep before trigger event
  int acqDelay = 0; // number of periods to delay the acquisition (multiple of 1 ms)

  // Calibration settings
  //
  // Command: c channel calMode A B C
  //
  // calMode:
  //   0 - raw
  //   P - polynomial A + B*x + C*x^2
  //   T - NTC thermistor A + B*ln(RT/R0) + C*(ln(RT/R0))^3
  //        Assuming voltage divider circuit: (5V)---[R0]---(A0)---[RT]---(GND)
  //        Example A B C values for a typical 100k NTC resistor: 3.084e-3 2.40e-4 2.79e-6
  //
  char calMode[6] = {'0','0','0','0','0','0'};
  float calPar[6][3] = {{0,1,0},{0,1,0},{0,1,0},{0,1,0},{0,1,0},{0,1,0}}; // A B C
  
  // Output settings
  //
  // Command: s valSep (any character, except t = tab, s = space)
  // Command: g graphMode
  //
  char valSep = '\t';
  bool graphMode = false; // 0, 1 - format data suitable for Serial Plotter
  
} cfg; //, input;

// buffer size based on initial configuration (make sure bufSize < maxBufSize)
int bufSize = cfg.nChannels*cfg.nSamples; // bufSize = nChannels*nSamples

const int maxBufSize = 1200;
const int maxInputStrLen = 64; // We are on a tight budget, make sure we don't go over it when a long input string is typed
const int maxAcqDelay = 2000;
const long maxSampPeriod = 900000L; // max(maxAcqDelay,maxBufSize)*maxSampPeriod should be < max(long type), otherwise the timestamp might not be properly calculated
const uint8_t maxChannels = 6;
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
volatile bool preventTrigFirstSamp = true;
volatile int curPivot = 0;      // current pivot location in the circular buffer, this is where the NEXT sample will be written to
volatile int8_t curChannel = 0; // channel from which the NEXT sample will be read
volatile int trigPivot = 0;
volatile int startPivot = 0;
volatile int adcCur[maxChannels];
volatile bool trigger = false;
volatile bool acqStarted = false;
volatile bool acqComplete = false;
volatile bool sampAvail = false;

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
  preventTrigFirstSamp = true;
}

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
  // set up Timer1 (a 16 bit timer, counts from 0 to 65535)
  //
  unsigned int cyclesPerMs = F_CPU / PRESCALER / 1000; // cyclesPerMs = fcpu / prescaler / 1000
  TCCR1A = 0x00;            // normal mode (non-PWM)
  TCCR1B = PRESCALER_BITS;  // set prescaler (determines base frequency)
  TCCR1B |= _BV(WGM12);     // configure timer to reset when TCNT1 reaches value in OCR1A
  OCR1A = cyclesPerMs - 1;  // set "TOP" value where timer is reset (= sample period)
  OCR1B = cyclesPerMs - 1;  // the AD converter will be set to perform a reading when TCNT1 reaches OCR1B 
  TIMSK1 |= _BV(OCIE1B);    // enable interrupt when TCNT1 reaches OCR1B value (TIMER1_COMPB_vect is not really used, but needed to trigger ADC)
  TCNT1 = 0;                // reset Timer1 count

  //
  // set up AD converter for cont. acquisition timed by Timer1
  //
  ADMUX = 0;                          // read from analog pin A0
  ADMUX |= _BV(REFS0) | _BV(ADLAR);   // use Vcc = 5V as reference for AD conversion, left adjust

  ADCSRA = _BV(ADEN) | _BV(ADATE);    // ADC Enable, Auto Trigger Enable, Interrupt enable
  ADCSRA |= _BV(ADPS2) | _BV(ADPS1) | _BV(ADPS0); // ADC Prescaler 128 -> F_CPU/128/(13 ADC cycles) = 9.6 kHz (= max AD conversion rate)
  ADCSRB = _BV(ADTS2) | _BV(ADTS0);   // set Auto Trigger source to Timer/Counter1 Compare Match B (i.e. when TCNT1 = OCR1B)
  ADCSRA |= _BV(ADIF);                // clear ADC interrupt flag
  
  enableISRs(); // enable ADC and Ext. Int. ISR handlers

  sei(); // set global interrupt flag (e.g. Timer0/1)
}


void loop() {
   if(inputComplete) { // process input data acquired in "serialEvent" function
      processInput();
   }
   
  if(cfg.trigMode == 'l' && sampAvail) { // live mode
    disableISRs();
    for(int c=0; c<cfg.nChannels; c++) {
      if(cfg.graphMode) { Serial.print(c); Serial.print(':'); }
      if(cfg.calMode[c] != '0') {
        float val;
        switch (cfg.calMode[c]) {
          case 'P': val = calcPoly((float) adcCur[c], c); break;
          case 'T': val = calcTemp((float) adcCur[c], c); break;
        }
        sci_print(val);
      }
      else {
        Serial.print(adcCur[c]);
      }
      if(c < cfg.nChannels -1) Serial.print(cfg.valSep);
    }
    Serial.println();
    sampAvail = false;
    enableISRs();
  }
  
  if (acqComplete) { // acquisition complete after a trigger event
//    int nPreTrig =  trigPivot - startPivot;
//    while(nPreTrig<0) nPreTrig += bufSize;
//    long t = (-nPreTrig/cfg.nChannels+cfg.acqDelay)*cfg.sampPeriod;
    long t = (-cfg.preTrigSamp+cfg.acqDelay)*cfg.sampPeriod;
    
    int c = 0;
    int i = startPivot;
    do {
      if(c == 0 && !cfg.graphMode) { Serial.print(t); Serial.print(cfg.valSep); }
      if(c > 0) Serial.print(cfg.valSep);
      if(cfg.graphMode) { Serial.print(c); Serial.print(':'); }

      int s = fromBuf(i);
      if(cfg.calMode[c] != '0') {
        float val;
        switch(cfg.calMode[c]) {
          case 'P': val = calcPoly((float) s, c); break;
          case 'T': val = calcTemp((float) s, c); break;
        }
        sci_print(val);
      }
      else {
        Serial.print(s);
      }
      
      if(c == cfg.nChannels-1) {
        t += cfg.sampPeriod;
        Serial.println();
      }
      
      c = (c + 1) % cfg.nChannels;
      i = (i + 1) % bufSize;
    } while (i != startPivot);

    Serial.println();

    resetAcq();
    preventTrigFirstSamp = true;
    enableISRs(); // continue filling the buffer when samples arrive
  }
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

float calcPoly(const float val, const int8_t c) {
  return cfg.calPar[c][0] + cfg.calPar[c][1] * val + cfg.calPar[c][2] * val * val;
}

float calcTemp(float val, const int8_t c) { // using three-term Steinhart-Hart Equation
  val = val / (1024. - val); // NTC resistor is on ground side in voltage divider
  float lval = log(val);
  val = cfg.calPar[c][0] + cfg.calPar[c][1] * lval + cfg.calPar[c][2] * pow(lval,3);
  val = 1. / val;
  val = val - 273.15;
  return val;
}

void serialEvent() { // called on Arduino UNO at end of "loop()" function
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
  
  char* rest = inputString; // pointer to start of remaining unparsed string
  
  while(rest[0] != '\0') {
    char prefix = tolower(rest[0]); // read (potential) control character

    if(prefix == '?') {
      reqSettingsPrint = true; 
      rest++;
      continue;
    }

    rest++; while(isspace(rest[0])) rest++; if(rest[0] == '\0') break;
    
    if(prefix == 't') { // trigMode trigThresh trigChannel preTrigSamp acqDelay
      char mode = tolower(rest[0]);
      switch(mode) {
        case 'x': case '\\': case '/': case 'e': case 'l':
          cfg.trigMode = mode; rest++; break;
      }
      long val;
      strtol_or_cont(val, rest); cfg.trigThresh = constrain(val,0,1023);
      strtol_or_cont(val, rest); cfg.trigChannel = constrain(val, 0, maxChannels-1);
      strtol_or_cont(val, rest); preTrigSamp = val; // constraints dealth with later
      strtol_or_cont(val, rest); cfg.acqDelay = constrain(val,0,maxAcqDelay);
    }
    else if(prefix == 's') { // value separator
      char sep = rest[0];
      switch(sep) {
        case 't': cfg.valSep = '\t'; rest++; break;
        case 's': cfg.valSep = ' '; rest++; break;
        default: cfg.valSep = sep; rest++;
      }
    }
    else if(prefix == 'c') { // calibration settings: channel, mode, A, B, C
      long chan;
      strtol_or_cont(chan, rest);
      
      if(chan >= 0 && chan < maxChannels) {
        while(isspace(rest[0])) rest++; if(rest[0] == '\0') break;
        char mode = toupper(rest[0]);
        switch(mode) {
          case '0': case 'P': case 'T':
            cfg.calMode[chan] = mode; rest++;
            float p;
            for(int i = 0; i<3; i++) {
              strtod_or_cont(p, rest);
              cfg.calPar[chan][i] = p;
            }
          break;
        }
      }
    }
    else {
      long val;
      strtol_or_cont(val, rest);
      
      switch(prefix) {
        case 'n':
          nSamples = val; // constraints dealth with later
          strtol_or_cont(val, rest);
          val = constrain(val,1,maxChannels); 
          if(val != cfg.nChannels) reqResetBuf = true;
          cfg.nChannels = val; // constraints dealth with later
          break;
        case 'p': sampPeriod = val; break;
        case 'g': cfg.graphMode = constrain(val,0,1); break;
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
  if(reqSettingsPrint) printSettings();
  
  resetAcq(); // always reset the acquisition (i.e. wait for a new trigger) when parameters are modified
  inputComplete = false; // ready to receive a new input string
  enableISRs();
}

void printSettings() {
  Serial.println();
  Serial.print('-');Serial.print('-');Serial.println('-');
  Serial.print('n'); Serial.print(':'); Serial.print(cfg.nSamples); Serial.print(' '); Serial.println(cfg.nChannels);
  Serial.print('p'); Serial.print(':'); Serial.println(cfg.sampPeriod);
  Serial.print('t'); Serial.print(':'); 
    Serial.print(cfg.trigMode); Serial.print(' ');
    Serial.print(cfg.trigThresh); Serial.print(' ');
    Serial.print(cfg.trigChannel); Serial.print(' ');
    Serial.print(cfg.preTrigSamp); Serial.print(' ');
    Serial.println(cfg.acqDelay);
  Serial.println();
  Serial.print('c'); Serial.print(':'); 
  for(int i=0; i<maxChannels; i++) { Serial.print(cfg.calMode[i]); Serial.print(' '); }
  Serial.println();
  for(int i=0; i<maxChannels; i++) {
    if(cfg.calMode[i] != '0') {
      Serial.print(i); Serial.print(':');
      for(int j=0; j<3; j++) { sci_print(cfg.calPar[i][j]); Serial.print(' '); }
      Serial.println();
    }
  }
  Serial.println();
  Serial.print('s'); Serial.print(':'); 
  switch(cfg.valSep) {
    case '\t': Serial.println('t'); break;
    case ' ': Serial.println('s'); break;
    default: Serial.println(cfg.valSep); break;
  }
  Serial.print('g'); Serial.print(':'); Serial.println(cfg.graphMode);
  Serial.println();
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

EMPTY_INTERRUPT (TIMER1_COMPB_vect); // dumy COMPB interrupt handler to make sure ISR(ADC_vect) gets triggered according to ADC setup

// External interrupt triggered when PIN 2 is pulled low (falling edge)
ISR(INT0_vect)
{
  if(cfg.trigMode == 'e') setTrig();
}

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

  if(preventTrigFirstSamp) {
    adcPrev = adcCur[curChannel];
    if(curChannel == cfg.nChannels-1) preventTrigFirstSamp = false;
  }

  if(curChannel == cfg.trigChannel) {
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
