/////////////code PMS5
#include <SoftwareSerial.h>

// Pins pour SoftwareSerial (RX=35, TX=15) - Assurez-vous que ces pins sont appropriées pour votre ESP32
// Note: Pin 35 est souvent input-only sur ESP32, ce qui est parfait pour RX.
// Note: TX n'est pas utilisé pour lire les données du PMS5003 en mode passif.
SoftwareSerial pmsSerial(15, 35); // RX, TX  // vert à 15 , 35 à bleu

// Structure pour contenir les données du PMS5003
struct pms5003data {
  uint16_t framelen;
  uint16_t pm10_standard, pm25_standard, pm100_standard;
  uint16_t pm10_env, pm25_env, pm100_env;
  uint16_t particles_03um, particles_05um, particles_10um, particles_25um, particles_50um, particles_100um;
  uint16_t unused;
  uint16_t checksum;
};

// Variable globale pour stocker les données lues
struct pms5003data data;

// --- Timeout pour la lecture série ---
// Durée maximale (en millisecondes) pour attendre une trame complète
#define PMS_READ_TIMEOUT 1500

void setup() {
  Serial.begin(115200);
  while (!Serial); // Attente de l'ouverture du port série (utile pour certaines cartes comme Leonardo, pas nécessaire pour ESP32 mais ne nuit pas)
  Serial.println("Initialisation du moniteur série terminée.");
  Serial.println("Initialisation du PMS5003...");

  // Initialise SoftwareSerial pour le PMS5003 à 9600 bauds
  pmsSerial.begin(9600);

  // Optionnel: Petit délai pour s'assurer que le capteur est prêt après la mise sous tension
  delay(1000);
  Serial.println("Setup terminé. Début de la lecture des données...");
}

void loop() {
  // Tente de lire les données du capteur
  if (readPMSdata(&pmsSerial)) {
    // Succès: Affiche les données
    Serial.println("Données lues avec succès");
    Serial.println();
    Serial.println("---------------------------------------");
    Serial.println("Unités de concentration (standard)");
    Serial.print("PM 1.0: "); Serial.print(data.pm10_standard);
    Serial.print("\t\tPM 2.5: "); Serial.print(data.pm25_standard);
    Serial.print("\t\tPM 10: "); Serial.println(data.pm100_standard);
    Serial.println("---------------------------------------");
    Serial.println("Unités de concentration (environnement)");
    Serial.print("PM 1.0: "); Serial.print(data.pm10_env);
    Serial.print("\t\tPM 2.5: "); Serial.print(data.pm25_env);
    Serial.print("\t\tPM 10: "); Serial.println(data.pm100_env);
    Serial.println("---------------------------------------");
    Serial.print("Particules > 0.3um / 0.1L air:"); Serial.println(data.particles_03um);
    Serial.print("Particules > 0.5um / 0.1L air:"); Serial.println(data.particles_05um);
    Serial.print("Particules > 1.0um / 0.1L air:"); Serial.println(data.particles_10um);
    Serial.print("Particules > 2.5um / 0.1L air:"); Serial.println(data.particles_25um);
    Serial.print("Particules > 5.0um / 0.1L air:"); Serial.println(data.particles_50um);
    Serial.print("Particules > 10.0 um / 0.1L air:"); Serial.println(data.particles_100um);
    Serial.println("---------------------------------------");
  } else {
    // Échec: Le message d'erreur spécifique est maintenant imprimé DANS readPMSdata
    // Serial.println("Échec de la lecture des données"); // Redondant si readPMSdata imprime déjà l'erreur
  }

  // Pause de 2 secondes entre les lectures
  delay(2000);
}

/**
 * @brief Tente de lire et de valider une trame de données du capteur PMS5003.
 * @param s Pointeur vers le flux série (par exemple, &pmsSerial).
 * @return boolean True si une trame valide a été lue, False sinon.
 */
