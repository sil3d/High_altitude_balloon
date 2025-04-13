/******************************************************************************
 * TTGO T-Beam V1.2 Multi-Sensor LoRa Sender (Utilisation GPS Interne)
 *
 * Description:
 * Lit les données du GPS INTERNE (Neo-6M via Serial2), de capteurs
 * environnementaux (BME280, ENS160, Ozone DFRobot, UV Analogique)
 * connectés en I2C/Analogique, affiche les informations sur un écran OLED
 * et les transmet via LoRa.
 *
 * Matériel:
 * - TTGO T-Beam V1.2 (avec GPS Interne NEO-6M)
 * - Écran OLED SSD1306 (I2C: SDA=21, SCL=22)
 * - SparkFun BME280 (I2C)
 * - SparkFun ENS160 (I2C)
 * - DFRobot Ozone Sensor (I2C)
 * - Capteur UV Analogique (Connecté au Pin 32)
 * - Buzzer (Connecté au Pin 14)
 * - LED Bleue (GPIO 4 utilisé comme indicateur)
 *
 * Notes de Configuration Spécifique:
 * - GPS Interne: ESP32_RX=34, ESP32_TX=12 @ 9600 baud sur Serial2
 * - LoRa RST: Pin 23
 * - Buzzer: Pin 14
 * - LED: Pin 4
 *
 * Auteur: (Votre Nom / Basé sur discussion)
 * Date: (Date)
 ******************************************************************************/

// --- Bibliothèques Incluses ---
#include <SPI.h>
#include <LoRa.h>
#include <TinyGPSPlus.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "SparkFunBME280.h"      // Bibliothèque SparkFun BME280
#include "SparkFun_ENS160.h"     // Bibliothèque SparkFun ENS160
#include "DFRobot_OzoneSensor.h" // Bibliothèque DFRobot Ozone

// --- Définitions et Constantes ---
// Écran OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1      // Pin Reset (-1 si partagé ou non utilisé)
#define SCREEN_ADDRESS 0x3C // Adresse I2C OLED (vérifier si 0x3D pour certains modèles)

// Module LoRa (Pins pour TTGO T-Beam V1.2)
#define LORA_SCK 5
#define LORA_MISO 19
#define LORA_MOSI 27
#define LORA_CS 18
#define LORA_RST 23   // Correct pour T-Beam V1.2
#define LORA_IRQ 26   // Aussi appelé DIO0

// --- Configuration GPS Interne ---
#define GPS_RX_PIN 34 // Pin RX de l'ESP32 (connecté au TX du GPS interne)
#define GPS_TX_PIN 12 // Pin TX de l'ESP32 (connecté au RX du GPS interne)
#define GPS_BAUD_RATE 9600

// Capteur UV (Analogique)
#define UV_SENSOR_PIN 32 // Vérifiez que c'est bien une broche ADC valide (ex: GPIO32)

// Capteur Ozone (DFRobot I2C)
#define COLLECT_NUMBER 20         // Nombre d'échantillons pour la moyenne Ozone (affecte temps lecture)
#define Ozone_IICAddress OZONE_ADDRESS_3 // Adresse I2C DFRobot (0x73 par défaut)

// BME280 Altitude
#define SEA_LEVEL_PRESSURE_HPA 1015.0f // Pression approx. au niveau de la mer en hPa (AJUSTER !)

// Périphériques Utilisateur
const int blueLED = 4;    // LED connectée au GPIO 4
const int buzzer = 14;    // Buzzer connecté au GPIO 14
#define BUTTON_PIN 39     // Bouton utilisateur du T-Beam (près du GPS)

// Intervalle de Boucle (pour Live Tracking) // <<< MODIFIÉ
#define TARGET_LOOP_INTERVAL_MS 1000 // Cible un cycle complet d'environ 1 seconde
#define MINIMUM_DELAY_MS 50 // Délai minimum si la boucle est très rapide

// --- Initialisation des objets ---
TinyGPSPlus gps;                      // Objet pour le parsing GPS
// Utilisation de Serial2 pour le GPS interne du T-Beam
HardwareSerial& GpsSerial = Serial2;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET); // Objet écran OLED
BME280 myBME;                         // Objet SparkFun BME280
SparkFun_ENS160 myENS;                // Objet SparkFun ENS160
DFRobot_OzoneSensor Ozone;            // Objet DFRobot Ozone

