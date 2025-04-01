#include <SPI.h>
#include <LoRa.h>
#include <TinyGPSPlus.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DFRobot_ENS160.h>
#include "DFRobot_BME280.h"
#include "DFRobot_OzoneSensor.h"

// Définitions pour l'écran OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define SCREEN_ADDRESS 0x3C

// Définitions pour le module LoRa
#define LORA_SCK 5
#define LORA_MISO 19
#define LORA_MOSI 27
#define LORA_CS 18
#define LORA_RST 23
#define LORA_IRQ 26

// Définition pour la LED
#define BLUE_LED 4

// Définition pour le capteur UV
#define UV_SENSOR_PIN 15

// Définition pour la pression au niveau de la mer (pour l'altitude)
#define SEA_LEVEL_PRESSURE 1015.0f

// Définitions pour le capteur d'ozone
#define COLLECT_NUMBER 20
#define Ozone_IICAddress OZONE_ADDRESS_3

// Initialisation des objets
TinyGPSPlus gps;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
DFRobot_ENS160_I2C ENS160(&Wire, 0x53);
typedef DFRobot_BME280_IIC BME;
BME bme(&Wire, 0x77);
DFRobot_OzoneSensor Ozone;

// Variables pour stocker les données des capteurs
float temperature;
float humidity;
float altitude;
uint32_t pressure;
uint8_t airQuality;
uint16_t tvoc;
uint16_t eCO2;
int16_t ozoneConcentration;
float uvIndex;

// Variables pour suivre l'état du système
bool loraInitialized = false;
bool bmeInitialized = false;
bool ensInitialized = false;
bool ozoneInitialized = false;
bool displayInitialized = false;

// Fonction pour afficher le statut du BME280
void printLastOperateStatus(BME::eStatus_t eStatus) {
  switch(eStatus) {
    case BME::eStatusOK:    Serial.println("BME280: succès"); break;
    case BME::eStatusErr:   Serial.println("BME280: Erreur inconnue"); break;
    case BME::eStatusErrDeviceNotDetected: Serial.println("BME280: Non détecté"); break;
    case BME::eStatusErrParameter:    Serial.println("BME280: Erreur de paramètre"); break;
    default: Serial.println("BME280: Statut inconnu"); break;
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000); // Donner du temps au port série pour s'initialiser
  Serial.println("Démarrage du programme...");
  
  Serial1.begin(9600, SERIAL_8N1, 34, 12); // GPS sur les broches TX=34, RX=12
  
  // Initialisation de l'OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("Échec de l'initialisation de l'OLED !");
    // Continuer malgré l'échec
  } else {
    displayInitialized = true;
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Initialisation...");
    display.display();
  }
  
  // Initialisation du module LoRa
  pinMode(BLUE_LED, OUTPUT);
  digitalWrite(BLUE_LED, LOW); // Éteindre la LED au démarrage
  
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_IRQ);
  
  Serial.println("Initialisation du LoRa...");
  if (!LoRa.begin(868E6)) {
    Serial.println("Échec de l'initialisation du module LoRa!");
    if (displayInitialized) {
      display.clearDisplay();
      display.setCursor(0, 0);
      display.println("LoRa: échec!");
      display.display();
    }
    // Continuer malgré l'échec
  } else {
    loraInitialized = true;
    LoRa.setSpreadingFactor(7);
    LoRa.setTxPower(14, PA_OUTPUT_PA_BOOST_PIN);
    Serial.println("LoRa: succès");
  }
  
  // Initialisation du BME280
  Serial.println("Initialisation du BME280...");
  bme.reset();
  delay(100); // Donner du temps au BME280 pour se réinitialiser
  
  int bmeRetries = 0;
  while(bme.begin() != BME::eStatusOK && bmeRetries < 3) {
    Serial.println("BME280: échec de l'initialisation, nouvelle tentative...");
    delay(1000);
    bmeRetries++;
  }
  
  if (bmeRetries >= 3) {
    Serial.println("BME280: échec après 3 tentatives");
    printLastOperateStatus(bme.lastOperateStatus);
    // Continuer malgré l'échec
  } else {
    bmeInitialized = true;
    Serial.println("BME280: succès");
  }
  
  // Initialisation de l'ENS160
  Serial.println("Initialisation de l'ENS160...");
  int ensRetries = 0;
  while(NO_ERR != ENS160.begin() && ensRetries < 3) {
    Serial.println("ENS160: échec de la communication, nouvelle tentative...");
    delay(1000);
    ensRetries++;
  }
  
  if (ensRetries >= 3) {
    Serial.println("ENS160: échec après 3 tentatives");
    // Continuer malgré l'échec
  } else {
    ensInitialized = true;
    ENS160.setPWRMode(ENS160_STANDARD_MODE);
    Serial.println("ENS160: succès");
  }
  
  // Initialisation du capteur d'ozone
  Serial.println("Initialisation du capteur d'ozone...");
  int ozoneRetries = 0;
  while(!Ozone.begin(Ozone_IICAddress) && ozoneRetries < 3) {
    Serial.println("Ozone: erreur de communication I2C, nouvelle tentative...");
    delay(1000);
    ozoneRetries++;
  }
  
  if (ozoneRetries >= 3) {
    Serial.println("Ozone: échec après 3 tentatives");
    // Continuer malgré l'échec
  } else {
    ozoneInitialized = true;
    Ozone.setModes(MEASURE_MODE_PASSIVE);
    Serial.println("Ozone: succès");
  }
  
  // Initialisation du capteur UV
  analogReadResolution(12);
  pinMode(UV_SENSOR_PIN, INPUT);
  
  Serial.println("Initialisation terminée, début des mesures.");
  if (displayInitialized) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Système prêt");
    display.display();
  }
  
  delay(1000); // Pause avant d'entrer dans la boucle
  Serial.println("Entrée dans loop()");
}

