/*************************************************** 
  This is code to operate the Infinity Mirror

  The clock displays lights between a mirror and partial mirror
  to give the effect of lights going to infinity.  The hour is shown
  on one LED, and the minute on another (or same) LED.

  Written to work with the Arduino 101
  
  Pins used:
  4, 5, 6 by the LED drivers
  8, 9, 10, 11 switch input to set clock
  A0 by the light sensor
  SDA & SCL by the clock

  Harware used:
  TLC5947 24 channel LED driver - configured as 3 bit channels
  PCF8523 Real Time Clock
  GA1A1S202WP Analog light sensor
  
  ****************************************************/
// #define DEBUG  /*  uncomment to get debug output  */

#include "Adafruit_TLC5947.h"
#include <Wire.h>
#include "RTClib.h"
#include "CurieTimerOne.h"
#include "Bounce2.h"

// How many boards do you have chained?
#define NUM_TLC5974 2
#define MAX_LIGHT 12
#define R_MAX_LIGHT 11    // one less than MAX_LIGHT for reverse loop

#define MAX_COLOR 4095  //  Maximum value for color intensity
#define MIN_FACTOR 0.05   //  Minimum factor for dimming MAX_COLOR

#define data   4
#define clock   5
#define latch   6
#define oe  -1  // set to -1 to not use the enable pin (its optional)

Adafruit_TLC5947 tlc = Adafruit_TLC5947(NUM_TLC5974, clock, data, latch);

#define lightSensorPin A0
#define seedSource A1

uint8_t globalHourLight;
uint8_t globalMinuteLight;
uint8_t globalMinuteAltLight;

RTC_PCF8523 rtc;

uint16_t maxColor;
float dimFactor;

#define RWIPE 0
#define RWIPECCW 1
#define GWIPE 2
#define GWIPECCW 3
#define BWIPE 4
#define BWIPECCW 5
#define YFADE 6
#define AFADE 7
#define MFADE 8
#define RFADE 9
#define GFADE 10
#define BFADE 11
#define WHEEL1 12
#define WHEEL2 13
#define WHEEL3 14
#define WHEEL4 15
#define BACKGROUND_MAX 16

#define WIPE_DELAY_MIN 3000   /*  Min and Max delay after doing a "wipe" */
#define WIPE_DELAY_MAX 6000
#define FADE_DELAY 1
#define FADE_STEP 80

#define bounceInterval 20 //ms bounce interval for switch debouncing

#define buttonMinute 8
Bounce bouncedButtonMinute = Bounce();
#define buttonFiveMinute 9
Bounce bouncedButtonFiveMinute = Bounce();
#define buttonHour 10
Bounce bouncedButtonHour = Bounce();
#define buttonTimeSet 11

bool request_t = false;

#ifdef DEBUG
int lastMinute;
#endif

int timeSwap[4][5] = {{1,1,1,1,0},{1,1,1,0,0},{1,1,0,0,0},{1,0,0,0,0}};

uint16_t lightArray[12][3]; // [0 - 11 hours][ red green blue ]
#define subRed 0
#define subGreen 1
#define subBlue 2

#define wipeClockWise true
#define wipeCounterClockWise false

void timeSet() {
//  request = TIME_SET;
    request_t = true;
}


void incTime(void);  // updates time depending on buttons pushed
void retrieveTime ( void);  // Obtains current hour and minute
void lightHour (uint8_t lit_hour);  // sets LED for specified hour
void lightMinute (uint8_t lit_min, uint8_t lit_sec);  // sets LEDs for specified minute
void lightTime (void);  // sets LEDs for current hour and minute
void colorCLear(void);  // turns off all LEDs

