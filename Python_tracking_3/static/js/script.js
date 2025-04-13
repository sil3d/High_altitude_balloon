$(document).ready(function () {
  // =========================================================================
  // Configuration
  // =========================================================================
  const CONFIG = {
    MAP_INITIAL_COORDS: [14.498, -17.071], // Somone, Sénégal (ou autre)
    MAP_INITIAL_ZOOM: 13,
    MAP_MAX_BALLOON_TRACK_POINTS: 500,
    CHART_MAX_POINTS: 100,
    STALE_DATA_THRESHOLD_MS: 60000,
    GEOLOCATION_TIMEOUT: 10000,
    USER_MARKER_ICON_URL: "https://img.icons8.com/color/48/000000/marker.png",
    BALLOON_MARKER_ICON_URL:
      "https://img.icons8.com/office/40/000000/hot-air-balloon.png",
    OSRM_SERVICE_URL: "https://router.project-osrm.org/route/v1",
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
  let lastKnownBalloonPosition = null;
  let lastKnownUserPosition = null;
  let lastValidDataTimestamp = null;

  // =========================================================================
  // Cache des éléments DOM
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
    // <<< AJOUT PMS >>>
    pm1_std: $("#pm1_std"),
    pm25_std: $("#pm25_std"),
    pm10_std: $("#pm10_std"),
    // <<< FIN AJOUT PMS >>>
    distance: $("#distance"),
    routeInfo: $("#route-info"),
    toggleFollowBtn: $("#toggle-follow-btn"),
    trackMeBtn: $("#track-me-btn"),
    altitudeChartCanvas: $("#altitudeChart"),
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
    const qualityMap = {
      1: "Excellent",
      2: "Bon",
      3: "Moyen",
      4: "Mauvais",
      5: "Très Mauvais",
    };
    return qualityMap[idx] || "Inconnu";
  }

  function setError(message, isPersistent = false) {
    if (message) {
      console.error("UI Error:", message);
      $ui.errorMessage.text(message);
      $ui.errorAlert.removeClass("d-none");
      if (isPersistent) {
        $ui.navbarError
          .text(message)
          .removeClass("d-none")
          .data("is-persistent", true);
      }
    } else {
      $ui.errorAlert.addClass("d-none");
      if (!$ui.navbarError.data("is-persistent")) {
        $ui.navbarError.addClass("d-none");
      }
      // $ui.navbarError.data('is-persistent', false); // Retire le flag si on efface
    }
  }

  function clearPersistentError() {
    console.log("Clearing persistent errors");
    $ui.navbarError.addClass("d-none").data("is-persistent", false);
    setError(null);
  }

  // =========================================================================
  // Initialisation Carte & Leaflet
  // (Fonction initMap inchangée - elle n'affiche pas les données PMS)
  // =========================================================================
  function initMap() {
    try {
      map = L.map("map", {
        center: CONFIG.MAP_INITIAL_COORDS,
        zoom: CONFIG.MAP_INITIAL_ZOOM,
      });

      const osmTile = L.tileLayer(
        "https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png",
        { attribution: "© OSM Contributors" }
      ).addTo(map);
      const satelliteTile = L.tileLayer(
        "https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}",
        { attribution: "Tiles © Esri" }
      );
      const topoTile = L.tileLayer(
        "https://{s}.tile.opentopomap.org/{z}/{x}/{y}.png",
        { attribution: "Map data: © OpenTopoMap contributors" }
      );
      const baseMaps = {
        OpenStreetMap: osmTile,
        Satellite: satelliteTile,
        Topographique: topoTile,
      };
      L.control.layers(baseMaps).addTo(map);

      const balloonIcon = L.icon({
        iconUrl: CONFIG.BALLOON_MARKER_ICON_URL,
        iconSize: [32, 32],
        iconAnchor: [16, 32],
        popupAnchor: [0, -32],
      });
      const userIcon = L.icon({
        iconUrl: CONFIG.USER_MARKER_ICON_URL,
        iconSize: [32, 32],
        iconAnchor: [16, 32],
        popupAnchor: [0, -32],
      });

      // Position initiale par défaut du ballon
      lastKnownBalloonPosition = L.latLng(
        CONFIG.MAP_INITIAL_COORDS[0],
        CONFIG.MAP_INITIAL_COORDS[1]
      );

      balloonMarker = L.marker(lastKnownBalloonPosition, {
        icon: balloonIcon,
        opacity: 0.6,
      })
        .addTo(map)
        .bindPopup("Ballon (Position initiale/par défaut)");
      userMarker = L.marker(CONFIG.MAP_INITIAL_COORDS, {
        icon: userIcon,
        opacity: 0,
      })
        .addTo(map)
        .bindPopup("Ma Position");
      balloonTrack = L.polyline([], { color: "red", weight: 3 }).addTo(map);

      routingControl = L.Routing.control({
        waypoints: [null, null],
        routeWhileDragging: false,
        show: false,
        addWaypoints: false,
        draggableWaypoints: false,
        lineOptions: { styles: [{ color: "blue", opacity: 0.6, weight: 4 }] },
        router: L.Routing.osrmv1({ serviceUrl: CONFIG.OSRM_SERVICE_URL }),
        createMarker: () => null,
      }).addTo(map);

      $(".leaflet-routing-container").hide();
      routingControl.on("routesfound", handleRouteFound);
      routingControl.on("routingerror", handleRouteError);

      console.log("Map initialized successfully.");
      updateDistanceAndRoute();
    } catch (error) {
      console.error("Map Initialization failed:", error);
      setError("Impossible d'initialiser la carte.", true);
    }
  }

  // =========================================================================
  // Initialisation Graphique (Chart.js)
  // (Fonction initChart inchangée)
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
          datasets: [
            {
              label: "Altitude Barométrique (m)",
              data: [], // Altitude values
              borderColor: "rgb(75, 192, 192)",
              backgroundColor: "rgba(75, 192, 192, 0.2)",
              tension: 0.1,
              pointRadius: 1,
              fill: true,
            },
          ],
        },
        options: {
          scales: {
            x: {
              type: "time",
              time: {
                unit: "minute",
                tooltipFormat: "HH:mm:ss",
                displayFormats: { minute: "HH:mm" },
              },
              title: { display: true, text: "Temps" },
            },
            y: {
              title: { display: true, text: "Altitude (m)" },
              beginAtZero: false, // L'altitude peut être négative ou commencer haut
            },
          },
          animation: { duration: 0 }, // Désactiver l'animation pour fluidité temps réel
          maintainAspectRatio: false, // Important pour canvas dans un div redimensionnable
          plugins: { legend: { display: false } }, // Cacher la légende par défaut
        },
      });
      console.log("Chart initialized successfully.");
    } catch (error) {
      console.error("Chart Initialization failed:", error);
      setError("Impossible d'initialiser le graphique.", true);
    }
  }

  // =========================================================================
  // Mise à jour de l'Interface Utilisateur (UI)
  // <<< MODIFIÉ pour inclure PMS >>>
  // =========================================================================
  function updateUI(data) {
    if (!data) {
      console.warn("updateUI called with null data.");
      return;
    }

    // --- Gérer Erreur Serveur ---
    if (data.error) {
      setError(`Erreur serveur: ${data.error}`, true);
      return; // Ne pas traiter le reste si erreur serveur
    } else {
      setError(null, false); // Efface erreur NON persistante
    }

    // --- Mise à jour Timestamp et Fraîcheur ---
    if (data.timestamp) {
      try {
        const date = new Date(data.timestamp * 1000);
        $ui.timestamp.text(
          date.toLocaleString("fr-FR", {
            dateStyle: "short",
            timeStyle: "medium",
          })
        );
        lastValidDataTimestamp = Date.now();
      } catch (e) {
        $ui.timestamp.text("Invalide");
      }
    } else {
      $ui.timestamp.text("N/A");
    }

    // --- Mise à jour Données Textuelles ---
    // GPS & Mouvement
    $ui.latitude.text(formatFloat(data.latitude, 5));
    $ui.longitude.text(formatFloat(data.longitude, 5));
    $ui.altitudeGps.text(formatFloat(data.altitude_gps, 1));
    $ui.satellites.text(formatInt(data.satellites));
    $ui.speedKmh.text(formatFloat(data.speed_kmh, 1));
    $ui.rssi.text(formatInt(data.rssi));

    // Environnement
    $ui.temperature.text(formatFloat(data.temperature, 1));
    const pressureHpa =
      data.pressure !== null && typeof data.pressure !== "undefined"
        ? (parseFloat(data.pressure) / 100.0).toFixed(1)
        : "N/A";
    $ui.pressure.text(pressureHpa);
    $ui.humidity.text(formatFloat(data.humidity, 1));
    $ui.altitudeBme.text(formatFloat(data.altitude_bme, 1));

    // Qualité Air & Autres
    const aq = formatInt(data.air_quality);
    $ui.airQuality.text(aq);
    $ui.airQualityText.text(getAirQualityText(aq));
    $ui.tvoc.text(formatInt(data.tvoc));
    $ui.eco2.text(formatInt(data.eco2));
    $ui.ozone.text(formatInt(data.ozone));
    $ui.uvIndex.text(formatFloat(data.uv_index, 1));

    // --- PMS (Standard) --- <<< AJOUTÉ
    $ui.pm1_std.text(formatInt(data.pm1_std));
    $ui.pm25_std.text(formatInt(data.pm25_std));
    $ui.pm10_std.text(formatInt(data.pm10_std));
    // --- FIN PMS ---

    // --- Mise à jour Carte ---
    const hasValidGps =
      typeof data.latitude === "number" &&
      typeof data.longitude === "number" &&
      !isNaN(data.latitude) &&
      !isNaN(data.longitude);
    if (map && balloonMarker) {
      if (hasValidGps) {
        const newLatLng = L.latLng(data.latitude, data.longitude);
        lastKnownBalloonPosition = newLatLng;

        balloonMarker.setLatLng(newLatLng).setOpacity(1);
        let popupContent = `<b>Ballon</b><br>Lat: ${data.latitude.toFixed(
          4
        )}<br>Lon: ${data.longitude.toFixed(4)}`;
        if (typeof data.altitude_gps === "number")
          popupContent += `<br>Alt GPS: ${data.altitude_gps.toFixed(0)}m`;
        if (typeof data.speed_kmh === "number")
          popupContent += `<br>Vit: ${data.speed_kmh.toFixed(1)}km/h`;
        balloonMarker.setPopupContent(popupContent);

        if (balloonTrack) {
          balloonTrack.addLatLng(newLatLng);
          let latlngs = balloonTrack.getLatLngs();
          if (latlngs.length > CONFIG.MAP_MAX_BALLOON_TRACK_POINTS) {
            latlngs.splice(
              0,
              latlngs.length - CONFIG.MAP_MAX_BALLOON_TRACK_POINTS
            );
            balloonTrack.setLatLngs(latlngs);
          }
        }
        if (isFollowingBalloon) map.panTo(newLatLng);
        updateDistanceAndRoute();
      } else {
        balloonMarker.setOpacity(0.6);
        if (lastKnownBalloonPosition) {
          balloonMarker.setLatLng(lastKnownBalloonPosition);
          balloonMarker.setPopupContent(
            balloonMarker.getPopup().getContent().split("<br>")[0] +
              "<br>(Signal GPS perdu)"
          );
        } else {
          balloonMarker.setPopupContent("Ballon (En attente de signal GPS)");
        }
      }
    } else {
      console.error("Map/Marker non initialisé pour updateUI");
    }

    // --- Mise à jour Graphique ---
    const hasValidAltitude =
      typeof data.altitude_bme === "number" && !isNaN(data.altitude_bme);
    const hasValidTimestampForChart = data.timestamp && data.timestamp > 0;

    if (altitudeChart) {
      if (hasValidAltitude && hasValidTimestampForChart) {
        try {
          const timestampMs = data.timestamp * 1000;
          const altitude = data.altitude_bme;
          altitudeChart.data.labels.push(timestampMs);
          altitudeChart.data.datasets[0].data.push(altitude);
          if (altitudeChart.data.labels.length > CONFIG.CHART_MAX_POINTS) {
            altitudeChart.data.labels.shift();
            altitudeChart.data.datasets[0].data.shift();
          }
          altitudeChart.update("none");
        } catch (e) {
          console.error("Failed to update chart:", e);
        }
      }
    } else {
      console.error("Chart non initialisé pour updateUI");
    }
  }

  // =========================================================================
  // Gestion des Événements SocketIO
  // (Fonction setupSocketIO inchangée - elle transmet les données à updateUI)
  // =========================================================================
  function setupSocketIO() {
    if (socket) socket.disconnect();
    socket = io();

    socket.on("connect", () => {
      console.log("SocketIO connected.");
      $ui.connectionStatus
        .removeClass("status-disconnected status-error")
        .addClass("status-ok")
        .attr("title", "Websocket Connecté");
      clearPersistentError();
    });

    socket.on("disconnect", (reason) => {
      console.warn("SocketIO disconnected:", reason);
      $ui.connectionStatus
        .removeClass(
          "status-ok status-receiving status-connecting status-error"
        )
        .addClass("status-disconnected")
        .attr("title", `Websocket Déconnecté: ${reason}`);
      setError("Connexion au serveur perdue.", true);
    });

    socket.on("connect_error", (err) => {
      console.error("SocketIO connection error:", err);
      $ui.connectionStatus
        .removeClass("status-ok status-receiving status-connecting")
        .addClass("status-error")
        .attr("title", `Erreur Websocket: ${err.message}`);
      setError(`Erreur de connexion Websocket: ${err.message}`, true);
    });

    socket.on("update_data", (data) => {
      $ui.connectionStatus.addClass("status-receiving");
      setTimeout(
        () => $ui.connectionStatus.removeClass("status-receiving"),
        500
      );
      updateUI(data); // Appelle la fonction qui gère maintenant aussi PMS
    });

    socket.on("initial_history", (history) => {
      console.log(`Initial history received (${history?.length || 0} points)`);
      if (!history || history.length === 0) return;

      const chartLabels = [];
      const chartAltitudes = [];
      const trackPoints = [];
      let lastValidHistoryPoint = null;

      history.forEach((point) => {
        if (
          point &&
          typeof point.altitude_bme === "number" &&
          point.timestamp > 0
        ) {
          chartLabels.push(point.timestamp * 1000);
          chartAltitudes.push(point.altitude_bme);
        }
        if (
          point &&
          typeof point.latitude === "number" &&
          typeof point.longitude === "number"
        ) {
          trackPoints.push([point.latitude, point.longitude]);
          lastValidHistoryPoint = point;
        }
      });

      if (altitudeChart && chartLabels.length > 0) {
        altitudeChart.data.labels = chartLabels.slice(-CONFIG.CHART_MAX_POINTS);
        altitudeChart.data.datasets[0].data = chartAltitudes.slice(
          -CONFIG.CHART_MAX_POINTS
        );
        altitudeChart.update("none");
      }
      if (balloonTrack && trackPoints.length > 0) {
        const limitedTrack = trackPoints.slice(
          -CONFIG.MAP_MAX_BALLOON_TRACK_POINTS
        );
        balloonTrack.setLatLngs(limitedTrack);
      }
      if (lastValidHistoryPoint) {
        updateUI(lastValidHistoryPoint); // Met à jour l'UI avec le dernier point
        if (map && lastKnownBalloonPosition) {
          map.setView(lastKnownBalloonPosition, CONFIG.MAP_INITIAL_ZOOM);
        }
      }
    });

    socket.on("serial_status", (statusInfo) => {
      console.log("Serial Status:", statusInfo);
      const indicator = $ui.serialStatusIndicator;
      const text = $ui.serialStatusText;
      indicator.removeClass(
        "status-ok status-error status-receiving status-connecting status-disconnected"
      );
      $ui.navbarError.data("is-persistent", false); // Réinitialiser avant de re-définir

      switch (statusInfo.status) {
        case "connected":
          indicator
            .addClass("status-ok")
            .attr("title", `Connecté sur ${statusInfo.port}`);
          text.text("Connecté");
          clearPersistentError();
          break;
        case "receiving":
          indicator
            .addClass("status-receiving")
            .attr("title", `Données reçues (${statusInfo.port})`);
          text.text("Réception...");
          clearPersistentError();
          break;
        case "error":
          indicator
            .addClass("status-error")
            .attr("title", `Erreur: ${statusInfo.message}`);
          text.text("Erreur");
          setError(statusInfo.message || "Erreur série inconnue", true);
          break;
        case "disconnected":
        default:
          indicator.addClass("status-disconnected").attr("title", "Déconnecté");
          text.text("Déconnecté");
          setError("Port série déconnecté", true);
          break;
      }
    });
  }

  // =========================================================================
  // Gestion des Actions Utilisateur (Boutons)
  // (Fonction setupButtonHandlers inchangée)
  // =========================================================================
  function setupButtonHandlers() {
    $ui.toggleFollowBtn.on("click", function () {
      isFollowingBalloon = !isFollowingBalloon;
      $(this)
        .text(isFollowingBalloon ? "Suivi Ballon: ON" : "Suivi Ballon: OFF")
        .toggleClass("btn-primary btn-outline-primary");
      if (isFollowingBalloon && lastKnownBalloonPosition && map) {
        map.panTo(lastKnownBalloonPosition);
      }
    });

    $ui.trackMeBtn.on("click", function () {
      if (!navigator.geolocation) {
        alert("La géolocalisation n'est pas supportée.");
        setError("Géolocalisation non supportée.", false);
        return;
      }
      const $button = $(this);
      $button
        .prop("disabled", true)
        .html(
          '<span class="spinner-border spinner-border-sm"></span> Localisation...'
        );

      navigator.geolocation.getCurrentPosition(
        (position) => {
          const lat = position.coords.latitude;
          const lon = position.coords.longitude;
          const accuracy = position.coords.accuracy;
          lastKnownUserPosition = L.latLng(lat, lon);

          if (userMarker && map) {
            let userPopupContent = `<b>Ma Position</b><br>Lat: ${lat.toFixed(
              4
            )}<br>Lon: ${lon.toFixed(4)}`;
            if (accuracy)
              userPopupContent += `<br>Précision: ~${accuracy.toFixed(0)} m`;
            userMarker
              .setLatLng(lastKnownUserPosition)
              .setOpacity(1)
              .setPopupContent(userPopupContent)
              .openPopup();

            if (accuracy) {
              if (userAccuracyCircle) {
                userAccuracyCircle
                  .setLatLng(lastKnownUserPosition)
                  .setRadius(accuracy);
              } else {
                userAccuracyCircle = L.circle(lastKnownUserPosition, {
                  radius: accuracy,
                  weight: 1,
                  color: "blue",
                  fillColor: "#cacaca",
                  fillOpacity: 0.2,
                }).addTo(map);
              }
              // userAccuracyCircle.setStyle({ opacity: accuracy > 500 ? 0 : 1, fillOpacity: accuracy > 500 ? 0 : 0.2 });
            } else if (userAccuracyCircle) {
              userAccuracyCircle.setStyle({ opacity: 0, fillOpacity: 0 });
            }
            map.setView(lastKnownUserPosition, map.getZoom());
            updateDistanceAndRoute();
          }
          $button.prop("disabled", false).html("📍 Localise Moi");
        },
        (error) => {
          console.error("Geolocation error:", error);
          let errorMsg = "Erreur de géolocalisation.";
          switch (error.code) {
            case error.PERMISSION_DENIED:
              errorMsg = "Permission refusée.";
              break;
            case error.POSITION_UNAVAILABLE:
              errorMsg = "Position indisponible.";
              break;
            case error.TIMEOUT:
              errorMsg = "Timeout géoloc.";
              break;
          }
          alert(errorMsg);
          setError(errorMsg, false);
          $button.prop("disabled", false).html("📍 Localise Moi");
        },
        {
          enableHighAccuracy: true,
          timeout: CONFIG.GEOLOCATION_TIMEOUT,
          maximumAge: 0,
        }
      );
    });
  }

  // =========================================================================
  // Calcul Distance et Mise à Jour Routage
  // (Fonctions updateDistanceAndRoute, handleRouteFound, handleRouteError inchangées)
  // =========================================================================
  function updateDistanceAndRoute() {
    if (lastKnownUserPosition && lastKnownBalloonPosition) {
      try {
        const distanceMeters = lastKnownUserPosition.distanceTo(
          lastKnownBalloonPosition
        );
        $ui.distance.text(`${(distanceMeters / 1000).toFixed(2)} km`);
        if (routingControl) {
          routingControl.setWaypoints([
            lastKnownUserPosition,
            lastKnownBalloonPosition,
          ]);
        }
      } catch (e) {
        console.error("Error calculating distance/waypoints:", e);
        $ui.distance.text("Erreur");
        $ui.routeInfo.text("Erreur");
        if (routingControl) $(".leaflet-routing-container").hide();
      }
    } else {
      $ui.distance.text("N/A");
      $ui.routeInfo.text("N/A");
      if (routingControl) {
        routingControl.setWaypoints([null, null]);
        $(".leaflet-routing-container").hide();
      }
    }
  }
  function handleRouteFound(e) {
    if (e.routes && e.routes.length > 0) {
      const summary = e.routes[0].summary;
      const distanceKm = (summary.totalDistance / 1000).toFixed(1);
      const timeMinutes = Math.round(summary.totalTime / 60);
      let timeString =
        timeMinutes < 60
          ? `${timeMinutes} min`
          : `${Math.floor(timeMinutes / 60)}h ${timeMinutes % 60}min`;
      $ui.routeInfo.text(`Route: ${distanceKm} km, ${timeString}`);
      $(".leaflet-routing-container").show();
    } else {
      $ui.routeInfo.text("Route non trouvée");
      $(".leaflet-routing-container").hide();
    }
  }
  function handleRouteError(e) {
    console.error("Routing error:", e.error);
    $ui.routeInfo.text("Erreur routage");
    $(".leaflet-routing-container").hide();
  }

  // =========================================================================
  // Vérification Périodique de la Fraîcheur des Données
  // (Fonction startDataFreshnessCheck inchangée)
  // =========================================================================
  function startDataFreshnessCheck() {
    setInterval(() => {
      if (
        lastValidDataTimestamp &&
        Date.now() - lastValidDataTimestamp > CONFIG.STALE_DATA_THRESHOLD_MS
      ) {
        console.warn(
          `Data is stale (> ${CONFIG.STALE_DATA_THRESHOLD_MS / 1000}s)`
        );
        if (balloonMarker && balloonMarker.options.opacity === 1) {
          balloonMarker.setOpacity(0.5);
        }
        if ($ui.connectionStatus.hasClass("status-ok")) {
          $ui.connectionStatus.attr(
            "title",
            "Websocket connecté, mais données non reçues récemment"
          );
        }
      }
    }, 15000);
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
