
//////// Receiver ///////////

#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// --- Définitions (inchangées) ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define SCREEN_ADDRESS 0x3C
#define LORA_SCK 5
#define LORA_MISO 19
#define LORA_MOSI 27
#define LORA_CS 18
#define LORA_RST 23
#define LORA_IRQ 26
#define LED_PIN 4

// --- Structures de données (inchangées) ---
struct GPSData {
  float lat = 0.0;
  float lon = 0.0;
  float alt = 0.0;
  int satellites = 0;
  String time = "--:--:--";
  bool hasGPSFix = false; // Sera mis à jour par le parser
};
struct EnvData {
  float temperature = NAN;
  uint32_t pressure = 0;
  float humidity = NAN;
  float altitude = NAN;
  bool valid = false;
};
struct AirData {
  int airQuality = 0;
  int tvoc = 0;
  int eCO2 = 0;
  bool valid = false;
};
struct OtherData {
  int ozone = -1;
  float uvIndex = -1.0;
  bool ozValid = false;
  bool uvValid = false;
};

// --- Variables globales ---
GPSData gpsData;
EnvData envData;
AirData airData;
OtherData otherData;
unsigned long lastReceiveTime = 0;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- Setup (inchangé par rapport à la version précédente sans GPS) ---
void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("Échec OLED !"); while (1);
  }
  display.clearDisplay(); display.setTextSize(1); display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0); display.println("Init Recepteur..."); display.display();

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_IRQ);
  Serial.println("Init LoRa Recepteur...");
  if (!LoRa.begin(868E6)) {
    Serial.println("Échec LoRa !");
    display.clearDisplay(); display.setCursor(0, 0); display.println("LoRa: échec!"); display.display();
    while (1);
  }
  Serial.println("LoRa OK.");
  display.clearDisplay(); display.setCursor(0, 0); display.println("Attente données..."); display.display();

  initializeDataStructures(); // Initialise les structs
}

// --- Loop (inchangé par rapport à la version précédente sans GPS) ---
void loop() {
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    digitalWrite(LED_PIN, HIGH);
    String receivedData = "";
    while (LoRa.available()) { receivedData += (char)LoRa.read(); }
    lastReceiveTime = millis();

    parseReceivedData(receivedData); // Traitement des données
    updateDisplay();                 // Affichage OLED
    printDataToSerial(receivedData); // Affichage Série

    digitalWrite(LED_PIN, LOW);
  }

  // Gestion du Timeout (inchangée, mais l'affichage GPS dépendra de hasGPSFix)
  if (millis() - lastReceiveTime > 60000 && lastReceiveTime != 0) {
    display.clearDisplay(); display.setCursor(0, 0);
    display.println("Timeout (>60s)");
    display.println("Verifiez Sender");
    if(gpsData.hasGPSFix){ // Affiche les dernieres coords connues si on en avait
        display.print("Last Fix: ");
        display.printf("%.2f,%.2f", gpsData.lat, gpsData.lon);
    }
    display.display();
    // Option: Réinitialiser les données après timeout ?
    // initializeDataStructures();
    // Pour éviter que le message timeout reste indéfiniment:
    lastReceiveTime = millis(); // Reset le timer pour réafficher le timeout après 60 nouvelles secondes
  }
}

// --- initializeDataStructures (inchangé) ---
void initializeDataStructures() {
  gpsData.lat = 0.0; gpsData.lon = 0.0; gpsData.alt = 0.0;
  gpsData.satellites = 0; gpsData.time = "--:--:--"; gpsData.hasGPSFix = false;
  envData.temperature = NAN; envData.pressure = 0; envData.humidity = NAN;
  envData.altitude = NAN; envData.valid = false;
  airData.airQuality = 0; airData.tvoc = 0; airData.eCO2 = 0; airData.valid = false;
  otherData.ozone = -1; otherData.uvIndex = -1.0;
  otherData.ozValid = false; otherData.uvValid = false;
}

