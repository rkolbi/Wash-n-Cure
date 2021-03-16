/*
///////////////////////////////////////////
////// Wash-n-Cure Rev 0.4.1 (ALPHA) ////// 
///////////////////////////////////////////


-AP hard coded to SSID: "Wash-n-Cure", Password: "password", IP: 10.0.1.1

-Over the Air (OTA) firmware updating, access via index page (10.0.1.1).

-Simple web interface to change wash and cure times, commit new times to EEPROM, OTA firmware update (.BIN
  file), and stop all functions.

-Button functions as: 
         When no functions are active: SW1 = Starts Cure        SW2 = Starts Wash       SW3 = EEPROM Menu
         Wash or Cure active:          SW1 = Run time +1        SW2 = Run time -1       SW3 = Stops function

-No times changes are fully committed to EEPROM (reboot will revert back) unless the 'Save Times' function is
  selected from the web interface or from EEPROM menu.
   EEPROM Menu: (User has one minute to make selection, else it exits the menu loop)
	-SW1 will eeprom.write wash/cure time to 'factory defaults'
	-SW2 will eeprom.write wash/cure times if they differ the eeprom.read.
	-SW3 will cancel the menu, returning to 'Ready'

-Wash and Cure functins use different stepper motor controls. 
   -Cure uses the "stepper.setSpeed(500)" and requires "stepper.runSpeed()" to keep moving - yeilding a
     constants turning motor.
   -Wash uses "stepper.moveTo(washSteps)", steps to move set by "int washSteps = 2000;", and requires
     "stepper.run();" to keep moving - allowing the motor to have directional change.

-=-=-

To do:

-Confirm stepper motor driver / control with hardware

-Add SW3 functions as following:

  During wash or cure cycle - have SW3 or Interlock initiate pause function.
	-OLED write Paused.
	-Pause state to calculate time left and store it in timePause. On resume, this time will be added to
          now and the cycle will run until completion.
	-Pasue will stop motor, LED, and fan.
	-Pressing SW3 while in Pause state will hard stop the running function and return to 'Ready'. This
          means a double-press will hard stop the cycle.
	-Pressing SW1 or SW2 while continue with cycle unless Interlock is open. If Interlock is open, SW1 &
          SW2 will be ignored.

-=-=-

PLATFORMIO.INI FILE CONTENTS:
; PlatformIO Project Configuration File
;
;   Build options: build flags, source filter
;   Upload options: custom upload port, speed and extra flags
;   Library options: dependencies, extra library storages
;   Advanced options: extra scripting
;
; Please visit documentation for the other options and examples
; https://docs.platformio.org/page/projectconf.html

[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
lib_deps = 
	http://github.com/scottchiefbaker/ESP-WebOTA
	http://github.com/waspinator/AccelStepper
 	http://github.com/thomasfredericks/Bounce2
	http://github.com/adafruit/Adafruit_BusIO
	http://github.com/adafruit/Adafruit-GFX-Library
	http://github.com/adafruit/Adafruit_SSD1306

*/





//////////////////////////////////////
////// LIBRARIES & DECLARATIONS //////
//////////////////////////////////////

// BASE LIBRARIES
#include <Arduino.h>
#include <Wire.h>
#include <Bounce2.h>


// UV, IR, AND SW PINS AND CONTROL
  #define UVLED 32                // PIN:32 UV LED
  #define FAN 27               	  // PIN:27 Fan
  #define PROX 4                  // PIN:04 Lid Proximity Sensor
  #define SW1 35                  // PIN:35 SW1 Button
  #define SW2 34                  // PIN:34 SW2 Button
  #define SW3 0                   // PIN:00 SW3 Button (Also used for programming mode)

  int IRstate;                    // Lid Proximity Sensor state

  Bounce debouncedSW1 = Bounce(); // Bounce instance for SW1
  Bounce debouncedSW2 = Bounce(); // Bounce instance for SW3
  Bounce debouncedSW3 = Bounce(); // Bounce instance for SW3
  #define btn delay(500)          // Wait function following button detection

// WASH AND CURE VARIABLES
  #define CureDefault 20
  #define WashDefault 8  
  int washSteps = 2000;
  boolean washDirection = false;
  boolean washActive = false;
  boolean cureActive = false;
  int washSeconds;
  int uvSeconds;      


