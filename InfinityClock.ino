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
//#define DEBUG  /*  uncomment to get debug output  */
//#define DEBUGA  /*  uncomment to get debug output  */

#include "Adafruit_TLC5947.h"
#include <Wire.h>
#include "RTClib.h"
#include "CurieTimerOne.h"
#include "Bounce2.h"
#include <math.h>

// How many boards do you have chained?
#define NUM_TLC5974 2
#define MAX_LIGHT 12
#define R_MAX_LIGHT 11    // one less than MAX_LIGHT for reverse loop

#define MAX_COLOR 4095  //  Maximum value for color intensity
#define MIN_FACTOR 0.03   //  Minimum factor for dimming MAX_COLOR

#define data   4
#define clock   5
#define latch   6
#define oe  -1  // set to -1 to not use the enable pin (its optional)

Adafruit_TLC5947 tlc = Adafruit_TLC5947(NUM_TLC5974, clock, data, latch);

#define lightSensorPin A0
#define seedSource A1

uint8_t globalHourLight;
uint8_t globalMinuteLight;

RTC_PCF8523 rtc;

uint16_t maxColor;
float dimFactor;

#define WIPE 0
#define CHASE 1
#define FADE 2
#define WHEEL 3
#define POPCORN 4
#define BACKGROUND_MAX 5

#define WIPE_DELAY_MIN 150   /*  Min and Max delay after doing a "wipe" */
#define WIPE_DELAY_MAX 300
#define FADE_DELAY 1
#define FADE_STEP 40
#define FADE_STEP_WIPE 80
#define POP_FADE_LOOPS 200
#define TEST_DELAY 1000
#define bounceInterval 20 //ms bounce interval for switch debouncing

#define buttonMinute 8
Bounce bouncedButtonMinute = Bounce();
#define buttonFiveMinute 9
Bounce bouncedButtonFiveMinute = Bounce();
#define buttonHour 10
Bounce bouncedButtonHour = Bounce();
#define buttonTimeSet 11

bool request_t = false;

class rgbLight {
  public:
    rgbLight(){
    }
    rgbLight(uint16_t max_value){
      r_value = 0;
      g_value = 0;
      b_value = 0;
      mx_val = max_value;
      dim_factor = 1.0;
    }
    rgbLight(uint16_t max_value, uint16_t r, uint16_t g, uint16_t b){
      r_value = r;
      g_value = g;
      b_value = b;
      mx_val = max_value;
      dim_factor = 1.0;
    }
    void randomize(){
      uint16_t colorVal[3];
      uint8_t primary = random(3);
      uint8_t secondary = random(2) + primary + 1;
      if (secondary > 2) {
        secondary -= 3;
      }
      colorVal[0] = 0;
      colorVal[1] = 0;
      colorVal[2] = 0;
      colorVal[primary] = random(mx_val);
      colorVal[secondary] = mx_val - colorVal[primary];
      r_value = colorVal[0];      
      g_value = colorVal[1];      
      b_value = colorVal[2];      
    }
    void set_dim(float dim_fact){
      dim_factor = dim_fact;
    }
    void set_max(float max_value){
      mx_val = max_value;
    }
    void set(uint16_t r, uint16_t g, uint16_t b){
      r_value = r;
      g_value = g;
      b_value = b;
    }
    boolean test_set() {
      return (r_value > 0 || g_value > 0 || b_value > 0);
    }
    void off() {
      r_value = 0;
      g_value = 0;
      b_value = 0;
    }
    void set(rgbLight old){
      r_value = old.r();
      g_value = old.g();
      b_value = old.b();
    }
    uint16_t r(){
      return (float) r_value * dimFactor;
    }
    uint16_t g(){
      return (float) g_value * dimFactor;
    }
    uint16_t b(){
      return (float) b_value * dimFactor;
   }
  private:
    uint16_t r_value;
    uint16_t g_value;
    uint16_t b_value;
    uint16_t mx_val;
    float dim_factor;
    
};


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
void getColor(void);  //  Generate a random color into the colorValue array

void setup() {
  #ifdef DEBUG
  Serial.begin(9600);
//  while(!Serial); // wait for Serial port to enumerate, need for Native USB based boards only
  #endif
//  Serial.begin(9600);
  
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
  delay(TEST_DELAY);
  for(uint8_t i=0; i<MAX_LIGHT; i++) {
    tlc.setLED(i, 0, MAX_COLOR, 0);    // Green test
    tlc.write();
  }
  delay(TEST_DELAY);
  for(uint8_t i=0; i<MAX_LIGHT; i++) {
    tlc.setLED(i, 0, 0, MAX_COLOR);    // Blue test
    tlc.write();
  }
  delay(TEST_DELAY);
}