// --- parseReceivedData (MODIFIÉ pour gérer GPS optionnel) ---
void parseReceivedData(String data) {
  // Réinitialiser les flags/valeurs avant chaque parsing
  initializeDataStructures(); // Le plus simple est de tout réinitialiser

  Serial.println("Parsing data: " + data);

  // 1. Trouver les marqueurs principaux
  int gpsIndex = data.indexOf("GPS,");
  int envIndex = data.indexOf("|ENV,");
  int airIndex = data.indexOf("|AIR,");
  int ozIndex = data.indexOf("|OZ,");
  int uvIndex = data.indexOf("|UV,");

  // Vérifier si la structure de base est là
  if (gpsIndex != 0 || envIndex == -1) {
      Serial.println("Erreur parsing: Structure de message invalide (GPS, ou |ENV, manquant/mal placé).");
      return; // Ne pas continuer si la structure de base est cassée
  }

  // 2. Parser la section GPS (entre "GPS," et "|ENV,")
  String gpsSection = data.substring(gpsIndex + 4, envIndex);
  if (gpsSection == "NO_FIX" || gpsSection == "") {
    gpsData.hasGPSFix = false;
    Serial.println("  GPS: NO_FIX reçu.");
  } else {
    // Essayer de parser les données GPS
    int c1 = gpsSection.indexOf(',');
    int c2 = gpsSection.indexOf(',', c1 + 1);
    int c3 = gpsSection.indexOf(',', c2 + 1);
    int c4 = gpsSection.indexOf(',', c3 + 1);

    if (c1 != -1 && c2 != -1 && c3 != -1 && c4 != -1) {
      gpsData.lat = gpsSection.substring(0, c1).toFloat();
      gpsData.lon = gpsSection.substring(c1 + 1, c2).toFloat();
      gpsData.alt = gpsSection.substring(c2 + 1, c3).toFloat();
      gpsData.satellites = gpsSection.substring(c3 + 1, c4).toInt();
      gpsData.time = gpsSection.substring(c4 + 1);
      // Vérification simple si lat/lon semblent valides (pas 0.0 par ex.)
      if (gpsData.lat != 0.0 || gpsData.lon != 0.0) {
           gpsData.hasGPSFix = true;
           Serial.println("  GPS: Données valides parsées.");
      } else {
          gpsData.hasGPSFix = false; // Considérer 0,0 comme invalide
          Serial.println("  GPS: Données parsées mais semblent invalides (0,0).");
      }

    } else {
      gpsData.hasGPSFix = false; // Format incorrect dans la section GPS
      Serial.println("  GPS: Erreur parsing section GPS (mauvais format de virgules).");
    }
  }

  // 3. Parser la section ENV (entre "|ENV," et "|AIR,")
  if (airIndex > envIndex) { // S'assurer que AIR est après ENV
      String envSection = data.substring(envIndex + 5, airIndex);
      if (envSection.indexOf("ERR") == -1) {
          int c1 = envSection.indexOf(',');
          int c2 = envSection.indexOf(',', c1 + 1);
          int c3 = envSection.indexOf(',', c2 + 1);
          if (c1 != -1 && c2 != -1 && c3 != -1) {
              envData.temperature = envSection.substring(0, c1).toFloat();
              envData.pressure = envSection.substring(c1 + 1, c2).toInt(); // ou toLong
              envData.humidity = envSection.substring(c2 + 1, c3).toFloat();
              envData.altitude = envSection.substring(c3 + 1).toFloat();
              envData.valid = true;
              Serial.println("  ENV: Données valides parsées.");
          } else { Serial.println("  ENV: Erreur parsing section (virgules)."); }
      } else { Serial.println("  ENV: ERR reçu."); }
  } else if (envIndex != -1) { // Cas où ENV est la dernière section (peu probable ici mais pour robustesse)
       String envSection = data.substring(envIndex + 5);
       // ... (parsing similaire au dessus) ...
       Serial.println("  ENV: Traitement comme dernière section (parsing non implémenté complétement ici).");
  }


  // 4. Parser la section AIR (entre "|AIR," et "|OZ,")
  if (airIndex != -1 && ozIndex > airIndex) { // S'assurer que OZ est après AIR
      String airSection = data.substring(airIndex + 5, ozIndex);
       if (airSection.indexOf("ERR") == -1) {
          int c1 = airSection.indexOf(',');
          int c2 = airSection.indexOf(',', c1 + 1);
          if (c1 != -1 && c2 != -1) {
              airData.airQuality = airSection.substring(0, c1).toInt();
              airData.tvoc = airSection.substring(c1 + 1, c2).toInt();
              airData.eCO2 = airSection.substring(c2 + 1).toInt();
              airData.valid = true;
              Serial.println("  AIR: Données valides parsées.");
          } else { Serial.println("  AIR: Erreur parsing section (virgules)."); }
      } else { Serial.println("  AIR: ERR reçu."); }
  } else if (airIndex != -1) { // Cas où AIR est la dernière section
      String airSection = data.substring(airIndex + 5);
      // ... (parsing similaire) ...
      Serial.println("  AIR: Traitement comme dernière section (parsing non implémenté complétement ici).");
  }

  // 5. Parser la section Ozone (entre "|OZ," et "|UV,")
  if (ozIndex != -1 && uvIndex > ozIndex) { // S'assurer que UV est après OZ
      String ozSection = data.substring(ozIndex + 4, uvIndex);
      if (ozSection != "ERR") {
          otherData.ozone = ozSection.toInt();
          otherData.ozValid = true;
          Serial.println("  OZ: Donnée valide parsée.");
      } else { Serial.println("  OZ: ERR reçu."); }
  } else if (ozIndex != -1) { // Cas où OZ est la dernière section
       String ozSection = data.substring(ozIndex + 4);
       // ... (parsing similaire) ...
       Serial.println("  OZ: Traitement comme dernière section (parsing non implémenté complétement ici).");
  }


  // 6. Parser la section UV (après "|UV,")
  if (uvIndex != -1) {
      String uvSection = data.substring(uvIndex + 4);
      float tempUv = uvSection.toFloat();
      if (tempUv >= 0) { // Vérifier si la valeur n'est pas -1.0 (erreur sender)
          otherData.uvIndex = tempUv;
          otherData.uvValid = true;
           Serial.println("  UV: Donnée valide parsée.");
      } else { Serial.println("  UV: ERR ou invalide reçu."); }
  }
  Serial.println("--- Fin Parsing ---");
}


