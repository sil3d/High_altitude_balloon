#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "DFRobot_OzoneSensor.h"

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define SCREEN_ADDRESS 0x3C
#define COLLECT_NUMBER   20            // collect number, the collection range is 1-100
#define Ozone_IICAddress OZONE_ADDRESS_3

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
DFRobot_OzoneSensor Ozone;

void setup() {
  Serial.begin(9600);

  // Initialize the display

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("Échec de l'initialisation de l'OLED !");
    while (1);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  while(!Ozone.begin(Ozone_IICAddress)) {
    Serial.println("I2c device number error !");
    delay(1000);
  }
  Serial.println("I2c connect success !");

  Ozone.setModes(MEASURE_MODE_PASSIVE);
}

void loop() {
  int16_t ozoneConcentration = Ozone.readOzoneData(COLLECT_NUMBER);
  Serial.print("Ozone concentration is ");
  Serial.print(ozoneConcentration);
  Serial.println(" PPB.");

  // Display the ozone concentration on the OLED
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("Ozone: ");
  display.print(ozoneConcentration);
  display.println(" PPB");
  display.display();

  delay(1000);
}
