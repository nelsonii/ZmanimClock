// 7MM_ZmanimClock_QTPy_SAMD21_v1B

const long buildDate = 2023070801;

// Board: Adafruit QT PY (SAMD21)
// Optimize: Faster (-O3) (default okay too)
// USB Stack: Arduino (not used)
// Debug: Off
// Port: COMxx (Adafruit QT PY (SAMD21))

// ==========================================================================

/*

REQUIRED LIBRARIES:

  DFRobot_DF1201S
    DFPlayer Pro MP3 Player for audio playback
    https://github.com/DFRobot/DFRobot_DF1201S

    IMPORTANT: CPP and H needs a patch (which doesn't help -- boo)
      CPP: uint16_t DFRobot_DF1201S::getINT(String str)
      H: uint16_t getINT(String str);

  FlashStorage
    Non-Volatile flash when EEPROM not available (ex. on the SAMD21)

  Adafruit_FreeTouch
    Capacitive Touch functionality for the SAMD21 MCU
    https://github.com/adafruit/Adafruit_FreeTouch

 Adafruit NeoNixel
   Onboard RBG LED support

  RTClib (Adafruit)
    Realtime Clock Support for DS1307, PCF8523, and DS3231 Precision
    https://github.com/adafruit/RTClib  


  Solar Position Algorithm (SPA)
    Local include files
    
*/

// ==========================================================================

#include "Adafruit_FreeTouch.h" // cap
#include <Adafruit_NeoPixel.h> // onboard RGB LED
#include <DFRobot_DF1201S.h> // MP3 Player (DFPlayer Pro)
#include <FlashStorage.h> // EEPROM-like storage for SAMD21
#include "RTClib.h" // Real Time Clock (Adafruit version)

#include "spa.h" // Local SPA (Solar) Code
#include "spa.c" // Local SPA (Solar) Code


// ==========================================================================

#define SERIAL_BAUDRATE     115200 // all the new boards can handle this speed
#define SERIAL_TIMEOUT      2000  // set to 1 sec (1000) in prod, 10 sec (10000) in dev

#define TOUCH_TIMEOUT       200  // time to allow user to press multiple buttons (for setup)

#define HEARTBEAT           60000 // typically 10000 (10 secs) to 60000 (60 secs)

#define MP3_TIMEOUT         1500   // MP3 Player track timeout (in testing)
#define MP3_BAUDRATE        115200 // default speed for MP3 Player
 

// ==========================================================================

DFRobot_DF1201S DF1201S;

RTC_PCF8523 rtc;
bool forceRTCreset = false;

// A0 = Mechanical switch, external, for default action ("big external button")
#define BUTTON_PIN A0 // external trigger / switch

//From front of unit, left to right, the touch points are A1, A2, A3
// A1 = "Volume Down" (aka setting down)
// A2 = "Default Action" (aka "speak the time")
// A3 = "Volume Up" (aka setting up)
Adafruit_FreeTouch touch_1 = Adafruit_FreeTouch(A1, OVERSAMPLE_4, RESISTOR_50K, FREQ_MODE_NONE);
Adafruit_FreeTouch touch_2 = Adafruit_FreeTouch(A2, OVERSAMPLE_4, RESISTOR_50K, FREQ_MODE_NONE);
Adafruit_FreeTouch touch_3 = Adafruit_FreeTouch(A3, OVERSAMPLE_4, RESISTOR_50K, FREQ_MODE_NONE);
int touch_1_threshold = 1000; int touch_2_threshold = 1000; int touch_3_threshold = 1000;

spa_data spa; // SPA structure
int result; // SPA return code
 
//A structure to hold the clock settings. The "valid" variable is set to "true"
//when the structure if filled with actual data for the first time.
typedef struct {
  boolean valid;
  int zipcode;
  int volume;
  int timezone;
  double longitude;
  double latitude;
  double elevation;
} ClockSetting;

//For storing a "ClockSetting" in the "clockFlashStorage"
FlashStorage(clockFlashStorage, ClockSetting);

//clockSettings is the variable holding the setting structure for runtime use.
ClockSetting defaultClockSetting;

int volume = 10; // Default volume.

// ==========================================================================

Adafruit_NeoPixel pixels = Adafruit_NeoPixel(1, PIN_NEOPIXEL);

// ==========================================================================