/********************************/
/*                              */
/*            LOOP              */
/*                              */
/********************************/

void loop() {

  rgbLight light(MAX_COLOR);
  int rawValue = analogRead(lightSensorPin);
  /*  calculate dimming factor based on light sensor, and truncate to a range of 0.3 to 1.0  */    
  dimFactor = (float) rawValue/400.0;
  if (dimFactor > 1.0) dimFactor = 1.0;
  if (dimFactor < MIN_FACTOR) dimFactor = MIN_FACTOR;
  maxColor = MAX_COLOR * dimFactor;
    
  #ifdef DEBUG
    Serial.print("light sensor raw = ");
    Serial.print(rawValue);
    Serial.print("  factor = ");
    Serial.print(dimFactor);
    Serial.print("  maxColor = ");
    Serial.print(maxColor);
    Serial.print("  log(rawValue) = ");
    Serial.print(log(rawValue));
    Serial.print("  log(factor) = ");
    Serial.println(log(dimFactor));
  #endif

  light.set(MAX_COLOR, MAX_COLOR, MAX_COLOR);
  Serial.println("color tests:");
  Serial.print(light.r());   Serial.print(" after dim: ");
  light.set_dim(dimFactor);
  Serial.println(light.r());   Serial.println("  ");

  if (request_t) {
    incTime();
  } else {
    doBackground(light);    
  }
  // while(1);  // for one cycle only
}

void doBackground(rgbLight lt) {
  uint8_t background = random(BACKGROUND_MAX);
//    background = POPCORN;
//    background = CHASE;
  uint16_t wipeDelay = random(WIPE_DELAY_MIN, WIPE_DELAY_MAX);
  uint8_t wipeDirection = random(2);
  
  lt.randomize();
  #ifdef DEBUG
  Serial.print("r = "); Serial.print(lt.r());
  Serial.print("  g = "); Serial.print(lt.g());
  Serial.print("  b = "); Serial.println(lt.b());
  #endif
  switch (background) {
      break;
    case WIPE:
      colorWipe(lt, wipeDelay, wipeDirection); 
      break;
    case FADE:
      colorFade(lt); 
      break;
    case CHASE:
      colorChase(lt, wipeDelay, maxColor, wipeDirection);
      break;
    case WHEEL:
      rainbowCycle(10, dimFactor);
      break;
    case POPCORN:
      popcorn(wipeDelay, maxColor);
      break;
  }
  delay(1000);
}

/*
 * 
 *    colorWipe  --  wipe a color in, then fade away
 *    
 */

// Fill the dots one after the other with a color
void colorWipe(rgbLight lt, uint16_t wait, boolean wipeDirection) {
  uint8_t this_light;
  uint16_t rinc = lt.r()/FADE_STEP_WIPE;
  uint16_t ginc = lt.g()/FADE_STEP_WIPE;
  uint16_t binc = lt.b()/FADE_STEP_WIPE;
  uint16_t fade_r = lt.r();
  uint16_t fade_g = lt.g();
  uint16_t fade_b = lt.b();
  #ifdef DEBUG
    Serial.print ("colorWipe maxColor = ");
    Serial.print (maxColor);
    Serial.print ("  wipeDirection = ");
    Serial.print (wipeDirection);
    Serial.print("  r/g/b = ");
    Serial.print(lt.r());
    Serial.print("/");
    Serial.print(lt.g());
    Serial.print("/");
    Serial.println(lt.b());
  #endif
  for(uint8_t i=0; i<MAX_LIGHT; i++) {
    if ( ! wipeDirection) { 
      this_light = R_MAX_LIGHT - i;
    } else {
      this_light = i;
    }
    if ((this_light != globalHourLight) && (this_light != globalMinuteLight)) {
      tlc.setLED(this_light, lt.r(), lt.g(), lt.b());
    }
    lightTime();
    tlc.write();
    if (request_t) break;
    delay(wait);
  }
  delay(random(2000,4000));
  #ifdef DEBUG
    Serial.println("begin delay now");
    Serial.print("  ");
    Serial.println(rinc);
  #endif
  for(uint8_t i = 0; i < FADE_STEP_WIPE; i++) {
    fade_r -= rinc;
    fade_g -= ginc;
    fade_b -= binc;
    for (this_light = 0; this_light < MAX_LIGHT; this_light++) {
      if ((this_light != globalHourLight) && (this_light != globalMinuteLight)) {
        tlc.setLED(this_light, fade_r, fade_g, fade_b);
        #ifdef DEBUG
          if (((i % 5) == 0) && (this_light == 6)) {
            Serial.println("delay now");
            Serial.print(i);
            Serial.print("  ");
            Serial.print("  ");
            Serial.println(rinc);
          }
        #endif
      }
    }
    lightTime();
    tlc.write();
    if (request_t) break;
    delay(15);
  }
  colorClear();
}

