//////SENDER ///


#include <SPI.h>
#include <LoRa.h>
#include <TinyGPSPlus.h> // Remis pour essayer d'utiliser le GPS
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DFRobot_ENS160.h>
#include "DFRobot_BME280.h"
#include "DFRobot_OzoneSensor.h"

// --- Définitions (inchangées) ---
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

// --- Initialisation des objets (GPS réactivé) ---
TinyGPSPlus gps; // GPS réactivé
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
DFRobot_ENS160_I2C ENS160(&Wire, 0x53);
typedef DFRobot_BME280_IIC BME;
BME bme(&Wire, 0x77);
DFRobot_OzoneSensor Ozone;

// --- Variables (inchangées) ---
float temperature;
float humidity;
float altitude;
uint32_t pressure;
uint8_t airQuality;
uint16_t tvoc;
uint16_t eCO2;
int16_t ozoneConcentration;
float uvIndex;
bool loraInitialized = false;
bool bmeInitialized = false;
bool ensInitialized = false;
bool ozoneInitialized = false;
bool displayInitialized = false;

// --- Fonctions utilitaires (inchangées) ---
void printLastOperateStatus(BME::eStatus_t eStatus) {
  // ... (code inchangé)
    switch(eStatus) {
    case BME::eStatusOK:    Serial.println("BME280: succès"); break;
    case BME::eStatusErr:   Serial.println("BME280: Erreur inconnue"); break;
    case BME::eStatusErrDeviceNotDetected: Serial.println("BME280: Non détecté"); break;
    case BME::eStatusErrParameter:    Serial.println("BME280: Erreur de paramètre"); break;
    default: Serial.println("BME280: Statut inconnu"); break;
  }
}

// --- Setup (GPS réactivé) ---
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("Démarrage du programme (avec tentative GPS)...");

  Serial1.begin(9600, SERIAL_8N1, 34, 12); // GPS réactivé (vérifier les broches!)

  // Initialisation OLED (inchangée)
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("Échec de l'initialisation de l'OLED !");
  } else {
    displayInitialized = true;
    display.clearDisplay(); display.setTextSize(1); display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0); display.println("Initialisation..."); display.display();
  }

  // Initialisation LoRa (inchangée)
  pinMode(BLUE_LED, OUTPUT); digitalWrite(BLUE_LED, LOW);
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_IRQ);
  Serial.println("Initialisation du LoRa...");
  if (!LoRa.begin(868E6)) {
    Serial.println("Échec LoRa!");
    if (displayInitialized) { display.clearDisplay(); display.setCursor(0, 0); display.println("LoRa: échec!"); display.display(); }
  } else {
    loraInitialized = true;
    LoRa.setSpreadingFactor(7); LoRa.setTxPower(14, PA_OUTPUT_PA_BOOST_PIN);
    Serial.println("LoRa: succès");
  }

  // Initialisation BME280 (inchangée)
  Serial.println("Initialisation du BME280...");
  bme.reset(); delay(100);
  int bmeRetries = 0;
  while(bme.begin() != BME::eStatusOK && bmeRetries < 3) { /*...*/ delay(1000); bmeRetries++; }
  if (bmeRetries >= 3) { Serial.println("BME280: échec init"); printLastOperateStatus(bme.lastOperateStatus); }
  else { bmeInitialized = true; Serial.println("BME280: succès"); }

  // Initialisation ENS160 (inchangée)
  Serial.println("Initialisation de l'ENS160...");
  int ensRetries = 0;
  while(NO_ERR != ENS160.begin() && ensRetries < 3) { /*...*/ delay(1000); ensRetries++; }
  if (ensRetries >= 3) { Serial.println("ENS160: échec init"); }
  else { ensInitialized = true; ENS160.setPWRMode(ENS160_STANDARD_MODE); Serial.println("ENS160: succès"); }

  // Initialisation Ozone (inchangée)
  Serial.println("Initialisation du capteur d'ozone...");
  int ozoneRetries = 0;
  while(!Ozone.begin(Ozone_IICAddress) && ozoneRetries < 3) { /*...*/ delay(1000); ozoneRetries++; }
  if (ozoneRetries >= 3) { Serial.println("Ozone: échec init"); }
  else { ozoneInitialized = true; Ozone.setModes(MEASURE_MODE_PASSIVE); Serial.println("Ozone: succès"); }

  // Initialisation UV (inchangée)
  analogReadResolution(12); pinMode(UV_SENSOR_PIN, INPUT);

  Serial.println("Initialisation terminée, début des mesures.");
  if (displayInitialized) {
    display.clearDisplay(); display.setCursor(0, 0); display.println("Système prêt"); display.display();
  }
  delay(1000);
  Serial.println("Entrée dans loop()");
}

