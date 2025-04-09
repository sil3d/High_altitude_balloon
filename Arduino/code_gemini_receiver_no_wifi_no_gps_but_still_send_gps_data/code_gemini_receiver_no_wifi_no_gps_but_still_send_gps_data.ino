//////// Receiver - CORRIGÉ pour Sender v2 ///////////

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
#define LED_PIN 4 // Même pin que le sender (BLUE_LED), peut être différente si besoin

// --- Structures de données (MODIFIÉES) ---
struct GPSData {
  float lat = 0.0;
  float lon = 0.0;
  float alt = 0.0;
  int satellites = 0;
  String time = "--:--:--";
  String date = "--/--/----"; // <-- AJOUTÉ: Champ pour la date
  bool hasGPSFix = false;
};

struct EnvData {
  float temperature = NAN;
  float pressure = NAN;   // <-- MODIFIÉ: Pression en float (Pa)
  float humidity = NAN;
  float altitude = NAN;   // Altitude Barométrique
  bool valid = false;
};

struct AirData { // Inchangé
  int airQuality = 0;
  int tvoc = 0;
  int eCO2 = 0;
  bool valid = false;
};

struct OtherData { // Inchangé
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

// --- Setup (inchangé) ---
void setup() {
  Serial.begin(115200);
  while (!Serial); // Attente pour certaines cartes
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("Échec OLED ! Vérifiez câblage/adresse."); while (1);
  }
  display.clearDisplay(); display.setTextSize(1); display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0); display.println("Init Recepteur..."); display.display();

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_IRQ);
  Serial.println("Init LoRa Recepteur...");
  if (!LoRa.begin(868E6)) {
    Serial.println("-> ÉCHEC LoRa ! Vérifiez module/broches.");
    display.clearDisplay(); display.setCursor(0, 0); display.println("LoRa: echec!"); display.display();
    while (1);
  }
  Serial.println("-> LoRa OK (868 MHz).");
  display.clearDisplay(); display.setCursor(0, 0); display.println("Attente donnees..."); display.display();

  initializeDataStructures(); // Initialise les structs
}

// --- Loop (inchangé) ---
void loop() {
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    digitalWrite(LED_PIN, HIGH); // Allumer LED pendant réception/traitement
    String receivedData = "";
    while (LoRa.available()) { receivedData += (char)LoRa.read(); }
    lastReceiveTime = millis();

    Serial.println("\nPaquet Recu:");
    parseReceivedData(receivedData); // Traitement des données
    updateDisplay();                 // Affichage OLED
    printDataToSerial(receivedData); // Affichage Série détaillée

    digitalWrite(LED_PIN, LOW); // Éteindre LED après traitement
  }

  // Gestion du Timeout (inchangée)
  if (millis() - lastReceiveTime > 60000 && lastReceiveTime != 0) { // Timeout après 60 secondes
    display.clearDisplay(); display.setCursor(0, 0);
    display.println("Timeout (>60s)");
    display.println("Verifiez Sender");
    if(gpsData.hasGPSFix){ // Affiche les dernieres coords connues si on en avait
        display.print("Last Fix: ");
        display.printf("%.2f,%.2f", gpsData.lat, gpsData.lon);
    }
    display.display();
    // Pour éviter que le message timeout reste indéfiniment et pollue:
    lastReceiveTime = 0; // Réinitialise pour ne réafficher qu'au prochain timeout
    initializeDataStructures(); // Optionnel: remettre les données à zéro après timeout
  }
}

// --- initializeDataStructures (MODIFIÉ) ---
void initializeDataStructures() {
  gpsData.lat = 0.0; gpsData.lon = 0.0; gpsData.alt = 0.0;
  gpsData.satellites = 0; gpsData.time = "--:--:--";
  gpsData.date = "--/--/----"; // <-- AJOUTÉ: Initialisation Date
  gpsData.hasGPSFix = false;

  envData.temperature = NAN; envData.pressure = NAN; envData.humidity = NAN; // Pression NAN
  envData.altitude = NAN; envData.valid = false;

  airData.airQuality = 0; airData.tvoc = 0; airData.eCO2 = 0; airData.valid = false;

  otherData.ozone = -1; otherData.uvIndex = -1.0;
  otherData.ozValid = false; otherData.uvValid = false;
}

