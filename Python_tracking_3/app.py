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
import openpyxl # Nécessaire pour to_excel
import math # Pour haversine manuel

# --- Configuration ---
SERIAL_PORT ='COM5'
BAUD_RATE = 115200
# Choisir CSV ou Excel pour le téléchargement
# DATA_FORMAT = 'csv'
# DATA_FILENAME = 'data/balloon_data.csv'
DATA_FORMAT = 'excel'
DATA_FILENAME = 'data/balloon_data.xlsx'
DATA_DIR = 'data'

DEBUG_MODE = False # Mettre False pour éviter les rechargements et problèmes de thread
MAX_HISTORY = 500 # Nombre max de points gardés en mémoire et dans le fichier

# --- Initialisation Flask et SocketIO ---
app = Flask(__name__)
app.config['SECRET_KEY'] = 'secret_key_pour_le_ballon_tracker!' # Changez ceci
socketio = SocketIO(app, async_mode='threading') # Ou 'eventlet'/'gevent' si installés

# --- Variables Globales ---
ser = None
serial_thread = None
stop_thread = threading.Event()
data_lock = threading.Lock()
latest_data = { # Initialise toutes les clés à None
    "timestamp": None, 'latitude': None, 'longitude': None, 'altitude_gps': None,
    'satellites': None, 'temperature': None, 'pressure': None, 'humidity': None,
    'altitude_bme': None, 'air_quality': None, 'tvoc': None, 'eco2': None,
    'ozone': None, 'uv_index': None, 'rssi': None, 'speed_kmh': None,
    'error': "Initialisation..."
}
data_history = [] # Liste de dictionnaires (comme latest_data)
previous_location_for_speed = None

# --- Fonctions Utilitaires ---

def ensure_data_dir():
    """Crée le dossier de données s'il n'existe pas."""
    if not os.path.exists(DATA_DIR):
        try:
            os.makedirs(DATA_DIR)
            print(f"Dossier '{DATA_DIR}' créé.")
        except OSError as e:
            print(f"Erreur lors de la création du dossier '{DATA_DIR}': {e}")

def haversine_manual(lat1, lon1, lat2, lon2):
    """Calcule la distance Haversine en mètres."""
    R = 6371000 # Rayon Terre en mètres
    phi1, phi2 = math.radians(lat1), math.radians(lat2)
    dphi = math.radians(lat2 - lat1)
    dlambda = math.radians(lon2 - lon1)
    a = math.sin(dphi/2)**2 + math.cos(phi1)*math.cos(phi2)*math.sin(dlambda/2)**2
    c = 2 * math.atan2(math.sqrt(a), math.sqrt(1-a))
    return R * c

def calculate_speed_kmh(current_lat, current_lon, current_time):
    """Calcule la vitesse en km/h. Nécessite global previous_location_for_speed."""
    global previous_location_for_speed
    # Vérifie si les données actuelles sont valides (nombres)
    if not all(isinstance(x, (int, float)) for x in [current_lat, current_lon]):
        return None

    current_coords = (current_lat, current_lon)
    # Assumer current_time est un timestamp UNIX float
    try:
        current_dt = datetime.fromtimestamp(current_time)
    except (TypeError, ValueError):
        print(f"Timestamp invalide pour calcul vitesse: {current_time}")
        return None # Timestamp invalide

    if not previous_location_for_speed:
         # Premier point GPS valide reçu
         previous_location_for_speed = {"lat": current_lat, "lon": current_lon, "dt": current_dt, "speed_kmh": 0.0}
         return 0.0

    try:
        prev_lat = previous_location_for_speed["lat"]
        prev_lon = previous_location_for_speed["lon"]
        prev_dt = previous_location_for_speed["dt"]

        # Vérifier que les coordonnées précédentes sont aussi valides
        if not all(isinstance(x, (int, float)) for x in [prev_lat, prev_lon]):
             print("Coordonnées précédentes invalides pour calcul vitesse.")
             # Mettre à jour avec les nouvelles coordonnées valides pour le prochain calcul
             previous_location_for_speed = {"lat": current_lat, "lon": current_lon, "dt": current_dt, "speed_kmh": 0.0}
             return 0.0

        distance_m = haversine_manual(prev_lat, prev_lon, current_lat, current_lon)
        time_diff_s = (current_dt - prev_dt).total_seconds()

        # Éviter division par zéro ou intervalles trop courts
        if time_diff_s < 0.5:
             # Retourner la vitesse précédente si l'intervalle est trop court
             return previous_location_for_speed.get("speed_kmh", 0.0)

        speed_mps = distance_m / time_diff_s
        speed_kmh = round(speed_mps * 3.6, 2)

        # Mettre à jour pour la prochaine itération
        previous_location_for_speed = {"lat": current_lat, "lon": current_lon, "dt": current_dt, "speed_kmh": speed_kmh}
        return speed_kmh

    except Exception as e:
        print(f"Erreur calcul vitesse: {e}")
        # Réinitialiser prudemment en cas d'erreur inattendue
        previous_location_for_speed = {"lat": current_lat, "lon": current_lon, "dt": current_dt, "speed_kmh": 0.0}
        return None

