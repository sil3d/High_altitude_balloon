import serial
import threading
import time
import json
import csv # Gardé si tu utilises le CSV
import os # Gardé si tu utilises le CSV
from datetime import datetime
from flask import Flask, render_template, send_file, request, jsonify # Ajout jsonify
from flask_socketio import SocketIO, emit
import pandas as pd # Pour la version Excel/DataFrame
import io # Pour la version Excel
import math # Pour haversine manuel

# --- Configuration ---
SERIAL_PORT ='COM3'
BAUD_RATE = 115200
DATA_FORMAT = 'excel'
DATA_FILENAME = 'data/balloon_data.xlsx'
DATA_DIR = 'data'
DEBUG_MODE = False
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
latest_data = {
    "timestamp": None, 'latitude': None, 'longitude': None, 'altitude_gps': None,
    'satellites': None, 'temperature': None, 'pressure': None, 'humidity': None,
    'altitude_bme': None, 'air_quality': None, 'tvoc': None, 'eco2': None,
    'ozone': None, 'uv_index': None, 'rssi': None, 'speed_kmh': None,
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

# parse_serial_data reste identique - il parse le format compact GPS|ENV|etc.
def parse_serial_data(compact_line):
    """
    Parse la chaîne compacte (ex: "GPS,NO_FIX|ENV,31.94,...|RSSI,-33"),
    retourne un dict avec toutes les clés.
    """
    data = { # Initialisation
        "timestamp": time.time(), 'latitude': None, 'longitude': None, 'altitude_gps': None,
        'satellites': None, 'temperature': None, 'pressure': None, 'humidity': None,
        'altitude_bme': None, 'air_quality': None, 'tvoc': None, 'eco2': None,
        'ozone': None, 'uv_index': None, 'rssi': None, 'speed_kmh': None, 'error': None
    }
    parts = compact_line.strip().split('|')
    valid_data_found_in_line = False

    for part in parts:
        if not part: continue
        elements = part.split(',')
        if len(elements) < 1: continue # Besoin au moins d'un header
        header = elements[0]
        values = elements[1:] # Peut être vide pour GPS,NO_FIX

        try:
            # MODIFICATION: Gérer "GPS,NO_FIX"
            if header == "GPS":
                if len(values) == 1 and values[0] == "NO_FIX":
                     # data['latitude'] = None # Déjà None par défaut
                     # data['longitude'] = None
                     # data['altitude_gps'] = None
                     # data['satellites'] = None
                     pass # GPS sans fix, on ne fait rien
                     # On ne met pas valid_data_found_in_line à True juste pour NO_FIX
                elif len(values) >= 4: # Cas avec fix GPS valide
                    try:
                        lat = float(values[0])
                        lon = float(values[1])
                        if lat != 0.0 or lon != 0.0:
                            data['latitude'] = lat
                            data['longitude'] = lon
                            data['altitude_gps'] = float(values[2])
                            data['satellites'] = int(values[3])
                            valid_data_found_in_line = True
                    except (ValueError, IndexError): pass # Ignore erreur parsing GPS
                else:
                     print(f"PY_WARN: Format GPS non reconnu: {part}")

            elif header == "ENV" and len(values) >= 4:
                try:
                    if values[0] != "ERR": data['temperature'] = float(values[0])
                    if values[1] != "ERR": data['pressure'] = float(values[1]) # Pression en Pa
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
                    # Gérer si la valeur est "ERR" textuelle
                    if values[0] != "ERR":
                         uv_val = float(values[0])
                         if uv_val >= 0: data['uv_index'] = uv_val
                    valid_data_found_in_line = True # On considère UV traité même si ERR
                except (ValueError, IndexError): pass
            elif header == "RSSI" and len(values) >= 1:
                try:
                    if values[0] != "ERR": data['rssi'] = int(values[0])
                except (ValueError, IndexError): pass
        except Exception as section_e:
             print(f"Erreur inattendue section {header}: {section_e}")

    if not valid_data_found_in_line:
        # Ne pas logger de warning ici, car une ligne GPS,NO_FIX seule est valide mais ne contient pas de "données utiles"
        # print(f"Warning: Aucune donnée utile reconnue dans: {compact_line}")
        pass

    #print(f"PY_DEBUG Parse Result: {data}") # Debug
    return data
def serial_reader_task():
    """Lit le port série ligne par ligne, identifie les lignes pertinentes,
       extrait les données et le RSSI, puis parse et émet."""
    global latest_data, data_history, ser
    print("Démarrage du thread de lecture série (Mode Multi-Lignes)...")

    last_rssi_value = None # Pour stocker le RSSI lu sur sa ligne séparée

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
                    # Ajout d'un timeout un peu plus long pour la connexion initiale peut aider
                    ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
                    print(f"Connecté avec succès à {SERIAL_PORT}") # DEBUG
                    with data_lock: latest_data['error'] = None
                    socketio.emit('serial_status', {'status': 'connected', 'port': SERIAL_PORT, 'message': None})
                    time.sleep(0.5) # Attendre un peu après la connexion
                    ser.reset_input_buffer() # Effacer ancien buffer
                    print("Buffer d'entrée réinitialisé.") # DEBUG
                except (serial.SerialException, PermissionError, FileNotFoundError) as e: # Ajout FileNotFoundError
                    serial_error_message = f"Échec connexion {SERIAL_PORT}: {e}"
                    print(f"ERREUR: {serial_error_message}") # DEBUG PLUS VISIBLE
                    ser = None
                    with data_lock:
                         if latest_data.get('error') != serial_error_message:
                             latest_data['error'] = serial_error_message
                             socketio.emit('update_data', latest_data) # S'assurer que l'erreur est émise
                    socketio.emit('serial_status', {'status': 'error', 'port': SERIAL_PORT, 'message': str(e)})
                    stop_thread.wait(5) # Attendre avant de réessayer
                    continue # Revenir au début de la boucle while pour réessayer

            # --- Phase 2: Lecture Ligne par Ligne ---
            if ser and ser.is_open:
                # Vérifier s'il y a des données en attente
                if ser.in_waiting > 0:
                    line = ser.readline()
                    # print(f"PY_DBG_BYTES: {line}") # Debug: voir les bytes bruts
                    try:
                        raw_line = line.decode('utf-8', errors='ignore').strip()

                        # !!! DEBUG CRUCIAL: Afficher CHAQUE ligne lue !!!
                        if raw_line: # Ne pas afficher les lignes vides
                            print(f"PY_READ_LINE: [{raw_line}]")

                        if raw_line:
                            # TEST 1: Est-ce la ligne de données brutes ?
                            data_prefix = "Donnees brutes: "
                            if raw_line.startswith(data_prefix):
                                compact_data_string = raw_line[len(data_prefix):]
                                print(f"PY_FOUND_DATA: Extracted [{compact_data_string}]") # DEBUG

                                parsed_data = parse_serial_data(compact_data_string)

                                if last_rssi_value is not None:
                                    parsed_data['rssi'] = last_rssi_value
                                    print(f"PY_APPLY_RSSI: Ajout RSSI={last_rssi_value}") # DEBUG
                                    # last_rssi_value = None # Décommentez si RSSI doit être consommé

                                speed = calculate_speed_kmh(
                                    parsed_data.get('latitude'), parsed_data.get('longitude'),
                                    parsed_data.get('timestamp')
                                )
                                parsed_data['speed_kmh'] = speed

                                with data_lock:
                                    latest_data = parsed_data.copy()
                                    data_history.append(latest_data)
                                    if len(data_history) > MAX_HISTORY: data_history.pop(0)
                                    # Effacer erreur SEULEMENT si on a des données valides
                                    if any(v is not None for k,v in parsed_data.items() if k not in ['timestamp', 'error', 'rssi', 'speed_kmh', 'latitude', 'longitude', 'altitude_gps', 'satellites']):
                                        if latest_data['error'] is not None:
                                            print("PY_CLEAR_ERROR: Erreur effacée car données reçues.") # DEBUG
                                            latest_data['error'] = None

                                print(f"PY_EMIT_UPDATE: {latest_data}") # DEBUG avant émission
                                socketio.emit('update_data', latest_data)
                                # Émettre le statut 'receiving' SEULEMENT si on reçoit vraiment
                                socketio.emit('serial_status', {'status': 'receiving', 'port': SERIAL_PORT, 'message': None})


                            # TEST 2: Est-ce la ligne RSSI ?
                            # Format attendu: "RSSI: -77 | SNR: 9.75"
                            elif raw_line.startswith("RSSI:"):
                                try:
                                    # Isoler la partie RSSI avant le |
                                    rssi_part = raw_line.split('|')[0] # Prend "RSSI: -77 "
                                    rssi_value_str = rssi_part.split(':')[1].strip() # Prend "-77"
                                    last_rssi_value = int(rssi_value_str) # Stocke la valeur
                                    print(f"PY_FOUND_RSSI: Stored {last_rssi_value}") # DEBUG
                                except (IndexError, ValueError) as e_rssi:
                                    print(f"PY_WARN: Impossible de parser RSSI: {raw_line} - {e_rssi}")

                            # Ignorer les autres lignes (commentaires, etc.)
                            # else:
                            #    # Déjà affiché par PY_READ_LINE
                            #    pass


                    except UnicodeDecodeError as ude:
                        print(f"ERREUR DECODAGE: {ude} pour bytes: {line}")
                    except serial.SerialException as e:
                        # ... (gestion erreur identique) ...
                        serial_error_message = f"Erreur série pendant lecture: {e}"
                        print(f"ERREUR LECTURE: {serial_error_message}")
                        if ser:
                            try: ser.close()
                            except Exception: pass
                        ser = None
                        with data_lock: latest_data['error'] = serial_error_message
                        socketio.emit('serial_status', {'status': 'error', 'port': SERIAL_PORT, 'message': str(e)})
                        socketio.emit('update_data', latest_data)
                        stop_thread.wait(2)
                    except Exception as e_proc:
                         print(f"Erreur traitement ligne '{raw_line}': {e_proc}")
                         # Mettre à jour l'erreur dans l'état global peut être utile
                         with data_lock:
                             error_state = latest_data.copy(); error_state['error'] = f"Erreur proc: {e_proc}"; error_state['timestamp'] = time.time()
                             latest_data = error_state
                         socketio.emit('update_data', latest_data)

                else:
                    # Pas de données en attente, petite pause pour ne pas surcharger le CPU
                    stop_thread.wait(0.05) # Pause plus courte pour réactivité
            else:
                 # Si on arrive ici, c'est que la connexion a échoué plus haut et ser est None
                 # Le message d'erreur a déjà été affiché. La boucle attend 5s avant de réessayer.
                 # print("Warning: Port série non disponible ou fermé.") # Redondant
                 stop_thread.wait(1) # Petite pause supplémentaire

        # Gérer les exceptions majeures hors de la boucle de lecture
        except Exception as e_main:
            serial_error_message = f"Erreur majeure thread série: {e_main}"
            print(f"ERREUR MAJEURE: {serial_error_message}")
            # Tenter de fermer proprement si possible
            if ser:
                try: ser.close()
                except Exception: pass
            ser = None
            # Mettre à jour l'état global avec l'erreur majeure
            with data_lock: latest_data['error'] = serial_error_message
            socketio.emit('serial_status', {'status': 'error', 'port': SERIAL_PORT, 'message': str(e_main)})
            socketio.emit('update_data', latest_data)
            # Attendre avant de potentiellement relancer la boucle (si elle n'est pas arrêtée)
            stop_thread.wait(5)

    # --- Nettoyage ---
    print("Arrêt thread série demandé.")
    if ser and ser.is_open:
        try: ser.close(); print(f"Port {SERIAL_PORT} fermé.")
        except Exception as e: print(f"Erreur fermeture port: {e}")
    ser = None
    print("Thread série terminé.")


# --- Routes Flask ---
# /, /download restent identiques
@app.route('/')
def index():
    return render_template('index.html')

@app.route('/download')
def download_data():
    global data_history
    with data_lock:
        if not data_history: return "Aucune donnée historique.", 404
        history_copy = list(data_history)
    try:
        df = pd.DataFrame(history_copy)
        if 'timestamp' in df.columns:
             # Correction fuseau horaire Sénégal = UTC+0
             try: df['timestamp_iso'] = pd.to_datetime(df['timestamp'], unit='s').dt.strftime('%Y-%m-%d %H:%M:%S')
             except Exception as date_e: print(f"Erreur conversion date: {date_e}")

        ensure_data_dir()
        if DATA_FORMAT == 'excel':
            output = io.BytesIO()
            with pd.ExcelWriter(output, engine='openpyxl') as writer: df.to_excel(writer, index=False, sheet_name='BalloonData')
            # Note: ExcelWriter avec 'with' gère la fermeture/sauvegarde dans le buffer
            output.seek(0)
            mimetype='application/vnd.openxmlformats-officedocument.spreadsheetml.sheet'
            download_name='balloon_data.xlsx'
            try: df.to_excel(DATA_FILENAME, index=False, engine='openpyxl')
            except Exception as save_e: print(f"Erreur sauvegarde locale Excel: {save_e}")
        elif DATA_FORMAT == 'csv':
            output = io.StringIO(); df.to_csv(output, index=False, quoting=csv.QUOTE_NONNUMERIC)
            output_bytes = io.BytesIO(output.getvalue().encode('utf-8')); output_bytes.seek(0); output = output_bytes
            mimetype='text/csv'; download_name='balloon_data.csv'
            try: df.to_csv(DATA_FILENAME, index=False, encoding='utf-8')
            except Exception as save_e: print(f"Erreur sauvegarde locale CSV: {save_e}")
        else: return "Format non supporté.", 500
        print(f"Préparation téléchargement ({DATA_FORMAT}) {len(history_copy)} lignes.")
        return send_file(output, mimetype=mimetype, download_name=download_name, as_attachment=True)
    except Exception as e:
        print(f"Erreur génération/envoi fichier ({DATA_FORMAT}): {e}")
        import traceback; traceback.print_exc()
        return f"Erreur serveur: {e}", 500

# --- Gestion SocketIO ---
# connect, disconnect restent identiques
@socketio.on('connect')
def handle_connect():
    sid = request.sid; print(f"Client connecté: {sid}")
    with data_lock:
        current_state = latest_data.copy(); history_subset = data_history[-100:]
    emit('update_data', current_state, room=sid)
    emit('initial_history', history_subset, room=sid)
    status = 'disconnected'; message = current_state.get('error', 'État inconnu')
    if ser and ser.is_open: status = 'connected'; message = None
    elif latest_data.get('error'): status = 'error'
    emit('serial_status', {'status': status, 'port': SERIAL_PORT, 'message': message}, room=sid)
    print(f"État initial envoyé à {sid}")

@socketio.on('disconnect')
def handle_disconnect():
    print(f"Client déconnecté: {request.sid}")

# --- Démarrage ---
# Reste identique
if __name__ == '__main__':
    ensure_data_dir()
    print("Démarrage serveur + thread série (Mode Multi-Lignes)...")
    serial_thread = threading.Thread(target=serial_reader_task, daemon=True); serial_thread.start()
    print(f"Serveur prêt sur http://0.0.0.0:5000 (Debug: {DEBUG_MODE})")
    try:
        socketio.run(app, host='0.0.0.0', port=5000, debug=DEBUG_MODE, allow_unsafe_werkzeug=DEBUG_MODE)
    except KeyboardInterrupt: print("Arrêt demandé...")
    finally:
        print("Signalisation arrêt thread..."); stop_thread.set()
        if serial_thread: serial_thread.join(timeout=2); print(f"Thread vivant: {serial_thread.is_alive()}")
        print("Serveur arrêté.")