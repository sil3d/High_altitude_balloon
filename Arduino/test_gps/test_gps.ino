#include <Arduino.h>
#include <TinyGPS++.h>
#include <HardwareSerial.h>

// --- Configuration GPS (Pins Corrigés) ---
#define GPS_RX_PIN 34 // Pin RX de l'ESP32 (connecté au TX du GPS)
#define GPS_TX_PIN 12 // Pin TX de l'ESP32 (connecté au RX du GPS)
#define GPS_BAUD_RATE 9600

// --- Configuration Buzzer ---
#define BUZZER_PIN 14           // GPIO où le buzzer passif est connecté
#define BUZZER_FREQUENCY 2000   // Fréquence du son en Hz
#define BLINK_ON_DURATION_MS 200  // Durée du son (ms)
#define BLINK_OFF_DURATION_MS 800 // Durée du silence (ms)

// Définir le port série matériel pour le GPS (Serial2 sur ESP32)
HardwareSerial gpsSerial(2);

// Créer un objet TinyGPSPlus
TinyGPSPlus gps;

// Variables pour l'affichage temporisé
unsigned long lastDisplayTime = 0;
const unsigned long DISPLAY_INTERVAL_MS = 1000; // Afficher les infos toutes les 1000 ms

// Variables pour le clignotement du buzzer
bool hasFix = false;          // Le GPS a-t-il un fix valide ?
bool isBuzzerCurrentlyOn = false; // Le buzzer est-il actuellement dans sa phase ON ?
unsigned long lastBlinkToggleTime = 0; // Dernier moment où le buzzer a changé d'état

