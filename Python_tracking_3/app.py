import serial
import threading
import time
import json
import csv
import os
from datetime import datetime
from flask import Flask, render_template, send_file, request
from flask_socketio import SocketIO, emit # Importation de Flask-SocketIO
from haversine import haversine, Unit # Pour le calcul de distance

# --- Configuration ---
SERIAL_PORT = 'COM4'  # MODIFIEZ CECI si votre port est différent
BAUD_RATE = 115200
CSV_FILENAME = 'data_log.csv'
DEBUG_MODE = True # Mettre à False en production

# --- Variables Globales ---
# Utiliser un verrou pour l'accès concurrentiel aux données partagées
data_lock = threading.Lock()
latest_data = {
    "raw": "En attente de données...",
    "parsed": None, # Contiendra le dictionnaire des données parsées
    "timestamp": None,
    "speed_kmh": 0.0
}
# Stocker l'historique récent pour le graphique et le calcul de vitesse
data_history = []
MAX_HISTORY = 200 # Garder les N derniers points pour l'affichage/calcul

# --- Initialisation Flask et SocketIO ---
app = Flask(__name__)
app.config['SECRET_KEY'] = 'votre_cle_secrete_ici_tres_importante!' # Important pour Flask et SocketIO
# Utiliser 'threading' car pyserial est bloquant et nous utilisons un thread séparé
# Cela évite d'avoir besoin de bibliothèques asynchrones comme eventlet ou gevent pour ce cas simple
socketio = SocketIO(app, async_mode='threading')

# --- Fonctions Utilitaires ---
# Les fonctions parse_arduino_data, calculate_speed, write_to_csv restent identiques
# ... (Collez ici les fonctions parse_arduino_data, calculate_speed, write_to_csv de la version précédente) ...
def parse_arduino_data(data_str):
    """Parse la chaîne de données reçue de l'Arduino."""
    parsed = {
        "gps": {"lat": None, "lon": None, "alt": None, "satellites": None, "time": None, "fix": False},
        "env": {"temp": None, "pressure": None, "humidity": None, "altitude": None},
        "air": {"quality_idx": None, "quality_desc": "?", "tvoc": None, "eco2": None},
        "other": {"ozone": None, "uv": None}
    }
    try:
        parts = data_str.split('|')
        for part in parts:
            if part.startswith("GPS,"):
                gps_data = part[4:]
                if gps_data != "NO_FIX":
                    fields = gps_data.split(',')
                    if len(fields) == 5:
                        parsed["gps"]["lat"] = float(fields[0])
                        parsed["gps"]["lon"] = float(fields[1])
                        parsed["gps"]["alt"] = float(fields[2])
                        parsed["gps"]["satellites"] = int(fields[3])
                        parsed["gps"]["time"] = fields[4]
                        parsed["gps"]["fix"] = True
                else:
                    parsed["gps"]["fix"] = False
            elif part.startswith("ENV,"):
                env_data = part[4:].split(',')
                if len(env_data) == 4:
                    parsed["env"]["temp"] = float(env_data[0])
                    parsed["env"]["pressure"] = int(env_data[1])
                    parsed["env"]["humidity"] = float(env_data[2])
                    parsed["env"]["altitude"] = float(env_data[3])
            elif part.startswith("AIR,"):
                air_data = part[4:].split(',')
                if len(air_data) == 3:
                    parsed["air"]["quality_idx"] = int(air_data[0])
                    parsed["air"]["tvoc"] = int(air_data[1])
                    parsed["air"]["eco2"] = int(air_data[2])
                    # Description qualité air
                    quality_map = {1: "Excellent", 2: "Bon", 3: "Moyen", 4: "Mauvais", 5: "Malsain"}
                    parsed["air"]["quality_desc"] = quality_map.get(parsed["air"]["quality_idx"], "?")
            elif part.startswith("OZ,"):
                oz_data = part[3:]
                parsed["other"]["ozone"] = int(oz_data)
            elif part.startswith("UV,"):
                uv_data = part[3:]
                parsed["other"]["uv"] = float(uv_data)
        return parsed
    except Exception as e:
        print(f"Erreur de parsing: {e} - Données: {data_str}")
        return None # Retourne None si le parsing échoue

