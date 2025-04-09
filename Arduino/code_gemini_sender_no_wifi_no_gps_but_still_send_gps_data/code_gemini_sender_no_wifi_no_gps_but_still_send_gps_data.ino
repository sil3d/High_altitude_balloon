////// SENDER - Code Complet Corrigé (SparkFun BME/ENS + Debug GPS) ///

#include <SPI.h>
#include <LoRa.h>
#include <TinyGPSPlus.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "SparkFunBME280.h"      // Bibliothèque SparkFun BME280
#include "SparkFun_ENS160.h"     // Bibliothèque SparkFun ENS160
#include "DFRobot_OzoneSensor.h" // Bibliothèque DFRobot Ozone

// --- Définitions ---
// Écran OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define SCREEN_ADDRESS 0x3C

// Module LoRa (Vérifiez ces broches pour votre carte ESP32 spécifique !)
#define LORA_SCK 5
#define LORA_MISO 19
#define LORA_MOSI 27
#define LORA_CS 18
#define LORA_RST 23   // Certaines cartes n'utilisent pas RST, vérifier
#define LORA_IRQ 26   // Aussi appelé DIO0

// LED Bleue (souvent intégrée)
#define BLUE_LED 4    // Vérifiez si c'est la bonne broche pour votre LED

// Capteur UV (Analogique)
#define UV_SENSOR_PIN 15 // Vérifiez que c'est bien une broche ADC valide

// GPS (Module NEO-6M)
#define GPS_RX_PIN 34   // Broche RX de l'ESP32 (connectée au TX du GPS)
#define GPS_TX_PIN 12   // Broche TX de l'ESP32 (connectée au RX du GPS) - Moins critique si on ne configure pas le GPS
#define GPS_BAUD 9600   // Baud rate par défaut du NEO-6M

// Capteur Ozone (DFRobot I2C)
#define COLLECT_NUMBER 20         // Nombre d'échantillons pour la moyenne Ozone
#define Ozone_IICAddress OZONE_ADDRESS_3 // Adresse I2C DFRobot (0x73) - Vérifiez si correcte

// BME280 Altitude
#define SEA_LEVEL_PRESSURE_HPA 1015.0f // Pression approx. au niveau de la mer en hPa (ajuster pour votre lieu)

// --- Initialisation des objets ---
TinyGPSPlus gps;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
BME280 myBME;                  // Objet SparkFun BME280
SparkFun_ENS160 myENS;         // Objet SparkFun ENS160
DFRobot_OzoneSensor Ozone;     // Objet DFRobot Ozone

// --- Variables Globales ---
float temperature = NAN; // Not a Number par défaut
float humidity = NAN;
float altitude = NAN;
float pressure = NAN;       // Pression en Pascals (Pa)
uint8_t airQuality = 0;     // AQI (1-5)
uint16_t tvoc = 0;          // TVOC (ppb)
uint16_t eCO2 = 0;          // eCO2 (ppm)
int16_t ozoneConcentration = -1; // Ozone (ppb), -1 si erreur
float uvIndex = -1.0;       // UV Index, -1.0 si erreur

// Indicateurs d'état d'initialisation
bool loraInitialized = false;
bool bmeInitialized = false;
bool ensInitialized = false;
bool ozoneInitialized = false;
bool displayInitialized = false;
bool gpsSerialStarted = false; // Pour savoir si Serial1 pour GPS est démarré