// --- parseReceivedData (MODIFIÉ pour correspondre au Sender v2) ---
void parseReceivedData(String data) {
  // Réinitialiser les flags/valeurs avant chaque parsing pour éviter d'afficher d'anciennes données si une section manque
  initializeDataStructures();

  Serial.println("  Parsing data: " + data);

  // 1. Trouver les marqueurs principaux (début de chaque section)
  int gpsIndex = data.indexOf("GPS,");
  int envIndex = data.indexOf("|ENV,");
  int airIndex = data.indexOf("|AIR,");
  int ozIndex = data.indexOf("|OZ,");
  int uvIndex = data.indexOf("|UV,");

  // Vérifier si la structure minimale attendue est là (GPS au début, puis ENV)
  if (gpsIndex != 0 || envIndex == -1) {
      Serial.println("  ERREUR PARSING: Structure message invalide (GPS, ou |ENV, manquant/mal place).");
      return; // Ne pas continuer si la structure de base est cassée
  }

  // 2. Parser la section GPS (entre "GPS," et "|ENV,")
  String gpsSection = data.substring(gpsIndex + 4, envIndex);
  if (gpsSection == "NO_FIX" || gpsSection == "") {
    gpsData.hasGPSFix = false;
    Serial.println("    GPS: NO_FIX recu.");
  } else {
    // Essayer de parser les données GPS (lat,lon,alt,sat,time,date)
    int c1 = gpsSection.indexOf(','); // après lat
    int c2 = gpsSection.indexOf(',', c1 + 1); // après lon
    int c3 = gpsSection.indexOf(',', c2 + 1); // après alt
    int c4 = gpsSection.indexOf(',', c3 + 1); // après sat
    int c5 = gpsSection.indexOf(',', c4 + 1); // après time

    if (c1 != -1 && c2 != -1 && c3 != -1 && c4 != -1 && c5 != -1) {
      gpsData.lat = gpsSection.substring(0, c1).toFloat();
      gpsData.lon = gpsSection.substring(c1 + 1, c2).toFloat();
      gpsData.alt = gpsSection.substring(c2 + 1, c3).toFloat();
      gpsData.satellites = gpsSection.substring(c3 + 1, c4).toInt();
      gpsData.time = gpsSection.substring(c4 + 1, c5); // Extrait l'heure
      gpsData.date = gpsSection.substring(c5 + 1);    // Extrait la date (le reste)

      // Vérifications de validité simples
      if (gpsData.time == "NO_TIME") gpsData.time = "--:--:--";
      if (gpsData.date == "NO_DATE") gpsData.date = "--/--/----";

      // Considérer le fix comme valide si lat ou lon n'est pas exactement 0.0
      if (gpsData.lat != 0.0 || gpsData.lon != 0.0) {
           gpsData.hasGPSFix = true;
           Serial.println("    GPS: Donnees valides parsees.");
      } else {
          gpsData.hasGPSFix = false; // Considérer 0,0 comme invalide ou non acquis
          Serial.println("    GPS: Donnees parsees mais semblent invalides (Lat/Lon = 0).");
      }

    } else {
      gpsData.hasGPSFix = false; // Format incorrect dans la section GPS
      Serial.println("    GPS: ERREUR PARSING section (mauvais format/virgules manquantes). Section=" + gpsSection);
    }
  }

  // 3. Parser la section ENV (entre "|ENV," et la section suivante ou la fin)
  if (envIndex != -1) {
    int endEnv = (airIndex != -1) ? airIndex : data.length(); // Fin de la section ENV
    String envSection = data.substring(envIndex + 5, endEnv);
    if (envSection.indexOf("ERR") == -1 && envSection.length() > 5) { // Vérif simple de longueur
        int c1 = envSection.indexOf(',');         // après temp
        int c2 = envSection.indexOf(',', c1 + 1); // après pression (Pa) <-- Commentaire corrigé
        int c3 = envSection.indexOf(',', c2 + 1); // après humidité (%)   <-- Commentaire corrigé
        if (c1 != -1 && c2 != -1 && c3 != -1) {
            envData.temperature = envSection.substring(0, c1).toFloat();
            // --- CORRECTION DE L'ORDRE D'ASSIGNATION ---
            envData.pressure = envSection.substring(c1 + 1, c2).toFloat(); // <-- Pression (Pa) est à l'index 1
            envData.humidity = envSection.substring(c2 + 1, c3).toFloat(); // <-- Humidité (%) est à l'index 2
            // ------------------------------------------
            envData.altitude = envSection.substring(c3 + 1).toFloat();     // Altitude barométrique (index 3)
            envData.valid = true; // Marquer comme valide seulement si tout est parsé
            Serial.println("    ENV: Donnees valides parsees.");
        } else { Serial.println("    ENV: ERREUR PARSING section (virgules). Section=" + envSection); }
    } else { Serial.println("    ENV: ERR recu ou section vide."); }
  }

  // 4. Parser la section AIR (entre "|AIR," et la section suivante ou la fin)
  if (airIndex != -1) {
      int endAir = (ozIndex != -1) ? ozIndex : data.length();
      String airSection = data.substring(airIndex + 5, endAir);
       if (airSection.indexOf("ERR") == -1 && airSection.length() > 3) {
          int c1 = airSection.indexOf(','); // après AQI
          int c2 = airSection.indexOf(',', c1 + 1); // après TVOC
          if (c1 != -1 && c2 != -1) {
              airData.airQuality = airSection.substring(0, c1).toInt();
              airData.tvoc = airSection.substring(c1 + 1, c2).toInt();
              airData.eCO2 = airSection.substring(c2 + 1).toInt();
              airData.valid = true;
              Serial.println("    AIR: Donnees valides parsees.");
          } else { Serial.println("    AIR: ERREUR PARSING section (virgules). Section=" + airSection); }
      } else { Serial.println("    AIR: ERR recu ou section vide."); }
  }

  // 5. Parser la section Ozone (entre "|OZ," et la section suivante ou la fin)
  if (ozIndex != -1) {
      int endOz = (uvIndex != -1) ? uvIndex : data.length();
      String ozSection = data.substring(ozIndex + 4, endOz);
      if (ozSection != "ERR" && ozSection.length() > 0) {
          otherData.ozone = ozSection.toInt();
          // Ajouter une vérification si la conversion a réussi (optionnel mais robuste)
          // if (String(otherData.ozone) == ozSection) {
               otherData.ozValid = true;
               Serial.println("    OZ: Donnee valide parsee.");
          // } else { Serial.println("    OZ: ERREUR PARSING section (conversion int). Section=" + ozSection); }
      } else { Serial.println("    OZ: ERR recu ou section vide."); }
  }

  // 6. Parser la section UV (après "|UV,")
  if (uvIndex != -1) {
      String uvSection = data.substring(uvIndex + 4);
      if (uvSection != "ERR" && uvSection.length() > 0) {
          otherData.uvIndex = uvSection.toFloat();
          // Vérifier si la valeur n'est pas négative (le sender envoie -1.0 pour erreur)
          if (otherData.uvIndex >= 0.0) {
              otherData.uvValid = true;
              Serial.println("    UV: Donnee valide parsee.");
          } else { Serial.println("    UV: Donnee recue mais invalide (<0)."); }
      } else { Serial.println("    UV: ERR recu ou section vide."); }
  }
  Serial.println("  --- Fin Parsing ---");
}