// NeoPixel Colors
uint32_t redLow = pixels.Color(32,0,0);
uint32_t redHigh = pixels.Color(255,0,0);
uint32_t greenLow = pixels.Color(0,32,0);
uint32_t greenHigh = pixels.Color(0,255,0);
uint32_t blueLow = pixels.Color(0,0,32);
uint32_t blueHigh = pixels.Color(0,0,255);
uint32_t pinkLow = pixels.Color(32,8,15);
uint32_t pinkHigh = pixels.Color(255,51,119);
uint32_t yellowLow = pixels.Color(32,22,0);
uint32_t yellowHigh = pixels.Color(255,170,0);
uint32_t whiteLow = pixels.Color(32,32,32);
uint32_t whiteHigh = pixels.Color(255,255,255);
uint32_t black = pixels.Color(0,0,0);
uint32_t orange = pixels.Color(255,40,0);

// Calendar/Clock Arrays

char daysOfWeek[7][12] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
char daysOfWeekFilename[7][9] = {"/sun.mp3", "/mon.mp3", "/tue.mp3", "/wed.mp3", "/thr.mp3", "/fri.mp3", "/sat.mp3"};

char monthsOfYear[12][12] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
char monthsOfYearFilename[12][9] = {"/jan.mp3", "/feb.mp3", "/mar.mp3", "/apr.mp3", "/may.mp3", "/jun.mp3", "/jul.mp3", "/aug.mp3", "/sep.mp3", "/oct.mp3", "/nov.mp3", "/dec.mp3"};

// Other calendar/clock settings

bool speakSeconds = false;
bool speakYear = false;

// ==========================================================================

void setup(void){


  //Startup the onboard NeoPixel
  pixels.begin();

  //Indicate that setup is starting
  pixels.clear(); pixels.setPixelColor(0, pinkLow); pixels.show(); delay(250);

  //Startup serial monitoring (optional)
  Serial.begin(SERIAL_BAUDRATE); // debug port

  //Wait for the serial monitor to be opened, or timeout after x seconds
  unsigned long serialTimeout = millis(); 
  while (!Serial && (millis() - serialTimeout <= SERIAL_TIMEOUT)) { delay(10); } 
  delay(200);

  Serial.print("Version:" ); Serial.println(buildDate);
  Serial.println(" Setup starting...");

  Serial.println(" ...reading Flash Settings");
  setupFlashDefaults(); // Populates defaultClockSetting

  Serial.println(" ...basic settings");
  //Set the default/startup volume based on flash setting
  volume = defaultClockSetting.volume;
  
  //External button / trigger -- WIP
  //pinMode(BUTTON_PIN, INPUT_PULLUP);
  
  Serial.println(" ...setup RTC");  pixels.clear(); pixels.setPixelColor(0, yellowLow); pixels.show();
  setupRTC();

  Serial.println(" ...setup MP3"); pixels.clear(); pixels.setPixelColor(0, blueLow); pixels.show();
  setupMP3();
  
  Serial.println(" ...setup Touch"); pixels.clear(); pixels.setPixelColor(0, orange); pixels.show();
  setupTouch();

  Serial.println(" Setup phase Complete."); pixels.clear(); pixels.setPixelColor(0, greenLow); pixels.show();
  
  //Speak welcome - Indicate that setup is complete
  DF1201S.enableAMP(); DF1201S.playSpecFile("/csbc.mp3"); delay(1000); DF1201S.disableAMP();
  actionDefault();
  
  Serial.println(" Starting main Loop...");
  
}

// ==========================================================================

void loop(){

  // I'm using flags and IF evaluations in order to handle multiple button presses.
  // Only certain sequences are allowed. And there is a delay to "sample" presses.

  bool cmdActionDefault = false;
  bool cmdVolumeUp = false;
  bool cmdVolumeDown = false;

  //if ( digitalRead(BUTTON_PIN) == HIGH) { actionDefault(); }

  // A looping delay here allows humans to press muti-buttons, but not accidently (hopefully)
  unsigned long touchTimeout = millis(); 
  while (millis() - touchTimeout <= TOUCH_TIMEOUT) {  
    if (touch_1.measure() > touch_1_threshold) { cmdVolumeDown = true;  } else { cmdVolumeDown = false; }
    if (touch_2.measure() > touch_2_threshold) { cmdActionDefault = true; } else { cmdActionDefault = false; }
    if (touch_3.measure() > touch_3_threshold) { cmdVolumeUp = true; } else { cmdVolumeUp = false; }
  }
  
  //if (cmdActionDefault && !cmdVolumeUp && !cmdVolumeDown) { speakSunrise(); } // testing
  if (cmdActionDefault && !cmdVolumeUp && !cmdVolumeDown) { actionDefault(); }
  if (!cmdActionDefault && cmdVolumeUp && !cmdVolumeDown) { volumeUp(); }
  if (!cmdActionDefault && !cmdVolumeUp && cmdVolumeDown) { volumeDown(); }
  if (!cmdActionDefault && cmdVolumeUp && cmdVolumeDown) { Serial.println("SETUP MODE"); }


  yield(); delay(1); // helpful for chips with watchdog timers, short pause to allow other tasks to catch up

  //Display available memory on a regular basis
  HeartBeat();

}