boolean readPMSdata(Stream *s) {
  // --- Étape 1: Trouver le début de la trame (0x42 0x4D) ---
  byte startChar1 = 0, startChar2 = 0;
  unsigned long startMillis = millis(); // Pour le timeout global

  while (startChar1 != 0x42 || startChar2 != 0x4D) {
    // Vérifie le timeout global
    if (millis() - startMillis > PMS_READ_TIMEOUT) {
      Serial.println("Erreur: Timeout en attente des octets de démarrage");
      // Vider le buffer en cas de timeout pour la prochaine tentative
      while(s->available()) s->read();
      return false;
    }

    // Attend qu'au moins un octet soit disponible
    if (s->available()) {
      startChar1 = startChar2; // Décale l'ancien deuxième octet
      startChar2 = s->read();  // Lit le nouvel octet
      // Débogage: voir ce qui est lu pendant la synchronisation
      // Serial.print(startChar2, HEX); Serial.print(" ");
    } else {
      // Petite pause si rien n'est disponible pour ne pas surcharger le CPU
      delay(5);
    }
  }
  // Serial.println("\nOctets de démarrage trouvés: 42 4D"); // Débogage

  // --- Étape 2: Lire le reste de la trame (30 octets) ---
  // Nous avons déjà lu 0x42 et 0x4D, il reste 30 octets à lire (y compris le checksum)
  byte buffer[32]; // Buffer complet pour faciliter le calcul du checksum
  buffer[0] = 0x42;
  buffer[1] = 0x4D;

  // Tente de lire les 30 octets restants
  int bytesRead = s->readBytes(buffer + 2, 30);

  // Vérifie si la lecture a réussi et si le nombre d'octets est correct
  if (bytesRead < 30) {
    Serial.print("Erreur: Lecture incomplète. Reçu ");
    Serial.print(bytesRead + 2); // +2 pour les octets de démarrage déjà lus
    Serial.println("/32 octets avant timeout.");
    // Vider le reste du buffer potentiel pour la prochaine tentative
     while(s->available()) s->read();
    return false;
  }

  // --- Étape 3: Vérifier le Checksum ---
  uint16_t sum = 0;
  for (uint8_t i = 0; i < 30; i++) {
    sum += buffer[i];
  }

  // Lire le checksum depuis les deux derniers octets du buffer
  uint16_t checksum_received = (buffer[30] << 8) | buffer[31];

  if (sum != checksum_received) {
    Serial.print("Erreur: Checksum invalide. Calculé: 0x");
    Serial.print(sum, HEX);
    Serial.print(", Reçu: 0x");
    Serial.println(checksum_received, HEX);
    // Il n'est pas nécessaire de vider le buffer ici car une trame complète (mais invalide) a été lue.
    // La prochaine lecture tentera de se synchroniser à nouveau.
    return false;
  }

  // --- Étape 4: Parser les données (si le checksum est valide) ---
  // Convertit les paires d'octets (Big Endian) en uint16_t (Little Endian de l'ESP32)
  // Les données utiles commencent à l'index 2 (après 0x42 0x4D)
  data.framelen        = (buffer[2] << 8) | buffer[3];
  data.pm10_standard   = (buffer[4] << 8) | buffer[5];
  data.pm25_standard   = (buffer[6] << 8) | buffer[7];
  data.pm100_standard  = (buffer[8] << 8) | buffer[9];
  data.pm10_env        = (buffer[10] << 8) | buffer[11];
  data.pm25_env        = (buffer[12] << 8) | buffer[13];
  data.pm100_env       = (buffer[14] << 8) | buffer[15];
  data.particles_03um  = (buffer[16] << 8) | buffer[17];
  data.particles_05um  = (buffer[18] << 8) | buffer[19];
  data.particles_10um  = (buffer[20] << 8) | buffer[21];
  data.particles_25um  = (buffer[22] << 8) | buffer[23];
  data.particles_50um  = (buffer[24] << 8) | buffer[25];
  data.particles_100um = (buffer[26] << 8) | buffer[27];
  data.unused          = (buffer[28] << 8) | buffer[29];
  data.checksum        = checksum_received; // Déjà extrait pour la vérification

  // Vérification supplémentaire (op tionnelle) de la longueur de trame
  if (data.framelen != 2 * 13 + 2) { // 2 octets par champ * 13 champs + 2 octets header
     Serial.print("Avertissement: Longueur de trame incorrecte reçue: ");
     Serial.println(data.framelen);
     // On pourrait retourner false ici, mais si le checksum est bon, on accepte souvent les données.
  }
  // La lecture et la validation ont réussi
  return true;
}
/////////////code PMS5