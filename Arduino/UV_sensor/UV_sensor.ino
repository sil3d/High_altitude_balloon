//UV SENSOR for this code i don't know why, but i don't use 15 in real life it can retrieve data for 3.3V and GND only 
//on the T-display lora32 LILYGO

#define UV_SENSOR_PIN 32  // Pin ADC de l'ESP32 connecté à SIG du capteur

void setup() {
    Serial.begin(115200);  // Initialisation de la communication série
    delay(1000);           // Pause pour stabiliser le capteur
    analogReadResolution(12);  // ESP32 : Résolution ADC de 12 bits (0 - 4095)
    pinMode(UV_SENSOR_PIN, INPUT);  // Définir le pin comme entrée
}

void loop() {
    int rawValue = analogRead(UV_SENSOR_PIN);  // Lecture de la valeur brute ADC

    if (rawValue > 0) {
        float voltage = rawValue * (3.3 / 4095.0);  // Conversion en tension (0V - 3.3V)
        float uvIndex = voltage / 0.1;  // Approximation de l'indice UV (0.1V ≈ 1 UV)

        Serial.print("Valeur ADC : ");
        Serial.print(rawValue);
        Serial.print(" | Tension : ");
        Serial.print(voltage, 2);
        Serial.print("V | Indice UV estimé : ");
        Serial.println(uvIndex, 2);
    } else {
        Serial.println("Erreur : aucune donnée reçue du capteur !");
    }

    delay(1000);  // Attendre 1 seconde avant la prochaine lecture
}
//UV SENSOR