// ==========================================================================

void actionDefault() {

  Serial.println();
  Serial.println("{actionDefault");

  //Calculate SPA
  calculateSPA();

   //Dump values to serial out
  debugTouch();
  debugRTC();
  debugSPAResults();

  //Do the talking 
  pixels.clear(); pixels.setPixelColor(0, whiteHigh); pixels.show();
  
  speakDayOfWeek();
  speakDate();
  speakTime();
  speakSunrise();
  speakSunset();

  pixels.clear(); pixels.setPixelColor(0, greenLow); pixels.show();

  Serial.println("actionDefault}");

}

// ==========================================================================

void volumeUp() {

  DF1201S.enableAMP();

  int newVolume = ++volume; volume = constrain(newVolume, 1, 25);
  DF1201S.setVol(volume); 
  //DF1201S.playSpecFile("/volup.mp3"); delay(1000); 
  char fileVolume[80]; sprintf(fileVolume, "%s%02i%s", "/", volume, ".mp3");
  DF1201S.playSpecFile(fileVolume); delay(750);
  Serial.print("Volume: "); Serial.println(volume);

  DF1201S.disableAMP();

  //TODO: Save to Flash (but don't want to do this too often, or we'll wear it out)

}

// ==========================================================================

void volumeDown() {

  DF1201S.enableAMP(); 

  int newVolume = --volume; volume = constrain(newVolume, 1, 25);
  DF1201S.setVol(volume);
  //DF1201S.playSpecFile("/voldn.mp3"); delay(1000); 
  char fileVolume[80]; sprintf(fileVolume, "%s%02i%s", "/", volume, ".mp3");
  DF1201S.playSpecFile(fileVolume); delay(750);
  Serial.print("Volume: "); Serial.println(volume);

  DF1201S.disableAMP();

  //TODO: Save to Flash (but don't want to do this too often, or we'll wear it out)

}

// ==========================================================================

void speakDayOfWeek() {

  DF1201S.enableAMP(); 

  DateTime now = rtc.now();
  Serial.print(" fileDay:"); Serial.println(daysOfWeekFilename[now.dayOfTheWeek()]);
  DF1201S.playSpecFile("/todayis.mp3"); 
  delay(600);  
  DF1201S.playSpecFile(daysOfWeekFilename[now.dayOfTheWeek()]); 
  delay(1000);

  DF1201S.disableAMP();

}//speakDayOfWeek

// ==========================================================================

void speakDate() {

  DF1201S.enableAMP(); 

  DateTime now = rtc.now();

  // Month
  Serial.print(" fileMonth:"); Serial.println(monthsOfYearFilename[now.month()-1]);
  DF1201S.playSpecFile(monthsOfYearFilename[now.month()-1]); 
  delay(750);  

  // Date
  char fileDate[80]; sprintf(fileDate, "%s%02i%s", "/d", now.day(), ".mp3");
  Serial.print(" fileDate:"); Serial.println(fileDate);
  DF1201S.playSpecFile(fileDate); 
  delay(600);

  // Year
  if (speakYear) {
    char fileYear[80]; sprintf(fileYear, "%s%04i%s", "/", now.year(), ".mp3");
    Serial.print(" fileYear:"); Serial.println(fileYear);
    DF1201S.playSpecFile(fileYear); 
    delay(1500);  
  }

  delay(750);

  DF1201S.disableAMP();

}//speakDate

// ==========================================================================