def calculate_speed(current_data_parsed, history):
    """Calcule la vitesse basée sur le dernier point GPS valide dans l'historique."""
    # Attention : la fonction originale prenait `current_data` (le dict complet)
    # Assurons-nous de passer les bonnes infos si on change l'appel
    if not current_data_parsed or not current_data_parsed["gps"]["fix"]:
        return 0.0

    last_valid_point = None
    # Trouver le point précédent le plus récent avec un fix GPS
    for i in range(len(history) - 1, -1, -1): # On cherche dans l'historique avant le point actuel
         prev_point_data = history[i].get("parsed")
         if prev_point_data and prev_point_data["gps"]["fix"]:
             last_valid_point = history[i]
             break

    if not last_valid_point:
        return 0.0

    try:
        # Coordonnées actuelles et précédentes
        current_coords = (current_data_parsed["gps"]["lat"], current_data_parsed["gps"]["lon"])
        prev_coords = (last_valid_point["parsed"]["gps"]["lat"], last_valid_point["parsed"]["gps"]["lon"])

        # Temps actuel (besoin du timestamp du point courant) et précédent
        # On suppose que le timestamp est ajouté au moment de la réception, AVANT l'appel à cette fonction
        # Trouver le timestamp courant correspondant à current_data_parsed
        # NOTE: C'est plus simple si la fonction `calculate_speed` est appelée avec le *point de donnée complet*
        # (incluant 'timestamp') plutôt que juste `parsed`. Revoyons l'appel dans `serial_reader_task`.
        # -> Modification : On va appeler calculate_speed AVANT d'ajouter le point à l'historique,
        #    en lui passant le point courant (avec timestamp) et l'historique *actuel*.

        # Correction : l'appel sera fait après ajout à history, donc on compare history[-1] et last_valid_point
        if len(history) < 1: return 0.0 # Ne devrait pas arriver si on appelle après ajout
        current_point = history[-1] # Le point qu'on vient d'ajouter
        current_time_s = current_point["timestamp"]
        prev_time_s = last_valid_point["timestamp"]

        # Calculs
        distance_m = haversine(prev_coords, current_coords, unit=Unit.METERS)
        time_diff_s = current_time_s - prev_time_s

        if time_diff_s <= 0: # Éviter division par zéro ou temps négatif
             return 0.0

        speed_mps = distance_m / time_diff_s
        speed_kmh = speed_mps * 3.6
        return round(speed_kmh, 2)

    except Exception as e:
        print(f"Erreur calcul vitesse: {e}")
        return 0.0

def write_to_csv(data_point):
    """Ajoute une ligne de données au fichier CSV."""
    file_exists = os.path.isfile(CSV_FILENAME)
    fieldnames = [
        'timestamp_iso', 'timestamp_unix', 'raw_data', 'latitude', 'longitude', 'altitude_gps', 'satellites',
        'gps_time', 'gps_fix', 'temperature', 'pressure', 'humidity', 'altitude_baro',
        'air_quality_idx', 'air_quality_desc', 'tvoc', 'eco2', 'ozone', 'uv_index', 'speed_kmh'
    ]

    flat_data = {
        'timestamp_iso': datetime.fromtimestamp(data_point['timestamp']).isoformat(),
        'timestamp_unix': data_point['timestamp'],
        'raw_data': data_point.get('raw', ''),
        'speed_kmh': data_point.get('speed_kmh', 0.0)
    }
    parsed = data_point.get('parsed')
    if parsed:
        flat_data.update({
            'latitude': parsed['gps']['lat'], 'longitude': parsed['gps']['lon'],
            'altitude_gps': parsed['gps']['alt'], 'satellites': parsed['gps']['satellites'],
            'gps_time': parsed['gps']['time'], 'gps_fix': parsed['gps']['fix'],
            'temperature': parsed['env']['temp'], 'pressure': parsed['env']['pressure'],
            'humidity': parsed['env']['humidity'], 'altitude_baro': parsed['env']['altitude'],
            'air_quality_idx': parsed['air']['quality_idx'], 'air_quality_desc': parsed['air']['quality_desc'],
            'tvoc': parsed['air']['tvoc'], 'eco2': parsed['air']['eco2'],
            'ozone': parsed['other']['ozone'], 'uv_index': parsed['other']['uv']
        })
    else:
         for key in fieldnames:
             if key not in flat_data:
                 flat_data[key] = None

    try:
        with open(CSV_FILENAME, 'a', newline='', encoding='utf-8') as csvfile:
            writer = csv.DictWriter(csvfile, fieldnames=fieldnames)
            if not file_exists or os.path.getsize(CSV_FILENAME) == 0:
                writer.writeheader()
            row_to_write = {k: flat_data.get(k) for k in fieldnames}
            writer.writerow(row_to_write)
    except IOError as e:
        print(f"Erreur d'écriture CSV: {e}")
    except Exception as e:
         print(f"Erreur CSV inattendue: {e}, Données: {flat_data}")

