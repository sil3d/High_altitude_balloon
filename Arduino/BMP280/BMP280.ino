#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h>

// Création d'une instance BMP280
Adafruit_BMP280 bmp; // I2C par défaut

void setup() {
  Serial.begin(115200);
  while (!Serial);  // Attendre l'ouverture du port série

  // Initialisation du capteur
  if (!bmp.begin(0x76)) { // L'adresse I2C peut être 0x76 ou 0x77
    Serial.println("Erreur : capteur BMP280 introuvable !");
    while (1);
  }

  // Configuration optionnelle
  bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,     // Mode
                  Adafruit_BMP280::SAMPLING_X2,     // Température
                  Adafruit_BMP280::SAMPLING_X16,    // Pression
                  Adafruit_BMP280::FILTER_X16,      // Filtrage
                  Adafruit_BMP280::STANDBY_MS_500); // Délai d'attente
}

void loop() {
  Serial.print("Température = ");
  Serial.print(bmp.readTemperature());
  Serial.println(" °C");

  Serial.print("Pression = ");
  Serial.print(bmp.readPressure() / 100.0F); // hPa
  Serial.println(" hPa");

  Serial.print("Altitude approximative = ");
  Serial.print(bmp.readAltitude(1013.25)); // hPa au niveau de la mer
  Serial.println(" m");

  Serial.println("------------------------");
  delay(2000); // Attente de 2 secondes
}