void setup() {
  // Initialiser le port série pour le débogage
  Serial.begin(115200);
  while (!Serial);
  Serial.println("Test GPS TTGO T-Beam V1.2 (Buzzer Clignotant)");
  Serial.println("---------------------------------------------");
  Serial.print("Buzzer (passif) sur GPIO: "); Serial.println(BUZZER_PIN);
  Serial.print("Fréquence: "); Serial.print(BUZZER_FREQUENCY); Serial.println(" Hz");
  Serial.print("Clignotement: "); Serial.print(BLINK_ON_DURATION_MS); Serial.print("ms ON / ");
  Serial.print(BLINK_OFF_DURATION_MS); Serial.println("ms OFF");


  // Configurer la pin du buzzer en sortie et l'éteindre
  pinMode(BUZZER_PIN, OUTPUT);
  noTone(BUZZER_PIN); // Assurer silence au démarrage

  // Initialiser le port série matériel pour le GPS
  gpsSerial.begin(GPS_BAUD_RATE, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

  Serial.print("Initialisation Serial2 GPS: ESP32_RX="); Serial.print(GPS_RX_PIN);
  Serial.print(", ESP32_TX="); Serial.print(GPS_TX_PIN);
  Serial.print(" @ "); Serial.print(GPS_BAUD_RATE); Serial.println(" bauds.");

  Serial.println("En attente des données GPS...");
  Serial.println("Le buzzer clignotera sur GPIO 14 lors d'un fix valide.");
}

void loop() {
  // --- Lecture GPS ---
  while (gpsSerial.available() > 0) {
    if (gps.encode(gpsSerial.read())) {
      // Nouvelle phrase NMEA reçue, vérifier le fix et afficher
      checkFixAndDisplayInfo();
    }
  }

  // --- Affichage Périodique ---
  // Appeler l'affichage même sans nouvelle phrase pour voir les mises à jour (heure, etc.)
  if (millis() - lastDisplayTime >= DISPLAY_INTERVAL_MS) {
     checkFixAndDisplayInfo(); // Vérifie aussi le fix à chaque affichage
  }

  // --- Gestion du Clignotement du Buzzer ---
  handleBuzzerBlink();
}

// Fonction pour vérifier le fix, mettre à jour l'état et afficher les infos
void checkFixAndDisplayInfo() {
   lastDisplayTime = millis(); // Mettre à jour le temps du dernier affichage

   bool currentLocationValid = gps.location.isValid();

   // --- Mise à jour de l'état du Fix (pour le buzzer) ---
   if (currentLocationValid && !hasFix) {
     // On vient d'obtenir un fix !
     Serial.println("\n*** FIX GPS OBTENU ! Activation clignotement Buzzer ***");
     hasFix = true;
     // On n'active pas le buzzer ici directement, handleBuzzerBlink() s'en chargera
     // On réinitialise le timer de clignotement pour commencer par une phase ON (ou OFF selon la logique)
     lastBlinkToggleTime = millis();
     isBuzzerCurrentlyOn = false; // Commence par être OFF, pour que le premier cycle l'allume
   } else if (!currentLocationValid && hasFix) {
     // On vient de perdre le fix !
     Serial.println("\n*** FIX GPS PERDU ! Arrêt Buzzer ***");
     hasFix = false;
     noTone(BUZZER_PIN); // Arrêter immédiatement le son
     isBuzzerCurrentlyOn = false; // Réinitialiser l'état
   }

   // --- Affichage des informations ---
   // (Le code d'affichage reste identique à la version précédente)
   Serial.print(F("---------------------\n"));
   Serial.print(F("Heure UTC: "));
   if (gps.time.isValid()) { if (gps.time.hour() < 10) Serial.print(F("0")); Serial.print(gps.time.hour()); Serial.print(F(":")); if (gps.time.minute() < 10) Serial.print(F("0")); Serial.print(gps.time.minute()); Serial.print(F(":")); if (gps.time.second() < 10) Serial.print(F("0")); Serial.print(gps.time.second()); } else { Serial.print(F("INVALID")); } Serial.println();
   Serial.print(F("Date UTC:  "));
   if (gps.date.isValid()) { Serial.print(gps.date.day()); Serial.print(F("/")); Serial.print(gps.date.month()); Serial.print(F("/")); Serial.print(gps.date.year()); } else { Serial.print(F("INVALID")); } Serial.println();
   Serial.print(F("Position:  "));
   if (currentLocationValid) { Serial.print(gps.location.lat(), 6); Serial.print(F(", ")); Serial.print(gps.location.lng(), 6); } else { Serial.print(F("INVALID (Waiting for fix...)")); } Serial.println();
   Serial.print(F("Altitude:  "));
   if (gps.altitude.isValid()) { Serial.print(gps.altitude.meters()); Serial.print(" m"); } else { Serial.print(F("INVALID")); } Serial.println();
   Serial.print(F("Satellites: "));
   if (gps.satellites.isValid()) { Serial.print(gps.satellites.value()); } else { Serial.print(F("INVALID")); } Serial.println();
   Serial.print(F("Vitesse:    "));
   if (gps.speed.isValid()) { Serial.print(gps.speed.kmph()); Serial.print(" km/h"); } else { Serial.print(F("INVALID")); } Serial.println();
   Serial.print(F("---------------------\n"));
}

// Fonction séparée pour gérer le clignotement du buzzer
void handleBuzzerBlink() {
  // Ne faire clignoter que si on a un fix GPS
  if (!hasFix) {
    // Si on n'a pas de fix, on s'assure que le buzzer est éteint (au cas où)
    if (isBuzzerCurrentlyOn) {
        noTone(BUZZER_PIN);
        isBuzzerCurrentlyOn = false;
    }
    return; // Sortir de la fonction si pas de fix
  }

  // On a un fix, gérer le clignotement basé sur millis()
  unsigned long currentMillis = millis();

  if (isBuzzerCurrentlyOn) {
    // Le buzzer est ON, vérifier s'il faut l'éteindre
    if (currentMillis - lastBlinkToggleTime >= BLINK_ON_DURATION_MS) {
      noTone(BUZZER_PIN);             // Éteindre le buzzer
      isBuzzerCurrentlyOn = false;     // Mettre à jour l'état
      lastBlinkToggleTime = currentMillis; // Enregistrer le moment du changement
    }
  } else {
    // Le buzzer est OFF, vérifier s'il faut l'allumer
    if (currentMillis - lastBlinkToggleTime >= BLINK_OFF_DURATION_MS) {
      tone(BUZZER_PIN, BUZZER_FREQUENCY); // Allumer le buzzer
      isBuzzerCurrentlyOn = true;       // Mettre à jour l'état
      lastBlinkToggleTime = currentMillis; // Enregistrer le moment du changement
    }
  }
}