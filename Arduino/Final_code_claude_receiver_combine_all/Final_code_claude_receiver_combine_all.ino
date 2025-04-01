#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

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

// LED pour indiquer la réception
#define LED_PIN 4

// Structure pour stocker les données GPS
struct GPSData {
  float lat;
  float lon;
  float alt;
  int satellites;
  String time;
  bool hasGPSFix;
};

// Structure pour stocker les données environnementales
struct EnvData {
  float temperature;
  uint32_t pressure;
  float humidity;
  float altitude;
};

// Structure pour stocker les données de qualité d'air
struct AirData {
  int airQuality;
  int tvoc;
  int eCO2;
};

// Structure pour stocker les données d'ozone et UV
struct OtherData {
  int ozone;
  float uvIndex;
};

// Variables pour stocker les données reçues
GPSData gpsData;
EnvData envData;
AirData airData;
OtherData otherData;

// Dernier temps de réception
unsigned long lastReceiveTime = 0;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  
  // Initialisation de l'OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("Échec de l'initialisation de l'OLED !");
    while (1);
  }
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Initialisation...");
  display.display();
  
  // Initialisation du module LoRa
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_IRQ);
  
  if (!LoRa.begin(868E6)) {
    Serial.println("Échec de l'initialisation du module LoRa!");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("LoRa: échec!");
    display.display();
    while (1);
  }
  
  LoRa.setSpreadingFactor(7);
  LoRa.setTxPower(14, PA_OUTPUT_PA_BOOST_PIN);
  
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("En attente de données...");
  display.display();
  
  // Initialiser les structures de données
  initializeDataStructures();
}

void loop() {
  // Vérifier si des données LoRa sont disponibles
  int packetSize = LoRa.parsePacket();
  
  if (packetSize) {
    // Clignoter la LED pour indiquer la réception
    digitalWrite(LED_PIN, HIGH);
    
    String receivedData = "";
    while (LoRa.available()) {
      receivedData += (char)LoRa.read();
    }
    
    // Mettre à jour le temps de réception
    lastReceiveTime = millis();
    
    // Traiter les données reçues
    parseReceivedData(receivedData);
    
    // Afficher les données sur l'écran OLED
    updateDisplay();
    
    // Afficher les données dans le moniteur série
    printDataToSerial(receivedData);
    
    digitalWrite(LED_PIN, LOW);
  }
  
  // Vérifier si aucune donnée n'a été reçue depuis un certain temps
  if (millis() - lastReceiveTime > 60000 && lastReceiveTime > 0) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Aucune donnée reçue");
    display.println("depuis > 60s");
    display.println();
    display.print("Dernières coord.: ");
    if (gpsData.hasGPSFix) {
      display.print(gpsData.lat, 6);
      display.print(", ");
      display.println(gpsData.lon, 6);
    } else {
      display.println("N/A");
    }
    display.display();
  }
}

void initializeDataStructures() {
  // GPS
  gpsData.lat = 0.0;
  gpsData.lon = 0.0;
  gpsData.alt = 0.0;
  gpsData.satellites = 0;
  gpsData.time = "";
  gpsData.hasGPSFix = false;
  
  // Environnement
  envData.temperature = 0.0;
  envData.pressure = 0;
  envData.humidity = 0.0;
  envData.altitude = 0.0;
  
  // Air
  airData.airQuality = 0;
  airData.tvoc = 0;
  airData.eCO2 = 0;
  
  // Autres
  otherData.ozone = 0;
  otherData.uvIndex = 0.0;
}

void parseReceivedData(String data) {
  // Diviser les données en sections
  int gpsIndex = data.indexOf("GPS,");
  int envIndex = data.indexOf("|ENV,");
  int airIndex = data.indexOf("|AIR,");
  int ozIndex = data.indexOf("|OZ,");
  int uvIndex = data.indexOf("|UV,");
  
  // Parser les données GPS
  if (gpsIndex != -1) {
    String gpsSection = data.substring(gpsIndex + 4, envIndex > 0 ? envIndex : data.length());
    if (gpsSection != "NO_FIX") {
      int commaIndex1 = gpsSection.indexOf(',');
      int commaIndex2 = gpsSection.indexOf(',', commaIndex1 + 1);
      int commaIndex3 = gpsSection.indexOf(',', commaIndex2 + 1);
      int commaIndex4 = gpsSection.indexOf(',', commaIndex3 + 1);
      
      gpsData.lat = gpsSection.substring(0, commaIndex1).toFloat();
      gpsData.lon = gpsSection.substring(commaIndex1 + 1, commaIndex2).toFloat();
      gpsData.alt = gpsSection.substring(commaIndex2 + 1, commaIndex3).toFloat();
      gpsData.satellites = gpsSection.substring(commaIndex3 + 1, commaIndex4).toInt();
      gpsData.time = gpsSection.substring(commaIndex4 + 1);
      gpsData.hasGPSFix = true;
    } else {
      gpsData.hasGPSFix = false;
    }
  }
  
  // Parser les données environnementales
  if (envIndex != -1) {
    String envSection = data.substring(envIndex + 5, airIndex > 0 ? airIndex : data.length());
    int commaIndex1 = envSection.indexOf(',');
    int commaIndex2 = envSection.indexOf(',', commaIndex1 + 1);
    int commaIndex3 = envSection.indexOf(',', commaIndex2 + 1);
    
    envData.temperature = envSection.substring(0, commaIndex1).toFloat();
    envData.pressure = envSection.substring(commaIndex1 + 1, commaIndex2).toInt();
    envData.humidity = envSection.substring(commaIndex2 + 1, commaIndex3).toFloat();
    envData.altitude = envSection.substring(commaIndex3 + 1).toFloat();
  }
  
  // Parser les données de qualité d'air
  if (airIndex != -1) {
    String airSection = data.substring(airIndex + 5, ozIndex > 0 ? ozIndex : data.length());
    int commaIndex1 = airSection.indexOf(',');
    int commaIndex2 = airSection.indexOf(',', commaIndex1 + 1);
    
    airData.airQuality = airSection.substring(0, commaIndex1).toInt();
    airData.tvoc = airSection.substring(commaIndex1 + 1, commaIndex2).toInt();
    airData.eCO2 = airSection.substring(commaIndex2 + 1).toInt();
  }
  
  // Parser les données d'ozone
  if (ozIndex != -1) {
    String ozSection = data.substring(ozIndex + 4, uvIndex > 0 ? uvIndex : data.length());
    otherData.ozone = ozSection.toInt();
  }
  
  // Parser les données UV
  if (uvIndex != -1) {
    String uvSection = data.substring(uvIndex + 4);
    otherData.uvIndex = uvSection.toFloat();
  }
}

