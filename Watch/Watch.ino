/*
  THIS WATCH CODE IS BUILT ON TOP OF THE CODE FROM https://github.com/olikraus/u8g2/, 
  which is licensed as Simplified BSD License! See the License File! 
  See GraphicsTest.ido for orignal code that this was built on top of.
*/
//CHECK OTA AND DEVBOARD BEFORE SWITCHING BOARDS devCapTouch
/*
  THESE ARE THE REQUIRED FILES
*/
#include <WiFi.h>//WiFi OTA + RTC + HTTP
#include <ESPmDNS.h>//WiFi OTA
#include <WiFiUdp.h>//WiFi OTA
#include <ArduinoOTA.h>//WiFi OTA
#include "time.h"//RTC
#include <Arduino.h> //For u8g2 + HTTP
#include <U8g2lib.h> //u8g2
#include <HTTPClient.h> //HTTP
#include "Options.h" //WiFi + Server Credentials 
/*
  WiFi Cred.
*/
const char* ssid = WIFI_SSID;//WiFi OTA                                                  SET SSID IN Options.h
const char* password = WIFI_PSWD;//WiFi OTA                                              SET PSWD IN Options.h
bool diasly_connected = true;//True if WiFi is connected (WiFi.waitForConnectResult() == WL_CONNECTED)
/*
  Wireless Firmware Updates
*/
bool ota = false; // OTA 
uint16_t otaCount=0; //OTA
/*
  Real Time Clock
*/
const char* ntpServer = SERVER_URL_NTP;//RTC
const long  gmtOffset_sec = -18000;//Eastern Time Zone
const int   daylightOffset_sec = 3600;//RTC
/*
  Graphics Library Setup
*/
 //GTEST START
#ifdef U8X8_HAVE_HW_SPI
#include <SPI.h>
#endif
#ifdef U8X8_HAVE_HW_I2C
#include <Wire.h>
#endif
//GTEST END
/*
  THIS IS THE OLED SCREEN
*/
U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(U8G2_R0, /* clock=*/ 4, /* data=*/ 5, /* reset=*/ U8X8_PIN_NONE);   // All Boards without Reset of the Display DEV
//U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(U8G2_R0, /* clock=*/ 25, /* data=*/ 23, /* reset=*/ U8X8_PIN_NONE);   // All Boards without Reset of the Display PCB
int state=1;//State=0 Menu, State 1= Time/Act Time Screen
bool statetask=false;//True when ToDo Menu Screen is Visible, False when General Menu is Visible
/*
  Timing
*/
char curTime[9];// Current Time String
char actTime[9];// Activity Time String
unsigned long timeStart;//Activity Start Time
#define SECS_PER_MIN  (60UL)
#define SECS_PER_HOUR (3600UL)
#define SECS_PER_DAY  (SECS_PER_HOUR * 24L)
#define numberOfSeconds(_time_) (_time_ % SECS_PER_MIN)  
#define numberOfMinutes(_time_) ((_time_ / SECS_PER_MIN) % SECS_PER_MIN)
#define numberOfHours(_time_) (( _time_% SECS_PER_DAY) / SECS_PER_HOUR)
bool timeMode = false;

/*
  Cap Touch 
*/
bool preventTrigger=false; // Keep Initilized To False. This keeps track when both Cap Touch are touched at the sametime and prevents multiple triggering.
bool readyCapTouch=false;//For Preventing Multiple Attachments
bool touchNext = false;// Cap Touch Firing
volatile int touchNextState=0; //Init=0 Waiting for Press, Pressed=1 Waiting for Debounce Period, Debounced=2 Waiting for release, Trigger=3 Do Action and Go to Init
volatile unsigned long touchNextStart;
volatile unsigned long touchNextLast;
int touchNextCount = 0;

bool touchSelect = false;// Cap Touch Firing
volatile int touchSelectState=0; //Init=0 Waiting for Press, Pressed=1 Waiting for Debounce Period, Debounced=2 Waiting for release, Trigger=3 Do Action and Go to Init
volatile unsigned long touchSelectStart;
volatile unsigned long touchSelectLast;
int touchSelectCount = 0;

bool devCapTouch=true;//Condition for Dev Watch (Breadboard) Vs Final Watch (PCB)
bool touchDebounce=false;


/*
  Debugging and Menu Options
*/
int devLineY=0;
char debugText0[]="                   ";
char debugText1[]="                   ";
char debugText2[]="                   ";
char debugText3[]="                   ";
char debugText4[]="                   ";
char debugTextDEFAULT[]="                   ";

