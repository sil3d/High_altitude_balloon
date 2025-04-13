import serial
import threading
import time
import json
import csv
import os
from datetime import datetime
from flask import Flask, render_template, send_file, request, jsonify
from flask_socketio import SocketIO, emit
import pandas as pd
import io
import math

# --- Configuration ---
SERIAL_PORT ='COM5' # Adaptez si nécessaire
BAUD_RATE = 115200  # <- Le récepteur envoie à 115200 maintenant
DATA_FORMAT = 'excel'
DATA_FILENAME = 'data/balloon_data.xlsx'
DATA_DIR = 'data'
DEBUG_MODE = False # Mettre à True pour plus de logs Flask/SocketIO
MAX_HISTORY = 500

# --- Initialisation Flask et SocketIO ---
app = Flask(__name__)
app.config['SECRET_KEY'] = 'secret_key_pour_le_ballon_tracker!' # Changez ceci
socketio = SocketIO(app, async_mode='threading')

# --- Variables Globales ---
ser = None
serial_thread = None
stop_thread = threading.Event()
data_lock = threading.Lock()
# <<< AJOUT PMS >>>
latest_data = {
    "timestamp": None, 'latitude': None, 'longitude': None, 'altitude_gps': None,
    'satellites': None, 'temperature': None, 'pressure': None, 'humidity': None,
    'altitude_bme': None, 'air_quality': None, 'tvoc': None, 'eco2': None,
    'ozone': None, 'uv_index': None,
    'pm1_std': None, 'pm25_std': None, 'pm10_std': None, # <- PMS Ajouté
    'rssi': None, 'speed_kmh': None,
    'error': "Initialisation..."
}
data_history = []
previous_location_for_speed = None

# --- Fonctions Utilitaires ---
# ensure_data_dir, haversine_manual, calculate_speed_kmh restent identiques
def ensure_data_dir():
    if not os.path.exists(DATA_DIR):
        try: os.makedirs(DATA_DIR); print(f"Dossier '{DATA_DIR}' créé.")
        except OSError as e: print(f"Erreur création dossier '{DATA_DIR}': {e}")

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
    except (TypeError, ValueError): print(f"Timestamp invalide: {current_time}"); return None
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