// TIMING VARIABLES
  #define now millis()  
  unsigned long lastTrigger = 0;
  unsigned long noteTrigger = 0;


// EEPROM STORAGE
#include <EEPROM.h>     
  #define EEPROM_SIZE 2         // Define the number of bytes we want to access in the EEPROM
  int WashValue;                // Store the washing timer value
  int CureValue;                // Store the curing timer value


// OLED SUPPORT
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
  #define SCREEN_WIDTH 128      // OLED display width, in pixels
  #define SCREEN_HEIGHT 64      // OLED display height, in pixels
  Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1); // SSD1306 display connected to I2C (SDA, SCL pins)


// STEPER DRIVER SUPPORT
#include <AccelStepper.h>
  #define dirPin 25             // Stepper driver DIR pin
  #define stepPin 33            // Stepper driver STP pin
  #define motorInterfaceType 1  // Must be set to 1 when using a driver
  #define motorEnable 26        // Stepper driver VDD pin
  AccelStepper stepper = AccelStepper(motorInterfaceType, stepPin, dirPin);


// NETWORKING SUPPORT
#include <WebOTA.h>
  const char* ssid = "Wash-n-Cure";
  const char* password = "password";
  IPAddress local_ip(10,0,1,1);
  IPAddress gateway(10,0,1,1);
  IPAddress subnet(255,255,255,0);





///////////////////////
////// FUNCTIONS //////
///////////////////////

// READY THE OLED DISPLAY
void readydisplay()
{
  display.clearDisplay();           // Clear the OLED display
  display.setTextSize(2);           // Set text size for display
  display.setTextColor(WHITE);      // Set text color for display
  display.setCursor(0, 10);         // Position the cursor
}


// START THE WASH FUNCTION
void wash() 
{
  Serial.println("Wash Cycle ON!");
  WashValue = EEPROM.read(0);
  Serial.print("Wash value from memory: ");
  Serial.println(WashValue);

  washSeconds = WashValue*60 ;      // Calculate wash seconds from WashValue (minutes)
  washActive = true;                // Set wash state to true
  lastTrigger = now;                // Reset the time trigger
  digitalWrite(FAN, HIGH);          // Turn on the fan
  digitalWrite(motorEnable, HIGH);  // Enable the motor
  stepper.setSpeed(8000);           // Set the motor speed
  stepper.setAcceleration(100);     // Set the stepper acceleration
  stepper.setCurrentPosition(0);    // Set starting position as 0
  stepper.moveTo(washSteps);        // Move stepper x steps
  washDirection = true;             // Motor moves clockwise

  readydisplay();                   // Set the OLED display
  display.println("Starting");      // OLED status display
  display.print("  Wash");          // OLED status display
  display.display();                // OLED status display
  noteTrigger = now + 2000;
  return;
}


// START THE CURE FUNCTION
void cure()
{ 
  Serial.println("UV Cycle ON!");
  CureValue = EEPROM.read(1);
  Serial.println("Cure value from memory");
  Serial.println(CureValue);

  uvSeconds = CureValue*60 ;        // Calculate wash seconds from CureValue (minutes)
  cureActive = true;                // Set cure state to true
  lastTrigger = now;                // Reset the time trigger
  digitalWrite(FAN, HIGH);          // Turn on the fan
  digitalWrite(motorEnable, HIGH);  // Enable the motor
  digitalWrite(UVLED, HIGH);        // Turn on the UV lamp
  stepper.setSpeed(500);            // Set the motor speed
  stepper.runSpeed();               // Start the motor
  
  readydisplay();                   // Set the OLED display
  display.println("Starting");      // OLED status display
  display.print("  Cure");          // OLED status display
  display.display();                // OLED status display
  noteTrigger = now + 2000;
  return;
}


// STOP THE WASH FUNCTION
void washoff()
{
  digitalWrite(FAN, LOW);           // Turn off the fan
  digitalWrite(motorEnable, LOW);   // Turn off the motor
  
  washActive = false;               // Set wash state to false
  Serial.println("Wash Cycle Finished! Returning to main loop");

  readydisplay();                   // Set the OLED display
  display.println("Washing");       // OLED status display
  display.print(" Done!");          // OLED status display
  display.display();                // OLED status display
  noteTrigger = now + 2000;
  return;
}


