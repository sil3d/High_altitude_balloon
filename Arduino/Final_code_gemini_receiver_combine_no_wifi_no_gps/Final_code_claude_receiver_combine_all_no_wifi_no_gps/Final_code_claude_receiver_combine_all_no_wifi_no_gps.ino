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

// Structure pour stocker les données GPS (conservée mais non utilisée activement)
struct GPSData {
  float lat;
  float lon;
  float alt;
  int satellites;
  String time;
  bool hasGPSFix; // Sera toujours false
};

// Structure pour stocker les données environnementales
struct EnvData {
  float temperature;
  uint32_t pressure; // Pression en Pascals (Pa)
  float humidity;
  float altitude;
  bool valid; // Indicateur si les données ENV sont valides/présentes
};

// Structure pour stocker les données de qualité d'air
struct AirData {
  int airQuality;
  int tvoc;
  int eCO2;
  bool valid; // Indicateur si les données AIR sont valides/présentes
};

// Structure pour stocker les données d'ozone et UV
struct OtherData {
  int ozone;
  float uvIndex;
  bool ozValid; // Indicateur si les données Ozone sont valides/présentes
  bool uvValid; // Indicateur si les données UV sont valides/présentes
};

// Variables pour stocker les données reçues
GPSData gpsData; // Conservée mais non remplie
EnvData envData;
AirData airData;
OtherData otherData;

// Dernier temps de réception
unsigned long lastReceiveTime = 0;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW); // S'assurer que la LED est éteinte

  // Initialisation de l'OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    // Serial.println peut rester ici car c'est au setup
    Serial.println("Échec de l'initialisation de l'OLED !");
    while (1);
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Initialisation Recep...");
  display.display();

  // Initialisation du module LoRa
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_IRQ);

  // Serial.println peut rester ici
  Serial.println("Initialisation LoRa Recepteur...");
  if (!LoRa.begin(868E6)) {
    Serial.println("Échec de l'initialisation du module LoRa!");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("LoRa: échec!");
    display.display();
    while (1);
  }

  Serial.println("LoRa initialisé.");
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

    // 1. Traiter les données reçues pour MAJ structures et OLED
    parseReceivedData(receivedData);

    // 2. Afficher les données sur l'écran OLED (utilise display.print)
    updateDisplay();

    // 3. *** CONSTRUIRE ET ENVOYER LA LIGNE DE DONNÉES POUR PYTHON ***
    String dataForPython = "";

    // -- Section ENV --
    if (envData.valid) {
      dataForPython += "ENV,";
      dataForPython += String(envData.temperature, 2); // 2 décimales
      dataForPython += ",";
      // ENVOYER LA PRESSION EN Pascals (Pa) - Python s'attend à ça
      dataForPython += String(envData.pressure);
      dataForPython += ",";
      dataForPython += String(envData.humidity, 2); // 2 décimales
      dataForPython += ",";
      dataForPython += String(envData.altitude, 2); // 2 décimales
    } else {
      dataForPython += "ENV,ERR,ERR,ERR,ERR";
    }

    // -- Section AIR --
    dataForPython += "|"; // Séparateur
    if (airData.valid) {
      dataForPython += "AIR,";
      dataForPython += String(airData.airQuality);
      dataForPython += ",";
      dataForPython += String(airData.tvoc);
      dataForPython += ",";
      dataForPython += String(airData.eCO2);
    } else {
      dataForPython += "AIR,ERR,ERR,ERR";
    }

    // -- Section OZ --
    dataForPython += "|"; // Séparateur
    if (otherData.ozValid) {
      dataForPython += "OZ,";
      dataForPython += String(otherData.ozone);
    } else {
      dataForPython += "OZ,ERR";
    }

    // -- Section UV --
    dataForPython += "|"; // Séparateur
    if (otherData.uvValid) {
      dataForPython += "UV,";
      dataForPython += String(otherData.uvIndex, 2); // 2 décimales
    } else {
      dataForPython += "UV,ERR";
    }

    // -- Section RSSI -- (Toujours ajoutée)
    dataForPython += "|"; // Séparateur
    dataForPython += "RSSI,";
    dataForPython += String(LoRa.packetRssi());

    // 4. *** Envoyer la ligne unique via Serial pour Python ***
    Serial.println(dataForPython);

    // 5. *** L'APPEL À printDataToSerial EST SUPPRIMÉ CI-DESSOUS ***
    // printDataToSerial(receivedData); // <-- NE PAS UTILISER CETTE FONCTION

    digitalWrite(LED_PIN, LOW);
  }

  // Gestion du timeout (inchangée)
  if (millis() - lastReceiveTime > 60000 && lastReceiveTime != 0) {
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Aucune donnée reçue");
    display.println("depuis > 60s");
    display.display();
    // Optionnel : envoyer un message d'erreur à Python ?
    // Serial.println("ERROR,TIMEOUT"); // Le parser Python devrait ignorer ça
  }
}

// La fonction initializeDataStructures reste inchangée
void initializeDataStructures() {
  gpsData.hasGPSFix = false;
  envData.valid = false;
  airData.valid = false;
  otherData.ozValid = false;
  otherData.uvValid = false;
  // Mettre à 0 les valeurs si nécessaire, mais les flags 'valid' sont le plus important
}