void speakTime() {

  DF1201S.enableAMP(); 

  DateTime now = rtc.now();

  DF1201S.playSpecFile("/timeis.mp3"); delay(800);

  //Hours
  char fileHrs[80]; sprintf(fileHrs, "%s%02i%s", "/", now.twelveHour(), ".mp3");
  Serial.print(" fileHrs:"); Serial.println(fileHrs);
  DF1201S.playSpecFile(fileHrs);
  delay(600); 

  //Minutes
  if(now.minute()==0) {}
  if(now.minute()>0 && now.minute()<10) { DF1201S.playSpecFile("/00.mp3"); delay(500); }
  if(now.minute()>0) {
    char fileMin[80]; sprintf(fileMin, "%s%02i%s", "/", now.minute(), ".mp3");
    Serial.print(" fileMin:"); Serial.println(fileMin);
    DF1201S.playSpecFile(fileMin); 
    delay(1000); 
  }  

  //AM/PM
  if (now.isPM()) {
     Serial.println(" PM");
     DF1201S.playSpecFile("/pm.mp3"); delay(600);
  }else{
     Serial.println(" AM");
     DF1201S.playSpecFile("/am.mp3"); delay(600);
  }

  //Seconds
  if (speakSeconds) { 
    DF1201S.playSpecFile("/and.mp3"); delay(400);
    char fileSec[80]; sprintf(fileSec, "%s%02i%s", "/", now.second(), ".mp3");
    Serial.print(" fileSec:"); Serial.println(fileSec);
    DF1201S.playSpecFile(fileSec); delay(600); 
    DF1201S.playSpecFile("/seconds.mp3"); delay(500);
   }

   delay(500); 

   DF1201S.disableAMP();

}//speakTime

// ==========================================================================

void speakSunrise() {

  DF1201S.enableAMP(); 

  int riseHrs, riseMin;
  calculateSPA();   //Re-calculate SPA, in case day rolls over since last update
  riseHrs = (int) spa.sunrise;
  riseMin = (int) 60.0*(spa.sunrise - riseHrs);

  DateTime now = rtc.now();

  DF1201S.playSpecFile("/today.mp3"); delay(700);
  DF1201S.playSpecFile("/sunrise.mp3"); delay(700);

  // Is / Was
  if (now.hour()>riseHrs) { DF1201S.playSpecFile("/wasat.mp3"); }
  else { DF1201S.playSpecFile("/willbeat.mp3"); }
  delay(700);

  char fileHrs[80]; sprintf(fileHrs, "%s%02i%s", "/", riseHrs, ".mp3");
  Serial.print(" fileRiseHrs:"); Serial.println(fileHrs);
  DF1201S.playSpecFile(fileHrs);
  delay(700); 

  if(riseMin==0) { } // Don't speak minutes at top of the hour
  if(riseMin>0 && riseMin<10) { DF1201S.playSpecFile("/00.mp3"); delay(700); }
  if(riseMin>0) {
    char fileMin[80]; sprintf(fileMin, "%s%02i%s", "/", riseMin, ".mp3");
    Serial.print(" fileRiseMin:"); Serial.println(fileMin);
    DF1201S.playSpecFile(fileMin); 
    delay(700); 
  }  

  //AM/PM -- PM should never happen on a Rise, but you never know!
  if (riseHrs<12) {
     Serial.println(" AM");
     DF1201S.playSpecFile("/am.mp3"); delay(700);
  }else{
     Serial.println(" PM");
     DF1201S.playSpecFile("/pm.mp3"); delay(700);
  }

  DF1201S.disableAMP();

}

// ==========================================================================

void speakSunset() {

  DF1201S.enableAMP(); 

  int setHrs, setMin;
  calculateSPA();   //Re-calculate SPA, in case day rolls over since last update
  setHrs = (int) spa.sunset;
  setMin = (int) 60.0*(spa.sunset - setHrs);

  DateTime now = rtc.now();

  DF1201S.playSpecFile("/today.mp3"); delay(700);
  DF1201S.playSpecFile("/sunset.mp3"); delay(700);

  // Is / Was
  if (now.hour()>setHrs) { DF1201S.playSpecFile("/wasat.mp3"); }
  else { DF1201S.playSpecFile("/willbeat.mp3"); }
  delay(700);

  int twelvehour;
  if (setHrs>12) { twelvehour=setHrs-12; } else { twelvehour=setHrs; } // convert to 12 hr

  char fileHrs[80]; sprintf(fileHrs, "%s%02i%s", "/", twelvehour, ".mp3");
  Serial.print(" fileSetHrs:"); Serial.println(fileHrs);
  DF1201S.playSpecFile(fileHrs);
  delay(700); 

  if(setMin==0) { } // Don't speak minutes at top of the hour
  if(setMin>0 && setMin<10) { DF1201S.playSpecFile("/00.mp3"); delay(700); }
  if(setMin>0) {
    char fileMin[80]; sprintf(fileMin, "%s%02i%s", "/", setMin, ".mp3");
    Serial.print(" fileRiseMin:"); Serial.println(fileMin);
    DF1201S.playSpecFile(fileMin); 
    delay(700); 
  }  

  //AM/PM -- AM should never happen on a Set, but you never know!
  if (setHrs<12) {
     Serial.println(" AM");
     DF1201S.playSpecFile("/am.mp3"); delay(700);
  }else{
     Serial.println(" PM");
     DF1201S.playSpecFile("/pm.mp3"); delay(700);
  }

  DF1201S.disableAMP();
  
}