// --- Variables Globales ---
// Lectures Capteurs
float temperature = NAN; // Not a Number par défaut
float humidity = NAN;
float altitude = NAN;    // Calculé à partir de la pression
float pressure = NAN;    // Pression en Pascals (Pa)
uint8_t airQuality = 0;  // AQI (1-5) de l'ENS160
uint16_t tvoc = 0;       // TVOC (ppb) de l'ENS160
uint16_t eCO2 = 0;       // eCO2 (ppm) de l'ENS160
int16_t ozoneConcentration = -1; // Ozone (ppb), -1 si erreur
float uvIndex = -1.0;    // UV Index, -1.0 si erreur

// Indicateurs d'état d'initialisation
bool loraInitialized = false;
bool bmeInitialized = false;
bool ensInitialized = false;
bool ozoneInitialized = false;
bool displayInitialized = false;
bool gpsSerialStarted = false; // Pour savoir si Serial2 pour GPS est démarré

// Compteur de paquets (si utile)
int pktCount = 0;

// --- Fonction Setup (exécutée une fois au démarrage) ---
void setup() {
  // Initialisation Moniteur Série
  Serial.begin(115200);
  while (!Serial); // Attente optionnelle de la connexion série
  Serial.println("\n\n--- Démarrage TTGO T-Beam V1.2 Multi-Sensor Sender (GPS INTERNE) ---");

  // Rappel Configuration Matérielle
  Serial.println("[CONFIG] Configuration Matérielle Utilisée:");
  Serial.println("[CONFIG]   GPS Interne -> Serial2 (RX=34, TX=12)");
  Serial.println("[CONFIG]   Buzzer ------> Pin 14");
  Serial.println("[CONFIG]   LoRa RST ----> Pin 23");
  Serial.println("[CONFIG]   LED ---------> Pin 4");
  Serial.println("[CONFIG]   Bouton ------> Pin 39");
  Serial.println("[CONFIG]   I2C ---------> SDA=21, SCL=22");
  Serial.println("[CONFIG]   UV Sensor ---> Pin 32");
  Serial.println("--------------------------------------------------");

  // Initialisation des broches OUTPUT/INPUT
  pinMode(blueLED, OUTPUT);      // Définir pin LED comme sortie
  digitalWrite(blueLED, LOW);    // S'assurer qu'elle est éteinte
  pinMode(buzzer, OUTPUT);       // Définir pin buzzer comme sortie
  pinMode(BUTTON_PIN, INPUT_PULLUP); // Activer résistance de tirage interne pour le bouton

  // Initialisation I2C (pour OLED, BME, ENS, Ozone)
  Serial.print("[INIT] Initialisation I2C (SDA=21, SCL=22)... ");
  Wire.begin(21, 22);
  Serial.println("OK");

  // Initialisation GPS Interne sur Serial2
  Serial.print("[INIT] Initialisation GPS Interne (Serial2 - RX=");
  Serial.print(GPS_RX_PIN); Serial.print(", TX="); Serial.print(GPS_TX_PIN);
  Serial.print(" @ "); Serial.print(GPS_BAUD_RATE); Serial.println(" baud)... ");
  // Utilise Serial2 avec les pins définis
  GpsSerial.begin(GPS_BAUD_RATE, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  gpsSerialStarted = true; // Indique que le port série est prêt
  Serial.println("OK");

  // Initialisation OLED
  Serial.print("[INIT] Initialisation OLED (Adresse 0x3C)... ");
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("ECHEC! Vérifiez câblage/adresse.");
  } else {
    displayInitialized = true;
    Serial.println("OK");
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("System Initializing...");
    display.display();
    delay(500); // Petite pause pour voir le message
  }

  // Initialisation LoRa
  Serial.print("[INIT] Initialisation LoRa (868 MHz)... ");
  Serial.printf("\n       Pins: SCK=%d, MISO=%d, MOSI=%d, CS=%d, RST=%d, IRQ=%d\n", LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS, LORA_RST, LORA_IRQ);
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_IRQ);
  if (!LoRa.begin(868E6)) { // Fréquence pour l'Europe
    Serial.println("[INIT]   -> ECHEC LoRa! Vérifiez module/broches.");
    if (displayInitialized) { display.setCursor(0, 10); display.println("Init LoRa FAILED"); display.display(); }
  } else {
    loraInitialized = true;
    LoRa.setSpreadingFactor(7); // SF7 rapide pour test, augmenter (ex: 10-12) pour portée
    LoRa.setTxPower(14);        // Puissance d'émission (max 20dBm souvent, vérifier législation locale)
    Serial.println("[INIT]   -> LoRa OK");
    if (displayInitialized) { display.setCursor(0, 10); display.println("Init LoRa OK"); display.display(); }
  }
  delay(500);

  // Initialisation BME280 (SparkFun) // <<< MODIFIÉ
  Serial.print("[INIT] Initialisation BME280 (SparkFun)... ");
  myBME.setI2CAddress(0x76); // Force l'adresse I2C à 0x76 (courante sur carte combo)
  if (!myBME.beginI2C(Wire)) { // Passer l'objet Wire
    Serial.println("ECHEC! Vérifiez câblage OU ADRESSE (0x76 essayée).");
    if (displayInitialized) { display.setCursor(0, 20); display.println("Init BME280 FAILED"); display.display(); }
  } else {
    bmeInitialized = true;
    Serial.println("OK (Adresse 0x76)");
    if (displayInitialized) { display.setCursor(0, 20); display.println("Init BME280 OK"); display.display(); }
  }
  delay(500);

  // Initialisation ENS160 (SparkFun)
  Serial.print("[INIT] Initialisation ENS160 (SparkFun)... ");
  if (!myENS.begin(Wire)) { // Passer l'objet Wire
    Serial.println("ECHEC! Vérifiez câblage/adresse (0x53 défaut).");
     if (displayInitialized) { display.setCursor(0, 30); display.println("Init ENS160 FAILED"); display.display(); }
  } else {
    Serial.print("Trouvé. Configuration... ");
    if (!myENS.setOperatingMode(SFE_ENS160_STANDARD)) {
        Serial.println("Echec config mode standard.");
        if (displayInitialized) { display.setCursor(0, 30); display.println("Init ENS160 CONF ERR"); display.display(); }
    } else {
        ensInitialized = true;
        Serial.println("OK (Mode Standard)");
        if (displayInitialized) { display.setCursor(0, 30); display.println("Init ENS160 OK"); display.display(); }
    }
  }
  delay(500);

  // Initialisation Ozone (DFRobot)
  Serial.print("[INIT] Initialisation Ozone (DFRobot)... ");
  int ozoneRetries = 0;
  while (!Ozone.begin(Ozone_IICAddress) && ozoneRetries < 3) {
      Serial.printf("\n       Tentative %d/3... ", ozoneRetries + 1);
      delay(1000);
      ozoneRetries++;
  }
  if (ozoneRetries >= 3) {
      Serial.println("ECHEC! Vérifiez câblage/adresse (0x73 défaut).");
      if (displayInitialized) { display.setCursor(0, 40); display.println("Init Ozone FAILED"); display.display(); }
  } else {
      ozoneInitialized = true;
      Ozone.setModes(MEASURE_MODE_PASSIVE); // Mode passif par défaut
      Serial.println("OK");
      if (displayInitialized) { display.setCursor(0, 40); display.println("Init Ozone OK"); display.display(); }
  }
  delay(500);

  // Initialisation UV (Analogique)
  Serial.print("[INIT] Configuration Capteur UV (Analogique Pin ");
  Serial.print(UV_SENSOR_PIN);
  Serial.print(")... ");
  analogReadResolution(12); // ESP32 ADC est 12 bits (0-4095)
  pinMode(UV_SENSOR_PIN, INPUT);
  Serial.println("OK");
  if (displayInitialized) { display.setCursor(0, 50); display.println("Init UV OK"); display.display(); }
  delay(1000); // Pause avant de démarrer la boucle

  Serial.println("\n--- Initialisation terminée ---");
  if (displayInitialized) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("System READY");
    display.display();
    delay(1500);
  }
  Serial.println("Entrée dans loop()...");
} // Fin setup()


