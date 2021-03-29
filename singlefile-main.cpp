/*

//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
//// Wash-n-Cure Rev 0.9.3 (BETA)
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


//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=

; PlatformIO Project Configuration File
;
;   Build options: build flags, source filter
;   Upload options: custom upload port, speed and extra flags
;   Library options: dependencies, extra library storages
;   Advanced options: extra scripting
;
; Please visit documentation for the other options and examples
; https://docs.platformio.org/page/projectconf.html

[platformio]
default_envs = 
	esp32dev


[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino

[env]
upload_speed = 921600
lib_deps = 
	http://github.com/tzapu/WiFiManager
	http://github.com/scottchiefbaker/ESP-WebOTA
	http://github.com/gin66/FastAccelStepper
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
int washSteps = 240000; // Number of motor step before reversing direction when washing
int washSpeed = 400; // Speed for wash cycle - no microstep jumpers installed
int cureSteps = 9999999; // High number of steps, no need to reverse
int cureSpeed = 6000; // Speed for cure cycle - no microstep jumpers installed
int stepperAccel = 500; // How quick to come up to speed
/* from FastAccelStepper github: In one setup, operating A4988 without microsteps has led
to erratic behaviour at some specific low (erratic means step forward/backward, while DIR is
kept low). No issue with 16 microstep. */
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

