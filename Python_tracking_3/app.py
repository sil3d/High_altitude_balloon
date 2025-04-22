# app.py (Version avec SQLite3)

import serial
import threading
import time
import json
import csv
import os
import sqlite3 # <<< Ajouté
from datetime import datetime
from flask import Flask, render_template, send_file, request, jsonify
from flask_socketio import SocketIO, emit
import pandas as pd
import io
import math


# --- Configuration ---
SERIAL_PORT = 'COM5' # Adaptez si nécessaire
BAUD_RATE = 115200   # Doit correspondre au Serial.begin() du RECEIVER ESP32
DATA_FORMAT = 'xlsx' # Format pour le téléchargement ('excel' ou 'csv')
DATA_DIR = 'data'
DB_FILENAME = os.path.join(DATA_DIR, 'balloon_data.db') # <<< Fichier Base de Données
DOWNLOAD_FILENAME_BASE = 'balloon_data' # Sera .xlsx ou .csv
DEBUG_MODE = False # Mettre à True pour plus de logs Flask/SocketIO
# MAX_HISTORY n'est plus nécessaire pour le stockage long terme

# --- Initialisation Flask et SocketIO ---
app = Flask(__name__)
app.config['SECRET_KEY'] = 'votre_super_secret_key_ici!' # CHANGEZ CECI
socketio = SocketIO(app, async_mode='threading')

# --- Variables Globales ---
ser = None
serial_thread = None
stop_thread = threading.Event()
data_lock = threading.Lock() # Toujours utile pour latest_data

# latest_data garde la dernière donnée reçue pour l'affichage immédiat
latest_data = {
    "timestamp": None, 'latitude': None, 'longitude': None, 'altitude_gps': None,
    'satellites': None, 'temperature': None, 'pressure': None, 'humidity': None,
    'altitude_bme': None, 'air_quality': None, 'tvoc': None, 'eco2': None,
    'ozone': None, 'uv_index': None,
    'pm1_std': None, 'pm25_std': None, 'pm10_std': None,
    'rssi': None, 'speed_kmh': None,
    'error': "Initialisation..."
}
# data_history est supprimée, remplacée par la DB

previous_location_for_speed = None # Gardé pour le calcul de vitesse

# --- Fonctions Utilitaires ---
def ensure_data_dir():
    if not os.path.exists(DATA_DIR):
        try: os.makedirs(DATA_DIR); print(f"Dossier '{DATA_DIR}' créé.")
        except OSError as e: print(f"Erreur création dossier '{DATA_DIR}': {e}")

# haversine et calculate_speed restent identiques
def haversine_manual(lat1, lon1, lat2, lon2):
    R = 6371000
    phi1, phi2 = math.radians(lat1), math.radians(lat2)
    dphi, dlambda = math.radians(lat2 - lat1), math.radians(lon2 - lon1)
    a = math.sin(dphi/2)**2 + math.cos(phi1)*math.cos(phi2)*math.sin(dlambda/2)**2
    c = 2 * math.atan2(math.sqrt(a), math.sqrt(1-a))
    return R * c

def calculate_speed_kmh(current_lat, current_lon, current_time):
    global previous_location_for_speed
    if not all(isinstance(x, (int, float)) for x in [current_lat, current_lon]): return None
    try: current_dt = datetime.fromtimestamp(current_time)
    except (TypeError, ValueError): return None
    if not previous_location_for_speed:
         previous_location_for_speed = {"lat": current_lat, "lon": current_lon, "dt": current_dt, "speed_kmh": 0.0}
         return 0.0
    try:
        prev_lat, prev_lon, prev_dt = previous_location_for_speed["lat"], previous_location_for_speed["lon"], previous_location_for_speed["dt"]
        if not all(isinstance(x, (int, float)) for x in [prev_lat, prev_lon]):
             previous_location_for_speed = {"lat": current_lat, "lon": current_lon, "dt": current_dt, "speed_kmh": 0.0}
             return 0.0
        distance_m = haversine_manual(prev_lat, prev_lon, current_lat, current_lon)
        time_diff_s = (current_dt - prev_dt).total_seconds()
        if time_diff_s < 0.5: return previous_location_for_speed.get("speed_kmh", 0.0)
        speed_mps = distance_m / time_diff_s
        speed_kmh = round(speed_mps * 3.6, 2)
        previous_location_for_speed = {"lat": current_lat, "lon": current_lon, "dt": current_dt, "speed_kmh": speed_kmh}
        return speed_kmh
    except Exception as e:
        print(f"Erreur calcul vitesse: {e}")
        previous_location_for_speed = {"lat": current_lat, "lon": current_lon, "dt": current_dt, "speed_kmh": 0.0}
        return None

