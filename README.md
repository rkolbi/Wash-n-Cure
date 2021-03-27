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
