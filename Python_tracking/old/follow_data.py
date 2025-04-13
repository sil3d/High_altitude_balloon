import serial
import serial.tools.list_ports
import time
import json
import threading
import logging
import re
import os
from flask import Flask, render_template, jsonify
from flask_socketio import SocketIO

# Configuration du logging
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(name)s - %(levelname)s - %(message)s')
logger = logging.getLogger('main')

# Initialisation de l'application Flask
app = Flask(__name__)
app.config['SECRET_KEY'] = 'secret!'
socketio = SocketIO(app)

# Variables globales
gps_data = []
initial_position = None
serial_port = None
is_running = False
thread = None
port_name = None  # Stockage du nom du port pour éviter les problèmes de redémarrage

def find_arduino_port():
    """Trouve automatiquement le port série Arduino"""
    global port_name
    
    # Si le port a déjà été identifié, le renvoyer
    if port_name:
        return port_name
        
    ports = list(serial.tools.list_ports.comports())
    for port in ports:
        # Recherche des ports qui pourraient être des Arduino/ESP32
        if "Arduino" in port.description or "CH340" in port.description or "CP210" in port.description or "FTDI" in port.description:
            logger.info(f"Port Arduino trouvé: {port.device}")
            port_name = port.device
            return port_name
    
    # Si aucun port reconnu automatiquement n'est trouvé, retourne le premier port disponible
    if ports:
        logger.info(f"Port automatique non trouvé, utilisation du premier port disponible: {ports[0].device}")
        port_name = ports[0].device
        return port_name
    
    logger.error("Aucun port série trouvé!")
    return None

def parse_gps_data(data):
    """Parse les données GPS reçues du module LoRa"""
    try:
        # Format attendu: "UTC:16:53:00 Lat:14.486680 Lon:-17.078957"
        # Utiliser des expressions régulières pour extraire les données
        utc_match = re.search(r'UTC:(\d+:\d+:\d+)', data)
        lat_match = re.search(r'Lat:(-?\d+\.\d+)', data)
        lon_match = re.search(r'Lon:(-?\d+\.\d+)', data)
        
        if lat_match and lon_match:
            lat = float(lat_match.group(1))
            lon = float(lon_match.group(1))
            timestamp = utc_match.group(1) if utc_match else time.strftime("%H:%M:%S")
            
            return {
                "lat": lat,
                "lon": lon,
                "alt": 0,  # Altitude non disponible dans ce format
                "timestamp": timestamp
            }
    except Exception as e:
        logger.error(f"Erreur lors du parsing des données GPS: {e}")
    
    return None

def read_serial():
    """Lit les données du port série en continu"""
    global gps_data, initial_position, is_running, serial_port
    
    while is_running:
        try:
            if serial_port and serial_port.is_open:
                if serial_port.in_waiting > 0:
                    line = serial_port.readline().decode('utf-8', errors='replace').strip()
                    # Ignorer les lignes d'information du récepteur
                    if line and not line.startswith(('-', 'Paquet', 'Taille', 'RSSI', 'SNR')):
                        logger.info(f"Données reçues: {line}")
                        point = parse_gps_data(line)
                        if point:
                            if not initial_position:
                                initial_position = point
                            gps_data.append(point)
                            socketio.emit('gps_update', point)
            else:
                time.sleep(1)
                # Essayer de reconnecter
                try_connect_serial()
                
        except Exception as e:
            logger.error(f"Erreur de lecture du port série: {e}")
            time.sleep(1)
            try_connect_serial()

def try_connect_serial():
    """Essaie de se connecter au port série"""
    global serial_port
    
    try:
        if serial_port and serial_port.is_open:
            serial_port.close()
        
        port = find_arduino_port()
        if port:
            # Essayer de se connecter avec un délai pour éviter les conflits
            time.sleep(0.5)
            serial_port = serial.Serial(port, 115200, timeout=1)
            logger.info(f"Connecté au port {port}")
            return True
    except Exception as e:
        logger.error(f"Erreur de connexion au port série: {e}")
    
    return False

def start_background_thread():
    """Démarre le thread de lecture des données série"""
    global thread, is_running
    
    if not thread or not thread.is_alive():
        is_running = True
        thread = threading.Thread(target=read_serial)
        thread.daemon = True
        thread.start()

@app.route('/')
def index():
    """Page principale de l'application"""
    return render_template('index.html')

@app.route('/api/track_data')
def get_track_data():
    """Endpoint pour récupérer toutes les données du trajet"""
    return jsonify({
        'initial_position': initial_position,
        'track': gps_data
    })

@socketio.on('connect')
def handle_connect():
    """Gestion de la connexion WebSocket"""
    logger.info('Client connecté')
    # Envoyer les données existantes au nouveau client
    if initial_position:
        socketio.emit('initial_position', initial_position)
    if gps_data:
        socketio.emit('full_track', gps_data)

@socketio.on('disconnect')
def handle_disconnect():
    """Gestion de la déconnexion WebSocket"""
    logger.info('Client déconnecté')

