/*

//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
//// Wash-n-Cure Rev 0.7.5 (ALPHA)
//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
-WiFi manager installed. OLED will display current IP for 10 seconds upon boot/reboot.

-Over the Air (OTA) firmware updating. Update URL displayed, but not linked - must copy/paste into browser.

-Simple web interface employing simple AJAX & JSON to change wash and cure times, commit new times to EEPROM,
 OTA firmware update (.BIN file), and stop all functions.

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
-Wash uses "stepper.moveTo(washSteps)", steps to "int washSteps = 2000;", and requires "stepper.run();" to
 keep moving until the steps are complete - allowing the motor to have directional change. The directional
 change occurs from polling "stepper.distanceToGo() == 0", then the code can take 'washSteps * -1' and
 restart the process - now running to the negative steps (turning the other way).

//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=

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
	-Pressing SW1 or SW2 while continue with cycle unless Interlock is open. If Interlock is open, SW1
	 and SW2 will be ignored.

//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=

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

//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
//// LIBRARIES & DECLARATIONS               LIBRARIES & DECLARATIONS                LIBRARIES & DECLARATIONS
//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=

// BASE LIBRARIES
#include <Arduino.h>
#include <Wire.h>
#include <Bounce2.h>
#include "index.html"

// UV, IR, AND SW PINS AND CONTROL
#define UVLED 32  // PIN:32 UV LED
#define FAN 27  // PIN:27 Fan
#define PROX 4  // PIN:04 Lid Proximity Sensor
#define SW1 35  // PIN:35 SW1 Button
#define SW2 34  // PIN:34 SW2 Button
#define SW3 0  // PIN:00 SW3 Button (Also used for programming mode)

int IRstate;  // Lid Proximity Sensor state

Bounce debouncedSW1 = Bounce();  // Bounce instance for SW1
Bounce debouncedSW2 = Bounce();  // Bounce instance for SW3
Bounce debouncedSW3 = Bounce();  // Bounce instance for SW3
#define btn delay(500)  // Wait function following button detection

// WASH AND CURE VARIABLES
#define CureDefault 20  // Factory restore value
#define WashDefault 8  // Factory restore value
int washSteps = 2000;  // Number of motor step before reversing direction when washing
boolean washDirection = false;  // Initial wash direction
boolean washActive = false;  // Initial wash state
boolean cureActive = false;  // Initial cure state
int washSeconds;  // int for wash cycle seconds
int cureSeconds;  // int for cure cycle seconds
int systemStatus;  // systemStatus to pass to web page. 100 = Ready, 2xx = cure and minutes,
//                    3xx = wash and minutes.

// TIMING VARIABLES
#define now millis()  // now = millis() for easier readability
unsigned long actionTrigger = 0;  // time trigger for logic of wash and cure cycles
unsigned long alertTrigger = 0;  // time trigger for OLED display messages

// EEPROM STORAGE
#include <EEPROM.h>
#define EEPROM_SIZE 2  // Define the number of bytes we want to access in the EEPROM
int washMinutes;  // Store the washing timer value
int cureMinutes;  // Store the curing timer value

// OLED SUPPORT for SSD1306 display connected to I2C (SDA, SCL pins)
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#define SCREEN_WIDTH 128  // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// STEPER DRIVER SUPPORT
#include <AccelStepper.h>
#define dirPin 25  // Stepper driver DIR pin
#define stepPin 33  // Stepper driver STP pin
#define motorInterfaceType 1  // Must be set to 1 when using a driver
#define motorEnable 26  // Stepper driver VDD pin
AccelStepper stepper = AccelStepper(motorInterfaceType, stepPin, dirPin);

// NETWORKING SUPPORT
#include <WebOTA.h>
#include <WiFiManager.h>
#include <ESPmDNS.h>

const char *ssid = "Wash-n-Cure";
const char *password = "password";
const char *hostname = "washncure";
WiFiManager wm;
WebServer server(80);

//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
//// FUNCTIONS          FUNCTIONS          FUNCTIONS          FUNCTIONS          FUNCTIONS         FUNCTIONS	
//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=

// READY THE OLED DISPLAY
void sendToOLED()
{
    display.clearDisplay();  // Clear the OLED display
    display.setTextSize(2);  // Set text size for display
    display.setTextColor(WHITE);  // Set text color for display
    display.setCursor(0, 10);  // Position the cursor
}

// START THE WASH FUNCTION
void wash()
{
    Serial.println("Wash Cycle ON!");
    washMinutes = EEPROM.read(0);
    Serial.print("Wash value from memory: ");
    Serial.println(washMinutes);

    washSeconds = washMinutes * 60;  // Calculate wash seconds from washMinutes (minutes)
    washActive = true;  // Set wash state to true
    actionTrigger = now;  // Reset the time trigger
    digitalWrite(FAN, HIGH);  // Turn on the fan
    digitalWrite(motorEnable, HIGH);  // Enable the motor
    stepper.setSpeed(8000);  // Set the motor speed
    stepper.setAcceleration(100);  // Set the stepper acceleration
    stepper.setCurrentPosition(0);  // Set starting position as 0
    stepper.moveTo(washSteps);  // Move stepper x steps
    washDirection = true;  // Motor moves clockwise

    sendToOLED();
    display.println("Starting");
    display.print("  Wash");
    display.display();
    alertTrigger = now + 2000;
    return;
}

// START THE CURE FUNCTION
void cure()
{
    Serial.println("UV Cycle ON!");
    cureMinutes = EEPROM.read(1);
    Serial.println("Cure value from memory");
    Serial.println(cureMinutes);

    cureSeconds = cureMinutes * 60;  // Calculate wash seconds from cureMinutes (minutes)
    cureActive = true;  // Set cure state to true
    actionTrigger = now;  // Reset the time trigger
    digitalWrite(FAN, HIGH);  // Turn on the fan
    digitalWrite(motorEnable, HIGH);  // Enable the motor
    digitalWrite(UVLED, HIGH);  // Turn on the UV lamp
    stepper.setSpeed(500);  // Set the motor speed
    stepper.runSpeed();  // Start the motor

    sendToOLED();
    display.println("Starting");
    display.print("  Cure");
    display.display();
    alertTrigger = now + 2000;
    return;
}

// STOP THE WASH FUNCTION
void washoff()
{
    digitalWrite(FAN, LOW);
    digitalWrite(motorEnable, LOW);

    washActive = false;
    Serial.println("Wash Cycle Finished! Returning to main loop");

    sendToOLED();
    display.println("Washing");
    display.print(" Done!");
    display.display();
    alertTrigger = now + 2000;
    return;
}

// STOP THE CURE FUNCTION
void cureoff()
{
    digitalWrite(FAN, LOW);
    digitalWrite(motorEnable, LOW);
    digitalWrite(UVLED, LOW);

    cureActive = false;
    Serial.println("UV Cycle Finished! Returning to main loop");

    sendToOLED();
    display.println("Curing ");
    display.print(" Done!");
    display.display();
    alertTrigger = now + 2000;
    return;
}

// STOP ALL WASH AND CURE FUNCTION
void StopAll()
{
    digitalWrite(FAN, LOW);
    digitalWrite(UVLED, LOW);
    digitalWrite(motorEnable, LOW);

    cureActive = false;  // Set cure state to false
    washActive = false;
    Serial.println("Stopping all functions!");

    sendToOLED();
    display.println("Interlock");
    display.println("  open!  ");
    display.print("");
    display.display();
    alertTrigger = now + 2000;
    return;
}

// INCREASE WASH TIME
void washUP()
{
    washMinutes = EEPROM.read(0);
    Serial.println("Increase wash time.");
    Serial.println(washMinutes);
    EEPROM.write(0, ++washMinutes);
    washMinutes = EEPROM.read(0);
    washSeconds = washMinutes * 60;
    Serial.println("New wash value from memory");
    Serial.println(washMinutes);

    sendToOLED();
    display.println("Wash time:");
    display.print(washMinutes);
    display.display();
    alertTrigger = now + 2000;
}

// DECREASE WASH TIME
void washDOWN()
{
    washMinutes = EEPROM.read(0);
    if (washMinutes > 1) 
    {
        if ((washActive == false) || (washActive == true && ((((washSeconds *1000) - (now - actionTrigger)) / 60000)) > 1))
        {
            Serial.println("Decrease wash time.");
            Serial.println(washMinutes);
            EEPROM.write(0, --washMinutes);
            washMinutes = EEPROM.read(0);
            washSeconds = washMinutes * 60;
            Serial.println("New wash value from memory");
            Serial.println(washMinutes);

            sendToOLED();
            display.println("Wash time:");
            display.print(washMinutes);
            display.display();
            alertTrigger = now + 2000;
        }
    }
}

// INCREASE CURE TIME
void cureUP()
{
    cureMinutes = EEPROM.read(1);
    Serial.println("Increase cure time.");
    Serial.println(cureMinutes);
    EEPROM.write(1, ++cureMinutes);
    cureMinutes = EEPROM.read(1);
    cureSeconds = cureMinutes * 60;
    Serial.println("New cure value from memory");
    Serial.println(cureMinutes);

    sendToOLED();
    display.println("Cure time:");
    display.print(cureMinutes);
    display.display();
    alertTrigger = now + 2000;
}

// DECREASE CURE TIME
void cureDOWN()
{
    cureMinutes = EEPROM.read(1);
    if (cureMinutes > 1) 
    {
        if ((cureActive == false) || (cureActive == true && ((((cureSeconds *1000) - (now - actionTrigger)) / 60000)) > 1))
        {
        Serial.println("Decrease cure time.");
        Serial.println(cureMinutes);
        EEPROM.write(1, --cureMinutes);
        cureMinutes = EEPROM.read(1);
        cureSeconds = cureMinutes * 60;
        Serial.println("New cure value from memory");
        Serial.println(cureMinutes);

        sendToOLED();
        display.println("Cure time:");
        display.print(cureMinutes);
        display.display();
        alertTrigger = now + 2000;
        }
    }
}

// EEPROM MENU ACTIONS
void eepromMenu()
{
    Serial.print("EEPROM Menu actions");
    sendToOLED();
    display.println("SW1-Reset");
    display.println("SW2-Save");
    display.print("SW3-Exit");
    display.display();

   	// NO DOUBLE-TAP
    debouncedSW3.update();
    delay(100);
    debouncedSW3.update();
    delay(100);

    Serial.println(".");
    actionTrigger = now;  // Reset the time trigger
    while ((now - actionTrigger) < 30000)  // Give 30 seconds for a response
    {
        debouncedSW1.update();
        debouncedSW2.update();
        debouncedSW3.update();

       	// SW1 - Reset Wash and Cure time to factory defauls
        if (debouncedSW1.fell())
        {
            btn;
            EEPROM.write(0, CureDefault);  // RESET CURE TIME
            washMinutes = EEPROM.read(0);  // READ BACK CURE TIME
            EEPROM.write(1, WashDefault);  // RESET WASH TIME
            cureMinutes = EEPROM.read(1);  // READ BACK WASH TIME
            EEPROM.commit();  // Commit changes to EEPROM

            sendToOLED();
            display.println("Wash&Cure");
            display.println(" EEPROM  ");
            display.print(" reset!  ");
            display.display();
            alertTrigger = now + 2000;
            return;  // exit this function
        }

       	// SW2 - Save current setting to EEPROM
        if (debouncedSW2.fell())
        {
            btn;
            EEPROM.commit();
            sendToOLED();
            display.println("Wash&Cure");
            display.println("  times  ");
            display.print("  saved");
            display.display();
            alertTrigger = now + 2000;
            return;  // exit this function
        }

       	// SW3 - Exit EEPROM menu
        if (debouncedSW3.fell())
        {
            btn;
            return;  // Exit this function
        }
    }
    Serial.println("Timed-out of EEPROM Menu.");
}

//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
//// WEB INTERFACE         WEB INTERFACE          WEB INTERFACE          WEB INTERFACE         WEB INTERFACE
//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=

// SERVER THE CONTENTS OF THE INDEX.HTML FILE
void handleRoot()
{
    String s = webpage;
    server.send(200, "text/html", s);
}

// 404 - NOT FOUND WEB RESPONSE
void handleNotFound()
{
    server.send(404, "text/plain", "404: Page does not exist.\n\n");
}

// This sends the JSON formatted data to the webpage. The systemStatus portion gives one of three posible conditions with time
// remaining; 100 = Idle/Ready, 2xx = Curing with xx minutes to go, 3xx - Washing with xx minutes to go. This three digit status
// is received by the index/root page and parsed the java to yeild a more human status such as, "Washing, 20 minutes to go."
void wncInfo()
{
    if (cureActive == false && washActive == false) systemStatus = 100;
    if (cureActive == true && washActive == false) systemStatus = 200 + ((((cureSeconds *1000) - (now - actionTrigger)) / 60000));
    if (cureActive == false && washActive == true) systemStatus = 300 + ((((washSeconds *1000) - (now - actionTrigger)) / 60000));
    server.send(200, "text/plane", "[" + String(washMinutes) + "," + String(cureMinutes) + "," + String(systemStatus) + "]");
}

void wncChange()
{
    String webAction = server.arg("go");
    Serial.print("Received webAction :");
    Serial.println(webAction);

    if (webAction == "1")  // Wash Time Increase by one minute
    {
        washUP();
        wncInfo();  // Send back updated data
    }
    else if (webAction == "2")  // Wash Time Decrease by one minute
    {
        washDOWN();
        wncInfo();
    }
    else if (webAction == "3")  // Cure Time Increase by one minute
    {
        cureUP();
        wncInfo();
    }
    else if (webAction == "4")  // Cure Time Decrease by one minute
    {
        cureDOWN();
        wncInfo();
    }
    else if (webAction == "5")  // Commit time changes to EEPROM
    {
        EEPROM.commit();
        sendToOLED();
        display.println("Wash&Cure");
        display.println("saving  ");
        display.print("settings");
        display.display();
        alertTrigger = millis() + 4000;
        wncInfo();  // Send back updated data
    }
    else if (webAction == "6")  // Stop All
    {
        StopAll();
        wncInfo();  // Send back updated data
    }
}

// EepromSave WEB RESPONSE
void handleEepromSave()
{
    EEPROM.commit();
    sendToOLED();
    display.println("Wash&Cure");
    display.println("  times  ");
    display.print("  saved");
    display.display();
    alertTrigger = now + 2000;
    handleRoot();
}

//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
//// SETUP           SETUP          SETUP            SETUP            SETUP           SETUP            SETUP	
//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=

void setup()
{
    // SET UP COMMINUCATIONS
    Serial.begin(9600);  // Set board's serial speed for terminal communcation

    WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP
    // WiFi.mode(WiFi_STA); // it is a good practice to make sure your code sets wifi mode how you want it.

    wm.setHostname(hostname);

    //WiFiManager, Local intialization. Once its business is done, there is no need to keep it around
    WiFiManager wm;

    //reset settings - wipe credentials for testing
    //wm.resetSettings();

    // Automatically connect using saved credentials,
    // if connection fails, it starts an access point with the specified name ( "Wash-n-Cure"),
    // if empty will auto generate SSID, if password is blank it will be anonymous AP (wm.autoConnect())
    // then goes into a blocking loop awaiting configuration and will return success result

    bool res;
    // res = wm.autoConnect(); // auto generated AP name from chipid
    // res = wm.autoConnect("Wash-n-Cure"); // anonymous ap
    res = wm.autoConnect("Wash-n-Cure","password"); // password protected ap

    if(!res) {
        Serial.println("Failed to connect");
        // ESP.restart();
    } 
    else {
        //if you get here you have connected to the WiFi    
        Serial.println("WiFi Connected...");
    }

    if(!MDNS.begin(hostname)) {
     Serial.println("Error starting mDNS");
     return;
    }
    MDNS.addService("http", "tcp", 80);
    Serial.println(WiFi.localIP());

    webota.init(777, "/update");  // Setup web over-the-air update port and directory


   // INITIALIZE EEPROM
    EEPROM.begin(EEPROM_SIZE);
    washMinutes = EEPROM.read(0);  // Read washMinutes from EEPROM location 0
    cureMinutes = EEPROM.read(1);  // Read cureMinutes from EEPROM location 1

   // SET UP STEPPER CONTROL
    stepper.setMaxSpeed(8000);  // Set max stepper speed to 8000
    stepper.setAcceleration(100);  // Set stepper acceleration to 100

   // DEFINE GPIO FUNCTIONS
    pinMode(PROX, INPUT);  // Set PROX pin as an input

    pinMode(motorEnable, OUTPUT);  // Set motorEnable pin as an output
    digitalWrite(motorEnable, LOW);  // Turn motor off

    pinMode(UVLED, OUTPUT);  // Set UVLED pin as an output
    digitalWrite(UVLED, LOW);  // ! ! ! NEVER SET THIS HIGH ! MOSFET DAMAGE WILL OCCUR ! ! !

    pinMode(FAN, OUTPUT);  // Set FAN pin as an output
    digitalWrite(FAN, LOW);  // ! ! ! NEVER SET THIS HIGH ! MOSFET DAMAGE WILL OCCUR ! ! !

    pinMode(SW1, INPUT);  // Set SW1 pin as an input
    debouncedSW1.attach(SW1);  // attach debouncedSW1 to SW1
    debouncedSW1.interval(25);  // 25 ms bounce interval

    pinMode(SW2, INPUT);  // Set SW2 pin as an input
    debouncedSW2.attach(SW2);  // attach debouncedSW2 to SW2
    debouncedSW2.interval(25);  // 25 ms bounce interval 

    pinMode(SW3, INPUT);  // Set SW3 pin as an input
    debouncedSW3.attach(SW3);  // attach debouncedSW3 to SW3
    debouncedSW3.interval(25);  // 25 ms bounce interval

   // OLED INITIALIZATION
    Wire.begin(21, 22);  //OLED I2C Pins
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))  // Address for the I2C OLED screen
    {
        Serial.println(F("SSD1306 allocation failed"));
        for (;;);
    }

    sendToOLED();
    display.println("WnC 0.7.5");
    display.println(WiFi.localIP());
    display.display();
    alertTrigger = now + 10000;

    // WEB PAGE CONFIGURATIONS

    // WHEN THE SERVER GETS A REQUEST FOR A PAGE, CALL ITS FUNCTION.
    server.on("/", handleRoot);
    server.on("/wncchange", wncChange);
    server.on("/wncinfo", wncInfo);

    server.onNotFound(handleNotFound);

    server.begin();
    Serial.print("SETUP Complete");

    // WAIT FOR SW3(PIN 0) TO RETURN TO A NORMAL INPUT
    actionTrigger = now;  // Reset the time trigger
    while ((now - actionTrigger) < 2000)  // 2 seconds to clear SW3
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

//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
//// LOOP         LOOP           LOOP          LOOP          LOOP           LOOP          LOOP          LOOP	
//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=

void loop()
{
    //MDNS.update();
    // RUN OTA UPDATE SERVICE  
    webota.handle();

   // RUN THE WEB SERVER
   server.handleClient();

   // OLED STATUS CHECK - WASHING
   if (washActive == true && now > alertTrigger)
   {
       sendToOLED();
       display.println("Washing...");
       display.print((((washSeconds *1000) - (now - actionTrigger)) / 60000));
       display.println(" minutes remaining");
       display.display();
    }
   
    // OLED STATUS CHECK - CURING
    if (cureActive == true && now > alertTrigger)
    {
        sendToOLED();
        display.println("Curing...");
        display.print((((cureSeconds *1000) - (now - actionTrigger)) / 60000));
        display.println(" minutes remaining");
        display.display();
    }

    // OLED STATUS CHECK - READY
    if (cureActive == false && washActive == false && IRstate == LOW && now > alertTrigger)
    {
        sendToOLED();
        display.println("Wash&Cure");
        display.println(" ");
        display.print("  Ready");
        display.display();
    }

    // INTERLOCK CHECK
    IRstate = digitalRead(PROX);
    if ((IRstate == HIGH && washActive == true) || (IRstate == HIGH && cureActive == true))
    {
        StopAll();
    }

    // WASH CYCLE CHECK 
    if (washActive == true && ((now - actionTrigger) > (washSeconds *1000)))
    {
        Serial.println("Washing stopped by timer.");
        washoff();
    }

    // CURE CYCLE CHECK  
    if (cureActive == true && ((now - actionTrigger) > (cureSeconds *1000)))
    {
        Serial.println("UV Cure stopped by timer.");
        cureoff();
    }

    // STEPPER MOTOR CHECK - IF WASHING, CHECK STEPS TO GO AND REVERSE DIRECTION IF STEPS HAVE BEEN COMPLETED
    if (stepper.distanceToGo() == 0 && washActive == true)
    {
        if (washDirection == true)
        {
            stepper.setCurrentPosition(0);  // Set starting position as 0
            stepper.moveTo((washSteps *-1));  // Move stepper x steps  
            Serial.println("Changing stepper direction to reverse.");
            washDirection = false;
        }
        else
        {
            stepper.setCurrentPosition(0);  // Set starting position as 0
            stepper.moveTo(washSteps);  // Move stepper x steps
            Serial.println("Changing stepper direction to forward.");
            washDirection = true;
        }
    }

    // STEPPER MOTOR CHECK - IF WASHING, KEEP STEPPER MOVING
    if (washActive == true)
    {
        stepper.run();
    }

    // STEPPER MOTOR CHECK - IF CURING, KEEP STEPPER MOVING
    if (cureActive == true)
    {
        stepper.runSpeed();
    }

//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
//// SWITCH CONTROL              SWITCH CONTROL                SWITCH CONTROL                 SWITCH CONTROL	
//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=

   // UPDATE THE BOUNCE INSTANCES
   debouncedSW1.update();
   debouncedSW2.update();
   debouncedSW3.update();

   // SW1 - Start cure function, or if a function is running - increase by one minute
   if (debouncedSW1.fell() && washActive == false && cureActive == false)
   {
        btn;
        cure();
   }
   else if (debouncedSW1.fell() && washActive == true && cureActive == false && washMinutes < 20)
   {
        btn;
        washUP();
   }
   else if (debouncedSW1.fell() && cureActive == true && washActive == false && cureMinutes < 20)
   {
        btn;
        cureUP();
   }

   // SW2 - Start wash function, or if a function is running - decrease by one minute
   if (debouncedSW2.fell() && cureActive == false && washActive == false)
   {
        btn;
        wash();
   }
   else if (debouncedSW2.fell() && cureActive == true && washActive == false && cureMinutes > 3)
   {
        btn;
        cureDOWN();
   }
   else if (debouncedSW2.fell() && washActive == true && cureActive == false && washMinutes > 3)
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
   else if (debouncedSW3.fell() && washActive == false && cureActive == true)
   {
        btn;
        cureoff();
   }

   // SW3 - If nothing is running, bring up the EEPROM menu
   if (debouncedSW3.fell() && washActive == false && cureActive == false)
   {
        btn;
        eepromMenu();
   }
}
