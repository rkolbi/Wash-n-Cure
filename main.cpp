/*

//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
//// Wash-n-Cure Rev 0.8.0 (ALPHA)
//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
-WiFi manager installed. OLED will display current IP for 10 seconds upon boot/reboot.

-Over the Air (OTA) firmware updating. Update link is the revision number displayed in the footer.

-Simple web interface employing simple AJAX & JSON to change wash and cure times, commit new times
 to EEPROM, OTA firmware update (.BIN file), and stop all functions.

-Button functions as:
         When no functions are active: SW1 = Starts Cure        SW2 = Starts Wash       SW3 = EEPROM Menu
         Wash or Cure active:          SW1 = Run time +1        SW2 = Run time -1       SW3 = Pause Menu

-EEPROM: No times changes are fully committed to EEPROM (reboot will revert back) unless the 'Save Times'
         function is selected from the web interface or from EEPROM menu.
         EEPROM Menu: (User has 10 seconds to make selection, else it exits the menu LOCKING loop)
        -SW1 will eeprom.write wash/cure time to 'factory defaults'
        -SW2 will eeprom.write wash/cure times if they differ the eeprom.read.
        -SW3 will cancel the menu, returning to 'Ready'

-PAUSE: 10 minute timer until StopAll is called. All web functions are ignored except Pause and Stop.
        Pause Menu:
        -SW1 will unpause
        -SW2 will stopall

-Wash and Cure functins use different stepper motor controls.
-Cure uses the "stepper.setSpeed(500)" and requires "stepper.runSpeed()" to keep moving - yeilding a
 constants turning motor.
-Wash uses "stepper.moveTo(washSteps)", steps to "int washSteps = 2000;", and requires
 "stepper.run();" to keep moving until the steps are complete - allowing the motor to have directional
 change. The directional change occurs from polling "stepper.distanceToGo() == 0", then the code can
 take 'washSteps * -1' and restart the process - now running to the negative steps (turning the other way).


To do:
//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
-Operational testing, ensure the following functions are operational / interoperational:
    1 - OTA firmware update & web interface
    1 - StopAll function from web interface
    1 - Time management from web interface
    1 - Time management from button
    1 - Pause function from web interface
    1 - Pause function from button
    0 - Stepper motion control (wash)
    0 - Stepper motion control (cure)

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

// BOARD & BASE LIBRARIES
#include <Arduino.h>
#include <Wire.h>
#include <Bounce2.h>
#include "index.html"

// UV, IR, AND SW PINS AND CONTROL
#define UVLED 32 // PIN:32 UV LED
#define FAN 27 // PIN:27 Fan
#define PROX 4 // PIN:04 Lid Proximity Sensor
#define SW1 35 // PIN:35 SW1 Button
#define SW2 34 // PIN:34 SW2 Button
#define SW3 0 // PIN:00 SW3 Button (Also used for programming mode)
int safetyInterlock; // Lid Proximity Sensor state

Bounce debouncedSW1 = Bounce(); // Bounce instance for SW1
Bounce debouncedSW2 = Bounce(); // Bounce instance for SW3
Bounce debouncedSW3 = Bounce(); // Bounce instance for SW3

// WASH AND CURE VARIABLES
#define CureDefault 20 // Factory restore value
#define WashDefault 8 // Factory restore value
int washSteps = 1000000; // Number of motor step before reversing direction when washing
int washSpeed = 10000;
int cureSpeed = 200;
int accelSpeed = 1000;
boolean washDirection = false; // Initial wash direction
boolean washActive = false; // Initial wash state
boolean cureActive = false; // Initial cure state
boolean pauseActive = false; // Initial pause state
int systemStatus; // systemStatus to pass to web page. 100 = Ready, 2xx = cure and minutes,
//                   3xx = wash and minutes, 4xx = paused-cure and minutes,
//                   5xx = paused-wash and minutes.

// TIMING VARIABLES
#define now millis() // now = millis() for easier readability
unsigned long cycleStartTime = 0; // time trigger for logic of wash and cure cycles
unsigned long cyclePauseTime = 0; // stores the time mark when the pause state was initiated
unsigned long cycleElapsedTime = 0; // stores how much time the cycle was run for, before being paused
unsigned long messageDurationTime = 0; // time trigger for OLED display messages
unsigned long messageSent = 0;

// EEPROM STORAGE
#include <EEPROM.h>
#define EEPROM_SIZE 2 // Define the number of bytes we want to access in the EEPROM
int washMinutes; // Store the washing timer value
int cureMinutes; // Store the curing timer value

// OLED SUPPORT for SSD1306 display connected to I2C (SDA, SCL pins)
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);


// STEPER DRIVER SUPPORT - A4988 Step Pin = ESP Pin 33, A4988 Direction Pin = ESP Pin 25 
#include <AccelStepper.h>
#define motorEnable 26 // A4988 VDD Pin = ESP Pin 26
AccelStepper stepper(AccelStepper::DRIVER, 33, 25);

// NETWORKING SUPPORT
#include <WebOTA.h>
#include <WiFiManager.h>
#include <ESPmDNS.h>
const char* ssid = "WnC-Setup";
const char* password = "password";
const char* hostname = "washNcure";
WebServer server(80);




//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
//// FUNCTIONS          FUNCTIONS          FUNCTIONS          FUNCTIONS          FUNCTIONS         FUNCTIONS
//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=

// READY THE OLED DISPLAY
//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
void sendToOLED()
{
    display.clearDisplay(); // Clear the OLED display
    display.setTextSize(2); // Set text size for display
    display.setTextColor(WHITE); // Set text color for display
    display.setCursor(0, 10); // Position the cursor
}

// START THE WASH FUNCTION
//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
void wash()
{
    Serial.print("Starting WASH cycle - ");
    washMinutes = EEPROM.read(0);
    Serial.print(washMinutes);
    Serial.print(" minutes.");

    washActive = true; // Set wash state to true
    cycleStartTime = now; // Reset the time trigger
    digitalWrite(FAN, HIGH); // Turn on the fan
    digitalWrite(motorEnable, HIGH); // Enable the motor
    stepper.setSpeed(washSpeed); // Set the motor speed
    stepper.setCurrentPosition(0); // Set starting position as 0
    stepper.moveTo(washSteps); // Move stepper x steps
    washDirection = true; // Motor moves clockwise

    sendToOLED();
    display.println("Starting");
    display.print("  Wash");
    display.display();
    messageDurationTime = now + 2000;
    return;
}

// START THE CURE FUNCTION
//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
void cure()
{
    Serial.print("Starting CURE cycle - ");
    cureMinutes = EEPROM.read(1);
    Serial.print(cureMinutes);
    Serial.print(" minutes.");

    cureActive = true; // Set cure state to true
    cycleStartTime = now; // Reset the time trigger
    digitalWrite(FAN, HIGH); // Turn on the fan
    digitalWrite(motorEnable, HIGH); // Enable the motor
    digitalWrite(UVLED, HIGH); // Turn on the UV lamp
    stepper.setSpeed(cureSpeed); // Set the motor speed
    stepper.runSpeed(); // Start the motor

    sendToOLED();
    display.println("Starting");
    display.print("  Cure");
    display.display();
    messageDurationTime = now + 2000;
    return;
}

// STOP ALL WASH AND CURE FUNCTION
//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
void StopAll()
{
    digitalWrite(FAN, LOW);
    digitalWrite(UVLED, LOW);
    digitalWrite(motorEnable, LOW);
    sendToOLED();
    if (cureActive == true)
    {
        Serial.println("Cure Cycle Finished!");
        display.println("Curing ");
        display.println(" Done!");
    }
    else
    {
        Serial.println("Wash Cycle Finished!");
        display.println("Washing");
        display.println(" Done!");
    }
    cureActive = false;
    washActive = false;
    pauseActive = false;
    display.display();
    messageDurationTime = now + 2000;
    return;
}

// PAUSE ACTIVE WASH OR CURE FUNCTION
//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
void cyclePause()
{
    if (washActive == true || cureActive == true)
    {
        pauseActive = true;
        cyclePauseTime = now;
        cycleElapsedTime = now - cycleStartTime; // if unpaused, cycleStartTime should = now - cycleElapsedTime 
        digitalWrite(FAN, LOW);
        digitalWrite(UVLED, LOW);
        digitalWrite(motorEnable, LOW);
        Serial.println(" Paused.");
    }
}

// UNPAUSE ACTIVE WASH OR CURE FUNCTION
//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
void cycleUnPause()
{
    if ((safetyInterlock == LOW) && (washActive == true || cureActive == true))
    {
        if (washActive == true)
        {
            digitalWrite(FAN, HIGH); // Turn on the fan
            digitalWrite(motorEnable, HIGH); // Enable the motor
            stepper.setSpeed(washSpeed); // Set the motor speed
            stepper.setCurrentPosition(0); // Set starting position as 0
            stepper.moveTo(washSteps); // Move stepper x steps
            washDirection = true; // Motor moves clockwise
        }
        else
        {
            digitalWrite(FAN, HIGH); // Turn on the fan
            digitalWrite(motorEnable, HIGH); // Enable the motor
            digitalWrite(UVLED, HIGH); // Turn on the UV lamp
            stepper.setSpeed(cureSpeed); // Set the motor speed
            stepper.runSpeed(); // Start the motor
        }
        if (washActive == true)
            Serial.print("Wash ");
        if (cureActive == true)
            Serial.print("Cure ");
        Serial.println(" UN-Paused.");
        cycleStartTime = now - cycleElapsedTime;
        pauseActive = false;
    }
    return; // exit this function
}

// INCREASE WASH TIME
//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
void washUP()
{
    washMinutes = EEPROM.read(0);
    EEPROM.write(0, ++washMinutes);
    washMinutes = EEPROM.read(0);
    Serial.print("+New wash time: ");
    Serial.println(washMinutes);

    sendToOLED();
    display.println("Wash time:");
    display.print(washMinutes);
    display.display();
    messageDurationTime = now + 2000;
}

// DECREASE WASH TIME
//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
void washDOWN()
{
    washMinutes = EEPROM.read(0);
    if (washMinutes > 1)
    {
        if ((washActive == false) || (washActive == true && (washMinutes - ((now - cycleStartTime) / 60000)) > 1))
        {
            EEPROM.write(0, --washMinutes);
            washMinutes = EEPROM.read(0);
            Serial.println("-New wash time: ");
            Serial.print(washMinutes);

            sendToOLED();
            display.println("Wash time:");
            display.print(washMinutes);
            display.display();
            messageDurationTime = now + 2000;
        }
    }
}

// INCREASE CURE TIME
//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
void cureUP()
{
    cureMinutes = EEPROM.read(1);
    EEPROM.write(1, ++cureMinutes);
    cureMinutes = EEPROM.read(1);
    Serial.print("+New cure time: ");
    Serial.println(cureMinutes);

    sendToOLED();
    display.println("Cure time:");
    display.print(cureMinutes);
    display.display();
    messageDurationTime = now + 2000;
}

// DECREASE CURE TIME
//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
void cureDOWN()
{
    cureMinutes = EEPROM.read(1);
    if (cureMinutes > 1)
    { 
        if ((cureActive == false) || (cureActive == true && (cureMinutes - ((now - cycleStartTime) / 60000)) > 1))
        {
            EEPROM.write(1, --cureMinutes);
            cureMinutes = EEPROM.read(1);
            Serial.print("-New cure time: ");
            Serial.println(cureMinutes);

            sendToOLED();
            display.println("Cure time:");
            display.print(cureMinutes);
            display.display();
            messageDurationTime = now + 2000;
        }
    }
}

// EEPROM MENU ACTIONS
//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
void eepromMenu()
{
    Serial.print("EEPROM:");
    sendToOLED();
    display.println("SW1-Reset");
    display.println("SW2-Save");
    display.print("SW3-Exit");
    display.display();

    cycleStartTime = now; // Reset the time trigger
    while ((now - cycleStartTime) < 10000) // Give 10 seconds for a response
    {
        debouncedSW1.update();
        debouncedSW2.update();
        debouncedSW3.update();

        // SW1 - Reset Wash and Cure time to factory defauls
        if (debouncedSW1.fell())
        {
            EEPROM.write(0, WashDefault); // RESET CURE TIME
            washMinutes = EEPROM.read(0); // READ BACK CURE TIME
            EEPROM.write(1, CureDefault); // RESET WASH TIME
            cureMinutes = EEPROM.read(1); // READ BACK WASH TIME
            EEPROM.commit(); // Commit changes to EEPROM

            sendToOLED();
            display.println("Wash&Cure");
            display.println(" EEPROM");
            display.print(" reset.");
            display.display();
            messageDurationTime = now + 2000;
            return; // exit this function
        }

        // SW2 - Save current setting to EEPROM
        if (debouncedSW2.fell())
        {
            EEPROM.commit();
            sendToOLED();
            display.println("Wash&Cure");
            display.println("  times");
            display.print("  saved.");
            display.display();
            messageDurationTime = now + 2000;
            return; // exit this function
        }

        // SW3 - Exit EEPROM menu
        if (debouncedSW3.fell())
            return; // Exit this function
    }
    Serial.println("Timed-out of EEPROM Menu.");
}




// WEB INTERFACE
//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=

// SERVE THE CONTENTS OF THE INDEX.HTML FILE
//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
void handleRoot()
{
    String s = webpage;
    server.send(200, "text/html", s);
}

// 404 - NOT FOUND WEB RESPONSE
//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
void handleNotFound()
{
    server.send(404, "text/plain", "404: Page does not exist.\n\n");
}

// SEND JSON INFORMATION TO WEB PAGE
//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
void wncInfo()
{
    if (cureActive == false && washActive == false)
        systemStatus = 50;
    if (pauseActive == true)
    {
        if (cureActive == true)
            systemStatus = 401 + (cureMinutes - (cycleElapsedTime / 60000));
        if (washActive == true)
            systemStatus = 501 + (washMinutes - (cycleElapsedTime / 60000));
    }
    else
    {
        if (cureActive == true)
            systemStatus = 201 + (cureMinutes - ((now - cycleStartTime) / 60000));
        if (washActive == true)
            systemStatus = 301 + (washMinutes - ((now - cycleStartTime) / 60000));
    }
    server.send(200, "text/plane", "[" + String(washMinutes) + "," + String(cureMinutes) + "," + String(systemStatus) + "]");
}

// RECEIVE COMMAND FROM WEB PAGE
//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
void wncChange()
{
    String webAction = server.arg("go");
    Serial.print("Received webAction :");
    Serial.println(webAction);

    if (webAction == "1" && pauseActive == false) // Wash Time Increase by one minute
    {
        washUP();
        wncInfo(); // Ackback
    }
    else if (webAction == "2" && pauseActive == false) // Wash Time Decrease by one minute
    {
        washDOWN();
        wncInfo();
    }
    else if (webAction == "3" && pauseActive == false) // Cure Time Increase by one minute
    {
        cureUP();
        wncInfo();
    }
    else if (webAction == "4" && pauseActive == false) // Cure Time Decrease by one minute
    {
        cureDOWN();
        wncInfo();
    }
    else if (webAction == "5" && pauseActive == false) // Commit time changes to EEPROM
    {
        EEPROM.commit();
        sendToOLED();
        display.println("Wash&Cure");
        display.println("saving");
        display.print("settings");
        display.display();
        messageDurationTime = millis() + 4000;
        wncInfo(); // Ackback
    }
    else if (webAction == "6") // Pause
    {
        if (washActive == true || cureActive == true)
        {
            if (pauseActive == false)
            {
                cyclePause();
                wncInfo(); // Ackback
            }
            else
            {
                cycleUnPause();
                wncInfo();
            }
        }
    }
    else if (webAction == "7") // Stop All
    {
        if (washActive == true || cureActive == true)
        {
            StopAll();
            wncInfo(); // Ackback
        }
    }
}

// EEPROM Save WEB RESPONSE
//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
void handleEepromSave()
{
    EEPROM.commit();
    sendToOLED();
    display.println("Wash&Cure");
    display.println("  times");
    display.print("  saved.");
    display.display();
    messageDurationTime = now + 2000;
    handleRoot();
}

//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
//// SETUP           SETUP           SETUP            SETUP            SETUP           SETUP           SETUP
//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
void setup()
{
   disableCore0WDT(); // Stop the watchdog timer

    // SET UP SERIAL PORT
    Serial.begin(9600); // Set board's serial speed for terminal communcation

    // OLED INITIALIZATION
    Wire.begin(21, 22); // OLED I2C Pins
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) // Address for the I2C OLED screen
    {
        Serial.println(F("SSD1306 allocation failed"));
        for (;;)
            ;
    }

    // SET UP NETWORK
    //=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
    // Show unit starting and give AP incase WiFi not yet set.
    display.clearDisplay(); // Clear the OLED display
    display.setTextSize(1); // Set text size for display
    display.setTextColor(WHITE); // Set text color for display
    display.setCursor(0, 10); // Position the cursor
    display.println("Starting Wash-n-Cure.");
    display.println("Set units's WiFi by");
    display.println("connecting to:");
    display.println("");
    display.print("AP: ");
    display.println(ssid);
    display.println("PWD: password");
    display.display();

    WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP

    // WiFiManager, Local intialization. Once its business is done, there is no need to keep it
    // around
    WiFiManager wm;

    // reset settings - wipe credentials for testing
    // wm.resetSettings();

    // Automatically connect using saved credentials,
    // if connection fails, it starts an access point with the specified name ( "Wash-n-Cure"),
    // if empty will auto generate SSID, if password is blank it will be anonymous AP
    // (wm.autoConnect())
    // then goes into a blocking loop awaiting configuration and will return success result

    bool res;
    // res = wm.autoConnect(); // auto generated AP name from chipid
    // res = wm.autoConnect(ssid); // anonymous ap
    res = wm.autoConnect(ssid, "password"); // password protected ap

    if (!res)
    {
        Serial.println("Failed to connect");
        // ESP.restart();
    }
    else
    {
        Serial.println("WiFi Connected...");
    }

    if (!MDNS.begin(hostname))
    {
        Serial.println("Error starting mDNS");
        return;
    }
    MDNS.addService("http", "tcp", 80);
    Serial.println(WiFi.localIP());

    webota.init(777, "/update"); // Setup web over-the-air update port and directory

    // SET UP STEPPER CONTROL
    stepper.setMaxSpeed(washSpeed); // Set max stepper speed to 8000
    stepper.setAcceleration(accelSpeed); // Set stepper acceleration to 100

    // DEFINE GPIO FUNCTIONS
    pinMode(PROX, INPUT); // Set PROX pin as an input

    pinMode(motorEnable, OUTPUT); // Set motorEnable pin as an output
    digitalWrite(motorEnable, LOW); // Turn motor off

    pinMode(UVLED, OUTPUT); // Set UVLED pin as an output
    digitalWrite(UVLED, LOW); // ! ! ! NEVER SET THIS HIGH ! MOSFET DAMAGE WILL OCCUR ! ! !

    pinMode(FAN, OUTPUT); // Set FAN pin as an output
    digitalWrite(FAN, LOW); // ! ! ! NEVER SET THIS HIGH ! MOSFET DAMAGE WILL OCCUR ! ! !

    pinMode(SW1, INPUT); // Set SW1 pin as an input
    debouncedSW1.attach(SW1); // attach debouncedSW1 to SW1
    debouncedSW1.interval(25); // 25 ms bounce interval

    pinMode(SW2, INPUT); 
    debouncedSW2.attach(SW2); 
    debouncedSW2.interval(25); 

    pinMode(SW3, INPUT); 
    debouncedSW3.attach(SW3); 
    debouncedSW3.interval(25); 

    // INITIALIZE EEPROM
    EEPROM.begin(EEPROM_SIZE);
    washMinutes = EEPROM.read(0); // Read washMinutes from EEPROM location 0
    cureMinutes = EEPROM.read(1); // Read cureMinutes from EEPROM location 1
    
    // CHECK EEPROM VALUES FOR WASH AND CURE
    if (washMinutes > 50)
    {
        EEPROM.write(0, WashDefault); // RESET WASH TIME
        washMinutes = EEPROM.read(0); // READ BACK WASH TIME
        EEPROM.write(1, CureDefault); // RESET CURE TIME
        cureMinutes = EEPROM.read(1); // READ BACK CURE TIME
        EEPROM.commit(); // Commit changes to EEPROM
    }

    // Show version and IP on OLED
    sendToOLED();
    display.println("WnC 0.8.0");
    display.println(WiFi.localIP());
    display.display();
    messageDurationTime = now + 10000;

    // WEB PAGE CONFIGURATIONS - WHEN THE SERVER GETS A REQUEST FOR A PAGE, CALL ITS FUNCTION.
    server.on("/", handleRoot);
    server.on("/wncchange", wncChange);
    server.on("/wncinfo", wncInfo);
    server.onNotFound(handleNotFound);
    server.begin();
   
    Serial.print("Startup Complete");

    // WAIT FOR SW3(PIN 0) TO RETURN TO A NORMAL INPUT
    cycleStartTime = now; // Reset the time trigger
    while ((now - cycleStartTime) < 2000) // 2 seconds to clear SW3
    {
        debouncedSW3.update();
        if (debouncedSW3.fell())
            Serial.print(".");
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
    // RUN OTA UPDATE SERVICE
    if (washActive == false || (washActive == true && pauseActive == true))
        webota.handle();

    // RUN THE WEB SERVER
    if (washActive == false || (washActive == true && pauseActive == true))
        server.handleClient();

    // CHECK IF PAUSED
    if (pauseActive == true)
    {
        sendToOLED();
        if (washActive == true)
            display.println("Paused Wsh");
        if (cureActive == true)
            display.println("Paused Cur");
        display.println("SW1-Resume");
        display.println("SW2-Cancel");
        display.display();
        
        if ((now - cyclePauseTime) > 600000) // CHECK IF PAUSE TIME HAS EXPIRED
            StopAll();
    }
    else
    {
        // OLED STATUS CHECK - WASHING
        if (washActive == true && now > messageDurationTime && (washMinutes - ((now - cycleStartTime) / 60000)) != messageSent)
        {
            sendToOLED();
            display.println("Washing...");
            display.print((washMinutes - ((now - cycleStartTime) / 60000)));
                if ((washMinutes - ((now - cycleStartTime) / 60000)) > 1)
                {
                    display.println(" minutes");
                }
                else
                {
                    display.println(" minute");
                }
            display.println("remaining.");
            display.display();
            messageSent = (washMinutes - ((now - cycleStartTime) / 60000));
        }

        // OLED STATUS CHECK - CURING
        if (cureActive == true && now > messageDurationTime && (cureMinutes - ((now - cycleStartTime) / 60000)) != messageSent)
        {
            sendToOLED();
            display.println("Curing...");
            display.print((cureMinutes - ((now - cycleStartTime) / 60000)));
                if ((cureMinutes - ((now - cycleStartTime) / 60000)) > 1)
                {
                    display.println(" minutes");
                }
                else
                {
                    display.println(" minute");
                }
            display.println("remaining.");
            display.display();
            messageSent = (cureMinutes - ((now - cycleStartTime) / 60000));
        }

        // OLED STATUS CHECK - READY
        if (cureActive == false && washActive == false && safetyInterlock == LOW && now > messageDurationTime)
        {
            sendToOLED();
            display.println("Wash&Cure");
            display.println(" ");
            display.print("  Ready");
            display.display();
        }

        // INTERLOCK CHECK
        safetyInterlock = digitalRead(PROX);
        if ((safetyInterlock == HIGH) && (washActive == true || cureActive == true))
        {
            Serial.println("Interlock tripped.");
            cyclePause();
        }

        // STEPPER MOTOR CHECK - IF CURING, KEEP STEPPER MOVING
        if (cureActive == true)
        {
            stepper.runSpeed();
            if ((now - cycleStartTime) > (cureMinutes * 60000)) // CURE CYCLE CHECK
                StopAll();
        
        }

        // STEPPER MOTOR CHECK - IF WASHING, KEEP STEPPER MOVING
        if (washActive == true)
        {
            stepper.run();

            // STEPPER MOTOR CHECK - IF WASHING, CHECK STEPS TO GO AND REVERSE DIRECTION IF STEPS COMPLETED
            if (stepper.distanceToGo() == 0)
            {
                stepper.setCurrentPosition(0); // Set starting position as 0
                Serial.println("Changing stepper direction.");
                if (washDirection == true)
                {
                    stepper.moveTo((washSteps * -1)); // Move stepper x steps
                    washDirection = false;
                }
                else
                {
                    stepper.moveTo(washSteps); // Move stepper x steps
                    washDirection = true;
                }
            }
            if ((now - cycleStartTime) > (washMinutes * 60000)) // WASH CYCLE CHECK
                StopAll();
        }
    }

 
    // SWITCH CONTROL
    //=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=

    // UPDATE THE BOUNCE INSTANCES
    debouncedSW1.update();
    debouncedSW2.update();
    debouncedSW3.update();

    if (pauseActive == true)
    {
        if (debouncedSW1.fell())
            cycleUnPause();
        if (debouncedSW2.fell())
            StopAll();
    }
    else
    {
        // SW1 - Start cure function, or if a function is running - increase by one minute
        if (debouncedSW1.fell() && washActive == false && cureActive == false)
        {
            cure();
        }
        else if (debouncedSW1.fell() && washActive == true)
        {
            washUP();
        }
        else if (debouncedSW1.fell() && cureActive == true)
        {
            cureUP();
        }

        // SW2 - Start wash function, or if a function is running - decrease by one minute
        if (debouncedSW2.fell() && cureActive == false && washActive == false)
        {
            wash();
        }
        else if (debouncedSW2.fell() && cureActive == true)
        {
            cureDOWN();
        }
        else if (debouncedSW2.fell() && washActive == true)
        {
            washDOWN();
        }

        // SW3 - Pause running function
        if (debouncedSW3.fell() && (washActive == true || cureActive == true))
        {
            if (pauseActive == false)
                cyclePause();
        }

        // SW3 - If nothing is running, bring up the EEPROM menu
        if (debouncedSW3.fell() && washActive == false && cureActive == false)
        {
            eepromMenu();
        }
    }
}