void loop() {
  Serial.println("Exécution de loop()");
  
  // Lecture des données GPS
  while (Serial1.available() > 0) {
    char c = Serial1.read();
    gps.encode(c);
  }
  
  // Lecture des données des capteurs environnementaux
  readEnvironmentalSensors();
  
  // Envoi des données via LoRa si initialisé
  if (loraInitialized) {
    sendDataOverLoRa();
  }
  
  // Affichage des données sur l'écran OLED
  if (displayInitialized) {
    updateDisplay();
  }
  
  Serial.println("Fin de loop(), attente...");
  delay(5000); // Envoyer des données toutes les 5 secondes
}

void readEnvironmentalSensors() {
  Serial.println("Lecture des capteurs...");
  
  // Lecture des données BME280 si initialisé
  if (bmeInitialized) {
    temperature = bme.getTemperature();
    pressure = bme.getPressure();
    altitude = bme.calAltitude(SEA_LEVEL_PRESSURE, pressure);
    humidity = bme.getHumidity();
    
    Serial.print("Température: ");
    Serial.print(temperature);
    Serial.println(" °C");
    
    Serial.print("Pression: ");
    Serial.print(pressure/100);
    Serial.println(" hPa");
    
    Serial.print("Humidité: ");
    Serial.print(humidity);
    Serial.println(" %");
    
    Serial.print("Altitude: ");
    Serial.print(altitude);
    Serial.println(" m");
  } else {
    Serial.println("BME280 non initialisé, pas de lecture");
  }
  
  // Mise à jour ENS160 avec la température et l'humidité actuelles si initialisé
  if (ensInitialized && bmeInitialized) {
    ENS160.setTempAndHum(temperature, humidity);
    
    // Lecture des données ENS160
    airQuality = ENS160.getAQI();
    tvoc = ENS160.getTVOC();
    eCO2 = ENS160.getECO2();
    
    Serial.print("Qualité de l'air: ");
    Serial.println(airQuality);
    
    Serial.print("TVOC: ");
    Serial.println(tvoc);
    
    Serial.print("eCO2: ");
    Serial.println(eCO2);
  } else {
    Serial.println("ENS160 non initialisé, pas de lecture");
  }
  
  // Lecture des données d'ozone si initialisé
  if (ozoneInitialized) {
    ozoneConcentration = Ozone.readOzoneData(COLLECT_NUMBER);
    
    Serial.print("Concentration d'ozone: ");
    Serial.print(ozoneConcentration);
    Serial.println(" ppb");
  } else {
    Serial.println("Capteur d'ozone non initialisé, pas de lecture");
  }
  
  // Lecture du capteur UV
  int rawUV = analogRead(UV_SENSOR_PIN);
  if (rawUV > 0) {
    float voltage = rawUV * (3.3 / 4095.0);
    uvIndex = voltage / 0.1;
    
    Serial.print("Indice UV: ");
    Serial.println(uvIndex);
  } else {
    uvIndex = -1.0; // Valeur d'erreur
    Serial.println("Erreur de lecture du capteur UV");
  }
}

