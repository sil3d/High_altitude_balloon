#include <SoftwareSerial.h>

SoftwareSerial pmsSerial(10, 11); // RX, TX

void setup() {
  Serial.begin(9600);
  pmsSerial.begin(9600);
}

void loop() {
  if (pmsSerial.available()) {
    // Read data from the sensor
    char ch = pmsSerial.read();
    Serial.write(ch);
    // Process the data as per the PMS5003 datasheet
  }
  // Add your code to handle the sensor data and output it
}