void setup() {
  #ifdef DEBUG
  Serial.begin(9600);
//  while(!Serial); // wait for Serial port to enumerate, need for Native USB based boards only
  #endif
  
  // Set up clock change pins used for interrupts
  pinMode(buttonMinute, INPUT);
  bouncedButtonMinute.attach(buttonMinute);
  bouncedButtonMinute.interval(bounceInterval);

  pinMode(buttonFiveMinute, INPUT);
  bouncedButtonFiveMinute.attach(buttonFiveMinute);
  bouncedButtonFiveMinute.interval(bounceInterval);

  pinMode(buttonHour, INPUT);
  bouncedButtonHour.attach(buttonHour);
  bouncedButtonHour.interval(bounceInterval);

  pinMode(buttonTimeSet, INPUT);
  attachInterrupt(buttonTimeSet, timeSet, RISING);
  request_t = false;
  
  pinMode(seedSource, INPUT);
  randomSeed(analogRead(seedSource));
  
  rtc.begin();
  
  #ifdef DEBUG
  Serial.println("clock started");
  Serial.println("TLC5974 test");
  #endif
  
  tlc.begin();
  if (oe >= 0) {
    pinMode(oe, OUTPUT);
    digitalWrite(oe, LOW);
  }

  maxColor = 4096;
  for(uint8_t i=0; i<MAX_LIGHT; i++) {
    tlc.setLED(i, MAX_COLOR, 0, 0);    // Red test
    tlc.write();
  }
  delay(4000);
  for(uint8_t i=0; i<MAX_LIGHT; i++) {
    tlc.setLED(i, 0, MAX_COLOR, 0);    // Green test
    tlc.write();
  }
  delay(4000);
  for(uint8_t i=0; i<MAX_LIGHT; i++) {
    tlc.setLED(i, 0, 0, MAX_COLOR);    // Blue test
    tlc.write();
  }
  delay(4000);
}

/********************************/
/*                              */
/*            LOOP              */
/*                              */
/********************************/

void loop() {
  
  int rawValue = analogRead(lightSensorPin);
  /*  calculate dimming factor based on light sensor, and truncate to a range of 0.3 to 1.0  */    
  dimFactor = (float) rawValue/512.0;
  if (dimFactor > 1.0) dimFactor = 1.0;
  if (dimFactor < MIN_FACTOR) dimFactor = MIN_FACTOR;
  maxColor = MAX_COLOR * dimFactor;
    
  #ifdef DEBUG
    Serial.print("light sensor raw = ");
    Serial.print(rawValue);
    Serial.print("  factor = ");
    Serial.print(dimFactor);
    Serial.print("  maxColor = ");
    Serial.println(maxColor);
  #endif
  
  if (request_t) {
    incTime();
  } else {
    doBackground();    
  }
}

void doBackground(void) {
  uint8_t background = random(BACKGROUND_MAX);
  uint16_t wipeDelay = random(WIPE_DELAY_MIN, WIPE_DELAY_MAX);
  switch (background) {
    case RWIPE:
      colorWipe(maxColor, 0, 0, wipeDelay, wipeClockWise); // "Red" (depending on your LED wiring)
      break;
    case RWIPECCW:
      colorWipe(maxColor, 0, 0, wipeDelay, wipeCounterClockWise); // "Red" (depending on your LED wiring)
      break;
    case GWIPE:
      colorWipe(0, maxColor, 0, wipeDelay, wipeClockWise); // "Green" (depending on your LED wiring)
      break;
    case GWIPECCW:
      colorWipe(0, maxColor, 0, wipeDelay, wipeCounterClockWise); // "Green" (depending on your LED wiring)
      break;
    case BWIPE:
      colorWipe(0, 0, maxColor, wipeDelay, wipeClockWise); // "Blue" (depending on your LED wiring)
      break;
    case BWIPECCW:
      colorWipe(0, 0, maxColor, wipeDelay, wipeCounterClockWise); // "Blue" (depending on your LED wiring)
      break;
    case YFADE:
      colorFade(maxColor/2, maxColor/2, 0); // "Yellowish" (depending on your LED wiring)
      break;
    case AFADE:
      colorFade(0, maxColor/2, maxColor/2); // "Aqua" (depending on your LED wiring)
      break;
    case MFADE:
      colorFade(maxColor/2, 0, maxColor/2); // "Magenta" (depending on your LED wiring)
      break;
    case RFADE:
      colorFade(maxColor, 0, 0); // "Red" (depending on your LED wiring)
      break;
    case GFADE:
      colorFade(0, maxColor, 0); // "Green" (depending on your LED wiring)
      break;
    case BFADE:
      colorFade(0, 0, maxColor); // "Blue" (depending on your LED wiring)
      break;
    case WHEEL1:   /*  do WHEEL more often */
    case WHEEL2:
    case WHEEL3:
    case WHEEL4:
      rainbowCycle(10);
      break;
  }
}


