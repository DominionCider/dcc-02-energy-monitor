#include <ESP8266WiFi.h>

// Define these in the config.h file
//#define WIFI_SSID "yourwifi"
//#define WIFI_PASSWORD "yourpassword"
//#define INFLUX_HOSTNAME "data.example.com"
//#define INFLUX_PORT 8086
//#define INFLUX_PATH "/write?db=<database>&u=<user>&p=<pass>"
//#define WEBSERVER_USERNAME "something"
//#define WEBSERVER_PASSWORD "something"
#include "config.h"

#define DEVICE_NAME "dcc02"

#define GREEN_LED_PIN 5
#define RED_LED_PIN 4
#define ONE_WIRE_PIN 2

#define N_SENSORS 1
byte sensorAddr[N_SENSORS][8] = {
  {0x28, 0xFF, 0x9C, 0x5C, 0x70, 0x14, 0x04, 0x4D}, //7
};
char * sensorNames[] = {
  "temp",
};
#define UPLOAD_FREQ 10000


#include "libdcc/webserver.h"
#include "libdcc/onewire.h"
#include "libdcc/influx.h"



#define NTOKENS (6)
float l1_realPower, l1_apparentPower, l1_Vrms, l1_Irms, l1_powerFactor, l2_realPower, l2_apparentPower, l2_Vrms, l2_Irms, l2_powerFactor;
int l1_powerSamples, l2_powerSamples;
void handle_serial_line(char * line) {
  if (line[0] != 'L') {
    Serial.print("Unknown input: ");
    Serial.println(line);
    return;
  }

  // If another 'L' is on this line, return
  for (int i = 1; line[i] != 0x00; i++) {
    if (line[i] == 'L') {
      return;
    }
  }

  // Change all space characters to NULL-terminators and assign pointers to each token
  // (Here be dragons)
  char * token[NTOKENS];
  int j = 1;
  token[0] = line;
  for (int i=0; j<NTOKENS; i++) {
    if (line[i] == ' ') {
      line[i] = 0x00;
      token[j] = line + i + 1;
      j++;
    }
  }

  for (int i=0; i<NTOKENS; i++) {
    Serial.print(token[i]);
    Serial.print(", ");
  }
  Serial.println();

  if (token[0][1] == '1') {
    l1_realPower += atof(token[1]);
    l1_apparentPower += atof(token[2]);
    l1_Vrms += atof(token[3]);
    l1_Irms += atof(token[4]);
    l1_powerFactor += atof(token[5]);
    l1_powerSamples++;
  } else if (token[0][1] == '2') {
    l2_realPower += atof(token[1]);
    l2_apparentPower += atof(token[2]);
    l2_Vrms += atof(token[3]);
    l2_Irms += atof(token[4]);
    l2_powerFactor += atof(token[5]);
    l2_powerSamples++;
  }
}

unsigned long lastUploadIteration;

void setup() {
  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(RED_LED_PIN, OUTPUT);
  digitalWrite(RED_LED_PIN, HIGH);
  digitalWrite(GREEN_LED_PIN, LOW);

  Serial.begin(115200);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  server.on("/restart", handleRestart);
  server.on("/status", handleStatus);
  server.on("/sensors", handleSensors);
  server.onNotFound(handleNotFound);
  server.begin();

  // Initialize zeroth iteration
  takeAllMeasurementsAsync();
  lastUploadIteration = millis();
  l1_realPower = 0;
  l2_realPower = 0;
  l1_apparentPower = 0;
  l2_apparentPower = 0;
  l1_Vrms = 0;
  l2_Vrms = 0;
  l1_Irms = 0;
  l2_Irms = 0;
  l1_powerFactor = 0;
  l2_powerFactor = 0;
  l1_powerSamples = 0;
  l2_powerSamples = 0;
}

#define SER_BUFFER_LENGTH (256)
char ser_buffer[SER_BUFFER_LENGTH];
int ser_buffer_index = 0;

WiFiClient client;