// --- Setup ---
void setup() {
  Serial.begin(115200);
  while (!Serial); // Attente que le port série soit prêt (utile sur certaines cartes)
  delay(1000);
  Serial.println("\n\n--- Initialisation du Sender ESP32 ---");

  // Initialisation I2C (AVANT les capteurs I2C !)
  Wire.begin(); // Utilise les broches I2C par défaut de l'ESP32 (GPIO 21=SDA, 22=SCL)

  // Initialisation GPS sur Serial1
  Serial.printf("Initialisation GPS sur Serial1 (RX:%d, TX:%d, Baud:%d)\n", GPS_RX_PIN, GPS_TX_PIN, GPS_BAUD);
  Serial1.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  gpsSerialStarted = true; // On suppose que begin réussit, mais on vérifiera la réception des données

  // Initialisation OLED
  Serial.println("Initialisation OLED...");
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("-> ÉCHEC OLED ! Vérifiez câblage/adresse.");
  } else {
    displayInitialized = true;
    Serial.println("-> OLED OK");
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("Init OLED OK");
    display.display();
    delay(500);
  }

  // Initialisation LoRa
  pinMode(BLUE_LED, OUTPUT);
  digitalWrite(BLUE_LED, LOW);
  Serial.println("Initialisation LoRa...");
  Serial.printf("-> Pins: SCK=%d, MISO=%d, MOSI=%d, CS=%d, RST=%d, IRQ=%d\n", LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS, LORA_RST, LORA_IRQ);
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_IRQ);
  if (!LoRa.begin(868E6)) { // Fréquence pour l'Europe
    Serial.println("-> ÉCHEC LoRa ! Vérifiez module/broches.");
    if (displayInitialized) { display.setCursor(0, 10); display.println("Init LoRa ECHEC"); display.display(); }
  } else {
    loraInitialized = true;
    LoRa.setSpreadingFactor(7); // SF7 pour test rapide, augmenter pour portée (ex: 10, 11, 12)
    LoRa.setTxPower(14);        // Puissance d'émission (max souvent 20, dépend de la région/matériel)
    Serial.println("-> LoRa OK (868 MHz)");
    if (displayInitialized) { display.setCursor(0, 10); display.println("Init LoRa OK"); display.display(); }
  }
  delay(500);


  // Initialisation BME280 (SparkFun)
  Serial.println("Initialisation BME280 (SparkFun)...");
  // myBME.setI2CAddress(0x76); // Décommentez et changez si adresse = 0x76
  if (!myBME.beginI2C(Wire)) { // Passer l'objet Wire explicitement
    Serial.println("-> ÉCHEC BME280 ! Vérifiez câblage/adresse (0x77 par défaut).");
    if (displayInitialized) { display.setCursor(0, 20); display.println("Init BME280 ECHEC"); display.display(); }
  } else {
    bmeInitialized = true;
    Serial.println("-> BME280 OK");
    if (displayInitialized) { display.setCursor(0, 20); display.println("Init BME280 OK"); display.display(); }
  }
  delay(500);

  // Initialisation ENS160 (SparkFun)
  Serial.println("Initialisation ENS160 (SparkFun)...");
  // myENS.setI2CAddress(0x52); // Décommentez et changez si adresse = 0x52
  if (!myENS.begin(Wire)) { // Passer l'objet Wire explicitement
    Serial.println("-> ÉCHEC ENS160 ! Vérifiez câblage/adresse (0x53 par défaut).");
     if (displayInitialized) { display.setCursor(0, 30); display.println("Init ENS160 ECHEC"); display.display(); }
  } else {
    Serial.println("-> ENS160 Trouvé. Configuration...");
    if (!myENS.setOperatingMode(SFE_ENS160_STANDARD)) {
        Serial.println("--> Échec configuration mode standard ENS160.");
        if (displayInitialized) { display.setCursor(0, 30); display.println("Init ENS160 CONF ERR"); display.display(); }
    } else {
        ensInitialized = true;
        Serial.println("-> ENS160 OK (Mode Standard)");
        if (displayInitialized) { display.setCursor(0, 30); display.println("Init ENS160 OK"); display.display(); }
    }
  }
  delay(500);

  // Initialisation Ozone (DFRobot)
  Serial.println("Initialisation Ozone (DFRobot)...");
  int ozoneRetries = 0;
  while (!Ozone.begin(Ozone_IICAddress) && ozoneRetries < 3) {
      Serial.printf("-> Tentative Ozone %d/3...\n", ozoneRetries + 1);
      delay(1000);
      ozoneRetries++;
  }
  if (ozoneRetries >= 3) {
      Serial.println("-> ÉCHEC Ozone ! Vérifiez câblage/adresse (0x73 par défaut).");
      if (displayInitialized) { display.setCursor(0, 40); display.println("Init Ozone ECHEC"); display.display(); }
  } else {
      ozoneInitialized = true;
      Ozone.setModes(MEASURE_MODE_PASSIVE); // Mode passif par défaut
      Serial.println("-> Ozone OK");
      if (displayInitialized) { display.setCursor(0, 40); display.println("Init Ozone OK"); display.display(); }
  }
  delay(500);

  // Initialisation UV (Analogique)
  Serial.println("Configuration Capteur UV (Analogique)...");
  analogReadResolution(12); // ESP32 ADC est 12 bits (0-4095)
  pinMode(UV_SENSOR_PIN, INPUT);
  Serial.printf("-> UV sur Pin %d (ADC 12bit)\n", UV_SENSOR_PIN);
  if (displayInitialized) { display.setCursor(0, 50); display.println("Init UV OK"); display.display(); }
  delay(1000); // Pause avant de démarrer la boucle

  Serial.println("\n--- Initialisation terminée ---");
  if (displayInitialized) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Systeme PRET");
    display.display();
    delay(1500);
  }
  Serial.println("Entrée dans loop()...");
}