// STOP THE CURE FUNCTION
void cureoff()
{
  digitalWrite(FAN, LOW);           // Turn off the fan
  digitalWrite(motorEnable, LOW);   // Turn off the motor
  digitalWrite(UVLED, LOW);         // Turn off UV
  
  cureActive = false;               // Set cure state to false
  Serial.println("UV Cycle Finished! Returning to main loop");

  readydisplay();                   // Set the OLED display
  display.println("Curing ");       // OLED status display
  display.print(" Done!");          // OLED status display
  display.display();                // OLED status display
  noteTrigger = now + 2000;
  return;
}


// STOP ALL WASH AND CURE FUNCTION
void StopAll()
{
  digitalWrite(FAN, LOW);           // Turn off the fan
  digitalWrite(UVLED, LOW);         // Turn off UV
  digitalWrite(motorEnable, LOW);   // Turn off the motor
  
  cureActive = false;               // Set cure state to false
  washActive = false;               // Set wash state to false
  Serial.println("Stopping all functions!");

  readydisplay();                   // Set the OLED display
  display.println("Interlock");     // OLED status display
  display.println("  open!  ");     // OLED status display
  display.print  ("");              // OLED status display
  display.display();                // OLED status display
  noteTrigger = now + 2000;
  return;
}


// INCREASE WASH TIME
void washUP()
{
  WashValue = EEPROM.read(0);
  Serial.println("Increase wash time.");
  Serial.println(WashValue);
  EEPROM.write(0, ++WashValue);
  WashValue = EEPROM.read(0);
  washSeconds = WashValue*60 ;
  Serial.println("New wash value from memory");
  Serial.println(WashValue);

  readydisplay();                   // Set the OLED display
  display.println("Wash time:");    // OLED status display
  display.print(WashValue);         // OLED status display
  display.display();                // OLED status display
  noteTrigger = now + 2000;
}


// DECREASE WASH TIME
void washDOWN()
{
  WashValue = EEPROM.read(0);
  Serial.println("Decrease wash time.");
  Serial.println(WashValue);
  EEPROM.write(0, --WashValue);
  WashValue = EEPROM.read(0);
  washSeconds = WashValue*60 ;
  Serial.println("New wash value from memory");
  Serial.println(WashValue);

  readydisplay();                   // Set the OLED display
  display.println("Wash time:");    // OLED status display
  display.print(WashValue);         // OLED status display
  display.display();                // OLED status display
  noteTrigger = now + 2000;
}


// INCREASE CURE TIME
void cureUP()
{
  CureValue = EEPROM.read(1);
  Serial.println("Increase cure time.");
  Serial.println(CureValue);
  EEPROM.write(1, ++CureValue);
  CureValue = EEPROM.read(1);
  uvSeconds = CureValue*60 ;
  Serial.println("New cure value from memory");
  Serial.println(CureValue);
  
  readydisplay();                   // Set the OLED display
  display.println("Cure time:");    // OLED status display
  display.print(CureValue);         // OLED status display
  display.display();                // OLED status display
  noteTrigger = now + 2000;
}


// DECREASE CURE TIME
void cureDOWN()
{
  CureValue = EEPROM.read(1);
  Serial.println("Decrease cure time.");
  Serial.println(CureValue);
  EEPROM.write(1, --CureValue);
  CureValue = EEPROM.read(1);
  uvSeconds = CureValue*60 ;
  Serial.println("New cure value from memory");
  Serial.println(CureValue);

  readydisplay();                   // Set the OLED display
  display.println("Cure time:");    // OLED status display
  display.print(CureValue);         // OLED status display
  display.display();                // OLED status display
  noteTrigger = now + 2000;
}