// --- Loop (Gestion lecture GPS réactivée) ---
void loop() {
  Serial.println("--- Début Loop ---");

  // Tenter de lire les données GPS en continu (non bloquant)
  while (Serial1.available() > 0) {
    char c = Serial1.read();
    gps.encode(c);
    // Serial.print(c); // Décommenter pour voir les données brutes NMEA
  }
  // Donner un peu de temps au GPS pour traiter les caractères reçus
  // gps.encode() traite caractère par caractère, la validité est mise à jour à l'intérieur.

  // Lire les données des capteurs environnementaux
  readEnvironmentalSensors();

  // Envoyer les données via LoRa (même si le GPS n'a pas de fix)
  if (loraInitialized) {
    sendDataOverLoRa();
  } else {
      Serial.println("LoRa non initialisé, pas d'envoi.");
  }

  // Afficher les données sur l'écran OLED
  if (displayInitialized) {
    updateDisplay();
  } else {
      Serial.println("OLED non initialisé, pas d'affichage.");
  }

  Serial.println("--- Fin Loop, attente... ---");
  delay(5000); // Intervalle d'envoi
}

// --- readEnvironmentalSensors (inchangée) ---
void readEnvironmentalSensors() {
  Serial.println("Lecture des capteurs...");

  // BME280
  if (bmeInitialized) {
    temperature = bme.getTemperature();
    pressure = bme.getPressure();
    altitude = bme.calAltitude(SEA_LEVEL_PRESSURE, pressure);
    humidity = bme.getHumidity();
    Serial.printf("BME: T=%.1fC, P=%.1fhPa, H=%.1f%%, Alt=%.1fm\n", temperature, pressure/100.0, humidity, altitude);
  } else {
    Serial.println("BME280 non initialisé.");
    temperature = NAN; pressure = 0; altitude = NAN; humidity = NAN; // Valeurs invalides
  }

  // ENS160 (nécessite T/H du BME)
  if (ensInitialized && bmeInitialized) {
    ENS160.setTempAndHum(temperature, humidity);
    airQuality = ENS160.getAQI();
    tvoc = ENS160.getTVOC();
    eCO2 = ENS160.getECO2();
    Serial.printf("ENS: AQI=%d, TVOC=%dppb, eCO2=%dppm\n", airQuality, tvoc, eCO2);
  } else {
    Serial.println("ENS160 ou BME non initialisé.");
    airQuality = 0; tvoc = 0; eCO2 = 0; // Valeurs par défaut/invalides
  }

  // Ozone
  if (ozoneInitialized) {
    ozoneConcentration = Ozone.readOzoneData(COLLECT_NUMBER);
     Serial.printf("Ozone: %d ppb\n", ozoneConcentration);
  } else {
    Serial.println("Ozone non initialisé.");
    ozoneConcentration = -1; // Valeur invalide
  }

  // UV
  int rawUV = analogRead(UV_SENSOR_PIN);
  if (rawUV > 0) { // Simple check
    float voltage = rawUV * (3.3 / 4095.0);
    uvIndex = voltage / 0.1;
    uvIndex = constrain(uvIndex, 0.0, 11.0); // Limiter
     Serial.printf("UV: %.1f\n", uvIndex);
  } else {
    uvIndex = -1.0; // Valeur invalide
    Serial.println("UV: Lecture invalide");
  }
}

