/******************************************************************************
 * TTGO T-Beam V1.2 Multi-Sensor LoRa Sender (Utilisation GPS Interne + PMS5003)
 *
 * Description:
 * Lit les données du GPS INTERNE (Neo-6M via Serial2), de capteurs
 * environnementaux (DFRobot BME280, ENS160, Ozone DFRobot, UV Analogique, PMS5003)
 * connectés en I2C/Analogique/Serial, affiche les informations sur un écran OLED
 * et les transmet via LoRa.
 *
 * Matériel:
 * - TTGO T-Beam V1.2 (avec GPS Interne NEO-6M)
 * - Écran OLED SSD1306 (I2C: SDA=21, SCL=22)
 * - DFRobot BME280 (I2C: 0x77 ou 0x76)  <<< MODIFIÉ
 * - SparkFun ENS160 (I2C)
 * - DFRobot Ozone Sensor (I2C)
 * - Capteur UV Analogique (Connecté au Pin 32)
 * - PMS5003 Particle Sensor (Serial: RX=15, TX=35) <<< CORRIGÉ PINOUT PMS
 * - Buzzer (Connecté au Pin 14)
 * - LED Bleue (GPIO 4 utilisé comme indicateur)
 *
 * Notes de Configuration Spécifique:
 * - GPS Interne: ESP32_RX=34, ESP32_TX=12 @ 9600 baud sur Serial2
 * - PMS5003:    ESP32_RX=15(PMS TX Green), ESP32_TX=35(PMS RX Blue) @ 9600 baud sur pmsSerial <<< CORRIGÉ PINOUT PMS
 * - LoRa RST:   Pin 23
 * - Buzzer:     Pin 14
 * - LED:        Pin 4
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
// #include "SparkFunBME280.h"       // <-- SUPPRIMÉ: Ancienne bibliothèque BME280
#include "DFRobot_BME280.h"       // <<< AJOUTÉ: Bibliothèque DFRobot BME280
#include "SparkFun_ENS160.h"      // Bibliothèque SparkFun ENS160
#include "DFRobot_OzoneSensor.h"  // Bibliothèque DFRobot Ozone
#include <SoftwareSerial.h>       // Pour PMS5003

// --- Définitions et Constantes ---
// Écran OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1        // Pin Reset (-1 si partagé ou non utilisé)
#define SCREEN_ADDRESS 0x3C  // Adresse I2C OLED (vérifier si 0x3D pour certains modèles)

// Module LoRa (Pins pour TTGO T-Beam V1.2)
#define LORA_SCK 5
#define LORA_MISO 19
#define LORA_MOSI 27
#define LORA_CS 18
#define LORA_RST 23  // Correct pour T-Beam V1.2
#define LORA_IRQ 26  // Aussi appelé DIO0

// --- Configuration GPS Interne ---
#define GPS_RX_PIN 34  // Pin RX de l'ESP32 (connecté au TX du GPS interne)
#define GPS_TX_PIN 12  // Pin TX de l'ESP32 (connecté au RX du GPS interne)
#define GPS_BAUD_RATE 9600

// --- Configuration PMS5003 ---
// ATTENTION: Correction des pins selon votre description du code PMS5 standalone
// PMS Green (PMS TX) -> ESP32 RX Pin = 15
// PMS Blue (PMS RX)  -> ESP32 TX Pin = 35 (Note: Pin 35 souvent INPUT ONLY sur ESP32 classique, vérifier compatibilité ou utiliser autre pin TX)
#define PMS_RX_PIN 15 // Pin RX ESP32 connecté au TX Vert du PMS
#define PMS_TX_PIN 35 // Pin TX ESP32 connecté au RX Bleu du PMS (Pin 35 Input Only?)
#define PMS_BAUD_RATE 9600
#define PMS_READ_TIMEOUT 1500 // Timeout pour lire une trame PMS

// Capteur UV (Analogique)
#define UV_SENSOR_PIN 32  // Vérifiez que c'est bien une broche ADC valide (ex: GPIO32)

// Capteur Ozone (DFRobot I2C)
#define COLLECT_NUMBER 20                 // Nombre d'échantillons pour la moyenne Ozone (affecte temps lecture)
#define Ozone_IICAddress OZONE_ADDRESS_3  // Adresse I2C DFRobot (0x73 par défaut)

// BME280 Altitude
#define SEA_LEVEL_PRESSURE_HPA 1015.0f  // Pression approx. au niveau de la mer en hPa (AJUSTER !) <<< UTILISÉ PAR DFRobot calAltitude

// Périphériques Utilisateur
const int blueLED = 4;  // LED connectée au GPIO 4
const int buzzer = 14;  // Buzzer connecté au GPIO 14
#define BUTTON_PIN 39   // Bouton utilisateur du T-Beam (près du GPS)

// --- Structure de données PMS5003 --- (Déjà présente)
struct pms5003data {
  uint16_t framelen;
  uint16_t pm10_standard, pm25_standard, pm100_standard;
  uint16_t pm10_env, pm25_env, pm100_env;
  uint16_t particles_03um, particles_05um, particles_10um, particles_25um, particles_50um, particles_100um;
  uint16_t unused;
  uint16_t checksum;
};

// --- Typedef pour DFRobot BME280 --- <<< AJOUTÉ
typedef DFRobot_BME280_IIC    BME;

// --- Initialisation des objets ---
TinyGPSPlus gps;                                                           // Objet pour le parsing GPS
HardwareSerial& GpsSerial = Serial2;                                       // Utilise Serial2 pour GPS interne
SoftwareSerial pmsSerial(PMS_RX_PIN, PMS_TX_PIN);                          // Objet SoftwareSerial pour PMS5003 (Pins Corrigés)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);  // Objet écran OLED
// BME280 myBME;                                                           // <-- SUPPRIMÉ: Ancien objet SparkFun BME280
BME bme(&Wire, 0x77);                                                      // <<< AJOUTÉ: Objet DFRobot BME280 (Adresse 0x77)
SparkFun_ENS160 myENS;                                                     // Objet SparkFun ENS160
DFRobot_OzoneSensor Ozone;                                                 // Objet DFRobot Ozone

// --- Variables Globales ---
// Lectures Capteurs
float temperature = NAN;          // Not a Number par défaut (float, Celsius)
float humidity = NAN;             // (float, Percent)
float altitude = NAN;             // Calculé à partir de la pression (float, meters)
// float pressure = NAN;          // <-- MODIFIÉ: Pression en Pascals (Pa), le type float est conservé mais la lecture DFRobot est uint32_t
float pressureHpa = NAN;          // <<< AJOUTÉ: Pour stocker la pression en hPa pour affichage/log facile
uint32_t pressurePa_raw = 0;      // <<< AJOUTÉ: Pour stocker la lecture brute Pa (uint32_t) du DFRobot BME280
uint8_t airQuality = 0;           // AQI (1-5) de l'ENS160
uint16_t tvoc = 0;                // TVOC (ppb) de l'ENS160
uint16_t eCO2 = 0;                // eCO2 (ppm) de l'ENS160
int16_t ozoneConcentration = -1;  // Ozone (ppb), -1 si erreur
float uvIndex = -1.0;             // UV Index, -1.0 si erreur
struct pms5003data pmsData;       // Variable pour stocker les données PMS5003 (Déjà présente)

// Indicateurs d'état d'initialisation
bool loraInitialized = false;
bool bmeInitialized = false; // <<< Conservé, utilisé pour DFRobot BME280
bool ensInitialized = false;
bool ozoneInitialized = false;
bool displayInitialized = false;
bool gpsSerialStarted = false;
bool pmsSerialStarted = false;    // Pour savoir si Serial pour PMS est démarré

// Compteur de paquets (si utile)
int pktCount = 0;

// --- Fonction pour afficher le statut DFRobot BME280 --- <<< AJOUTÉ (Adapté de l'exemple)
void printBmeStatus(BME::eStatus_t eStatus) {
  Serial.print(" Status: ");
  switch(eStatus) {
    case BME::eStatusOK:    Serial.println("OK"); break;
    case BME::eStatusErr:   Serial.println("Erreur Inconnue"); break;
    case BME::eStatusErrDeviceNotDetected:    Serial.println("Non détecté"); break;
    case BME::eStatusErrParameter:    Serial.println("Erreur Paramètre"); break;
    default: Serial.println("Statut Inconnu"); break;
  }
}

// --- Fonction Setup (exécutée une fois au démarrage) ---
void setup() {
  // Initialisation Moniteur Série
  Serial.begin(115200);
  while (!Serial)
    ;
  Serial.println("\n\n--- Démarrage TTGO T-Beam V1.2 Multi-Sensor Sender (GPS INTERNE + PMS5003 + DFRobot BME280) ---"); // <<< MODIFIÉ

  // Rappel Configuration Matérielle
  Serial.println("[CONFIG] Configuration Matérielle Utilisée:");
  Serial.println("[CONFIG]   GPS Interne -> Serial2 (RX=34, TX=12)");
  Serial.printf("[CONFIG]   PMS5003 ----> SoftSerial (ESP_RX=%d <- PMS_TX_Green, ESP_TX=%d -> PMS_RX_Blue)\n", PMS_RX_PIN, PMS_TX_PIN); // <<< CORRIGÉ/CLARIFIÉ
  Serial.println("[CONFIG]   Buzzer ------> Pin 14");
  Serial.println("[CONFIG]   LoRa RST ----> Pin 23");
  Serial.println("[CONFIG]   LED ---------> Pin 4");
  Serial.println("[CONFIG]   Bouton ------> Pin 39");
  Serial.println("[CONFIG]   I2C ---------> SDA=21, SCL=22");
  Serial.println("[CONFIG]   UV Sensor ---> Pin 32");
  Serial.println("--------------------------------------------------");

  // Initialisation des broches OUTPUT/INPUT
  pinMode(blueLED, OUTPUT);
  digitalWrite(blueLED, LOW);
  pinMode(buzzer, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // Initialisation I2C (pour OLED, BME, ENS, Ozone)
  Serial.print("[INIT] Initialisation I2C (SDA=21, SCL=22)... ");
  Wire.begin(21, 22);
  Serial.println("OK");

  // Initialisation GPS Interne sur Serial2
  Serial.print("[INIT] Initialisation GPS Interne (Serial2 - RX=");
  Serial.print(GPS_RX_PIN);
  Serial.print(", TX=");
  Serial.print(GPS_TX_PIN);
  Serial.print(" @ ");
  Serial.print(GPS_BAUD_RATE);
  Serial.println(" baud)... ");
  GpsSerial.begin(GPS_BAUD_RATE, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  gpsSerialStarted = true;
  Serial.println("OK");

  // Initialisation PMS5003 sur SoftwareSerial
  Serial.print("[INIT] Initialisation PMS5003 (SoftwareSerial - ESP_RX=");
  Serial.print(PMS_RX_PIN);
  Serial.print(", ESP_TX=");
  Serial.print(PMS_TX_PIN);
  Serial.print(" @ ");
  Serial.print(PMS_BAUD_RATE);
  Serial.println(" baud)... ");
  pmsSerial.begin(PMS_BAUD_RATE);
  pmsSerialStarted = true;
  while(pmsSerial.available()) { pmsSerial.read(); }
  Serial.println("OK (port ouvert, lecture tentée dans la boucle)");
  delay(100);

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
    delay(500);
  }

  // Initialisation LoRa
  Serial.print("[INIT] Initialisation LoRa (868 MHz)... ");
  Serial.printf("\n       Pins: SCK=%d, MISO=%d, MOSI=%d, CS=%d, RST=%d, IRQ=%d\n", LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS, LORA_RST, LORA_IRQ);
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_IRQ);
  if (!LoRa.begin(868E6)) {
    Serial.println("[INIT]   -> ECHEC LoRa! Vérifiez module/broches.");
    if (displayInitialized) updateDisplayWithMessage("Init LoRa FAILED");
  } else {
    loraInitialized = true;
    LoRa.setSpreadingFactor(7);
    LoRa.setTxPower(14);
    Serial.println("[INIT]   -> LoRa OK");
    if (displayInitialized) updateDisplayWithMessage("Init LoRa OK");
  }
  delay(500);

  // --- Initialisation BME280 (DFRobot) --- <<< MODIFIÉ
  Serial.print("[INIT] Initialisation BME280 (DFRobot, Addr=0x77)... ");
  bme.reset(); // Reset le capteur
  BME::eStatus_t status = bme.begin();
  if (status != BME::eStatusOK) {
    Serial.print("ECHEC!");
    printBmeStatus(status); // Affiche la raison de l'échec
    if (displayInitialized) updateDisplayWithMessage("Init BME280 FAILED");
  } else {
    bmeInitialized = true;
    Serial.println("OK");
    if (displayInitialized) updateDisplayWithMessage("Init BME280 OK");
  }
  delay(500);
  // --- Fin Initialisation BME280 (DFRobot) ---

  // Initialisation ENS160 (SparkFun)
  Serial.print("[INIT] Initialisation ENS160 (SparkFun)... ");
  if (!myENS.begin(Wire)) {
    Serial.println("ECHEC! Vérifiez câblage/adresse (0x53 défaut).");
     if (displayInitialized) updateDisplayWithMessage("Init ENS160 FAILED");
  } else {
    Serial.print("Trouvé. Configuration... ");
    if (!myENS.setOperatingMode(SFE_ENS160_STANDARD)) {
      Serial.println("Echec config mode standard.");
       if (displayInitialized) updateDisplayWithMessage("Init ENS160 CONF ERR");
    } else {
      ensInitialized = true;
      Serial.println("OK (Mode Standard)");
       if (displayInitialized) updateDisplayWithMessage("Init ENS160 OK");
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
    if (displayInitialized) updateDisplayWithMessage("Init Ozone FAILED");
  } else {
    ozoneInitialized = true;
    Ozone.setModes(MEASURE_MODE_PASSIVE);
    Serial.println("OK");
    if (displayInitialized) updateDisplayWithMessage("Init Ozone OK");
  }
  delay(500);

  // Initialisation UV (Analogique)
  Serial.print("[INIT] Configuration Capteur UV (Analogique Pin ");
  Serial.print(UV_SENSOR_PIN);
  Serial.print(")... ");
  analogReadResolution(12);
  pinMode(UV_SENSOR_PIN, INPUT);
  Serial.println("OK");
  if (displayInitialized) updateDisplayWithMessage("Init UV OK");

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
  readGps(); // Fonction dédiée pour la clarté

  // --- 2. Lecture des Capteurs Environnementaux (BME, ENS, Ozone, UV) ---
  readEnvironmentalSensors(); // <<< Modifiée pour DFRobot BME280

  // --- 3. Lecture du Capteur de Particules (PMS5003) --- (Déjà présente)
  readPMSSensor();

  // --- 4. Envoi des données via LoRa ---
  if (loraInitialized) {
    sendDataOverLoRa(); // <<< Vérifier que les bonnes variables sont envoyées
  } else {
    Serial.println("[LoRa] Non initialisé, pas d'envoi.");
  }

  // --- 5. Mise à jour de l'Affichage OLED ---
  if (displayInitialized) {
    updateDisplay(); // <<< Vérifier que les bonnes variables sont affichées
  } else {
    Serial.println("[OLED] Non initialisé, pas d'affichage.");
  }

  // --- 6. Gestion Bouton (Exemple simple: appui court -> bip) ---
  if (digitalRead(BUTTON_PIN) == LOW) {
    Serial.println("[BTN] Bouton pressé!");
    tone(buzzer, 1500, 100);
    delay(200); // Simple anti-rebond/pause
  }

  // --- Fin de boucle et Attente ---
  unsigned long endLoopTime = millis();
  Serial.printf("================= FIN Boucle (Durée: %lu ms) =================\n", endLoopTime - startLoopTime);

  // Attendre avant la prochaine itération
  smartDelay(5000); // Objectif: intervalle total d'environ 5 secondes
} // Fin loop()

// --- Fonction pour afficher un message sur l'OLED pendant l'init ---
// (Aucun changement nécessaire ici)
void updateDisplayWithMessage(const char* message) {
    if (!displayInitialized) return;
    int16_t currentLineY = 0;
    for(int i = 0; i < 8; ++i) {
        if(i * 8 >= SCREEN_HEIGHT - 8) {
             currentLineY = i * 8;
             break;
        }
         currentLineY = (i+1) * 8;
    }
     if (currentLineY >= SCREEN_HEIGHT) currentLineY = SCREEN_HEIGHT - 8;
    display.fillRect(0, currentLineY, SCREEN_WIDTH, 8, SSD1306_BLACK);
    display.setCursor(0, currentLineY);
    display.println(message);
    display.display();
}


// --- Fonction de Lecture GPS ---
// (Aucun changement nécessaire ici)
void readGps() {
  Serial.print("[GPS] Lecture données Serial2... ");
  bool receivedGpsDataThisLoop = false;
  unsigned long gpsStartTime = millis();
  if (gpsSerialStarted) {
    while (GpsSerial.available() > 0 && millis() - gpsStartTime < 150) { // Limite le temps passé ici
      char c = GpsSerial.read();
      if (gps.encode(c)) {
        receivedGpsDataThisLoop = true;
        // Si une phrase complète est reçue, on peut sortir plus tôt
        // break; // Optionnel: dépend si on veut traiter *tous* les caractères disponibles ou juste la 1ère trame
      }
    }
  } else {
    Serial.print("GPS Serial non démarré! ");
  }
  Serial.printf("Terminé (%lu ms). ", millis() - gpsStartTime);
  if (receivedGpsDataThisLoop) Serial.print("Données traitées. ");
  else Serial.print("Aucune nouvelle donnée. ");

  Serial.printf("Chars=%lu OK=%lu Fail=%lu | Fix=%d Sats=%d Age=%lums\n",
                gps.charsProcessed(), gps.passedChecksum(), gps.failedChecksum(),
                gps.location.isValid(),
                gps.satellites.isValid() ? gps.satellites.value() : 0,
                gps.location.isValid() ? gps.location.age() : 0);
}


// --- Fonction de Lecture des Capteurs Environnementaux (BME, ENS, O3, UV) ---
// <<< MODIFIÉE pour DFRobot BME280 >>>
void readEnvironmentalSensors() {
  Serial.println("\n[SENSORS] Lecture des capteurs environnementaux:");

  // BME280 (DFRobot)
  if (bmeInitialized) {
    temperature = bme.getTemperature();    // Lecture température (float C)
    humidity = bme.getHumidity();        // Lecture humidité (float %)
    pressurePa_raw = bme.getPressure();    // Lecture pression (uint32_t Pa)

    // Convertir Pa en float hPa pour stockage/affichage facile
    pressureHpa = (float)pressurePa_raw / 100.0;

    // Calculer l'altitude en utilisant la fonction de la bibliothèque DFRobot
    // Elle prend la pression au niveau de la mer en hPa (float) et la pression mesurée en Pa (uint32_t)
    if (pressurePa_raw > 10000 && pressurePa_raw < 120000) { // Vérification basique de validité
        altitude = bme.calAltitude(SEA_LEVEL_PRESSURE_HPA, pressurePa_raw); // Calcul altitude (float m)
    } else {
        altitude = NAN; // Mettre NaN si la pression semble invalide
        pressureHpa = NAN; // Mettre NaN aussi pour hPa
    }

    // Affichage des valeurs lues/calculées
    Serial.printf("  BME280: T=%.1f C, H=%.1f %%, P=%.0f Pa (%.1f hPa), Alt=%.1f m\n",
                  isnan(temperature) ? -99.9 : temperature,
                  isnan(humidity) ? -99.9 : humidity,
                  (float)pressurePa_raw, // Afficher Pa (casté en float pour printf)
                  isnan(pressureHpa) ? -99.9 : pressureHpa, // Afficher hPa
                  isnan(altitude) ? -999.9 : altitude); // Afficher Altitude
  } else {
    Serial.println("  BME280: Non initialisé.");
    // Réinitialiser les variables si non initialisé
    temperature = humidity = pressureHpa = altitude = NAN;
    pressurePa_raw = 0;
  }

  // ENS160
  if (ensInitialized) {
     // La bibliothèque SparkFun ENS160 tente une compensation automatique si elle
     // détecte un capteur T/H compatible (comme AHT2x) sur le même bus I2C.
     // Il n'y a pas de fonction publique pour pousser manuellement T/H depuis un autre capteur.
     // Donc on lit directement les valeurs compensées par l'ENS160 lui-même.
    if (myENS.checkDataStatus()) { // Vérifier si de nouvelles données sont prêtes
      airQuality = myENS.getAQI();  // Index Qualité Air (1-5)
      tvoc = myENS.getTVOC();       // Composés Organiques Volatils Totaux (ppb)
      eCO2 = myENS.getECO2();       // Concentration CO2 équivalente (ppm)
      Serial.printf("  ENS160: AQI=%d, TVOC=%d ppb, eCO2=%d ppm\n", airQuality, tvoc, eCO2);
    } else {
       Serial.println("  ENS160: Pas de nouvelles données prêtes.");
       // Ne pas réinitialiser les anciennes valeurs si aucune nouvelle donnée n'est prête
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
      ozoneConcentration = -1; // Garder -1 en cas d'erreur
    }
  } else {
    Serial.println("  Ozone: Non initialisé.");
    ozoneConcentration = -1; // Reset si non initialisé
  }

  // UV (Analogique)
  int rawUV = analogRead(UV_SENSOR_PIN);
  if (rawUV >= 0 && rawUV <= 4095) {
    float voltage = rawUV * (3.3 / 4095.0);
    // IMPORTANT: Ajuster ce facteur (0.1 V/Index) selon VOTRE capteur UV.
    uvIndex = voltage / 0.1;
    uvIndex = constrain(uvIndex, 0.0, 15.0); // Limiter à une plage raisonnable
    Serial.printf("  UV: Index=%.1f (Raw=%d, Volt=%.2fV)\n", uvIndex, rawUV, voltage);
  } else {
    uvIndex = -1.0;
    Serial.printf("  UV: Lecture analogique invalide (Raw=%d)\n", rawUV);
  }
} // Fin readEnvironmentalSensors()

// --- Fonction de Lecture du PMS5003 ---
// (Aucun changement nécessaire ici, la fonction readPMSdata est appelée)
void readPMSSensor() {
  Serial.print("\n[SENSORS] Lecture du PMS5003... ");
  if (!pmsSerialStarted) {
      Serial.println("PMS Serial non initialisé.");
      // Mettre les données PMS à zéro/invalide
      pmsData.pm10_standard = pmsData.pm25_standard = pmsData.pm100_standard = 0;
      // Mettre aussi les autres valeurs à 0 si utilisées
      return;
  }

  // Tente de lire les données
  if (readPMSdata(&pmsSerial)) {
      // Succès: Les données sont dans la variable globale 'pmsData'
      Serial.print("OK. ");
      Serial.printf("PM1.0=%d, PM2.5=%d, PM10=%d (Std ug/m3)\n",
                    pmsData.pm10_standard, pmsData.pm25_standard, pmsData.pm100_standard);

  } else {
      // Échec: Le message d'erreur détaillé est imprimé DANS readPMSdata
      Serial.println("Echec de lecture.");
      // Mettre les données PMS à zéro/invalide en cas d'échec
      pmsData.pm10_standard = pmsData.pm25_standard = pmsData.pm100_standard = 0;
      // Mettre aussi les autres valeurs à 0 si utilisées
  }
}


// --- Fonction readPMSdata (copiée et adaptée, déjà présente) ---
// (Aucun changement nécessaire ici)
boolean readPMSdata(Stream *s) {
  byte startChar1 = 0, startChar2 = 0;
  unsigned long startMillis = millis();

  while (startChar1 != 0x42 || startChar2 != 0x4D) {
    if (millis() - startMillis > PMS_READ_TIMEOUT) {
      Serial.println("Erreur PMS: Timeout attente démarrage");
      while(s->available()) s->read();
      return false;
    }
    if (s->available()) {
      startChar1 = startChar2;
      startChar2 = s->read();
    } else {
      delay(5);
    }
  }

  byte buffer[32];
  buffer[0] = 0x42;
  buffer[1] = 0x4D;

  int bytesRead = 0;
  unsigned long readStart = millis();
  while(bytesRead < 30 && millis() - readStart < PMS_READ_TIMEOUT / 2) {
      if(s->available()) {
          buffer[2 + bytesRead] = s->read();
          bytesRead++;
      } else {
          delay(2);
      }
  }

  if (bytesRead < 30) {
    Serial.print("Erreur PMS: Lecture incomplète (");
    Serial.print(bytesRead + 2); Serial.println("/32 octets)");
    while(s->available()) s->read();
    return false;
  }

  uint16_t sum = 0;
  for (uint8_t i = 0; i < 30; i++) {
    sum += buffer[i];
  }
  uint16_t checksum_received = (buffer[30] << 8) | buffer[31];

  if (sum != checksum_received) {
    Serial.print("Erreur PMS: Checksum invalide. Calc:0x");
    Serial.print(sum, HEX); Serial.print(", Recu:0x"); Serial.println(checksum_received, HEX);
    return false;
  }

  pmsData.framelen        = (buffer[2] << 8) | buffer[3];
  pmsData.pm10_standard   = (buffer[4] << 8) | buffer[5];
  pmsData.pm25_standard   = (buffer[6] << 8) | buffer[7];
  pmsData.pm100_standard  = (buffer[8] << 8) | buffer[9];
  pmsData.pm10_env        = (buffer[10] << 8) | buffer[11];
  pmsData.pm25_env        = (buffer[12] << 8) | buffer[13];
  pmsData.pm100_env       = (buffer[14] << 8) | buffer[15];
  pmsData.particles_03um  = (buffer[16] << 8) | buffer[17];
  pmsData.particles_05um  = (buffer[18] << 8) | buffer[19];
  pmsData.particles_10um  = (buffer[20] << 8) | buffer[21];
  pmsData.particles_25um  = (buffer[22] << 8) | buffer[23];
  pmsData.particles_50um  = (buffer[24] << 8) | buffer[25];
  pmsData.particles_100um = (buffer[26] << 8) | buffer[27];
  pmsData.unused          = (buffer[28] << 8) | buffer[29];
  pmsData.checksum        = checksum_received;

  if (pmsData.framelen != 28) {
     Serial.print("Avertissement PMS: Longueur trame reçue: "); Serial.println(pmsData.framelen);
  }
  return true;
}


// --- Fonction d'Envoi des Données via LoRa ---
// <<< VÉRIFIER les variables BME280 utilisées >>>
void sendDataOverLoRa() {
  Serial.print("[LoRa] Préparation et envoi du paquet... ");
  digitalWrite(blueLED, HIGH); // Allume LED pendant l'envoi

  LoRa.beginPacket();

  // 1. Section GPS (Inchangé)
  LoRa.print("GPS,");
  bool gpsPosValid = gps.location.isValid() && gps.location.isUpdated() && gps.location.age() < 5000;
  bool gpsTimeValid = gps.time.isValid() && gps.time.isUpdated() && gps.time.age() < 5000;
  bool gpsDateValid = gps.date.isValid() && gps.date.isUpdated() && gps.date.age() < 5000;
  if (gpsPosValid) {
    LoRa.print(gps.location.lat(), 6); LoRa.print(",");
    LoRa.print(gps.location.lng(), 6); LoRa.print(",");
    LoRa.print(gps.altitude.meters()); LoRa.print(",");
    LoRa.print(gps.satellites.value()); LoRa.print(",");
  } else { LoRa.print("ERR,ERR,ERR,ERR,"); }
  if (gpsTimeValid) LoRa.printf("%02d:%02d:%02d,", gps.time.hour(), gps.time.minute(), gps.time.second());
  else LoRa.print("ERR,");
  if (gpsDateValid) LoRa.printf("%d/%d/%d", gps.date.day(), gps.date.month(), gps.date.year());
  else LoRa.print("ERR");

  // 2. Section Environnement (BME280 - DFRobot) <<< MODIFIÉ pour envoyer Pa et hPa
  LoRa.print("|ENV,");
  LoRa.print(isnan(temperature) ? -99.9 : temperature, 1); LoRa.print(",");
  // Envoi de la pression brute en Pa (uint32_t, mais LoRa.print peut gérer ça ou le caster implicitement)
  LoRa.print(pressurePa_raw); LoRa.print(","); // Pression Pa (uint32_t)
  LoRa.print(isnan(humidity) ? -99.9 : humidity, 1); LoRa.print(",");
  LoRa.print(isnan(altitude) ? -999.9 : altitude, 1); // Altitude (m)

  // 3. Section Qualité Air (ENS160 - Inchangé)
  LoRa.print("|AIR,");
  if (ensInitialized) { LoRa.print(airQuality); LoRa.print(","); LoRa.print(tvoc); LoRa.print(","); LoRa.print(eCO2); }
  else { LoRa.print("ERR,ERR,ERR"); }

  // 4. Section Ozone (DFRobot - Inchangé)
  LoRa.print("|OZ,");
  if (ozoneInitialized && ozoneConcentration >= 0) { LoRa.print(ozoneConcentration); }
  else { LoRa.print("ERR"); }

  // 5. Section UV (Analogique - Inchangé)
  LoRa.print("|UV,");
  if (uvIndex >= 0.0) { LoRa.print(uvIndex, 1); }
  else { LoRa.print("ERR"); }

  // 6. Section Particules (PMS5003 - Inchangé)
  LoRa.print("|PMS,");
  if (pmsSerialStarted) { // On envoie si le port série a été initialisé
      LoRa.print(pmsData.pm10_standard); LoRa.print(",");
      LoRa.print(pmsData.pm25_standard); LoRa.print(",");
      LoRa.print(pmsData.pm100_standard);
  } else { LoRa.print("ERR,ERR,ERR"); }

  // Fin du paquet
  pktCount++;
  Serial.print(" Paquet #"); Serial.print(pktCount); Serial.print("... ");
  // DEBUG: Voir la taille du payload avant envoi (approx)
  // Serial.printf(" (Taille estimée: %d octets)... ", LoRa.packetLength()); // Note: packetLength might not be accurate before endPacket

  int result = LoRa.endPacket(); // Envoi réel
  digitalWrite(blueLED, LOW); // Éteint LED après l'envoi

  if (result) { Serial.println("Succès."); }
  else { Serial.print("ECHEC! Code: "); Serial.println(result); }
} // Fin sendDataOverLoRa()


// --- Fonction de Mise à Jour de l'Affichage OLED ---
// <<< VÉRIFIER les variables BME280 utilisées >>>
void updateDisplay() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Ligne 1: Statut GPS (Fix et Sats - Inchangé)
  bool gpsFix = gps.location.isValid() && gps.location.age() < 5000;
  display.print("GPS:");
  if (gps.satellites.isValid()) display.printf(" Sat:%2d", gps.satellites.value());
  else display.print(" Sat:--");
  display.printf(" Fix:%s\n", gpsFix ? "OK" : "NO");

  // Ligne 2: Coordonnées GPS (si Fix - Inchangé)
  if (gpsFix) display.printf("La:%.3f Lo:%.3f\n", gps.location.lat(), gps.location.lng());
  else display.println("Lat: ---.--- Lon:---.---");

  // Ligne 3: Température & Humidité (BME - DFRobot) <<< VÉRIFIÉ
  display.print("T:");
  if (bmeInitialized && !isnan(temperature)) display.printf("%.1fC ", temperature);
  else display.print("ERR ");
  display.print("H:");
  if (bmeInitialized && !isnan(humidity)) display.printf("%.0f%%\n", humidity);
  else display.print("ERR\n");

  // Ligne 4: Pression (hPa) & Altitude (BME - DFRobot) <<< MODIFIÉ pour afficher pressureHpa
  display.print("P:");
  // Utiliser la variable pressureHpa qui contient la pression en hPa
  if (bmeInitialized && !isnan(pressureHpa)) display.printf("%.0fhPa ", pressureHpa);
  else display.print("ERR ");
  display.print("A:");
  if (bmeInitialized && !isnan(altitude)) display.printf("%.0fm\n", altitude);
  else display.print("ERR\n");

  // Ligne 5: Qualité Air (ENS) & Ozone (O3 - Inchangé)
  display.print("AQ:");
  if (ensInitialized) display.printf("%d ", airQuality);
  else display.print("E ");
  display.print("O3:");
   if (ozoneInitialized && ozoneConcentration >= 0) display.printf("%d\n", ozoneConcentration);
   else display.print("E\n");

  // Ligne 6: UV & PM2.5 (Standard - Inchangé)
  display.print("UV:");
  if (uvIndex >= 0.0) display.printf("%.1f ", uvIndex);
  else display.print("E ");
  display.print("PM2.5:");
  if (pmsSerialStarted) display.printf("%d\n", pmsData.pm25_standard); // Affiche 0 si lecture échoue (car reset à 0)
  else display.print("ERR\n");

   // Ligne 7: PM1.0 & PM10 (Standard - Inchangé)
   display.print("PM1:");
   if (pmsSerialStarted) display.printf("%d ", pmsData.pm10_standard);
   else display.print("E ");
   display.print("PM10:");
   if (pmsSerialStarted) display.printf("%d\n", pmsData.pm100_standard);
   else display.print("E\n");

  // Ligne 8: Heure GPS ou Compteur Paquet LoRa (Inchangé)
   if (gps.time.isValid() && gps.time.isUpdated() && gps.time.age() < 5000) {
       display.printf("UTC %02d:%02d:%02d", gps.time.hour(), gps.time.minute(), gps.time.second());
   } else if (loraInitialized) {
       display.printf("LoRa Pkt: %d", pktCount);
   } else {
       display.print("Time: ---");
   }

  display.display();
} // Fin updateDisplay()

// --- Fonction smartDelay (lit le GPS pendant l'attente - Inchangé) ---
static void smartDelay(unsigned long ms) {
  unsigned long start = millis();
  do {
    if (gpsSerialStarted) {
      while (GpsSerial.available() > 0) {
        gps.encode(GpsSerial.read());
      }
    }
    // Ne pas lire le PMS ici, car readPMSdata est bloquante
    yield(); // Important pour ESP32
  } while (millis() - start < ms);
} // Fin smartDelay()