// --- Boucle Principale (exécutée répétitivement) ---
void loop() {
  Serial.println("\n================= DEBUT Boucle =================");
  unsigned long startLoopTime = millis();

  // --- 1. Lecture et Traitement GPS ---
  Serial.print("[GPS] Lecture données Serial2... "); // <<< Mise à jour pour GPS interne
  bool receivedGpsDataThisLoop = false;
  unsigned long gpsStartTime = millis();
  if (gpsSerialStarted) {
    // Lire les données série disponibles depuis le GPS (maintenant sur Serial2 via GpsSerial)
    while (GpsSerial.available() > 0 && millis() - gpsStartTime < 150) { // Lire pendant max 150ms
      char c = GpsSerial.read();
      // DEBUG GPS BRUT: Serial.print(c);
      if (gps.encode(c)) { // Donner le caractère à TinyGPS++
        receivedGpsDataThisLoop = true; // Indique qu'au moins un caractère a été traité
      }
    }
  } else {
    Serial.print("GPS Serial non démarré! ");
  }
  Serial.printf("Terminé (%lu ms). ", millis() - gpsStartTime);
  if (receivedGpsDataThisLoop) Serial.print("Données traitées. "); else Serial.print("Aucune nouvelle donnée. ");

  // Affichage Debug GPS
  Serial.printf("Chars=%lu OK=%lu Fail=%lu | Fix=%d Sats=%d Age=%lums\n",
                gps.charsProcessed(), gps.passedChecksum(), gps.failedChecksum(),
                gps.location.isValid(),
                gps.satellites.isValid() ? gps.satellites.value() : 0,
                gps.location.isValid() ? gps.location.age() : 0);


  // --- 2. Lecture des Capteurs Environnementaux ---
  readEnvironmentalSensors(); // Inchangé


  // --- 3. Envoi des données via LoRa ---
  if (loraInitialized) {
    sendDataOverLoRa(); // Inchangé (utilise l'objet gps)
  } else {
    Serial.println("[LoRa] Non initialisé, pas d'envoi.");
  }


  // --- 4. Mise à jour de l'Affichage OLED ---
  if (displayInitialized) {
    updateDisplay(); // Inchangé (utilise l'objet gps)
  } else {
    Serial.println("[OLED] Non initialisé, pas d'affichage.");
  }

  // --- 5. Gestion Bouton (Exemple simple: appui court -> bip) ---
  if (digitalRead(BUTTON_PIN) == LOW) { // Inchangé
    Serial.println("[BTN] Bouton pressé!");
    tone(buzzer, 1500, 100); // Beep court (1500Hz, 100ms)
    delay(200); // Petit délai anti-rebond/répétition
  }

  // --- Fin de boucle et Attente ---
  unsigned long endLoopTime = millis();
  unsigned long loopDuration = endLoopTime - startLoopTime;
  unsigned long delayNeeded = TARGET_LOOP_INTERVAL_MS > loopDuration ? TARGET_LOOP_INTERVAL_MS - loopDuration : MINIMUM_DELAY_MS;
  Serial.printf("================= FIN Boucle (Durée: %lu ms, SmartDelay: %lu ms) =================\n", loopDuration, delayNeeded);

  // Attendre avant la prochaine itération (permet la lecture du GPS pendant l'attente)
  smartDelay(delayNeeded);
} // Fin loop()


