
// Define these in the wifi_creds.hpp file
//#define WIFI_SSID "yourwifi"
//#define WIFI_PASSWORD "yourpassword"
//#define HOSTNAME "data.example.com"
//#define PATH "/write?db=<database>&u=<user>&p=<pass>"
#include "credentials.h"

#define PORT 8086
#define TIMEOUT 5000

#define GREEN_LED_PIN 5
#define RED_LED_PIN 4
#define ONE_WIRE_PIN 2

#define N_SENSORS 1
byte sensorAddr[N_SENSORS][8] = {
  {0x28, 0xFF, 0x9C, 0x5C, 0x70, 0x14, 0x04, 0x4D}, //7
};


#include <ESP8266WiFi.h>
#include <OneWire.h>

OneWire  ds(ONE_WIRE_PIN);

// Reads data from sensor addr `a` and puts the temperature in temp (in celsius)
// Returns 1 on success, 0 on failure
int readTemperature(byte a[8], float *temp) {
  ds.reset();
  ds.select(a);
  ds.write(0xBE); // Read Scratchpad

  byte data[9];
  for (int i=0; i<9; i++) {
    data[i] = ds.read();
    Serial.print(data[i], HEX);
    Serial.print(" ");
  }
  byte calculatedCRC = OneWire::crc8(data, 8);
  if (calculatedCRC != data[8]) {
    Serial.print("CRC Failed: ");
    Serial.print(calculatedCRC, HEX);
    Serial.print(" != ");
    Serial.println(data[8], HEX);
    return 0;
  }

  uint8_t reg_msb = data[1];
  uint8_t reg_lsb = data[0];
  uint16_t TReading = reg_msb << 8 | reg_lsb;

  int SignBit, Whole, Fract;
  SignBit = TReading & 0x8000;  // test most sig bit
  if (SignBit) // negative
	TReading = (TReading ^ 0xffff) + 1; // 2's comp

  Whole = TReading >> 4;  // separate off the whole and fractional portions
  Fract = (TReading & 0xf) * 100 / 16;

  *temp = Whole + (TReading & 0xf) / 16.;
  if (SignBit)
      *temp *= -1;

  os_printf("Temperature: %c%d.%d Celsius\r\n",SignBit ? '-' : ' ', Whole, Fract < 10 ? 0 : Fract);
  return 1;
}


