#include <SPI.h>
#include <LoRa.h>
#include <TinyGPSPlus.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// --- Initialisation des objets ---
TinyGPSPlus gps;                      // Objet pour le parsing GPS
HardwareSerial& GpsSerial = Serial1;  // Alias pour Serial1 utilisé pour le GPS



// Configuration OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1      // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C // Adresse I2C pour OLED 128x64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Configuration LoRa pour TTGO T-Beam V1.2
#define LORA_SCK 5
#define LORA_MISO 19
#define LORA_MOSI 27
#define LORA_CS 18
#define LORA_RST 23   // Correction basée sur le diagramme T-Beam V1.2
#define LORA_IRQ 26   // Broche DIO0 (IRQ) LoRa

// Périphériques
const int blueLED = 4;    // Un GPIO disponible
const int buzzer = 14;    // <<< MODIFIÉ: Buzzer connecté au GPIO 14 (selon demande utilisateur)
int pktCount = 0;


// Bouton Utilisateur
#define BUTTON_PIN 39 // Bouton utilisateur (près du GPS)

void setup() {
  Serial.begin(115200);
  while (!Serial);
  Serial.println("Démarrage TTGO T-Beam V1.2...");
  Serial.println("--- ATTENTION ---");
  Serial.println("GPS Externe configuré sur RX=33, TX=25 (Connexion Manuelle)");
  Serial.println("Buzzer configuré sur Pin 14");
  Serial.println("LoRa RST configuré sur Pin 23 (Standard T-Beam V1.2)");
  Serial.println("-----------------");


  // Initialisation GPS Externe sur Serial1
  // <<< MODIFIÉ: Utilise les pins RX=33, TX=25 comme demandé
  GpsSerial.begin(9600, SERIAL_8N1, 33, 25);
  Serial.println("Serial1 (GPS Externe) initialisé sur Pins RX=33, TX=25");

  // Initialisation OLED
  Wire.begin(21, 22); // Initialise I2C sur les pins SDA=21, SCL=22
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("Echec initialisation SSD1306");
    while (1);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("OLED OK");
  display.display();
  delay(1000);

  // Initialisation LoRa
  Serial.println("Initialisation LoRa...");
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_IRQ); // Utilise LORA_RST=23

  if (!LoRa.begin(868E6)) { // Fréquence 868 MHz pour l'Europe
    Serial.println("Echec démarrage LoRa!");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("LoRa ECHEC!");
    display.display();
    while (1);
  }
  Serial.println("LoRa OK!");
  display.println("LoRa OK!");
  display.display();
  delay(1000);

  // Initialisation des broches des périphériques
  pinMode(blueLED, OUTPUT);
  pinMode(buzzer, OUTPUT);   // <<< Utilise le pin 14 pour le buzzer
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  remote_clear_display();
  Serial.println("Setup terminé. Entrée dans la boucle...");
}

// --- Le reste du code (loop, display_gps_info, print_info_json, remote_clear_display) ---
// --- reste identique à la version précédente. Assurez-vous de l'inclure.      ---

// Fonction pour envoyer une commande 'clear display' via LoRa
void remote_clear_display() {
  LoRa.beginPacket();
  pktCount++;
  LoRa.print("clearDisp");
  LoRa.endPacket();
  Serial.println("Commande 'clearDisp' envoyée via LoRa.");
}

// Fonction pour afficher les informations GPS sur le moniteur série en format JSON
void print_info_json() {
  Serial.print("{\'Valid\': \'");
  Serial.print(gps.location.isValid());
  Serial.print("\', \'Lat\': \'");
  Serial.print(gps.location.lat(), 6);
  Serial.print("\', \'Long\': \'");
  Serial.print(gps.location.lng(), 6);
  Serial.print("\', \'Satellites\': \'");
  Serial.print(gps.satellites.value());
  Serial.print("\', \'Altitude\': \'");
  Serial.print(gps.altitude.meters());
  Serial.print("\', \'Time\': \'");
  if (gps.time.isValid()) {
     Serial.printf("%02d:%02d:%02d", gps.time.hour(), gps.time.minute(), gps.time.second());
  } else {
     Serial.print("INVALID");
  }
  Serial.print("\', \'Button state\': \'");
  Serial.print(digitalRead(BUTTON_PIN) == LOW ? "Pressed" : "Released");
  Serial.println("\'}");
}

// Fonction pour afficher les informations GPS sur l'OLED
void display_gps_info() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  if (gps.location.isValid()) {
    display.print("Sats: ");
    display.println(gps.satellites.value());
    display.print("Lat: ");
    display.println(gps.location.lat(), 4);
    display.print("Lon: ");
    display.println(gps.location.lng(), 4);
    display.print("Alt: ");
    display.print(gps.altitude.meters(), 1);
    display.println(" m");

    if (gps.time.isValid()) {
        display.printf("UTC: %02d:%02d:%02d", gps.time.hour(), gps.time.minute(), gps.time.second());
    } else {
        display.print("Time: N/A");
    }

  } else {
    display.println("Recherche GPS...");
    display.print("Satellites Trouves: ");
    display.println(gps.satellites.value());
    display.println("Attente fix...");
  }

  display.display();
}


void loop() {
  // --- Gestion Bouton ---
  static unsigned long lastButtonPress = 0;
  static bool buttonState = HIGH;
  bool currentButtonState = digitalRead(BUTTON_PIN);

  if (currentButtonState == LOW && buttonState == HIGH) {
    if (millis() - lastButtonPress > 500) {
        Serial.println("Bouton Appuye !");
        remote_clear_display();
        pktCount = 0;
        lastButtonPress = millis();
    }
  }
  buttonState = currentButtonState;

  // --- Traitement Données GPS ---
  while (GpsSerial.available() > 0) {
    char c = GpsSerial.read();
    // Serial.write(c);
    gps.encode(c);
  }

  // --- Actions Périodiques (toutes les secondes environ) ---
  static unsigned long lastActionTime = 0;
  unsigned long currentTime = millis();

  if (currentTime - lastActionTime >= 1000) {
      lastActionTime = currentTime;

      // Afficher info GPS sur Moniteur Série
      print_info_json();

      // Mettre à jour l'écran OLED
      display_gps_info();

      // Transmettre Paquet LoRa si GPS valide
      if (gps.location.isValid()) {
          digitalWrite(blueLED, HIGH);
          LoRa.beginPacket();
          if(gps.time.isValid()){
              LoRa.printf("UTC:%02d:%02d:%02d ", gps.time.hour(), gps.time.minute(), gps.time.second());
          }
          LoRa.print("Lat:");
          LoRa.print(gps.location.lat(), 6);
          LoRa.print(" Lon:");
          LoRa.print(gps.location.lng(), 6);
          LoRa.endPacket();
          digitalWrite(blueLED, LOW);
          Serial.println("Paquet LoRa envoye (GPS valide).");

          // Emettre un son après transmission réussie (sur Pin 14)
          tone(buzzer, 1000, 200); // 1000Hz pour 200ms

      } else {
          Serial.println("Donnees GPS invalides. Pas de paquet LoRa envoye.");
      }
  }
}