def parse_serial_data(line):
    """
    Parse la ligne série, renvoie un dict avec toutes les clés, même si données absentes (valeur=None).
    Ne lève pas d'erreur si une section est manquante.
    """
    # Initialise avec toutes les clés possibles à None et le timestamp
    data = {
        "timestamp": time.time(), 'latitude': None, 'longitude': None, 'altitude_gps': None,
        'satellites': None, 'temperature': None, 'pressure': None, 'humidity': None,
        'altitude_bme': None, 'air_quality': None, 'tvoc': None, 'eco2': None,
        'ozone': None, 'uv_index': None, 'rssi': None, 'speed_kmh': None, # Sera ajouté après
        'error': None # Réinitialiser l'erreur potentielle
    }
    raw_line = line.strip() # Garder une copie brute pour debug
    parts = raw_line.split('|')
    valid_data_found_in_line = False # Flag pour savoir si on a parsé au moins une section valide

    for part in parts:
        if not part: continue
        elements = part.split(',')
        if len(elements) < 2: continue # Besoin header + valeur(s)
        header = elements[0]
        values = elements[1:]

        try: # Try/Except par section pour ignorer les sections mal formées
            if header == "GPS" and len(values) >= 4:
                try:
                    lat = float(values[0])
                    lon = float(values[1])
                    if lat != 0.0 or lon != 0.0: # Considérer 0,0 comme invalide
                        data['latitude'] = lat
                        data['longitude'] = lon
                        data['altitude_gps'] = float(values[2])
                        data['satellites'] = int(values[3])
                        valid_data_found_in_line = True
                except (ValueError, IndexError): pass # Ignore les erreurs GPS
            elif header == "ENV" and len(values) >= 4:
                try:
                    if values[0] != "ERR": data['temperature'] = float(values[0])
                    if values[1] != "ERR": data['pressure'] = float(values[1])
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
                    uv_val = float(values[0])
                    if uv_val >= 0: data['uv_index'] = uv_val
                    valid_data_found_in_line = True
                except (ValueError, IndexError): pass
            elif header == "RSSI" and len(values) >= 1:
                try:
                    if values[0] != "ERR": data['rssi'] = int(values[0])
                    # RSSI est informatif, ne met pas valid_data_found_in_line à True seul
                except (ValueError, IndexError): pass
        except Exception as section_e:
             # Erreur inattendue dans le traitement d'une section
             print(f"Erreur inattendue parsing section {header}: {section_e}")
             # Continue avec les autres sections

    if not valid_data_found_in_line:
        print(f"Warning: Aucune donnée valide reconnue dans la ligne: {raw_line}")
        # Garder error=None car ce n'est pas une erreur de parsing, juste pas de données utiles
        # Le timestamp est quand même valide

    return data