// --- Boucle Principale ---
void loop() {
  Serial.println("\n--- DEBUT Boucle ---");
  unsigned long startLoopTime = millis();

  // 1. Lecture GPS
  Serial.print("Lecture GPS... ");
  bool receivedGpsDataThisLoop = false;
  unsigned long gpsStartTime = millis();
  if (gpsSerialStarted) {
    while (Serial1.available() > 0 && millis() - gpsStartTime < 100) { // Lire pendant max 100ms pour éviter blocage
      char c = Serial1.read();
      // --- DEBUG GPS BRUT: Décommentez la ligne suivante pour voir ce qui arrive du GPS ---
      // Serial.print(c);
      // --------------------------------------------------------------------------------
      if (gps.encode(c)) {
        receivedGpsDataThisLoop = true; // TinyGPS a traité un caractère
      }
    }
  }
  Serial.printf("Terminé (%lu ms). ", millis() - gpsStartTime);
  if (receivedGpsDataThisLoop) Serial.println("Données GPS reçues."); else Serial.println("Aucune nouvelle donnée GPS.");

  // Affichage Debug GPS (même si pas de fix)
   // CORRIGÉ: Utilisation de passedChecksum() au lieu de sentencesProcessed()
   Serial.printf("  GPS Debug: Chars=%lu SentencesOK=%lu CSFail=%lu Fix=%d Sats=%d Age=%lums\n",
                gps.charsProcessed(),       // Total caractères traités par TinyGPS
                gps.passedChecksum(),       // <<< CORRIGÉ ICI: Total phrases NMEA avec checksum OK
                gps.failedChecksum(),       // Nombre de checksums échoués (indique bruit/problème)
                gps.location.isValid(),     // Fix valide ?
                gps.satellites.value(),     // Nb satellites (si valide)
                gps.location.age());        // Âge de la position (ms)


  // 2. Lecture Capteurs Environnementaux
  readEnvironmentalSensors();

  // 3. Envoi LoRa
  if (loraInitialized) {
    sendDataOverLoRa();
  } else {
    Serial.println("LoRa non initialisé, pas d'envoi.");
  }

  // 4. Mise à jour Affichage OLED
  if (displayInitialized) {
    updateDisplay();
  } else {
    Serial.println("OLED non initialisé, pas d'affichage.");
  }

  unsigned long endLoopTime = millis();
  Serial.printf("--- FIN Boucle (Durée: %lu ms) ---\n", endLoopTime - startLoopTime);

  // 5. Attente avant prochaine boucle
  delay(5000); // Intervalle total de ~5 secondes
}