// La fonction parseReceivedData reste inchangée (elle met à jour les structures)
// Assure-toi qu'elle fonctionne correctement et met bien les flags .valid/.ozValid/.uvValid
void parseReceivedData(String data) {
  // Réinitialiser les flags de validité avant chaque parsing
  envData.valid = false;
  airData.valid = false;
  otherData.ozValid = false;
  otherData.uvValid = false;
  gpsData.hasGPSFix = false; // Assurer que c'est toujours faux

  int envIndex = data.indexOf("ENV,");
  int airIndex = data.indexOf("|AIR,");
  int ozIndex = data.indexOf("|OZ,");
  int uvIndex = data.indexOf("|UV,");

  if (envIndex == 0) {
    int endEnvIndex = (airIndex > envIndex) ? airIndex : data.length();
    String envSection = data.substring(envIndex + 4, endEnvIndex);
    if (envSection.indexOf("ERR") == -1) {
        int commaIndex1 = envSection.indexOf(',');
        int commaIndex2 = envSection.indexOf(',', commaIndex1 + 1);
        int commaIndex3 = envSection.indexOf(',', commaIndex2 + 1);
        if (commaIndex1 != -1 && commaIndex2 != -1 && commaIndex3 != -1) {
            envData.temperature = envSection.substring(0, commaIndex1).toFloat();
            // Lit la pression comme un entier (uint32_t peut gérer de grandes valeurs Pa)
            envData.pressure = envSection.substring(commaIndex1 + 1, commaIndex2).toInt(); // Ou .toFloat() si envoyé comme float
            envData.humidity = envSection.substring(commaIndex2 + 1, commaIndex3).toFloat();
            envData.altitude = envSection.substring(commaIndex3 + 1).toFloat();
            envData.valid = true;
        } // else { Serial.println("Err Parse ENV"); } // Garder les Serial.println d'erreur ici est OK
    } // else { Serial.println("ENV ERR Rcvd"); }
  } // else { Serial.println("ENV marker missing"); }

  if (airIndex > envIndex) {
    int endAirIndex = (ozIndex > airIndex) ? ozIndex : data.length();
    String airSection = data.substring(airIndex + 5, endAirIndex);
    if (airSection.indexOf("ERR") == -1) {
        int commaIndex1 = airSection.indexOf(',');
        int commaIndex2 = airSection.indexOf(',', commaIndex1 + 1);
        if (commaIndex1 != -1 && commaIndex2 != -1) {
            airData.airQuality = airSection.substring(0, commaIndex1).toInt();
            airData.tvoc = airSection.substring(commaIndex1 + 1, commaIndex2).toInt();
            airData.eCO2 = airSection.substring(commaIndex2 + 1).toInt();
            airData.valid = true;
        } // else { Serial.println("Err Parse AIR"); }
    } // else { Serial.println("AIR ERR Rcvd"); }
  }

  if (ozIndex > airIndex) {
    int endOzIndex = (uvIndex > ozIndex) ? uvIndex : data.length();
    String ozSection = data.substring(ozIndex + 4, endOzIndex);
    if (ozSection != "ERR") {
        otherData.ozone = ozSection.toInt();
        otherData.ozValid = true;
    } // else { Serial.println("OZ ERR Rcvd"); }
  }

  if (uvIndex > ozIndex) {
    String uvSection = data.substring(uvIndex + 4);
    float tempUv = uvSection.toFloat();
    // Si le sender envoie "ERR" pour UV, toFloat() donnera 0.0.
    // Si le sender envoie -1.0 pour erreur, cette condition est bonne.
    // Adapte si le sender envoie "ERR" textuel pour UV.
    if (uvSection != "ERR" && tempUv >= 0) {
        otherData.uvIndex = tempUv;
        otherData.uvValid = true;
    } // else { Serial.println("UV ERR/Invalid Rcvd"); }
  }
}


// La fonction updateDisplay reste inchangée (elle utilise display.print)
void updateDisplay() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);

  display.println("--- DATA RECU ---");

  // Ligne 1: Temp & Hum
  display.print("T:");
  if (envData.valid) display.print(envData.temperature, 1); else display.print("N/A");
  display.print("C ");
  display.print("H:");
  if (envData.valid) display.print(envData.humidity, 0); else display.print("N/A");
  display.println("%");

  // Ligne 2: Pression(hPa) & Alt Baro
  display.print("P:");
  if (envData.valid) display.print(envData.pressure / 100.0, 1); else display.print("N/A"); // Affichage OLED en hPa
  display.print("hPa ");
  display.print("Alt:");
  if (envData.valid) display.print(envData.altitude, 0); else display.print("N/A");
  display.println("m");

  // Ligne 3: AQI & eCO2
  display.print("AQI:");
  if (airData.valid) display.print(airData.airQuality); else display.print("N/A");
  display.print(" CO2e:");
  if (airData.valid) display.print(airData.eCO2); else display.print("N/A");
  display.println(""); // Pas de place pour ppm

  // Ligne 4: Ozone & UV
  display.print(" O3:");
  if (otherData.ozValid) display.print(otherData.ozone); else display.print("N/A");
  display.print(" UV:");
  if (otherData.uvValid) display.print(otherData.uvIndex, 1); else display.print("N/A");
  display.println("");

  // Ligne 6: RSSI & Temps écoulé
  display.print("RSSI:");
  display.print(LoRa.packetRssi());
  display.print("dBm");
  long secondsAgo = (millis() - lastReceiveTime) / 1000;
  display.print(" ("); display.print(secondsAgo); display.print("s)");

  display.display();
}

// *** CETTE FONCTION N'EST PLUS APPELÉE DEPUIS loop() ***
// Tu peux la commenter ou la supprimer complètement si tu veux.
/*
void printDataToSerial(String rawData) {
  // ... (tout le code de cette fonction est maintenant inutile pour Python) ...
}
*/