// EEPROM MENU ACTIONS
void eepromMenu()
{
Serial.print("EEPROM Menu actions");
readydisplay();                     // Set the OLED display
display.println("SW1-Reset");       // OLED status display
display.println("SW2-Save");        // OLED status display
display.print  ("SW3-Exit");        // OLED status display
display.display();                  // OLED status display  

// NO DOUBLE-TAP
  debouncedSW3.update(); delay(100); debouncedSW3.update(); delay(100); 

Serial.println(".");
lastTrigger = now;                    // Reset the time trigger
while ( (now - lastTrigger) < 30000)  // Give 30 seconds for a response
  { 
    debouncedSW1.update();  debouncedSW2.update();  debouncedSW3.update();   
  
    // SW1 - Reset Wash and Cure time to factory defauls
    if (debouncedSW1.fell())
    { 
      btn;
      EEPROM.write(0, CureDefault);   // RESET CURE TIME
      WashValue = EEPROM.read(0);     // READ BACK CURE TIME
      EEPROM.write(1, WashDefault);   // RESET WASH TIME
      CureValue = EEPROM.read(1);     // READ BACK WASH TIME
      EEPROM.commit();                // Commit changes to EEPROM
      
      readydisplay();                 // Set the OLED display
      display.println("Wash&Cure");   // OLED status display
      display.println(" EEPROM  ");   // OLED status display
      display.print  (" reset!  ");   // OLED status display
      display.display();              // OLED status display
      noteTrigger = now + 2000;
      return; // exit this function
    }

    // SW2 - Save current setting to EEPROM
    if (debouncedSW2.fell())
    { 
      btn;
      EEPROM.commit();
      readydisplay();                   // Set the OLED display
      display.println("Wash&Cure");     // OLED status display
      display.println("  times  ");     // OLED status display
      display.print  ("  saved");       // OLED status display
      display.display();                // OLED status display
      noteTrigger = now + 2000;
      return; // exit this function
    }

    // SW3 - Exit EEPROM menu
    if (debouncedSW3.fell())
    { 
      btn;
      return; // Exit this function
    }
  }
  Serial.println("Timed-out of EEPROM Menu.");
}





///////////////////////////
////// WEB INTERFACE //////
///////////////////////////

// SET WEBSERVER PORT
WebServer server(80);


// INDEX PAGE ( / ) HTML BODY
const char* htmlIndex1 = "<html><head><meta name=viewport content='width=400'><style>A {text-decoration: none;} table, th, td { border: 0px; border-collapse: collapse; } th, td { padding: 15px; text-align: center; } #t01 { width:400px; }</style></head><body><center><h2>Wash & Cure</h2><h5>revision 0.4</h5><br><br><table id='t01'><tr><th>Wash Time</th><th></th><th>Cure Time</th></tr><tr><td>&#x2206 <a href='wu'>UP</a> &#x2206</td><td></td><td>&#x2206 <a href='cu'>UP</a> &#x2206</td></tr><tr><td><b>";
    // Server inserts WashValue here
const char* htmlIndex2 = "</b></td><td>&#x21AF <a href='es'>Save Times</a> &#x21AF</td><td><b>";
    // Server inserts CureValue here
const char* htmlIndex3 = "</b></td></tr><tr><td>&#x2207 <a href='wd'>DOWN</a> &#x2207</td><td></td><td>&#x2207 <a href='cd'>DOWN</a> &#x2207</td></tr><tr></tr><tr><td>&#x220E <a href='sa'>STOP ALL</a></td><td></td><td>&#x21BB <a href='http://10.0.1.1:777/update'>UPDATE .BIN</a></td></tr></table>";


// MAIN PAGE
void handleRoot() 
{
  server.send(200, "text/html", htmlIndex1 + String(WashValue) + htmlIndex2 + String(CureValue) + htmlIndex3);
}


// WashUP WEB RESPONSE
void handleWashUp() 
{
  washUP();
  handleRoot();
}


// WashDOWN WEB RESPONSE
void handleWashDown() 
{
  if (WashValue > 3) washDOWN();
  handleRoot();
}

// CureUP WEB RESPONSE
void handleCureUp() 
{
  cureUP();
  handleRoot();
}


// cureDOWN WEB RESPONSE
void handleCureDown() 
{
  if (CureValue > 3) cureDOWN();
  handleRoot();
}


// StopAll WEB RESPONSE
void handleStopAll() 
{
  StopAll();
  handleRoot();
}


// 404 - NOT FOUND WEB RESPONSE
void handleNotFound() 
{
  server.send(404, "text/plain", "404: Page does not exist.\n\n");
}


