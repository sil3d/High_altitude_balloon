from flask import Flask, render_template, jsonify, request, Response
import serial
import time
import threading
import pandas as pd
from datetime import datetime
import os

app = Flask(__name__)

# Configuration
SERIAL_PORT = 'COM3'  # À modifier selon votre port série
BAUD_RATE = 115200
EXCEL_FILE = 'donnees_ballon.xlsx'

# Variables globales pour stocker les données
data_lock = threading.Lock()
latest_data = {
    'gps': {'lat': 0.0, 'lon': 0.0, 'alt': 0.0, 'satellites': 0, 'time': '', 'hasGPSFix': False},
    'env': {'temperature': 0.0, 'pressure': 0, 'humidity': 0.0, 'altitude': 0.0},
    'air': {'airQuality': 0, 'tvoc': 0, 'eCO2': 0},
    'other': {'ozone': 0, 'uvIndex': 0.0},
    'raw': '',
    
}

# Connexion série
serial_conn = None
stop_thread = False

def parse_data(raw_data):
    """Parse les données brutes reçues du module LoRa"""
    data = {}
    data['raw'] = raw_data
    data['timestamp'] = datetime.now().strftime("%H:%M:%S")
    
    # Initialiser les structures
    data['gps'] = {'lat': 0.0, 'lon': 0.0, 'alt': 0.0, 'satellites': 0, 'time': '', 'hasGPSFix': False}
    data['env'] = {'temperature': 0.0, 'pressure': 0, 'humidity': 0.0, 'altitude': 0.0}
    data['air'] = {'airQuality': 0, 'tvoc': 0, 'eCO2': 0}
    data['other'] = {'ozone': 0, 'uvIndex': 0.0}
    
    # Diviser les données en sections
    gps_index = raw_data.find("GPS,")
    env_index = raw_data.find("|ENV,")
    air_index = raw_data.find("|AIR,")
    oz_index = raw_data.find("|OZ,")
    uv_index = raw_data.find("|UV,")
    
    # Parser les données GPS
    if gps_index != -1:
        gps_section = raw_data[gps_index + 4:env_index if env_index > 0 else len(raw_data)]
        if gps_section != "NO_FIX":
            parts = gps_section.split(',')
            if len(parts) >= 5:
                data['gps']['lat'] = float(parts[0])
                data['gps']['lon'] = float(parts[1])
                data['gps']['alt'] = float(parts[2])
                data['gps']['satellites'] = int(parts[3])
                data['gps']['time'] = parts[4]
                data['gps']['hasGPSFix'] = True
    
    # Parser les données environnementales
    if env_index != -1:
        env_section = raw_data[env_index + 5:air_index if air_index > 0 else len(raw_data)]
        parts = env_section.split(',')
        if len(parts) >= 4:
            data['env']['temperature'] = float(parts[0])
            data['env']['pressure'] = int(parts[1])
            data['env']['humidity'] = float(parts[2])
            data['env']['altitude'] = float(parts[3])
    
    # Parser les données de qualité d'air
    if air_index != -1:
        air_section = raw_data[air_index + 5:oz_index if oz_index > 0 else len(raw_data)]
        parts = air_section.split(',')
        if len(parts) >= 3:
            data['air']['airQuality'] = int(parts[0])
            data['air']['tvoc'] = int(parts[1])
            data['air']['eCO2'] = int(parts[2])
    
    # Parser les données d'ozone
    if oz_index != -1:
        oz_section = raw_data[oz_index + 4:uv_index if uv_index > 0 else len(raw_data)]
        data['other']['ozone'] = int(oz_section)
    
    # Parser les données UV
    if uv_index != -1:
        uv_section = raw_data[uv_index + 4:]
        data['other']['uvIndex'] = float(uv_section)
    
    return data

