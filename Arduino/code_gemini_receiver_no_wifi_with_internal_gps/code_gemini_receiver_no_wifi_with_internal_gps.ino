//////// Receiver - ADAPTÉ pour Sender avec DFRobot BME280 & PMS5003 ///////////
//////// AVEC GESTION BUZZER (PIN 13) ///////////

#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// --- Définitions (inchangées sauf ajout BUZZER_PIN) ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C
#define LORA_SCK 5
#define LORA_MISO 19
#define LORA_MOSI 27
#define LORA_CS 18
#define LORA_RST 23
#define LORA_IRQ 26
#define LED_PIN 4  // Même pin que le sender (BLUE_LED), peut être différente si besoin
#define BUZZER_PIN 13 // <<< AJOUT BUZZER >>> Broche du buzzer

// --- Structures de données (MODIFIÉES: Ajout PMSData) ---
struct GPSData {
  float lat = 0.0;
  float lon = 0.0;
  float alt = 0.0;
  int satellites = 0;
  String time = "--:--:--";
  String date = "--/--/----";
  bool hasGPSFix = false;
};

struct EnvData {
  float temperature = NAN;
  float pressure = NAN;  // Pression stockée en Pascals (Pa) reçus
  float humidity = NAN;
  float altitude = NAN;  // Altitude Barométrique
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

// <<< AJOUTÉ: Structure pour les données PMS >>>
struct PMSData {
  int pm1_std = 0;
  int pm25_std = 0;
  int pm10_std = 0;  // Note: pm100_standard du sender correspond à PM10
  bool valid = false;
};

// --- Variables globales (MODIFIÉES: Ajout pmsData) ---
GPSData gpsData;
EnvData envData;
AirData airData;
OtherData otherData;
PMSData pmsData;  // <<< AJOUTÉ: Instance de la structure PMS
unsigned long lastReceiveTime = 0;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- Fonction pour le motif de bips de perte de signal ---
// <<< AJOUT BUZZER >>>
void signalLostBeepPattern() {
  int beepDuration = 1000; // Durée du bip en ms
  int pauseDuration =500; // Durée de la pause entre les bips en ms
  int numBeeps = 3;       // Nombre de bips dans le motif

  Serial.println("Signal LoRa perdu - Emission motif sonore...");
  for (int i = 0; i < numBeeps; i++) {
    tone(BUZZER_PIN, 1000);
    delay(beepDuration);
    noTone(BUZZER_PIN);
    if (i < numBeeps - 1) { // Ne pas faire de pause après le dernier bip
      delay(pauseDuration);
    }
  }
  // Le motif s'arrête ici. Il ne reprendra que si un nouveau timeout se produit.
}


// --- Setup (Modifié pour initialiser le buzzer) ---
void setup() {
  Serial.begin(115200);
  while (!Serial)
    ;
  Serial.println("\n\n--- Démarrage TTGO T-Beam V1.2 Multi-Sensor Sender ---");
  Serial.println("[CONFIG] Mode: Long Range LoRa activé.");

  // Configuration LoRa (inchangée)
  LoRa.setSpreadingFactor(10);
  Serial.println("   Spreading Factor: 10");
  LoRa.setSignalBandwidth(125E3);
  Serial.println("   Signal Bandwidth: 125 kHz");
  LoRa.setCodingRate4(8);

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // <<< AJOUT BUZZER >>> Initialisation du buzzer
  pinMode(BUZZER_PIN, OUTPUT);
  noTone(BUZZER_PIN); // S'assurer qu'il est éteint au début

  // Initialisation OLED (inchangée)
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("Échec OLED ! Vérifiez câblage/adresse.");
    while (1)
      ;
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Init Recepteur...");
  display.display();

  // Initialisation LoRa (inchangée)
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_IRQ);
  Serial.println("Init LoRa Recepteur...");
  if (!LoRa.begin(868E6)) {
    Serial.println("-> ÉCHEC LoRa ! Vérifiez module/broches.");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("LoRa: echec!");
    display.display();
    while (1)
      ;
  }
  Serial.println("-> LoRa OK (868 MHz).");
  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("Attente donnees...");
  display.display();

