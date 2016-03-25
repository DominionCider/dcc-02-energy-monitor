#include <stdio.h>
#include <stdlib.h>
#include <avr/wdt.h>
#include <EEPROM.h>
#include "EmonLib.h"

// In SerialCommand.h set the following:
#define SERIALCOMMAND_HARDWAREONLY 1
#define SERIALCOMMANDBUFFER 128
#include <SerialCommand.h>

EnergyMonitor emon1;
EnergyMonitor emon2;

#define LED_PIN 8
#define AC_REF_PIN 0
#define L1_CT_PIN 2
#define L2_CT_PIN 1

struct Settings {
  // AC reference calibration
  float ac_calibration;
  float ac_phase_shift;
  
  // Current transformer calibration
  float l1_ct_calibration;
  float l2_ct_calibration;
  
  // Number of zero-crossings to observe when taking a measurement
  int   crossings;
};
Settings settings;

// Forces the AVR to reset using the watchdog timer
void soft_reset() {
  wdt_disable();  
  wdt_enable(WDTO_15MS);
  while (1) {}
}

// Loads settings from EEPROM
void load_settings() {
  EEPROM.get(0, settings);
  Serial.print("LOAD ");
  Serial.print(settings.ac_calibration);
  Serial.print(" ");
  Serial.print(settings.ac_phase_shift);
  Serial.print(" ");
  Serial.print(settings.l1_ct_calibration);
  Serial.print(" ");
  Serial.print(settings.l2_ct_calibration);
  Serial.print(" ");
  Serial.print(settings.crossings);
  Serial.println("");
}

// Handles Serial command, parses new settings, saves to EEPROM, and resets the AVR
SerialCommand SCmd;
#define SETTINGS_TOKENS (5)
void set_settings() {
  String arg[SETTINGS_TOKENS];
  Settings newSettings;
  
  for (int i=0; i<SETTINGS_TOKENS; i++) {
    arg[i] = SCmd.next();
    if (arg[i] == NULL) {
      Serial.println("ERROR");
      return;
    }
  }

  newSettings.ac_calibration = arg[0].toFloat();
  newSettings.ac_phase_shift = arg[1].toFloat();
  newSettings.l1_ct_calibration = arg[2].toFloat();
  newSettings.l2_ct_calibration = arg[3].toFloat();
  newSettings.crossings = arg[4].toInt();
  
  EEPROM.put(0, newSettings);
  soft_reset();
}

void unknown_cmd() {
  Serial.println("UNKNOWN");
}

void setup() {  
  // Two flashes  
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  delay(150);
  digitalWrite(LED_PIN, LOW);
  delay(150);
  digitalWrite(LED_PIN, HIGH);
  delay(150);
  digitalWrite(LED_PIN, LOW);
  delay(150);
  digitalWrite(LED_PIN, HIGH);
  
  Serial.begin(115200);
  SCmd.addCommand("SET", set_settings);
  SCmd.addDefaultHandler(unknown_cmd);
  
  // Set unused pins low
  pinMode(A3, OUTPUT);
  digitalWrite(A3, LOW);
  pinMode(A4, OUTPUT);
  digitalWrite(A4, LOW);
  pinMode(A5, OUTPUT);
  digitalWrite(A5, LOW);
  
  load_settings();
  
  if (settings.crossings > 0) {
    // Configure EmonLib
    emon1.voltage(AC_REF_PIN, settings.ac_calibration, settings.ac_phase_shift);
    emon1.current(L1_CT_PIN, settings.l1_ct_calibration);
    emon2.voltage(AC_REF_PIN, settings.ac_calibration, settings.ac_phase_shift);
    emon2.current(L2_CT_PIN, settings.l2_ct_calibration);
    
    digitalWrite(LED_PIN, LOW);
  }
}

void loop() {
  SCmd.readSerial();
  
  if (settings.crossings > 0) {
    digitalWrite(LED_PIN, HIGH);
    emon1.calcVI(settings.crossings, settings.crossings*100);
    printReading("L1", emon1);
    digitalWrite(LED_PIN, LOW);
    delay(100);
  
    digitalWrite(LED_PIN, HIGH);
    emon2.calcVI(settings.crossings, settings.crossings*100);
    printReading("L2", emon2);
    delay(100);
    digitalWrite(LED_PIN, LOW);
  } else {
    Serial.println("NOSETTINGS");
    delay(1000);
  }
}

void printReading(char* sensorName, EnergyMonitor m) {
  Serial.print(sensorName);
  Serial.print(" ");
  Serial.print(m.realPower);
  Serial.print(" ");
  Serial.print(m.apparentPower);
  Serial.print(" ");
  Serial.print(m.Vrms);
  Serial.print(" ");
  Serial.print(m.Irms);
  Serial.print(" ");
  Serial.print(m.powerFactor);
  Serial.println("");
}
