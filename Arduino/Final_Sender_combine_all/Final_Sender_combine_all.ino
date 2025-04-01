#include <SPI.h>
#include <LoRa.h>
#include <TinyGPSPlus.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DFRobot_ENS160.h>
#include "DFRobot_BME280.h"
#include "DFRobot_OzoneSensor.h"

// Définir les constantes et objets
TinyGPSPlus gps;
DFRobot_ENS160_I2C ENS160(&Wire, 0x53);
DFRobot_BME280_IIC bme(&Wire, 0x77);
DFRobot_OzoneSensor Ozone;

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
#define SEA_LEVEL_PRESSURE 1015.0f
#define UV_SENSOR_PIN 15
#define Ozone_IICAddress OZONE_ADDRESS_3
#define COLLECT_NUMBER 20

Adafruit_SSD1606 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
const int blueLED = 4;

void setup() {
  Serial.begin(115200);
  Serial1.begin(9600, SERIAL_8N1, 34, 12); // GPS

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

  pinMode(blueLED, OUTPUT);
  pinMode(UV_SENSOR_PIN, INPUT);
  analogReadResolution(12);

  // Initialisation des capteurs environnementaux
  bme.reset();
  while (bme.begin() != BME::eStatusOK) {
    Serial.println("BME begin failed");
    delay(2000);
  }
  while (ENS160.begin() != NO_ERR) {
    Serial.println("ENS160 begin failed");
    delay(3000);
  }
  ENS160.setPWRMode(ENS160_STANDARD_MODE);
  ENS160.setTempAndHum(bme.getTemperature(), bme.getHumidity());

  while (!Ozone.begin(Ozone_IICAddress)) {
    Serial.println("Ozone sensor begin failed");
    delay(1000);
  }
  Ozone.setModes(MEASURE_MODE_PASSIVE);
}

void loop() {
  // Lecture des données GPS
  while (Serial1.available()) {
    char c = Serial1.read();
    gps.encode(c);
  }

  // Lecture des capteurs environnementaux
  float temp = bme.getTemperature();
  float humi = bme.getHumidity();
  float alti = bme.calAltitude(SEA_LEVEL_PRESSURE, bme.getPressure());
  uint8_t AQI = ENS160.getAQI();
  uint16_t TVOC = ENS160.getTVOC();
  uint16_t ECO2 = ENS160.getECO2();
  int16_t ozoneConcentration = Ozone.readOzoneData(COLLECT_NUMBER);
  int rawValue = analogRead(UV_SENSOR_PIN);
  float voltage = rawValue * (3.3 / 4095.0);
  float uvIndex = voltage / 0.1;

  // Envoi des données via LoRa
  if (gps.location.isValid()) {
    digitalWrite(blueLED, HIGH);

    LoRa.beginPacket();
    LoRa.print("Lat: "); LoRa.print(gps.location.lat(), 6);
    LoRa.print(", Lng: "); LoRa.print(gps.location.lng(), 6);
    LoRa.print(", Alt: "); LoRa.print(alti);
    LoRa.print(" m, Sats: "); LoRa.print(gps.satellites.value());
    LoRa.print(", Temp: "); LoRa.print(temp);
    LoRa.print(" C, Hum: "); LoRa.print(humi);
    LoRa.print(" %, AQI: "); LoRa.print(AQI);
    LoRa.print(", TVOC: "); LoRa.print(TVOC);
    LoRa.print(" ppb, ECO2: "); LoRa.print(ECO2);
    LoRa.print(" ppm, O3: "); LoRa.print(ozoneConcentration);
    LoRa.print(" PPB, UV: "); LoRa.print(uvIndex);
    LoRa.endPacket();

    digitalWrite(blueLED, LOW);
  }

  // Affichage des données sur l'OLED
  display.clearDisplay();
  display.setCursor(0, 0);
  if (gps.location.isValid()) {
    display.print("Lat: "); display.println(gps.location.lat(), 6);
    display.print("Lng: "); display.println(gps.location.lng(), 6);
    display.print("Alt: "); display.print(alti); display.println(" m");
    display.print("Sats: "); display.println(gps.satellites.value());
    display.print("Temp: "); display.print(temp); display.println(" C");
    display.print("Hum: "); display.print(humi); display.println(" %");
    display.print("AQI: "); display.println(AQI);
    display.print("O3: "); display.print(ozoneConcentration); display.println(" PPB");
    display.print("UV: "); display.println(uvIndex);
  } else {
    display.println("Searching for GPS...");
    display.print("Satellites: "); display.println(gps.satellites.value());
  }
  display.display();

  delay(5000);
}