  initializeDataStructures();  // Initialise les structs (y compris PMS)
}

// --- Loop (Modifié pour les bips) ---
void loop() {
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    digitalWrite(LED_PIN, HIGH); // Allume la LED

    // <<< AJOUT BUZZER >>> Faire un bip court à la réception
    tone(BUZZER_PIN, 1000);
    delay(500); // Durée du bip de réception (ajuster si besoin)
    noTone(BUZZER_PIN);

    String receivedData = "";
    while (LoRa.available()) { receivedData += (char)LoRa.read(); }
    lastReceiveTime = millis(); // Mettre à jour le temps de la dernière réception

    Serial.println("\nPaquet Recu:");
    parseReceivedData(receivedData);  // Traitement des données (modifié)
    updateDisplay();                  // Affichage OLED (modifié)
    printDataToSerial(receivedData);  // Affichage Série détaillée (modifié)

    digitalWrite(LED_PIN, LOW); // Eteint la LED
  }

  // Gestion du Timeout (Modifié pour le motif de bips)
  // Vérifie si plus de 60 secondes se sont écoulées depuis la dernière réception valide
  if (millis() - lastReceiveTime > 60000 && lastReceiveTime != 0) {

    // <<< AJOUT BUZZER >>> Jouer le motif sonore de perte de signal
    signalLostBeepPattern();

    // Afficher le message de timeout sur l'OLED (comme avant)
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("Timeout (>60s)");
    display.println("Verifiez Sender");
    if (gpsData.hasGPSFix) {
      display.print("Last Fix: ");
      display.printf("%.2f,%.2f", gpsData.lat, gpsData.lon);
    }
    display.display();

    // Réinitialiser pour ne pas re-déclencher le timeout immédiatement
    // et effacer les anciennes données
    lastReceiveTime = 0;
    initializeDataStructures();
    Serial.println("Timeout: Reinitialisation des donnees et attente."); // Log série
  }
}

// --- initializeDataStructures (INCHANGÉ) ---
void initializeDataStructures() {
  gpsData.lat = 0.0;
  gpsData.lon = 0.0;
  gpsData.alt = 0.0;
  gpsData.satellites = 0;
  gpsData.time = "--:--:--";
  gpsData.date = "--/--/----";
  gpsData.hasGPSFix = false;

  envData.temperature = NAN;
  envData.pressure = NAN;
  envData.humidity = NAN;
  envData.altitude = NAN;
  envData.valid = false;

  airData.airQuality = 0;
  airData.tvoc = 0;
  airData.eCO2 = 0;
  airData.valid = false;

  otherData.ozone = -1;
  otherData.uvIndex = -1.0;
  otherData.ozValid = false;
  otherData.uvValid = false;

  pmsData.pm1_std = 0;
  pmsData.pm25_std = 0;
  pmsData.pm10_std = 0;
  pmsData.valid = false;
}

