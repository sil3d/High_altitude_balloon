import serial
import folium
from folium.plugins import TimestampedGeoJson

# Configuration du port série
ser = serial.Serial('COM4', 115200)  # Remplacez 'COM3' par le port série de votre récepteur

# Créer une carte centrée sur une position initiale
m = folium.Map(location=[48.8566, 2.3522], zoom_start=10)

# Liste pour stocker les points GPS
points = []

try:
    while True:
        # Lire une ligne de données depuis le port série
        line = ser.readline().decode('utf-8').strip()
        print("Données reçues :", line)

        # Extraire les données GPS (exemple de format : "Lat: 48.8566, Lng: 2.3522, Alt: 35 m")
        if "Lat:" in line and "Lng:" in line:
            lat = float(line.split("Lat: ")[1].split(",")[0])
            lng = float(line.split("Lng: ")[1].split(",")[0])
            alt = float(line.split("Alt: ")[1].split(" ")[0])

            # Ajouter le point à la liste
            points.append({
                "coordinates": [lng, lat],
                "time": "2023-10-01T00:00:00",  # Remplacez par l'heure réelle si disponible
                "altitude": alt
            })

            # Ajouter un marqueur sur la carte
            folium.Marker([lat, lng], popup=f"Alt: {alt} m").add_to(m)

            # Centrer la carte sur la dernière position
            m.location = [lat, lng]

            # Sauvegarder la carte dans un fichier HTML
            m.save("ballon_tracker.html")

except KeyboardInterrupt:
    print("Arrêt du programme.")

finally:
    ser.close()