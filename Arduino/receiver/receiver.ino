#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define SCREEN_ADDRESS 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

#define LORA_SCK 5
#define LORA_MISO 19
#define LORA_MOSI 27
#define LORA_CS 18
#define LORA_RST 23
#define LORA_IRQ 26

void setup() {
  Serial.begin(115200);

  // Initialisation de l'OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("Échec de l'initialisation de l'OLED !");
    while (1);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  // Initialisation du module LoRa
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_IRQ);

  if (!LoRa.begin(868E6)) { // Fréquence LoRa adaptée à votre région
    Serial.println("Starting LoRa failed!");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("LoRa failed!");
    display.display();
    while (1);
  }
  LoRa.setSpreadingFactor(7); // Facteur d'étalement
  LoRa.setTxPower(14, PA_OUTPUT_PA_BOOST_PIN); // Puissance d'émission

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("waiting for data...");
  display.display();
}

void loop() {
  // Vérifier si des données LoRa sont disponibles
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    String receivedData = "";
    while (LoRa.available()) {
      receivedData += (char)LoRa.read();
    }

    // Afficher les données reçues sur l'OLED
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("RecivedData :");
    display.println(receivedData);
    display.display();

    // Afficher les données dans le moniteur série
    Serial.println("receivedData :");
    Serial.println(receivedData);
  }
}