# <<< MODIFICATION parse_serial_data pour PMS >>>
def parse_serial_data(compact_line):
    """
    Parse la chaîne compacte (ex: "GPS,...|ENV,...|AIR,...|OZ,...|UV,...|PMS,...|RSSI,..."),
    retourne un dict avec toutes les clés.
    """
    data = { # Initialisation
        "timestamp": time.time(), 'latitude': None, 'longitude': None, 'altitude_gps': None,
        'satellites': None, 'temperature': None, 'pressure': None, 'humidity': None,
        'altitude_bme': None, 'air_quality': None, 'tvoc': None, 'eco2': None,
        'ozone': None, 'uv_index': None,
        'pm1_std': None, 'pm25_std': None, 'pm10_std': None, # <- PMS Ajouté
        'rssi': None, 'speed_kmh': None, 'error': None
    }
    # Retirer le préfixe "Donnees brutes: " s'il est présent (au cas où)
    data_prefix = "Donnees brutes: "
    if compact_line.startswith(data_prefix):
        compact_line = compact_line[len(data_prefix):]

    parts = compact_line.strip().split('|')
    valid_data_found_in_line = False

    for part in parts:
        if not part: continue
        elements = part.split(',')
        if len(elements) < 1: continue
        header = elements[0]
        values = elements[1:]

        try:
            # --- GPS ---
            if header == "GPS":
                 # Format GPS attendu: lat,lon,alt,sat,time,date OU ERR,ERR,ERR,ERR,ERR,ERR
                 if len(values) >= 6:
                     if values[0] != "ERR":
                         try:
                             lat = float(values[0])
                             lon = float(values[1])
                             if lat != 0.0 or lon != 0.0:
                                 data['latitude'] = lat
                                 data['longitude'] = lon
                                 data['altitude_gps'] = float(values[2]) if values[2] != "ERR" else None
                                 data['satellites'] = int(values[3]) if values[3] != "ERR" else None
                                 # On pourrait parser time/date ici aussi si nécessaire pour l'historique
                                 valid_data_found_in_line = True
                             else: print("PY_WARN: Coordonnées GPS nulles reçues.")
                         except (ValueError, IndexError) as e_gps: print(f"PY_ERR: Parsing GPS: {e_gps} in {part}")
                     else: print("PY_INFO: Section GPS marquée ERR reçue.")
                 else: print(f"PY_WARN: Format section GPS inattendu: {part}")

            # --- ENV ---
            elif header == "ENV" and len(values) >= 4:
                try:
                    # temp, pressure_pa, humidity, altitude_bme
                    if values[0] != "ERR": data['temperature'] = float(values[0])
                    if values[1] != "ERR": data['pressure'] = float(values[1]) # Reçoit Pa
                    if values[2] != "ERR": data['humidity'] = float(values[2])
                    if values[3] != "ERR": data['altitude_bme'] = float(values[3])
                    valid_data_found_in_line = True
                except (ValueError, IndexError) as e_env: print(f"PY_ERR: Parsing ENV: {e_env} in {part}")

            # --- AIR ---
            elif header == "AIR" and len(values) >= 3:
                 try:
                    # aqi, tvoc, eco2
                    if values[0] != "ERR": data['air_quality'] = int(values[0])
                    if values[1] != "ERR": data['tvoc'] = int(values[1])
                    if values[2] != "ERR": data['eco2'] = int(values[2])
                    valid_data_found_in_line = True
                 except (ValueError, IndexError) as e_air: print(f"PY_ERR: Parsing AIR: {e_air} in {part}")

            # --- OZ ---
            elif header == "OZ" and len(values) >= 1:
                 try:
                    # ozone
                    if values[0] != "ERR": data['ozone'] = int(values[0])
                    valid_data_found_in_line = True
                 except (ValueError, IndexError) as e_oz: print(f"PY_ERR: Parsing OZ: {e_oz} in {part}")

            # --- UV ---
            elif header == "UV" and len(values) >= 1:
                try:
                    # uv_index
                    if values[0] != "ERR":
                         uv_val = float(values[0])
                         if uv_val >= 0: data['uv_index'] = uv_val
                    valid_data_found_in_line = True # On considère traité même si ERR
                except (ValueError, IndexError) as e_uv: print(f"PY_ERR: Parsing UV: {e_uv} in {part}")

            # --- PMS --- <<< AJOUTÉ
            elif header == "PMS" and len(values) >= 3:
                try:
                    # pm1_std, pm25_std, pm10_std
                    if values[0] != "ERR": data['pm1_std'] = int(values[0])
                    if values[1] != "ERR": data['pm25_std'] = int(values[1])
                    if values[2] != "ERR": data['pm10_std'] = int(values[2])
                    valid_data_found_in_line = True
                except (ValueError, IndexError) as e_pms: print(f"PY_ERR: Parsing PMS: {e_pms} in {part}")

            # --- RSSI (Si jamais inclus dans la même ligne, peu probable avec le code actuel) ---
            elif header == "RSSI" and len(values) >= 1:
                try:
                    if values[0] != "ERR": data['rssi'] = int(values[0])
                except (ValueError, IndexError): pass # Moins critique
            # --- Fin des sections ---

        except Exception as section_e:
             print(f"Erreur inattendue section {header}: {section_e}")

    # Calcul vitesse si on a des coordonnées valides
    if data.get('latitude') is not None and data.get('longitude') is not None:
        data['speed_kmh'] = calculate_speed_kmh(data['latitude'], data['longitude'], data['timestamp'])
    else:
        data['speed_kmh'] = None # Pas de vitesse si pas de coordonnées

    #print(f"PY_DEBUG Parse Result: {data}") # Debug
    return data


