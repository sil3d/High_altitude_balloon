import serial
import threading
import time
import os
import pandas as pd
from datetime import datetime
from flask import Flask, render_template, send_from_directory, request, jsonify
from flask_socketio import SocketIO, emit
from geopy.distance import geodesic
import math # Import math module for isnan check

# --- Configuration ---
SERIAL_PORT = 'COM4'  # Adapte si ton port COM est différent
BAUD_RATE = 115200
EXCEL_FILE = 'data/balloon_data.xlsx'
LOG_FILE = 'serial_log.txt' # Pour débugger les données brutes reçues

# Créer le dossier data s'il n'existe pas
if not os.path.exists('data'):
    os.makedirs('data')

# --- Initialisation Flask et SocketIO ---
# Utilise eventlet pour de meilleures performances avec SocketIO et le thread série
# Si eventlet pose problème, tu peux essayer async_mode='threading'
app = Flask(__name__)
app.config['SECRET_KEY'] = 'fdhfghdguoyergjlkrjggheghu!' # Change ceci pour la production
socketio = SocketIO(app)

# --- Variables globales pour stocker les données ---
latest_data = {
    "raw": "En attente de données...",
    "parsed": None, # Contiendra les données structurées
    "timestamp": None,
    "error": None # Pour stocker les erreurs de port série
}
data_log = [] # Liste pour stocker l'historique pour Excel
data_lock = threading.Lock() # Pour protéger l'accès aux variables partagées
serial_connection = None
serial_thread_running = True

# --- Fonction pour parser les données Arduino ---
def parse_arduino_data(line):
    """Parse une ligne de données reçue de l'Arduino."""
    data = {}
    try:
        parts = line.split('|')
        for part in parts:
            if part.startswith("GPS,"):
                gps_str = part[4:]
                if gps_str == "NO_FIX":
                    data['gps'] = {"hasGPSFix": False}
                else:
                    gps_parts = gps_str.split(',')
                    if len(gps_parts) == 5:
                        data['gps'] = {
                            "lat": float(gps_parts[0]),
                            "lon": float(gps_parts[1]),
                            "alt": float(gps_parts[2]),
                            "satellites": int(gps_parts[3]),
                            "time": gps_parts[4],
                            "hasGPSFix": True
                        }
                    else:
                         data['gps'] = {"hasGPSFix": False, "error": "Format GPS invalide"}
            elif part.startswith("ENV,"):
                env_parts = part[4:].split(',')
                if len(env_parts) == 4:
                    data['env'] = {
                        "temperature": float(env_parts[0]),
                        "pressure": int(env_parts[1]),
                        "humidity": float(env_parts[2]),
                        "altitude": float(env_parts[3])
                    }
            elif part.startswith("AIR,"):
                air_parts = part[4:].split(',')
                if len(air_parts) == 3:
                     air_quality_map = {1: "Excellent", 2: "Bon", 3: "Moyen", 4: "Mauvais", 5: "Malsain"}
                     aq_index = int(air_parts[0])
                     data['air'] = {
                        "airQualityIndex": aq_index,
                        "airQuality": air_quality_map.get(aq_index, "Inconnu"),
                        "tvoc": int(air_parts[1]),
                        "eCO2": int(air_parts[2])
                    }
            elif part.startswith("OZ,"):
                data['other'] = data.get('other', {}) # Crée 'other' si n'existe pas
                data['other']['ozone'] = int(part[3:])
            elif part.startswith("UV,"):
                data['other'] = data.get('other', {}) # Crée 'other' si n'existe pas
                data['other']['uvIndex'] = float(part[3:])
        return data
    except Exception as e:
        print(f"Erreur de parsing: {e} sur la ligne: {line}")
        return {"error": f"Erreur parsing: {e}", "raw_line": line}