// --- Fonction de Lecture des Capteurs ---
void readEnvironmentalSensors() {
  Serial.println("Lecture des capteurs environnementaux:");

  // BME280 (SparkFun)
  if (bmeInitialized) {
    temperature = myBME.readTempC();
    humidity = myBME.readFloatHumidity();
    pressure = myBME.readFloatPressure(); // Pression en Pascals (Pa)

    // Calcul manuel de l'altitude si pression valide
    if (!isnan(pressure) && pressure > 0) {
       altitude = 44330.0 * (1.0 - pow(pressure / (SEA_LEVEL_PRESSURE_HPA * 100.0), 0.1903));
    } else {
       altitude = NAN; // Pression invalide -> Altitude invalide
    }

    Serial.printf("  BME280: T=%.1f C, H=%.1f %%, P=%.0f Pa (%.1f hPa), Alt=%.1f m\n",
                  temperature, humidity, pressure, pressure / 100.0, altitude);
  } else {
    Serial.println("  BME280: Non initialisé.");
    // Les variables gardent leur dernière valeur ou NAN si jamais initialisées
  }

  // ENS160 (SparkFun) - La bibliothèque SparkFun tente une compensation auto avec BME280 sur le bus I2C.
  if (ensInitialized) {
     // CORRIGÉ: La ligne myENS.setEnvironmentData(...) a été supprimée car elle n'existe pas dans cette bibliothèque.
     // La compensation T/H est normalement gérée automatiquement par la bibliothèque SparkFun ENS160
     // si un BME280 SparkFun est détecté sur le même bus I2C.

    if (myENS.checkDataStatus()) { // Vérifier si de nouvelles données sont prêtes
        airQuality = myENS.getAQI(); // Index Qualité Air (1-5)
        tvoc = myENS.getTVOC();      // Composés Organiques Volatils Totaux (ppb)
        eCO2 = myENS.getECO2();      // Concentration CO2 équivalente (ppm)
        Serial.printf("  ENS160: AQI=%d, TVOC=%d ppb, eCO2=%d ppm\n", airQuality, tvoc, eCO2);
    } else {
        Serial.println("  ENS160: Pas de nouvelles données prêtes.");
        // Garder les anciennes valeurs ? Ou réinitialiser ? Pour l'instant on garde.
    }
  } else {
    Serial.println("  ENS160: Non initialisé.");
  }

  // Ozone (DFRobot)
  if (ozoneInitialized) {
    // La lecture peut prendre un peu de temps à cause de COLLECT_NUMBER
    ozoneConcentration = Ozone.readOzoneData(COLLECT_NUMBER);
    if (ozoneConcentration >= 0) { // La lib renvoie -1 en cas d'erreur
       Serial.printf("  Ozone: %d ppb\n", ozoneConcentration);
    } else {
       Serial.println("  Ozone: Erreur lecture (-1)");
       ozoneConcentration = -1; // Assurer valeur d'erreur
    }
  } else {
    Serial.println("  Ozone: Non initialisé.");
  }

  // UV (Analogique)
  int rawUV = analogRead(UV_SENSOR_PIN);
  // Vérification simple, pourrait être améliorée (ex: plage attendue)
  if (rawUV >= 0 && rawUV <= 4095) { // Valeur ADC valide pour 12 bits
    float voltage = rawUV * (3.3 / 4095.0); // Conversion en tension (suppose Vref=3.3V)
    // La conversion tension -> UV Index dépend *fortement* du capteur spécifique.
    // Ceci est une estimation générique, À AJUSTER pour votre capteur !
    uvIndex = voltage / 0.1; // Exemple: 100mV par unité d'index UV
    uvIndex = constrain(uvIndex, 0.0, 15.0); // Limiter à une plage raisonnable
    Serial.printf("  UV: Index=%.1f (Raw=%d, Volt=%.2fV)\n", uvIndex, rawUV, voltage);
  } else {
    uvIndex = -1.0; // Valeur invalide
    Serial.printf("  UV: Lecture invalide (Raw=%d)\n", rawUV);
  }
}