# <<< MODIFICATION serial_reader_task pour BAUD_RATE et DEBUG >>>
def serial_reader_task():
    """Lit le port série ligne par ligne, identifie les lignes pertinentes,
       extrait les données et le RSSI, puis parse et émet."""
    global latest_data, data_history, ser
    print("Démarrage du thread de lecture série (Mode Multi-Lignes)...")

    last_rssi_value = None

    while not stop_thread.is_set():
        serial_error_message = None
        try:
            # --- Phase 1: Connexion/Reconnexion ---
            if ser is None or not ser.is_open:
                if ser:
                    try: ser.close()
                    except Exception: pass
                print(f"Tentative de connexion à {SERIAL_PORT}@{BAUD_RATE}...") # DEBUG
                try:
                    # Le BAUD_RATE doit correspondre à celui du moniteur série du RECEIVER
                    ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
                    print(f"Connecté avec succès à {SERIAL_PORT}") # DEBUG
                    with data_lock: latest_data['error'] = None
                    socketio.emit('serial_status', {'status': 'connected', 'port': SERIAL_PORT, 'message': None})
                    time.sleep(0.5)
                    ser.reset_input_buffer()
                    print("Buffer d'entrée réinitialisé.") # DEBUG
                except (serial.SerialException, PermissionError, FileNotFoundError) as e:
                    serial_error_message = f"Échec connexion {SERIAL_PORT}: {e}"
                    print(f"ERREUR: {serial_error_message}") # DEBUG PLUS VISIBLE
                    ser = None
                    with data_lock:
                         if latest_data.get('error') != serial_error_message:
                             latest_data['error'] = serial_error_message
                             socketio.emit('update_data', latest_data) # S'assurer que l'erreur est émise
                    socketio.emit('serial_status', {'status': 'error', 'port': SERIAL_PORT, 'message': str(e)})
                    stop_thread.wait(5)
                    continue

            # --- Phase 2: Lecture Ligne par Ligne ---
            if ser and ser.is_open:
                if ser.in_waiting > 0:
                    line = ser.readline()
                    try:
                        raw_line = line.decode('utf-8', errors='ignore').strip()

                        # Afficher CHAQUE ligne lue SI elle n'est pas vide
                        if raw_line:
                            print(f"PY_READ_LINE: [{raw_line}]")

                        if raw_line:
                            # TEST 1: Ligne de données brutes du récepteur?
                            # Le récepteur imprime "Donnees brutes: GPS,..."
                            data_prefix = "Donnees brutes: "
                            if raw_line.startswith(data_prefix):
                                compact_data_string = raw_line[len(data_prefix):]
                                print(f"PY_FOUND_DATA: Extracted [{compact_data_string}]")

                                parsed_data = parse_serial_data(compact_data_string)

                                if last_rssi_value is not None:
                                    parsed_data['rssi'] = last_rssi_value
                                    print(f"PY_APPLY_RSSI: Ajout RSSI={last_rssi_value}")
                                    # Consommer le RSSI pour ne pas l'appliquer plusieurs fois
                                    last_rssi_value = None

                                # Calcul de la vitesse est maintenant DANS parse_serial_data
                                # car il dépend de la présence de lat/lon

                                with data_lock:
                                    # Effacer erreur SEULEMENT si on a des données VALIDES (hors timestamp/rssi/speed/error)
                                    has_valid_sensor_data = any(
                                        v is not None for k, v in parsed_data.items()
                                        if k not in ['timestamp', 'rssi', 'speed_kmh', 'error', 'latitude', 'longitude', 'altitude_gps', 'satellites']
                                    )
                                    if has_valid_sensor_data and latest_data.get('error'):
                                        print("PY_CLEAR_ERROR: Erreur effacée car données capteur reçues.")
                                        parsed_data['error'] = None # Assure-toi d'effacer l'erreur dans le nouveau paquet

                                    latest_data = parsed_data.copy() # Remplace les anciennes données

                                    # Ajouter à l'historique SEULEMENT si on a une coordonnée GPS valide (ou d'autres données si nécessaire)
                                    if latest_data.get('latitude') is not None or has_valid_sensor_data:
                                        data_history.append(latest_data)
                                        if len(data_history) > MAX_HISTORY: data_history.pop(0)
                                    else:
                                        print("PY_INFO: Données non ajoutées à l'historique (pas de GPS ni données capteur valides).")


                                print(f"PY_EMIT_UPDATE: {latest_data}")
                                socketio.emit('update_data', latest_data)
                                socketio.emit('serial_status', {'status': 'receiving', 'port': SERIAL_PORT, 'message': None})


                            # TEST 2: Ligne RSSI/SNR ?
                            # Le récepteur imprime "RSSI: -XX | SNR: Y.YY"
                            elif raw_line.startswith("RSSI:"):
                                try:
                                    rssi_part = raw_line.split('|')[0]
                                    rssi_value_str = rssi_part.split(':')[1].strip()
                                    # Tenter de stocker, sera appliqué à la PROCHAINE ligne de données
                                    last_rssi_value = int(rssi_value_str)
                                    print(f"PY_FOUND_RSSI: Stored {last_rssi_value} for next data packet")
                                except (IndexError, ValueError) as e_rssi:
                                    print(f"PY_WARN: Impossible de parser RSSI: {raw_line} - {e_rssi}")
                                    # Ne pas écraser un éventuel ancien last_rssi_value valide
                            # Ignorer les autres lignes (commentaires, etc.)
                        else:
                            pass # Ligne vide ignorée
                    except UnicodeDecodeError as ude:
                        print(f"ERREUR DECODAGE: {ude} pour bytes: {line}")
                    except serial.SerialException as e:
                        serial_error_message = f"Erreur série pendant lecture: {e}"
                        print(f"ERREUR LECTURE: {serial_error_message}")
                        if ser: 
                            try: 
                                ser.close() 
                            except Exception: 
                                pass
                        ser = None
                        with data_lock: latest_data['error'] = serial_error_message
                        socketio.emit('serial_status', {'status': 'error', 'port': SERIAL_PORT, 'message': str(e)})
                        socketio.emit('update_data', latest_data)
                        stop_thread.wait(2)
                    except Exception as e_proc:
                         print(f"Erreur traitement ligne: {e_proc} pour ligne: {raw_line}")
                         with data_lock:
                             latest_data['error'] = f"Erreur proc: {e_proc}"; latest_data['timestamp'] = time.time()
                         socketio.emit('update_data', latest_data)
                else:
                    stop_thread.wait(0.05)
            else:
                 stop_thread.wait(1)
        except Exception as e_main:
            serial_error_message = f"Erreur majeure thread série: {e_main}"
            print(f"ERREUR MAJEURE: {serial_error_message}")
            if ser: 
                try: 
                    ser.close() 
                except Exception: 
                    pass
            ser = None
            with data_lock: latest_data['error'] = serial_error_message
            socketio.emit('serial_status', {'status': 'error', 'port': SERIAL_PORT, 'message': str(e_main)})
            socketio.emit('update_data', latest_data)
            stop_thread.wait(5)
    # --- Nettoyage ---
    print("Arrêt thread série demandé.")
    if ser and ser.is_open: 
        try: 
            ser.close(); print(f"Port {SERIAL_PORT} fermé.") 
        except Exception as e: 
            print(f"Erreur fermeture port: {e}")
    ser = None; print("Thread série terminé.")


