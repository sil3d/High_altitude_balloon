#include <SPI.h>
#include <LoRa.h>

// --- CONFIGURATION LoRa POUR VOTRE CARTE RECEPTRICE ---
// !! ATTENTION !! Mettez ici les pins corrects pour VOTRE carte réceptrice !!
// Exemple pour TTGO LoRa32 V2.1 (Adaptez si nécessaire !)
#define LORA_SCK 5
#define LORA_MISO 19
#define LORA_MOSI 27
#define LORA_CS 18
#define LORA_RST 23 // Souvent 14 ou 23 sur les cartes TTGO
#define LORA_IRQ 26 // Souvent appelé DIO0

// Fréquence LoRa (Doit correspondre à l'émetteur !)
#define LORA_FREQUENCY 868E6

// Optionnel : Pour afficher sur un écran OLED si votre récepteur en a un
 #include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
bool hasDisplay = false;

void setup() {
  Serial.begin(115200);
  while (!Serial);
  Serial.println("Initialisation Récepteur LoRa...");

  // --- Initialisation OLED (si présent) ---
  Wire.begin(); // Pour TTGO LoRa32: SDA=21, SCL=22
  if (display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
     hasDisplay = true;
     display.clearDisplay();
     display.setTextSize(1);
     display.setTextColor(SSD1306_WHITE);
     display.setCursor(0, 0);
     display.println("Recepteur LoRa");
     display.display();
     Serial.println("OLED Initialisé.");
   } else {
     Serial.println("Echec initialisation OLED.");
   }

  // --- Initialisation LoRa ---
  Serial.println("Configuration LoRa...");
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_IRQ);

  if (!LoRa.begin(LORA_FREQUENCY)) {
    Serial.println("Echec démarrage LoRa ! Vérifiez les connexions et les pins.");
    // if(hasDisplay) { display.println("LoRa ECHEC!"); display.display(); }
    while (1);
  }
  // LoRa.setSpreadingFactor(7); // Assurez-vous que ça correspond à l'émetteur si vous l'avez changé
  // LoRa.setSignalBandwidth(125E3); // Idem
  LoRa.enableCrc(); // Recommandé
  LoRa.receive(); // Mettre en mode réception active

  Serial.print("Récepteur LoRa démarré sur ");
  Serial.print(LORA_FREQUENCY / 1E6);
  Serial.println(" MHz.");
  // if(hasDisplay) { display.println("Pret a recevoir..."); display.display(); }
}

// ... (le reste de ton code setup et début de loop) ...

void loop() {
  int packetSize = LoRa.parsePacket();

  if (packetSize > 0) {
    String receivedText = "";
    while (LoRa.available()) {
      receivedText += (char)LoRa.read();
    }

    float rssi = LoRa.packetRssi();
    float snr = LoRa.packetSnr();

    // Afficher les informations sur le moniteur série (pour le débogage)
    Serial.println("-------------------------");
    Serial.print("Paquet Reçu (LoRa): '");
    Serial.print(receivedText);
    Serial.println("'");
    Serial.print("Taille: ");
    Serial.println(packetSize);
    Serial.print("RSSI: ");
    Serial.print(rssi);
    Serial.println(" dBm");
    Serial.print("SNR: ");
    Serial.print(snr);
    Serial.println(" dB");
    Serial.println("-------------------------");

    // >>> TRANSMETTRE UNIQUEMENT LES DONNÉES GPS À PYTHON <<<
    // On suppose ici que 'receivedText' contient "latitude,longitude"
    // Python lira cette ligne spécifique.
    Serial.println(receivedText);
    // >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>

    // Optionnel: Afficher sur l'OLED
    if(hasDisplay) {
       display.clearDisplay();
       display.setCursor(0,0);
       display.println("Paquet Recu:");
       // Affiche la donnée brute reçue, peut-être les coordonnées
       display.println(receivedText.substring(0, 20));
       display.printf("RSSI: %.1f dBm\n", rssi);
       display.printf("SNR:  %.1f dB\n", snr);
       display.display();
     }
     // Remettre en mode réception pour le prochain paquet
     LoRa.receive();
  }
   delay(10); // Petite pause
}