def create_template():
    """Crée le fichier de template HTML"""
    # Créer le dossier templates s'il n'existe pas
    if not os.path.exists('templates'):
        os.makedirs('templates')
    
    # Créer le fichier de template HTML
    with open('templates/index.html', 'w', encoding='utf-8') as f:
        f.write('''<!DOCTYPE html>
<html lang="fr">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Tracker GPS</title>
    <link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css" />
    <style>
        body, html {
            margin: 0;
            padding: 0;
            height: 100%;
            font-family: Arial, sans-serif;
        }
        #map {
            width: 100%;
            height: 90vh;
        }
        .info-bar {
            padding: 10px;
            background-color: #f0f0f0;
            border-bottom: 1px solid #ccc;
        }
        .status {
            color: #d9534f;
            font-weight: bold;
        }
        .status.connected {
            color: #5cb85c;
        }
    </style>
</head>
<body>
    <div class="info-bar">
        <span id="connection-status" class="status">Connexion...</span> | 
        Points: <span id="points-count">0</span> | 
        Dernière position: <span id="last-position">N/A</span>
    </div>
    <div id="map"></div>

    <script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"></script>
    <script src="https://cdnjs.cloudflare.com/ajax/libs/socket.io/4.7.2/socket.io.min.js"></script>
    <script>
        // Initialisation de la carte
        const map = L.map('map').setView([14.486680, -17.078957], 12); // Vue par défaut sur Dakar
        
        L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
            attribution: '&copy; <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors'
        }).addTo(map);
        
        // Variables pour stocker les données du trajet
        let trackPoints = [];
        let trackLine = null;
        let initialMarker = null;
        let currentMarker = null;
        
        // Connexion WebSocket
        const socket = io();
        const statusElement = document.getElementById('connection-status');
        const pointsCountElement = document.getElementById('points-count');
        const lastPositionElement = document.getElementById('last-position');
        
        socket.on('connect', function() {
            statusElement.textContent = 'Connecté';
            statusElement.classList.add('connected');
        });
        
        socket.on('disconnect', function() {
            statusElement.textContent = 'Déconnecté';
            statusElement.classList.remove('connected');
        });
        
        // Réception de la position initiale
        socket.on('initial_position', function(data) {
            console.log('Position initiale reçue:', data);
            setInitialPosition(data);
        });
        
        // Réception du trajet complet (au chargement)
        socket.on('full_track', function(data) {
            console.log('Trajet complet reçu:', data);
            trackPoints = data;
            updateTrackLine();
            updateUI();
            
            // Zoom sur le trajet
            if (trackPoints.length > 0) {
                const bounds = trackPoints.map(p => [p.lat, p.lon]);
                map.fitBounds(bounds);
            }
        });
        
        // Réception des mises à jour en temps réel
        socket.on('gps_update', function(point) {
            console.log('Nouveau point GPS:', point);
            trackPoints.push(point);
            updateTrackLine();
            updateCurrentPosition(point);
            updateUI();
        });
        
        function setInitialPosition(position) {
            if (!initialMarker) {
                initialMarker = L.marker([position.lat, position.lon], {
                    title: 'Point de départ',
                    icon: L.icon({
                        iconUrl: 'https://cdn.rawgit.com/pointhi/leaflet-color-markers/master/img/marker-icon-green.png',
                        iconSize: [25, 41],
                        iconAnchor: [12, 41],
                        popupAnchor: [1, -34]
                    })
                }).addTo(map);
                initialMarker.bindPopup('Point de départ - ' + position.timestamp).openPopup();
                
                // Centrer la carte sur la position initiale
                map.setView([position.lat, position.lon], 15);
            }
        }
        
        function updateCurrentPosition(position) {
            if (!currentMarker) {
                currentMarker = L.marker([position.lat, position.lon], {
                    title: 'Position actuelle',
                    icon: L.icon({
                        iconUrl: 'https://cdn.rawgit.com/pointhi/leaflet-color-markers/master/img/marker-icon-red.png',
                        iconSize: [25, 41],
                        iconAnchor: [12, 41],
                        popupAnchor: [1, -34]
                    })
                }).addTo(map);
            } else {
                currentMarker.setLatLng([position.lat, position.lon]);
            }
            
            currentMarker.bindPopup('Position actuelle - ' + position.timestamp);
            
            // Centrer la carte sur la nouvelle position
            map.panTo([position.lat, position.lon]);
        }
        
        function updateTrackLine() {
            // Supprimer l'ancienne ligne si elle existe
            if (trackLine) {
                map.removeLayer(trackLine);
            }
            
            // Créer une nouvelle ligne avec tous les points
            if (trackPoints.length > 1) {
                const coordinates = trackPoints.map(p => [p.lat, p.lon]);
                trackLine = L.polyline(coordinates, {
                    color: 'blue',
                    weight: 4,
                    opacity: 0.7
                }).addTo(map);
            }
        }
        
        function updateUI() {
            pointsCountElement.textContent = trackPoints.length;
            
            if (trackPoints.length > 0) {
                const lastPoint = trackPoints[trackPoints.length - 1];
                lastPositionElement.textContent = `Lat: ${lastPoint.lat.toFixed(6)}, Lon: ${lastPoint.lon.toFixed(6)} - ${lastPoint.timestamp}`;
            }
        }
        
        // Récupérer les données existantes au chargement de la page
        fetch('/api/track_data')
            .then(response => response.json())
            .then(data => {
                console.log('Données existantes:', data);
                
                if (data.initial_position) {
                    setInitialPosition(data.initial_position);
                }
                
                if (data.track && data.track.length > 0) {
                    trackPoints = data.track;
                    updateTrackLine();
                    updateCurrentPosition(trackPoints[trackPoints.length - 1]);
                    updateUI();
                    
                    // Zoom sur le trajet
                    const bounds = trackPoints.map(p => [p.lat, p.lon]);
                    map.fitBounds(bounds);
                }
            })
            .catch(error => console.error('Erreur lors du chargement des données:', error));
    </script>
</body>
</html>''')

if __name__ == '__main__':
    # Créer le template
    create_template()
    
    # Tentative de connexion au port série
    if try_connect_serial():
        # Démarrer le thread de lecture
        start_background_thread()
        
        # Démarrer l'application Flask avec use_reloader=False pour éviter les problèmes de port série
        logger.info("Démarrage du serveur web...")
        socketio.run(app, host='0.0.0.0', debug=True, use_reloader=False)
    else:
        logger.error("Impossible de démarrer l'application, aucun port série disponible.")