// --- parseReceivedData (INCHANGÉ) ---
void parseReceivedData(String data) {
  // Réinitialiser les flags/valeurs avant chaque parsing
  initializeDataStructures();

  Serial.println("  Parsing data: " + data);

  // 1. Trouver les marqueurs principaux
  int gpsIndex = data.indexOf("GPS,");
  int envIndex = data.indexOf("|ENV,");
  int airIndex = data.indexOf("|AIR,");
  int ozIndex = data.indexOf("|OZ,");
  int uvIndex = data.indexOf("|UV,");
  int pmsIndex = data.indexOf("|PMS,");

  if (gpsIndex != 0 || envIndex == -1) {
    Serial.println("  ERREUR PARSING: Structure message invalide (GPS, ou |ENV, manquant/mal place).");
    return;
  }

  // 2. Parser la section GPS
  String gpsSection = data.substring(gpsIndex + 4, envIndex);
  int c1_gps = gpsSection.indexOf(',');
  int c2_gps = gpsSection.indexOf(',', c1_gps + 1);
  int c3_gps = gpsSection.indexOf(',', c2_gps + 1);
  int c4_gps = gpsSection.indexOf(',', c3_gps + 1);
  int c5_gps = gpsSection.indexOf(',', c4_gps + 1);
  if (c1_gps != -1 && c2_gps != -1 && c3_gps != -1 && c4_gps != -1 && c5_gps != -1) {
    if (gpsSection.substring(0, c1_gps) != "ERR") {
      gpsData.lat = gpsSection.substring(0, c1_gps).toFloat();
      gpsData.lon = gpsSection.substring(c1_gps + 1, c2_gps).toFloat();
      gpsData.alt = gpsSection.substring(c2_gps + 1, c3_gps).toFloat();
      gpsData.satellites = gpsSection.substring(c3_gps + 1, c4_gps).toInt();
      gpsData.time = gpsSection.substring(c4_gps + 1, c5_gps);
      gpsData.date = gpsSection.substring(c5_gps + 1);
      if (gpsData.lat != 0.0 || gpsData.lon != 0.0) gpsData.hasGPSFix = true;
      Serial.println("    GPS: Donnees valides parsees.");
    } else {
      Serial.println("    GPS: Section ERR recue.");
    }
  } else {
    Serial.println("    GPS: ERREUR PARSING section (format/virgules). Section=" + gpsSection);
  }

  // 3. Parser la section ENV
  if (envIndex != -1) {
    int endEnv = (airIndex != -1) ? airIndex : (ozIndex != -1 ? ozIndex : (uvIndex != -1 ? uvIndex : (pmsIndex != -1 ? pmsIndex : data.length())));
    String envSection = data.substring(envIndex + 5, endEnv);
    int c1_env = envSection.indexOf(',');
    int c2_env = envSection.indexOf(',', c1_env + 1);
    int c3_env = envSection.indexOf(',', c2_env + 1);
    if (c1_env != -1 && c2_env != -1 && c3_env != -1) {
      if (envSection.substring(0, c1_env).indexOf("ERR") == -1) {
        envData.temperature = envSection.substring(0, c1_env).toFloat();
        envData.pressure = envSection.substring(c1_env + 1, c2_env).toFloat();
        envData.humidity = envSection.substring(c2_env + 1, c3_env).toFloat();
        envData.altitude = envSection.substring(c3_env + 1).toFloat();
        envData.valid = true;
        Serial.println("    ENV: Donnees valides parsees.");
      } else {
        Serial.println("    ENV: Section ERR recue.");
      }
    } else {
      Serial.println("    ENV: ERREUR PARSING section (virgules). Section=" + envSection);
    }
  }

  // 4. Parser la section AIR
  if (airIndex != -1) {
    int endAir = (ozIndex != -1) ? ozIndex : (uvIndex != -1 ? uvIndex : (pmsIndex != -1 ? pmsIndex : data.length()));
    String airSection = data.substring(airIndex + 5, endAir);
    int c1_air = airSection.indexOf(',');
    int c2_air = airSection.indexOf(',', c1_air + 1);
    if (c1_air != -1 && c2_air != -1) {
      if (airSection.substring(0, c1_air).indexOf("ERR") == -1) {
        airData.airQuality = airSection.substring(0, c1_air).toInt();
        airData.tvoc = airSection.substring(c1_air + 1, c2_air).toInt();
        airData.eCO2 = airSection.substring(c2_air + 1).toInt();
        airData.valid = true;
        Serial.println("    AIR: Donnees valides parsees.");
      } else {
        Serial.println("    AIR: Section ERR recue.");
      }
    } else {
      Serial.println("    AIR: ERREUR PARSING section (virgules). Section=" + airSection);
    }
  }

  // 5. Parser la section Ozone
  if (ozIndex != -1) {
    int endOz = (uvIndex != -1) ? uvIndex : (pmsIndex != -1 ? pmsIndex : data.length());
    String ozSection = data.substring(ozIndex + 4, endOz);
    if (ozSection != "ERR" && ozSection.length() > 0) {
      otherData.ozone = ozSection.toInt();
      otherData.ozValid = true;
      Serial.println("    OZ: Donnee valide parsee.");
    } else {
      Serial.println("    OZ: ERR recu ou section vide.");
    }
  }

  // 6. Parser la section UV
  if (uvIndex != -1) {
    int endUv = (pmsIndex != -1) ? pmsIndex : data.length();
    String uvSection = data.substring(uvIndex + 4, endUv);
    if (uvSection != "ERR" && uvSection.length() > 0) {
      otherData.uvIndex = uvSection.toFloat();
      if (otherData.uvIndex >= 0.0) {
        otherData.uvValid = true;
        Serial.println("    UV: Donnee valide parsee.");
      } else {
        Serial.println("    UV: Donnee recue mais invalide (<0).");
      }
    } else {
      Serial.println("    UV: ERR recu ou section vide.");
    }
  }

  // 7. Parser la section PMS
  if (pmsIndex != -1) {
    String pmsSection = data.substring(pmsIndex + 5);
    int c1_pms = pmsSection.indexOf(',');
    int c2_pms = pmsSection.indexOf(',', c1_pms + 1);
    if (c1_pms != -1 && c2_pms != -1) {
      if (pmsSection.substring(0, c1_pms).indexOf("ERR") == -1) {
        pmsData.pm1_std = pmsSection.substring(0, c1_pms).toInt();
        pmsData.pm25_std = pmsSection.substring(c1_pms + 1, c2_pms).toInt();
        pmsData.pm10_std = pmsSection.substring(c2_pms + 1).toInt();
        pmsData.valid = true;
        Serial.println("    PMS: Donnees valides parsees.");
      } else {
        Serial.println("    PMS: Section ERR recue.");
      }
    } else {
      Serial.println("    PMS: ERREUR PARSING section (virgules). Section=" + pmsSection);
    }
  }

  Serial.println("  --- Fin Parsing ---");
}