def serial_reader_task():
    """Lit le port série, parse, met à jour latest_data/history, emit via SocketIO."""
    global latest_data, data_history, ser
    print("Démarrage du thread de lecture série...")

    while not stop_thread.is_set():
        serial_error_message = None # Message d'erreur spécifique à cette itération
        try:
            # --- Phase 1: Connexion/Reconnexion ---
            if ser is None or not ser.is_open:
                if ser: # Nettoyer l'ancienne instance
                    try: ser.close()
                    except Exception: pass
                try:
                    #print(f"Tentative connexion {SERIAL_PORT}...") # Moins verbeux
                    ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
                    print(f"Connecté à {SERIAL_PORT}")
                    with data_lock: latest_data['error'] = None # Effacer ancienne erreur
                    socketio.emit('serial_status', {'status': 'connected', 'port': SERIAL_PORT, 'message': None})
                    time.sleep(0.5)
                    ser.reset_input_buffer()
                except (serial.SerialException, PermissionError) as e:
                    serial_error_message = f"Échec connexion {SERIAL_PORT}: {e}"
                    # print(serial_error_message) # Affiché une seule fois par le handler d'erreur
                    ser = None
                    # Mettre à jour l'état global seulement si l'erreur change ou est nouvelle
                    with data_lock:
                         if latest_data.get('error') != serial_error_message:
                             latest_data['error'] = serial_error_message
                             # Emettre le changement d'état d'erreur
                             socketio.emit('update_data', latest_data)
                    socketio.emit('serial_status', {'status': 'error', 'port': SERIAL_PORT, 'message': str(e)})
                    stop_thread.wait(5) # Attendre avant de retenter
                    continue # Revenir au début de la boucle while

            # --- Phase 2: Lecture ---
            if ser and ser.is_open: # Vérifier à nouveau au cas où il aurait été fermé entre temps
                if ser.in_waiting > 0:
                    line = ser.readline()
                    try:
                        raw_line = line.decode('utf-8', errors='ignore').strip()
                        if raw_line:
                            parsed_data = parse_serial_data(raw_line)

                            # Calculer la vitesse
                            speed = calculate_speed_kmh(
                                parsed_data.get('latitude'), parsed_data.get('longitude'),
                                parsed_data.get('timestamp')
                            )
                            parsed_data['speed_kmh'] = speed

                            with data_lock:
                                latest_data = parsed_data.copy() # Mettre à jour
                                data_history.append(latest_data)
                                if len(data_history) > MAX_HISTORY:
                                    data_history.pop(0)
                                # Effacer l'erreur s'il y en avait une avant et qu'on reçoit des données valides
                                if latest_data['error'] is not None and any(v is not None for k, v in latest_data.items() if k not in ['timestamp', 'error', 'speed_kmh', 'rssi']):
                                    latest_data['error'] = None

                            socketio.emit('update_data', latest_data)
                            # Potentiellement effacer l'indicateur d'erreur série si on reçoit des données
                            socketio.emit('serial_status', {'status': 'receiving', 'port': SERIAL_PORT, 'message': None})


                    except serial.SerialException as e:
                        serial_error_message = f"Erreur série pendant lecture: {e}"
                        print(serial_error_message)
                        if ser:
                            try: ser.close()
                            except Exception: pass
                        ser = None # Force reconnexion
                        with data_lock: latest_data['error'] = serial_error_message
                        socketio.emit('serial_status', {'status': 'error', 'port': SERIAL_PORT, 'message': str(e)})
                        socketio.emit('update_data', latest_data) # Envoyer l'erreur
                        stop_thread.wait(2) # Attendre un peu avant reconnexion
                    except UnicodeDecodeError:
                        print(f"Erreur décodage ligne série: {line}") # Ignorer
                    except Exception as e_proc:
                         # Erreur inattendue pendant le traitement (parsing, vitesse, ...)
                         print(f"Erreur traitement ligne '{raw_line}': {e_proc}")
                         with data_lock:
                             # Créer un état d'erreur spécifique sans écraser les données précédentes si possible
                             error_state = latest_data.copy()
                             error_state['error'] = f"Erreur traitement: {e_proc}"
                             error_state['timestamp'] = time.time()
                             latest_data = error_state # Mettre à jour avec l'erreur
                         socketio.emit('update_data', latest_data) # Notifier le client de l'erreur

                else:
                    # Pas de données en attente, petite pause
                    stop_thread.wait(0.1)
            else:
                 # Cas où ser est devenu None ou non ouvert (ne devrait pas arriver si la logique est correcte)
                 print("Warning: Port série non disponible pour lecture.")
                 stop_thread.wait(1)


        except Exception as e_main:
            # Erreur majeure dans la boucle principale
            serial_error_message = f"Erreur majeure thread série: {e_main}"
            print(serial_error_message)
            if ser:
                 try: ser.close()
                 except Exception: pass
            ser = None
            with data_lock: latest_data['error'] = serial_error_message
            socketio.emit('serial_status', {'status': 'error', 'port': SERIAL_PORT, 'message': str(e_main)})
            socketio.emit('update_data', latest_data)
            stop_thread.wait(5) # Attente avant de redémarrer la boucle

    # --- Nettoyage ---
    print("Arrêt thread série demandé.")
    if ser and ser.is_open:
        try:
            ser.close()
            print(f"Port série {SERIAL_PORT} fermé.")
        except Exception as e: print(f"Erreur fermeture finale port série: {e}")
    ser = None
    print("Thread série terminé.")

# --- Routes Flask ---
@app.route('/')
def index():
    return render_template('index.html')