// Fill the dots one after the other with a color
void colorWipe(uint16_t r, uint16_t g, uint16_t b, uint8_t wait, boolean wipeDirection) {
  uint8_t this_light;
  #ifdef DEBUG
    Serial.print ("colorWipe maxColor = ");
    Serial.print (maxColor);
    Serial.print ("  wipeDirection = ");
    Serial.print (wipeDirection);
    Serial.print("  r/g/b = ");
    Serial.print(r);
    Serial.print("/");
    Serial.print(g);
    Serial.print("/");
    Serial.println(b);
  #endif
  for(uint8_t i=0; i<MAX_LIGHT; i++) {
    if ( ! wipeDirection) { 
      this_light = R_MAX_LIGHT - i;
    } else {
      this_light = i;
    }
    if ((this_light != globalHourLight) && (this_light != globalMinuteLight)) {
      tlc.setLED(this_light, r, g, b);
      lightArray[this_light][subRed] = r;
      lightArray[this_light][subGreen] = g;
      lightArray[this_light][subBlue] = b;
    }
    lightTime();
    tlc.write();
    if (request_t) break;
    delay(wait);
    /*
    #ifdef DEBUG
      Serial.print("ColorWipe i = ");
      Serial.print(i);
      Serial.print("  this_light = ");
      Serial.print(this_light);
      Serial.print("  r/g/b = ");
      Serial.print(r);
      Serial.print("/");
      Serial.print(g);
      Serial.print("/");
      Serial.println(b);
    #endif 
    */ 
  }
}
// Fade a color in and back out
void colorFade(uint16_t r, uint16_t g, uint16_t b) {
  uint16_t rinc = r/FADE_STEP;
  uint16_t ginc = g/FADE_STEP;
  uint16_t binc = b/FADE_STEP;
  uint16_t rsub = 0;
  uint16_t gsub = 0;
  uint16_t bsub = 0;
  
  for (uint8_t j=0; j<FADE_STEP; j++) {
    for(uint8_t i=0; i<MAX_LIGHT; i++) {
      if ((i != globalHourLight) && (i != globalMinuteLight)) {
        tlc.setLED(i, rsub, gsub, bsub);
        lightArray[i][subRed] = rsub;
        lightArray[i][subGreen] = gsub;
        lightArray[i][subBlue] = bsub;
      }
      lightTime();
      tlc.write();
      if (request_t) break;
    }
    rsub = rsub +rinc;
    gsub = gsub + ginc;
    bsub = bsub + binc;
  }
  for (uint8_t j=0; j<FADE_STEP-1; j++) {
    for(uint8_t i=0; i<MAX_LIGHT; i++) {
      if ((i != globalHourLight) && (i != globalMinuteLight)) {
        tlc.setLED(i, rsub, gsub, bsub);
        lightArray[i][subRed] = rsub;
        lightArray[i][subGreen] = gsub;
        lightArray[i][subBlue] = bsub;
      }
      lightTime();
      tlc.write();
      if (request_t) break;
    }
    rsub = rsub - rinc;
    gsub = gsub - ginc;
    bsub = bsub - binc;
  }
}

void colorClear(void) {
  for(uint8_t i=0; i<MAX_LIGHT; i++) {
    tlc.setLED(i, 0, 0, 0);
    lightArray[i][subRed] = 0;
    lightArray[i][subGreen] = 0;
    lightArray[i][subBlue] = 0;
    lightTime();
    tlc.write();
  }
}

// Slightly different, this makes the rainbow equally distributed throughout
void rainbowCycle(uint8_t wait) {
  uint32_t i, j;
  #ifdef DEBUG
    Serial.println ("rainbowCycle **********");
  #endif

  for(j=0; j<4096; j += 6) { // cycle through colors on wheel
    #ifdef DEBUG
      Serial.print ("j = ");
      Serial.println (j);
    #endif
    for(i=0; i< MAX_LIGHT; i++) {
      if ((i != globalHourLight) && (i != globalMinuteLight)) {
        Wheel(i, ((i * 4096 / (MAX_LIGHT)) + j) & 4095);
      }
      if (request_t) break;
    }
    if (request_t) break;
    lightTime();
    tlc.write();
    delay(wait);
  }
}