# parse_serial_data reste identique - il parse la ligne et retourne un dict
def parse_serial_data(compact_line):
    data = {
        "timestamp": time.time(), 'latitude': None, 'longitude': None, 'altitude_gps': None,
        'satellites': None, 'temperature': None, 'pressure': None, 'humidity': None,
        'altitude_bme': None, 'air_quality': None, 'tvoc': None, 'eco2': None,
        'ozone': None, 'uv_index': None,
        'pm1_std': None, 'pm25_std': None, 'pm10_std': None,
        'rssi': None, 'speed_kmh': None, 'error': None
    }
    data_prefix = "Donnees brutes: "
    if compact_line.startswith(data_prefix):
        compact_line = compact_line[len(data_prefix):]

    parts = compact_line.strip().split('|')
    valid_data_found_in_line = False

    for part in parts:
        if not part: continue
        elements = part.split(',')
        if len(elements) < 1: continue
        header = elements[0]; values = elements[1:]
        try:
            if header == "GPS" and len(values) >= 6:
                if values[0] != "ERR":
                    try:
                        lat, lon = float(values[0]), float(values[1])
                        if lat != 0.0 or lon != 0.0:
                            data['latitude'] = lat; data['longitude'] = lon
                            data['altitude_gps'] = float(values[2]) if values[2] != "ERR" else None
                            data['satellites'] = int(values[3]) if values[3] != "ERR" else None
                            valid_data_found_in_line = True
                    except (ValueError, IndexError): pass
            elif header == "ENV" and len(values) >= 4:
                try:
                    if values[0] != "ERR": data['temperature'] = float(values[0])
                    if values[1] != "ERR": data['pressure'] = float(values[1]) # Pa
                    if values[2] != "ERR": data['humidity'] = float(values[2])
                    if values[3] != "ERR": data['altitude_bme'] = float(values[3])
                    valid_data_found_in_line = True
                except (ValueError, IndexError): pass
            elif header == "AIR" and len(values) >= 3:
                try:
                    if values[0] != "ERR": data['air_quality'] = int(values[0])
                    if values[1] != "ERR": data['tvoc'] = int(values[1])
                    if values[2] != "ERR": data['eco2'] = int(values[2])
                    valid_data_found_in_line = True
                except (ValueError, IndexError): pass
            elif header == "OZ" and len(values) >= 1:
                try:
                    if values[0] != "ERR": data['ozone'] = int(values[0])
                    valid_data_found_in_line = True
                except (ValueError, IndexError): pass
            elif header == "UV" and len(values) >= 1:
                try:
                    if values[0] != "ERR":
                        uv_val = float(values[0])
                        if uv_val >= 0: data['uv_index'] = uv_val
                    valid_data_found_in_line = True
                except (ValueError, IndexError): pass
            elif header == "PMS" and len(values) >= 3:
                try:
                    if values[0] != "ERR": data['pm1_std'] = int(values[0])
                    if values[1] != "ERR": data['pm25_std'] = int(values[1])
                    if values[2] != "ERR": data['pm10_std'] = int(values[2])
                    valid_data_found_in_line = True
                except (ValueError, IndexError): pass
            # RSSI est géré séparément dans serial_reader_task
        except Exception as section_e: print(f"Erreur parsing section {header}: {section_e}")

    if data.get('latitude') is not None:
        data['speed_kmh'] = calculate_speed_kmh(data['latitude'], data['longitude'], data['timestamp'])
    return data