// --- updateDisplay (MODIFIÉ pour Pression en hPa) ---
void updateDisplay() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Ligne 1: Statut GPS
  if (gpsData.hasGPSFix) {
    display.printf("GPS:%d FIX", gpsData.satellites); // Raccourci pour espace
    if(gpsData.time != "--:--:--") { // Afficher l'heure si dispo
        display.print(" "); display.print(gpsData.time);
    }
    display.println("");
  } else {
    display.println("GPS: No Fix");
  }

  // Ligne 2: Lat / Lon (si fix) ou T/H
  if (gpsData.hasGPSFix) {
    display.printf("L:%.4f Lo:%.4f\n", gpsData.lat, gpsData.lon);
  } else {
    // Si pas de GPS, afficher T/H ici
    display.print("T:");
    if (envData.valid) display.printf("%.1fC", envData.temperature); else display.print("N/A");
    display.print(" H:");
    if (envData.valid) display.printf("%.0f%%", envData.humidity); else display.print("N/A");
    display.println(); // NL
  }

  // Ligne 3: Alt GPS/Baro (si fix) ou P/AltBaro
   if (gpsData.hasGPSFix) {
       display.print("A_GPS:"); display.printf("%.0fm", gpsData.alt);
       display.print(" A_B:");
       if(envData.valid && !isnan(envData.altitude)) display.printf("%.0fm", envData.altitude); else display.print("N/A");
       display.println(); // NL
   } else {
        display.print("P:");
        // Afficher pression en hPa (diviser la valeur reçue en Pa par 100)
        if (envData.valid && !isnan(envData.pressure)) display.printf("%.0fhPa", envData.pressure / 100.0); else display.print("N/A");
        display.print(" A:");
        if (envData.valid && !isnan(envData.altitude)) display.printf("%.0fm", envData.altitude); else display.print("N/A");
        display.println(); // NL
   }

  // Ligne 4: AQI / CO2e
  display.print("AQI:");
  if (airData.valid) display.print(airData.airQuality); else display.print("N/A");
  display.print(" CO2:");
  if (airData.valid) display.print(airData.eCO2); else display.print("N/A");
  // display.println("ppm"); // Manque de place

  // Ligne 5: Ozone / UV
  display.print(" O3:");
  if (otherData.ozValid) display.print(otherData.ozone); else display.print("N/A");
  // display.print("ppb");
  display.print(" UV:");
  if (otherData.uvValid) display.printf("%.1f", otherData.uvIndex); else display.print("N/A");
  display.println(""); // NL

  // Ligne 6: RSSI / Temps écoulé
  display.print("RSSI:"); display.print(LoRa.packetRssi());
  if(lastReceiveTime > 0) {
      long secondsAgo = (millis() - lastReceiveTime) / 1000;
      display.print(" ("); display.print(secondsAgo); display.print("s)");
  }

  display.display();
}

