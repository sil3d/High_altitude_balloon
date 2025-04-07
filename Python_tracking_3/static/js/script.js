/**
 * static/js/script.js
 * Logique Frontend pour l'application de suivi de ballon stratosphérique
 * Utilise Leaflet.js, Chart.js, Socket.IO et Leaflet Routing Machine.
 */

document.addEventListener('DOMContentLoaded', function () {
    // --- Éléments DOM ---
    const wsStatusEl = document.getElementById('wsStatus');
    const arduinoStatusEl = document.getElementById('arduinoStatus');
    const rawDataDisplayEl = document.getElementById('rawDataDisplay');
    const lastUpdateTimestampEl = document.getElementById('lastUpdateTimestamp');
    const downloadCsvBtn = document.getElementById('downloadCsvBtn');
    const trackMeBtn = document.getElementById('trackMeBtn');
    const toggleTrackBalloonBtn = document.getElementById('toggleTrackBalloonBtn');
    const distanceDisplayEl = document.getElementById('distanceDisplay');
    const mapElement = document.getElementById('map');
    const altitudeChartCanvas = document.getElementById('altitudeChart');
    const statusIndicator = document.getElementById('statusIndicator');

    // --- Variables d'état ---
    let map = null;
    let balloonMarker = null;
    let userMarker = null;
    let balloonTrackPolyline = null;
    let userToBalloonPolyline = null; // Ligne droite (vol d'oiseau)
    let routingControl = null;      // Contrôle pour la route routière
    let altitudeChart = null;
    let socket = null; // Instance Socket.IO
    let isTrackingBalloon = false;
    let isTrackingUser = false;
    let userLocationWatchId = null;
    let userLocation = null; // [lat, lng]
    let balloonLocation = null; // [lat, lng]
    const MAX_CHART_POINTS = 100;
    const MAX_TRACK_POINTS = 500;

    // --- Fonctions d'Initialisation ---

    function initializeMap() {
        if (!mapElement) {
            console.error("L'élément 'map' n'a pas été trouvé dans le DOM.");
            return;
        }
        const initialLat = 14.497; // Somone, Sénégal approx.
        const initialLng = -17.060;
        const initialZoom = 9;

        map = L.map(mapElement).setView([initialLat, initialLng], initialZoom);

        // Couches de tuiles
        const osmTile = L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', { attribution: '© OpenStreetMap contributors' });
        const satelliteTile = L.tileLayer('https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}', { attribution: 'Tiles © Esri' });
        const topoTile = L.tileLayer('https://{s}.tile.opentopomap.org/{z}/{x}/{y}.png', { attribution: 'Map data: © OpenStreetMap contributors, SRTM | Map style: © OpenTopoMap (CC-BY-SA)' });
        osmTile.addTo(map);
        const baseMaps = { "OpenStreetMap": osmTile, "Satellite": satelliteTile, "Topographique": topoTile };
        L.control.layers(baseMaps).addTo(map);

        // --- Icônes personnalisées ---
         const balloonIcon = L.icon({
             // Utilisation de l'icône locale demandée
             iconUrl: '/static/img/icons8-balloon-48.png',
             iconSize:     [48, 48], // Taille de l'image
             iconAnchor:   [24, 48], // Ancre en bas au centre (x=largeur/2, y=hauteur)
             popupAnchor:  [0, -50]  // Popup juste au-dessus de l'ancre
         });
         const userIcon = L.icon({
             iconUrl: 'https://cdn-icons-png.flaticon.com/64/684/684908.png',
             iconSize: [25, 25],
             iconAnchor: [12, 25],
             popupAnchor: [0, -25]
         });

        // Marqueurs
        balloonMarker = L.marker([initialLat, initialLng], {icon: balloonIcon})
            .addTo(map)
            .bindPopup("Ballon (en attente de données...)")
            .openPopup();

        userMarker = L.marker([0, 0], {icon: userIcon})
            .bindPopup("Votre Position"); // Ne sera ajouté à la map que si localisation OK

        // Polylignes
        balloonTrackPolyline = L.polyline([], {color: 'blue', weight: 3, opacity: 0.8}).addTo(map);
        userToBalloonPolyline = L.polyline([], {color: 'gray', weight: 1, opacity: 0.9, dashArray: '5, 5'}).addTo(map); // Ligne droite en gris léger

        // Initialiser le contrôle de routage (sans l'ajouter à la carte)
        routingControl = L.Routing.control({
            waypoints: [], // Sera défini dynamiquement
            router: L.Routing.osrmv1({ serviceUrl: 'https://router.project-osrm.org/route/v1' }), // Routeur OSRM demo
            routeWhileDragging: false,
            addWaypoints: false,
            draggableWaypoints: false,
            show: false, // Ne pas montrer le panneau d'instructions par défaut
            lineOptions: { styles: [{color: '#FF4500', opacity: 0.7, weight: 6}] } // Route en orange vif
            // createMarker: function() { return null; } // Option: pour cacher les marqueurs A/B du plugin si on utilise déjà les nôtres
        });
        // Ne pas utiliser .addTo(map) ici.
         // --- AJOUT : Écouteur pour les clics sur la carte ---
         map.on('click', function(e) {
            // 'e' est l'objet événement du clic
            // 'e.latlng' contient les coordonnées Lat/Lng du point cliqué

            const clickedLat = e.latlng.lat.toFixed(6); // Latitude avec 6 décimales
            const clickedLng = e.latlng.lng.toFixed(6); // Longitude avec 6 décimales

            // Créer le contenu du popup
            const popupContent = `<b>Coordonnées Cliquées :</b><br>Latitude: ${clickedLat}<br>Longitude: ${clickedLng}`;

            // Créer et ouvrir un popup à l'endroit du clic
            L.popup()
                .setLatLng(e.latlng)     // Positionne le popup à l'endroit cliqué
                .setContent(popupContent) // Met le texte dans le popup
                .openOn(map);            // Ouvre le popup sur la carte 'map'

            // Optionnel : Afficher aussi dans la console pour débogage
            console.log(`Clic sur la carte aux coordonnées : Lat ${clickedLat}, Lon ${clickedLng}`);
        });
    }

    function initializeChart() {
        if (!altitudeChartCanvas) {
             console.error("L'élément 'altitudeChart' n'a pas été trouvé dans le DOM.");
             return;
        }
        const ctx = altitudeChartCanvas.getContext('2d');
        altitudeChart = new Chart(ctx, {
            type: 'line',
            data: { labels: [], datasets: [{ label: 'Altitude Barométrique (m)', data: [], borderColor: 'rgb(54, 162, 235)', backgroundColor: 'rgba(54, 162, 235, 0.2)', tension: 0.1, fill: true, pointRadius: 1, pointHoverRadius: 3 }] },
            options: {
                responsive: true, maintainAspectRatio: false,
                 scales: { x: { type: 'time', time: { unit: 'minute', tooltipFormat: 'HH:mm:ss', displayFormats: { minute: 'HH:mm' } }, title: { display: true, text: 'Heure' }, ticks: { maxRotation: 0, autoSkip: true, maxTicksLimit: 10 } }, y: { beginAtZero: false, title: { display: true, text: 'Altitude (m)' } } },
                plugins: { tooltip: { mode: 'index', intersect: false }, legend: { display: true } },
                 animation: { duration: 0 }, hover: { animationDuration: 0 }, responsiveAnimationDuration: 0
            }
        });
    }

    // --- Fonctions de Mise à Jour UI ---

    function updateStatusIndicator(statusData) {
        let alertClass = 'alert-info';
        let arduinoStatusText = 'En attente...';
        if (statusData.arduino === 'ok') arduinoStatusText = 'Données OK';
        else if (statusData.arduino === 'error') arduinoStatusText = 'Erreur Arduino/Série';
        else if (statusData.arduino === 'nodata') arduinoStatusText = 'Pas de données récentes';
        else if (statusData.arduino === 'invalid') arduinoStatusText = 'Données invalides';

        if (statusData.ws === 'Connecté' && statusData.arduino === 'ok') alertClass = 'alert-success';
        else if (statusData.ws === 'Déconnecté' || statusData.ws === 'Erreur Connexion' || statusData.ws === 'Échec Reconnexion' || statusData.arduino === 'error') alertClass = 'alert-danger';
        else if (statusData.ws === 'Connexion...' || statusData.ws === 'Reconnexion...' || statusData.arduino === 'nodata' || statusData.arduino === 'invalid') alertClass = 'alert-warning';

        wsStatusEl.textContent = statusData.ws;
        arduinoStatusEl.textContent = arduinoStatusText;
        if(statusIndicator) statusIndicator.className = `alert ${alertClass} d-flex align-items-center`;
    }

    function updateUI(data) {
        if (!data) return;

        let currentArduinoStatus = 'nodata';
        if (data.raw && data.raw.includes("Erreur")) currentArduinoStatus = 'error';
        else if (data.parsed) currentArduinoStatus = 'ok';
        else if (data.raw) currentArduinoStatus = 'invalid';
        const currentWsStatus = socket?.connected ? 'Connecté' : (socket?.connecting ? 'Connexion...' : 'Déconnecté'); // Peut être plus précis que les events seuls
        updateStatusIndicator({ ws: currentWsStatus, arduino: currentArduinoStatus });

        // Affichage données textuelles
        rawDataDisplayEl.textContent = data.raw || 'N/A';
        const timestamp = data.timestamp ? new Date(data.timestamp * 1000).toLocaleTimeString() : 'N/A';
        lastUpdateTimestampEl.textContent = timestamp;

        const p = data.parsed;
        // GPS Data
        document.getElementById('data-lat').textContent = p?.gps?.lat?.toFixed(6) ?? 'N/A';
        document.getElementById('data-lon').textContent = p?.gps?.lon?.toFixed(6) ?? 'N/A';
        document.getElementById('data-alt-gps').textContent = p?.gps?.alt?.toFixed(1) ?? 'N/A';
        document.getElementById('data-sat').textContent = p?.gps?.satellites ?? 'N/A';
        document.getElementById('data-fix').innerHTML = p?.gps?.fix ? '<span class="text-success">Oui</span>' : '<span class="text-danger">Non</span>';
        document.getElementById('data-time').textContent = p?.gps?.time ?? 'N/A';
        document.getElementById('data-speed').textContent = data.speed_kmh?.toFixed(1) ?? 'N/A';
        // Env Data
        document.getElementById('data-temp').textContent = p?.env?.temp?.toFixed(1) ?? 'N/A';
        const pressureHpa = p?.env?.pressure ? (p.env.pressure / 100).toFixed(1) : 'N/A';
        document.getElementById('data-pressure').textContent = pressureHpa;
        document.getElementById('data-humidity').textContent = p?.env?.humidity?.toFixed(1) ?? 'N/A';
        document.getElementById('data-alt-baro').textContent = p?.env?.altitude?.toFixed(1) ?? 'N/A';
        // Air Data
        document.getElementById('data-aqi-idx').textContent = p?.air?.quality_idx ?? 'N/A';
        document.getElementById('data-aqi-desc').textContent = p?.air?.quality_desc ?? '?';
        document.getElementById('data-tvoc').textContent = p?.air?.tvoc ?? 'N/A';
        document.getElementById('data-eco2').textContent = p?.air?.eco2 ?? 'N/A';
        // Other Data
        document.getElementById('data-ozone').textContent = p?.other?.ozone ?? 'N/A';
        document.getElementById('data-uv').textContent = p?.other?.uv?.toFixed(1) ?? 'N/A';

        // Mise à jour Carte
        if (p?.gps?.fix && p.gps.lat != null && p.gps.lon != null) {
             balloonLocation = [p.gps.lat, p.gps.lon];
             if (balloonMarker) {
                 balloonMarker.setLatLng(balloonLocation).setPopupContent(
                     `<b>Ballon</b><br>Lat: ${p.gps.lat.toFixed(4)}, Lon: ${p.gps.lon.toFixed(4)}<br>Alt GPS: ${p.gps.alt?.toFixed(0)}m | Baro: ${p.env?.altitude?.toFixed(0)}m<br>Vitesse: ${data.speed_kmh?.toFixed(1)} km/h`
                 );
             }
             if (balloonTrackPolyline) {
                 balloonTrackPolyline.addLatLng(balloonLocation);
                 const latLngs = balloonTrackPolyline.getLatLngs();
                 if (latLngs.length > MAX_TRACK_POINTS) balloonTrackPolyline.setLatLngs(latLngs.slice(1)); // Enlever le plus ancien
             }
             if (isTrackingBalloon && map) map.panTo(balloonLocation);
             updateUserToBalloonLine(); // MAJ ligne droite/distance
             updateRoute();            // MAJ route routière
         } else {
             balloonLocation = null;
             updateUserToBalloonLine(); // Nettoyer ligne droite
             updateRoute();            // Nettoyer route routière
         }

        // Mise à jour Graphique
        if (altitudeChart && p?.env?.altitude != null && data.timestamp) {
            const chartTimestamp = data.timestamp * 1000;
            const labels = altitudeChart.data.labels;
            const chartData = altitudeChart.data.datasets[0].data;
            const lastLabel = labels[labels.length - 1];
            const lastData = chartData[chartData.length - 1];

            if (lastLabel !== chartTimestamp || lastData !== p.env.altitude) {
                 labels.push(chartTimestamp);
                 chartData.push(p.env.altitude);
                 while (labels.length > MAX_CHART_POINTS) { labels.shift(); chartData.shift(); }
                 altitudeChart.update();
            }
        }
    }

     function updateHistoryUI(historyData) {
         console.log("Chargement de l'historique:", historyData.length, "points");
         if (!altitudeChart || !balloonTrackPolyline || !map) return;

         altitudeChart.data.labels = [];
         altitudeChart.data.datasets[0].data = [];
         const trackPoints = [];
         let lastValidPoint = null;

         historyData.forEach(point => {
             const p = point.parsed;
             const ts = point.timestamp * 1000;
             if (p?.env?.altitude != null && point.timestamp) {
                 altitudeChart.data.labels.push(ts);
                 altitudeChart.data.datasets[0].data.push(p.env.altitude);
             }
             if (p?.gps?.fix && p.gps.lat != null && p.gps.lon != null) {
                 trackPoints.push([p.gps.lat, p.gps.lon]);
                 lastValidPoint = point;
             }
         });

         while (altitudeChart.data.labels.length > MAX_CHART_POINTS) { altitudeChart.data.labels.shift(); altitudeChart.data.datasets[0].data.shift(); }
         const limitedTrackPoints = trackPoints.length > MAX_TRACK_POINTS ? trackPoints.slice(-MAX_TRACK_POINTS) : trackPoints;

         balloonTrackPolyline.setLatLngs(limitedTrackPoints);
         altitudeChart.update();

         if (lastValidPoint) {
             updateUI(lastValidPoint);
             const lastLoc = [lastValidPoint.parsed.gps.lat, lastValidPoint.parsed.gps.lon];
             if (limitedTrackPoints.length > 1) map.fitBounds(limitedTrackPoints, {padding: [30, 30], maxZoom: 15});
             else map.setView(lastLoc, 13);
         } else if (historyData.length > 0) {
             updateUI(historyData[historyData.length - 1]);
         }
     }

    // --- Fonctions de Géolocalisation, Distance et Route ---

    function haversineDistance(coords1, coords2) {
        // Calcule la distance "à vol d'oiseau" en km
        if (!coords1 || !coords2) return 0;
        function toRad(x) { return x * Math.PI / 180; }
        const R = 6371;
        const dLat = toRad(coords2[0] - coords1[0]);
        const dLon = toRad(coords2[1] - coords1[1]);
        const lat1 = toRad(coords1[0]); const lat2 = toRad(coords2[0]);
        const a = Math.sin(dLat/2) * Math.sin(dLat/2) + Math.sin(dLon/2) * Math.sin(dLon/2) * Math.cos(lat1) * Math.cos(lat2);
        const c = 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1-a));
        return R * c;
    }

    function updateUserToBalloonLine() {
        // Met à jour la ligne droite et l'affichage de la distance
        if (userLocation && balloonLocation && map.hasLayer(userMarker)) {
            const distanceKm = haversineDistance(userLocation, balloonLocation);
             distanceDisplayEl.textContent = `${distanceKm.toFixed(2)} km`;
             if (userToBalloonPolyline) userToBalloonPolyline.setLatLngs([userLocation, balloonLocation]);
        } else {
             distanceDisplayEl.textContent = 'N/A';
             if (userToBalloonPolyline) userToBalloonPolyline.setLatLngs([]);
        }
    }

     function updateRoute() {
        // Met à jour l'itinéraire routier
         if (userLocation && balloonLocation && map && routingControl && map.hasLayer(userMarker)) {
            try {
                const userLatLng = L.latLng(userLocation[0], userLocation[1]);
                const balloonLatLng = L.latLng(balloonLocation[0], balloonLocation[1]);

                // Vérifie si le contrôle est déjà sur la carte (pour éviter les erreurs potentielles)
                // Utilise une propriété interne (non idéale mais fonctionne souvent) ou une variable drapeau
                if (!routingControl._map) { // Vérifie si le contrôle est attaché à une carte
                    routingControl.addTo(map);
                }

                routingControl.setWaypoints([userLatLng, balloonLatLng]);
            } catch (e) {
                console.error("Erreur lors de la mise à jour de la route:", e);
                 // Peut arriver si le contrôle est dans un état instable, on essaie de le retirer
                if (routingControl && routingControl._map) {
                    try { map.removeControl(routingControl); } catch (removeError) {}
                }
            }
         } else if (routingControl && routingControl._map) {
             // Si l'une des positions manque ou userMarker n'est pas sur la carte, retirer la route
             try {
                 map.removeControl(routingControl);
             } catch (e) {
                 console.error("Erreur lors de la suppression du contrôle de route:", e);
             }
         }
     }


    function trackUserLocation() {
        if (!navigator.geolocation) { alert("La géolocalisation n'est pas supportée."); return; }
        if (isTrackingUser) { stopTrackingUser(); return; }

        isTrackingUser = true;
        trackMeBtn.innerHTML = `<span class="spinner-border spinner-border-sm" role="status" aria-hidden="true"></span> Arrêter Suivi`;
        trackMeBtn.classList.replace('btn-success', 'btn-warning');

        navigator.geolocation.getCurrentPosition(
             (position) => {
                 userLocation = [position.coords.latitude, position.coords.longitude];
                 if (!map.hasLayer(userMarker)) userMarker.addTo(map);
                 userMarker.setLatLng(userLocation);
                 map.setView(userLocation, 14);
                 updateUserToBalloonLine();
                 updateRoute(); // Première mise à jour de la route
             },
             handleGeolocationError,
             { enableHighAccuracy: true, timeout: 10000, maximumAge: 60000 }
         );

        userLocationWatchId = navigator.geolocation.watchPosition(
            (position) => {
                 userLocation = [position.coords.latitude, position.coords.longitude];
                 if (!map.hasLayer(userMarker)) userMarker.addTo(map);
                 userMarker.setLatLng(userLocation).bindPopup(`Vous êtes ici<br><small>Lat: ${userLocation[0].toFixed(4)}, Lon: ${userLocation[1].toFixed(4)}</small>`);
                 updateUserToBalloonLine();
                 updateRoute(); // Mise à jour continue de la route
            },
            handleGeolocationError,
            { enableHighAccuracy: true, timeout: 20000, maximumAge: 10000 }
        );
    }

    function stopTrackingUser() {
        if (userLocationWatchId !== null) { navigator.geolocation.clearWatch(userLocationWatchId); userLocationWatchId = null; }
        isTrackingUser = false;
        trackMeBtn.innerHTML = `<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" fill="currentColor" class="bi bi-person-bounding-box" viewBox="0 0 16 16"><path d="M1.5 1a.5.5 0 0 0-.5.5v3a.5.5 0 0 1-1 0v-3A1.5 1.5 0 0 1 1.5 0h3a.5.5 0 0 1 0 1h-3zM11 .5a.5.5 0 0 1 .5-.5h3A1.5 1.5 0 0 1 16 1.5v3a.5.5 0 0 1-1 0v-3a.5.5 0 0 0-.5-.5h-3a.5.5 0 0 1-.5-.5zM.5 11a.5.5 0 0 1 .5.5v3a.5.5 0 0 0 .5.5h3a.5.5 0 0 1 0 1h-3A1.5 1.5 0 0 1 0 14.5v-3a.5.5 0 0 1 .5-.5zm15 0a.5.5 0 0 1 .5.5v3a1.5 1.5 0 0 1-1.5 1.5h-3a.5.5 0 0 1 0-1h3a.5.5 0 0 0 .5-.5v-3a.5.5 0 0 1 .5-.5z"/><path d="M3 14s-1 0-1-1 1-4 6-4 6 3 6 4-1 1-1 1H3zm5-6a3 3 0 1 0 0-6 3 3 0 0 0 0 6z"/></svg> Ma Position`;
        trackMeBtn.classList.replace('btn-warning', 'btn-success');
        // Option: Enlever le marqueur et la route quand on arrête
        // if (map.hasLayer(userMarker)) map.removeLayer(userMarker);
        // userLocation = null; // Ceci va déclencher la suppression de la route dans updateRoute()
        // updateUserToBalloonLine();
        // updateRoute(); // Appel pour nettoyer la route si userLocation est null
    }

    function handleGeolocationError(error) {
        console.error("Erreur de géolocalisation:", error);
        let message = "Impossible d'obtenir la position.";
         switch(error.code) {
            case error.PERMISSION_DENIED: message = "Permission refusée."; break;
            case error.POSITION_UNAVAILABLE: message = "Position indisponible."; break;
            case error.TIMEOUT: message = "Timeout."; break;
         }
         alert(`Erreur de localisation: ${message}`);
         stopTrackingUser();
    }

    // --- Connexion et Gestion Socket.IO ---

    function connectSocketIO() {
        socket = io({ reconnectionAttempts: 5, reconnectionDelay: 3000 });

        socket.on('connect', () => { console.log('Socket.IO Connecté. ID:', socket.id); updateStatusIndicator({ ws: 'Connecté', arduino: arduinoStatusEl.textContent === 'Données OK' ? 'ok' : 'nodata' }); });
        socket.on('disconnect', (reason) => { console.log('Socket.IO Déconnecté:', reason); updateStatusIndicator({ ws: 'Déconnecté', arduino: 'Inconnu' }); });
        socket.on('connect_error', (error) => { console.error('Erreur connexion Socket.IO:', error); updateStatusIndicator({ ws: 'Erreur Connexion', arduino: 'Inconnu' }); });
        socket.on('reconnect_attempt', (attempt) => { console.log(`Tentative reconnexion #${attempt}...`); updateStatusIndicator({ ws: 'Reconnexion...', arduino: 'Inconnu' }); });
        socket.on('reconnect_failed', () => { console.error('Échec reconnexion Socket.IO.'); updateStatusIndicator({ ws: 'Échec Reconnexion', arduino: 'Inconnu' }); });

        socket.on('init', (message) => {
            console.log("<- Événement 'init' reçu");
            let historyHasValidGps = false; // Drapeau pour savoir si l'historique a une position
            if (message?.data) {
                if (message.data.history?.length > 0) {
                    // Vérifier si l'historique contient au moins un point GPS valide
                    historyHasValidGps = message.data.history.some(point => point?.parsed?.gps?.fix);
                    updateHistoryUI(message.data.history); // Met à jour UI avec l'historique
                } else if (message.data.latest) {
                    // S'il n'y a pas d'historique, utiliser le dernier point
                    updateUI(message.data.latest);
                    // Si ce dernier point a un fix, on le note
                    if (message.data.latest?.parsed?.gps?.fix) {
                        historyHasValidGps = true;
                    }
                }
       
                // --- AJOUT POUR LE TEST SANS LILYGO ---
                // Si après traitement de l'init, on n'a toujours pas de position pour le ballon
                // ET que balloonLocation est toujours null, on utilise la position initiale du marqueur.
                if (!historyHasValidGps && balloonLocation === null && balloonMarker) {
                    console.log("Aucune position GPS historique/récente pour le ballon. Utilisation de la position initiale du marqueur pour le test.");
                    const initialMarkerPos = balloonMarker.getLatLng();
                    balloonLocation = [initialMarkerPos.lat, initialMarkerPos.lng];
                    // On peut optionnellement appeler updateRoute ici si userLocation est déjà connu
                    if (userLocation) {
                        console.log("Tentative de mise à jour de la route avec position initiale du ballon.");
                        updateRoute();
                    }
                }
                // --- FIN DE L'AJOUT ---
       
            } else console.warn("Message 'init' incomplet:", message);
       });
    }

    // --- Configuration des Écouteurs d'Événements ---

    function setupEventListeners() {
        if (downloadCsvBtn) downloadCsvBtn.addEventListener('click', () => { window.location.href = '/download_csv'; });
        if (trackMeBtn) trackMeBtn.addEventListener('click', trackUserLocation);
        if (toggleTrackBalloonBtn) {
            toggleTrackBalloonBtn.addEventListener('click', () => {
                isTrackingBalloon = !isTrackingBalloon;
                const iconSVG = `<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" fill="currentColor" class="bi bi-broadcast-pin" viewBox="0 0 16 16"><path d="M3.05 3.05a7 7 0 0 0 0 9.9.5.5 0 0 1-.707.707 8 8 0 0 1 0-11.314.5.5 0 0 1 .707.707zm2.122 2.122a4 4 0 0 0 0 5.656.5.5 0 1 1-.708.708 5 5 0 0 1 0-7.072.5.5 0 0 1 .708.708zm5.656-.708a.5.5 0 0 1 .708 0 5 5 0 0 1 0 7.072.5.5 0 1 1-.708-.708 4 4 0 0 0 0-5.656.5.5 0 0 1 0-.708zm2.122-2.12a.5.5 0 0 1 .707 0 8 8 0 0 1 0 11.313.5.5 0 0 1-.707-.707 7 7 0 0 0 0-9.9.5.5 0 0 1 0-.707zM6 8a2 2 0 1 1-4 0 2 2 0 0 1 4 0z"/></svg>`;
                toggleTrackBalloonBtn.innerHTML = `${iconSVG} Suivi Ballon: ${isTrackingBalloon ? '<span class="text-warning fw-bold">ON</span>' : '<span class="text-info">OFF</span>'}`;
                toggleTrackBalloonBtn.classList.toggle('btn-info', !isTrackingBalloon);
                toggleTrackBalloonBtn.classList.toggle('btn-warning', isTrackingBalloon);
                if (isTrackingBalloon && balloonLocation && map) map.panTo(balloonLocation);
            });
        }
    }

    // --- Fonction Principale d'Initialisation ---
    function initializeApp() {
        console.log("Initialisation de l'application...");
        initializeMap();
        initializeChart();
        setupEventListeners();
        connectSocketIO();
        updateStatusIndicator({ ws: 'Connexion...', arduino: 'En attente...' });
    }

    // --- Démarrage ---
    initializeApp();

}); // Fin DOMContentLoaded