// --- updateDisplay (MODIFIÉ pour afficher GPS si valide) ---
void updateDisplay() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Ligne 1: Titre / Statut GPS
  if (gpsData.hasGPSFix) {
    display.print("GPS OK ("); display.print(gpsData.satellites); display.print(")");
    // Afficher l'heure si dispo et pas "NO_TIME"
    if(gpsData.time.length() > 0 && gpsData.time != "NO_TIME") {
        display.print(" "); display.print(gpsData.time);
    }
    display.println("");
  } else {
    display.println("GPS: Pas de Fix");
  }

  // Ligne 2: Lat / Lon (si fix) ou T/H
  if (gpsData.hasGPSFix) {
    display.printf("L:%.4f Lo:%.4f\n", gpsData.lat, gpsData.lon);
  } else {
    // Si pas de GPS, afficher T/H ici
    display.print("T:");
    if (envData.valid) display.print(envData.temperature, 1); else display.print("N/A");
    display.print("C H:");
    if (envData.valid) display.print(envData.humidity, 0); else display.print("N/A");
    display.println("%");
  }

  // Ligne 3: Altitude GPS/Baro (si fix) ou P/AltBaro
   if (gpsData.hasGPSFix) {
       display.print("AltGPS:"); display.print(gpsData.alt,0); display.print("m");
       display.print(" AltB:");
       if(envData.valid) display.print(envData.altitude,0); else display.print("N/A");
       display.println("m");
   } else {
        display.print("P:");
        if (envData.valid) display.print(envData.pressure / 100.0, 1); else display.print("N/A");
        display.print("hPa A:");
        if (envData.valid) display.print(envData.altitude, 0); else display.print("N/A");
        display.println("m");
   }


  // Ligne 4: AQI / CO2e
  display.print("AQI:");
  if (airData.valid) display.print(airData.airQuality); else display.print("N/A");
  display.print(" CO2e:");
  if (airData.valid) display.print(airData.eCO2); else display.print("N/A");
  //display.println("ppm");

  // Ligne 5: Ozone / UV
  display.print(" O3:"); // Alignement approx
  if (otherData.ozValid) display.print(otherData.ozone); else display.print("N/A");
  //display.print("ppb");
  display.print(" UV:");
  if (otherData.uvValid) display.print(otherData.uvIndex, 1); else display.print("N/A");
  display.println("");


  // Ligne 6: RSSI / Temps écoulé
  display.print("RSSI:"); display.print(LoRa.packetRssi());
  long secondsAgo = (millis() - lastReceiveTime) / 1000;
  display.print(" ("); display.print(secondsAgo); display.print("s)");

  display.display();
}