// --- Fonction de Lecture des Capteurs Environnementaux ---
void readEnvironmentalSensors() {
  Serial.println("\n[SENSORS] Lecture des capteurs environnementaux:");

  // BME280 (SparkFun)
  if (bmeInitialized) {
    temperature = myBME.readTempC();
    humidity = myBME.readFloatHumidity();
    pressure = myBME.readFloatPressure(); // Pression en Pascals (Pa)

    // Calcul manuel de l'altitude si pression valide
    if (!isnan(pressure) && pressure > 10000) { // Pression > 100 hPa (sanity check)
       altitude = 44330.0 * (1.0 - pow(pressure / (SEA_LEVEL_PRESSURE_HPA * 100.0), 0.1903));
    } else {
       altitude = NAN; // Pression invalide -> Altitude invalide
       pressure = NAN; // Marquer aussi la pression comme invalide si < 100hPa
    }

    Serial.printf("  BME280: T=%.1f C, H=%.1f %%, P=%.0f Pa (%.1f hPa), Alt=%.1f m\n",
                  isnan(temperature) ? -99.9 : temperature,
                  isnan(humidity) ? -99.9 : humidity,
                  isnan(pressure) ? -9999 : pressure,
                  isnan(pressure) ? -99.9 : pressure / 100.0,
                  isnan(altitude) ? -999.9 : altitude);


  } else {
    Serial.println("  BME280: Non initialisé.");
  }

  // ENS160 (SparkFun)
  // La bibliothèque SparkFun gère la compensation T/H auto si BME280 SparkFun présent.
  if (ensInitialized) {
     if (myENS.checkDataStatus()) { // Vérifier si de nouvelles données sont prêtes
        airQuality = myENS.getAQI(); // Index Qualité Air (1-5)
        tvoc = myENS.getTVOC();      // Composés Organiques Volatils Totaux (ppb)
        eCO2 = myENS.getECO2();      // Concentration CO2 équivalente (ppm)
        Serial.printf("  ENS160: AQI=%d, TVOC=%d ppb, eCO2=%d ppm\n", airQuality, tvoc, eCO2);
    } else {
        Serial.println("  ENS160: Pas de nouvelles données prêtes.");
    }
  } else {
    Serial.println("  ENS160: Non initialisé.");
    airQuality = 0; tvoc = 0; eCO2 = 0; // Reset si non initialisé
  }

  // Ozone (DFRobot)
  if (ozoneInitialized) {
    ozoneConcentration = Ozone.readOzoneData(COLLECT_NUMBER);
    if (ozoneConcentration >= 0) {
       Serial.printf("  Ozone: %d ppb\n", ozoneConcentration);
    } else {
       Serial.println("  Ozone: Erreur lecture (-1)");
       ozoneConcentration = -1;
    }
  } else {
    Serial.println("  Ozone: Non initialisé.");
    ozoneConcentration = -1;
  }

  // UV (Analogique)
  int rawUV = analogRead(UV_SENSOR_PIN);
  if (rawUV >= 0 && rawUV <= 4095) {
    float voltage = rawUV * (3.3 / 4095.0);
    // ATTENTION: La conversion tension -> UV Index dépend FORTEMENT du capteur !
    // Ceci est une ESTIMATION GENERIC. À AJUSTER pour VOTRE capteur spécifique.
    // Exemple: Si 100mV (0.1V) correspond à 1 Index UV.
    uvIndex = voltage / 0.1; // Facteur d'échelle (ajuster selon le capteur)
    uvIndex = constrain(uvIndex, 0.0, 15.0); // Limiter à une plage raisonnable
    Serial.printf("  UV: Index=%.1f (Raw=%d, Volt=%.2fV)\n", uvIndex, rawUV, voltage);
  } else {
    uvIndex = -1.0; // Valeur invalide
    Serial.printf("  UV: Lecture analogique invalide (Raw=%d)\n", rawUV);
  }
} // Fin readEnvironmentalSensors()