/*
  Menu Selection
 */
int menuSelection=-1;//Current Selection
int menuMaxSelection=4;//Maximum Menu Items Count
int menuSelected=-1;//Last Selected
/*
 * RTOS
 */
void TaskServer( void *pvParameters ); //RTOS
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED; // MUX ---Ended Up Causing Crashes. So, Currently not used and came up with an alternative solution(TaskNotify). May plan to use again in the future.
char taskOptions[100]=""; // The RAW Task Options String Read From The Server
char taskOptionReady[100]="";// The appropriate NULL Terminated Task Options To Display as Menu
TaskHandle_t netTask;//TaskNotify

void printActTime() {// Get's Current Activity Time Ela. To Display
  unsigned long elapsedTime=(millis()-timeStart)/1000;
  snprintf(actTime, sizeof(actTime), "%02d:%02d:%02d", numberOfMinutes(elapsedTime), numberOfSeconds(elapsedTime), numberOfHours(elapsedTime));
}
void printTime() {// Get's Current Time To Display
    if (diasly_connected){   
      struct tm timeinfo;//RTC
      if(!getLocalTime(&timeinfo)){//RTC
        Serial.println("Failed to obtain time");//RTC
        return;//RTC
      }
      snprintf(curTime, sizeof(curTime), "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);      
    }
}
void dispText(){//Display Debug Text in Buffer
  u8g2.drawStr(0, 10, debugText0);  
  u8g2.drawStr(0, 20, debugText1);  
  u8g2.drawStr(0, 30, debugText2);  
  u8g2.drawStr(0, 40, debugText3);  
  u8g2.drawStr(0, 50, debugText4);  
}
void dispTextCenter(){//Display Menu Text in Buffer (Notice debug is Hijacked for a Menu to save resources)
  
  u8g2.drawStr(((menuSelection==0||statetask)? 12 : ((u8g2.getDisplayWidth()/2)-(u8g2.getMaxCharWidth()*strlen(debugText0)/2))), 10, debugText0);  
  u8g2.drawStr(((menuSelection==1||statetask)? 12 : ((u8g2.getDisplayWidth()/2)-(u8g2.getMaxCharWidth()*strlen(debugText1)/2))), 20, debugText1);  
  u8g2.drawStr(((menuSelection==2||statetask)? 12 : ((u8g2.getDisplayWidth()/2)-(u8g2.getMaxCharWidth()*strlen(debugText2)/2))), 30, debugText2);  
  u8g2.drawStr(((menuSelection==3||statetask)? 12 : ((u8g2.getDisplayWidth()/2)-(u8g2.getMaxCharWidth()*strlen(debugText3)/2))), 40, debugText3);  
  u8g2.drawStr(((menuSelection==4||statetask)? 12 : ((u8g2.getDisplayWidth()/2)-(u8g2.getMaxCharWidth()*strlen(debugText4)/2))), 50, debugText4);  
  u8g2.drawBox(0,10,u8g2.getDisplayWidth(),1);//Bars
  u8g2.drawBox(0,20,u8g2.getDisplayWidth(),1);//Bars
  u8g2.drawBox(0,30,u8g2.getDisplayWidth(),1);//Bars
  u8g2.drawBox(0,40,u8g2.getDisplayWidth(),1);//Bars
  u8g2.drawBox(0,50,u8g2.getDisplayWidth(),1);//Bars
  if (menuSelection==-1){
    u8g2.drawRBox(0, 12,8,6, 3);// Rounded Corner Box Edge
    u8g2.drawBox(0,12,3,6);//Box Edge No Round Left Side  
  }else{
    u8g2.drawRBox(0, 10*(menuSelection+1)+2,6,6, 3);// Rounded Corner Box Edge
  }
}
void printText(char C[]){//Adds Text to Buffer (This is purposely Serial for easy debugging)
  strncpy ( debugText4, debugText3, sizeof(debugTextDEFAULT) );
  strncpy ( debugText3, debugText2, sizeof(debugTextDEFAULT) );
  strncpy ( debugText2, debugText1, sizeof(debugTextDEFAULT) );
  strncpy ( debugText1, debugText0, sizeof(debugTextDEFAULT) );
  strncpy ( debugText0, C, sizeof(debugTextDEFAULT) );
}
void clrText(){//Removes All Text/Menu
  strncpy ( debugText4, debugTextDEFAULT, sizeof(debugTextDEFAULT) );
  strncpy ( debugText3, debugTextDEFAULT, sizeof(debugTextDEFAULT) );
  strncpy ( debugText2, debugTextDEFAULT, sizeof(debugTextDEFAULT) );
  strncpy ( debugText1, debugTextDEFAULT, sizeof(debugTextDEFAULT) );
  strncpy ( debugText0, debugTextDEFAULT, sizeof(debugTextDEFAULT) );
}
void touchBothFire(){ // Both Metals Contacts (Next + Select) Event
  if (state==1){
    state=0;
    menuSelection=-1;
    statetask=false;
  }else{
    clrText();
    state=1;  
  }
}
void touchNextFire(){// Next input Event
  if (preventTrigger){
    preventTrigger=false;// Previous Cap Released First. Event Already Occured.
  }else if (touchSelectState==3){
    preventTrigger=true; // Prevent Other Events From Occuring on Other Cap Release
    touchBothFire();
  }else{
    if (state==1){
      timeMode=!timeMode;
    }else if (state==0){
      if (menuSelection==-1){
        menuSelection=1;  
      }else if (menuSelection<menuMaxSelection){
        menuSelection+=1;  
      }else{
        menuSelection=0;  
      }
      
    }else{
        
    }
    
  }
}
void touchSelectFire(){// Select input Event
  
  if (preventTrigger){
    preventTrigger=false;// Previous Cap Released First. Event Already Occured.
  }else if (touchNextState==3){
    preventTrigger=true; // Prevent Other Events From Occuring on Other Cap Release
    touchBothFire();
  }else{
    if (state==1){
      timeStart=millis();
      menuSelected=-1;
      xTaskNotify(netTask, 1, eSetValueWithOverwrite);//TaskNotify
    }else if (state==0){
      if (statetask){
        timeMode=false;
        timeStart=millis();
        clrText();
        state=1;
        menuSelected=max(menuSelection,0);
        xTaskNotify(netTask, 1, eSetValueWithOverwrite);//TaskNotify
      }else{
        if (menuSelection<1){
          statetask=true;
        }
      }
      
    }else{
      
    }
    
  }
  
  
}
void touchHandlerNext(){//Handles Next Input Contact
  touchNext=false;
  unsigned long currentMillis = millis();
  if (touchNextState==1){
    bool pressed = false;
    if (currentMillis - touchNextLast <= 100){
      pressed = true;
    }  
    if (((currentMillis-touchNextStart >= 150) && pressed)||(!touchDebounce)){
      touchNextState=2;
    }
    if (pressed==false){
       touchNextState=0;
    }
  }else if (touchNextState==2){
    bool pressed = false;
    if (currentMillis - touchNextLast <= 100){
      pressed = true;
    }
    if (pressed==true){
      touchNext=true;//Confirmed Debounced Cap Touch
    }  else {
       touchNextState=3;
    }
  }else if (touchNextState==3){
    touchNextState=0;
    touchNextCount++;
    char pressCNT[3];
    itoa(touchNextCount,pressCNT,10);
//    printText(pressCNT);
    touchNextFire();
  }
}

void touchHandlerSelect(){//Handles Select Input Contact
  touchSelect=false;
  unsigned long currentMillis = millis();
  if (touchSelectState==1){
    bool pressed = false;
    if (currentMillis - touchSelectLast <= 100){
      pressed = true;
    }  
    if (((currentMillis-touchSelectStart >= 150) && pressed)||(!touchDebounce)){
      touchSelectState=2;
    }
    if (pressed==false){
       touchSelectState=0;
    }
  }else if (touchSelectState==2){
    bool pressed = false;
    if (currentMillis - touchSelectLast <= 100){
      pressed = true;
    }
    if (pressed==true){
      touchSelect=true;//Confirmed Debounced Cap Touch
    }  else {
       touchSelectState=3;
    }
  }else if (touchSelectState==3){
    touchSelectState=0;
    touchSelectCount++;
    char pressCNT[3];
    itoa(touchSelectCount,pressCNT,10);
//    printText(pressCNT);
    touchSelectFire();
  }
}
//GTEST START
void u8g2_prepare(void) {
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.setFontRefHeightExtendedText();
  u8g2.setDrawColor(1);
  u8g2.setFontPosTop();
  u8g2.setFontDirection(0);
}
//GTEST END
void u8g2_box_frame(uint8_t a) {//GTEST
  if (state==1){
    dispText();//Debug Text Print Out
    
    char dispTime[6];
    if (timeMode){
      strncpy ( dispTime, actTime, 5 );
    }else{
      strncpy ( dispTime, curTime, 5 );
    }
    
    dispTime[5]='\0';
    u8g2.drawBox(0,0,u8g2.getMaxCharWidth()*5,10);//White Background Box Top Left
    if (timeMode){
      u8g2.drawBox(0,u8g2.getDisplayHeight()-1,u8g2.getDisplayWidth()*atoi(&curTime[6])/60,1);//Bottom Progressbar Box
    }else{
      char minElap[3];
      strncpy ( minElap, actTime, 2 );
      minElap[2]='\0';
      u8g2.drawBox(0,u8g2.getDisplayHeight()-1,u8g2.getDisplayWidth()*atoi(&minElap[0])/60,1);//Bottom Progressbar Box
    }
    u8g2.setFont(u8g2_font_6x12_t_symbols );//Symbol Font
    u8g2.setFontPosTop();
    if (diasly_connected){
      if (timeMode){
        char minHour[3];
        strncpy ( minHour, curTime, 2 );
        minHour[2]='\0';
        if ((atoi(&minHour[0])>8)&&(atoi(&minHour[0])<22)){
          u8g2.drawBox(u8g2.getDisplayWidth()-3,(u8g2.getDisplayHeight()*atoi(&minHour[0])/24)-3,u8g2.getDisplayWidth(),7);//Bottom Progressbar Box
        }  
      }else{
        int hoursAct=atoi(&actTime[6]);
        if (hoursAct<2){
          u8g2.drawUTF8(u8g2.getDisplayWidth()-u8g2.getMaxCharWidth(), u8g2.getMaxCharHeight()*hoursAct, "☀");//☁ ☂ ☔ ⏰ ⏱ ⏳  
        }else if (hoursAct==2){ 
          u8g2.drawUTF8(u8g2.getDisplayWidth()-u8g2.getMaxCharWidth(), u8g2.getMaxCharHeight()*2, "☁");//☁ ☂ ☔ ⏰ ⏱ ⏳
        }else{
          u8g2.drawUTF8(u8g2.getDisplayWidth()-u8g2.getMaxCharWidth(), u8g2.getMaxCharHeight()*min(hoursAct,4), "☂");//☁ ☂ ☔ ⏰ ⏱ ⏳
        }
        
//      u8g2.drawUTF8(u8g2.getDisplayWidth()-u8g2.getMaxCharWidth(), u8g2.getMaxCharHeight()*1, "☀");//☁ ☂ ☔ ⏰ ⏱ ⏳
//      u8g2.drawUTF8(u8g2.getDisplayWidth()-u8g2.getMaxCharWidth(), u8g2.getMaxCharHeight()*2, "☁");//☁ ☂ ☔ ⏰ ⏱ ⏳
//      u8g2.drawUTF8(u8g2.getDisplayWidth()-u8g2.getMaxCharWidth(), u8g2.getMaxCharHeight()*3, "☂");//☁ ☂ ☔ ⏰ ⏱ ⏳
//      u8g2.drawUTF8(u8g2.getDisplayWidth()-u8g2.getMaxCharWidth(), u8g2.getMaxCharHeight()*4, "☂");//☁ ☂ ☔ ⏰ ⏱ ⏳
      }  
      
    }
    u8g2.setDrawColor(0);// Black
    u8g2.drawStr( 0, 0, dispTime);
    u8g2.setDrawColor(1);// Black
    u8g2.setFont(u8g2_font_logisoso20_tr  );
    u8g2.setFontPosTop();
   if (timeMode){
      u8g2.drawStr( 10, 30, curTime);  
    }else{
      char truncElap[6];
      strncpy ( truncElap, actTime, 5 );
//        strncpy ( truncElap, actTime, 2 );
      truncElap[5]='\0';
      u8g2.drawStr( 10+u8g2.getMaxCharWidth(), 30, truncElap);  
    }
    
    
    if (touchNext){
      u8g2.drawDisc(10,18,9);
    }    
    if (touchSelect){
      u8g2.drawDisc(24+a,16,7);
    }    
    
    
  }else if (state==0){
    u8g2.drawBox(0,0,u8g2.getMaxCharWidth()*5,10);//White Background Box Top Left
    u8g2.drawBox(u8g2.getDisplayWidth()-u8g2.getMaxCharWidth()*5,0,u8g2.getMaxCharWidth()*5,10);//White Background Box Top Right
    char dispTimeLeft[6];
    char dispTimeRight[6];
    strncpy ( dispTimeLeft, actTime, 5 );
    strncpy ( dispTimeRight, curTime, 5 );
    dispTimeLeft[5]='\0';
    dispTimeRight[5]='\0';
    u8g2.setFont(u8g2_font_6x12_t_symbols );//Symbol Font
    u8g2.setFontPosTop();
    u8g2.setDrawColor(0);// Black
    u8g2.drawStr( 0, 0, dispTimeLeft);
    u8g2.drawStr( u8g2.getDisplayWidth()-u8g2.getMaxCharWidth()*5, 0, dispTimeRight);
    u8g2.setDrawColor(1);// Black
    if (statetask){
      
      strncpy ( taskOptionReady, taskOptions, 99 );
//      taskOptionReady[50]='\0';
      taskOptionReady[99]='\0';
      taskOptionReady[79]='\0';
      taskOptionReady[59]='\0';
      taskOptionReady[39]='\0';
      taskOptionReady[19]='\0';
      printText(taskOptionReady+80);
      printText(taskOptionReady+60);
      printText(taskOptionReady+40);
      printText(taskOptionReady+20);
      printText(taskOptionReady);
    }else{
      printText("Daily Charts");
      printText("Find Phone");
      printText("Reminders");
      printText("Timer");
      printText("ToDo");

    }
    
    dispTextCenter();//Debug Text Print Out
  }
    touchHandlerNext();
    touchHandlerSelect();
//  u8g2.drawStr( 0, 0, "drawBox");
//  u8g2.drawBox(5,10,20,10);
//  u8g2.drawBox(10+a,15,30,7);
//  u8g2.drawStr( 0, 30, "drawFrame");
//  u8g2.drawFrame(5,10+30,20,10);
//  u8g2.drawFrame(10+a,15+30,30,7);
}//GTEST
//GTEST START
void u8g2_disc_circle(uint8_t a) {
  u8g2.drawStr( 0, 0, "drawDisc");
  u8g2.drawDisc(10,18,9);
  u8g2.drawDisc(24+a,16,7);
  u8g2.drawStr( 0, 30, "drawCircle");
  u8g2.drawCircle(10,18+30,9);
  u8g2.drawCircle(24+a,16+30,7);
}

void u8g2_r_frame(uint8_t a) {
  u8g2.drawStr( 0, 0, "drawRFrame/Box");
  u8g2.drawRFrame(5, 10,40,30, a+1);
  u8g2.drawRBox(50, 10,25,40, a+1);
}

void u8g2_string(uint8_t a) {
  u8g2.setFontDirection(0);
  u8g2.drawStr(30+a,31, " 0");
  u8g2.setFontDirection(1);
  u8g2.drawStr(30,31+a, " 90");
  u8g2.setFontDirection(2);
  u8g2.drawStr(30-a,31, " 180");
  u8g2.setFontDirection(3);
  u8g2.drawStr(30,31-a, " 270");
}

void u8g2_line(uint8_t a) {
  u8g2.drawStr( 0, 0, "drawLine");
  u8g2.drawLine(7+a, 10, 40, 55);
  u8g2.drawLine(7+a*2, 10, 60, 55);
  u8g2.drawLine(7+a*3, 10, 80, 55);
  u8g2.drawLine(7+a*4, 10, 100, 55);
}

void u8g2_triangle(uint8_t a) {
  uint16_t offset = a;
  u8g2.drawStr( 0, 0, "drawTriangle");
  u8g2.drawTriangle(14,7, 45,30, 10,40);
  u8g2.drawTriangle(14+offset,7-offset, 45+offset,30-offset, 57+offset,10-offset);
  u8g2.drawTriangle(57+offset*2,10, 45+offset*2,30, 86+offset*2,53);
  u8g2.drawTriangle(10+offset,40+offset, 45+offset,30+offset, 86+offset,53+offset);
}

void u8g2_ascii_1() {
  char s[2] = " ";
  uint8_t x, y;
  u8g2.drawStr( 0, 0, "ASCII page 1");
  for( y = 0; y < 6; y++ ) {
    for( x = 0; x < 16; x++ ) {
      s[0] = y*16 + x + 32;
      u8g2.drawStr(x*7, y*10+10, s);
    }
  }
}

void u8g2_ascii_2() {
  char s[2] = " ";
  uint8_t x, y;
  u8g2.drawStr( 0, 0, "ASCII page 2");
  for( y = 0; y < 6; y++ ) {
    for( x = 0; x < 16; x++ ) {
      s[0] = y*16 + x + 160;
      u8g2.drawStr(x*7, y*10+10, s);
    }
  }
}

void u8g2_extra_page(uint8_t a)
{
  u8g2.drawStr( 0, 0, "Unicode");
  u8g2.setFont(u8g2_font_unifont_t_symbols);
  u8g2.setFontPosTop();
  u8g2.drawUTF8(0, 24, "☀ ☁");
  switch(a) {
    case 0:
    case 1:
    case 2:
    case 3:
      u8g2.drawUTF8(a*3, 36, "☂");
      break;
    case 4:
    case 5:
    case 6:
    case 7:
      u8g2.drawUTF8(a*3, 36, "☔");
      break;
  }
}

#define cross_width 24
#define cross_height 24
static const unsigned char cross_bits[] U8X8_PROGMEM  = {
  0x00, 0x18, 0x00, 0x00, 0x24, 0x00, 0x00, 0x24, 0x00, 0x00, 0x42, 0x00, 
  0x00, 0x42, 0x00, 0x00, 0x42, 0x00, 0x00, 0x81, 0x00, 0x00, 0x81, 0x00, 
  0xC0, 0x00, 0x03, 0x38, 0x3C, 0x1C, 0x06, 0x42, 0x60, 0x01, 0x42, 0x80, 
  0x01, 0x42, 0x80, 0x06, 0x42, 0x60, 0x38, 0x3C, 0x1C, 0xC0, 0x00, 0x03, 
  0x00, 0x81, 0x00, 0x00, 0x81, 0x00, 0x00, 0x42, 0x00, 0x00, 0x42, 0x00, 
  0x00, 0x42, 0x00, 0x00, 0x24, 0x00, 0x00, 0x24, 0x00, 0x00, 0x18, 0x00, };

#define cross_fill_width 24
#define cross_fill_height 24
static const unsigned char cross_fill_bits[] U8X8_PROGMEM  = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0x18, 0x64, 0x00, 0x26, 
  0x84, 0x00, 0x21, 0x08, 0x81, 0x10, 0x08, 0x42, 0x10, 0x10, 0x3C, 0x08, 
  0x20, 0x00, 0x04, 0x40, 0x00, 0x02, 0x80, 0x00, 0x01, 0x80, 0x18, 0x01, 
  0x80, 0x18, 0x01, 0x80, 0x00, 0x01, 0x40, 0x00, 0x02, 0x20, 0x00, 0x04, 
  0x10, 0x3C, 0x08, 0x08, 0x42, 0x10, 0x08, 0x81, 0x10, 0x84, 0x00, 0x21, 
  0x64, 0x00, 0x26, 0x18, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, };