// --- updateDisplay (INCHANGÉ) ---
void updateDisplay() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Ligne 1: Statut GPS et Heure
  if (gpsData.hasGPSFix) {
    display.printf("GPS:%d FIX", gpsData.satellites);
    if (gpsData.time != "--:--:--") {
      display.print(" ");
      display.print(gpsData.time);
    }
    display.println("");
  } else {
    display.println("GPS: No Fix");
  }

  // Ligne 2: Lat / Lon (si fix) ou T/H
  if (gpsData.hasGPSFix) {
    display.printf("L:%.3f Lo:%.3f\n", gpsData.lat, gpsData.lon);
  } else {
    display.print("T:");
    if (envData.valid) display.printf("%.1fC", envData.temperature);
    else display.print("N/A");
    display.print(" H:");
    if (envData.valid) display.printf("%.0f%%", envData.humidity);
    else display.print("N/A");
    display.println();
  }

  // Ligne 3: Alt GPS/Baro (si fix) ou P(hPa)/AltBaro
  if (gpsData.hasGPSFix) {
    display.print("A_GPS:");
    display.printf("%.0fm ", gpsData.alt);
    display.print("A_B:");
    if (envData.valid && !isnan(envData.altitude)) display.printf("%.0fm", envData.altitude);
    else display.print("N/A");
    display.println();
  } else {
    display.print("P:");
    if (envData.valid && !isnan(envData.pressure)) display.printf("%.0fhPa ", envData.pressure / 100.0);
    else display.print("N/A ");
    display.print("A:");
    if (envData.valid && !isnan(envData.altitude)) display.printf("%.0fm", envData.altitude);
    else display.print("N/A");
    display.println();
  }

  // Ligne 4: AQI / CO2e
  display.print("AQ:");
  if (airData.valid) display.print(airData.airQuality);
  else display.print("N/A");
  display.print(" CO2:");
  if (airData.valid) display.print(airData.eCO2);
  else display.print("N/A");
  display.println(" ");

  // Ligne 5: Ozone / UV
  display.print("O3:");
  if (otherData.ozValid) display.print(otherData.ozone);
  else display.print("N/A");
  display.print(" UV:");
  if (otherData.uvValid) display.printf("%.1f", otherData.uvIndex);
  else display.print("N/A");
  display.println("");

  // Ligne 6: PM2.5 / PM10
  display.print("PM2.5:");
  if (pmsData.valid) display.printf("%d", pmsData.pm25_std);
  else display.print("N/A");
  display.print(" PM10:");
  if (pmsData.valid) display.printf("%d", pmsData.pm10_std);
  else display.print("N/A");
  display.println("");

  // Ligne 7: PM1.0 / RSSI
  display.print("PM1:");
  if (pmsData.valid) display.printf("%d", pmsData.pm1_std);
  else display.print("N/A");
  display.print(" RSSI:");
  display.print(LoRa.packetRssi());
  display.println("");

  // Ligne 8: Temps écoulé depuis réception
  if (lastReceiveTime > 0) {
    long secondsAgo = (millis() - lastReceiveTime) / 1000;
    display.print("Recu il y a: ");
    display.print(secondsAgo);
    display.print("s");
  } else {
    display.print("Attente...");
  }

  display.display();
}