# --- Routes Flask ---
# / et /download restent identiques conceptuellement, mais /download inclura les nouvelles colonnes PMS
@app.route('/')
def index():
    return render_template('index.html')

@app.route('/download')
def download_data():
    global data_history
    with data_lock:
        if not data_history: return "Aucune donnée historique.", 404
        # Crée une copie pour éviter les problèmes de concurrence pendant la génération du fichier
        history_copy = list(data_history)
    try:
        # Le DataFrame inclura automatiquement les nouvelles colonnes PMS
        df = pd.DataFrame(history_copy)
        if 'timestamp' in df.columns:
             try:
                 # Convertir en datetime lisible (supposant UTC, adaptez si nécessaire)
                 df['timestamp_iso'] = pd.to_datetime(df['timestamp'], unit='s').dt.strftime('%Y-%m-%d %H:%M:%S')
             except Exception as date_e: print(f"Erreur conversion date: {date_e}")

        ensure_data_dir() # S'assurer que le dossier 'data/' existe

        # Sauvegarde locale et préparation pour le téléchargement
        if DATA_FORMAT == 'excel':
            output = io.BytesIO()
            # Utiliser 'with' garantit la fermeture correcte de l'ExcelWriter
            with pd.ExcelWriter(output, engine='openpyxl') as writer:
                df.to_excel(writer, index=False, sheet_name='BalloonData')
            output.seek(0) # Rembobiner le buffer pour la lecture
            mimetype='application/vnd.openxmlformats-officedocument.spreadsheetml.sheet'
            download_name='balloon_data.xlsx'
            try: df.to_excel(DATA_FILENAME, index=False, engine='openpyxl') # Sauvegarde locale
            except Exception as save_e: print(f"Erreur sauvegarde locale Excel: {save_e}")
        elif DATA_FORMAT == 'csv':
            output = io.StringIO()
            df.to_csv(output, index=False, quoting=csv.QUOTE_NONNUMERIC)
            output_bytes = io.BytesIO(output.getvalue().encode('utf-8'))
            output_bytes.seek(0)
            output = output_bytes # Remplacer output pour send_file
            mimetype='text/csv'
            download_name='balloon_data.csv'
            try: df.to_csv(DATA_FILENAME, index=False, encoding='utf-8') # Sauvegarde locale
            except Exception as save_e: print(f"Erreur sauvegarde locale CSV: {save_e}")
        else:
            return "Format de données non supporté.", 500

        print(f"Préparation téléchargement ({DATA_FORMAT}) {len(history_copy)} lignes.")
        return send_file(output, mimetype=mimetype, download_name=download_name, as_attachment=True)

    except Exception as e:
        print(f"Erreur génération/envoi fichier ({DATA_FORMAT}): {e}")
        import traceback
        traceback.print_exc() # Imprime la trace complète de l'erreur serveur
        return f"Erreur serveur lors de la génération du fichier: {e}", 500