// --- Fonction d'Envoi des Données via LoRa ---
void sendDataOverLoRa() {
  Serial.print("[LoRa] Préparation et envoi du paquet... ");
  digitalWrite(blueLED, HIGH); // Allumer LED pendant la préparation/transmission

  LoRa.beginPacket(); // Démarrer la construction du paquet

  // Format: GPS,lat,lon,alt,sats,time,date|ENV,temp,press,hum,alt|AIR,aqi,tvoc,eco2|OZ,ozone|UV,uvindex
  // Utilisation de marqueurs et séparateurs '|' et ','
  // Envoyer "ERR" ou valeur numérique invalide (-999) si capteur/donnée indisponible

  // 1. Section GPS
  LoRa.print("GPS,");
  bool gpsPosValid = gps.location.isValid() && gps.location.age() < 5000; // Position valide et récente (< 5 sec)
  bool gpsTimeValid = gps.time.isValid() && gps.time.age() < 5000; // Heure valide et récente
  bool gpsDateValid = gps.date.isValid() && gps.date.age() < 5000; // Date valide et récente

  if (gpsPosValid) {
    LoRa.print(gps.location.lat(), 6); LoRa.print(","); // Latitude
    LoRa.print(gps.location.lng(), 6); LoRa.print(","); // Longitude
    LoRa.print(gps.altitude.meters()); LoRa.print(","); // Altitude (GPS)
    LoRa.print(gps.satellites.value()); LoRa.print(","); // Nb Satellites
  } else {
    LoRa.print("ERR,ERR,ERR,ERR,"); // Pas de position valide
  }
  if(gpsTimeValid){ // Heure UTC
     LoRa.printf("%02d:%02d:%02d,", gps.time.hour(), gps.time.minute(), gps.time.second());
  } else { LoRa.print("ERR,"); }
  if(gpsDateValid){ // Date UTC
     LoRa.printf("%d/%d/%d", gps.date.day(), gps.date.month(), gps.date.year());
  } else { LoRa.print("ERR"); }


  // 2. Section Environnement (BME280)
  LoRa.print("|ENV,");
  LoRa.print(isnan(temperature) ? -99.9 : temperature, 1); LoRa.print(","); // Température
  LoRa.print(isnan(pressure) ? -9999 : pressure, 0); LoRa.print(",");   // Pression (Pa)
  LoRa.print(isnan(humidity) ? -99.9 : humidity, 1); LoRa.print(",");   // Humidité (%)
  LoRa.print(isnan(altitude) ? -999.9 : altitude, 1);                   // Altitude (calculée)


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

  // Fin et envoi du paquet LoRa
  pktCount++; // Incrémenter compteur local
  Serial.print("Paquet #"); Serial.print(pktCount); Serial.print("... ");
  int result = LoRa.endPacket(); // endPacket peut être bloquant (attend TX complete)

  digitalWrite(blueLED, LOW); // Éteindre LED après tentative de transmission

  if (result) {
    Serial.println("Succès.");
    // Optionnel: Beep succès court sur buzzer
    // tone(buzzer, 1200, 50);
  } else {
    Serial.println("ECHEC!");
    // Optionnel: Beep erreur plus long/grave sur buzzer
    // tone(buzzer, 500, 200);
  }
} // Fin sendDataOverLoRa()