# --- Fonctions Base de Données SQLite ---

# <<< NOUVEAU: Initialisation DB >>>
def init_db():
    """Crée la table de télémétrie si elle n'existe pas."""
    try:
        # Utilisation de 'with' pour gérer la connexion/commit/close
        with sqlite3.connect(DB_FILENAME) as conn:
            cursor = conn.cursor()
            cursor.execute("""
                CREATE TABLE IF NOT EXISTS telemetry (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    timestamp REAL NOT NULL,
                    latitude REAL,
                    longitude REAL,
                    altitude_gps REAL,
                    satellites INTEGER,
                    temperature REAL,
                    pressure REAL,
                    humidity REAL,
                    altitude_bme REAL,
                    air_quality INTEGER,
                    tvoc INTEGER,
                    eco2 INTEGER,
                    ozone INTEGER,
                    uv_index REAL,
                    pm1_std INTEGER,
                    pm25_std INTEGER,
                    pm10_std INTEGER,
                    rssi INTEGER,
                    speed_kmh REAL
                )
            """)
            # Ajouter un index sur le timestamp peut accélérer les requêtes ORDER BY / WHERE
            cursor.execute("CREATE INDEX IF NOT EXISTS idx_timestamp ON telemetry (timestamp)")
            print(f"Base de données '{DB_FILENAME}' initialisée/vérifiée.")
    except sqlite3.Error as e:
        print(f"ERREUR DB (init): {e}")
        # Gérer l'erreur potentiellement critique (ex: arrêter l'appli?)
        raise # Renvoyer l'erreur pour arrêter si l'init échoue

# <<< NOUVEAU: Insertion Données >>>
def insert_data(data):
    """Insère un dictionnaire de données dans la table telemetry."""
    # Définir l'ordre des colonnes correspondant à la table et aux placeholders
    # ATTENTION: L'ordre est crucial ici !
    cols = ['timestamp', 'latitude', 'longitude', 'altitude_gps', 'satellites',
            'temperature', 'pressure', 'humidity', 'altitude_bme', 'air_quality',
            'tvoc', 'eco2', 'ozone', 'uv_index', 'pm1_std', 'pm25_std',
            'pm10_std', 'rssi', 'speed_kmh']
    # Créer la chaîne de placeholders (?, ?, ...)
    placeholders = ', '.join(['?'] * len(cols))
    sql = f"INSERT INTO telemetry ({', '.join(cols)}) VALUES ({placeholders})"

    # Extraire les valeurs du dictionnaire dans le bon ordre, en utilisant None si la clé manque
    values = tuple(data.get(col) for col in cols)

    try:
        with sqlite3.connect(DB_FILENAME, timeout=10) as conn: # timeout pour éviter lock db prolongé
            cursor = conn.cursor()
            cursor.execute(sql, values)
            # Pas besoin de commit explicite avec 'with' pour les opérations simples
            # conn.commit() # commit est automatique à la sortie du 'with' sans erreur
            print(f"DB_INSERT OK: Timestamp {data.get('timestamp')}") # Log succès
    except sqlite3.Error as e:
        print(f"ERREUR DB (insert): {e} - Data: {data}")
        # Décider quoi faire: loguer, ignorer, réessayer?

