$(document).ready(function() {
    // =========================================================================
    // Configuration
    // =========================================================================
    const CONFIG = {
        MAP_INITIAL_COORDS: [14.498, -17.071], // Somone
        MAP_INITIAL_ZOOM: 9,
        MAP_MAX_BALLOON_TRACK_POINTS: 500, // Max points in balloon track line
        CHART_MAX_POINTS: 100, // Max points in altitude chart
        STALE_DATA_THRESHOLD_MS: 60000, // Mark data as stale after 60 seconds
        GEOLOCATION_TIMEOUT: 10000, // Timeout for getting user location (10s)
        USER_MARKER_ICON_URL: 'https://img.icons8.com/color/48/000000/marker.png',
        BALLOON_MARKER_ICON_URL: 'https://img.icons8.com/office/40/000000/hot-air-balloon.png',
        OSRM_SERVICE_URL: 'https://router.project-osrm.org/route/v1' // Routing engine
    };

    // =========================================================================
    // Variables Globales
    // =========================================================================
    let map;
    let balloonMarker;
    let userMarker;
    let userAccuracyCircle = null;
    let balloonTrack;
    let routingControl;
    let altitudeChart;
    let socket;

    let isFollowingBalloon = false;
    let lastKnownBalloonPosition = null; // L.LatLng object
    let lastKnownUserPosition = null; // L.LatLng object
    let lastValidDataTimestamp = null; // JS timestamp (ms) of last valid data receipt

    // =========================================================================
    // Cache des éléments DOM (pour la performance)
    // =========================================================================
    const $ui = {
        connectionStatus: $('#connection-status'),
        serialStatusIndicator: $('#serial-status-indicator'),
        serialStatusText: $('#serial-status-text'),
        navbarError: $('#navbar-error'),
        errorAlert: $('#error-alert'),
        errorMessage: $('#error-message'),
        timestamp: $('#timestamp'),
        latitude: $('#latitude'),
        longitude: $('#longitude'),
        altitudeGps: $('#altitude_gps'),
        satellites: $('#satellites'),
        speedKmh: $('#speed_kmh'),
        rssi: $('#rssi'),
        temperature: $('#temperature'),
        pressure: $('#pressure'),
        humidity: $('#humidity'),
        altitudeBme: $('#altitude_bme'),
        airQuality: $('#air_quality'),
        airQualityText: $('#air_quality_text'),
        tvoc: $('#tvoc'),
        eco2: $('#eco2'),
        ozone: $('#ozone'),
        uvIndex: $('#uv_index'),
        distance: $('#distance'),
        routeInfo: $('#route-info'),
        toggleFollowBtn: $('#toggle-follow-btn'),
        trackMeBtn: $('#track-me-btn'),
        altitudeChartCanvas: $('#altitudeChart') // Canvas element itself
    };

    // =========================================================================
    // Fonctions Utilitaires (Helpers)
    // =========================================================================
    const formatFloat = (val, dec = 1) => (val === null || typeof val === 'undefined' || isNaN(parseFloat(val))) ? 'N/A' : parseFloat(val).toFixed(dec);
    const formatInt = (val) => (val === null || typeof val === 'undefined' || isNaN(parseInt(val))) ? 'N/A' : parseInt(val);

    function getAirQualityText(index) {
        const idx = parseInt(index);
        if (isNaN(idx)) return "N/A";
        // Note: Les seuils exacts dépendent de la source des données (ex: norme AQI)
        const qualityMap = { 1: "Excellent", 2: "Bon", 3: "Moyen", 4: "Mauvais", 5: "Très Mauvais" };
        return qualityMap[idx] || "Inconnu";
    }

    // Affiche ou masque les messages d'erreur
    function setError(message, isPersistent = false) {
        if (message) {
            console.error("UI Error:", message);
            $ui.errorMessage.text(message);
            $ui.errorAlert.removeClass('d-none');
            if (isPersistent) {
                $ui.navbarError.text(message).removeClass('d-none').data('is-persistent', true);
            }
        } else {
            $ui.errorAlert.addClass('d-none');
            // Ne masque l'erreur navbar que si elle n'était pas marquée comme persistante
            if (!$ui.navbarError.data('is-persistent')) {
                $ui.navbarError.addClass('d-none');
            }
            // Dans tous les cas où on efface, on retire le flag persistant au cas où
            // (une erreur persistante ne doit être enlevée que par une action spécifique, ex: reconnexion)
            //$ui.navbarError.data('is-persistent', false); // Attention avec ça
        }
    }

    // Efface spécifiquement les erreurs persistantes (appelé lors d'une reconnexion par ex.)
    function clearPersistentError() {
         console.log("Clearing persistent errors");
         $ui.navbarError.addClass('d-none').data('is-persistent', false);
         setError(null); // Efface aussi l'alerte générale
    }


    // =========================================================================
    // Initialisation Carte & Leaflet
    function initMap() {
        try {
            map = L.map('map', {
                center: CONFIG.MAP_INITIAL_COORDS,
                zoom: CONFIG.MAP_INITIAL_ZOOM
            });

            // --- Layers ---
            const osmTile = L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', { attribution: '© OSM Contributors' }).addTo(map); // Default
            const satelliteTile = L.tileLayer('https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}', { attribution: 'Tiles © Esri' });
            const topoTile = L.tileLayer('https://{s}.tile.opentopomap.org/{z}/{x}/{y}.png', { attribution: 'Map data: © OpenTopoMap contributors' });
            const baseMaps = { "OpenStreetMap": osmTile, "Satellite": satelliteTile, "Topographique": topoTile };
            L.control.layers(baseMaps).addTo(map);

            // --- Icons ---
            const balloonIcon = L.icon({ iconUrl: CONFIG.BALLOON_MARKER_ICON_URL, iconSize: [32, 32], iconAnchor: [16, 32], popupAnchor: [0, -32] });
            const userIcon = L.icon({ iconUrl: CONFIG.USER_MARKER_ICON_URL, iconSize: [32, 32], iconAnchor: [16, 32], popupAnchor: [0, -32] });

            // --- Définir la position initiale PAR DÉFAUT du ballon ---
            // On utilise les coordonnées initiales de la carte comme point de départ
            // Cela permet de tester le routage même sans données réelles du ballon.
            lastKnownBalloonPosition = L.latLng(CONFIG.MAP_INITIAL_COORDS[0], CONFIG.MAP_INITIAL_COORDS[1]);
            console.log("Setting DEFAULT balloon position for testing:", lastKnownBalloonPosition);

            // --- Markers ---
            // Initialiser le marqueur ballon à sa position par défaut
            balloonMarker = L.marker(lastKnownBalloonPosition, { // Utilise la position par défaut
                icon: balloonIcon,
                opacity: 0.8 // Légèrement différent pour indiquer que c'est peut-être pas une donnée fraîche
            }).addTo(map).bindPopup("Ballon (Position initiale/par défaut)"); // Mettre à jour le popup

            userMarker = L.marker(CONFIG.MAP_INITIAL_COORDS, { // Le marqueur utilisateur reste à sa position initiale (invisible)
                icon: userIcon,
                opacity: 0 // Invisible au début
            }).addTo(map).bindPopup("Ma Position");

            // --- Balloon Track ---
            balloonTrack = L.polyline([], { color: 'red', weight: 3 }).addTo(map);
            // Optionnel: Ajouter le point initial à la trace si vous voulez la voir démarrer de là
            // balloonTrack.addLatLng(lastKnownBalloonPosition);

            // --- Routing Control ---
            routingControl = L.Routing.control({
                waypoints: [null, null], // Toujours initialisés à null, seront mis à jour par setWaypoints
                routeWhileDragging: false,
                show: false,
                addWaypoints: false,
                draggableWaypoints: false,
                lineOptions: { styles: [{ color: 'blue', opacity: 0.6, weight: 4 }] },
                router: L.Routing.osrmv1({ serviceUrl: CONFIG.OSRM_SERVICE_URL }),
                createMarker: () => null
            }).addTo(map);

            // Cacher le conteneur du panneau de routage au début
             $('.leaflet-routing-container').hide();

            // --- Listeners pour les résultats du routage ---
             routingControl.on('routesfound', handleRouteFound);
             routingControl.on('routingerror', handleRouteError);

            console.log("Map initialized successfully (with default balloon position).");

            // Mettre à jour l'affichage initial de la distance/route (sera N/A car user position est null)
            updateDistanceAndRoute();

        } catch (error) {
            console.error("Map Initialization failed:", error);
            setError("Impossible d'initialiser la carte.", true);
        }
    }

    // =========================================================================
    // Initialisation Graphique (Chart.js)
    // =========================================================================
    function initChart() {
        try {
            if ($ui.altitudeChartCanvas.length === 0) {
                 console.error("Altitude chart canvas not found!");
                 setError("Impossible d'afficher le graphique (canvas manquant).", true);
                 return;
            }
            const ctx = $ui.altitudeChartCanvas[0].getContext('2d');
            altitudeChart = new Chart(ctx, {
                type: 'line',
                data: {
                    labels: [], // Timestamps (ms)
                    datasets: [{
                        label: 'Altitude Barométrique (m)',
                        data: [], // Altitude values
                        borderColor: 'rgb(75, 192, 192)',
                        backgroundColor: 'rgba(75, 192, 192, 0.2)',
                        tension: 0.1,
                        pointRadius: 1,
                        fill: true
                    }]
                },
                options: {
                    scales: {
                        x: {
                            type: 'time',
                            time: {
                                unit: 'minute',
                                tooltipFormat: 'HH:mm:ss',
                                displayFormats: { minute: 'HH:mm' }
                            },
                            title: { display: true, text: 'Temps' }
                        },
                        y: {
                            title: { display: true, text: 'Altitude (m)' },
                            beginAtZero: false // L'altitude peut être négative ou commencer haut
                        }
                    },
                    animation: { duration: 0 }, // Désactiver l'animation pour fluidité temps réel
                    maintainAspectRatio: false, // Important pour canvas dans un div redimensionnable
                    plugins: { legend: { display: false } } // Cacher la légende par défaut
                }
            });
            console.log("Chart initialized successfully.");
        } catch (error) {
            console.error("Chart Initialization failed:", error);
            setError("Impossible d'initialiser le graphique.", true);
        }
    }

    // =========================================================================
    // Mise à jour de l'Interface Utilisateur (UI)
    // =========================================================================
    function updateUI(data) {
        if (!data) {
            console.warn("updateUI called with null data.");
            return;
        }

        // --- Gérer Erreur Serveur ---
        if (data.error) {
            setError(`Erreur serveur: ${data.error}`, true);
            // Ne pas traiter le reste des données si une erreur est signalée
            return;
        } else {
            // Si on reçoit des données valides, on peut effacer une éventuelle erreur NON persistante
            setError(null, false);
        }

        // --- Mise à jour Timestamp et Fraîcheur ---
        if (data.timestamp) {
            try {
                const date = new Date(data.timestamp * 1000); // Convertir secondes UNIX en ms
                $ui.timestamp.text(date.toLocaleString('fr-FR', { dateStyle: 'short', timeStyle: 'medium' }));
                lastValidDataTimestamp = Date.now(); // Enregistrer l'heure de réception JS
            } catch (e) {
                $ui.timestamp.text('Invalide');
                console.warn("Timestamp formatting error:", e, data.timestamp);
            }
        } else {
            $ui.timestamp.text('N/A');
        }

        // --- Mise à jour Données Textuelles ---
        $ui.latitude.text(formatFloat(data.latitude, 5));
        $ui.longitude.text(formatFloat(data.longitude, 5));
        $ui.altitudeGps.text(formatFloat(data.altitude_gps, 1));
        $ui.satellites.text(formatInt(data.satellites));
        $ui.speedKmh.text(formatFloat(data.speed_kmh, 1));
        $ui.rssi.text(formatInt(data.rssi));
        $ui.temperature.text(formatFloat(data.temperature, 1));
        const pressureHpa = (data.pressure !== null && typeof data.pressure !== 'undefined') ? (parseFloat(data.pressure) / 100.0).toFixed(1) : 'N/A';
        $ui.pressure.text(pressureHpa);
        $ui.humidity.text(formatFloat(data.humidity, 1));
        $ui.altitudeBme.text(formatFloat(data.altitude_bme, 1));
        const aq = formatInt(data.air_quality);
        $ui.airQuality.text(aq);
        $ui.airQualityText.text(getAirQualityText(aq));
        $ui.tvoc.text(formatInt(data.tvoc));
        $ui.eco2.text(formatInt(data.eco2));
        $ui.ozone.text(formatInt(data.ozone));
        $ui.uvIndex.text(formatFloat(data.uv_index, 1));

              // --- Mise à jour Carte ---
              const hasValidGps = typeof data.latitude === 'number' && typeof data.longitude === 'number' && !isNaN(data.latitude) && !isNaN(data.longitude);

              if (map && balloonMarker) {
                  if (hasValidGps) {
                      // --- CAS: Données GPS Valides Reçues ---
                      console.log("GPS data IS VALID. Updating map objects."); // Log pour confirmer
                      const newLatLng = L.latLng(data.latitude, data.longitude);
                      lastKnownBalloonPosition = newLatLng; // Mémoriser la dernière position valide
      
                      // Mettre à jour le marqueur
                      balloonMarker.setLatLng(newLatLng).setOpacity(1); // Rendre opaque
                      let popupContent = `<b>Ballon</b><br>Lat: ${data.latitude.toFixed(4)}<br>Lon: ${data.longitude.toFixed(4)}`;
                      if (typeof data.altitude_gps === 'number') popupContent += `<br>Alt GPS: ${data.altitude_gps.toFixed(0)}m`;
                      if (typeof data.speed_kmh === 'number') popupContent += `<br>Vit: ${data.speed_kmh.toFixed(1)}km/h`;
                      balloonMarker.setPopupContent(popupContent);
      
                      // Mettre à jour la trace (la route du ballon)
                      if (balloonTrack) {
                          console.log("Adding point to track:", newLatLng.toString()); // Log pour confirmer
                          balloonTrack.addLatLng(newLatLng);
                          // Limiter le nombre de points... (code inchangé)
                          let latlngs = balloonTrack.getLatLngs();
                          if (latlngs.length > CONFIG.MAP_MAX_BALLOON_TRACK_POINTS) {
                              latlngs.splice(0, latlngs.length - CONFIG.MAP_MAX_BALLOON_TRACK_POINTS);
                              balloonTrack.setLatLngs(latlngs);
                          }
                      }
      
                      // Centrer la carte si le suivi est activé
                      if (isFollowingBalloon) {
                          console.log("Following is ON. Panning map to:", newLatLng); // Log pour confirmer
                          map.panTo(newLatLng);
                      }
      
                      // Mettre à jour la route et la distance si l'utilisateur est localisé
                       updateDistanceAndRoute();
      
                  } else {
                      // --- CAS: Données GPS NON Valides ou Absentes ---
                      console.warn("Received data without valid GPS coordinates."); // Log
      
                      // Rendre le marqueur semi-transparent
                      balloonMarker.setOpacity(0.6); // Utiliser 0.6 comme pour l'initialisation
      
                      // Positionner le marqueur:
                      if (lastKnownBalloonPosition) {
                          // Si on a DÉJÀ eu une position valide, on laisse le marqueur LÀ OÙ IL ÉTAIT.
                          balloonMarker.setLatLng(lastKnownBalloonPosition); // Assure qu'il reste à la dernière position connue
                          balloonMarker.setPopupContent(balloonMarker.getPopup().getContent().split('<br>')[0] + "<br>(Signal GPS perdu)"); // Mettre à jour popup
                      } else {
                          // Si on n'a JAMAIS eu de position valide, on le laisse à la position initiale par défaut.
                          // Note: Il est déjà à CONFIG.MAP_INITIAL_COORDS grâce à initMap,
                          // donc pas besoin de setLatLng ici, juste s'assurer que le popup est correct.
                          balloonMarker.setPopupContent("Ballon (En attente de signal GPS)");
                      }
                      // IMPORTANT: Dans ce cas (pas de GPS valide), on NE MET PAS À JOUR la trace (balloonTrack.addLatLng)
                      // et on NE CENTRE PAS la carte (map.panTo). Le suivi est impossible sans coordonnées.
                  }
              } else {
                  console.error("Cannot update map/marker: Map or balloonMarker is not initialized.");
              }

        // --- Mise à jour Graphique ---
        const hasValidAltitude = typeof data.altitude_bme === 'number' && !isNaN(data.altitude_bme);
        const hasValidTimestampForChart = data.timestamp && data.timestamp > 0;

        if (altitudeChart) { // Vérifier que le graphique existe
            if (hasValidAltitude && hasValidTimestampForChart) {
                try {
                    const timestampMs = data.timestamp * 1000;
                    const altitude = data.altitude_bme;

                    altitudeChart.data.labels.push(timestampMs);
                    altitudeChart.data.datasets[0].data.push(altitude);

                    // Limiter le nombre de points dans le graphique
                    if (altitudeChart.data.labels.length > CONFIG.CHART_MAX_POINTS) {
                        altitudeChart.data.labels.shift();
                        altitudeChart.data.datasets[0].data.shift();
                    }
                    altitudeChart.update('none'); // Mettre à jour sans animation
                } catch (e) {
                    console.error("Failed to update chart:", e);
                }
            } else {
                // Optionnel: logguer si l'altitude ou le timestamp manque pour le graphique
                 // console.warn("Data received without valid altitude or timestamp for chart.");
            }
        } else {
             console.error("Cannot update chart: Chart is not initialized.");
        }
    }

    // =========================================================================
    // Gestion des Événements SocketIO
    // =========================================================================
    function setupSocketIO() {
        // Assurer une seule instance de socket
        if (socket) {
            socket.disconnect();
        }
        socket = io({
            // Options de connexion si nécessaire (ex: reconnectionAttempts: 5)
        });

        socket.on('connect', () => {
            console.log('SocketIO connected.');
            $ui.connectionStatus.removeClass('status-disconnected status-error').addClass('status-ok').attr('title', 'Websocket Connecté');
            clearPersistentError(); // Efface les erreurs précédentes (WS ou Série) à la connexion réussie
            // Le serveur devrait envoyer l'état initial (history, serial status) après connexion
        });

        socket.on('disconnect', (reason) => {
            console.warn('SocketIO disconnected:', reason);
            $ui.connectionStatus.removeClass('status-ok status-receiving status-connecting status-error').addClass('status-disconnected').attr('title', `Websocket Déconnecté: ${reason}`);
            setError("Connexion au serveur perdue.", true); // Erreur persistante
        });

        socket.on('connect_error', (err) => {
            console.error('SocketIO connection error:', err);
            $ui.connectionStatus.removeClass('status-ok status-receiving status-connecting').addClass('status-error').attr('title', `Erreur Websocket: ${err.message}`);
            setError(`Erreur de connexion Websocket: ${err.message}`, true); // Erreur persistante
        });

        // --- Réception des données temps réel ---
        socket.on('update_data', (data) => {
            // Effet visuel rapide pour montrer la réception
            $ui.connectionStatus.addClass('status-receiving');
            setTimeout(() => $ui.connectionStatus.removeClass('status-receiving'), 500);
            // Mettre à jour l'UI
            updateUI(data);
        });

        // --- Réception de l'historique initial ---
        socket.on('initial_history', (history) => {
            console.log(`Initial history received (${history?.length || 0} points)`);
            if (!history || history.length === 0) return;

            const chartLabels = [];
            const chartAltitudes = [];
            const trackPoints = [];
            let lastValidHistoryPoint = null;

            history.forEach(point => {
                // Pour le graphique
                if (point && typeof point.altitude_bme === 'number' && point.timestamp > 0) {
                    chartLabels.push(point.timestamp * 1000);
                    chartAltitudes.push(point.altitude_bme);
                }
                // Pour la trace carte
                if (point && typeof point.latitude === 'number' && typeof point.longitude === 'number') {
                    trackPoints.push([point.latitude, point.longitude]);
                    lastValidHistoryPoint = point; // Garder le dernier point GPS valide
                }
            });

            // Mettre à jour le graphique avec l'historique (limité)
            if (altitudeChart && chartLabels.length > 0) {
                altitudeChart.data.labels = chartLabels.slice(-CONFIG.CHART_MAX_POINTS);
                altitudeChart.data.datasets[0].data = chartAltitudes.slice(-CONFIG.CHART_MAX_POINTS);
                altitudeChart.update('none');
            }

            // Mettre à jour la trace carte avec l'historique (limité)
            if (balloonTrack && trackPoints.length > 0) {
                const limitedTrack = trackPoints.slice(-CONFIG.MAP_MAX_BALLOON_TRACK_POINTS);
                balloonTrack.setLatLngs(limitedTrack);
                 console.log(`Balloon track initialized with ${limitedTrack.length} points from history.`);
            }

            // Mettre à jour la position initiale du marqueur ballon avec le dernier point valide
            if (lastValidHistoryPoint) {
                console.log("Setting initial balloon position from history.");
                updateUI(lastValidHistoryPoint); // Utilise updateUI pour cohérence
                // On pourrait vouloir centrer la carte sur ce point initial
                if (map && lastKnownBalloonPosition) {
                    map.setView(lastKnownBalloonPosition, CONFIG.MAP_INITIAL_ZOOM);
                }
            }
        });

        // --- Réception du statut du port série ---
        socket.on('serial_status', (statusInfo) => {
            console.log("Serial Status:", statusInfo);
            const indicator = $ui.serialStatusIndicator;
            const text = $ui.serialStatusText;
            indicator.removeClass('status-ok status-error status-receiving status-connecting status-disconnected');
            // Réinitialiser l'état persistant de l'erreur navbar avant de potentiellement la redéfinir
            $ui.navbarError.data('is-persistent', false);

            switch (statusInfo.status) {
                case 'connected':
                    indicator.addClass('status-ok').attr('title', `Connecté sur ${statusInfo.port}`);
                    text.text('Connecté');
                    clearPersistentError(); // Efface une potentielle erreur série précédente
                    break;
                case 'receiving':
                    indicator.addClass('status-receiving').attr('title', `Données reçues (${statusInfo.port})`);
                    text.text('Réception...');
                    clearPersistentError(); // Efface aussi si on reçoit des données
                    break;
                case 'error':
                    indicator.addClass('status-error').attr('title', `Erreur: ${statusInfo.message}`);
                    text.text('Erreur');
                    setError(statusInfo.message || 'Erreur série inconnue', true); // Erreur série persistante
                    break;
                case 'disconnected':
                default:
                    indicator.addClass('status-disconnected').attr('title', 'Déconnecté');
                    text.text('Déconnecté');
                    setError('Port série déconnecté', true); // Déconnexion série = persistante
                    break;
            }
        });
    }

    // =========================================================================
    // Gestion des Actions Utilisateur (Boutons)
    // =========================================================================
    function setupButtonHandlers() {
        // --- Bouton Suivi Ballon ---
        $ui.toggleFollowBtn.on('click', function() {
            isFollowingBalloon = !isFollowingBalloon;
            $(this).text(isFollowingBalloon ? 'Suivi Ballon: ON' : 'Suivi Ballon: OFF')
                   .toggleClass('btn-primary btn-outline-primary');
             console.log("Balloon following toggled:", isFollowingBalloon);
            // Si on active le suivi et qu'on a une position, on centre la carte dessus
            if (isFollowingBalloon && lastKnownBalloonPosition && map) {
                 console.log("Panning to last known balloon position.");
                 map.panTo(lastKnownBalloonPosition);
            }
        });

        // --- Bouton Localisation Utilisateur ---
        $ui.trackMeBtn.on('click', function() {
            if (!navigator.geolocation) {
                alert("La géolocalisation n'est pas supportée par votre navigateur.");
                setError("Géolocalisation non supportée.", false);
                return;
            }

            const $button = $(this);
            $button.prop('disabled', true).html('<span class="spinner-border spinner-border-sm" role="status" aria-hidden="true"></span> Localisation...');

            navigator.geolocation.getCurrentPosition(
                (position) => {
                    const lat = position.coords.latitude;
                    const lon = position.coords.longitude;
                    const accuracy = position.coords.accuracy; // Précision en mètres

                    lastKnownUserPosition = L.latLng(lat, lon);
                    console.log(`User located at [${lat}, ${lon}] with accuracy ${accuracy}m.`);

                    // Mettre à jour le marqueur utilisateur
                    if (userMarker && map) {
                        let userPopupContent = `<b>Ma Position</b><br>Lat: ${lat.toFixed(4)}<br>Lon: ${lon.toFixed(4)}`;
                        if (accuracy) {
                            userPopupContent += `<br>Précision: ~${accuracy.toFixed(0)} m`;
                        }
                        userMarker.setLatLng(lastKnownUserPosition)
                                  .setOpacity(1) // Rendre visible
                                  .setPopupContent(userPopupContent)
                                  .openPopup(); // Afficher le popup avec la précision

                        // Mettre à jour ou créer le cercle de précision
                        if (accuracy) {
                             if (userAccuracyCircle) {
                                userAccuracyCircle.setLatLng(lastKnownUserPosition).setRadius(accuracy);
                             } else {
                                userAccuracyCircle = L.circle(lastKnownUserPosition, {
                                    radius: accuracy,
                                    weight: 1,
                                    color: 'blue',
                                    fillColor: '#cacaca',
                                    fillOpacity: 0.2
                                }).addTo(map);
                             }
                             // Optionnel: cacher le cercle si trop imprécis
                             // userAccuracyCircle.setStyle({ opacity: accuracy > 500 ? 0 : 1, fillOpacity: accuracy > 500 ? 0 : 0.2 });
                        } else if (userAccuracyCircle) {
                            // Si pas de précision retournée, cacher le cercle
                            userAccuracyCircle.setStyle({ opacity: 0, fillOpacity: 0 });
                        }

                        // Centrer la vue sur l'utilisateur
                        map.setView(lastKnownUserPosition, map.getZoom());

                        // Mettre à jour la distance et potentiellement la route
                        updateDistanceAndRoute();
                    }

                    $button.prop('disabled', false).html('📍 Localise Moi');
                },
                (error) => {
                    console.error("Geolocation error:", error);
                    let errorMsg = "Erreur de géolocalisation.";
                    switch(error.code) {
                       case error.PERMISSION_DENIED: errorMsg = "Permission de géolocalisation refusée."; break;
                       case error.POSITION_UNAVAILABLE: errorMsg = "Position actuelle indisponible."; break;
                       case error.TIMEOUT: errorMsg = "Timeout dépassé lors de la géolocalisation."; break;
                    }
                    alert(errorMsg);
                    setError(errorMsg, false); // Erreur non persistante
                    $button.prop('disabled', false).html('📍 Localise Moi');
                    // Cacher le marqueur utilisateur et le cercle si erreur? Ou le laisser à sa dernière position connue? Pour l'instant on le laisse.
                },
                { // Options de géolocalisation
                    enableHighAccuracy: true,
                    timeout: CONFIG.GEOLOCATION_TIMEOUT,
                    maximumAge: 0 // Forcer une position fraîche
                }
            );
        });
    }

    // =========================================================================
    // Calcul Distance et Mise à Jour Routage
    // =========================================================================
    function updateDistanceAndRoute() {
        if (lastKnownUserPosition && lastKnownBalloonPosition) {
            try {
                // Calculer la distance à vol d'oiseau
                const distanceMeters = lastKnownUserPosition.distanceTo(lastKnownBalloonPosition);
                $ui.distance.text(`${(distanceMeters / 1000).toFixed(2)} km`);

                // Mettre à jour les waypoints pour le calcul de route
                if (routingControl) {
                    routingControl.setWaypoints([lastKnownUserPosition, lastKnownBalloonPosition]);
                    // Le panneau de routage sera affiché par le listener 'routesfound' si une route est trouvée
                    // On le cache s'il n'y a pas de route (géré dans handleRouteError ou handleRouteFound sans route)
                }
            } catch (e) {
                console.error("Error calculating distance or setting waypoints:", e);
                $ui.distance.text('Erreur');
                $ui.routeInfo.text('Erreur');
                if (routingControl) $('.leaflet-routing-container').hide();
            }
        } else {
            // Si une des positions manque, pas de calcul possible
            $ui.distance.text('N/A');
            $ui.routeInfo.text('N/A');
            if (routingControl) {
                routingControl.setWaypoints([null, null]); // Vider les waypoints
                 $('.leaflet-routing-container').hide(); // Cacher le panneau
            }
        }
    }

    // Gestionnaire pour une route trouvée
    function handleRouteFound(e) {
        if (e.routes && e.routes.length > 0) {
            const summary = e.routes[0].summary;
            const distanceKm = (summary.totalDistance / 1000).toFixed(1);
            const timeMinutes = Math.round(summary.totalTime / 60);
            let timeString = "";
            if (timeMinutes < 60) {
                timeString = `${timeMinutes} min`;
            } else {
                timeString = `${Math.floor(timeMinutes / 60)}h ${timeMinutes % 60}min`;
            }
            $ui.routeInfo.text(`Route: ${distanceKm} km, ${timeString}`);
             $('.leaflet-routing-container').show(); // Afficher le panneau
            console.log(`Route found: ${distanceKm} km, ${timeString}`);
        } else {
            $ui.routeInfo.text('Route non trouvée');
             $('.leaflet-routing-container').hide(); // Cacher si pas de route
            console.warn("Routing completed but no routes found.");
        }
    }

    // Gestionnaire pour une erreur de routage
    function handleRouteError(e) {
        console.error("Routing error:", e.error);
        $ui.routeInfo.text('Erreur routage');
        $('.leaflet-routing-container').hide(); // Cacher en cas d'erreur
    }


    // =========================================================================
    // Vérification Périodique de la Fraîcheur des Données
    // =========================================================================
    function startDataFreshnessCheck() {
        setInterval(() => {
            if (lastValidDataTimestamp && (Date.now() - lastValidDataTimestamp > CONFIG.STALE_DATA_THRESHOLD_MS)) {
                console.warn(`Data is stale (no update received for > ${CONFIG.STALE_DATA_THRESHOLD_MS / 1000}s)`);
                // Indiquer visuellement que les données sont anciennes
                if (balloonMarker && balloonMarker.options.opacity === 1) { // Ne pas le faire si déjà transparent
                    balloonMarker.setOpacity(0.5);
                }
                // Optionnel : Mettre à jour le statut websocket pour indiquer le problème
                if ($ui.connectionStatus.hasClass('status-ok')) {
                    $ui.connectionStatus.attr('title', 'Websocket connecté, mais données non reçues récemment');
                    // On pourrait changer la classe aussi, mais 'status-ok' reste techniquement vrai pour WS
                }
                // Afficher une alerte non persistante ?
                // setError(`Données non reçues depuis > ${CONFIG.STALE_DATA_THRESHOLD_MS / 1000}s`, false);
            }
        }, 15000); // Vérifier toutes les 15 secondes
    }

    // =========================================================================
    // Point d'Entrée Principal
    // =========================================================================
    function initializeApp() {
        console.log("Initializing application...");
        initMap();
        initChart();
        setupSocketIO();
        setupButtonHandlers();
        startDataFreshnessCheck();
        updateDistanceAndRoute(); // Mettre N/A initialement
        console.log("Application initialized and ready.");
    }

    // Lancer l'initialisation une fois le DOM prêt
    initializeApp();

}); // Fin de $(document).ready