# --- Thread pour lire le port série ---
def read_serial_port():
    """Lit les données du port série en continu."""
    global latest_data, data_log, serial_connection, serial_thread_running
    print(f"Tentative de connexion au port série {SERIAL_PORT}...")
    error_emitted = False # Pour n'émettre l'erreur qu'une fois

    while serial_thread_running:
        try:
            if serial_connection is None or not serial_connection.is_open:
                 # Libère l'ancien objet si nécessaire
                if serial_connection:
                    try:
                        serial_connection.close()
                    except Exception:
                        pass # Ignore les erreurs à la fermeture
                # Nouvelle tentative de connexion
                serial_connection = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=2)
                print(f"Connecté à {SERIAL_PORT}")
                with data_lock:
                    latest_data['error'] = None # Réinitialise l'erreur si succès
                error_emitted = False # Réinitialise l'indicateur d'erreur
                socketio.emit('serial_status', {'status': 'connected', 'port': SERIAL_PORT})

            if serial_connection and serial_connection.is_open:
                if serial_connection.in_waiting > 0:
                    try:
                        line = serial_connection.readline().decode('utf-8', errors='ignore').strip()
                        if line:
                            # Log brut pour débug
                            with open(LOG_FILE, 'a') as f:
                                f.write(f"{datetime.now()}: {line}\n")

                            parsed = parse_arduino_data(line)
                            timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

                            with data_lock:
                                latest_data["raw"] = line
                                latest_data["parsed"] = parsed
                                latest_data["timestamp"] = timestamp
                                latest_data["error"] = None # Efface l'erreur si lecture OK

                                # Ajoute au log pour Excel (seulement si data valide)
                                if parsed and 'error' not in parsed:
                                    log_entry = parsed.copy() # Copie pour ne pas modifier l'original
                                    log_entry['timestamp'] = timestamp
                                    # Aplatir la structure pour Excel si besoin (optionnel mais propre)
                                    flat_entry = {}
                                    flat_entry['timestamp'] = timestamp
                                    for key, value in log_entry.items():
                                        if isinstance(value, dict):
                                            for sub_key, sub_value in value.items():
                                                # Vérifier si la valeur est NaN avant de l'ajouter
                                                if isinstance(sub_value, float) and math.isnan(sub_value):
                                                    flat_entry[f"{key}_{sub_key}"] = None # Ou une chaîne vide ''
                                                else:
                                                    flat_entry[f"{key}_{sub_key}"] = sub_value
                                        elif isinstance(value, float) and math.isnan(value):
                                             flat_entry[key] = None # Ou ''
                                        else:
                                            flat_entry[key] = value
                                    data_log.append(flat_entry)

                            # Émettre les données via SocketIO
                            socketio.emit('update_data', latest_data, namespace='/')
                            #print(f"Données envoyées: {latest_data}") # Debug

                    except serial.SerialException as se:
                        print(f"Erreur série pendant la lecture: {se}")
                        with data_lock:
                           latest_data['error'] = f"Erreur lecture série: {se}"
                        socketio.emit('update_data', latest_data)
                        socketio.emit('serial_status', {'status': 'error', 'message': str(se)})
                        if serial_connection:
                           serial_connection.close()
                        serial_connection = None
                        time.sleep(5) # Attendre avant de retenter
                    except UnicodeDecodeError as ude:
                         print(f"Erreur de décodage: {ude} - Ligne ignorée")
                         # Optionnel: logguer la ligne brute qui a causé l'erreur
                    except Exception as e:
                        print(f"Erreur inattendue pendant la lecture: {e}")
                        with data_lock:
                             latest_data['error'] = f"Erreur lecture: {e}"
                        socketio.emit('update_data', latest_data)
                        # Ne pas forcément fermer la connexion pour une erreur de parsing
                        time.sleep(1)
                else:
                    # Pas de données reçues, évite la boucle trop rapide
                    time.sleep(0.1)

        except serial.SerialException as e:
            if not error_emitted:
                print(f"Erreur: Impossible de se connecter à {SERIAL_PORT}. Raison: {e}")
                with data_lock:
                    latest_data['error'] = f"Port Série {SERIAL_PORT} non trouvé ou occupé."
                    latest_data['raw'] = "Erreur de connexion série."
                    latest_data['parsed'] = None
                socketio.emit('update_data', latest_data) # Informe le client de l'erreur
                socketio.emit('serial_status', {'status': 'error', 'message': str(e)})
                error_emitted = True
            serial_connection = None # Assure que la connexion est marquée comme nulle
            time.sleep(5) # Attendre 5 secondes avant de réessayer

        except Exception as ex:
             if not error_emitted:
                print(f"Erreur générale dans le thread série: {ex}")
                with data_lock:
                    latest_data['error'] = f"Erreur générale thread série: {ex}"
                socketio.emit('update_data', latest_data)
                socketio.emit('serial_status', {'status': 'error', 'message': f"Erreur générale: {ex}"})
                error_emitted = True
             time.sleep(5) # Attendre avant de retenter

    print("Thread série terminé.")
    if serial_connection and serial_connection.is_open:
        serial_connection.close()
        print(f"Port série {SERIAL_PORT} fermé.")