// --- sendDataOverLoRa (MODIFIÉE pour gérer GPS optionnel) ---
void sendDataOverLoRa() {
  Serial.print("Préparation envoi LoRa... ");
  digitalWrite(BLUE_LED, HIGH);

  LoRa.beginPacket();

  // 1. Section GPS : Toujours présente, contenu variable
  LoRa.print("GPS,"); // Marqueur toujours présent
  // Vérifier si le fix GPS est valide ET si les données sont récentes
  if (gps.location.isValid() && gps.location.isUpdated() && gps.date.isValid() && gps.time.isValid() && gps.date.age() < 2000 && gps.time.age() < 2000) {
    Serial.print("GPS Valide - ");
    LoRa.print(gps.location.lat(), 6); LoRa.print(",");
    LoRa.print(gps.location.lng(), 6); LoRa.print(",");
    LoRa.print(gps.altitude.meters()); LoRa.print(",");
    LoRa.print(gps.satellites.value()); LoRa.print(",");
    // Format Heure (s'assurer que l'heure est valide avant)
    if(gps.time.isValid()){
        LoRa.printf("%02d:%02d:%02d", gps.time.hour(), gps.time.minute(), gps.time.second());
    } else {
        LoRa.print("NO_TIME"); // Si heure invalide
    }
  } else {
    Serial.print("GPS Invalide/Pas de Fix - ");
    LoRa.print("NO_FIX"); // Valeur spéciale si pas de fix valide/récent
  }

  // 2. Section ENV : Toujours présente après GPS
  LoRa.print("|ENV,"); // Séparateur et marqueur
  if (bmeInitialized) {
    LoRa.print(temperature); LoRa.print(",");
    LoRa.print(pressure); LoRa.print(",");
    LoRa.print(humidity); LoRa.print(",");
    LoRa.print(altitude);
  } else {
    LoRa.print("ERR,ERR,ERR,ERR"); // Indicateur d'erreur BME
  }

  // 3. Section AIR : Toujours présente après ENV
  LoRa.print("|AIR,");
  if (ensInitialized) {
    LoRa.print(airQuality); LoRa.print(",");
    LoRa.print(tvoc); LoRa.print(",");
    LoRa.print(eCO2);
  } else {
    LoRa.print("ERR,ERR,ERR"); // Indicateur d'erreur ENS
  }

  // 4. Section OZ : Toujours présente après AIR
  LoRa.print("|OZ,");
  if (ozoneInitialized) {
    LoRa.print(ozoneConcentration);
  } else {
    LoRa.print("ERR"); // Indicateur d'erreur Ozone
  }

  // 5. Section UV : Toujours présente après OZ
  LoRa.print("|UV,");
  // Envoyer -1.0 si la lecture était invalide, sinon la valeur
  LoRa.print(uvIndex);

  LoRa.endPacket();

  digitalWrite(BLUE_LED, LOW);
  Serial.println("Paquet LoRa envoyé.");
}

// --- updateDisplay (MODIFIÉE pour afficher état GPS) ---
void updateDisplay() {
  //Serial.println("Mise à jour affichage..."); // Peut être verbeux
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Afficher état GPS en premier
  display.print("GPS: ");
  if (gps.location.isValid() && gps.location.isUpdated()) {
      display.print(gps.satellites.value());
      display.print(" SAT");
      if(gps.hdop.isValid()) { // Afficher précision si dispo
        display.printf(" HDOP:%.1f", gps.hdop.hdop()/100.0);
      }
      display.println(""); // Nouvelle ligne
      // Afficher Lat/Lon sur la ligne suivante
      display.printf("L:%.4f Lo:%.4f\n", gps.location.lat(), gps.location.lng());
  } else {
      display.print("Cherche (Sat:");
      display.print(gps.satellites.value()); // Afficher sats même sans fix
      display.println(")");
      display.println("L:---.-- Lo:---.--"); // Placeholder
  }

  // Afficher autres capteurs
  // Ligne 3: Temp & Humidité
  display.print("T:");
  if (bmeInitialized) display.print(temperature, 1); else display.print("ERR");
  display.print("C H:");
  if (bmeInitialized) display.print(humidity, 0); else display.print("ERR");
  display.println("%");

  // Ligne 4: Altitude & Pression
  display.print("Alt:");
  if (bmeInitialized) display.print(altitude, 0); else display.print("ERR");
  display.print("m P:");
  if (bmeInitialized) display.print(pressure/100.0,0); else display.print("ERR"); // Pression en hPa sans décimale
  //display.println("hPa"); // Prend trop de place

  // Ligne 5: AQI & CO2
  display.print(" AQI:");
  if (ensInitialized) display.print(airQuality); else display.print("ERR");
  display.print(" CO2:");
  if (ensInitialized) display.print(eCO2); else display.print("ERR");

  // Ligne 6: Ozone & UV
  display.print(" O3:");
  if (ozoneInitialized) display.print(ozoneConcentration); else display.print("ERR");
  display.print(" UV:");
  if (uvIndex >= 0) display.print(uvIndex, 1); else display.print("ERR");

   // --- Affichage GPS ---
  if (gps.location.isValid() && gps.satellites.isValid()) {
    display.printf("Sat: %d\n", gps.satellites.value());
  } else {
    display.println("Sat: --");
  }

// État du fix GPS
if (gps.location.isValid() && gps.location.age() < 2000) {
  display.println("Fix: OK");

  // Affichage latitude et longitude
  display.print("Lat: ");
  display.println(gps.location.lat(), 6); // 6 chiffres après la virgule pour plus de précision
  display.print("Lon: ");
  display.println(gps.location.lng(), 6);
} else {
  display.println("Fix: --");

  // Affichage vide ou placeholder pour éviter d'afficher de fausses valeurs
  display.println("Lat: --");
  display.println("Lon: --");
}

  display.display();
  //Serial.println("Affichage mis à jour.");
}

//////SENDER ///