#define NTOKENS (6)
float l1_realPower, l1_apparentPower, l1_Vrms, l1_Irms, l1_powerFactor, l2_realPower, l2_apparentPower, l2_Vrms, l2_Irms, l2_powerFactor;
int l1_powerSamples, l2_powerSamples;
void handle_serial_line(char * line) {
  if (line[0] != 'L') {
    Serial.print("Unknown input: ");
    Serial.println(line);
    return;
  }

  // Change all space characters to NULL-terminators and assign pointers to each token
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

void setup() {
  pinMode(GREEN_LED_PIN, OUTPUT);
  pinMode(RED_LED_PIN, OUTPUT);
  digitalWrite(RED_LED_PIN, HIGH);
  digitalWrite(GREEN_LED_PIN, LOW);

  Serial.begin(115200);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

#define SER_BUFFER_LENGTH (256)
char ser_buffer[SER_BUFFER_LENGTH];
int ser_buffer_index = 0;

void loop() {
  char fields[N_SENSORS+10][16];
  
  // Instruct all ds18b20 devices to take a measurement
  ds.reset();
  ds.skip();
  ds.write(0x44, 1); // Start conversion
  delay(100);

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

  // ds18b20 need ~750ms to take measurement
  delay(700);
  

  // Read each ds18b20 device individually
  float temp[N_SENSORS] = {0.0};
  for (int i=0; i<N_SENSORS; i++) {
    Serial.print("Temperature sensor ");
    Serial.print(i);
    Serial.print(": ");
    if (readTemperature(sensorAddr[i], &temp[i])) {
      Serial.print(temp[i]);
      Serial.println();
      dtostrf(temp[i], 1, 3, fields[i]);
    } else {
      strcpy(fields[i], "null");
    }

    delay(100);
  }

  if (WiFi.status() != WL_CONNECTED) {
    digitalWrite(RED_LED_PIN, HIGH);
    delay(1000);
    Serial.println("Connecting to wifi...");
    return;
  }
  digitalWrite(RED_LED_PIN, LOW);
  digitalWrite(GREEN_LED_PIN, HIGH);

  WiFiClient client;
  if (client.connect(HOSTNAME, PORT)) {
    Serial.print("Connected to ");
    Serial.print(HOSTNAME);
    Serial.print(":");
    Serial.println(PORT);
    delay(50);


    // If there are powerSamples, divide the accumulators by the number of samples to report the average
    if (l1_powerSamples) {
      dtostrf(l1_realPower/l1_powerSamples, 1, 3, fields[N_SENSORS]);
      dtostrf(l1_apparentPower/l1_powerSamples, 1, 3, fields[N_SENSORS+1]);
      dtostrf(l1_Vrms/l1_powerSamples, 1, 3, fields[N_SENSORS+2]);
      dtostrf(l1_Irms/l1_powerSamples, 1, 3, fields[N_SENSORS+3]);
      dtostrf(l1_powerFactor/l1_powerSamples, 1, 3, fields[N_SENSORS+4]);
    } else {
      // No powerSamples, so report null for all power fields
      for (int i=N_SENSORS; i<N_SENSORS+5; i++) {
        strcpy(fields[i], "null");
      }
    }
    if (l2_powerSamples) {
      dtostrf(l2_realPower/l2_powerSamples, 1, 3, fields[N_SENSORS+5]);
      dtostrf(l2_apparentPower/l2_powerSamples, 1, 3, fields[N_SENSORS+6]);
      dtostrf(l2_Vrms/l2_powerSamples, 1, 3, fields[N_SENSORS+7]);
      dtostrf(l2_Irms/l2_powerSamples, 1, 3, fields[N_SENSORS+8]);
      dtostrf(l2_powerFactor/l2_powerSamples, 1, 3, fields[N_SENSORS+9]);
    } else {
      // No powerSamples, so report null for all power fields
      for (int i=N_SENSORS+5; i<N_SENSORS+10; i++) {
        strcpy(fields[i], "null");
      }
    }
    
    

    // Prepare JSON payload for InfluxDB
    char body[512];
    os_sprintf(
      body,
      "dcc02 temp=%s,l1_realPower=%s,l1_apparentPower=%s,l1_Vrms=%s,l1_Irms=%s,l1_powerFactor=%s,l2_realPower=%s,l2_apparentPower=%s,l2_Vrms=%s,l2_Irms=%s,l2_powerFactor=%s,uptime=%di",
      fields[0],
      fields[1],
      fields[2],
      fields[3],
      fields[4],
      fields[5],
      fields[6],
      fields[7],
      fields[8],
      fields[9],
      fields[10],
      millis()
    );
    Serial.print("Writing to ");
    Serial.println(PATH);
    Serial.println(body);
    Serial.println(strlen(body));
    
    
    // Make HTTP POST Request to InfluxDB
    client.print("POST ");
    client.print(PATH);
    client.print(" HTTP/1.1\r\n");

    client.print("Host: ");
    client.print(HOSTNAME);
    client.print(":");
    client.print(PORT);
    client.print("\r\n");

    client.print("User-Agent: ESP8266 Arduino\r\n");

    client.print("Connection: close\r\n");

    client.print("Content-Length: ");
    client.print(strlen(body));
    client.print("\r\n");
    
    client.print("\r\n");
    client.print(body);
    
    for (int i=0; i<TIMEOUT; i+=100) {
      if (client.available()) {
        break;
      }
      Serial.print(".");
      delay(100);
    }
    
    while (client.available()) {
      Serial.println(client.readStringUntil('\r'));
    }

  } else {
    digitalWrite(RED_LED_PIN, HIGH);
  }

  digitalWrite(GREEN_LED_PIN, LOW);
  delay(10000);
}
