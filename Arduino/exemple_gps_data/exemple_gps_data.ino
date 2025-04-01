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
#define LORA_RST 14
#define LORA_IRQ 26

const int blueLED = 4;
int pktCount = 0;

#define BUTTON_PIN 39

void setup() {
  Serial.begin(115200);
  Serial1.begin(9600, SERIAL_8N1, 34, 12); // TX = 34, RX = 12 pour GPS

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("Échec de l'initialisation de l'OLED !");
    while (1);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_IRQ);

  pinMode(blueLED, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  if (!LoRa.begin(868E6)) {
    Serial.println("Starting LoRa failed!");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("LoRa failed!");
    display.display();
    while (1);
  }
  LoRa.setSpreadingFactor(7);
  LoRa.setTxPower(14, PA_OUTPUT_PA_BOOST_PIN);
  remote_clear_display();
}

void remote_clear_display() {
  LoRa.beginPacket();
  pktCount++;
  LoRa.print("clearDisp");
  LoRa.endPacket();
}

void print_info_json() {
  Serial.print("{\'Valid\': \'");
  Serial.print(gps.location.isValid());
  Serial.print("\', \'Lat\': \'");
  Serial.print(gps.location.lat(), 5);
  Serial.print("\', \'Long\': \'");
  Serial.print(gps.location.lng(), 4);
  Serial.print("\', \'Satellites\': \'");
  Serial.print(gps.satellites.value());
  Serial.print("\', \'Altitude\': \'");
  Serial.print(gps.altitude.meters());
  Serial.print("\', \'Time\': \'");
  Serial.printf("%.2d:%.2d:%.2d", gps.time.hour(), gps.time.minute(), gps.time.second());
  Serial.print("\', \'Button state\': \'");
  Serial.print(digitalRead(BUTTON_PIN) == LOW ? "Pressed" : "Released");
  Serial.println("\'}");
}

void display_gps_info() {
  display.clearDisplay();
  display.setCursor(0, 0);

  if (gps.location.isValid()) {
    display.print("Satellites: ");
    display.println(gps.satellites.value());
    display.print("Lat: ");
    display.println(gps.location.lat(), 4);
    display.print("Long: ");
    display.println(gps.location.lng(), 4);
    display.print("Alt: ");
    display.print(gps.altitude.meters()); // Altitude en mètres
    display.println(" m");
    display.print("Time: ");
    display.printf("%.2d:%.2d:%.2d", gps.time.hour(), gps.time.minute(), gps.time.second());
  } else {
    display.println("Searching for GPS...");
    display.print("Satellites: ");
    display.println(gps.satellites.value());
  }

  display.display();
}

void loop() {
  // Gestion du bouton
  if (digitalRead(BUTTON_PIN) == LOW) {
    Serial.println("Bouton pressé !");
    remote_clear_display();
    pktCount = 0;
    delay(500); // Délai pour éviter les rebonds
  }

  // Lecture des données GPS
  while (Serial1.available()) {
    char c = Serial1.read();
    Serial.write(c);
    gps.encode(c);
  }

  print_info_json();
  display_gps_info();

  if (gps.location.isValid()) {
    digitalWrite(blueLED, HIGH);
    LoRa.beginPacket();
    LoRa.printf("Time: %.2d:%.2d:%.2d\n", gps.time.hour(), gps.time.minute(), gps.time.second());
    LoRa.print("Lat: ");
    LoRa.print(gps.location.lat(), 4);
    LoRa.print(" Long: ");
    LoRa.print(gps.location.lng(), 4);
    LoRa.endPacket();
    digitalWrite(blueLED, LOW);
  } else {
    LoRa.beginPacket();
    LoRa.print("Error: Invalid GPS data");
    LoRa.endPacket();
  }

  smartDelay(1000); // Mise à jour toutes les 1 seconde
}

static void smartDelay(unsigned long ms) {
  unsigned long start = millis();
  do {
    while (Serial1.available())
      gps.encode(Serial1.read());
  } while (millis() - start < ms);
}