#define cross_block_width 14
#define cross_block_height 14
static const unsigned char cross_block_bits[] U8X8_PROGMEM  = {
  0xFF, 0x3F, 0x01, 0x20, 0x01, 0x20, 0x01, 0x20, 0x01, 0x20, 0x01, 0x20, 
  0xC1, 0x20, 0xC1, 0x20, 0x01, 0x20, 0x01, 0x20, 0x01, 0x20, 0x01, 0x20, 
  0x01, 0x20, 0xFF, 0x3F, };

void u8g2_bitmap_overlay(uint8_t a) {
  uint8_t frame_size = 28;

  u8g2.drawStr(0, 0, "Bitmap overlay");

  u8g2.drawStr(0, frame_size + 12, "Solid / transparent");
  u8g2.setBitmapMode(false /* solid */);
  u8g2.drawFrame(0, 10, frame_size, frame_size);
  u8g2.drawXBMP(2, 12, cross_width, cross_height, cross_bits);
  if(a & 4)
    u8g2.drawXBMP(7, 17, cross_block_width, cross_block_height, cross_block_bits);

  u8g2.setBitmapMode(true /* transparent*/);
  u8g2.drawFrame(frame_size + 5, 10, frame_size, frame_size);
  u8g2.drawXBMP(frame_size + 7, 12, cross_width, cross_height, cross_bits);
  if(a & 4)
    u8g2.drawXBMP(frame_size + 12, 17, cross_block_width, cross_block_height, cross_block_bits);
}