# --- Tâche de Lecture Série (Modifiée pour insérer dans DB) ---
def serial_reader_task():
    global latest_data, ser
    print("Démarrage du thread de lecture série (Mode Multi-Lignes + SQLite)...")
    last_rssi_value = None

    while not stop_thread.is_set():
        serial_error_message = None
        try:
            if ser is None or not ser.is_open:
                # ... (logique de connexion/reconnexion identique) ...
                if ser: 
                    try: 
                        ser.close() 
                    except Exception: pass
                print(f"Tentative de connexion à {SERIAL_PORT}@{BAUD_RATE}...")
                try:
                    ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
                    print(f"Connecté avec succès à {SERIAL_PORT}")
                    with data_lock: latest_data['error'] = None
                    socketio.emit('serial_status', {'status': 'connected', 'port': SERIAL_PORT, 'message': None})
                    time.sleep(0.5); ser.reset_input_buffer()
                except (serial.SerialException, PermissionError, FileNotFoundError) as e:
                    serial_error_message = f"Échec connexion {SERIAL_PORT}: {e}"
                    print(f"ERREUR: {serial_error_message}")
                    ser = None
                    with data_lock:
                         if latest_data.get('error') != serial_error_message:
                             latest_data['error'] = serial_error_message
                             socketio.emit('update_data', latest_data)
                    socketio.emit('serial_status', {'status': 'error', 'port': SERIAL_PORT, 'message': str(e)})
                    stop_thread.wait(5)
                    continue

            if ser and ser.is_open:
                if ser.in_waiting > 0:
                    line = ser.readline()
                    try:
                        raw_line = line.decode('utf-8', errors='ignore').strip()
                        if raw_line: print(f"PY_READ_LINE: [{raw_line}]")

                        if raw_line:
                            data_prefix = "Donnees brutes: "
                            if raw_line.startswith(data_prefix):
                                compact_data_string = raw_line[len(data_prefix):]
                                print(f"PY_FOUND_DATA: Extracted [{compact_data_string}]")

                                parsed_data = parse_serial_data(compact_data_string)

                                # Appliquer le dernier RSSI connu avant l'insertion/émission
                                if last_rssi_value is not None:
                                    parsed_data['rssi'] = last_rssi_value
                                    print(f"PY_APPLY_RSSI: Ajout RSSI={last_rssi_value}")
                                    last_rssi_value = None # Consommer RSSI

                                # ---- MODIFICATION PRINCIPALE ----
                                # 1. Insérer dans la base de données
                                insert_data(parsed_data)

                                # 2. Mettre à jour latest_data (pour l'UI temps réel)
                                with data_lock:
                                    has_valid_sensor_data = any(
                                        v is not None for k, v in parsed_data.items()
                                        if k not in ['timestamp', 'rssi', 'speed_kmh', 'error', 'latitude', 'longitude', 'altitude_gps', 'satellites']
                                    )
                                    if has_valid_sensor_data and latest_data.get('error'):
                                        print("PY_CLEAR_ERROR: Erreur effacée car données capteur reçues.")
                                        parsed_data['error'] = None # Efface l'erreur pour l'envoi

                                    latest_data = parsed_data.copy() # Garder une copie pour l'UI

                                # 3. Émettre vers les clients WebSocket
                                print(f"PY_EMIT_UPDATE: {latest_data}")
                                socketio.emit('update_data', latest_data)
                                socketio.emit('serial_status', {'status': 'receiving', 'port': SERIAL_PORT, 'message': None})
                                # data_history.append() est SUPPRIMÉ

                            elif raw_line.startswith("RSSI:"):
                                try:
                                    rssi_part = raw_line.split('|')[0]
                                    rssi_value_str = rssi_part.split(':')[1].strip()
                                    last_rssi_value = int(rssi_value_str)
                                    print(f"PY_FOUND_RSSI: Stored {last_rssi_value} for next packet")
                                except (IndexError, ValueError) as e_rssi:
                                    print(f"PY_WARN: Impossible parser RSSI: {raw_line} - {e_rssi}")
                        # else: ligne vide ignorée
                    except UnicodeDecodeError as ude: print(f"ERREUR DECODAGE: {ude} pour bytes: {line}")
                    except serial.SerialException as e:
                        # ... (gestion erreur lecture identique) ...
                        serial_error_message = f"Erreur série pendant lecture: {e}"
                        print(f"ERREUR LECTURE: {serial_error_message}")
                        if ser: 
                            try: 
                                ser.close() 
                            except Exception: pass
                        ser = None
                        with data_lock: latest_data['error'] = serial_error_message
                        socketio.emit('serial_status', {'status': 'error', 'port': SERIAL_PORT, 'message': str(e)})
                        socketio.emit('update_data', latest_data)
                        stop_thread.wait(2)
                    except Exception as e_proc:
                         print(f"Erreur traitement ligne: {e_proc} pour ligne: {raw_line}")
                         with data_lock: latest_data['error'] = f"Erreur proc: {e_proc}"; latest_data['timestamp'] = time.time()
                         socketio.emit('update_data', latest_data)
                else: stop_thread.wait(0.05)
            else: stop_thread.wait(1)
        except Exception as e_main:
            # ... (gestion erreur majeure identique) ...
            serial_error_message = f"Erreur majeure thread série: {e_main}"
            print(f"ERREUR MAJEURE: {serial_error_message}")
            if ser: 
                try: 
                    ser.close() 
                except Exception: pass; ser = None
            with data_lock: latest_data['error'] = serial_error_message
            socketio.emit('serial_status', {'status': 'error', 'port': SERIAL_PORT, 'message': str(e_main)})
            socketio.emit('update_data', latest_data)
            stop_thread.wait(5)

    print("Arrêt thread série demandé.")
    if ser and ser.is_open: 
        try: 
            ser.close(); 
            print(f"Port {SERIAL_PORT} fermé.") 
        except Exception as e: 
            print(f"Erreur fermeture port: {e}")
    ser = None; print("Thread série terminé.")