/*
 * 
 *   popcorn -- randomly place colors around the clock
 * 
 */

void popcorn(uint16_t wait, uint16_t local_max) {
   #ifdef DEBUG
     Serial.println("in popcorn ");
   #endif
   rgbLight ccl[MAX_LIGHT];
   for (int i = 0; i < MAX_LIGHT; i++){
     ccl[i].off();
     ccl[i].set_max(local_max);
   }
   colorClear();  // insure all lights (except hour and minute) are dark
   #ifdef DEBUG
     Serial.println("  after colorClear() ");
   #endif
   for (int i = 0; i < MAX_LIGHT; i++) {
     int lno = random(MAX_LIGHT);
     #ifdef DEBUG
      Serial.print("1st lno = ");Serial.println(lno);
     #endif
     while (ccl[lno].test_set()) {
       lno = random(MAX_LIGHT);
       #ifdef DEBUG
        Serial.print("secondary lno = ");Serial.println(lno);
       #endif
     }
     ccl[lno].randomize();
     #ifdef DEBUG
      Serial.print("desired lno = ");Serial.println(lno);
      Serial.print(" r = ");Serial.print( ccl[lno].r());Serial.print(" g = ");Serial.print(ccl[lno].g());Serial.print(" b = ");Serial.println(ccl[lno].b());
     #endif
     illumenLight(ccl[lno], lno);
     lightTime();
     tlc.write();
     if (request_t) break;
     delay (wait*2);
   }
   delay(2000);
   uint16_t popFadeDec[MAX_LIGHT][3];
   #define POP_DEC_FACT 0.0001
   for (int i = 0; i < MAX_LIGHT; i++) {
    popFadeDec[i][0] = max(1, (float)ccl[i].r() * POP_DEC_FACT);
    popFadeDec[i][1] = max(1, (float)ccl[i].g() * POP_DEC_FACT);
    popFadeDec[i][2] = max(1, (float)ccl[i].b() * POP_DEC_FACT);
//      Serial.print("popFadeDec [i] [rgb] = ");Serial.print(i);Serial.print("  "); Serial.print(ccl[i].r());
//      Serial.print("  ");Serial.print(popFadeDec[i][0]);Serial.print("  "); Serial.print(ccl[i].g());Serial.print("  ");Serial.print(popFadeDec[i][1]);Serial.print("  ");
//      Serial.print(popFadeDec[i][2]);Serial.print("  "); Serial.print(ccl[i].b());Serial.println("  ");
   #ifdef DEBUG
    if (i == 1 ) {
      Serial.print("popFadeDec [i] [rgb] = ");Serial.print(i);
      Serial.print("  ");Serial.print(popFadeDec[i][0]);Serial.print("  ");Serial.print(popFadeDec[i][1]);Serial.print("  ");
      Serial.print(popFadeDec[i][2]);Serial.println("  ");
    }
   #endif
   }
   for(uint16_t i = 0; i < POP_FADE_LOOPS; i++) {
    int set_count = 0;
    for (int wl = 0; wl < MAX_LIGHT; wl++) {
//      if ((wl != globalHourLight) && (wl != globalMinuteLight)) {
        ccl[wl].set(decLight(ccl[wl].r(),popFadeDec[wl][0]), decLight(ccl[wl].g(),popFadeDec[wl][1]), decLight(ccl[wl].b(),popFadeDec[wl][2]));
        illumenLight(ccl[wl], wl);
        if ( ! ccl[wl].test_set()) set_count++;
        #ifdef DEBUG
          Serial.print("  set_count =  ");Serial.println(set_count);
        #endif
 //     }
       #ifdef DEBUG
       if (wl == 1 ) {
          Serial.print("iteration i  = ");Serial.print(i);
          Serial.print("  ccl [wl] [rgb] = ");Serial.print(wl);
          Serial.print("  ");Serial.print(ccl[wl].r());Serial.print("  ");Serial.print(ccl[wl].g());Serial.print("  ");
          Serial.print(ccl[wl].b());Serial.print("  set_count =  ");Serial.println(set_count);
       }
       #endif
    }
    if (set_count >= (MAX_LIGHT - 2)) break;  // once all lights are at zero, quit
    lightTime();
    tlc.write();
    if (request_t) break;
    delay(90);
  }
  colorClear();
}