@app.route('/download')
def download_data():
    """Génère et envoie le fichier de données (CSV ou Excel)."""
    global data_history
    with data_lock:
        if not data_history:
            return "Aucune donnée historique à télécharger.", 404
        # Créer une copie pour travailler dessus sans bloquer la réception
        history_copy = list(data_history)

    try:
        df = pd.DataFrame(history_copy)
        # Optionnel: Mettre le timestamp en format lisible
        if 'timestamp' in df.columns:
             df['timestamp_iso'] = pd.to_datetime(df['timestamp'], unit='s').dt.tz_localize('UTC').dt.tz_convert('Africa/Dakar').dt.strftime('%Y-%m-%d %H:%M:%S')

        ensure_data_dir() # S'assurer que le dossier existe

        if DATA_FORMAT == 'excel':
            output = io.BytesIO()
            # Utiliser openpyxl explicitement
            writer = pd.ExcelWriter(output, engine='openpyxl')
            df.to_excel(writer, index=False, sheet_name='BalloonData')
            # Fermer writer pour écrire dans BytesIO (important!)
            # writer.save() # Méthode dépréciée
            writer.close()
            output.seek(0)
            mimetype='application/vnd.openxmlformats-officedocument.spreadsheetml.sheet'
            download_name='balloon_data.xlsx'
            # Sauvegarde locale optionnelle pour backup/debug
            try: df.to_excel(DATA_FILENAME, index=False, engine='openpyxl')
            except Exception as save_e: print(f"Erreur sauvegarde locale Excel: {save_e}")

        elif DATA_FORMAT == 'csv':
            output = io.StringIO()
            df.to_csv(output, index=False, quoting=csv.QUOTE_NONNUMERIC)
            output_bytes = io.BytesIO(output.getvalue().encode('utf-8')) # Convertir en BytesIO pour send_file
            output_bytes.seek(0)
            output = output_bytes # Réassigner pour send_file
            mimetype='text/csv'
            download_name='balloon_data.csv'
            # Sauvegarde locale optionnelle
            try: df.to_csv(DATA_FILENAME, index=False, encoding='utf-8')
            except Exception as save_e: print(f"Erreur sauvegarde locale CSV: {save_e}")
        else:
             return "Format de données non supporté.", 500

        print(f"Préparation téléchargement ({DATA_FORMAT}) avec {len(history_copy)} lignes.")
        return send_file(output, mimetype=mimetype, download_name=download_name, as_attachment=True)

    except Exception as e:
        print(f"Erreur lors de la génération/envoi du fichier ({DATA_FORMAT}): {e}")
        import traceback
        traceback.print_exc() # Imprimer la trace complète de l'erreur
        return f"Erreur serveur lors de la génération du fichier: {e}", 500


# --- Gestion SocketIO ---
@socketio.on('connect')
def handle_connect():
    sid = request.sid
    print(f"Client connecté: {sid}")
    with data_lock:
        # Envoyer l'état actuel complet
        current_state = latest_data.copy()
        history_subset = data_history[-100:] # Envoyer les 100 derniers points
    # Envoyer séparément pour clarté côté client
    emit('update_data', current_state, room=sid)
    emit('initial_history', history_subset, room=sid)
    # Envoyer le statut série actuel
    status = 'disconnected'
    message = current_state.get('error', 'État inconnu')
    if ser and ser.is_open:
        status = 'connected' # Ou 'receiving' si on veut être plus précis
        message = None
    elif latest_data.get('error'):
         status = 'error'

    emit('serial_status', {'status': status, 'port': SERIAL_PORT, 'message': message}, room=sid)
    print(f"État initial envoyé à {sid}")


@socketio.on('disconnect')
def handle_disconnect():
    print(f"Client déconnecté: {request.sid}")

# --- Démarrage ---
if __name__ == '__main__':
    ensure_data_dir() # Créer dossier data au démarrage si besoin
    print("Démarrage du serveur et du thread série...")
    serial_thread = threading.Thread(target=serial_reader_task, daemon=True)
    serial_thread.start()

    print(f"Serveur Flask-SocketIO prêt sur http://0.0.0.0:5000")
    print(f"Mode Debug: {'Activé' if DEBUG_MODE else 'Désactivé'}")
    try:
        # Utiliser debug=True avec précaution car il redémarre le serveur et peut tuer/redémarrer le thread série
        socketio.run(app, host='0.0.0.0', port=5000, debug=DEBUG_MODE,
                     allow_unsafe_werkzeug=True if DEBUG_MODE else False)
    except KeyboardInterrupt:
        print("Arrêt demandé (Ctrl+C)...")
    finally:
        print("Signalisation arrêt thread série...")
        stop_thread.set()
        if serial_thread:
             serial_thread.join(timeout=2)
             if serial_thread.is_alive(): print("Warning: Thread série non terminé.")
        print("Serveur arrêté.")