void updateDisplay() {
  display.clearDisplay();
  display.setCursor(0, 0);
  
  // Page 1: Données GPS et environnementales
  display.println("== BALLON STRATO ==");
  
  if (gpsData.hasGPSFix) {
    display.print("Lat: ");
    display.println(gpsData.lat, 6);
    display.print("Lon: ");
    display.println(gpsData.lon, 6);
    display.print("Alt: ");
    display.print(gpsData.alt);
    display.print("m (");
    display.print(envData.altitude);
    display.println("m)");
    display.print("Sat: ");
    display.print(gpsData.satellites);
    display.print("  T: ");
    display.print(envData.temperature, 1);
    display.println("C");
  } else {
    display.println("GPS: Pas de signal");
    display.print("T: ");
    display.print(envData.temperature, 1);
    display.print("C  H: ");
    display.print(envData.humidity, 1);
    display.println("%");
    display.print("P: ");
    display.print(envData.pressure/100);
    display.println("hPa");
  }
  
  display.print("AQ: ");
  switch(airData.airQuality) {
    case 1: display.print("Excellent"); break;
    case 2: display.print("Bon"); break;
    case 3: display.print("Moyen"); break;
    case 4: display.print("Mauvais"); break;
    case 5: display.print("Malsain"); break;
    default: display.print("?"); break;
  }
  
  display.print(" O3: ");
  display.print(otherData.ozone);
  display.println("ppb");
  
  display.print("CO2e: ");
  display.print(airData.eCO2);
  display.print("ppm  UV: ");
  display.println(otherData.uvIndex, 1);
  
  display.print("Mise a jour: ");
  display.println(gpsData.time);
  
  display.display();
}

void printDataToSerial(String rawData) {
  Serial.println("\n--- DONNÉES REÇUES ---");
  Serial.println("Données brutes: " + rawData);
  
  Serial.println("\n-- DONNÉES TRAITÉES --");
  
  if (gpsData.hasGPSFix) {
    Serial.println("GPS:");
    Serial.println("  Latitude: " + String(gpsData.lat, 6));
    Serial.println("  Longitude: " + String(gpsData.lon, 6));
    Serial.println("  Altitude GPS: " + String(gpsData.alt) + " m");
    Serial.println("  Satellites: " + String(gpsData.satellites));
    Serial.println("  Heure: " + gpsData.time);
  } else {
    Serial.println("GPS: Pas de signal");
  }
  
  Serial.println("Environnement:");
  Serial.println("  Température: " + String(envData.temperature, 2) + " °C");
  Serial.println("  Pression: " + String(envData.pressure/100) + " hPa");
  Serial.println("  Humidité: " + String(envData.humidity, 2) + " %");
  Serial.println("  Altitude barométrique: " + String(envData.altitude, 2) + " m");
  
  Serial.println("Qualité de l'air:");
  Serial.print("  Indice de qualité: " + String(airData.airQuality) + " (");
  switch(airData.airQuality) {
    case 1: Serial.println("Excellent)"); break;
    case 2: Serial.println("Bon)"); break;
    case 3: Serial.println("Moyen)"); break;
    case 4: Serial.println("Mauvais)"); break;
    case 5: Serial.println("Malsain)"); break;
    default: Serial.println("Inconnu)"); break;
  }
  Serial.println("  TVOC: " + String(airData.tvoc) + " ppb");
  Serial.println("  CO2 équivalent: " + String(airData.eCO2) + " ppm");
  
  Serial.println("Autres données:");
  Serial.println("  Ozone: " + String(otherData.ozone) + " ppb");
  Serial.println("  Indice UV: " + String(otherData.uvIndex, 2));
  
  Serial.println("------------------------");
}