# --- Routes Flask ---

@app.route('/')
def index():
    return render_template('index.html')

# <<< MODIFIÉ: Route /download lit depuis SQLite >>>
@app.route('/download')
def download_data():
    db_data = []
    conn = None # Initialiser hors du try pour le finally
    try:
        # Se connecter et lire TOUTES les données de la base
        conn = sqlite3.connect(DB_FILENAME)
        cursor = conn.cursor()
        cursor.execute("SELECT * FROM telemetry ORDER BY timestamp ASC") # Récupérer dans l'ordre chronologique
        rows = cursor.fetchall()

        # Obtenir les noms de colonnes
        colnames = [description[0] for description in cursor.description]

        if not rows:
            return "Aucune donnée enregistrée dans la base.", 404

        # Convertir les lignes (tuples) en liste de dictionnaires (plus facile pour Pandas)
        # for row in rows:
        #    db_data.append(dict(zip(colnames, row)))
        # Ou directement dans le DataFrame
        df = pd.DataFrame(rows, columns=colnames)
        print(f"DB_READ: {len(df)} lignes lues depuis la base pour le téléchargement.")

        # Traitement du DataFrame (conversion timestamp, etc.)
        if 'timestamp' in df.columns:
             try: df['timestamp_iso'] = pd.to_datetime(df['timestamp'], unit='s').dt.strftime('%Y-%m-%d %H:%M:%S')
             except Exception as date_e: print(f"Erreur conversion date: {date_e}")
        if 'id' in df.columns:
            df = df.drop(columns=['id']) # On n'a pas besoin de l'ID interne dans le fichier téléchargé

        # Génération du fichier Excel ou CSV
        ensure_data_dir()
        output_filename = f"{DOWNLOAD_FILENAME_BASE}.{DATA_FORMAT}"
        local_filepath = os.path.join(DATA_DIR, output_filename) # Pour sauvegarde locale optionnelle

        if DATA_FORMAT == 'xlsx':
            output = io.BytesIO()
            # Utilisation explicite de 'openpyxl' comme moteur (bonne pratique)
            with pd.ExcelWriter(output, engine='openpyxl') as writer:
                df.to_excel(writer, index=False, sheet_name='TelemetryData')
            output.seek(0)
            # --- MODIFIÉ: Le mimetype pour .xlsx ---
            mimetype='application/vnd.openxmlformats-officedocument.spreadsheetml.sheet'
            download_name = output_filename # Le nom inclut déjà .xlsx
            try:
                # Sauvegarde locale en .xlsx
                df.to_excel(local_filepath, index=False, engine='openpyxl')
                print(f"Fichier sauvegardé localement : {local_filepath}")
            except Exception as save_e:
                print(f"Erreur sauvegarde locale Excel (.xlsx): {save_e}")
        elif DATA_FORMAT == 'csv':
            output = io.StringIO(); df.to_csv(output, index=False, quoting=csv.QUOTE_NONNUMERIC)
            output_bytes = io.BytesIO(output.getvalue().encode('utf-8')); output_bytes.seek(0); output = output_bytes
            mimetype='text/csv'; download_name=output_filename
            try: df.to_csv(local_filepath, index=False, encoding='utf-8') # Sauvegarde locale
            except Exception as save_e: print(f"Erreur sauvegarde locale CSV: {save_e}")
        else: return "Format de téléchargement non supporté.", 500

        print(f"Préparation téléchargement ({DATA_FORMAT}) {len(df)} lignes.")
        return send_file(output, mimetype=mimetype, download_name=download_name, as_attachment=True)

    except sqlite3.Error as e:
        print(f"ERREUR DB (download): {e}")
        return f"Erreur base de données lors de la récupération des données: {e}", 500
    except Exception as e:
        print(f"Erreur génération/envoi fichier ({DATA_FORMAT}): {e}")
        import traceback; traceback.print_exc()
        return f"Erreur serveur lors de la génération du fichier: {e}", 500
    finally:
        if conn:
            conn.close() # Toujours fermer la connexion DB