# --- Tâche de Lecture Série en Arrière-plan ---
def serial_reader_task():
    """Lit les données du port série et met à jour les variables globales."""
    global latest_data, data_history
    ser = None
    print("Démarrage du thread de lecture série...") # Message de démarrage
    while True:
        try:
            if ser is None:
                print(f"Tentative de connexion au port série {SERIAL_PORT}...")
                ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=2)
                print(f"Connecté à {SERIAL_PORT}")

            if ser.in_waiting > 0:
                line = ser.readline()
                try:
                    raw_line = line.decode('utf-8', errors='ignore').strip()
                except UnicodeDecodeError:
                     print(f"Erreur de décodage Unicode, ligne ignorée: {line}")
                     continue

                if raw_line and "GPS," in raw_line: # Filtrer un peu les lignes potentiellement vides/corrompues
                    # print(f"Reçu: {raw_line}") # Décommenter pour débug verbeux
                    current_timestamp = time.time()
                    parsed = parse_arduino_data(raw_line)

                    if parsed:
                         # Créer le nouveau point de donnée SANS la vitesse pour l'instant
                        new_data_point = {
                            "raw": raw_line,
                            "parsed": parsed,
                            "timestamp": current_timestamp,
                            "speed_kmh": 0.0 # Initialisation
                        }

                        speed = 0.0 # Vitesse par défaut
                        with data_lock:
                            # 1. Ajouter le nouveau point (sans vitesse calculée) à l'historique
                            data_history.append(new_data_point)
                            if len(data_history) > MAX_HISTORY:
                                data_history.pop(0)

                            # 2. Calculer la vitesse en utilisant le dernier point ajouté et l'historique précédent
                            #    On passe `parsed` et l'historique *actuel* (qui contient le nouveau point)
                            speed = calculate_speed(parsed, data_history)

                            # 3. Mettre à jour la vitesse dans le point qu'on vient d'ajouter à l'historique
                            #    et dans latest_data
                            data_history[-1]["speed_kmh"] = speed # Met à jour le dernier élément de la liste
                            new_data_point["speed_kmh"] = speed # S'assurer que la vitesse est aussi dans la copie

                            # 4. Mettre à jour les données globales les plus récentes
                            latest_data = new_data_point.copy() # Utiliser la copie mise à jour avec la vitesse

                        # Écrire dans le CSV (maintenant avec la vitesse)
                        write_to_csv(new_data_point)

                        # Envoyer les nouvelles données via SocketIO à tous les clients
                        # socketio.emit est thread-safe quand async_mode='threading'
                        socketio.emit('update', {'data': new_data_point})
                        # print("Données envoyées via SocketIO") # Décommenter pour débug

                    else:
                         print(f"Parsing échoué pour: {raw_line}")
                         with data_lock:
                              latest_data["raw"] = raw_line
                              latest_data["parsed"] = None
                              latest_data["timestamp"] = time.time()
                              latest_data["speed_kmh"] = 0.0
                         # Envoyer l'état même si parsing échoué (pour MAJ raw data)
                         socketio.emit('update', {'data': latest_data})

            else:
                time.sleep(0.1) # Petite pause si pas de données

        except serial.SerialException as e:
            print(f"Erreur série: {e}. Réessai dans 5 secondes...")
            if ser and ser.is_open:
                ser.close()
            ser = None
            error_state = {}
            with data_lock:
                latest_data["raw"] = f"Erreur de connexion série ({e})"
                latest_data["parsed"] = None
                latest_data["timestamp"] = time.time()
                latest_data["speed_kmh"] = 0.0
                error_state = latest_data.copy()
            # Envoyer l'état d'erreur aux clients
            socketio.emit('update', {'data': error_state})
            time.sleep(5)
        except Exception as e:
            print(f"Erreur inattendue dans serial_reader_task: {e}")
            # En cas d'erreur grave, on pourrait vouloir s'arrêter ou juste attendre
            time.sleep(2)


# --- Routes Flask ---

@app.route('/')
def index():
    """Affiche la page principale."""
    return render_template('index.html')

@app.route('/download_csv')
def download_csv():
    """Permet de télécharger le fichier CSV."""
    try:
        return send_file(CSV_FILENAME,
                         mimetype='text/csv',
                         download_name='ballon_data_log.csv',
                         as_attachment=True)
    except FileNotFoundError:
        return "Erreur: Fichier CSV non trouvé.", 404

# --- Gestion SocketIO ---

@socketio.on('connect')
def handle_connect():
    """Gère les nouvelles connexions client."""
    print(f"Client connecté: {request.sid}")
    # Envoyer l'état actuel (dernières données + historique) au nouveau client uniquement
    with data_lock:
        # Créer une copie pour éviter les problèmes de concurrence pendant l'envoi
        current_state = {
             "latest": latest_data.copy(),
             "history": list(data_history) # Envoyer une copie de l'historique actuel
        }
    # Utilise emit() sans broadcast=True pour n'envoyer qu'au client courant (sid)
    emit('init', {'data': current_state})
    print("Données initiales envoyées au client")

@socketio.on('disconnect')
def handle_disconnect():
    """Gère les déconnexions client."""
    print(f"Client déconnecté: {request.sid}")

# Pas besoin de fonction broadcast_data explicite, socketio.emit() fait le travail

# --- Démarrage ---
if __name__ == '__main__':
    print("Préparation du démarrage du serveur...")
    # Démarrer le thread de lecture série en arrière-plan
    # daemon=True permet au thread de se terminer si le programme principal s'arrête
    serial_thread = threading.Thread(target=serial_reader_task, daemon=True)
    serial_thread.start()
    print("Thread série démarré.")

    # Démarrer le serveur Flask-SocketIO
    print(f"Serveur Flask-SocketIO démarré sur http://0.0.0.0:5000")
    print("Accédez via http://localhost:5000 ou l'URL Ngrok")
    # Utiliser socketio.run() au lieu de app.run()
    # allow_unsafe_werkzeug=True est parfois nécessaire avec debug=True pour le reloader
    socketio.run(app, host='0.0.0.0', port=5000, debug=DEBUG_MODE,
                 allow_unsafe_werkzeug=True if DEBUG_MODE else False)