uint16_t decLight (uint16_t popVal, uint16_t popDec) {
  if (popVal > popDec) {
    return popVal - popDec;
  } else {
    return 0;
  }
}

/*
 * 
 *      colorChase  --  Chase a color around the clock
 * 
 */
 
// chase a color around the clock
void colorChase(rgbLight lt, uint16_t wait, uint16_t local_max, boolean wipeDirection) {
  #define CHASE_FACTOR 0.008
  #define CHASE_CYCLES 5
  uint16_t rinc = (float)lt.r() * CHASE_FACTOR;
  uint16_t ginc = (float)lt.g() * CHASE_FACTOR;
  uint16_t binc = (float)lt.b() * CHASE_FACTOR;
//    Serial.print("lt values = "); Serial.print(lt.r()); Serial.print("  "); Serial.print(lt.g());  Serial.print("  ");Serial.println(lt.b());
//    Serial.print("float     = "); Serial.print(rinc); Serial.print("  "); Serial.print(ginc);  Serial.print("  ");Serial.println(binc);
//    Serial.print("divide    = "); Serial.print(rincx); Serial.print("  "); Serial.print(gincx);  Serial.print("  ");Serial.println(bincx);
   rgbLight ccl[MAX_LIGHT];
   for (int i = 0; i < MAX_LIGHT; i++){
     ccl[i].off();
     ccl[i].set_max(local_max);
   }
   colorClear();  // insure all lights (except hour and minute) are dark
  
  //  Determine starting light to "chase" around the clock

  uint16_t aLight = random(MAX_LIGHT);
  while ((aLight == globalHourLight) || (aLight == globalMinuteLight)) {
    aLight = random(MAX_LIGHT);
    }
  // Loop around the clock starting lights
  for(uint8_t i=0; i<MAX_LIGHT*CHASE_CYCLES; i++) {
    if (i < MAX_LIGHT*(CHASE_CYCLES-1)) ccl[aLight].set(lt);
    #ifdef DEBUG
        Serial.print("aLight = "); Serial.println(aLight);
    #endif

    for (int k = 0; k < 3; k++){
     for (int j = 0; j < MAX_LIGHT; j++){
        ccl[j].set( decLight(ccl[j].r(), rinc), decLight(ccl[j].g(), ginc), decLight(ccl[j].b(), binc));
        #ifdef DEBUG
          Serial.print("j = "); Serial.print(j); 
          Serial.print("  r = "); Serial.print(ccl[j].r());Serial.print("  g = "); Serial.print(ccl[j].g());Serial.print("  b = "); Serial.println(ccl[j].b());
        #endif
        illumenLight(ccl[j], j);
      illumenLight(ccl[aLight], aLight);
      lightTime();
      tlc.write();
      delay(1);
    }    // j loop
    }   // k loop
      #ifdef DEBUG
        Serial.print("aLight = "); Serial.println (aLight);
      #endif
      lightTime();
      tlc.write();
      if (request_t) break;
      delay (wait / 12);
      if ( wipeDirection ) {
        aLight = (aLight + 1) % MAX_LIGHT;
      } else {
        if (aLight == 0) {
          aLight = MAX_LIGHT - 1;
        } else {
          aLight -= 1;
        }
      }
  }  // i loop
  colorClear();
}

/*
 * 
 *   illumenLight -- set light value if not writing over the hour or minute light
 * 
 */

void illumenLight(rgbLight lt, uint8_t no) {
  if ((no != globalHourLight) && (no != globalMinuteLight)) {
    tlc.setLED(no, lt.r(), lt.g(), lt.b());
    #ifdef DEBUG
//      Serial.print("illumenLight number = "); Serial.println(no);
    #endif
  }  
}

/*
 * 
 *    colorFade -- fade a color in, then back out
 * 
 */
 