// ==========================================================================


void waitForMP3() {

  //WIP WIP WIP WIP  WIP WIP WIP WIP WIP WIP WIP WIP WIP WIP WIP WIP WIP WIP WIP WIP WIP WIP WIP WIP WIP WIP

  unsigned long mp3Timeout = millis(); 
  while ((DF1201S.getTotalTime() > DF1201S.getCurTime()) && (millis() - mp3Timeout <= MP3_TIMEOUT)) { delay(10); }
}

// ==========================================================================

void setupFlashDefaults() {

  defaultClockSetting = clockFlashStorage.read();

  if (defaultClockSetting.valid==true) {
    Serial.println("   Flash Settings Found and loaded.");
    return;
  }

  if (defaultClockSetting.valid==false) {
    Serial.println("   No Flash Settings found.");
    //Nothing in flash storage, put in some defaults
    defaultClockSetting.valid = true;
    defaultClockSetting.zipcode = 76522;
    defaultClockSetting.volume = 10;
    defaultClockSetting.timezone = -5;
    defaultClockSetting.longitude = -97.905490;
    defaultClockSetting.latitude = 31.115040;
    defaultClockSetting.elevation = 1089.00;
    //Store the new defaults in flash
    clockFlashStorage.write(defaultClockSetting);
    Serial.println("   Defaults Settings written.");
    return;    
  }
 
}//setupFlashDefaults

// ==========================================================================

void setupMP3() {

  Serial1.begin(115200); // serial port for AT coms to MP3 Player

  //Startup the MP3 player
  while(!DF1201S.begin(Serial1)){
    Serial.println(" MP3 not found! Trying again..."); delay(1000);
    pixels.clear(); pixels.setPixelColor(0, redLow); pixels.show();
  }

  DF1201S.disableAMP(); // Start with the Amp disabled
  DF1201S.setVol(volume); // Start with a low default volume
  DF1201S.setPrompt(false); // false = disables power-on prompt
  DF1201S.setLED(false); // false = disables onboard LED
  DF1201S.switchFunction(DF1201S.MUSIC); // AKA Playback mode
  DF1201S.setPlayMode(DF1201S.SINGLE); // Don't repeat
  
}//setupMP3

// ==========================================================================

