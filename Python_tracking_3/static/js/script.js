$(document).ready(function() {
    // --- Configuration ---
    const MAX_CHART_POINTS = 100;
    const SOMONE_COORDS = [14.498, -17.071];

    // --- Variables globales JS ---
    let map;
    let balloonMarker;
    let userMarker;
    let balloonTrack;
    let routingControl;
    let altitudeChart;
    let isFollowing = false;
    let lastBalloonPosition = null;
    let userPosition = null;
    let lastValidDataTimestamp = null; // Pour vérifier si les données sont fraîches

    // --- Éléments DOM (mis en cache pour performance) ---
    const $connectionStatus = $('#connection-status');
    const $serialStatusIndicator = $('#serial-status-indicator');
    const $serialStatusText = $('#serial-status-text');
    const $navbarError = $('#navbar-error');
    const $errorAlert = $('#error-alert');
    const $errorMessage = $('#error-message');

    // --- Initialisation SocketIO ---
    const socket = io();

    socket.on('connect', () => {
        console.log('Connecté au serveur SocketIO');
        $connectionStatus.removeClass('status-disconnected status-error').addClass('status-ok').attr('title', 'Websocket Connecté');
        // Le serveur enverra l'état initial (data, history, serial status)
    });

    socket.on('disconnect', () => {
        console.warn('Déconnecté du serveur SocketIO');
        $connectionStatus.removeClass('status-ok status-receiving status-connecting status-error').addClass('status-disconnected').attr('title', 'Websocket Déconnecté');
        // Optionnel: griser les données ou afficher un message
        setError("Connexion au serveur perdue.", true); // Afficher erreur persistante
    });

    socket.on('connect_error', (err) => {
        console.error('Erreur de connexion SocketIO:', err);
        $connectionStatus.removeClass('status-ok status-receiving status-connecting').addClass('status-error').attr('title', `Erreur Websocket: ${err.message}`);
        setError(`Erreur Websocket: ${err.message}`, true);
    });

    // --- Initialisation Carte et Graphique ---
    function initMap() {
        try {
            map = L.map('map').setView(SOMONE_COORDS, 13);
            const osmTile = L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', { attribution: '© OSM Contributors' }).addTo(map);
            const satelliteTile = L.tileLayer('https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}', { attribution: 'Tiles © Esri' });
             const topoTile = L.tileLayer('https://{s}.tile.opentopomap.org/{z}/{x}/{y}.png', { attribution: 'Map data: © OpenTopoMap contributors' });

            const baseMaps = { "OpenStreetMap": osmTile, "Satellite": satelliteTile, "Topographique": topoTile };
            L.control.layers(baseMaps).addTo(map);

            const balloonIcon = L.icon({ iconUrl: 'https://img.icons8.com/office/40/000000/hot-air-balloon.png', iconSize: [32, 32], iconAnchor: [16, 32], popupAnchor: [0, -32] });
            balloonMarker = L.marker(SOMONE_COORDS, { icon: balloonIcon, opacity: 0.5 }).addTo(map).bindPopup("Ballon (en attente)"); //.openPopup(); // Pas d'openPopup initial

             const userIcon = L.icon({ iconUrl: 'https://img.icons8.com/color/48/000000/marker.png', iconSize: [32, 32], iconAnchor: [16, 32], popupAnchor: [0, -32] });
            userMarker = L.marker(SOMONE_COORDS, { icon: userIcon, opacity: 0 }).addTo(map).bindPopup("Ma Position"); // Invisible au début

            balloonTrack = L.polyline([], { color: 'red', weight: 3 }).addTo(map);

            routingControl = L.Routing.control({
                waypoints: [null, null], routeWhileDragging: false, show: false, addWaypoints: false, draggableWaypoints: false,
                lineOptions: { styles: [{ color: 'blue', opacity: 0.6, weight: 4 }] },
                router: L.Routing.osrmv1({ serviceUrl: 'https://router.project-osrm.org/route/v1' }),
                createMarker: function() { return null; }
            }).addTo(map);
            $('.leaflet-routing-container').hide();
            console.log("Carte initialisée.");
        } catch (e) {
            console.error("Erreur initialisation carte:", e);
            setError("Impossible d'initialiser la carte.", true);
        }
    }

    function initChart() {
       try {
            const ctx = document.getElementById('altitudeChart').getContext('2d');
            altitudeChart = new Chart(ctx, {
                type: 'line',
                data: { labels: [], datasets: [{ label: 'Altitude Barométrique (m)', data: [], borderColor: 'rgb(75, 192, 192)', backgroundColor: 'rgba(75, 192, 192, 0.2)', tension: 0.1, pointRadius: 1, fill: true }] },
                options: {
                    scales: {
                        x: { type: 'time', time: { unit: 'minute', tooltipFormat: 'HH:mm:ss', displayFormats: { minute: 'HH:mm' } }, title: { display: true, text: 'Temps' } },
                        y: { title: { display: true, text: 'Altitude (m)' }, beginAtZero: false } },
                    animation: { duration: 0 }, maintainAspectRatio: false, plugins: { legend: { display: false } }
                }
            });
            console.log("Graphique initialisé.");
       } catch (e) {
           console.error("Erreur initialisation graphique:", e);
           setError("Impossible d'initialiser le graphique.", true);
       }
    }

     // --- Fonctions Helper ---
    const na = (val) => (val === null || typeof val === 'undefined') ? 'N/A' : val;
    const formatFloat = (val, dec = 1) => (val === null || typeof val === 'undefined' || isNaN(parseFloat(val))) ? 'N/A' : parseFloat(val).toFixed(dec);
    const formatInt = (val) => (val === null || typeof val === 'undefined' || isNaN(parseInt(val))) ? 'N/A' : parseInt(val);

    function getAirQualityText(index) {
        const idx = parseInt(index);
        if (isNaN(idx)) return "N/A";
        const qualityMap = { 1: "Excellent", 2: "Bon", 3: "Moyen", 4: "Mauvais", 5: "Malsain" };
        return qualityMap[idx] || "Inconnu";
    }

    function setError(message, isPersistent = false) {
        if (message) {
            console.error("Erreur UI:", message);
            $errorMessage.text(message);
            $errorAlert.removeClass('d-none');
             // Afficher aussi dans la navbar pour erreurs persistantes
             if(isPersistent){
                 $navbarError.text(message).removeClass('d-none');
             }
        } else {
            $errorAlert.addClass('d-none');
             $navbarError.addClass('d-none'); // Cacher aussi l'erreur navbar
        }
    }

     // --- Mise à jour UI ---
    function updateUI(data) {
        // console.log("Update UI avec:", data); // Décommenter pour debug détaillé
        if (!data) {
            console.warn("updateUI appelée avec des données nulles.");
            return;
        }

         // Gérer l'erreur globale venant du serveur
         if (data.error) {
             setError(data.error, true); // Erreur persistante venant du serveur
         } else {
             setError(null); // Effacer l'erreur si les données reçues sont OK
         }

        // MAJ Données Textuelles (vérifie null/undefined avec les helpers)
        $('#latitude').text(formatFloat(data.latitude, 5));
        $('#longitude').text(formatFloat(data.longitude, 5));
        $('#altitude_gps').text(formatFloat(data.altitude_gps, 1));
        $('#satellites').text(formatInt(data.satellites));
        $('#speed_kmh').text(formatFloat(data.speed_kmh, 1));
        $('#rssi').text(formatInt(data.rssi));
        $('#temperature').text(formatFloat(data.temperature, 1));
        const pressureHpa = (data.pressure !== null && typeof data.pressure !== 'undefined') ? (parseFloat(data.pressure) / 100.0).toFixed(1) : 'N/A';
        $('#pressure').text(pressureHpa);
        $('#humidity').text(formatFloat(data.humidity, 1));
        $('#altitude_bme').text(formatFloat(data.altitude_bme, 1));
        const aq = formatInt(data.air_quality);
        $('#air_quality').text(aq);
        $('#air_quality_text').text(getAirQualityText(aq));
        $('#tvoc').text(formatInt(data.tvoc));
        $('#eco2').text(formatInt(data.eco2));
        $('#ozone').text(formatInt(data.ozone));
        $('#uv_index').text(formatFloat(data.uv_index, 1));

        // MAJ Timestamp
         if (data.timestamp) {
             try {
                 // Convertir timestamp UNIX (secondes) en millisecondes pour JS Date
                 const date = new Date(data.timestamp * 1000);
                 $('#timestamp').text(date.toLocaleString('fr-FR', { dateStyle: 'short', timeStyle: 'medium' }));
                 lastValidDataTimestamp = Date.now(); // Noter quand on a reçu la dernière donnée valide
             } catch(e) {
                  $('#timestamp').text('Invalide');
                  console.warn("Erreur formatage timestamp:", e, data.timestamp);
             }
        } else {
            $('#timestamp').text('N/A');
        }

        // MAJ Carte (seulement si lat/lon sont des nombres valides)
        if (typeof data.latitude === 'number' && typeof data.longitude === 'number') {
            const newPos = [data.latitude, data.longitude];
            lastBalloonPosition = L.latLng(newPos);

            if (balloonMarker) {
                balloonMarker.setLatLng(newPos).setOpacity(1); // Rendre visible
                let popupContent = `<b>Ballon</b><br>Lat: ${data.latitude.toFixed(4)}<br>Lon: ${data.longitude.toFixed(4)}`;
                if (typeof data.altitude_gps === 'number') popupContent += `<br>Alt GPS: ${data.altitude_gps.toFixed(0)}m`;
                if (typeof data.speed_kmh === 'number') popupContent += `<br>Vit: ${data.speed_kmh.toFixed(1)}km/h`;
                balloonMarker.setPopupContent(popupContent);
                // Ne pas ouvrir le popup automatiquement sauf si l'utilisateur clique
            } else { console.error("balloonMarker non défini!"); }

            if (balloonTrack) {
                balloonTrack.addLatLng(newPos);
                 // Optionnel: Limiter le nombre de points dans la trace pour la performance
                 const maxTrackPoints = 500;
                 let latlngs = balloonTrack.getLatLngs();
                 if (latlngs.length > maxTrackPoints) {
                     latlngs.splice(0, latlngs.length - maxTrackPoints); // Enlever les plus anciens
                     balloonTrack.setLatLngs(latlngs);
                 }
            } else { console.error("balloonTrack non défini!"); }

            if (isFollowing && map) { map.panTo(newPos); }
            if (routingControl && userPosition) {
                routingControl.setWaypoints([userPosition, lastBalloonPosition]);
                $('.leaflet-routing-container').show();
            }
            updateDistanceAndRoute();
        } else {
             // Si pas de GPS, rendre le marqueur semi-transparent et mettre à jour popup
             if (balloonMarker) {
                 balloonMarker.setOpacity(0.5).setPopupContent("Ballon (Position GPS non reçue)");
             }
        }

        // MAJ Graphique (seulement si altitude_bme et timestamp sont valides)
        if (altitudeChart && typeof data.altitude_bme === 'number' && data.timestamp) {
            try {
                const timestamp_ms = data.timestamp * 1000; // Convertir en ms
                const altitude = data.altitude_bme;

                // Vérifier si le timestamp est raisonnable (évite dates étranges si timestamp corrompu)
                if (timestamp_ms > 0) {
                    altitudeChart.data.labels.push(timestamp_ms);
                    altitudeChart.data.datasets[0].data.push(altitude);

                    if (altitudeChart.data.labels.length > MAX_CHART_POINTS) {
                        altitudeChart.data.labels.shift();
                        altitudeChart.data.datasets[0].data.shift();
                    }
                    altitudeChart.update('none'); // 'none' pour désactiver l'animation si besoin
                } else {
                     console.warn("Timestamp invalide pour graphique:", data.timestamp);
                }
            } catch (e) {
                console.error("Erreur mise à jour graphique:", e);
            }
        }
    }

    // --- Gestionnaires d'événements SocketIO ---
    socket.on('update_data', (data) => {
        // Met à jour l'interface avec les dernières données reçues
        updateUI(data);
    });

    socket.on('initial_history', (history) => {
        console.log(`Historique initial reçu (${history.length} points)`);
        if (altitudeChart && history && history.length > 0) {
             const labels = [];
             const altitudes = [];
             const trackPoints = [];
             history.forEach(point => {
                 if (point && typeof point.altitude_bme === 'number' && point.timestamp) {
                     try { labels.push(point.timestamp * 1000); altitudes.push(point.altitude_bme); } catch(e){}
                 }
                 if (point && typeof point.latitude === 'number' && typeof point.longitude === 'number') {
                     trackPoints.push([point.latitude, point.longitude]);
                 }
             });
             altitudeChart.data.labels = labels;
             altitudeChart.data.datasets[0].data = altitudes;
             altitudeChart.update('none');

             if(balloonTrack && trackPoints.length > 0) { balloonTrack.setLatLngs(trackPoints); }
        } else {
             console.log("Historique vide ou graphique non prêt.");
        }
    });

    socket.on('serial_status', (statusInfo) => {
        console.log("Statut Série:", statusInfo);
        const indicator = $serialStatusIndicator;
        const text = $serialStatusText;
        const navbarErr = $navbarError;
        indicator.removeClass('status-ok status-error status-receiving status-connecting status-disconnected');

        switch (statusInfo.status) {
            case 'connected':
                indicator.addClass('status-ok').attr('title', `Connecté sur ${statusInfo.port}`);
                text.text('Connecté');
                navbarErr.addClass('d-none'); // Cacher erreur navbar si connecté
                break;
            case 'receiving':
                 indicator.addClass('status-receiving').attr('title', `Données reçues (${statusInfo.port})`);
                 text.text('Réception...');
                 navbarErr.addClass('d-none');
                 break;
            case 'error':
                indicator.addClass('status-error').attr('title', `Erreur: ${statusInfo.message}`);
                text.text('Erreur');
                 // Afficher l'erreur dans la navbar aussi
                 navbarErr.text(statusInfo.message || 'Erreur inconnue').removeClass('d-none');
                break;
            case 'disconnected':
            default:
                indicator.addClass('status-disconnected').attr('title', 'Déconnecté');
                text.text('Déconnecté');
                navbarErr.text('Port série déconnecté').removeClass('d-none'); // Afficher déconnecté
                break;
        }
    });

    // --- Gestion des boutons ---
    $('#toggle-follow-btn').on('click', function() {
        isFollowing = !isFollowing;
        $(this).text(isFollowing ? 'Suivi Ballon: ON' : 'Suivi Ballon: OFF');
        $(this).toggleClass('btn-outline-primary btn-primary');
        if (isFollowing && lastBalloonPosition && map) { map.panTo(lastBalloonPosition); }
    });

    $('#track-me-btn').on('click', function() {
        if (!navigator.geolocation) { alert("Géolocalisation non supportée."); return; }
        $(this).prop('disabled', true).text('Localisation...');
        navigator.geolocation.getCurrentPosition(
            (position) => {
                userPosition = L.latLng(position.coords.latitude, position.coords.longitude);
                if (userMarker && map) { userMarker.setLatLng(userPosition).setOpacity(1); }
                if (routingControl) { routingControl.setWaypoints([userPosition, lastBalloonPosition]); $('.leaflet-routing-container').show(); }
                updateDistanceAndRoute();
                $('#track-me-btn').prop('disabled', false).text('📍 Localise Moi');
            },
            (error) => {
                 console.error("Erreur Géoloc:", error); alert(`Erreur Géoloc: ${error.message}`);
                 $('#track-me-btn').prop('disabled', false).text('📍 Localise Moi'); },
            { enableHighAccuracy: true, timeout: 10000, maximumAge: 0 }
        );
    });

     // --- Fonctions de mise à jour distance/route ---
     function updateDistanceAndRoute() {
         if (userPosition && lastBalloonPosition) {
             const distanceMeters = userPosition.distanceTo(lastBalloonPosition);
             $('#distance').text(`${(distanceMeters / 1000).toFixed(2)} km`);
             // Info route (si routage a réussi)
             routingControl.on('routesfound', function(e) {
                 if (e.routes && e.routes.length > 0) {
                     const summary = e.routes[0].summary;
                     $('#route-info').text(`${(summary.totalDistance / 1000).toFixed(1)} km, ${Math.round(summary.totalTime / 60)} min`);
                 } else { $('#route-info').text('Route non trouvée'); }
             });
             routingControl.on('routingerror', function(e) { $('#route-info').text('Erreur routage'); });
         } else {
             $('#distance').text('N/A'); $('#route-info').text('N/A');
         }
     }

     // --- Vérification périodique de la fraîcheur des données ---
     setInterval(() => {
         if (lastValidDataTimestamp && (Date.now() - lastValidDataTimestamp > 60000)) { // > 60 secondes
             console.warn("Aucune donnée reçue depuis plus de 60 secondes.");
             // Optionnel: Afficher un avertissement ou griser les données
             // setError("Aucune donnée reçue depuis > 60s", false); // Erreur non persistante
             // Rendre le marqueur ballon semi-transparent pour indiquer le délai ?
             if(balloonMarker) balloonMarker.setOpacity(0.5);
         }
     }, 15000); // Vérifier toutes les 15 secondes

    // --- Initialisation ---
    console.log("Initialisation de l'interface...");
    initMap();
    initChart();
    console.log("Interface prête.");

}); // Fin de document.ready