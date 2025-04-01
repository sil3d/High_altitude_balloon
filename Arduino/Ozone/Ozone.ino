///////ozone Gravity SEN0321
#include "DFRobot_OzoneSensor.h"

#define COLLECT_NUMBER   20            // collect number, the collection range is 1-100
#define Ozone_IICAddress OZONE_ADDRESS_3
/*   iic slave Address, The default is ADDRESS_3
       ADDRESS_0               0x70      // iic device address
       ADDRESS_1               0x71
       ADDRESS_2               0x72
       ADDRESS_3               0x73
*/
DFRobot_OzoneSensor Ozone;
void setup() 
{
  Serial.begin(115200);
  while(!Ozone.begin(Ozone_IICAddress)) {
    Serial.println("I2c device number error !");
    delay(1000);
  }  Serial.println("I2c connect success !");
/*   Set iic mode, active mode or passive mode
       MEASURE_MODE_AUTOMATIC            // active  mode
       MEASURE_MODE_PASSIVE              // passive mode
*/
    Ozone.setModes(MEASURE_MODE_PASSIVE);
}


void loop() 
{
/*   Smooth data collection
       COLLECT_NUMBER                    // The collection range is 1-100
*/
  int16_t ozoneConcentration = Ozone.readOzoneData(COLLECT_NUMBER);
  Serial.print("Ozone concentration is ");
  Serial.print(ozoneConcentration);
  Serial.println(" PPB.");
  delay(1000);
}
///////ozone Gravity SEN0321