// Fade a color in and back out
void colorFade(rgbLight lt) {
  uint16_t rinc = lt.r()/FADE_STEP;
  uint16_t ginc = lt.g()/FADE_STEP;
  uint16_t binc = lt.b()/FADE_STEP;
  uint16_t rsub = 0;
  uint16_t gsub = 0;
  uint16_t bsub = 0;
  
  for (uint8_t j=0; j<(FADE_STEP); j++) {
    for(uint8_t i=0; i<MAX_LIGHT; i++) {
      if ((i != globalHourLight) && (i != globalMinuteLight)) {
        tlc.setLED(i, rsub, gsub, bsub);
      }
      lightTime();
      tlc.write();
      delay(5);
      if (request_t) break;
    }
    #ifdef DEBUG
      Serial.print(j); Serial.print("  ");
    #endif
    rsub = rsub + rinc;
    gsub = gsub + ginc;
    bsub = bsub + binc;
  }
  for (uint8_t j=0; j<FADE_STEP+5; j++) {
    for(uint8_t i=0; i<MAX_LIGHT; i++) {
      if ((i != globalHourLight) && (i != globalMinuteLight)) {
        tlc.setLED(i, rsub, gsub, bsub);
      }
      lightTime();
      tlc.write();
      delay(5);
      if (request_t) break;
    }
    rsub = decLight(rsub, rinc);
    gsub = decLight(gsub, ginc);
    bsub = decLight(bsub, binc);
  }
  colorClear();
}

/*
 * 
 *   colorClear - clear all colors to "black"
 * 
 */
 
void colorClear(void) {
  for(uint8_t i=0; i<MAX_LIGHT; i++) {
    if ((i != globalHourLight) && (i != globalMinuteLight)) {
      tlc.setLED(i, 0, 0, 0);
      lightTime();
      tlc.write();
    }
  }
}

/*
 * 
 *   rainbowCycle -- use the color wheel to set colors around the clock, then rotate it.
 * 
 */
 
// Slightly different, this makes the rainbow equally distributed throughout
void rainbowCycle(uint8_t wait, float ldf) {
  uint32_t i, j;
  float localDimFact;
  #ifdef DEBUG
    Serial.println ("rainbowCycle **********");
  #endif

  for(j=0; j<8192; j += 6) { // cycle through colors on wheel
    #ifdef DEBUG
      Serial.print ("j = ");
      Serial.println (j);
    #endif
    if (j<240) localDimFact = min((ldf * (float) j * 0.005), ldf);
    for(i=0; i< MAX_LIGHT; i++) {
      if ((i != globalHourLight) && (i != globalMinuteLight)) {
        Wheel(i, ((i * 4096 / (MAX_LIGHT)) + j) & 4095, localDimFact);
      }
      if (request_t) break;
    }
    if (j>7970) {localDimFact = localDimFact * 0.9;}  // start fading just before loop ending, 
          //  subtract 6 * fade loops from number of loops in for statement.
    if (request_t) break;
    lightTime();
    tlc.write();
    delay(wait);
  }
}

// Input a value 0 to 4095 to get a color value.
// The colours are a transition r - g - b - back to r.
void Wheel(uint8_t ledn, uint16_t WheelPos, float localDimFactor) {
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
  r = (float) r * localDimFactor;
  g = (float) g * localDimFactor;
  b = (float) b * localDimFactor;
  tlc.setLED(ledn, r, g, b);
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

/*
 * 
 *   incTime -- use the buttons to adjust the time
 * 
 */
 
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
  lightHour (hh);
  lightMinute (mm, ss);
  tlc.write();
  rtc.adjust(DateTime(yr, mth, dy, hh, mm, ss));
  if (! digitalRead(buttonTimeSet)) request_t = false;
}

void lightTime(void) {
  DateTime now = rtc.now();
  uint8_t ss = now.second();
  uint8_t hh = now.hour();
  uint8_t mm = now.minute();
  lightHour( hh );
  lightMinute( mm, ss );  
}

void lightHour (uint8_t lit_hour) {
  if (lit_hour > 11) {
    lit_hour = lit_hour - 12;
  }
     tlc.setLED(lit_hour,maxColor/2,maxColor/2,maxColor/2);
  globalHourLight = lit_hour;
}

void lightMinute (uint8_t lit_min, uint8_t lit_sec) {
  uint8_t this_min = (lit_min + 2)/ 5;
 // uint16_t min_fract = lit_min % 5;
 // int this_min = lit_min / 5;
/*  Serial.print("glob Min "); Serial.print(globalMinuteLight); 
  Serial.print("  glob Hour "); Serial.print(globalHourLight); 
  Serial.print("  lit_sec "); Serial.print(lit_sec); 
  Serial.print("  lit_sec & 3 "); Serial.println(lit_sec % 3); */
  globalMinuteLight = this_min;    
  if ((globalHourLight != globalMinuteLight) || ((lit_sec % 3) == 0)) 
      tlc.setLED(globalMinuteLight,maxColor,0,0); 
}