void sendDataOverLoRa() {
  Serial.println("Envoi des données via LoRa...");
  digitalWrite(BLUE_LED, HIGH); // Allumer la LED pendant l'envoi
  
  LoRa.beginPacket();
  
  // Données GPS
  if (gps.location.isValid()) {
    LoRa.print("GPS,");
    LoRa.print(gps.location.lat(), 6);
    LoRa.print(",");
    LoRa.print(gps.location.lng(), 6);
    LoRa.print(",");
    LoRa.print(gps.altitude.meters());
    LoRa.print(",");
    LoRa.print(gps.satellites.value());
    LoRa.print(",");
    LoRa.printf("%.2d:%.2d:%.2d", gps.time.hour(), gps.time.minute(), gps.time.second());
  } else {
    LoRa.print("GPS,NO_FIX");
  }
  
  // Données environnementales
  LoRa.print("|ENV,");
  if (bmeInitialized) {
    LoRa.print(temperature);
    LoRa.print(",");
    LoRa.print(pressure);
    LoRa.print(",");
    LoRa.print(humidity);
    LoRa.print(",");
    LoRa.print(altitude);
  } else {
    LoRa.print("ERR,ERR,ERR,ERR");
  }
  
  // Données qualité de l'air
  LoRa.print("|AIR,");
  if (ensInitialized) {
    LoRa.print(airQuality);
    LoRa.print(",");
    LoRa.print(tvoc);
    LoRa.print(",");
    LoRa.print(eCO2);
  } else {
    LoRa.print("ERR,ERR,ERR");
  }
  
  // Données ozone
  LoRa.print("|OZ,");
  if (ozoneInitialized) {
    LoRa.print(ozoneConcentration);
  } else {
    LoRa.print("ERR");
  }
  
  // Données UV
  LoRa.print("|UV,");
  LoRa.print(uvIndex);
  
  LoRa.endPacket();
  
  digitalWrite(BLUE_LED, LOW); // Éteindre la LED
  Serial.println("Données envoyées");
}

void updateDisplay() {
  Serial.println("Mise à jour de l'affichage...");
  display.clearDisplay();
  display.setCursor(0, 0);
  
  // Affichage des données GPS
  if (gps.location.isValid()) {
    display.println("== GPS ==");
    display.print("Lat: ");
    display.println(gps.location.lat(), 6);
    display.print("Lon: ");
    display.println(gps.location.lng(), 6);
    display.print("Alt: ");
    display.print(gps.altitude.meters());
    display.println("m");
    display.print("Sat: ");
    display.print(gps.satellites.value());
    
    // Affichage des données essentielles des capteurs
    display.setCursor(70, 0);
    if (bmeInitialized) {
      display.print("T:");
      display.print(temperature, 1);
      display.println("C");
      
      display.setCursor(70, 8);
      display.print("Alt:");
      display.print(altitude, 0);
      display.println("m");
    } else {
      display.println("T: ERR");
      display.setCursor(70, 8);
      display.println("Alt: ERR");
    }
    
    display.setCursor(70, 16);
    if (ozoneInitialized) {
      display.print("O3:");
      display.print(ozoneConcentration);
      display.println("ppb");
    } else {
      display.println("O3: ERR");
    }
    
    display.setCursor(70, 24);
    display.print("UV:");
    display.println(uvIndex, 1);
  } else {
    display.println("Recherche GPS...");
    display.print("Satellites: ");
    display.println(gps.satellites.value());
    
    if (bmeInitialized) {
      display.print("T: ");
      display.print(temperature, 1);
      display.println("C");
      
      display.print("P: ");
      display.print(pressure/100);
      display.println("hPa");
      
      display.print("H: ");
      display.print(humidity, 1);
      display.println("%");
    } else {
      display.println("T: ERR");
      display.println("P: ERR");
      display.println("H: ERR");
    }
  }
  
  display.display();
  Serial.println("Affichage mis à jour");
}