// --- printDataToSerial (MODIFIÉ pour Pression et Date) ---
void printDataToSerial(String rawData) {
  Serial.println("\n--- DONNEES RECUES ---");
  Serial.println("  Donnees brutes: " + rawData);
  Serial.print("  RSSI: "); Serial.print(LoRa.packetRssi());
  Serial.print(" | SNR: "); Serial.println(LoRa.packetSnr());

  Serial.println("\n-- DONNEES TRAITEES --");

  if (gpsData.hasGPSFix) {
    Serial.println("  GPS (Fix OK):");
    Serial.println("    Latitude: " + String(gpsData.lat, 6));
    Serial.println("    Longitude: " + String(gpsData.lon, 6));
    Serial.println("    Altitude GPS: " + String(gpsData.alt, 1) + " m");
    Serial.println("    Satellites: " + String(gpsData.satellites));
    Serial.println("    Heure UTC: " + gpsData.time); // <-- Heure
    Serial.println("    Date UTC: " + gpsData.date);    // <-- Date
  } else {
    Serial.println("  GPS: Pas de Fix Valide Recu");
  }

  Serial.println("  Environnement:");
  if (envData.valid) {
    Serial.println("    Temperature: " + String(envData.temperature, 1) + " C");
    // Afficher pression en hPa (reçue en Pa)
    Serial.println("    Pression: " + String(envData.pressure / 100.0, 1) + " hPa (" + String(envData.pressure, 0) + " Pa)");
    Serial.println("    Humidite: " + String(envData.humidity, 1) + " %");
    Serial.println("    Altitude baro: " + String(envData.altitude, 1) + " m");
  } else { Serial.println("    Donnees ENV non valides/recues."); }

  Serial.println("  Qualite de l'air:");
   if (airData.valid) {
    Serial.print("    Indice Qualite Air (AQI): " + String(airData.airQuality) + " (");
    switch (airData.airQuality) { // Ajouter interprétation AQI
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
  } else { Serial.println("    Donnees AIR non valides/recues."); }

  Serial.println("  Autres donnees:");
   if (otherData.ozValid) { Serial.println("    Ozone: " + String(otherData.ozone) + " ppb"); }
   else { Serial.println("    Donnees Ozone non valides/recues."); }
   if (otherData.uvValid) { Serial.println("    Indice UV: " + String(otherData.uvIndex, 1)); }
   else { Serial.println("    Donnees UV non valides/recues."); }

  Serial.println("------------------------");
}