def serial_reader():
    """Thread pour lire les données série"""
    global latest_data, stop_thread, serial_conn
    
    try:
        serial_conn = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
        print(f"Connecté au port série {SERIAL_PORT}")
    except Exception as e:
        print(f"Erreur lors de la connexion au port série: {e}")
        return
    
    buffer = ""
    
    while not stop_thread:
        try:
            if serial_conn.in_waiting:
                data = serial_conn.read(serial_conn.in_waiting).decode('utf-8', errors='ignore')
                buffer += data
                
                # Chercher des lignes complètes
                lines = buffer.split("\n")
                buffer = lines[-1]  # Garder la dernière ligne incomplète
                
                for line in lines[:-1]:
                    line = line.strip()
                    if "GPS," in line and "|ENV," in line:  # S'assurer que c'est une ligne de données complète
                        parsed_data = parse_data(line)
                        
                        with data_lock:
                            latest_data = parsed_data
                            # Ajouter à l'historique (limité à 100 points)
                            if parsed_data['gps']['hasGPSFix']:
                                history_point = {
                                    'lat': parsed_data['gps']['lat'],
                                    'lon': parsed_data['gps']['lon'],
                                    'alt': parsed_data['gps']['alt'],
                                    'time': parsed_data['timestamp']
                                }
                                latest_data['history'].append(history_point)
                                if len(latest_data['history']) > 100:
                                    latest_data['history'] = latest_data['history'][-100:]
                        
                        # Sauvegarder les données dans Excel
                        save_to_excel(parsed_data)
                
            time.sleep(0.1)  # Petite pause pour éviter de surcharger le CPU
            
        except Exception as e:
            print(f"Erreur dans la lecture série: {e}")
            time.sleep(1)  # Pause plus longue en cas d'erreur
    
    if serial_conn:
        serial_conn.close()

def save_to_excel(data):
    """Sauvegarde les données dans un fichier Excel"""
    try:
        # Préparer les données pour Excel
        row = {
            'Timestamp': datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
            'Latitude': data['gps']['lat'] if data['gps']['hasGPSFix'] else None,
            'Longitude': data['gps']['lon'] if data['gps']['hasGPSFix'] else None,
            'Altitude_GPS': data['gps']['alt'] if data['gps']['hasGPSFix'] else None,
            'Satellites': data['gps']['satellites'] if data['gps']['hasGPSFix'] else None,
            'Heure_GPS': data['gps']['time'] if data['gps']['hasGPSFix'] else None,
            'Temperature': data['env']['temperature'],
            'Pression': data['env']['pressure'],
            'Humidite': data['env']['humidity'],
            'Altitude_Baro': data['env']['altitude'],
            'Qualite_Air': data['air']['airQuality'],
            'TVOC': data['air']['tvoc'],
            'eCO2': data['air']['eCO2'],
            'Ozone': data['other']['ozone'],
            'Indice_UV': data['other']['uvIndex'],
        }
        
        # Charger le fichier existant ou créer un nouveau DataFrame
        if os.path.exists(EXCEL_FILE):
            df = pd.read_excel(EXCEL_FILE)
            df = pd.concat([df, pd.DataFrame([row])], ignore_index=True)
        else:
            df = pd.DataFrame([row])
        
        # Sauvegarder le DataFrame
        df.to_excel(EXCEL_FILE, index=False)
    
    except Exception as e:
        print(f"Erreur lors de la sauvegarde dans Excel: {e}")

# Démarrer le thread de lecture série
serial_thread = threading.Thread(target=serial_reader)
serial_thread.daemon = True
serial_thread.start()

@app.route('/')
def index():
    """Page principale"""
    return render_template('index.html')


@app.route('/download')
def download_excel():
    """Téléchargement du fichier Excel"""
    if os.path.exists(EXCEL_FILE):
        with open(EXCEL_FILE, 'rb') as f:
            data = f.read()
        
        return Response(
            data,
            mimetype="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet",
            headers={"Content-disposition": f"attachment; filename={EXCEL_FILE}"}
        )
    else:
        return "Aucune donnée disponible pour le téléchargement", 404

# Arrêt propre du thread lors de la fermeture de l'application
def cleanup():
    global stop_thread
    stop_thread = True
    if serial_thread.is_alive():
        serial_thread.join(timeout=1.0)
    print("Arrêt du serveur Flask")

# Enregistrer la fonction de nettoyage
import atexit
atexit.register(cleanup)

if __name__ == '__main__':
    app.run(debug=True, host='0.0.0.0', port=5000)