void u8g2_bitmap_modes(uint8_t transparent) {
  const uint8_t frame_size = 24;

  u8g2.drawBox(0, frame_size * 0.5, frame_size * 5, frame_size);
  u8g2.drawStr(frame_size * 0.5, 50, "Black");
  u8g2.drawStr(frame_size * 2, 50, "White");
  u8g2.drawStr(frame_size * 3.5, 50, "XOR");
  
  if(!transparent) {
    u8g2.setBitmapMode(false /* solid */);
    u8g2.drawStr(0, 0, "Solid bitmap");
  } else {
    u8g2.setBitmapMode(true /* transparent*/);
    u8g2.drawStr(0, 0, "Transparent bitmap");
  }
  u8g2.setDrawColor(0);// Black
  u8g2.drawXBMP(frame_size * 0.5, 24, cross_width, cross_height, cross_bits);
  u8g2.setDrawColor(1); // White
  u8g2.drawXBMP(frame_size * 2, 24, cross_width, cross_height, cross_bits);
  u8g2.setDrawColor(2); // XOR
  u8g2.drawXBMP(frame_size * 3.5, 24, cross_width, cross_height, cross_bits);
}

uint8_t draw_state = 0;
//GTEST END
void draw(void) {//GTEST
  u8g2_prepare();
  switch(draw_state >> 3) {
    case 0: u8g2_box_frame(draw_state&7); break;
    case 1: u8g2_box_frame(draw_state&7); break;
    case 2: u8g2_box_frame(draw_state&7); break;
    case 3: u8g2_box_frame(draw_state&7); break;
    case 4: u8g2_box_frame(draw_state&7); break;
    case 5: u8g2_box_frame(draw_state&7); break;
    case 6: u8g2_box_frame(draw_state&7); break;
    case 7: u8g2_box_frame(draw_state&7); break;
    case 8: u8g2_box_frame(draw_state&7); break;
    case 9: u8g2_box_frame(draw_state&7); break;
    case 10: u8g2_box_frame(draw_state&7); break;
    case 11: u8g2_box_frame(draw_state&7); break;
//    case 0: u8g2_box_frame(draw_state&7); break;        // OLD CODE TO SEE GRAPHIC TEST. This will stay as comments for now!
//    case 1: u8g2_disc_circle(draw_state&7); break;
//    case 2: u8g2_r_frame(draw_state&7); break;
//    case 3: u8g2_string(draw_state&7); break;
//    case 4: u8g2_line(draw_state&7); break;
//    case 5: u8g2_triangle(draw_state&7); break;
//    case 6: u8g2_ascii_1(); break;
//    case 7: u8g2_ascii_2(); break;
//    case 8: u8g2_extra_page(draw_state&7); break;
//    case 9: u8g2_bitmap_modes(0); break;
//    case 10: u8g2_bitmap_modes(1); break;
//    case 11: u8g2_bitmap_overlay(draw_state&7); break;
  }
}//GTEST
void gotTouchNext(){// Touch Interupt Next
   Serial.print("touchNext() running on core ");Serial.println(xPortGetCoreID()); 
 if (touchNextState==0){
  touchNextState=1;
  touchNextStart=millis();
 }
 touchNextLast=millis();
}
void gotTouchSelect(){// Touch Interupt Select
  Serial.print("touchSelect() running on core ");Serial.println(xPortGetCoreID()); 
  if (touchSelectState==0){
  touchSelectState=1;
  touchSelectStart=millis();
 }
 touchSelectLast=millis();
}
void setup() {
  Serial.begin(115200);//WiFi OTA
  
  snprintf(curTime, sizeof(curTime), "%02d:%02d:%02d",0, 0, 0);//Init Time 00:00:00
  snprintf(actTime, sizeof(actTime), "%02d:%02d:%02d",0, 0, 0);//Init Time 00:00:00
  
  Serial.println("Booting");//WiFi OTA
  WiFi.mode(WIFI_STA);//WiFi OTA
  WiFi.begin(ssid, password);//WiFi OTA + RTOS
  if (WiFi.waitForConnectResult() != WL_CONNECTED) {//WiFi OTA (Switched from while to if Statement
    Serial.println("Connection Failed! Rebooting...");//WiFi OTA
    delay(5000);//WiFi OTA
//    ESP.restart();//WiFi OTA
    diasly_connected=false;
    
    Serial.println("Connection Failed! DIASLY NOT CONNECTED...");
    
  }//WiFi OTA

  // Port defaults to 3232      //WiFi OTA
  // ArduinoOTA.setPort(3232);  //WiFi OTA

  // Hostname defaults to esp3232-[MAC] //WiFi OTA 
   ArduinoOTA.setHostname("Watch"); //WiFi OTA

  // No authentication by default      //WiFi OTA
   ArduinoOTA.setPassword("Strat365");  //WiFi OTA

  // Password can be set with it's md5 value as well     //WiFi OTA
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3       //WiFi OTA
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");  //WiFi OTA

  ArduinoOTA//WiFi OTA
    .onStart([]() {//WiFi OTA
      String type;//WiFi OTA
      if (ArduinoOTA.getCommand() == U_FLASH)//WiFi OTA
        type = "sketch";//WiFi OTA
      else // U_SPIFFS//WiFi OTA
        type = "filesystem";//WiFi OTA

      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()//WiFi OTA
      Serial.println("Start updating " + type);//WiFi OTA
    })//WiFi OTA
    .onEnd([]() {//WiFi OTA
      Serial.println("\nEnd");//WiFi OTA
    })//WiFi OTA
    .onProgress([](unsigned int progress, unsigned int total) {//WiFi OTA
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));//WiFi OTA
    })//WiFi OTA
    .onError([](ota_error_t error) {//WiFi OTA
      Serial.printf("Error[%u]: ", error);//WiFi OTA
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");//WiFi OTA
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");//WiFi OTA
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");//WiFi OTA
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");//WiFi OTA
      else if (error == OTA_END_ERROR) Serial.println("End Failed");//WiFi OTA
    });//WiFi OTA

  ArduinoOTA.begin();//WiFi OTA

  Serial.println("Ready");//WiFi OTA
  Serial.print("IP address: ");//WiFi OTA
  Serial.println(WiFi.localIP());//WiFi OTA
  u8g2.begin(); //GTEST
  timeStart=millis();
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);//RTC
  
  xTaskCreatePinnedToCore(TaskServer,  "Server",  1024*10,  NULL, 0,  &netTask,  0);//RTOS + TaskNotify
 
}
void loop() {
  
  if (ota){ //OTA
    uint16_t start_60=millis(); //OTA
    while (otaCount<60000){ //OTA
      ArduinoOTA.handle(); //WiFi OTA
      otaCount=millis()-start_60; //OTA
      delay(25); //OTA
    } //OTA
    ota=false; //OTA
  //T8
  } else {
    if (readyCapTouch==false){
      if (devCapTouch){
        touchAttachInterrupt(T4, gotTouchSelect, 40);
        touchAttachInterrupt(T3, gotTouchNext, 40);
      }else{
        touchAttachInterrupt(T8, gotTouchSelect, 40);
        touchAttachInterrupt(T9, gotTouchNext, 40);
      
      }
      readyCapTouch=true; 
    }
      printTime();
      printActTime();
      u8g2.clearBuffer(); //GTEST
      draw(); //GTEST
      u8g2.sendBuffer(); //GTEST
      
      // increase the state //GTEST
      draw_state++; //GTEST
      if ( draw_state >= 12*8 ) //GTEST
        draw_state = 0; //GTEST
    
      // deley between each page
      delay(100); //GTEST

  }
  
  
}
void TaskServer(void *pvParameters) //RTOS Task where all TimeKeeper Server Events Occur
{
  uint32_t ulNotifiedValue;//TaskNotify
  (void) pvParameters; //RTOS
  for (;;) // A Task shall never return or exit. //RTOS
  {
    xTaskNotifyWait( 0x00, ULONG_MAX, &ulNotifiedValue, 5000 / portTICK_PERIOD_MS );//TaskNotify
//    vTaskDelay(5000); //HTTP- REQUIRED IF xTaskNotifyWait NOT PRESENT(TaskNotify or this)
    if((WiFi.status() == WL_CONNECTED)) {//HTTP
      HTTPClient http; //HTTP
      int httpCode=-1;
      if( ( ulNotifiedValue & 0x01 ) != 0 )//TaskNotify
      {
        char charSelected[3];
        itoa(menuSelected,charSelected,10);
        char URL[32] = SERVER_URL_TRACK;
        strcat(URL,charSelected);
        http.begin(URL); //HTTP
        httpCode = http.GET(); // start connection and send HTTP header  //HTTP
      }else{
        http.begin(SERVER_URL_TASK); //HTTP
        httpCode = http.GET(); // start connection and send HTTP header  //HTTP
      }

      if(httpCode > 0) {// httpCode will be negative on error  //HTTP
        if(httpCode == HTTP_CODE_OK) { // file found at server  //HTTP
          if( ( ulNotifiedValue & 0x01 ) != 0 )//TaskNotify
          {
            Serial.println("Creating New Log"); //HTTP
          }else{
            http.getString().toCharArray(taskOptions, 100);
          }
        }
      }else{
        Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str()); //HTTP
      }
      http.end(); //HTTP
    } 
    
  }
}
