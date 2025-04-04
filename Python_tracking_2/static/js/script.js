document.addEventListener('DOMContentLoaded', function () {
    // --- Configuration ---
    const SENEGAL_SOMONE_COORDS = [14.49, -17.07]; // Coordonnées approximatives de Somone
    const INITIAL_ZOOM = 11;
    const MAX_ZOOM = 18;

    // --- Initialisation Socket.IO ---
    // Connecte-toi au namespace par défaut '/'
    const socket = io(); // Ou io.connect('http://' + document.domain + ':' + location.port);

    // --- Initialisation Leaflet ---
    const map = L.map('map').setView(SENEGAL_SOMONE_COORDS, INITIAL_ZOOM);

    // Couches de tuiles (Map layers)
    const osmTile = L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', {
        maxZoom: MAX_ZOOM,
        attribution: '© <a href="http://www.openstreetmap.org/copyright">OpenStreetMap</a>'
    });
    const satelliteTile = L.tileLayer('https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}', {
        maxZoom: MAX_ZOOM,
        attribution: 'Tiles © Esri — Source: Esri, i-cubed, USDA, USGS, AEX, GeoEye, Getmapping, Aerogrid, IGN, IGP, UPR-EGP, and the GIS User Community'
    });
     const topoTile = L.tileLayer('https://{s}.tile.opentopomap.org/{z}/{x}/{y}.png', {
        maxZoom: MAX_ZOOM,
        attribution: 'Map data: © <a href="https://www.openstreetmap.org/copyright">OpenStreetMap</a> contributors, <a href="http://viewfinderpanoramas.org">SRTM</a> | Map style: © <a href="https://opentopomap.org">OpenTopoMap</a> (<a href="https://creativecommons.org/licenses/by-sa/3.0/">CC-BY-SA</a>)'
    });

    // Ajoute la couche par défaut
    osmTile.addTo(map);

    // Contrôle des couches
    const baseMaps = {
        "OpenStreetMap": osmTile,
        "Satellite": satelliteTile,
        "Topographique": topoTile
    };
    L.control.layers(baseMaps).addTo(map);

    // --- Marqueurs et Tracés ---
    let balloonMarker = null;
    let userMarker = null;
    const balloonPath = L.polyline([], { color: 'red', weight: 3 }).addTo(map); // Tracé du ballon
    let userAccuracyCircle = null;
    let routeLine = null; // Pour la ligne droite entre user et ballon

    // Icônes personnalisées (optionnel)
    const balloonIcon = L.icon({
        iconUrl: 'https://img.icons8.com/plasticine/100/000000/hot-air-balloon.png', // Trouve une icône de ballon
        iconSize: [38, 38],
        iconAnchor: [19, 38],
        popupAnchor: [0, -38]
    });
    const userIcon = L.icon({
        iconUrl: 'https://img.icons8.com/ios-filled/50/000000/user-location.png', // Trouve une icône utilisateur
        iconSize: [30, 30],
        iconAnchor: [15, 15],
        popupAnchor: [0, -15]
    });


    // --- État du suivi ---
    let isTrackingBalloon = false;
    let userPosition = null; // Stocke la dernière position connue de l'utilisateur {lat, lon}

    // --- Éléments DOM ---
    const trackBalloonBtn = document.getElementById('track-balloon-btn');
    const trackMeBtn = document.getElementById('track-me-btn');
    const centerMapBtn = document.getElementById('center-map-btn');
    const lastUpdateEl = document.getElementById('last-update');
    const rawDataEl = document.getElementById('raw-data');
    const errorMessageEl = document.getElementById('error-message');
    const errorTextEl = document.getElementById('error-text');
    const serialStatusEl = document.getElementById('serial-status').querySelector('.badge');

    // --- Fonctions d'aide ---
    function updateTextField(id, value, unit = '', decimals = null) {
        const el = document.getElementById(id);
        if (el) {
            let text = 'N/A';
             // Vérifie si la valeur est valide (pas null, undefined ou NaN)
            if (value !== null && value !== undefined && !isNaN(value)) {
                 if (typeof value === 'number' && decimals !== null) {
                    text = value.toFixed(decimals) + unit;
                 } else {
                    text = value + unit;
                 }
            } else if (value === "NO_FIX") { // Gérer le cas spécifique "NO_FIX"
                text = "Pas de signal";
            }
             el.textContent = text;
        } else {
            console.warn(`Element with ID ${id} not found.`);
        }
    }

    function formatTimestamp(isoTimestamp) {
        if (!isoTimestamp) return "Jamais";
        try {
            const date = new Date(isoTimestamp);
            // Vérifie si la date est valide
             if (isNaN(date.getTime())) {
                // Si ce n'est pas un format ISO standard, essaie un format plus simple
                // Exemple : "2023-11-20 15:30:00"
                const parts = isoTimestamp.split(/[\s-:]/);
                 if (parts.length === 6) {
                    // Attention: les mois en JS sont 0-indexés
                    const isoLike = `${parts[0]}-${parts[1].padStart(2, '0')}-${parts[2].padStart(2, '0')}T${parts[3].padStart(2, '0')}:${parts[4].padStart(2, '0')}:${parts[5].padStart(2, '0')}`;
                    const parsedDate = new Date(isoLike);
                     if (!isNaN(parsedDate.getTime())) {
                         return parsedDate.toLocaleTimeString('fr-FR');
                     }
                 }
                return "Format inconnu"; // Retourne si le parsing échoue toujours
            }
            return date.toLocaleTimeString('fr-FR');
        } catch (e) {
            console.error("Error parsing timestamp:", isoTimestamp, e);
            return "Invalide";
        }
    }

    function updateMapBalloon(gpsData) {
        if (gpsData && gpsData.hasGPSFix && gpsData.lat != null && gpsData.lon != null) {
            const latLng = [gpsData.lat, gpsData.lon];

             // Affiche les données GPS même si le marqueur n'est pas créé
            document.getElementById('gps-status').textContent = 'Fix GPS Acquis';
            document.getElementById('gps-status').className = 'text-success';
            document.getElementById('gps-data').style.display = 'block';
            updateTextField('data-lat', gpsData.lat, '', 6);
            updateTextField('data-lon', gpsData.lon, '', 6);
            updateTextField('data-alt-gps', gpsData.alt, ' m', 1);
            updateTextField('data-sat', gpsData.satellites);
            updateTextField('data-time', gpsData.time);

            if (!balloonMarker) {
                balloonMarker = L.marker(latLng, { icon: balloonIcon }).addTo(map)
                    .bindPopup(`<b>Ballon</b><br>Lat: ${gpsData.lat.toFixed(4)}<br>Lon: ${gpsData.lon.toFixed(4)}<br>Alt: ${gpsData.alt.toFixed(1)}m`);
            } else {
                balloonMarker.setLatLng(latLng);
                // Met à jour le popup si nécessaire
                 balloonMarker.setPopupContent(`<b>Ballon</b><br>Lat: ${gpsData.lat.toFixed(4)}<br>Lon: ${gpsData.lon.toFixed(4)}<br>Alt: ${gpsData.alt.toFixed(1)}m`);
            }

            // Ajoute le point au tracé
            balloonPath.addLatLng(latLng);

            // Centre la carte si le suivi est actif
            if (isTrackingBalloon) {
                map.setView(latLng);
            }
            // Met à jour la distance si la position utilisateur est connue
            if (userPosition) {
                 updateDistanceAndRoute();
            }

        } else {
             document.getElementById('gps-status').textContent = gpsData?.error || 'Pas de Fix GPS';
             document.getElementById('gps-status').className = 'text-warning';
             document.getElementById('gps-data').style.display = 'none'; // Cache les détails GPS
              // Réinitialise les champs GPS texte
             updateTextField('data-lat', null);
             updateTextField('data-lon', null);
             updateTextField('data-alt-gps', null);
             updateTextField('data-sat', null);
             updateTextField('data-time', null);
             // Ne pas recentrer si le GPS est perdu et que le suivi est actif
             // Met à jour la distance (indiquera une erreur)
             updateDistanceAndRoute(); // Pour afficher "Pas de fix GPS" dans la distance
        }
    }

    function updateDistanceAndRoute() {
         const distanceInfoEl = document.getElementById('distance-info');
         // Enlève l'ancienne ligne droite
         if (routeLine) {
            map.removeLayer(routeLine);
            routeLine = null;
         }

         if (userPosition && balloonMarker) {
             const userLatLng = L.latLng(userPosition.lat, userPosition.lon);
             const balloonLatLng = balloonMarker.getLatLng();

              // Demande au serveur de calculer la distance (plus précis avec geopy)
             socket.emit('request_distance', {
                 userLat: userPosition.lat,
                 userLon: userPosition.lon
             });

             // Dessine une ligne droite simple entre les deux points
             routeLine = L.polyline([userLatLng, balloonLatLng], {
                color: 'blue',
                weight: 2,
                opacity: 0.7,
                dashArray: '5, 10' // Ligne en pointillés
             }).addTo(map);

         } else if (!userPosition) {
             distanceInfoEl.textContent = 'Distance Ballon-Moi: Activez "Ma Position"';
         } else if (!balloonMarker) {
              distanceInfoEl.textContent = 'Distance Ballon-Moi: En attente de la position du ballon...';
         } else {
              distanceInfoEl.textContent = 'Distance Ballon-Moi: Impossible de calculer (données manquantes).';
         }
    }


    // --- Gestionnaires d'événements SocketIO ---
    socket.on('connect', () => {
        console.log('Connecté au serveur WebSocket');
        serialStatusEl.textContent = 'Connecté';
        serialStatusEl.className = 'badge bg-success';
    });

    socket.on('disconnect', () => {
        console.log('Déconnecté du serveur WebSocket');
        serialStatusEl.textContent = 'Déconnecté';
        serialStatusEl.className = 'badge bg-danger';
    });

    socket.on('serial_status', (data) => {
         console.log('Statut Série:', data);
         if (data.status === 'connected') {
             serialStatusEl.textContent = `Connecté (${data.port})`;
             serialStatusEl.className = 'badge bg-success';
             errorMessageEl.style.display = 'none';
         } else if (data.status === 'error') {
             serialStatusEl.textContent = 'Erreur Série';
             serialStatusEl.className = 'badge bg-danger';
             errorTextEl.textContent = data.message || 'Erreur inconnue';
             errorMessageEl.style.display = 'block';
         } else {
             serialStatusEl.textContent = 'Déconnecté';
             serialStatusEl.className = 'badge bg-warning text-dark';
         }
    });


    socket.on('update_data', (data) => {
        // console.log('Données reçues:', data); // Debug

        // Met à jour l'heure de la dernière réception
        lastUpdateEl.textContent = data.timestamp ? `Dernière MAJ: ${formatTimestamp(data.timestamp)}` : "Jamais mis à jour";

        // Affiche les données brutes
        rawDataEl.textContent = data.raw || "N/A";

        // Affiche le message d'erreur global s'il y en a un
        if (data.error) {
             errorTextEl.textContent = data.error;
             errorMessageEl.style.display = 'block';
        } else {
             errorMessageEl.style.display = 'none';
        }

        // Met à jour les champs si les données parsées existent
        if (data.parsed && typeof data.parsed === 'object' && !data.parsed.error) {
            const parsed = data.parsed;

            // Mise à jour GPS et carte
            updateMapBalloon(parsed.gps); // Gère l'affichage GPS et le marqueur/tracé

            // Mise à jour Environnement
            updateTextField('data-temp', parsed.env?.temperature, ' °C', 1);
            // Convertit Pa en hPa
             const pressureHpa = parsed.env?.pressure ? parsed.env.pressure / 100 : null;
            updateTextField('data-press', pressureHpa, ' hPa', 1);
            updateTextField('data-hum', parsed.env?.humidity, ' %', 1);
            updateTextField('data-alt-baro', parsed.env?.altitude, ' m', 1);

            // Mise à jour Qualité d'air
            updateTextField('data-aq', parsed.air?.airQuality);
             updateTextField('data-aq-index', parsed.air?.airQualityIndex);
            updateTextField('data-tvoc', parsed.air?.tvoc, ' ppb');
            updateTextField('data-eco2', parsed.air?.eCO2, ' ppm');

            // Mise à jour Autres
            updateTextField('data-ozone', parsed.other?.ozone, ' ppb');
            updateTextField('data-uv', parsed.other?.uvIndex, '', 1);

        } else if (data.parsed?.error) {
             // Gérer spécifiquement une erreur de parsing rapportée par le serveur
             console.error("Erreur de parsing côté serveur:", data.parsed.error);
             errorTextEl.textContent = `Erreur parsing données: ${data.parsed.error}`;
             errorMessageEl.style.display = 'block';
              // Peut-être réinitialiser certains champs si l'erreur est critique
        }
         else {
             // Si data.parsed est null ou vide (avant la première réception valide)
             // Optionnel: réinitialiser les champs autres que GPS qui est géré par updateMapBalloon
             console.log("Données parsées non disponibles ou vides.");
             // updateTextField('data-temp', null); // etc. pour les autres champs
         }
    });

     socket.on('distance_update', (data) => {
        const distanceInfoEl = document.getElementById('distance-info');
        if (data.distance_km !== undefined) {
             distanceInfoEl.innerHTML = `<strong>Distance Ballon-Moi:</strong> ${data.distance_km} km`;
        } else if (data.error) {
             distanceInfoEl.innerHTML = `<strong>Distance Ballon-Moi:</strong> <span class="text-danger">${data.error}</span>`;
        } else {
             distanceInfoEl.textContent = 'Distance Ballon-Moi: Calcul en cours...';
        }
    });


    // --- Gestionnaires d'événements des boutons ---
    trackBalloonBtn.addEventListener('click', () => {
        isTrackingBalloon = !isTrackingBalloon;
        trackBalloonBtn.classList.toggle('active', isTrackingBalloon);
        trackBalloonBtn.innerHTML = isTrackingBalloon
             ? '<i class="fas fa-stop-circle"></i> Arrêter Suivi'
             : '<i class="fas fa-satellite-dish"></i> Suivre Ballon';

        if (isTrackingBalloon && balloonMarker) {
            map.setView(balloonMarker.getLatLng()); // Centre immédiatement
        }
         // Si on arrête le suivi, on ne fait rien de spécial sur la vue
    });

    trackMeBtn.addEventListener('click', () => {
        trackMeBtn.disabled = true; // Désactive pendant la recherche
         trackMeBtn.innerHTML = '<i class="fas fa-spinner fa-spin"></i> Recherche...';

        if (navigator.geolocation) {
            navigator.geolocation.getCurrentPosition(
                (position) => {
                    userPosition = {
                        lat: position.coords.latitude,
                        lon: position.coords.longitude
                    };
                    const latLng = [userPosition.lat, userPosition.lon];
                    const accuracy = position.coords.accuracy; // Précision en mètres

                    if (!userMarker) {
                        userMarker = L.marker(latLng, { icon: userIcon }).addTo(map)
                            .bindPopup(`<b>Ma Position</b><br>Précision: ${accuracy.toFixed(0)} m`);
                    } else {
                        userMarker.setLatLng(latLng)
                         .setPopupContent(`<b>Ma Position</b><br>Précision: ${accuracy.toFixed(0)} m`);;
                    }

                     // Ajoute ou met à jour le cercle de précision
                     if (!userAccuracyCircle) {
                        userAccuracyCircle = L.circle(latLng, {
                            radius: accuracy,
                            color: '#136AEC',
                            fillColor: '#136AEC',
                            fillOpacity: 0.15,
                            weight: 1 // Épaisseur de la bordure
                        }).addTo(map);
                    } else {
                        userAccuracyCircle.setLatLng(latLng).setRadius(accuracy);
                    }


                    map.setView(latLng, map.getZoom() < 15 ? 15 : map.getZoom()); // Centre sur l'utilisateur et zoom un peu si besoin

                    // Met à jour la distance maintenant que la position est connue
                     updateDistanceAndRoute();

                     trackMeBtn.disabled = false;
                     trackMeBtn.innerHTML = '<i class="fas fa-location-arrow"></i> Ma Position';
                },
                (error) => {
                    console.error("Erreur de géolocalisation: ", error);
                    let message = "Impossible d'obtenir la position.";
                    switch (error.code) {
                        case error.PERMISSION_DENIED:
                            message = "Permission de géolocalisation refusée.";
                            break;
                        case error.POSITION_UNAVAILABLE:
                            message = "Position non disponible.";
                            break;
                        case error.TIMEOUT:
                            message = "Timeout lors de la demande de position.";
                            break;
                    }
                    alert(message);
                     trackMeBtn.disabled = false;
                     trackMeBtn.innerHTML = '<i class="fas fa-location-arrow"></i> Ma Position';
                },
                { // Options de géolocalisation
                    enableHighAccuracy: true, // Demande une position plus précise
                    timeout: 10000, // Temps max pour obtenir la position (10s)
                    maximumAge: 0 // Force une nouvelle position (pas de cache)
                }
            );
        } else {
            alert("La géolocalisation n'est pas supportée par votre navigateur.");
            trackMeBtn.disabled = false;
            trackMeBtn.innerHTML = '<i class="fas fa-location-arrow"></i> Ma Position';
        }
    });

    centerMapBtn.addEventListener('click', () => {
         const markers = [];
         if (balloonMarker) markers.push(balloonMarker);
         if (userMarker) markers.push(userMarker);

         if (markers.length > 0) {
             const group = new L.featureGroup(markers);
             map.fitBounds(group.getBounds().pad(0.3)); // pad ajoute un peu de marge
         } else {
             // Si aucun marqueur, recentre sur Somone
             map.setView(SENEGAL_SOMONE_COORDS, INITIAL_ZOOM);
         }
          // Désactive le suivi du ballon si on recentre manuellement
         if (isTrackingBalloon) {
             isTrackingBalloon = false;
             trackBalloonBtn.classList.remove('active');
             trackBalloonBtn.innerHTML = '<i class="fas fa-satellite-dish"></i> Suivre Ballon';
         }
    });

}); // Fin de DOMContentLoaded