// EepromSave WEB RESPONSE
void handleEepromSave() 
{
  EEPROM.commit();
  readydisplay();                   // Set the OLED display
  display.println("Wash&Cure");     // OLED status display
  display.println("  times  ");     // OLED status display
  display.print  ("  saved");       // OLED status display
  display.display();                // OLED status display
  noteTrigger = now + 2000;
  handleRoot();
}





///////////////////
////// SETUP //////
///////////////////

void setup()
{
// SET UP COMMINUCATIONS
  Serial.begin(9600);                           // Set board's serial speed for terminal communcation
  WiFi.softAP(ssid, password);                  // Set board's ssid and password
  WiFi.softAPConfig(local_ip, gateway, subnet); // Set board's ip, gateway, and subnet
  webota.init(777, "/update");                  // Setup web over-the-air update port and directory


// INITIALIZE EEPROM
  EEPROM.begin(EEPROM_SIZE);
  WashValue = EEPROM.read(0);                   // Read WashValue from EEPROM location 0
  CureValue = EEPROM.read(1);                   // Read CureValue from EEPROM location 1


// SET UP STEPPER CONTROL
  stepper.setMaxSpeed(8000);                    // Set max stepper speed to 8000
  stepper.setAcceleration(100);                 // Set stepper acceleration to 100


// DEFINE GPIO FUNCTIONS
  pinMode(PROX, INPUT);                         // Set PROX pin as an input

  pinMode(motorEnable, OUTPUT);                 // Set motorEnable pin as an output
    digitalWrite(motorEnable, LOW);             // Turn motor off
  
  pinMode(UVLED, OUTPUT);                       // Set UVLED pin as an output
    digitalWrite(UVLED, LOW);                   // ! ! ! NEVER SET THIS HIGH ! MOSFET DAMAGE WILL OCCUR ! ! !
  
  pinMode(FAN, OUTPUT);                         // Set FAN pin as an output
    digitalWrite(FAN, LOW);                     // ! ! ! NEVER SET THIS HIGH ! MOSFET DAMAGE WILL OCCUR ! ! !

  pinMode(SW1, INPUT);                          // Set SW1 pin as an input
    debouncedSW1.attach(SW1);                   // attach debouncedSW1 to SW1
    debouncedSW1.interval(25);                  // 25 ms bounce interval

  pinMode(SW2, INPUT);                          // Set SW2 pin as an input
    debouncedSW2.attach(SW2);                   // attach debouncedSW2 to SW2
    debouncedSW2.interval(25);                  // 25 ms bounce interval 

  pinMode(SW3, INPUT);                          // Set SW3 pin as an input
    debouncedSW3.attach(SW3);                   // attach debouncedSW3 to SW3
    debouncedSW3.interval(25);                  // 25 ms bounce interval


// OLED INITIALIZATION
  Wire.begin(21,22);//OLED I2C Pins
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) // Address for the I2C OLED screen
  { 
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }

  readydisplay();                   // Set the OLED display
  display.println("Wash&Cure");     // OLED status display
  display.println(" ");             // OLED status display
  display.print  (" rev 0.4");      // OLED status display
  display.display();                // OLED status display
  noteTrigger = now + 4000;


// WEB PAGE CONFIGURATIONS

// WHEN THE SERVER GETS A REQUEST FOR A PAGE, CALL ITS FUNCTION.
  server.on("/", handleRoot);  
  server.on("/wu", handleWashUp);
  server.on("/wd", handleWashDown);
  server.on("/cu", handleCureUp);
  server.on("/cd", handleCureDown);
  server.on("/sa", handleStopAll);
  server.on("/es", handleEepromSave);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.print("SETUP Complete");

// WAIT FOR SW3(PIN 0) TO RETURN TO A NORMAL INPUT
  lastTrigger = now;                    // Reset the time trigger
  while ( (now - lastTrigger) < 2000)   // 2 seconds to clear SW3
  { 
    debouncedSW3.update();
    if (debouncedSW3.fell()) 
    {
      Serial.print(".");
    }
    btn;
  }
  
  Serial.println(" ");
  Serial.print("Initialization time: ");
  Serial.println(now);
}





///////////////////////
////// MAIN LOOP //////
///////////////////////

