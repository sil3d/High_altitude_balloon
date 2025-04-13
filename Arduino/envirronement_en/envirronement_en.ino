#include <Wire.h>
#include "SparkFunBME280.h"
#include "SparkFun_ENS160.h"

BME280 myBME;
SparkFun_ENS160 myENS; 

void setup() {
  Serial.begin(115200);
  Serial.println("Reading basic values from BME280");

  Wire.begin();

  if (myBME.beginI2C() == false) {
    Serial.println("The BME280 sensor did not respond. Please check wiring.");
    while(1); // Freeze
  }

  if (myENS.begin() == false) {
    Serial.println("The ENS160 sensor did not respond. Please check wiring.");
    while(1); // Freeze
  }

  Serial.println("Sensors initialized.");

  // Set ENS160 to standard mode
  myENS.setOperatingMode(SFE_ENS160_STANDARD);
}

void loop() {
  // Get BME280 data
  float temperature = myBME.readTempC();
  float humidity = myBME.readFloatHumidity();
  float pressure = myBME.readFloatPressure();
  float altitude = myBME.readFloatAltitudeMeters();  // No argument needed here

  // Print BME280 data
  Serial.print("Temperature: ");
  Serial.print(temperature, 2);
  Serial.print(" C, Humidity: ");
  Serial.print(humidity, 2);
  Serial.print(" %, Pressure: ");
  Serial.print(pressure, 2);
  Serial.print(" hPa, Altitude: ");
  Serial.print(altitude, 2);
  Serial.println(" m");

  // Get ENS160 data
  if (myENS.checkDataStatus()) {
    Serial.print("Air Quality Index (1-5): ");
    Serial.println(myENS.getAQI());

    Serial.print("Total Volatile Organic Compounds: ");
    Serial.print(myENS.getTVOC());
    Serial.println(" ppb");

    Serial.print("CO2 concentration: ");
    Serial.print(myENS.getECO2());
    Serial.println(" ppm");

    Serial.print("Gas Sensor Status Flag: ");
    Serial.println(myENS.getFlags());
  }

  delay(200);
}