// --- printDataToSerial (INCHANGÉ) ---
void printDataToSerial(String rawData) {
  Serial.println("\n--- DONNEES RECUES ---");
  Serial.println("  Donnees brutes: " + rawData);
  Serial.print("  RSSI: ");
  Serial.print(LoRa.packetRssi());
  Serial.print(" | SNR: ");
  Serial.println(LoRa.packetSnr());

  Serial.println("\n-- DONNEES TRAITEES --");

  // Section GPS
  if (gpsData.hasGPSFix) {
    Serial.println("  GPS (Fix OK):");
    Serial.println("    Latitude: " + String(gpsData.lat, 6));
    Serial.println("    Longitude: " + String(gpsData.lon, 6));
    Serial.println("    Altitude GPS: " + String(gpsData.alt, 1) + " m");
    Serial.println("    Satellites: " + String(gpsData.satellites));
    Serial.println("    Heure UTC: " + gpsData.time);
    Serial.println("    Date UTC: " + gpsData.date);
  } else {
    Serial.println("  GPS: Pas de Fix Valide Recu");
  }

  // Section Environnement
  Serial.println("  Environnement:");
  if (envData.valid) {
    Serial.println("    Temperature: " + String(envData.temperature, 1) + " C");
    Serial.println("    Pression: " + String(envData.pressure / 100.0, 1) + " hPa (" + String(envData.pressure, 0) + " Pa)");
    Serial.println("    Humidite: " + String(envData.humidity, 1) + " %");
    Serial.println("    Altitude baro: " + String(envData.altitude, 1) + " m");
  } else {
    Serial.println("    Donnees ENV non valides/recues.");
  }

  // Section Qualité Air
  Serial.println("  Qualite de l'air:");
  if (airData.valid) {
    Serial.print("    Indice Qualite Air (AQI): " + String(airData.airQuality) + " (");
    switch (airData.airQuality) {
      case 1: Serial.print("Excellent"); break;
      case 2: Serial.print("Bon"); break;
      case 3: Serial.print("Moyen"); break;
      case 4: Serial.print("Mauvais"); break;
      case 5: Serial.print("Tres Mauvais"); break;
      default: Serial.print("Inconnu"); break;
    }
    Serial.println(")");
    Serial.println("    TVOC: " + String(airData.tvoc) + " ppb");
    Serial.println("    CO2 equivalent: " + String(airData.eCO2) + " ppm");
  } else {
    Serial.println("    Donnees AIR non valides/recues.");
  }

  // Section Autres Données
  Serial.println("  Autres donnees:");
  if (otherData.ozValid) {
    Serial.println("    Ozone: " + String(otherData.ozone) + " ppb");
  } else {
    Serial.println("    Donnees Ozone non valides/recues.");
  }
  if (otherData.uvValid) {
    Serial.println("    Indice UV: " + String(otherData.uvIndex, 1));
  } else {
    Serial.println("    Donnees UV non valides/recues.");
  }

  // Section PMS
  Serial.println("  Particules (Standard):");
  if (pmsData.valid) {
    Serial.println("    PM1.0 : " + String(pmsData.pm1_std) + " ug/m3");
    Serial.println("    PM2.5 : " + String(pmsData.pm25_std) + " ug/m3");
    Serial.println("    PM10  : " + String(pmsData.pm10_std) + " ug/m3");
  } else {
    Serial.println("    Donnees PMS non valides/recues.");
  }

  Serial.println("------------------------");
}