// Input a value 0 to 4095 to get a color value.
// The colours are a transition r - g - b - back to r.
void Wheel(uint8_t ledn, uint16_t WheelPos) {
  int r, g, b;
  if(WheelPos < 1365) {
    r = 3*WheelPos;
    g = 4095 - 3*WheelPos;
    b = 0;
  } else if(WheelPos < 2731) {
    WheelPos -= 1365;
    r = 4095 - 3*WheelPos;
    g = 0;
    b = 3*WheelPos;
  } else {
    WheelPos -= 2731;
    r = 0;
    g = 3*WheelPos;
    b = 4095 - 3*WheelPos;
  }
  r = (float) r * dimFactor;
  g = (float) g * dimFactor;
  b = (float) b * dimFactor;
  tlc.setLED(ledn, r, g, b);
  lightArray[ledn][subRed] = r;
  lightArray[ledn][subGreen] = g;
  lightArray[ledn][subBlue] = b;
  #ifdef DEBUG
    if (ledn==0) {
      Serial.print ("wheelpos = ");
      Serial.print (WheelPos);
      Serial.print ("  r = ");
      Serial.print (r);
      Serial.print ("  g = ");
      Serial.print (g);
      Serial.print ("  b = ");
      Serial.println (b);
    }
  #endif
}

void incTime (void)
{
  colorClear(); // Clear lights
  
  DateTime adj = rtc.now();
  uint16_t yr = adj.year();
  uint8_t mth = adj.month();
  uint8_t dy = adj.day();
  uint8_t hh = adj.hour();
  uint8_t mm = adj.minute();
  uint8_t ss = adj.second();
  
  bouncedButtonMinute.update();
  bouncedButtonFiveMinute.update();
  bouncedButtonHour.update();
  
  if ( bouncedButtonMinute.rose() ){
    #ifdef DEBUG
    Serial.println("minute button"); 
    #endif
    mm = mm + 1;
    if (mm > 59) { mm = 0; }
  }  
  if ( bouncedButtonFiveMinute.rose() ){
      #ifdef DEBUG
      Serial.println("5 minute button");
      #endif
      mm = (mm / 5) * 5 + 5;  // insure mm is multiple of 5
      if (mm > 59) { mm = 0; }
  }
  if ( bouncedButtonHour.rose() ){
      #ifdef DEBUG
      Serial.println("hour button");
      #endif   
      hh = hh + 1;
      if (hh > 23) { hh = 0; }
  }
  lightHour (hh, 0);
  lightMinute (mm, ss);
  tlc.write();
  rtc.adjust(DateTime(yr, mth, dy, hh, mm, ss));
  if (! digitalRead(buttonTimeSet)) request_t = false;
}

void lightTime(void) {
  DateTime now = rtc.now();
  uint8_t hh = now.hour();
  uint8_t mm = now.minute();
  uint8_t ss = now.second();
  lightMinute( mm, ss );  
  lightHour( hh, ss );
}

void lightHour (uint8_t lit_hour, uint8_t lit_sec) {
  if (lit_hour > 11) {
    lit_hour = lit_hour - 12;
  }
     tlc.setLED(lit_hour,maxColor,maxColor,maxColor);
  globalHourLight = lit_hour;
}

/*     
 *   Routine to light minute LED, and seemlessly move from on to the next 
 *   
 *  
 */
void lightMinute (uint8_t lit_min, uint8_t lit_sec) {
  uint16_t min_fract = lit_min % 5;
  int this_min = lit_min / 5;
  
  globalMinuteLight = this_min;    
  if (min_fract > 2) {
    if (lit_sec > 29) {
      int next_min = this_min + 1;
      if (next_min == 12) next_min = 0;
      globalMinuteLight = next_min;
    }
  }
  if (globalHourLight != globalMinuteLight) tlc.setLED(globalMinuteLight,maxColor,0,0); 

  #ifdef DEBUG
  if (lit_sec != lastMinute) {
    Serial.print("lit_min = ");
    Serial.print(lit_min);
    Serial.print("  min_fract = ");
    Serial.print(min_fract);
    Serial.print("  globalMinuteLight = ");
    Serial.println(globalMinuteLight);
    //Serial.println(digitalRead(buttonTimeSet));
    lastMinute = lit_sec; 
  }  
  #endif
}