# --- Routes Flask ---
@app.route('/')
def index():
    """Sert la page HTML principale."""
    return render_template('index.html')

@app.route('/download_excel')
def download_excel():
    """Sauvegarde les données dans un fichier Excel et le propose au téléchargement."""
    global data_log
    if not data_log:
        return "Aucune donnée à télécharger.", 404

    try:
        with data_lock:
             # Crée une copie pour éviter les problèmes de concurrence pendant la sauvegarde
            log_copy = list(data_log)

        if not log_copy:
             return "Aucune donnée à télécharger.", 404

        df = pd.DataFrame(log_copy)
        # Réorganiser les colonnes (optionnel, pour la lisibilité)
        cols = ['timestamp'] + [col for col in df.columns if col != 'timestamp']
        # S'assurer que toutes les colonnes existent avant de réindexer
        existing_cols = [c for c in cols if c in df.columns]
        df = df[existing_cols]

        # Sauvegarde dans le fichier
        df.to_excel(EXCEL_FILE, index=False, engine='openpyxl')

        # Propose le fichier au téléchargement
        return send_from_directory(os.path.dirname(EXCEL_FILE),
                                   os.path.basename(EXCEL_FILE),
                                   as_attachment=True)
    except Exception as e:
        print(f"Erreur lors de la création/téléchargement du fichier Excel: {e}")
        return f"Erreur lors de la génération du fichier Excel: {e}", 500

# --- Événements SocketIO ---
@socketio.on('connect', namespace='/')
def handle_connect():
    """Gère la connexion d'un nouveau client."""
    print('Client connecté')
    # Envoie les dernières données connues au nouveau client
    with data_lock:
        emit('update_data', latest_data)
        if serial_connection and serial_connection.is_open:
             emit('serial_status', {'status': 'connected', 'port': SERIAL_PORT})
        elif latest_data.get('error'):
             emit('serial_status', {'status': 'error', 'message': latest_data['error']})
        else:
             emit('serial_status', {'status': 'disconnected'})


@socketio.on('disconnect', namespace='/')
def handle_disconnect():
    """Gère la déconnexion d'un client."""
    print('Client déconnecté')

@socketio.on('request_distance', namespace='/')
def handle_distance_request(data):
    """Calcule la distance entre le ballon et l'utilisateur."""
    user_lat = data.get('userLat')
    user_lon = data.get('userLon')

    if user_lat is None or user_lon is None:
        emit('distance_update', {'error': 'Coordonnées utilisateur manquantes'})
        return

    with data_lock:
        balloon_data = latest_data.get("parsed", {}).get("gps")

    if balloon_data and balloon_data.get("hasGPSFix"):
        balloon_lat = balloon_data.get("lat")
        balloon_lon = balloon_data.get("lon")

        if balloon_lat is not None and balloon_lon is not None:
            try:
                user_coords = (user_lat, user_lon)
                balloon_coords = (balloon_lat, balloon_lon)
                distance_km = geodesic(user_coords, balloon_coords).km
                emit('distance_update', {'distance_km': round(distance_km, 2)})
            except Exception as e:
                 emit('distance_update', {'error': f'Erreur calcul distance: {e}'})
        else:
            emit('distance_update', {'error': 'Coordonnées ballon invalides'})
    else:
        emit('distance_update', {'error': 'Pas de fix GPS pour le ballon'})


# --- Démarrage de l'application ---
if __name__ == '__main__':
    print("Démarrage du thread de lecture série...")
    serial_thread = threading.Thread(target=read_serial_port, daemon=True)
    serial_thread.start()

    print("Démarrage du serveur Flask sur http://0.0.0.0:5000")
    # Utilise socketio.run pour démarrer correctement avec eventlet ou threading
    # host='0.0.0.0' permet d'accéder au serveur depuis d'autres appareils sur le même réseau
    socketio.run(app, host='0.0.0.0', port=5000, debug=False, use_reloader=False)
     # debug=False et use_reloader=False sont importants quand on utilise un thread custom

    # Pour arrêter proprement le thread série à la fermeture du serveur (Ctrl+C)
    serial_thread_running = False
    serial_thread.join() # Attendre que le thread se termine
    print("Application terminée.")