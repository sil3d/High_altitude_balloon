$(document).ready(function () {
  // =========================================================================
  // Configuration
  // =========================================================================
  const CONFIG = {
    MAP_INITIAL_COORDS: [14.498, -17.071], // Coordonnées initiales
    MAP_INITIAL_ZOOM: 13,
    MAP_MAX_BALLOON_TRACK_POINTS: 500, // Max points pour le tracé du ballon
    CHART_MAX_POINTS: 100, // Max points pour le graphique
    STALE_DATA_THRESHOLD_MS: 60000, // 1 minute avant de considérer les données périmées
    GEOLOCATION_TIMEOUT: 10000, // Timeout pour la géolocalisation (10s)
    GEOLOCATION_MAX_AGE: 0,     // Forcer une nouvelle position à chaque fois
    GEOLOCATION_HIGH_ACCURACY: true, // Préférer le GPS pour la géoloc utilisateur
    USER_MARKER_ICON_URL: "https://img.icons8.com/color/48/000000/marker.png",
    BALLOON_MARKER_ICON_URL: "https://img.icons8.com/office/40/000000/hot-air-balloon.png",
    OSRM_SERVICE_URL: "https://router.project-osrm.org/route/v1", // Service de routage
  };

  // =========================================================================
  // Variables Globales
  // =========================================================================
  let map;
  let balloonMarker;
  let userMarker;
  let userAccuracyCircle = null; // Cercle de précision pour l'utilisateur
  let balloonTrack; // Tracé du ballon (Polyline)
  let routingControl; // Contrôle de routage Leaflet
  let altitudeChart; // Instance du graphique Chart.js
  let socket; // Connexion WebSocket

  let isFollowingBalloon = false; // Flag pour le suivi auto du ballon sur la carte
  let lastKnownBalloonPosition = null; // Dernières coordonnées valides reçues du ballon
  let firstValidBalloonPosition = null; // PREMIÈRES coordonnées valides reçues (départ du track)
  let lastKnownUserPosition = null; // Dernières coordonnées connues de l'utilisateur
  let lastValidDataTimestamp = null; // Timestamp de la dernière donnée reçue du serveur
  let geolocationWatchId = null; // ID pour le suivi continu de la position utilisateur (watchPosition)

  // =========================================================================
  // Cache des éléments DOM (jQuery)
  // =========================================================================
  const $ui = {
    connectionStatus: $("#connection-status"),
    serialStatusIndicator: $("#serial-status-indicator"),
    serialStatusText: $("#serial-status-text"),
    navbarError: $("#navbar-error"),
    errorAlert: $("#error-alert"),
    errorMessage: $("#error-message"),
    timestamp: $("#timestamp"),
    latitude: $("#latitude"),
    longitude: $("#longitude"),
    altitudeGps: $("#altitude_gps"),
    satellites: $("#satellites"),
    speedKmh: $("#speed_kmh"),
    rssi: $("#rssi"),
    temperature: $("#temperature"),
    pressure: $("#pressure"),
    humidity: $("#humidity"),
    altitudeBme: $("#altitude_bme"),
    airQuality: $("#air_quality"),
    airQualityText: $("#air_quality_text"),
    tvoc: $("#tvoc"),
    eco2: $("#eco2"),
    ozone: $("#ozone"),
    uvIndex: $("#uv_index"),
    pm1_std: $("#pm1_std"),
    pm25_std: $("#pm25_std"),
    pm10_std: $("#pm10_std"),
    distance: $("#distance"), // Distance à vol d'oiseau
    routeInfo: $("#route-info"), // Infos de routage (distance/temps)
    toggleFollowBtn: $("#toggle-follow-btn"), // Bouton Suivi Ballon ON/OFF
    // --- MODIFIÉ: ID et variable pour le bouton de suivi utilisateur ---
    toggleTrackMeBtn: $("#toggle-track-me-btn"), // !! Assurez-vous que l'ID dans le HTML est bien "toggle-track-me-btn" !!
    // --- FIN MODIFICATION ---
    altitudeChartCanvas: $("#altitudeChart"), // Canvas du graphique
  };

  // =========================================================================
  // Fonctions Utilitaires (Helpers)
  // =========================================================================
  const formatFloat = (val, dec = 1) =>
    val === null || typeof val === "undefined" || isNaN(parseFloat(val))
      ? "N/A"
      : parseFloat(val).toFixed(dec);
  const formatInt = (val) =>
    val === null || typeof val === "undefined" || isNaN(parseInt(val))
      ? "N/A"
      : parseInt(val);

  function getAirQualityText(index) {
    const idx = parseInt(index);
    if (isNaN(idx)) return "N/A";
    const qualityMap = { 1: "Excellent", 2: "Bon", 3: "Moyen", 4: "Mauvais", 5: "Très Mauvais"};
    return qualityMap[idx] || "Inconnu";
  }

  function setError(message, isPersistent = false) {
    if (message) {
      console.error("UI Error:", message);
      $ui.errorMessage.text(message);
      $ui.errorAlert.removeClass("d-none");
      if (isPersistent) {
        $ui.navbarError.text(message).removeClass("d-none").data("is-persistent", true);
      }
    } else {
      $ui.errorAlert.addClass("d-none");
      if (!$ui.navbarError.data("is-persistent")) {
        $ui.navbarError.addClass("d-none");
      }
    }
  }

  function clearPersistentError() {
    console.log("Clearing persistent errors");
    $ui.navbarError.addClass("d-none").data("is-persistent", false);
    setError(null); // Efface aussi l'alerte non persistante
  }

  // =========================================================================
  // Initialisation Carte & Leaflet
  // =========================================================================
  function initMap() {
    try {
      map = L.map("map", {
        center: CONFIG.MAP_INITIAL_COORDS,
        zoom: CONFIG.MAP_INITIAL_ZOOM,
      });

      // Couches de fond (Tiles)
      const osmTile = L.tileLayer("https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png", { attribution: "© OSM Contributors" }).addTo(map);
      const satelliteTile = L.tileLayer("https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}", { attribution: "Tiles © Esri" });
      const topoTile = L.tileLayer("https://{s}.tile.opentopomap.org/{z}/{x}/{y}.png", { attribution: "Map data: © OpenTopoMap contributors"});
      const baseMaps = { OpenStreetMap: osmTile, Satellite: satelliteTile, Topographique: topoTile };
      L.control.layers(baseMaps).addTo(map);

      // Icônes personnalisées
      const balloonIcon = L.icon({ iconUrl: CONFIG.BALLOON_MARKER_ICON_URL, iconSize: [32, 32], iconAnchor: [16, 32], popupAnchor: [0, -32] });
      const userIcon = L.icon({ iconUrl: CONFIG.USER_MARKER_ICON_URL, iconSize: [32, 32], iconAnchor: [16, 32], popupAnchor: [0, -32] });

      // Position initiale du ballon (avant réception de données)
      const initialBalloonLatLng = L.latLng(CONFIG.MAP_INITIAL_COORDS[0], CONFIG.MAP_INITIAL_COORDS[1]);
      lastKnownBalloonPosition = initialBalloonLatLng; // Initialiser ici aussi

      balloonMarker = L.marker(initialBalloonLatLng, { icon: balloonIcon, opacity: 0.6 }) // Commencer semi-transparent
        .addTo(map)
        .bindPopup("Ballon (Position initiale/par défaut)");

      // Marqueur utilisateur (commence invisible)
      userMarker = L.marker(CONFIG.MAP_INITIAL_COORDS, { icon: userIcon, opacity: 0 })
        .addTo(map)
        .bindPopup("Ma Position");

      // Tracé du ballon (initialement vide)
      balloonTrack = L.polyline([], { color: "red", weight: 3 }).addTo(map);

      // Contrôle de routage (initialement caché)
      routingControl = L.Routing.control({
        waypoints: [null, null],
        routeWhileDragging: false,
        show: false, // Important: ne pas afficher l'itinéraire textuel par défaut
        addWaypoints: false,
        draggableWaypoints: false,
        lineOptions: { styles: [{ color: "blue", opacity: 0.6, weight: 4 }] },
        router: L.Routing.osrmv1({ serviceUrl: CONFIG.OSRM_SERVICE_URL }),
        createMarker: () => null, // Ne pas créer de marqueurs pour les waypoints
      }).addTo(map);

      $(".leaflet-routing-container").hide(); // Cacher l'interface de routage
      routingControl.on("routesfound", handleRouteFound);
      routingControl.on("routingerror", handleRouteError);

      console.log("Map initialized successfully.");
      updateDistanceAndRoute(); // Mettre à jour l'affichage initial (N/A)
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
      const ctx = $ui.altitudeChartCanvas[0].getContext("2d");
      altitudeChart = new Chart(ctx, {
        type: "line",
        data: {
          labels: [], // Timestamps (ms)
          datasets: [{
              label: "Altitude Barométrique (m)",
              data: [], // Altitudes
              borderColor: "rgb(75, 192, 192)",
              backgroundColor: "rgba(75, 192, 192, 0.2)",
              tension: 0.1, pointRadius: 1, fill: true,
          }],
        },
        options: {
          scales: {
            x: { type: "time", time: { unit: "minute", tooltipFormat: "HH:mm:ss", displayFormats: { minute: "HH:mm" } }, title: { display: true, text: "Temps" }},
            y: { title: { display: true, text: "Altitude (m)" }, beginAtZero: false },
          },
          animation: { duration: 0 }, maintainAspectRatio: false, plugins: { legend: { display: false } },
        },
      });
      console.log("Chart initialized successfully.");
    } catch (error) {
      console.error("Chart Initialization failed:", error);
      setError("Impossible d'initialiser le graphique.", true);
    }
  }

  // =========================================================================
  // Mise à jour de l'Interface Utilisateur (UI) - Réception Données Ballon
  // =========================================================================
  function updateUI(data) {
    if (!data) { console.warn("updateUI called with null data."); return; }

    // Gérer Erreur Serveur spécifique
    if (data.error) { setError(`Erreur serveur: ${data.error}`, true); return; }
    else { setError(null, false); } // Efface erreur NON persistante si succès

    // Timestamp et Fraîcheur
    if (data.timestamp) {
      try {
        const date = new Date(data.timestamp * 1000);
        $ui.timestamp.text(date.toLocaleString("fr-FR", { dateStyle: "short", timeStyle: "medium" }));
        lastValidDataTimestamp = Date.now();
      } catch (e) { $ui.timestamp.text("Invalide"); }
    } else { $ui.timestamp.text("N/A"); }

    // Mise à jour Données Textuelles (GPS, Env, Air, PMS...)
    $ui.latitude.text(formatFloat(data.latitude, 5));
    $ui.longitude.text(formatFloat(data.longitude, 5));
    $ui.altitudeGps.text(formatFloat(data.altitude_gps, 1));
    $ui.satellites.text(formatInt(data.satellites));
    $ui.speedKmh.text(formatFloat(data.speed_kmh, 1));
    $ui.rssi.text(formatInt(data.rssi));
    $ui.temperature.text(formatFloat(data.temperature, 1));
    const pressureHpa = data.pressure !== null && typeof data.pressure !== "undefined" ? (parseFloat(data.pressure) / 100.0).toFixed(1) : "N/A";
    $ui.pressure.text(pressureHpa);
    $ui.humidity.text(formatFloat(data.humidity, 1));
    $ui.altitudeBme.text(formatFloat(data.altitude_bme, 1));
    const aq = formatInt(data.air_quality);
    $ui.airQuality.text(aq); $ui.airQualityText.text(getAirQualityText(aq));
    $ui.tvoc.text(formatInt(data.tvoc)); $ui.eco2.text(formatInt(data.eco2));
    $ui.ozone.text(formatInt(data.ozone)); $ui.uvIndex.text(formatFloat(data.uv_index, 1));
    $ui.pm1_std.text(formatInt(data.pm1_std)); $ui.pm25_std.text(formatInt(data.pm25_std));
    $ui.pm10_std.text(formatInt(data.pm10_std));

    // Mise à jour Carte (Marqueur Ballon + Tracé)
    const hasValidGps = typeof data.latitude === "number" && typeof data.longitude === "number" && !isNaN(data.latitude) && !isNaN(data.longitude);

    if (map && balloonMarker) {
      if (hasValidGps) {
        const newLatLng = L.latLng(data.latitude, data.longitude);
        lastKnownBalloonPosition = newLatLng; // Mettre à jour la dernière position connue

        // --- Stocker la première position GPS valide reçue ---
        if (firstValidBalloonPosition === null) {
          firstValidBalloonPosition = newLatLng;
          console.log("Première position GPS valide du ballon enregistrée:", firstValidBalloonPosition);
        }
        // --- Fin stockage première position ---

        balloonMarker.setLatLng(newLatLng).setOpacity(1); // Rendre opaque
        let popupContent = `<b>Ballon</b><br>Lat: ${data.latitude.toFixed(4)}<br>Lon: ${data.longitude.toFixed(4)}`;
        if (typeof data.altitude_gps === "number") popupContent += `<br>Alt GPS: ${data.altitude_gps.toFixed(0)}m`;
        if (typeof data.speed_kmh === "number") popupContent += `<br>Vit: ${data.speed_kmh.toFixed(1)}km/h`;
        balloonMarker.setPopupContent(popupContent);

        // Ajouter au tracé
        if (balloonTrack) {
          balloonTrack.addLatLng(newLatLng);
          let latlngs = balloonTrack.getLatLngs();
          if (latlngs.length > CONFIG.MAP_MAX_BALLOON_TRACK_POINTS) {
            latlngs.splice(0, latlngs.length - CONFIG.MAP_MAX_BALLOON_TRACK_POINTS);
            balloonTrack.setLatLngs(latlngs);
          }
        }

        // Suivre le ballon si activé
        if (isFollowingBalloon) map.panTo(newLatLng);
        updateDistanceAndRoute(); // Mettre à jour distance/route (si user localisé)

      } else {
        // --- Gestion Perte de Signal GPS Ballon ---
        balloonMarker.setOpacity(0.6); // Rendre semi-transparent
        if (lastKnownBalloonPosition && !lastKnownBalloonPosition.equals(L.latLng(CONFIG.MAP_INITIAL_COORDS[0], CONFIG.MAP_INITIAL_COORDS[1]))) {
          // Si on a déjà eu une position VALIDE (différente de l'initiale)
          balloonMarker.setLatLng(lastKnownBalloonPosition); // Laisser à la dernière position
          let popupContent = `<b>Ballon (Signal GPS Perdu)</b><br>Dernière position:<br>Lat: ${lastKnownBalloonPosition.lat.toFixed(4)}<br>Lon: ${lastKnownBalloonPosition.lng.toFixed(4)}`;
          // Ajouter la position de départ si elle existe et est différente de la dernière
          if (firstValidBalloonPosition && !firstValidBalloonPosition.equals(lastKnownBalloonPosition, 0.0001)) {
             popupContent += `<br><hr style='margin: 3px 0;'>Position départ track:<br>Lat: ${firstValidBalloonPosition.lat.toFixed(4)}<br>Lon: ${firstValidBalloonPosition.lng.toFixed(4)}`;
          }
          balloonMarker.setPopupContent(popupContent);
        } else {
          // Si on n'a JAMAIS eu de position valide (ou si on est toujours sur l'initiale)
          balloonMarker.setPopupContent("Ballon (En attente du premier signal GPS valide)");
          // Le marqueur reste où il était (initialement ou à la dernière pos)
        }
        // Ne pas mettre à jour la distance/route car la position du ballon n'est pas fiable
      }
    } else { console.error("Map/Marker non initialisé pour updateUI"); }

    // Mise à jour Graphique Altitude
    const hasValidAltitude = typeof data.altitude_bme === "number" && !isNaN(data.altitude_bme);
    const hasValidTimestampForChart = data.timestamp && data.timestamp > 0;

    if (altitudeChart) {
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
          altitudeChart.update("none"); // 'none' pour éviter l'animation
        } catch (e) { console.error("Failed to update chart:", e); }
      }
    } else { console.error("Chart non initialisé pour updateUI"); }
  }

  // =========================================================================
  // Gestion des Événements SocketIO (Connexion Serveur)
  // =========================================================================
  function setupSocketIO() {
    if (socket) socket.disconnect();
    socket = io(); // Connexion au serveur Socket.IO

    socket.on("connect", () => {
      console.log("SocketIO connected.");
      $ui.connectionStatus.removeClass("status-disconnected status-error").addClass("status-ok").attr("title", "Websocket Connecté");
      clearPersistentError();
    });

    socket.on("disconnect", (reason) => {
      console.warn("SocketIO disconnected:", reason);
      $ui.connectionStatus.removeClass("status-ok status-receiving status-connecting status-error").addClass("status-disconnected").attr("title", `Websocket Déconnecté: ${reason}`);
      setError("Connexion au serveur perdue.", true);
    });

    socket.on("connect_error", (err) => {
      console.error("SocketIO connection error:", err);
      $ui.connectionStatus.removeClass("status-ok status-receiving status-connecting").addClass("status-error").attr("title", `Erreur Websocket: ${err.message}`);
      setError(`Erreur de connexion Websocket: ${err.message}`, true);
    });

    // Réception des données mises à jour
    socket.on("update_data", (data) => {
      $ui.connectionStatus.addClass("status-receiving"); // Feedback visuel rapide
      setTimeout(() => $ui.connectionStatus.removeClass("status-receiving"), 500);
      updateUI(data); // Appeler la fonction principale de mise à jour
    });

    // Réception de l'historique initial
    socket.on("initial_history", (history) => {
      console.log(`Initial history received (${history?.length || 0} points)`);
      if (!history || history.length === 0) return;

      const chartLabels = []; const chartAltitudes = []; const trackPoints = [];
      let lastValidHistoryPoint = null; firstValidBalloonPosition = null; // Reset first pos

      history.forEach((point) => {
        if (point && typeof point.altitude_bme === "number" && point.timestamp > 0) {
          chartLabels.push(point.timestamp * 1000); chartAltitudes.push(point.altitude_bme);
        }
        if (point && typeof point.latitude === "number" && typeof point.longitude === "number") {
          const histLatLng = L.latLng(point.latitude, point.longitude);
          trackPoints.push(histLatLng);
          lastValidHistoryPoint = point; // Garder le dernier point valide
          if (firstValidBalloonPosition === null) { // Trouver la première position valide de l'historique
              firstValidBalloonPosition = histLatLng;
          }
        }
      });

      // Mettre à jour le graphique avec l'historique
      if (altitudeChart && chartLabels.length > 0) {
        altitudeChart.data.labels = chartLabels.slice(-CONFIG.CHART_MAX_POINTS);
        altitudeChart.data.datasets[0].data = chartAltitudes.slice(-CONFIG.CHART_MAX_POINTS);
        altitudeChart.update("none");
      }
      // Mettre à jour le tracé du ballon avec l'historique
      if (balloonTrack && trackPoints.length > 0) {
        const limitedTrack = trackPoints.slice(-CONFIG.MAP_MAX_BALLOON_TRACK_POINTS);
        balloonTrack.setLatLngs(limitedTrack);
      }
      // Mettre à jour l'UI avec le dernier point de l'historique
      if (lastValidHistoryPoint) {
        updateUI(lastValidHistoryPoint); // Appelle updateUI qui gère lastKnown/firstValid
        // Centrer la vue sur la dernière position connue du ballon après chargement historique
        if (map && lastKnownBalloonPosition) {
          map.setView(lastKnownBalloonPosition, CONFIG.MAP_INITIAL_ZOOM);
        }
      }
    });

    // Réception du statut de la connexion série (Arduino/ESP)
    socket.on("serial_status", (statusInfo) => {
      console.log("Serial Status:", statusInfo);
      const indicator = $ui.serialStatusIndicator; const text = $ui.serialStatusText;
      indicator.removeClass("status-ok status-error status-receiving status-connecting status-disconnected");
      $ui.navbarError.data("is-persistent", false); // Réinitialiser avant de potentiellement définir une erreur persistante

      switch (statusInfo.status) {
        case "connected":
          indicator.addClass("status-ok").attr("title", `Connecté sur ${statusInfo.port}`); text.text("Connecté"); clearPersistentError(); break;
        case "receiving":
          indicator.addClass("status-receiving").attr("title", `Données reçues (${statusInfo.port})`); text.text("Réception..."); clearPersistentError(); break;
        case "error":
          indicator.addClass("status-error").attr("title", `Erreur: ${statusInfo.message}`); text.text("Erreur"); setError(statusInfo.message || "Erreur série inconnue", true); break;
        case "disconnected": default:
          indicator.addClass("status-disconnected").attr("title", "Déconnecté"); text.text("Déconnecté"); setError("Port série déconnecté", true); break;
      }
    });
  }

  // =========================================================================
  // Géolocalisation Continue de l'Utilisateur (watchPosition)
  // =========================================================================

  /** Met à jour la position de l'utilisateur sur la carte */
  function updateUserMapPosition(position) {
    const lat = position.coords.latitude;
    const lon = position.coords.longitude;
    const accuracy = position.coords.accuracy;
    lastKnownUserPosition = L.latLng(lat, lon); // Stocker la position

    console.log(`Position utilisateur MàJ: ${lat.toFixed(5)}, ${lon.toFixed(5)} (Précision: ${accuracy.toFixed(0)}m)`);

    if (!map || !userMarker) return; // Sécurité

    // Mettre à jour le marqueur utilisateur
    let userPopupContent = `<b>Ma Position</b><br>Lat: ${lat.toFixed(4)}<br>Lon: ${lon.toFixed(4)}`;
    if (accuracy) userPopupContent += `<br>Précision: ~${accuracy.toFixed(0)} m`;
    userMarker.setLatLng(lastKnownUserPosition).setOpacity(1).setPopupContent(userPopupContent); // Rendre visible

    // Mettre à jour le cercle de précision
    if (accuracy) {
       if (userAccuracyCircle) {
          userAccuracyCircle.setLatLng(lastKnownUserPosition).setRadius(accuracy);
       } else {
          userAccuracyCircle = L.circle(lastKnownUserPosition, { radius: accuracy, weight: 1, color: "blue", fillColor: "#cacaca", fillOpacity: 0.2 }).addTo(map);
       }
    } else if (userAccuracyCircle) { // Si pas d'info de précision, cacher le cercle
       userAccuracyCircle.remove();
       userAccuracyCircle = null;
    }

    // Mettre à jour la distance et la route vers le ballon
    updateDistanceAndRoute();

    // Mettre à jour l'état du bouton si on était en "Activation..."
    if ($ui.toggleTrackMeBtn.find('.spinner-border').length > 0) {
        $ui.toggleTrackMeBtn.prop("disabled", false)
           .removeClass("btn-warning").addClass("btn-success") // Passer de Jaune (activation) à Vert (actif)
           .html("Suivi Position: ON");
    }
  }

  /** Gère les erreurs de géolocalisation */
  function handleGeolocationError(error) {
    console.error("Erreur Géoloc (watchPosition):", error);
    let errorMsg = "Erreur suivi position.";
    let stopTracking = false;
    let disableButton = false;

    switch (error.code) {
      case error.PERMISSION_DENIED:
        errorMsg = "Permission localisation refusée.";
        stopTracking = true; // Arrêter le suivi
        disableButton = true; // Désactiver le bouton
        alert(errorMsg + " Activez la localisation dans les paramètres de votre navigateur.");
        break;
      case error.POSITION_UNAVAILABLE:
        errorMsg = "Position indisponible."; // Peut être temporaire
        setError(errorMsg, false);
        break;
      case error.TIMEOUT:
        errorMsg = "Timeout géoloc."; // Souvent temporaire
        setError(errorMsg, false);
        break;
      default:
         errorMsg = `Erreur inconnue (${error.message}).`;
         setError(errorMsg, false);
         break;
    }

    if (stopTracking) {
        stopWatchingUserPosition(disableButton); // Arrêter et potentiellement désactiver bouton
    } else {
         // Si l'erreur n'arrête pas le suivi, remettre bouton en état normal ON (s'il était en activation)
         if ($ui.toggleTrackMeBtn.find('.spinner-border').length > 0) {
             $ui.toggleTrackMeBtn.prop("disabled", false)
                .removeClass("btn-warning").addClass("btn-success") // Ou une autre couleur si on veut indiquer une erreur temporaire?
                .html("Suivi Position: ON");
         } else if (geolocationWatchId === null){ // Si l'erreur arrive AVANT même que watchID soit défini
             $ui.toggleTrackMeBtn.prop("disabled", false)
                .removeClass("btn-warning btn-success").addClass("btn-outline-secondary")
                .html("Suivre ma position");
         }
    }
  }

  /** Démarre la surveillance de la position de l'utilisateur */
  function startWatchingUserPosition() {
    if (!navigator.geolocation) {
      alert("Géolocalisation non supportée."); setError("Géolocalisation non supportée.", false); return;
    }
    if (geolocationWatchId !== null) { console.warn("watchPosition déjà actif."); return; }

    console.log("Démarrage suivi position utilisateur...");
    $ui.toggleTrackMeBtn.prop("disabled", true)
       .removeClass("btn-outline-secondary btn-success").addClass("btn-warning") // Bouton jaune "Activation..."
       .html('<span class="spinner-border spinner-border-sm"></span> Activation...');

    // Démarrer watchPosition
    geolocationWatchId = navigator.geolocation.watchPosition(
      updateUserMapPosition, // Callback succès
      handleGeolocationError,  // Callback erreur
      { // Options
        enableHighAccuracy: CONFIG.GEOLOCATION_HIGH_ACCURACY,
        timeout: CONFIG.GEOLOCATION_TIMEOUT,
        maximumAge: CONFIG.GEOLOCATION_MAX_AGE,
      }
    );

    // Vérifier si watchPosition a pu démarrer (rare, mais possible échec immédiat)
    if (geolocationWatchId === undefined || geolocationWatchId === null) {
        console.error("Impossible de démarrer watchPosition.");
        handleGeolocationError({ code: 0, message: "navigator.geolocation.watchPosition n'a pas retourné d'ID." });
        geolocationWatchId = null; // S'assurer que c'est null
    }
  }

  /** Arrête la surveillance de la position de l'utilisateur */
  function stopWatchingUserPosition(disableButton = false) {
    if (geolocationWatchId !== null) {
      console.log("Arrêt suivi position utilisateur.");
      navigator.geolocation.clearWatch(geolocationWatchId);
      geolocationWatchId = null;

      // Mettre à jour bouton: désactivé si erreur fatale, sinon normal état OFF
       $ui.toggleTrackMeBtn.prop("disabled", disableButton)
           .removeClass("btn-success btn-warning").addClass("btn-outline-secondary")
           .html(disableButton ? "Suivi désactivé" : "Suivre ma position");

      // Optionnel: Atténuer marqueur utilisateur et enlever cercle précision
      if (userMarker) userMarker.setOpacity(0.6);
      if (userAccuracyCircle) { userAccuracyCircle.remove(); userAccuracyCircle = null; }

    } else {
      console.log("Aucun suivi de position à arrêter.");
      // S'assurer que le bouton est bien dans l'état OFF même si on a cliqué Stop sans être actif
      $ui.toggleTrackMeBtn.prop("disabled", false)
          .removeClass("btn-success btn-warning").addClass("btn-outline-secondary")
          .html("Suivre ma position");
    }
  }

  // =========================================================================
  // Gestion des Actions Utilisateur (Boutons)
  // =========================================================================
  function setupButtonHandlers() {
    // Bouton Suivi Ballon (panoramique auto carte)
    $ui.toggleFollowBtn.on("click", function () {
      isFollowingBalloon = !isFollowingBalloon;
      $(this).text(isFollowingBalloon ? "Suivi Ballon: ON" : "Suivi Ballon: OFF")
             .toggleClass("btn-primary btn-outline-primary");
      if (isFollowingBalloon && lastKnownBalloonPosition && map) {
        map.panTo(lastKnownBalloonPosition); // Centrer immédiatement si activé
      }
    });

    // --- MODIFIÉ: Bouton Suivre Ma Position (Activer/Désactiver watchPosition) ---
    // !! Assurez-vous que l'ID du bouton dans le HTML est "toggle-track-me-btn" !!
    $ui.toggleTrackMeBtn.on("click", function () {
      if (geolocationWatchId === null) {
        // Si le suivi n'est PAS actif -> démarrer
        startWatchingUserPosition();
      } else {
        // Si le suivi EST actif -> arrêter
        stopWatchingUserPosition();
      }
    });
    // --- FIN MODIFICATION ---
  }

  // =========================================================================
  // Calcul Distance et Mise à Jour Routage
  // =========================================================================
  function updateDistanceAndRoute() {
    // Nécessite la position de l'utilisateur ET une position (même ancienne) du ballon
    if (lastKnownUserPosition && lastKnownBalloonPosition) {
      try {
        // Distance à vol d'oiseau
        const distanceMeters = lastKnownUserPosition.distanceTo(lastKnownBalloonPosition);
        $ui.distance.text(`${(distanceMeters / 1000).toFixed(2)} km`);

        // Mettre à jour les points de départ/arrivée pour le routage
        // Ne le fait que si les deux positions sont valides
        if (routingControl) {
          routingControl.setWaypoints([lastKnownUserPosition, lastKnownBalloonPosition]);
          // L'affichage/masquage du conteneur est géré par les events 'routesfound'/'routingerror'
        }
      } catch (e) {
        console.error("Error calculating distance/waypoints:", e);
        $ui.distance.text("Erreur"); $ui.routeInfo.text("Erreur");
        if (routingControl) $(".leaflet-routing-container").hide();
      }
    } else {
      // Si une des positions manque, afficher N/A et cacher la route
      $ui.distance.text("N/A"); $ui.routeInfo.text("N/A");
      if (routingControl) {
        routingControl.setWaypoints([null, null]); // Vider les waypoints
        $(".leaflet-routing-container").hide();
      }
    }
  }

  // Callback si une route est trouvée
  function handleRouteFound(e) {
    if (e.routes && e.routes.length > 0) {
      const summary = e.routes[0].summary;
      const distanceKm = (summary.totalDistance / 1000).toFixed(1);
      const timeMinutes = Math.round(summary.totalTime / 60);
      let timeString = timeMinutes < 60 ? `${timeMinutes} min` : `${Math.floor(timeMinutes / 60)}h ${timeMinutes % 60}min`;
      $ui.routeInfo.text(`Route: ${distanceKm} km, ${timeString}`);
      $(".leaflet-routing-container").show(); // Afficher la ligne de route
    } else {
      $ui.routeInfo.text("Route non trouvée");
      $(".leaflet-routing-container").hide(); // Cacher si pas de route
    }
  }

  // Callback si erreur de routage
  function handleRouteError(e) {
    console.error("Routing error:", e.error);
    $ui.routeInfo.text("Erreur routage");
    $(".leaflet-routing-container").hide(); // Cacher en cas d'erreur
  }

  // =========================================================================
  // Vérification Périodique de la Fraîcheur des Données du Ballon
  // =========================================================================
  function startDataFreshnessCheck() {
    setInterval(() => {
      if (lastValidDataTimestamp && (Date.now() - lastValidDataTimestamp > CONFIG.STALE_DATA_THRESHOLD_MS)) {
        console.warn(`Données périmées (> ${CONFIG.STALE_DATA_THRESHOLD_MS / 1000}s)`);
        // Rendre le marqueur ballon semi-transparent si les données sont vieilles
        if (balloonMarker && balloonMarker.options.opacity === 1) {
           // Ne change l'opacité que s'il était à 1 (données fraîches avant)
           // Si le signal était déjà perdu (opacité 0.6), on ne change rien
          // balloonMarker.setOpacity(0.6); // Déjà géré par la perte de signal dans updateUI
        }
        // Modifier le tooltip de l'indicateur de connexion websocket si besoin
        if ($ui.connectionStatus.hasClass("status-ok")) {
          $ui.connectionStatus.attr("title", "Websocket connecté, mais données non reçues récemment");
        }
      }
    }, 15000); // Vérifier toutes les 15 secondes
  }

  // =========================================================================
  // Point d'Entrée Principal - Initialisation de l'Application
  // =========================================================================
  function initializeApp() {
    console.log("Initializing application...");
    initMap(); // Initialiser la carte Leaflet
    initChart(); // Initialiser le graphique Chart.js
    setupSocketIO(); // Établir la connexion WebSocket
    setupButtonHandlers(); // Attacher les écouteurs d'événements aux boutons
    startDataFreshnessCheck(); // Lancer la vérification périodique des données
    updateDistanceAndRoute(); // Afficher N/A au début pour distance/route
    console.log("Application initialized and ready.");
  }

  // Lancer l'initialisation une fois que le DOM est prêt
  initializeApp();

}); // Fin de $(document).ready