// --- printDataToSerial (MODIFIÉ pour afficher GPS si valide) ---
void printDataToSerial(String rawData) {
  Serial.println("\n--- DONNÉES REÇUES ---");
  Serial.println("Données brutes: " + rawData);
  Serial.print("RSSI: "); Serial.print(LoRa.packetRssi());
  Serial.print(" | SNR: "); Serial.println(LoRa.packetSnr());

  Serial.println("\n-- DONNÉES TRAITÉES --");

  if (gpsData.hasGPSFix) {
    Serial.println("GPS (Fix OK):");
    Serial.println("  Latitude: " + String(gpsData.lat, 6));
    Serial.println("  Longitude: " + String(gpsData.lon, 6));
    Serial.println("  Altitude GPS: " + String(gpsData.alt) + " m");
    Serial.println("  Satellites: " + String(gpsData.satellites));
    Serial.println("  Heure Fix: " + gpsData.time);
  } else {
    Serial.println("GPS: Pas de Fix Valide Reçu");
  }

  Serial.println("Environnement:");
  if (envData.valid) {
    Serial.println("  Température: " + String(envData.temperature, 2) + " °C");
    Serial.println("  Pression: " + String(envData.pressure / 100.0, 2) + " hPa");
    Serial.println("  Humidité: " + String(envData.humidity, 2) + " %");
    Serial.println("  Altitude baro: " + String(envData.altitude, 2) + " m");
  } else { Serial.println("  Données ENV non valides/reçues."); }

  Serial.println("Qualité de l'air:");
   if (airData.valid) {
    Serial.print("  Indice Qualité: " + String(airData.airQuality)); /* ... (switch case comme avant) ... */ Serial.println(")");
    Serial.println("  TVOC: " + String(airData.tvoc) + " ppb");
    Serial.println("  CO2 équivalent: " + String(airData.eCO2) + " ppm");
  } else { Serial.println("  Données AIR non valides/reçues."); }

  Serial.println("Autres données:");
   if (otherData.ozValid) { Serial.println("  Ozone: " + String(otherData.ozone) + " ppb"); }
   else { Serial.println("  Données Ozone non valides/reçues."); }
   if (otherData.uvValid) { Serial.println("  Indice UV: " + String(otherData.uvIndex, 2)); }
   else { Serial.println("  Données UV non valides/reçues."); }

  Serial.println("------------------------");
}

//////// Receiver ///////////