// --- Fonction d'Envoi LoRa ---
void sendDataOverLoRa() {
  Serial.print("Envoi LoRa... ");
  digitalWrite(BLUE_LED, HIGH); // Allumer LED pendant transmission

  LoRa.beginPacket();

  // Format: GPS,val1,val2,...|ENV,val1,val2,...|AIR,val1,val2,...|OZ,val1|UV,val1
  // Utilisation de marqueurs et séparateurs '|'

  // 1. Section GPS
  LoRa.print("GPS,");
  bool gpsValid = gps.location.isValid() && gps.location.isUpdated() && gps.location.age() < 5000; // Position valide et récente (<5s)
  bool timeValid = gps.time.isValid() && gps.time.age() < 5000; // Heure valide et récente
  bool dateValid = gps.date.isValid() && gps.date.age() < 5000; // Date valide et récente

  if (gpsValid) {
    Serial.print("[GPS OK] ");
    LoRa.print(gps.location.lat(), 6); LoRa.print(","); // Latitude
    LoRa.print(gps.location.lng(), 6); LoRa.print(","); // Longitude
    LoRa.print(gps.altitude.meters()); LoRa.print(","); // Altitude (GPS)
    LoRa.print(gps.satellites.value()); LoRa.print(","); // Nb Satellites
    if (timeValid) { // Heure
       LoRa.printf("%02d:%02d:%02d,", gps.time.hour(), gps.time.minute(), gps.time.second());
    } else { LoRa.print("NO_TIME,"); }
    if (dateValid) { // Date
       LoRa.printf("%d/%d/%d", gps.date.day(), gps.date.month(), gps.date.year());
    } else { LoRa.print("NO_DATE"); }
  } else {
    Serial.print("[GPS NO_FIX] ");
    LoRa.print("NO_FIX"); // Pas de données GPS valides
  }

  // 2. Section Environnement (BME280)
  LoRa.print("|ENV,");
  if (bmeInitialized && !isnan(temperature) && !isnan(humidity) && !isnan(pressure) && !isnan(altitude)) {
    LoRa.print(temperature, 1); LoRa.print(",");  // Index 0: Température
    // --- LIGNES INVERSÉES POUR CORRECTION ---
    LoRa.print(pressure, 0); LoRa.print(",");   // Index 1: Pression (Pa) <-- ENVOYER PRESSION EN 2ème
    LoRa.print(humidity, 1); LoRa.print(",");   // Index 2: Humidité (%) <-- ENVOYER HUMIDITÉ EN 3ème
    // --------------------------------------
    LoRa.print(altitude, 1);                    // Index 3: Altitude
  } else {
    LoRa.print("ERR,ERR,ERR,ERR");
  }


  // 3. Section Qualité Air (ENS160)
  LoRa.print("|AIR,");
  if (ensInitialized) {
    LoRa.print(airQuality); LoRa.print(",");
    LoRa.print(tvoc); LoRa.print(",");
    LoRa.print(eCO2);
  } else {
    LoRa.print("ERR,ERR,ERR"); // Marqueur d'erreur ENS
  }

  // 4. Section Ozone (DFRobot)
  LoRa.print("|OZ,");
  if (ozoneInitialized && ozoneConcentration >= 0) {
    LoRa.print(ozoneConcentration);
  } else {
    LoRa.print("ERR"); // Marqueur d'erreur Ozone
  }

  // 5. Section UV (Analogique)
  LoRa.print("|UV,");
  if (uvIndex >= 0.0) {
    LoRa.print(uvIndex, 1);
  } else {
    LoRa.print("ERR"); // Marqueur d'erreur UV
  }

  // Fin du paquet LoRa
  int result = LoRa.endPacket(); // endPacket peut être bloquant

  digitalWrite(BLUE_LED, LOW); // Éteindre LED après transmission

  if (result) {
    Serial.println("Paquet LoRa envoyé avec succès.");
  } else {
    Serial.println("Échec de l'envoi du paquet LoRa !");
  }
}

// --- Fonction de Mise à Jour de l'Affichage OLED ---
void updateDisplay() {
  //Serial.println("Mise à jour affichage OLED..."); // Optionnel, peut être verbeux
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Ligne 1 & 2: Statut GPS
  bool gpsFix = gps.location.isValid() && gps.location.age() < 5000;
  display.print("GPS:");
  if (gps.satellites.isValid()) {
     display.printf(" Sat:%d", gps.satellites.value());
  } else {
     display.print(" Sat:--");
  }
  display.printf(" Fix:%s\n", gpsFix ? "OK" : "NO"); // Nouvelle ligne
  if (gpsFix) {
      display.printf(" L:%.4f Lo:%.4f\n", gps.location.lat(), gps.location.lng());
  } else {
      display.println(" L:---.---- Lo:---.----");
  }

  // Ligne 3: Température & Humidité
  display.print("T:");
  if (bmeInitialized && !isnan(temperature)) display.printf("%.1fC", temperature); else display.print("ERR");
  display.print(" H:");
  if (bmeInitialized && !isnan(humidity)) display.printf("%.0f%%", humidity); else display.print("ERR");
  display.println(); // NL

  // Ligne 4: Pression & Altitude
  display.print("P:");
  if (bmeInitialized && !isnan(pressure)) display.printf("%.0fhPa", pressure / 100.0); else display.print("ERR");
  display.print(" A:");
  if (bmeInitialized && !isnan(altitude)) display.printf("%.0fm", altitude); else display.print("ERR");
  display.println(); // NL

  // Ligne 5: AQI & CO2
  display.print("AQI:");
  
  if (ensInitialized) display.print(airQuality); else display.print("ERR");
  display.print(" CO2:");
  if (ensInitialized) display.print(eCO2); else display.print("ERR");
  //display.println("ppm"); // Manque de place

  // Ligne 6: Ozone & UV
  display.print(" O3:");
  if (ozoneInitialized && ozoneConcentration >=0) display.print(ozoneConcentration); else display.print("ERR");
  display.print(" UV:");
  if (uvIndex >= 0.0) display.printf("%.1f", uvIndex); else display.print("ERR");

  // Afficher sur l'écran
  display.display();
}