// --- Fonction de Mise à Jour de l'Affichage OLED ---
void updateDisplay() {
  //Serial.println("[OLED] Mise à jour affichage..."); // Optionnel, peut être verbeux
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Ligne 1: Statut GPS (Fix et Sats)
  bool gpsFix = gps.location.isValid() && gps.location.age() < 5000;
  display.print("GPS:");
  if (gps.satellites.isValid()) {
     display.printf(" Sat:%2d", gps.satellites.value()); // %2d pour aligner
  } else {
     display.print(" Sat:--");
  }
  display.printf(" Fix:%s\n", gpsFix ? "OK" : "NO"); // Nouvelle ligne

  // Ligne 2: Coordonnées GPS (si Fix)
  if (gpsFix) {
      display.printf("La:%.4f Lo:%.4f\n", gps.location.lat(), gps.location.lng());
  } else {
      display.println("Lat: ---.---- Lon:---.----");
  }

  // Ligne 3: Température & Humidité (BME)
  display.print("T:");
  if (bmeInitialized && !isnan(temperature)) display.printf("%.1fC", temperature); else display.print("ERR");
  display.print(" H:");
  if (bmeInitialized && !isnan(humidity)) display.printf("%.0f%%", humidity); else display.print("ERR");
  display.println(); // NL

  // Ligne 4: Pression & Altitude (BME)
  display.print("P:");
  if (bmeInitialized && !isnan(pressure)) display.printf("%.0fhPa", pressure / 100.0); else display.print("ERR");
  display.print(" A:");
  if (bmeInitialized && !isnan(altitude)) display.printf("%.0fm", altitude); else display.print("ERR");
  display.println(); // NL

  // Ligne 5: Qualité Air (ENS)
  display.print("AQI:");
  if (ensInitialized) display.printf("%d ", airQuality); else display.print("E "); // E pour Erreur
  display.print("CO2:");
  if (ensInitialized) display.printf("%d", eCO2); else display.print("ERR");
  display.println(); // NL, ppm manque de place

  // Ligne 6: Ozone (O3) & UV
  display.print("O3:");
  if (ozoneInitialized && ozoneConcentration >=0) display.printf("%dppb ", ozoneConcentration); else display.print("ERR  ");
  display.print("UV:");
  if (uvIndex >= 0.0) display.printf("%.1f", uvIndex); else display.print("ERR");
  //display.println(); // Pas de NL ici, c'est la dernière ligne utilisée

  // Envoyer le buffer à l'écran physique
  display.display();
} // Fin updateDisplay()

// --- Fonction smartDelay (lit le GPS pendant l'attente) ---
static void smartDelay(unsigned long ms) {
  unsigned long start = millis();
  do {
    // Pendant l'attente, on continue de lire les données série du GPS
    if (gpsSerialStarted) {
      while (GpsSerial.available() > 0) { // Lire depuis Serial2 maintenant
        gps.encode(GpsSerial.read());
      }
    }
    yield(); // Donne du temps à d'autres tâches (utile pour ESP32)
  } while (millis() - start < ms);
} // Fin smartDelay()