# --- Gestion SocketIO ---
# connect et disconnect restent identiques
@socketio.on('connect')
def handle_connect():
    sid = request.sid; print(f"Client connecté: {sid}")
    with data_lock:
        current_state = latest_data.copy(); history_subset = data_history[-100:]
    emit('update_data', current_state, room=sid)
    emit('initial_history', history_subset, room=sid)
    status = 'disconnected'; message = current_state.get('error', 'État inconnu')
    port_status = SERIAL_PORT # Utiliser le port configuré
    if ser and ser.is_open: status = 'connected'; message = None
    elif latest_data.get('error'): status = 'error'
    emit('serial_status', {'status': status, 'port': port_status, 'message': message}, room=sid)
    print(f"État initial envoyé à {sid}")

@socketio.on('disconnect')
def handle_disconnect():
    print(f"Client déconnecté: {request.sid}")

# --- Démarrage ---
# Reste identique
if __name__ == '__main__':
    ensure_data_dir()
    print("Démarrage serveur + thread série (Mode Multi-Lignes)...")
    serial_thread = threading.Thread(target=serial_reader_task, daemon=True)
    serial_thread.start()
    print(f"Serveur prêt sur http://0.0.0.0:5000 (Debug Flask/SocketIO: {DEBUG_MODE})")
    try:
        # Utiliser 'use_reloader=False' si DEBUG_MODE est True pour éviter les problèmes de thread double
        socketio.run(app, host='0.0.0.0', port=5000, debug=DEBUG_MODE, use_reloader=False, allow_unsafe_werkzeug=True)
    except KeyboardInterrupt: print("Arrêt demandé...")
    finally:
        print("Signalisation arrêt thread..."); stop_thread.set()
        if serial_thread: serial_thread.join(timeout=2); print(f"Thread série encore vivant: {serial_thread.is_alive()}")
        print("Serveur arrêté.")