// STEPER DRIVER SUPPORT - A4988 Step Pin = ESP Pin 33, A4988 Direction Pin = ESP Pin 25, A4988 VDD Pin = ESP Pin 26 
#include <FastAccelStepper.h>
const int dirPin = 25;
const int stepPin = 33;
const int motorEnable = 26; 
FastAccelStepperEngine engine = FastAccelStepperEngine();
FastAccelStepper *stepper = NULL;

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
    stepper->setSpeedInUs(washSpeed);
    stepper->setAcceleration(stepperAccel);
    stepper->move(washSteps);

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
    stepper->setSpeedInUs(cureSpeed);
    stepper->setAcceleration(stepperAccel);
    stepper->move(cureSteps);

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
    stepper->stopMove();
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
        stepper->stopMove();
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
            stepper->setSpeedInUs(washSpeed);
            stepper->setAcceleration(stepperAccel);
            stepper->move(washSteps);
        }
        else
        {
            digitalWrite(FAN, HIGH); // Turn on the fan
            digitalWrite(motorEnable, HIGH); // Enable the motor
            digitalWrite(UVLED, HIGH); // Turn on the UV lamp
            stepper->setSpeedInUs(cureSpeed);
            stepper->setAcceleration(stepperAccel);
            stepper->move(cureSteps);
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
// COMPRESSED WEB INTERFACE
// Good howto here: https://www.mischianti.org/2020/10/26/web-server-with-esp8266-and-esp32-byte-array-gzipped-pages-and-spiffs-2/
#define index_html_gz_len 1193
const uint8_t index_html_gz[] PROGMEM = {  
0x1F, 0x8B, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0xA5, 0x57, 0x6D, 0x53, 0xE3, 0x36, 
0x10, 0xFE, 0x2B, 0x3A, 0x33, 0x85, 0x64, 0x30, 0xB6, 0xE3, 0x90, 0x0B, 0x38, 0xB6, 0x3B, 0x14, 
0xD2, 0xDE, 0x75, 0xA0, 0x30, 0x84, 0xBE, 0xCD, 0x0D, 0xD3, 0x51, 0xEC, 0x4D, 0xA2, 0xC1, 0x91, 
0x5C, 0x4B, 0x49, 0x48, 0x99, 0xFC, 0xF7, 0xAE, 0xE4, 0xC4, 0x97, 0x40, 0xE0, 0x80, 0xFB, 0x60, 
0x47, 0x92, 0x77, 0x9F, 0x67, 0x77, 0xA5, 0x5D, 0x6D, 0xC2, 0x0F, 0x67, 0x97, 0xA7, 0x37, 0x7F, 
0x5F, 0x75, 0xC9, 0x48, 0x8D, 0xB3, 0x38, 0xD4, 0x6F, 0x92, 0x51, 0x3E, 0x8C, 0x2C, 0xE0, 0x16, 
0xCE, 0x81, 0xA6, 0x31, 0x09, 0xC7, 0xA0, 0x28, 0x49, 0x46, 0xB4, 0x90, 0xA0, 0x22, 0x6B, 0xA2, 
0x06, 0x07, 0x47, 0x96, 0xBB, 0x5A, 0xE7, 0x74, 0x0C, 0xD1, 0x94, 0xC1, 0x2C, 0x17, 0x85, 0x22, 
0x89, 0xE0, 0x0A, 0xB8, 0x8A, 0xF6, 0x66, 0x2C, 0x55, 0xA3, 0xE8, 0xD0, 0xF3, 0xF6, 0xE2, 0x50, 
0xAA, 0x79, 0x06, 0x44, 0xCD, 0x73, 0x88, 0x2C, 0x05, 0xF7, 0xCA, 0x4D, 0xA4, 0xB4, 0xE2, 0xBE, 
0x48, 0xE7, 0x0F, 0x03, 0x94, 0x3F, 0x18, 0xD0, 0x31, 0xCB, 0xE6, 0xC1, 0x49, 0xC1, 0x68, 0xD6, 
0x19, 0xD3, 0x62, 0xC8, 0x78, 0xE0, 0x75, 0x12, 0x91, 0x89, 0x22, 0xD8, 0xF1, 0x1B, 0xFE, 0xA0, 
0x99, 0x2C, 0x68, 0x90, 0x31, 0x7E, 0xF7, 0xA0, 0xD5, 0x0F, 0x52, 0x48, 0x44, 0x41, 0x15, 0x13, 
0x3C, 0xE0, 0x82, 0xC3, 0x4A, 0x12, 0xFA, 0x90, 0xC2, 0x00, 0x25, 0xA7, 0x4C, 0x32, 0x05, 0xE9, 
0xEB, 0x84, 0x47, 0x62, 0x0A, 0xC5, 0x13, 0xD1, 0x09, 0x4F, 0xA1, 0x40, 0xC6, 0xA7, 0xF2, 0x34, 
0x51, 0x6C, 0x0A, 0xAF, 0x56, 0x70, 0x74, 0x0C, 0x91, 0x20, 0xA7, 0x69, 0xCA, 0xF8, 0x30, 0x68, 
0xE5, 0xF7, 0x1D, 0xA3, 0x4B, 0x33, 0x36, 0xE4, 0x41, 0x82, 0xC1, 0x82, 0xA2, 0xD3, 0xA7, 0xC9, 
0xDD, 0xB0, 0x10, 0x08, 0x12, 0xEC, 0x1C, 0xB5, 0x8E, 0xFD, 0xE3, 0x0A, 0xA6, 0x91, 0xF8, 0x47, 
0xCD, 0x66, 0xC7, 0xC4, 0x49, 0xB2, 0xFF, 0x20, 0xF0, 0xBD, 0xFC, 0x7E, 0xE1, 0x0C, 0x84, 0x50, 
0x1A, 0x55, 0xA0, 0xA7, 0x9A, 0x7E, 0xC0, 0xEE, 0x21, 0xED, 0x64, 0x30, 0x50, 0x18, 0xB9, 0xBE, 
0x50, 0x4A, 0x8C, 0x71, 0x60, 0x36, 0x21, 0x68, 0x78, 0xDE, 0x0F, 0x6B, 0x0C, 0x07, 0x4B, 0xE4, 
0x4D, 0x9E, 0xD2, 0xDC, 0xA7, 0xA6, 0x2D, 0x1C, 0x45, 0xFB, 0x19, 0xD8, 0x2A, 0xB5, 0xD5, 0xE8, 
0xA1, 0x2F, 0x0A, 0x74, 0x26, 0xF0, 0x88, 0x14, 0x19, 0x4B, 0xC9, 0x8E, 0xE7, 0x69, 0x36, 0xBD, 
0xA6, 0x51, 0x33, 0x9A, 0x4B, 0x08, 0x56, 0x83, 0x2D, 0x58, 0xA1, 0x6B, 0x8E, 0x42, 0x1C, 0xEA, 
0xBD, 0x27, 0x66, 0x5C, 0x9E, 0x88, 0xCD, 0x68, 0xE0, 0xC9, 0x2B, 0x47, 0x71, 0x98, 0xB2, 0xA9, 
0x79, 0x91, 0x24, 0xA3, 0x52, 0x46, 0x56, 0x19, 0x4D, 0x14, 0xE8, 0xB3, 0x61, 0x1C, 0x32, 0xFC, 
0x8D, 0xFF, 0xA4, 0x72, 0x44, 0x76, 0xC9, 0xE9, 0xA4, 0x80, 0xD0, 0x35, 0xCB, 0x6E, 0x1F, 0x1F, 
0xFD, 0x0D, 0x01, 0xE4, 0x98, 0x66, 0x59, 0x7C, 0x0D, 0x92, 0x71, 0x92, 0x17, 0x8C, 0x2B, 0xDC, 
0x04, 0x82, 0x61, 0x53, 0x38, 0x13, 0x09, 0x48, 0xA9, 0xE7, 0x52, 0x99, 0x4D, 0x34, 0xA7, 0xB7, 
0x10, 0x99, 0x83, 0x76, 0x1A, 0xB5, 0xD0, 0x35, 0xFC, 0x1A, 0x07, 0x9F, 0xDE, 0x5C, 0x2A, 0x18, 
0x93, 0x1E, 0x0A, 0x4F, 0x64, 0x40, 0x42, 0x99, 0x53, 0x4E, 0x58, 0x1A, 0x59, 0xD2, 0xAC, 0xA0, 
0x51, 0xAE, 0x5E, 0xAA, 0xE4, 0xCD, 0x63, 0xA2, 0x87, 0xB9, 0xA2, 0xF4, 0x78, 0x54, 0x1A, 0x7B, 
0xC1, 0xF8, 0x44, 0x81, 0x0C, 0x5D, 0x5C, 0xD0, 0x8B, 0xD5, 0x40, 0xFB, 0xB0, 0xF9, 0xD5, 0x35, 
0x7A, 0xFA, 0x49, 0x11, 0x6F, 0x82, 0xFB, 0xCA, 0x89, 0xE0, 0x49, 0xC6, 0x92, 0x3B, 0xE4, 0x05, 
0x9E, 0xD6, 0x1A, 0x75, 0x2B, 0xDE, 0xDD, 0xB9, 0xF7, 0x7D, 0xEF, 0x23, 0x29, 0x43, 0x51, 0x4E, 
0x30, 0x0A, 0x46, 0x5C, 0x63, 0xA4, 0xA5, 0x7E, 0x35, 0xD8, 0x0A, 0xD4, 0x5C, 0x03, 0x32, 0x86, 
0x6C, 0x07, 0xDA, 0xB0, 0xA8, 0x0A, 0xC1, 0x0C, 0x99, 0xFF, 0x99, 0xD2, 0xCC, 0x8A, 0xBD, 0x55, 
0x14, 0x9E, 0xD2, 0x56, 0xD2, 0x09, 0xC2, 0x6F, 0x93, 0xFE, 0xB6, 0xB7, 0x7E, 0x65, 0x64, 0x7B, 
0xDD, 0xDB, 0xF6, 0x9B, 0xBD, 0x3D, 0x5C, 0x03, 0x5A, 0xF3, 0xF6, 0x31, 0xD0, 0x86, 0x45, 0x5F, 
0x11, 0x8B, 0xC7, 0x3C, 0x46, 0xCE, 0x2D, 0x37, 0xFB, 0xD1, 0xDE, 0xBF, 0xEC, 0xD0, 0xC7, 0xA5, 
0x1D, 0xAD, 0x93, 0x26, 0xB9, 0x3A, 0xF9, 0xBD, 0xD7, 0x25, 0xCB, 0xD9, 0x4B, 0x86, 0x6C, 0x45, 
0x6A, 0x57, 0x1E, 0x75, 0x49, 0xEF, 0xE6, 0xF2, 0x6A, 0xE9, 0x51, 0xF7, 0xCD, 0x40, 0xAD, 0x25, 
0x50, 0xE3, 0xE4, 0x67, 0xD2, 0x3B, 0xF9, 0xA3, 0xB4, 0x08, 0x27, 0xDB, 0x80, 0x56, 0x2E, 0x97, 
0x89, 0x22, 0x93, 0x82, 0xE5, 0x2A, 0x1E, 0x4C, 0x78, 0x62, 0xF2, 0xC9, 0xC0, 0x41, 0xFD, 0x61, 
0x4A, 0x0B, 0xC2, 0x23, 0x0E, 0x33, 0xF2, 0xD7, 0xC5, 0xF9, 0x27, 0xA5, 0xF2, 0x6B, 0xF8, 0x77, 
0x02, 0x52, 0x75, 0xB8, 0x23, 0x72, 0xE0, 0x35, 0xEB, 0x97, 0xEE, 0x8D, 0x65, 0x5B, 0x33, 0x9E, 
0xE0, 0x2D, 0xC3, 0x87, 0xF0, 0xE3, 0x50, 0x44, 0xD6, 0x3E, 0xD8, 0x1F, 0xBC, 0xBA, 0xCD, 0x1D, 
0x83, 0x52, 0xB7, 0xF1, 0xF2, 0xB9, 0x61, 0x63, 0x10, 0x13, 0x55, 0x5B, 0x11, 0xD4, 0xEA, 0x0F, 
0x43, 0x50, 0x67, 0x54, 0xD1, 0x5A, 0x7D, 0x61, 0x37, 0xEB, 0x8B, 0x8A, 0xB9, 0x5A, 0x36, 0xDC, 
0xB0, 0x8D, 0x1B, 0x1C, 0xC1, 0x0B, 0x2C, 0x27, 0x73, 0x9D, 0xBE, 0x50, 0x12, 0x47, 0x6B, 0xC8, 
0x6C, 0x50, 0x3B, 0x8C, 0x22, 0x35, 0x62, 0xD2, 0x31, 0x62, 0x3A, 0xEF, 0x61, 0x77, 0xD7, 0xF7, 
0xBC, 0xE5, 0x6A, 0x99, 0xF6, 0x2B, 0x86, 0x5F, 0x7B, 0x97, 0xBF, 0x39, 0xB9, 0xBE, 0x23, 0x6B, 
0x4B, 0x1D, 0x99, 0x0B, 0x2E, 0xE1, 0x06, 0xCB, 0x5B, 0xBD, 0x83, 0x60, 0xA9, 0x48, 0x26, 0x63, 
0x2C, 0x6B, 0x0E, 0xDA, 0xD6, 0xCD, 0x40, 0x0F, 0x7F, 0x9A, 0x7F, 0x4E, 0x6B, 0x5F, 0x73, 0xA7, 
0xEE, 0x30, 0xCE, 0xA1, 0xF8, 0x74, 0x73, 0x71, 0x1E, 0xC1, 0x17, 0xEF, 0xD6, 0x7E, 0x56, 0xA5, 
0x4A, 0xA0, 0x4D, 0x95, 0xC6, 0xAD, 0xDD, 0x42, 0xEB, 0xE0, 0x8B, 0x7F, 0xBB, 0xBB, 0xFB, 0x3C, 
0xE1, 0xB2, 0x5E, 0xAD, 0xEB, 0x5A, 0xD7, 0xDA, 0x45, 0xC7, 0xAA, 0xDB, 0xBE, 0xE7, 0xBF, 0x0F, 
0x01, 0xB3, 0xA8, 0x40, 0x81, 0x6C, 0x4E, 0xD0, 0x3A, 0xAC, 0xAA, 0x36, 0x69, 0x90, 0xB1, 0xA9, 
0x65, 0xA4, 0x80, 0x31, 0x65, 0x1C, 0xD7, 0x34, 0x81, 0xC6, 0x8E, 0x91, 0x65, 0x75, 0x2C, 0xF4, 
0xFC, 0xC0, 0xF7, 0x1A, 0x9D, 0xEF, 0x64, 0xB3, 0xF6, 0xF9, 0xBE, 0xB5, 0x64, 0x94, 0xEB, 0x94, 
0x0B, 0x0C, 0x7E, 0xF3, 0xFB, 0xBD, 0xD2, 0xDB, 0xF4, 0x2D, 0xB7, 0x9A, 0xDA, 0xAD, 0xA5, 0x4B, 
0xCD, 0xF7, 0xBB, 0x54, 0x51, 0xBD, 0xE8, 0x93, 0x61, 0xC4, 0x0E, 0xAB, 0x62, 0x3C, 0x7C, 0x2B, 
0xE3, 0x15, 0x9D, 0x48, 0x48, 0x5F, 0x17, 0x41, 0xC3, 0xD6, 0x5A, 0x63, 0x6B, 0xBD, 0x93, 0xED, 
0x15, 0xCE, 0x3D, 0x49, 0xA0, 0xC5, 0xC2, 0x86, 0xC7, 0xA5, 0x82, 0xF1, 0x81, 0xB0, 0x4C, 0x89, 
0x80, 0x65, 0x89, 0x58, 0x54, 0xE6, 0x60, 0xB3, 0xD5, 0x9D, 0xE2, 0xE0, 0x9C, 0xE1, 0x95, 0x8D, 
0x06, 0xD4, 0xAC, 0xB3, 0xCB, 0x8B, 0xD3, 0xB2, 0x37, 0x3D, 0x17, 0xD8, 0x42, 0xA4, 0x96, 0xBD, 
0xB5, 0x88, 0x98, 0x32, 0xF3, 0x59, 0xF7, 0x1E, 0x98, 0x5D, 0xCF, 0xD4, 0x19, 0x1F, 0x9A, 0xF5, 
0x0E, 0xDE, 0x5B, 0x65, 0xB1, 0x5B, 0xEF, 0x4E, 0xCA, 0xAE, 0x0C, 0x1B, 0x81, 0xBC, 0xAA, 0x85, 
0x95, 0x49, 0xB3, 0x02, 0x7B, 0xD2, 0xDA, 0x5E, 0x48, 0xC9, 0xA8, 0x80, 0x41, 0x64, 0xED, 0x91, 
0x7D, 0x32, 0x63, 0x3C, 0x15, 0x33, 0x27, 0x13, 0x89, 0xE9, 0x3F, 0x1C, 0x6C, 0x49, 0x94, 0xC0, 
0xFE, 0x09, 0x3F, 0xED, 0xB9, 0xEE, 0x36, 0x89, 0x11, 0x76, 0x2E, 0xBA, 0xE3, 0xD6, 0x12, 0x41, 
0xBB, 0xDD, 0xDE, 0x8A, 0x42, 0xD5, 0x68, 0x25, 0x33, 0xC9, 0x53, 0xAC, 0x5A, 0x16, 0x89, 0x0B, 
0xD0, 0x5D, 0x31, 0x76, 0x8A, 0xC4, 0x73, 0x8E, 0x1D, 0xBC, 0x5D, 0x68, 0x4C, 0xF6, 0xC8, 0xBA, 
0x1F, 0x6E, 0xBE, 0x2A, 0xE3, 0xEE, 0xAA, 0xFB, 0x72, 0x75, 0x9F, 0x86, 0x3F, 0xE6, 0xDF, 0xC1, 
0xFF, 0x23, 0x06, 0x06, 0xCD, 0x2D, 0x0C, 0x00, 0x00
};


// SERVE THE MAIN PAGE
//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
void handleRoot() {
    const char* dataType = "text/html";
    server.sendHeader(F("Content-Encoding"), F("gzip"));
    server.send_P(200, dataType, (const char*)index_html_gz, index_html_gz_len);
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

    /* Automatically connect using saved credentials, if connection fails, it starts an access point
    with the specified name ( "Wash-n-Cure"), if empty will auto generate SSID, if password is blank
    it will be anonymous AP (wm.autoConnect()) then goes into a blocking loop awaiting configuration
    and will return success result */

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

    // SET UP GPIO
    //=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
    pinMode(PROX, INPUT); // Set PROX pin as an input
    pinMode(motorEnable, OUTPUT); // Set motorEnable pin as an output
    digitalWrite(motorEnable, LOW); // Turn motor off
    pinMode(stepPin, OUTPUT);
	pinMode(dirPin, OUTPUT);
    engine.init();
  	stepper = engine.stepperConnectToPin(stepPin);
	stepper->setDirectionPin(dirPin);
    stepper->setAutoEnable(true);

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
    display.println("WnC 0.9.3");
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

    // DISPLAY FINAL STARTUP MESSAGE
    Serial.println(" ");
    Serial.print("Initialization time: ");
    Serial.println(now);
}




//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
//// LOOP         LOOP           LOOP          LOOP          LOOP           LOOP          LOOP          LOOP
//=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
void loop()
{
    // RUN OTA UPDATE SERVICE (if not washing)
    if (washActive == false || (washActive == true && pauseActive == true))
        webota.handle();

    // RUN THE WEB SERVER
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
        if (washActive == true && now > messageDurationTime)
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
        }

        // OLED STATUS CHECK - CURING
        if (cureActive == true && now > messageDurationTime)
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

        // STEPPER MOTOR CHECK - IF WASHING, KEEP STEPPER MOVING
        if (washActive == true && ((now - cycleStartTime) > (washMinutes * 60000)))
        {
            StopAll();
        }
        if (washActive == true && !stepper->isRunning())
        {
            washSteps = washSteps * -1;
            stepper->move(washSteps);
        }

        // STEPPER MOTOR CHECK - IF CURING, KEEP STEPPER MOVING
        if (cureActive == true && ((now - cycleStartTime) > (cureMinutes * 60000)))
        {
            StopAll();
        }
        if (cureActive == true && (!stepper->isRunning())) // CURE CYCLE CHECK
                    stepper->move(cureSteps);
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