void setupRTC() {

  //Begin the RTC
  while(!rtc.begin()) {
    Serial.println(" RTC not found. Trying again..."); delay(1000);
    pixels.clear(); pixels.setPixelColor(0, redLow); pixels.show();

  }

  if (forceRTCreset) {
    Serial.println(" RTC FORCE RESET was requested. Setting Default Date and Time.");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
  
  if (!rtc.initialized() || rtc.lostPower()) {
    Serial.println(" RTC is NOT initialized OR lost power. Setting Default Date and Time.");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  rtc.start(); // in case it was stopped (line power loss)

}//setupRTC

// ==========================================================================

void setupTouch() {

  //Begin the three touch pins
  //The threshold for touch is being set at % of the initial value

  //Up
  if (touch_1.begin()) { touch_1_threshold = touch_1.measure() * 1.50; }
  else { Serial.println(" Touch failed on pin A1."); }

  //Down
  if (touch_2.begin()) { touch_2_threshold = touch_2.measure() * 1.50; }
  else { Serial.println(" Touch failed on pin A2."); }
  
  //Enter
  if (touch_3.begin()) { touch_3_threshold = touch_3.measure() * 1.75; }
  else { Serial.println(" Touch failed on pin A1."); }
  
}//setupTouch

// ==========================================================================

void calculateSPA() {
  
  Serial.println(" Calculating SPA...");

  Serial.println("  setSPAValues.");
  //Set the values that SPA needs
  setSPAValues();
  
  //Ask SPA to calculate based on values provided previously
  Serial.println("  SPAcalculate.");
  result = spa_calculate(&spa);

  //Anything other than a zero return value means low level error
  Serial.print(" SPA Return Code: "); Serial.print(result); 
  if (result==0) { Serial.print(" OK"); }
  if (result!=0) { Serial.print(" ERROR!"); }
  Serial.println();

}

// ==========================================================================

void setSPAValues() {

  //For the "real thing" the dates will be from the real time clock
  //The other stuff can be hard-coded / settings / GPS / etc.

  DateTime now = rtc.now();
  
  spa.year = now.year();
  spa.month = now.month();
  spa.day = now.day();
  spa.hour = now.hour();
  spa.minute = now.minute();
  spa.second = now.second();
   
  spa.delta_t = 67;
  spa.timezone = defaultClockSetting.timezone; // -5;
  
  // Copperas Cove, TX
  spa.longitude = defaultClockSetting.longitude; // -97.905490;
  spa.latitude = defaultClockSetting.latitude; // 31.115040;
  spa.elevation = defaultClockSetting.elevation; // 1089.00;

  spa.pressure = 820;
  spa.temperature = 11;
  spa.slope = 30;
  spa.azm_rotation = -10;
  spa.atmos_refract = 0.5667;
  spa.function = SPA_ZA_RTS; //SPA_ALL;
   
}

//=================================================================================

void HeartBeat() 
{
  // Show status update every (HEARTBEAT) seconds 
  static unsigned long last = millis();
  if (millis() - last > HEARTBEAT) {
    last = millis();
    Serial.print("[HEARTBEAT] "); Serial.print(buildDate);
    Serial.print(" Uptime: "); Serial.print((int) (last/60000)); Serial.print(" minutes");
    Serial.println();
  }
}//HeartBeat

// ==========================================================================
// ==========================================================================


void debugSPAResults() {

  int riseHrs, transitHrs, setHrs;
  float riseMin, riseSec, transitMin, transitSec, setMin, setSec;

  riseHrs = (int) spa.sunrise;
  riseMin = 60.0*(spa.sunrise - riseHrs);
  riseSec = 60.0*(riseMin - (int)riseMin);

  transitHrs = (int) spa.suntransit;
  transitMin = 60.0*(spa.suntransit - transitHrs);
  transitSec = 60.0*(transitMin - (int)transitMin);

  setHrs = (int) spa.sunset;
  setMin = 60.0*(spa.sunset - setHrs);
  setSec = 60.0*(setMin - (int)setMin);

  Serial.print(" Zenith:"); Serial.print(spa.zenith); Serial.print(" deg");
  Serial.print(" Sunrise:"); 
    Serial.print(riseHrs); Serial.print(":"); 
    Serial.print((int)riseMin); Serial.print(":");
    Serial.print((int)riseSec); 
  Serial.print(" Transit:"); 
    Serial.print(transitHrs); Serial.print(":"); 
    Serial.print((int)transitMin); Serial.print(":");
    Serial.print((int)transitSec); 
  Serial.print(" Sunset:");
    Serial.print(setHrs); Serial.print(":"); 
    Serial.print((int)setMin); Serial.print(":");
    Serial.print((int)setSec); 
  Serial.print(" L:"); Serial.print(spa.l); Serial.print(" deg");
  Serial.print(" B:"); Serial.print(spa.b); Serial.print(" deg");
  Serial.print(" R:"); Serial.print(spa.r); Serial.print(" AU");
  Serial.println();
}//debugSPAResults

//===================================================================================

void debugTouch() {
  Serial.print(" Touch Thresholds ");
  Serial.print(" T1:"); Serial.print(touch_1_threshold);
  Serial.print(" T2:"); Serial.print(touch_2_threshold);
  Serial.print(" T3:"); Serial.print(touch_3_threshold);
  Serial.println();
  Serial.print(" Touch Values ");
  Serial.print(" T1:"); Serial.print(touch_1.measure());
  Serial.print(" T2:"); Serial.print(touch_2.measure());
  Serial.print(" T3:"); Serial.print(touch_3.measure());
  Serial.println();
}//debugTouch

// ==========================================================================

void debugRTC() {
    DateTime now = rtc.now();
    Serial.print(" RTC Values ");
    Serial.print(now.year(), DEC);
    Serial.print('/');
    Serial.print(now.month(), DEC);
    Serial.print('/');
    Serial.print(now.day(), DEC);
    Serial.print(" (");
    Serial.print(daysOfWeek[now.dayOfTheWeek()]);
    Serial.print(") ");
    Serial.print(now.hour(), DEC);
    Serial.print(':');
    Serial.print(now.minute(), DEC);
    Serial.print(':');
    Serial.print(now.second(), DEC);
    Serial.println();
}//debugRTC

// ==========================================================================
// ==========================================================================

// <EOF>
