//sender code for lilygo with t-display and lora

#include <SPI.h>
#include <LoRa.h>
#include <TinyGPSPlus.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

TinyGPSPlus gps;

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

const int blueLED = 4; // LED bleue pour indiquer l'envoi de données

void setup() {
  Serial.begin(115200);
  Serial1.begin(9600, SERIAL_8N1, 34, 12); // TX = 34, RX = 12 pour GPS

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

  pinMode(blueLED, OUTPUT);

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
}

void loop() {
  // Lecture des données GPS
  while (Serial1.available()) {
    char c = Serial1.read();
    gps.encode(c);
  }

  // Envoi des données GPS via LoRa
  if (gps.location.isValid()) {
    digitalWrite(blueLED, HIGH); // Allumer la LED bleue pendant l'envoi

    LoRa.beginPacket();
    LoRa.print("Lat: ");
    LoRa.print(gps.location.lat(), 6);
    LoRa.print(", Lng: ");
    LoRa.print(gps.location.lng(), 6);
    LoRa.print(", Alt: ");
    LoRa.print(gps.altitude.meters());
    LoRa.print(" m, Sats: ");
    LoRa.print(gps.satellites.value());
    LoRa.print(", Time: ");
    LoRa.printf("%.2d:%.2d:%.2d", gps.time.hour(), gps.time.minute(), gps.time.second());
    LoRa.endPacket();

    digitalWrite(blueLED, LOW); // Éteindre la LED bleue
  }

  // Affichage des données GPS sur l'OLED
  display.clearDisplay();
  display.setCursor(0, 0);
  if (gps.location.isValid()) {
    display.print("Lat: ");
    display.println(gps.location.lat(), 6);
    display.print("Lng: ");
    display.println(gps.location.lng(), 6);
    display.print("Alt: ");
    display.print(gps.altitude.meters());
    display.println(" m");
    display.print("Sats: ");
    display.println(gps.satellites.value());
    display.print("Time: ");
    display.printf("%.2d:%.2d:%.2d", gps.time.hour(), gps.time.minute(), gps.time.second());
  } else {
    display.println("Searching for GPS...");
    display.print("Satellites: ");
    display.println(gps.satellites.value());
  }
  display.display();

  delay(5000); // Envoyer des données toutes les 5 secondes
}

//sender code for lilygo with t-display and lora