# --- Gestion SocketIO ---

# <<< MODIFIÉ: handle_connect lit l'historique depuis SQLite >>>
@socketio.on('connect')
def handle_connect():
    sid = request.sid; print(f"Client connecté: {sid}")
    history_to_send = []
    conn = None
    try:
        # Récupérer les 100 derniers points de la DB pour l'historique initial
        conn = sqlite3.connect(DB_FILENAME)
        conn.row_factory = sqlite3.Row # Pour obtenir des résultats comme des dictionnaires
        cursor = conn.cursor()
        # Sélectionner les 100 plus récents
        cursor.execute("SELECT * FROM telemetry ORDER BY timestamp DESC LIMIT 100")
        rows = cursor.fetchall()
        # Convertir les sqlite3.Row en dictionnaires standard et inverser l'ordre pour l'affichage chronologique
        history_to_send = [dict(row) for row in reversed(rows)]
        print(f"DB_READ: {len(history_to_send)} lignes lues pour l'historique initial.")

    except sqlite3.Error as e:
        print(f"ERREUR DB (initial history): {e}")
        # Envoyer un historique vide en cas d'erreur DB
        history_to_send = []
    finally:
        if conn:
            conn.close()

    # Envoyer l'état actuel et l'historique lu
    with data_lock: current_state = latest_data.copy()
    emit('update_data', current_state, room=sid) # Dernier point connu
    emit('initial_history', history_to_send, room=sid) # Historique de la DB

    # Envoyer le statut série actuel
    status = 'disconnected'; message = current_state.get('error', 'État inconnu')
    port_status = SERIAL_PORT
    if ser and ser.is_open: status = 'connected'; message = None
    elif latest_data.get('error'): status = 'error' # Utiliser l'erreur de latest_data si présente
    emit('serial_status', {'status': status, 'port': port_status, 'message': message}, room=sid)

    print(f"État initial et historique DB ({len(history_to_send)} points) envoyés à {sid}")


@socketio.on('disconnect')
def handle_disconnect():
    print(f"Client déconnecté: {request.sid}")

# --- Démarrage ---
if __name__ == '__main__':
    ensure_data_dir() # Crée le dossier data/ si besoin
    init_db()         # Crée/Vérifie la base de données et la table
    print("Démarrage serveur + thread série (Mode Multi-Lignes + SQLite)...")
    serial_thread = threading.Thread(target=serial_reader_task, daemon=True)
    serial_thread.start()
    print(f"Serveur prêt sur http://0.0.0.0:5000 (Debug Flask/SocketIO: {DEBUG_MODE})")
    try:
        socketio.run(app, host='0.0.0.0', port=5000, debug=DEBUG_MODE, use_reloader=False, allow_unsafe_werkzeug=True)
    except KeyboardInterrupt: print("Arrêt demandé...")
    finally:
        print("Signalisation arrêt thread..."); stop_thread.set()
        if serial_thread: serial_thread.join(timeout=2); print(f"Thread série encore vivant: {serial_thread.is_alive()}")
        print("Serveur arrêté.")