void loop()
{
// RUN OTA UPDATE SERVICE  
webota.handle();


// RUN THE WEB SERVER
server.handleClient();


// OLED STATUS CHECK - WASHING
if (washActive == true && now > noteTrigger)
{
  readydisplay();                         // Set the OLED display
  display.println("Washing...");          // OLED status display
  display.print((((washSeconds*1000)-(now-lastTrigger))/60000));
  display.println(" minutes remaining");  // OLED status display
  display.display();                      // OLED status display
}


// OLED STATUS CHECK - CURING
if (cureActive == true && now > noteTrigger)
{
  readydisplay();                         // Set the OLED display
  display.println("Curing...");           // OLED status display
  display.print((((uvSeconds*1000)-(now-lastTrigger))/60000));
  display.println(" minutes remaining");  // OLED status display
  display.display();                      // OLED status display
}


// OLED STATUS CHECK - READY
if (cureActive == false && washActive == false && IRstate == LOW && now > noteTrigger)
{
  readydisplay();                         // Set the OLED display
  display.println("Wash&Cure");           // OLED status display
  display.println(" ");                   // OLED status display
  display.print  ("  Ready");             // OLED status display
  display.display();                      // OLED status display
}


// INTERLOCK CHECK
IRstate = digitalRead(PROX);
if ( (IRstate == HIGH && washActive == true) || (IRstate == HIGH && cureActive == true) ) 
{
  StopAll();
}


// WASH CYCLE CHECK 
if (washActive == true && ( (now - lastTrigger) > (washSeconds*1000) ) ) 
  {
    Serial.println("Washing stopped by timer.");
    washoff();
  }


// CURE CYCLE CHECK  
if(cureActive == true && ( (now - lastTrigger) > (uvSeconds*1000) ) ) 
  {
    Serial.println("UV Cure stopped by timer.");
    cureoff();
  }


// STEPPER MOTOR CHECK

// IF WASHING, CHECK STEPS TO GO AND REVERSE DIRECTION IF STEPS HAVE BEEN COMPLETED
if (stepper.distanceToGo() == 0 && washActive == true)
  {
    if (washDirection == true)
    {
      stepper.setCurrentPosition(0);    // Set starting position as 0
      stepper.moveTo((washSteps* -1));  // Move stepper x steps  
      Serial.println("Changing stepper direction to reverse.");
      washDirection = false;
    }
      else
    {
      stepper.setCurrentPosition(0);    // Set starting position as 0
      stepper.moveTo(washSteps);        // Move stepper x steps
      Serial.println("Changing stepper direction to forward.");
      washDirection = true;
    }
  }

// IF WASHING, KEEP STEPPER MOVING
if (washActive == true)
{
  stepper.run();  
}

// IF CURING, KEEP STEPPER MOVING
if (cureActive == true)
{
  stepper.runSpeed();
}





////////////////////////////
////// SWITCH CONTROL //////
////////////////////////////

// UPDATE THE BOUNCE INSTANCES
debouncedSW1.update();  debouncedSW2.update();  debouncedSW3.update();   


// SW1 - Start cure function, or if a function is running - increase by one minute
if (debouncedSW1.fell() && washActive == false && cureActive == false)
  { 
    btn;
    cure();
  }
    else if( debouncedSW1.fell() && washActive == true && cureActive == false &&WashValue < 20)
      {
        btn;
        washUP();
      }
        else if( debouncedSW1.fell() && cureActive == true && washActive == false && CureValue < 20)
          {
	        btn;
          cureUP();
          }


// SW2 - Start wash function, or if a function is running - decrease by one minute
if (debouncedSW2.fell() && cureActive == false && washActive == false  )
  { 
    btn;
    wash();
  }
    else if( debouncedSW2.fell() && cureActive == true && washActive == false && CureValue > 3)
      {
        btn;
        cureDOWN();
      }
        else if( debouncedSW2.fell() && washActive == true && cureActive == false && WashValue > 3)
          {
	        btn;
          washDOWN();
          }


// SW3 - Stop any running function
if (debouncedSW3.fell() && washActive == true && cureActive == false)
  {
    btn;
    washoff();
  }
    else if( debouncedSW3.fell() && washActive == false && cureActive == true )
      {
        btn;
        cureoff();
      }

// SW3 - If nothing is running, bring up the EEPROM menu
if ( debouncedSW3.fell() && washActive == false && cureActive == false)
  {
    btn;
    eepromMenu();
  }
}