void loop() {
  server.handleClient();

  // Accumulate serial input into our buffer
  while (Serial.available() > 0) {
    ser_buffer[ser_buffer_index] = Serial.read();
    ser_buffer_index++;

    // If the last two characters are \r\n then handle the line and clear the buffer
    if ((ser_buffer[ser_buffer_index-1] == 0x0a && ser_buffer[ser_buffer_index-2] == 0x0d) || ser_buffer_index == SER_BUFFER_LENGTH - 1) {
      ser_buffer[ser_buffer_index-2] = 0x00; // strip the \r\n and null-terminate the string
      handle_serial_line(ser_buffer);
      ser_buffer_index = 0;
    }
    yield();
  }

  // Copy HTTP client response to Serial
  while (client.connected() && client.available()) {
    Serial.print(client.readStringUntil('\r'));
  }

  // If we are NOT ready to do a sensor iteraction, return early
  if (millis() < lastUploadIteration + UPLOAD_FREQ) {
    return;
  }

  digitalWrite(GREEN_LED_PIN, HIGH);

  // Read sensors
  float temp[N_SENSORS];
  float accum = 0.0;
  float numAccum = 0;
  String sensorBody = String(DEVICE_NAME) + " uptime=" + String(millis()) + "i";
  for (int i=0; i<N_SENSORS; i++) {
    Serial.print("Temperature sensor ");
    Serial.print(i);
    Serial.print(": ");
    if (readTemperature(sensorAddr[i], &temp[i])) {
      Serial.print(temp[i]);
      Serial.println();
      sensorBody += String(",") + sensorNames[i] + "=" + String(temp[i], 3);
    } else {
      temp[i] = NAN;
    }
  }

  // Instruct sensors to take measurements for next iteration
  takeAllMeasurementsAsync();

  // Compute energy metrics
  // If there are powerSamples, divide the accumulators by the number of samples to report the average
  if (l1_powerSamples) {
    sensorBody += String(",l1_realPower=") + String(l1_realPower/l1_powerSamples, 3);
    sensorBody += String(",l1_apparentPower=") + String(l1_apparentPower/l1_powerSamples, 3);
    sensorBody += String(",l1_Vrms=") + String(l1_Vrms/l1_powerSamples, 3);
    sensorBody += String(",l1_Irms=") + String(l1_Irms/l1_powerSamples, 3);
    sensorBody += String(",l1_powerFactor=") + String(l1_powerFactor/l1_powerSamples, 3);
  }
  if (l2_powerSamples) {
    sensorBody += String(",l2_realPower=") + String(l2_realPower/l2_powerSamples, 3);
    sensorBody += String(",l2_apparentPower=") + String(l2_apparentPower/l2_powerSamples, 3);
    sensorBody += String(",l2_Vrms=") + String(l2_Vrms/l2_powerSamples, 3);
    sensorBody += String(",l2_Irms=") + String(l2_Irms/l2_powerSamples, 3);
    sensorBody += String(",l2_powerFactor=") + String(l2_powerFactor/l2_powerSamples, 3);
  }

  // Reset energy values for the next reading
  l1_realPower = 0;
  l2_realPower = 0;
  l1_apparentPower = 0;
  l2_apparentPower = 0;
  l1_Vrms = 0;
  l2_Vrms = 0;
  l1_Irms = 0;
  l2_Irms = 0;
  l1_powerFactor = 0;
  l2_powerFactor = 0;
  l1_powerSamples = 0;
  l2_powerSamples = 0;

  Serial.println(sensorBody);

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Wifi connected to " + WiFi.SSID() + " IP:" + WiFi.localIP().toString());
    unsigned long start = millis();
    client.connect(INFLUX_HOSTNAME, INFLUX_PORT);
    postRequestAsync(sensorBody, client);
    if (millis() - start > 500) {
      Serial.println("Connection timeout. Rebooting.");
      delay(500);
      ESP.restart();
    }
    digitalWrite(RED_LED_PIN, LOW);
  } else {
    Serial.println("Connecting to wifi...");
    digitalWrite(RED_LED_PIN, HIGH);
  }
  digitalWrite(GREEN_LED_PIN, LOW);

  lastUploadIteration = millis();
}
