#include <DHT.h>
#include <WiFi.h>
#include <SPI.h>
#include <SD.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <Adafruit_AHTX0.h>
#include <ScioSense_ENS160.h>
#include <WebServer.h>
#include <ArduinoJson.h>

#include "config.h"

WiFiUDP ntpUDP;                
NTPClient timeClient(ntpUDP, ntp_server, ntp_offset, ntp_update_interval);  
WebServer server(80);

DHT dht(DHTPIN, DHTTYPE);  
Adafruit_AHTX0 aht;                 
ScioSense_ENS160 ens160(ENS160_I2CADDR_1);

// Variables système
bool ledState = LOW;  
bool wifiConnected = false;
bool sdCardInitialized = false;
bool ahtInitialized = false;
bool ens160Initialized = false;
bool webServerStarted = false;
bool sdHealthy = true;

// Variables WiFi robuste
unsigned long lastWifiCheck = 0;
unsigned long lastWifiReconnect = 0;
int wifiReconnectAttempts = 0;
unsigned long lastSuccessfulConnection = 0;
int consecutiveFailures = 0;

unsigned long lastNTPSync = 0;
bool timeInitialized = false;
String lastKnownTime = "1970-01-01 00:00:00";

// Variables I2C
unsigned long lastI2CError = 0;
int i2cErrorCount = 0;
unsigned long lastI2CReset = 0;
unsigned long lastI2CScan = 0;
bool i2cDevicesDetected[128] = {false};

// Variables cache serveur
unsigned long lastDataUpdate = 0;

struct CachedResponse {
  String data;
  unsigned long timestamp;
  bool valid;
};

CachedResponse apiCurrentCache;
CachedResponse apiStatsCache;

// Variables SD
unsigned long sdLastCheck = 0;
uint64_t sdTotalSpace = 0;
uint64_t sdUsedSpace = 0;

// Variables statistiques
int validReadingsCount = 0;
int totalReadingsCount = 0;

struct SensorData {
  String date;         
  String time;         
  float temperatureDHT;
  float humidityDHT;
  float temperatureAHT;
  float humidityAHT;
  uint16_t eco2;
  uint16_t tvoc;
  uint8_t aqi;
  bool validDHT = false;
  bool validAHT = false;
  bool validENS160 = false;
};

SensorData cachedData;
bool cacheValid = false;

// Fonctions serveur web
void handleRootModern();
void handleCSS();
void handleDailyStats();
void handleHistoryData();
void handleCurrentDataCached();
void handleStatsCached();
void handleHealthCheck();
void handleFileListOptimized();
void handleFavicon();
void handleNotFoundOptimized();
void handleRootOptimized();

// Fonctions utilitaires serveur
String buildOptimizedJSON(const SensorData& data);

// Fonctions WiFi
void init_wifi_enhanced();
void connect_wifi_enhanced();
void manage_wifi_connection_enhanced();
void init_time_sync();

// Fonctions serveur web
void init_webserver_enhanced();

// Fonctions capteurs
void read_all_sensors_robust(SensorData &datas);
void monitor_i2c_health_enhanced();
void scan_i2c_enhanced();
void init_sensors_robust();

// Fonctions SD
void init_sd_card_robust();
bool test_sd_write();
void save_on_sd_card_robust(const SensorData& datas);
void monitor_sd_space();
void clean_old_files_robust();
void force_cleanup_sd();
void perform_cleanup(int daysToKeep);
String extractDateFromFilename(const String& filename);
bool isValidDateFormat(const String& dateStr);
time_t parseFileDate(const String& dateStr);

// Fonctions temps
String catch_time_robust();

// Fonctions I2C et capteurs de base
void init_i2c();
void init_dht();
float catch_temp();
float catch_hum();

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("=== DEMARRAGE ESP32 STATION METEO V3 ===");
  
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
  init_i2c();
  init_wifi_enhanced();
  init_dht();
  init_sd_card_robust();
  init_sensors_robust();
  
  Serial.println("=== SETUP TERMINE ===");
}

// Dans la fonction loop(), remplacer :

// Remplacer la fonction loop() par :

void loop() {
  unsigned long startTime = millis();
  
  // Gestion WiFi haute fréquence
  manage_wifi_connection_enhanced();
  
  // Gérer requêtes web avec monitoring
  if (wifiConnected && webServerStarted) {
    unsigned long webStart = millis();
    server.handleClient();
    
    unsigned long webTime = millis() - webStart;
    if (webTime > DEBUG_WEB_REQUEST_WARNING_MS) {
      Serial.printf("Requete web lente: %lu ms\n", webTime);
    }
  }

  // Lecture capteurs - OPTIMISATION : toutes les 2 minutes (120000 ms)
  static unsigned long lastSensorRead = 0;
  unsigned long now = millis();
  
  if (now - lastSensorRead >= 120000) { // 2 minutes
    lastSensorRead = now;
    
    // ALLUMER LA LED PENDANT LA RECOLTE
    digitalWrite(LED_PIN, HIGH);
    Serial.println("=== DEBUT LECTURE CAPTEURS ===");
    
    SensorData datas;
    String fullTime = catch_time_robust();
    datas.date = fullTime.substring(0, 10);
    datas.time = fullTime.substring(11);

    read_all_sensors_robust(datas);
    
    // Mettre à jour cache
    cachedData = datas;
    lastDataUpdate = millis();
    cacheValid = true;
    
    // Sauvegarde SD uniquement si données valides
    if (sdCardInitialized && sdHealthy) {
      save_on_sd_card_robust(datas);
      clean_old_files_robust();
    }
    
    // ETEINDRE LA LED APRES LA RECOLTE
    digitalWrite(LED_PIN, LOW);
    Serial.printf("=== FIN LECTURE CAPTEURS (duree: %lu ms) ===\n", millis() - now);
  }
  
  // Monitoring I2C et SD - encore moins fréquent (toutes les 10 minutes)
  static unsigned long lastMonitoring = 0;
  if (now - lastMonitoring >= 600000) { // 10 minutes
    lastMonitoring = now;
    monitor_i2c_health_enhanced();
    monitor_sd_space();
  }
  
  // Performance monitoring
  unsigned long loopTime = millis() - startTime;
  if (loopTime > DEBUG_LOOP_TIME_WARNING_MS) {
    Serial.printf("Loop lente: %lu ms\n", loopTime);
  }
  
  // Monitoring mémoire périodique (toutes les 5 minutes)
  static unsigned long lastMemoryCheck = 0;
  if (now - lastMemoryCheck >= 300000) { // 5 minutes
    lastMemoryCheck = now;
    Serial.printf("Memoire libre: %u octets\n", ESP.getFreeHeap());
    
    // Afficher infos temps
    if (timeInitialized) {
      Serial.printf("Derniere sync NTP: %lu ms ago\n", millis() - lastNTPSync);
    }
    
    // Afficher statistiques capteurs
    if (totalReadingsCount > 0) {
      float successRate = (float)validReadingsCount / totalReadingsCount * 100;
      Serial.printf("Statistiques: %d lectures, %.1f%% succes\n", 
                   totalReadingsCount, successRate);
    }
  }
  
  delay(1000 * TIME_SCALE); // Boucle toutes les secondes
}

// ===== WIFI ULTRA-ROBUSTE =====

void init_wifi_enhanced() {
  Serial.println("Initialisation WiFi ultra-robuste...");
  
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);
  
  connect_wifi_enhanced();
}

void connect_wifi_enhanced() {
  Serial.printf("Connexion WiFi a %s...\n", ssid);
  
  WiFi.disconnect(true);
  delay(1000);
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  unsigned long startTime = millis();
  
  while (WiFi.status() != WL_CONNECTED && attempts < MAX_WIFI_RETRIES) {
    delay(250);
    Serial.print(".");
    attempts++;
    
    if (millis() - startTime > 30000) {
      Serial.println("\nTimeout connexion WiFi");
      break;
    }
    
    if (attempts % 10 == 0) {
      wl_status_t status = WiFi.status();
      Serial.printf("\nTentative %d/%d, Status: %d", attempts, MAX_WIFI_RETRIES, status);
      
      if (status == WL_CONNECT_FAILED || status == WL_CONNECTION_LOST) {
        Serial.println(" - Redemarrage WiFi...");
        WiFi.disconnect();
        delay(2000);
        WiFi.begin(ssid, password);
      }
    }
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    wifiReconnectAttempts = 0;
    consecutiveFailures = 0;
    lastSuccessfulConnection = millis();
    
    Serial.println("\nWiFi connecte avec succes !");
    Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("Signal: %d dBm\n", WiFi.RSSI());
    Serial.printf("Canal: %d\n", WiFi.channel());
    
    init_time_sync();
    
    if (!webServerStarted) {
      init_webserver_enhanced();
    }
  } else {
    wifiConnected = false;
    consecutiveFailures++;
    Serial.printf("\nEchec connexion WiFi (tentative %d) - mode offline\n", consecutiveFailures);
  }
}

void manage_wifi_connection_enhanced() {
  unsigned long now = millis();
  
  if (now - lastWifiCheck > WIFI_CHECK_INTERVAL_MS) {
    lastWifiCheck = now;
    
    wl_status_t status = WiFi.status();
    int rssi = WiFi.RSSI();
    
    if (status == WL_CONNECTED) {
      if (!wifiConnected) {
        Serial.println("WiFi reconnecte automatiquement !");
        wifiConnected = true;
        wifiReconnectAttempts = 0;
        consecutiveFailures = 0;
        lastSuccessfulConnection = now;
        
        if (!webServerStarted) {
          init_webserver_enhanced();
        }
      }
      
      if (rssi < -85) {
        Serial.printf("Signal tres faible: %d dBm\n", rssi);
      }
      
      // Force reconnexion si instable
      if (consecutiveFailures > 3 && (now - lastSuccessfulConnection > WIFI_TIMEOUT_MS * 10)) {
        Serial.println("Force reconnexion (instabilite detectee)");
        WiFi.disconnect();
        delay(1000);
        connect_wifi_enhanced();
      }
      
    } else {
      if (wifiConnected) {
        Serial.printf("Connexion WiFi perdue (Status: %d, RSSI: %d)\n", status, rssi);
        wifiConnected = false;
        webServerStarted = false;
        consecutiveFailures++;
      }
      
      if ((now - lastWifiReconnect > WIFI_RECONNECT_DELAY_MS && 
           wifiReconnectAttempts < WIFI_MAX_RECONNECT_ATTEMPTS)) {
        
        Serial.printf("Reconnexion WiFi (%d/%d)\n", 
                     wifiReconnectAttempts + 1, WIFI_MAX_RECONNECT_ATTEMPTS);
        
        lastWifiReconnect = now;
        wifiReconnectAttempts++;
        
        connect_wifi_enhanced();
      }
    }
  }
  
  // Reset compteurs après succès prolongé
  if (wifiConnected && (now - lastSuccessfulConnection > 600000)) {
    wifiReconnectAttempts = 0;
    consecutiveFailures = 0;
  }
}

void init_time_sync() {
  Serial.println("Synchronisation temps NTP initiale...");
  timeClient.begin();
  timeClient.setUpdateInterval(300000); // 5 minutes
  
  // Une seule tentative rapide au démarrage
  if (timeClient.forceUpdate()) {
    timeInitialized = true;
    lastNTPSync = millis();
    Serial.println("Synchronisation NTP initiale reussie");
  } else {
    Serial.println("Echec NTP initial - continuera en arriere-plan");
    timeInitialized = false;
  }
}

// ===== SERVEUR WEB HAUTE PERFORMANCE =====

void init_webserver_enhanced() {
  Serial.println("Initialisation serveur web haute performance...");
  
  // Configuration serveur optimisée
  const char* headers[] = {"User-Agent", "Accept-Encoding", "Connection"};
  server.collectHeaders(headers, 3);
  server.enableCORS(true);
  
  server.on("/", HTTP_GET, handleRootModern);
  server.on("/favicon.ico", HTTP_GET, handleFavicon);
  server.on("/api/current", HTTP_GET, handleCurrentDataCached);
  server.on("/api/stats", HTTP_GET, handleStatsCached);
  server.on("/api/files", HTTP_GET, handleFileListOptimized);
  server.on("/api/health", HTTP_GET, handleHealthCheck);
  server.on("/api/daily", HTTP_GET, handleDailyStats);
  server.on("/api/history", HTTP_GET, handleHistoryData);
  server.on("/style.css", HTTP_GET, handleCSS);
  server.onNotFound(handleNotFoundOptimized);
  
  server.begin();
  webServerStarted = true;
  Serial.println("Serveur web haute performance demarre !");
  Serial.printf("URL: http://%s\n", WiFi.localIP().toString().c_str());
}

void handleHistoryData() {
  if (!sdCardInitialized) {
    server.send(503, "application/json", "{\"error\":\"SD card not available\"}");
    return;
  }
  
  String timeRange = server.arg("range");
  if (timeRange == "") timeRange = "24h";
  
  StaticJsonDocument<4096> doc; // Augmenté pour plus de données
  JsonArray dataArray = doc.createNestedArray("data");
  
  String today = catch_time_robust().substring(0, 10);
  String filename = "/" + today + ".csv";
  
  if (SD.exists(filename)) {
    File dataFile = SD.open(filename, FILE_READ);
    if (dataFile) {
      String line;
      bool firstLine = true;
      int pointsCount = 0;
      int maxPoints = 100; // Augmenté pour avoir plus de points
      
      // Lire ligne par ligne
      while (dataFile.available() && pointsCount < maxPoints) {
        line = dataFile.readStringUntil('\n');
        line.trim();
        
        if (firstLine || line.startsWith("Date") || line.length() < 10) {
          firstLine = false;
          continue;
        }
        
        // Parser la ligne CSV
        int commaIndex = 0;
        String values[15]; // Augmenté pour tous les champs
        int valueIndex = 0;
        
        for (int i = 0; i < line.length() && valueIndex < 15; i++) {
          if (line.charAt(i) == ',' || i == line.length() - 1) {
            if (i == line.length() - 1 && line.charAt(i) != ',') {
              values[valueIndex] = line.substring(commaIndex, i + 1);
            } else {
              values[valueIndex] = line.substring(commaIndex, i);
            }
            commaIndex = i + 1;
            valueIndex++;
          }
        }
        
        if (valueIndex >= 9) {
          JsonObject point = dataArray.createNestedObject();
          
          // Heure
          String timeStr = values[1];
          point["time"] = timeStr;
          
          // Température (priorité DHT22, sinon AHT21)
          float tempDHT = values[2].toFloat();
          float tempAHT = values[4].toFloat();
          if (tempDHT > -50 && tempDHT < 100 && tempDHT != -999) {
            point["temperature"] = round(tempDHT * 10) / 10.0;
          } else if (tempAHT > -50 && tempAHT < 100 && tempAHT != -999) {
            point["temperature"] = round(tempAHT * 10) / 10.0;
          }
          
          // Humidité (priorité DHT22, sinon AHT21)
          float humDHT = values[3].toFloat();
          float humAHT = values[5].toFloat();
          if (humDHT > 0 && humDHT <= 100 && humDHT != -999) {
            point["humidity"] = round(humDHT * 10) / 10.0;
          } else if (humAHT > 0 && humAHT <= 100 && humAHT != -999) {
            point["humidity"] = round(humAHT * 10) / 10.0;
          }
          
          // Qualité de l'air - ECO2
          if (valueIndex > 6) {
            uint16_t eco2 = values[6].toInt();
            if (eco2 != 0 && eco2 != 65535 && eco2 >= 400 && eco2 <= 5000) {
              point["eco2"] = eco2;
            }
          }
          
          // Qualité de l'air - TVOC
          if (valueIndex > 7) {
            uint16_t tvoc = values[7].toInt();
            if (tvoc != 0 && tvoc != 65535 && tvoc <= 1000) {
              point["tvoc"] = tvoc;
            }
          }
          
          // Qualité de l'air - AQI
          if (valueIndex > 8) {
            uint8_t aqi = values[8].toInt();
            if (aqi != 0 && aqi != 255 && aqi >= 1 && aqi <= 5) {
              point["aqi"] = aqi;
            }
          }
          
          pointsCount++;
        }
      }
      
      dataFile.close();
      doc["points_count"] = pointsCount;
      doc["date"] = today;
      doc["filename"] = filename;
      
    } else {
      doc["error"] = "Cannot read file";
      doc["points_count"] = 0;
    }
  } else {
    doc["points_count"] = 0;
    doc["note"] = "No data file for today";
    doc["date"] = today;
    doc["filename"] = filename;
  }
  
  String response;
  serializeJson(doc, response);
  
  server.sendHeader("Cache-Control", "public, max-age=30"); // Cache 30 secondes
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", response);
}

// Dans handleRootModern(), remplacer la partie JavaScript par :

void handleRootModern() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="fr">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Station Météo ESP32</title>
    <link rel="stylesheet" href="/style.css">
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
</head>
<body>
    <header>
        <h1>🌡️ Station Météorologique ESP32</h1>
        <div class="status-bar">
            <span id="status-indicator">🔴 Déconnecté</span>
            <span id="last-update">Jamais</span>
        </div>
    </header>

    <main>
        <!-- Cartes de données actuelles -->
        <section class="current-data">
            <div class="card temperature">
                <div class="card-header">
                    <h3>🌡️ Température</h3>
                    <span class="sensor-status" id="temp-status">--</span>
                </div>
                <div class="value-container">
                    <div class="main-value" id="current-temp">--°C</div>
                    <div class="sub-values">
                        <span class="min-max">Min: <span id="temp-min">--</span>°C</span>
                        <span class="min-max">Max: <span id="temp-max">--</span>°C</span>
                    </div>
                </div>
            </div>

            <div class="card humidity">
                <div class="card-header">
                    <h3>💧 Humidité</h3>
                    <span class="sensor-status" id="hum-status">--</span>
                </div>
                <div class="value-container">
                    <div class="main-value" id="current-hum">--%</div>
                    <div class="sub-values">
                        <span class="min-max">Min: <span id="hum-min">--</span>%</span>
                        <span class="min-max">Max: <span id="hum-max">--</span>%</span>
                    </div>
                </div>
            </div>

            <div class="card air-quality">
                <div class="card-header">
                    <h3>🌪️ Qualité Air</h3>
                    <span class="sensor-status" id="air-status">--</span>
                </div>
                <div class="value-container">
                    <div class="main-value" id="current-aqi">-- AQI</div>
                    <div class="sub-values">
                        <span class="air-detail">CO₂: <span id="co2-value">--</span> ppm</span>
                        <span class="air-detail">TVOC: <span id="tvoc-value">--</span> ppb</span>
                    </div>
                </div>
            </div>

            <div class="card system">
                <div class="card-header">
                    <h3>⚙️ Système</h3>
                    <span class="sensor-status" id="sys-status">--</span>
                </div>
                <div class="value-container">
                    <div class="main-value" id="uptime">--</div>
                    <div class="sub-values">
                        <span class="sys-detail">WiFi: <span id="wifi-signal">--</span> dBm</span>
                        <span class="sys-detail">RAM: <span id="free-heap">--</span> KB</span>
                    </div>
                </div>
            </div>
        </section>

        <!-- Graphiques -->
        <section class="charts">
            <div class="chart-container">
                <h3>📈 Température (Aujourd'hui)</h3>
                <canvas id="tempChart"></canvas>
            </div>
            <div class="chart-container">
                <h3>📈 Humidité (Aujourd'hui)</h3>
                <canvas id="humChart"></canvas>
            </div>
            <div class="chart-container">
                <h3>📈 Qualité de l'air (Aujourd'hui)</h3>
                <canvas id="airChart"></canvas>
            </div>
        </section>

        <!-- Statistiques détaillées -->
        <section class="detailed-stats">
            <div class="stats-card">
                <h3>📊 Statistiques Journalières</h3>
                <div class="stats-grid">
                    <div class="stat-item">
                        <span class="stat-label">Lectures totales</span>
                        <span class="stat-value" id="total-readings">--</span>
                    </div>
                    <div class="stat-item">
                        <span class="stat-label">Taux de succès</span>
                        <span class="stat-value" id="success-rate">--%</span>
                    </div>
                    <div class="stat-item">
                        <span class="stat-label">Erreurs I2C</span>
                        <span class="stat-value" id="i2c-errors">--</span>
                    </div>
                    <div class="stat-item">
                        <span class="stat-label">Espace SD</span>
                        <span class="stat-value" id="sd-space">-- MB</span>
                    </div>
                </div>
            </div>
        </section>
    </main>

    <script>
        let tempChart, humChart, airChart;
        let dailyStats = {
            temp: { min: null, max: null },
            hum: { min: null, max: null },
            eco2: { min: null, max: null },
            tvoc: { min: null, max: null },
            aqi: { min: null, max: null }
        };
        let historicalDataLoaded = false;

        // Initialisation des graphiques
        function initCharts() {
            const chartConfig = {
                type: 'line',
                options: {
                    responsive: true,
                    maintainAspectRatio: false,
                    plugins: {
                        legend: { display: true }
                    },
                    scales: {
                        x: { 
                            display: true,
                            ticks: {
                                maxTicksLimit: 12,
                                callback: function(value, index, values) {
                                    const label = this.getLabelForValue(value);
                                    return label.substring(0, 5); // Afficher seulement HH:MM
                                }
                            }
                        },
                        y: { beginAtZero: false }
                    },
                    elements: {
                        point: { radius: 2 },
                        line: { tension: 0.2 }
                    },
                    animation: false
                }
            };

            // Graphique température
            tempChart = new Chart(document.getElementById('tempChart'), {
                ...chartConfig,
                data: {
                    labels: [],
                    datasets: [{
                        label: 'Température',
                        data: [],
                        borderColor: '#ff6b6b',
                        backgroundColor: 'rgba(255, 107, 107, 0.1)',
                        fill: true
                    }]
                },
                options: {
                    ...chartConfig.options,
                    scales: {
                        ...chartConfig.options.scales,
                        y: {
                            beginAtZero: false,
                            title: {
                                display: true,
                                text: 'Température (°C)'
                            }
                        }
                    }
                }
            });

            // Graphique humidité
            humChart = new Chart(document.getElementById('humChart'), {
                ...chartConfig,
                data: {
                    labels: [],
                    datasets: [{
                        label: 'Humidité',
                        data: [],
                        borderColor: '#4ecdc4',
                        backgroundColor: 'rgba(78, 205, 196, 0.1)',
                        fill: true
                    }]
                },
                options: {
                    ...chartConfig.options,
                    scales: {
                        ...chartConfig.options.scales,
                        y: {
                            beginAtZero: true,
                            max: 100,
                            title: {
                                display: true,
                                text: 'Humidité (%)'
                            }
                        }
                    }
                }
            });

            // Graphique qualité de l'air (3 métriques)
            airChart = new Chart(document.getElementById('airChart'), {
                ...chartConfig,
                data: {
                    labels: [],
                    datasets: [
                        {
                            label: 'AQI (1-5)',
                            data: [],
                            borderColor: '#45b7d1',
                            backgroundColor: 'rgba(69, 183, 209, 0.1)',
                            fill: false,
                            yAxisID: 'y'
                        },
                        {
                            label: 'CO₂ (ppm)',
                            data: [],
                            borderColor: '#96CEB4',
                            backgroundColor: 'rgba(150, 206, 180, 0.1)',
                            fill: false,
                            yAxisID: 'y1'
                        },
                        {
                            label: 'TVOC (ppb)',
                            data: [],
                            borderColor: '#FFEAA7',
                            backgroundColor: 'rgba(255, 234, 167, 0.1)',
                            fill: false,
                            yAxisID: 'y2'
                        }
                    ]
                },
                options: {
                    ...chartConfig.options,
                    scales: {
                        x: chartConfig.options.scales.x,
                        y: {
                            type: 'linear',
                            display: true,
                            position: 'left',
                            min: 1,
                            max: 5,
                            title: {
                                display: true,
                                text: 'AQI (1-5)'
                            }
                        },
                        y1: {
                            type: 'linear',
                            display: false,
                            position: 'right',
                            min: 400,
                            max: 2000,
                            grid: {
                                drawOnChartArea: false,
                            },
                        },
                        y2: {
                            type: 'linear',
                            display: false,
                            position: 'right',
                            min: 0,
                            max: 500,
                            grid: {
                                drawOnChartArea: false,
                            },
                        }
                    }
                }
            });
        }

        // Charger les données historiques du jour
        async function loadHistoricalData() {
            try {
                console.log('Chargement des données historiques...');
                const response = await fetch('/api/history?range=today');
                
                if (!response.ok) {
                    throw new Error(`Erreur HTTP: ${response.status}`);
                }
                
                const historyData = await response.json();
                console.log('Données reçues:', historyData);
                
                if (historyData.data && historyData.data.length > 0) {
                    console.log(`${historyData.data.length} points de données historiques chargés`);
                    
                    // Vider les graphiques
                    tempChart.data.labels = [];
                    tempChart.data.datasets[0].data = [];
                    humChart.data.labels = [];
                    humChart.data.datasets[0].data = [];
                    airChart.data.labels = [];
                    airChart.data.datasets[0].data = [];
                    airChart.data.datasets[1].data = [];
                    airChart.data.datasets[2].data = [];
                    
                    // Ajouter toutes les données historiques
                    historyData.data.forEach(point => {
                        const timeLabel = point.time || '00:00:00';
                        const shortTime = timeLabel.substring(0, 5); // HH:MM
                        
                        tempChart.data.labels.push(shortTime);
                        humChart.data.labels.push(shortTime);
                        airChart.data.labels.push(shortTime);
                        
                        // Température
                        if (point.temperature !== undefined && point.temperature !== -999) {
                            tempChart.data.datasets[0].data.push(point.temperature);
                            updateMinMax('temp', point.temperature);
                        } else {
                            tempChart.data.datasets[0].data.push(null);
                        }
                        
                        // Humidité
                        if (point.humidity !== undefined && point.humidity !== -999) {
                            humChart.data.datasets[0].data.push(point.humidity);
                            updateMinMax('hum', point.humidity);
                        } else {
                            humChart.data.datasets[0].data.push(null);
                        }
                        
                        // AQI
                        if (point.aqi !== undefined && point.aqi !== 0 && point.aqi >= 1 && point.aqi <= 5) {
                            airChart.data.datasets[0].data.push(point.aqi);
                            updateMinMax('aqi', point.aqi);
                        } else {
                            airChart.data.datasets[0].data.push(null);
                        }
                        
                        // CO₂
                        if (point.eco2 !== undefined && point.eco2 !== 0 && point.eco2 >= 400) {
                            airChart.data.datasets[1].data.push(point.eco2);
                            updateMinMax('eco2', point.eco2);
                        } else {
                            airChart.data.datasets[1].data.push(null);
                        }
                        
                        // TVOC
                        if (point.tvoc !== undefined && point.tvoc !== 0) {
                            airChart.data.datasets[2].data.push(point.tvoc);
                            updateMinMax('tvoc', point.tvoc);
                        } else {
                            airChart.data.datasets[2].data.push(null);
                        }
                    });
                    
                    // Mettre à jour les graphiques
                    tempChart.update('none');
                    humChart.update('none');
                    airChart.update('none');
                    
                    historicalDataLoaded = true;
                    console.log('Données historiques chargées avec succès');
                    
                    // Afficher les min/max de qualité d'air
                    console.log('Stats qualité air:', {
                        aqi: dailyStats.aqi,
                        eco2: dailyStats.eco2,
                        tvoc: dailyStats.tvoc
                    });
                    
                } else {
                    console.log('Aucune donnée historique disponible:', historyData);
                    historicalDataLoaded = true;
                }
                
            } catch (error) {
                console.error('Erreur lors du chargement des données historiques:', error);
                historicalDataLoaded = true;
            }
        }

        // Mise à jour des données actuelles
        async function updateCurrentData() {
            try {
                const response = await fetch('/api/current');
                const data = await response.json();
                
                document.getElementById('status-indicator').textContent = '🟢 Connecté';
                document.getElementById('last-update').textContent = 'Dernière MAJ: ' + new Date().toLocaleTimeString();
                
                // Mise à jour des valeurs actuelles
                updateCurrentValues(data);
                
                // Ajouter le nouveau point aux graphiques (seulement si les données historiques sont chargées)
                if (historicalDataLoaded) {
                    addNewDataPoint(data);
                }
                
                // Mise à jour des statistiques journalières
                updateDailyStats();
                
            } catch (error) {
                document.getElementById('status-indicator').textContent = '🔴 Erreur';
                console.error('Erreur:', error);
            }
        }

        function updateCurrentValues(data) {
            const sensors = data.sensors || {};
            
            // Température
            if (sensors.dht22 && sensors.dht22.status === 'OK') {
                document.getElementById('current-temp').textContent = sensors.dht22.temperature + '°C';
                document.getElementById('temp-status').textContent = '✅';
                updateMinMax('temp', sensors.dht22.temperature);
            } else if (sensors.aht21 && sensors.aht21.status === 'OK') {
                document.getElementById('current-temp').textContent = sensors.aht21.temperature + '°C';
                document.getElementById('temp-status').textContent = '✅';
                updateMinMax('temp', sensors.aht21.temperature);
            } else {
                document.getElementById('temp-status').textContent = '❌';
            }
            
            // Humidité
            if (sensors.dht22 && sensors.dht22.status === 'OK') {
                document.getElementById('current-hum').textContent = sensors.dht22.humidity + '%';
                document.getElementById('hum-status').textContent = '✅';
                updateMinMax('hum', sensors.dht22.humidity);
            } else if (sensors.aht21 && sensors.aht21.status === 'OK') {
                document.getElementById('current-hum').textContent = sensors.aht21.humidity + '%';
                document.getElementById('hum-status').textContent = '✅';
                updateMinMax('hum', sensors.aht21.humidity);
            } else {
                document.getElementById('hum-status').textContent = '❌';
            }
            
            // Qualité de l'air
            if (sensors.ens160 && sensors.ens160.status === 'OK') {
                document.getElementById('current-aqi').textContent = sensors.ens160.aqi + ' AQI';
                document.getElementById('co2-value').textContent = sensors.ens160.eco2;
                document.getElementById('tvoc-value').textContent = sensors.ens160.tvoc;
                document.getElementById('air-status').textContent = '✅';
            } else {
                document.getElementById('air-status').textContent = '❌';
            }
            
            // Système
            const system = data.system || {};
            document.getElementById('uptime').textContent = formatUptime(data.uptime || 0);
            document.getElementById('wifi-signal').textContent = system.wifi_rssi || '--';
            document.getElementById('free-heap').textContent = Math.round((system.free_heap || 0) / 1024);
            document.getElementById('sys-status').textContent = '✅';
        }

        function addNewDataPoint(data) {
            const now = new Date();
            const currentTime = now.getHours().toString().padStart(2, '0') + ':' + 
                              now.getMinutes().toString().padStart(2, '0');
            
            const sensors = data.sensors || {};
            
            // Ajouter le nouveau point
            tempChart.data.labels.push(currentTime);
            humChart.data.labels.push(currentTime);
            airChart.data.labels.push(currentTime);
            
            // Température et humidité
            if (sensors.dht22 && sensors.dht22.status === 'OK') {
                tempChart.data.datasets[0].data.push(sensors.dht22.temperature);
                humChart.data.datasets[0].data.push(sensors.dht22.humidity);
            } else if (sensors.aht21 && sensors.aht21.status === 'OK') {
                tempChart.data.datasets[0].data.push(sensors.aht21.temperature);
                humChart.data.datasets[0].data.push(sensors.aht21.humidity);
            } else {
                tempChart.data.datasets[0].data.push(null);
                humChart.data.datasets[0].data.push(null);
            }
            
            // Qualité de l'air
            if (sensors.ens160 && sensors.ens160.status === 'OK') {
                airChart.data.datasets[0].data.push(sensors.ens160.aqi); // AQI
                airChart.data.datasets[1].data.push(sensors.ens160.eco2); // CO₂
                airChart.data.datasets[2].data.push(sensors.ens160.tvoc); // TVOC
            } else {
                airChart.data.datasets[0].data.push(null);
                airChart.data.datasets[1].data.push(null);
                airChart.data.datasets[2].data.push(null);
            }
            
            // Limiter le nombre de points affichés (garder les 120 derniers points)
            const maxPoints = 120;
            if (tempChart.data.labels.length > maxPoints) {
                tempChart.data.labels.shift();
                tempChart.data.datasets[0].data.shift();
                humChart.data.labels.shift();
                humChart.data.datasets[0].data.shift();
                airChart.data.labels.shift();
                airChart.data.datasets[0].data.shift();
                airChart.data.datasets[1].data.shift();
                airChart.data.datasets[2].data.shift();
            }
            
            // Mettre à jour les graphiques
            tempChart.update('none');
            humChart.update('none');
            airChart.update('none');
        }

        function updateMinMax(type, value) {
            if (!dailyStats[type]) dailyStats[type] = { min: null, max: null };
            
            if (!dailyStats[type].min || value < dailyStats[type].min) {
                dailyStats[type].min = value;
                if (type === 'temp') {
                    document.getElementById('temp-min').textContent = value.toFixed(1);
                } else if (type === 'hum') {
                    document.getElementById('hum-min').textContent = value.toFixed(1);
                }
            }
            if (!dailyStats[type].max || value > dailyStats[type].max) {
                dailyStats[type].max = value;
                if (type === 'temp') {
                    document.getElementById('temp-max').textContent = value.toFixed(1);
                } else if (type === 'hum') {
                    document.getElementById('hum-max').textContent = value.toFixed(1);
                }
            }
        }

        async function updateDailyStats() {
            try {
                const response = await fetch('/api/stats');
                const stats = await response.json();
                
                document.getElementById('total-readings').textContent = stats.total_readings || 0;
                document.getElementById('success-rate').textContent = (stats.success_rate || 0).toFixed(1);
                document.getElementById('i2c-errors').textContent = stats.i2c_errors || 0;
                document.getElementById('sd-space').textContent = stats.sd_space_mb || 0;
                
            } catch (error) {
                console.error('Erreur stats:', error);
            }
        }

        function formatUptime(seconds) {
            const days = Math.floor(seconds / 86400);
            const hours = Math.floor((seconds % 86400) / 3600);
            const mins = Math.floor((seconds % 3600) / 60);
            return `${days}j ${hours}h ${mins}m`;
        }

        // Initialisation
        document.addEventListener('DOMContentLoaded', async function() {
            console.log('Initialisation de la page...');
            
            // Initialiser les graphiques
            initCharts();
            
            // Charger d'abord les données historiques
            await loadHistoricalData();
            
            // Puis faire la première mise à jour des données actuelles
            await updateCurrentData();
            
            // Programmer les mises à jour régulières toutes les 2 minutes (120000 ms)
            setInterval(updateCurrentData, 120000);
            
            console.log('Initialisation terminée. Mises à jour programmées toutes les 2 minutes.');
        });
    </script>
</body>
</html>
)rawliteral";
  
  server.send(200, "text/html", html);
}

void handleCSS() {
  String css = R"rawliteral(
* {
    margin: 0;
    padding: 0;
    box-sizing: border-box;
}

body {
    font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
    background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
    min-height: 100vh;
    color: #333;
}

header {
    background: rgba(255, 255, 255, 0.95);
    padding: 1rem 2rem;
    box-shadow: 0 2px 10px rgba(0,0,0,0.1);
    margin-bottom: 2rem;
}

header h1 {
    color: #4a5568;
    margin-bottom: 0.5rem;
}

.status-bar {
    display: flex;
    gap: 2rem;
    font-size: 0.9rem;
    color: #666;
}

main {
    max-width: 1200px;
    margin: 0 auto;
    padding: 0 1rem;
}

.current-data {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(250px, 1fr));
    gap: 1.5rem;
    margin-bottom: 3rem;
}

.card {
    background: rgba(255, 255, 255, 0.95);
    border-radius: 15px;
    padding: 1.5rem;
    box-shadow: 0 8px 32px rgba(0,0,0,0.1);
    backdrop-filter: blur(10px);
    border: 1px solid rgba(255, 255, 255, 0.2);
    transition: transform 0.3s ease;
}

.card:hover {
    transform: translateY(-5px);
}

.card-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 1rem;
}

.card-header h3 {
    color: #4a5568;
    font-size: 1.1rem;
}

.sensor-status {
    font-size: 1.2rem;
}

.main-value {
    font-size: 2.5rem;
    font-weight: bold;
    color: #2d3748;
    margin-bottom: 0.5rem;
}

.sub-values {
    display: flex;
    flex-direction: column;
    gap: 0.3rem;
    font-size: 0.9rem;
    color: #666;
}

.temperature .main-value { color: #e53e3e; }
.humidity .main-value { color: #38b2ac; }
.air-quality .main-value { color: #3182ce; }
.system .main-value { color: #805ad5; font-size: 1.8rem; }

.charts {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(350px, 1fr));
    gap: 2rem;
    margin-bottom: 3rem;
}

.chart-container {
    background: rgba(255, 255, 255, 0.95);
    border-radius: 15px;
    padding: 1.5rem;
    box-shadow: 0 8px 32px rgba(0,0,0,0.1);
}

.chart-container h3 {
    color: #4a5568;
    margin-bottom: 1rem;
    text-align: center;
}

.chart-container canvas {
    max-height: 300px;
}

.detailed-stats {
    margin-bottom: 3rem;
}

.stats-card {
    background: rgba(255, 255, 255, 0.95);
    border-radius: 15px;
    padding: 2rem;
    box-shadow: 0 8px 32px rgba(0,0,0,0.1);
}

.stats-card h3 {
    color: #4a5568;
    margin-bottom: 1.5rem;
    text-align: center;
}

.stats-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
    gap: 1.5rem;
}

.stat-item {
    display: flex;
    flex-direction: column;
    align-items: center;
    text-align: center;
    padding: 1rem;
    background: rgba(248, 250, 252, 0.8);
    border-radius: 10px;
}

.stat-label {
    font-size: 0.9rem;
    color: #666;
    margin-bottom: 0.5rem;
}

.stat-value {
    font-size: 1.8rem;
    font-weight: bold;
    color: #2d3748;
}

@media (max-width: 768px) {
    .current-data {
        grid-template-columns: 1fr;
    }
    
    .charts {
        grid-template-columns: 1fr;
    }
    
    .stats-grid {
        grid-template-columns: repeat(2, 1fr);
    }
    
    header {
        padding: 1rem;
    }
    
    .status-bar {
        flex-direction: column;
        gap: 0.5rem;
    }
}
)rawliteral";
  
  server.send(200, "text/css", css);
}

void handleDailyStats() {
  if (!sdCardInitialized) {
    server.send(503, "application/json", "{\"error\":\"SD card not available\"}");
    return;
  }
  
  String today = catch_time_robust().substring(0, 10);
  String filename = "/" + today + ".csv";
  
  StaticJsonDocument<1024> doc;
  doc["date"] = today;
  
  if (SD.exists(filename)) {
    File dataFile = SD.open(filename, FILE_READ);
    if (dataFile) {
      // Initialiser min/max
      float tempMin = 999, tempMax = -999;
      float humMin = 999, humMax = -999;
      int readingsCount = 0;
      
      String line;
      bool firstLine = true;
      
      while (dataFile.available()) {
        line = dataFile.readStringUntil('\n');
        line.trim();
        
        if (firstLine || line.startsWith("Date")) {
          firstLine = false;
          continue;
        }
        
        // Parser la ligne CSV
        int commaIndex = 0;
        String values[14];
        int valueIndex = 0;
        
        for (int i = 0; i < line.length() && valueIndex < 14; i++) {
          if (line.charAt(i) == ',' || i == line.length() - 1) {
            values[valueIndex] = line.substring(commaIndex, i);
            commaIndex = i + 1;
            valueIndex++;
          }
        }
        
        if (valueIndex >= 6) {
          float tempDHT = values[2].toFloat();
          float humDHT = values[3].toFloat();
          float tempAHT = values[4].toFloat();
          float humAHT = values[5].toFloat();
          
          // Utiliser DHT si valide, sinon AHT
          float temp = (tempDHT > -50 && tempDHT < 100) ? tempDHT : tempAHT;
          float hum = (humDHT > 0 && humDHT <= 100) ? humDHT : humAHT;
          
          if (temp > -50 && temp < 100) {
            if (temp < tempMin) tempMin = temp;
            if (temp > tempMax) tempMax = temp;
          }
          
          if (hum > 0 && hum <= 100) {
            if (hum < humMin) humMin = hum;
            if (hum > humMax) humMax = hum;
          }
          
          readingsCount++;
        }
      }
      
      dataFile.close();
      
      doc["readings_count"] = readingsCount;
      if (tempMin < 999) {
        doc["temperature"]["min"] = round(tempMin * 10) / 10.0;
        doc["temperature"]["max"] = round(tempMax * 10) / 10.0;
      }
      if (humMin < 999) {
        doc["humidity"]["min"] = round(humMin * 10) / 10.0;
        doc["humidity"]["max"] = round(humMax * 10) / 10.0;
      }
      
    } else {
      doc["error"] = "Cannot read file";
    }
  } else {
    doc["readings_count"] = 0;
    doc["note"] = "No data file for today";
  }
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleCurrentDataCached() {
  unsigned long now = millis();
  
  if (apiCurrentCache.valid && (now - apiCurrentCache.timestamp < API_CACHE_DURATION_MS)) {
    server.sendHeader("Cache-Control", "public, max-age=2");
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", apiCurrentCache.data);
    return;
  }
  
  SensorData currentData;
  String fullTime = catch_time_robust();
  currentData.date = fullTime.substring(0, 10);
  currentData.time = fullTime.substring(11);
  
  read_all_sensors_robust(currentData);
  
  String json = buildOptimizedJSON(currentData);
  
  apiCurrentCache.data = json;
  apiCurrentCache.timestamp = now;
  apiCurrentCache.valid = true;
  
  server.sendHeader("Cache-Control", "public, max-age=2");
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Content-Type", "application/json");
  server.send(200, "application/json", json);
}

String buildOptimizedJSON(const SensorData& data) {
  StaticJsonDocument<JSON_BUFFER_SIZE> doc;
  
  doc["timestamp"] = data.date + " " + data.time;
  doc["uptime"] = millis() / 1000;
  
  JsonObject sensors = doc.createNestedObject("sensors");
  
  if (data.validDHT) {
    JsonObject dht = sensors.createNestedObject("dht22");
    dht["temperature"] = round(data.temperatureDHT * 100) / 100.0;
    dht["humidity"] = round(data.humidityDHT * 10) / 10.0;
    dht["status"] = "OK";
  } else {
    sensors["dht22"]["status"] = "ERROR";
  }
  
  if (data.validAHT) {
    JsonObject aht = sensors.createNestedObject("aht21");
    aht["temperature"] = round(data.temperatureAHT * 100) / 100.0;
    aht["humidity"] = round(data.humidityAHT * 10) / 10.0;
    aht["status"] = "OK";
  } else {
    sensors["aht21"]["status"] = "ERROR";
  }
  
  if (data.validENS160) {
    JsonObject ens = sensors.createNestedObject("ens160");
    ens["eco2"] = data.eco2;
    ens["tvoc"] = data.tvoc;
    ens["aqi"] = data.aqi;
    ens["status"] = "OK";
  } else {
    sensors["ens160"]["status"] = "ERROR";
  }
  
  JsonObject system = doc.createNestedObject("system");
  system["wifi_rssi"] = WiFi.RSSI();
  system["free_heap"] = ESP.getFreeHeap();
  system["sd_card"] = sdCardInitialized;
  system["i2c_errors"] = i2cErrorCount;
  
  String output;
  serializeJson(doc, output);
  return output;
}

void handleHealthCheck() {
  StaticJsonDocument<STATS_JSON_BUFFER_SIZE> doc;
  doc["status"] = "OK";
  doc["wifi"] = wifiConnected;
  doc["uptime"] = millis() / 1000;
  doc["free_memory"] = ESP.getFreeHeap();
  doc["sensors"]["dht22"] = true;
  doc["sensors"]["aht21"] = ahtInitialized;
  doc["sensors"]["ens160"] = ens160Initialized;
  doc["sd_card"] = sdCardInitialized;
  
  String response;
  serializeJson(doc, response);
  
  server.sendHeader("Cache-Control", "no-cache");
  server.send(200, "application/json", response);
}

void handleStatsCached() {
  unsigned long now = millis();
  
  if (apiStatsCache.valid && (now - apiStatsCache.timestamp < STATS_CACHE_DURATION_MS)) {
    server.send(200, "application/json", apiStatsCache.data);
    return;
  }
  
  StaticJsonDocument<STATS_JSON_BUFFER_SIZE> doc;
  doc["total_readings"] = totalReadingsCount;
  doc["valid_readings"] = validReadingsCount;
  doc["success_rate"] = totalReadingsCount > 0 ? (float)validReadingsCount / totalReadingsCount * 100 : 0;
  doc["i2c_errors"] = i2cErrorCount;
  doc["wifi_signal"] = WiFi.RSSI();
  doc["free_heap"] = ESP.getFreeHeap();
  doc["sd_space_mb"] = sdTotalSpace > 0 ? sdTotalSpace - sdUsedSpace : 0;
  
  String response;
  serializeJson(doc, response);
  
  apiStatsCache.data = response;
  apiStatsCache.timestamp = now;
  apiStatsCache.valid = true;
  
  server.send(200, "application/json", response);
}

void handleRootOptimized() {
  String html = "<!DOCTYPE html><html><head><title>Station Meteo ESP32</title></head>";
  html += "<body><h1>Station Meteorologique ESP32</h1>";
  html += "<p>Acces API:</p><ul>";
  html += "<li><a href='/api/current'>Donnees actuelles</a></li>";
  html += "<li><a href='/api/health'>Etat systeme</a></li>";
  html += "<li><a href='/api/stats'>Statistiques</a></li>";
  html += "<li><a href='/api/files'>Fichiers SD</a></li>";
  html += "</ul></body></html>";
  
  server.send(200, "text/html", html);
}

void handleFileListOptimized() {
  if (!sdCardInitialized) {
    server.send(503, "application/json", "{\"error\":\"SD card not available\"}");
    return;
  }
  
  StaticJsonDocument<1024> doc;
  JsonArray files = doc.createNestedArray("files");
  
  File root = SD.open("/");
  if (root) {
    File file = root.openNextFile();
    while (file) {
      if (!file.isDirectory()) {
        JsonObject fileObj = files.createNestedObject();
        fileObj["name"] = file.name();
        fileObj["size"] = file.size();
      }
      file.close();
      file = root.openNextFile();
    }
    root.close();
  }
  
  String response;
  serializeJson(doc, response);
  server.send(200, "application/json", response);
}

void handleFavicon() {
  server.send(404, "text/plain", "Not found");
}

void handleNotFoundOptimized() {
  StaticJsonDocument<128> doc;
  doc["error"] = "Endpoint not found";
  doc["available_endpoints"] = JsonArray();
  doc["available_endpoints"].add("/api/current");
  doc["available_endpoints"].add("/api/health");
  doc["available_endpoints"].add("/api/files");
  
  String response;
  serializeJson(doc, response);
  
  server.send(404, "application/json", response);
}

// ===== GESTION CAPTEURS I2C ULTRA-ROBUSTE =====

void read_all_sensors_robust(SensorData &datas) {
  totalReadingsCount++;
  
  // DHT22 - lecture fiable
  datas.temperatureDHT = catch_temp();
  datas.humidityDHT = catch_hum();
  datas.validDHT = !isnan(datas.temperatureDHT) && !isnan(datas.humidityDHT) &&
                   datas.temperatureDHT > DHT_TEMP_MIN && datas.temperatureDHT < DHT_TEMP_MAX &&
                   datas.humidityDHT >= DHT_HUM_MIN && datas.humidityDHT <= DHT_HUM_MAX;
  
  if (datas.validDHT) {
    Serial.printf("DHT22 => T=%.2f°C, H=%.1f%%\n", datas.temperatureDHT, datas.humidityDHT);
  } else {
    Serial.printf("DHT22 lecture invalide: T=%.2f, H=%.1f\n", datas.temperatureDHT, datas.humidityDHT);
    datas.temperatureDHT = ERROR_TEMP_VALUE;
    datas.humidityDHT = ERROR_HUM_VALUE;
  }
  
  delay(SENSOR_READ_DELAY_MS);
  
  // AHT21 - avec validation stricte et retry
  datas.validAHT = false;
  if (ahtInitialized) {
    for (int retry = 0; retry < 2; retry++) {
      sensors_event_t humEvent, tempEvent;
      
      if (aht.getEvent(&humEvent, &tempEvent)) {
        float temp = tempEvent.temperature;
        float hum = humEvent.relative_humidity;
        
        if (!isnan(temp) && !isnan(hum) && 
            temp > AHT_TEMP_MIN && temp < AHT_TEMP_MAX &&
            hum >= AHT_HUM_MIN && hum <= AHT_HUM_MAX &&
            abs(temp) < 200 && abs(hum) < 200) {
          
          datas.temperatureAHT = temp;
          datas.humidityAHT = hum;
          datas.validAHT = true;
          Serial.printf("AHT21 => T=%.2f°C, H=%.1f%% (retry %d)\n", temp, hum, retry);
          break;
        } else {
          Serial.printf("AHT21 valeurs aberrantes: T=%.2f, H=%.1f (retry %d)\n", temp, hum, retry);
          if (retry == 0) delay(500);
        }
      } else {
        Serial.printf("AHT21 echec communication (retry %d)\n", retry);
        if (retry == 0) delay(500);
      }
    }
    
    if (!datas.validAHT) {
      i2cErrorCount++;
      Serial.println("AHT21 definitivement en erreur");
    }
  }
  
  if (!datas.validAHT) {
    datas.temperatureAHT = ERROR_TEMP_VALUE;
    datas.humidityAHT = ERROR_HUM_VALUE;
  }
  
  delay(SENSOR_READ_DELAY_MS);
  
  // ENS160 - avec validation stricte et retry
  datas.validENS160 = false;
  if (ens160Initialized) {
    for (int retry = 0; retry < 2; retry++) {
      if (ens160.available()) {
        ens160.measure(true);
        delay(50);
        ens160.measureRaw(true);
        delay(50);
        
        uint16_t eco2 = ens160.geteCO2();
        uint16_t tvoc = ens160.getTVOC();
        uint8_t aqi = ens160.getAQI();
        
        if (eco2 != 65535 && tvoc != 65535 && aqi != 255 &&
            eco2 >= ENS160_ECO2_MIN && eco2 <= ENS160_ECO2_MAX &&
            tvoc >= 0 && tvoc <= ENS160_TVOC_MAX &&
            aqi >= ENS160_AQI_MIN && aqi <= ENS160_AQI_MAX) {
          
          datas.eco2 = eco2;
          datas.tvoc = tvoc;
          datas.aqi = aqi;
          datas.validENS160 = true;
          Serial.printf("ENS160 => eCO2=%u ppm, TVOC=%u ppb, AQI=%u (retry %d)\n", 
                       eco2, tvoc, aqi, retry);
          break;
        } else {
          Serial.printf("ENS160 valeurs aberrantes: eCO2=%u, TVOC=%u, AQI=%u (retry %d)\n", 
                       eco2, tvoc, aqi, retry);
          if (retry == 0) delay(500);
        }
      } else {
        Serial.printf("ENS160 non disponible (retry %d)\n", retry);
        if (retry == 0) delay(500);
      }
    }
    
    if (!datas.validENS160) {
      i2cErrorCount++;
      Serial.println("ENS160 definitivement en erreur");
    }
  }
  
  if (!datas.validENS160) {
    datas.eco2 = ERROR_CO2_VALUE;
    datas.tvoc = ERROR_TVOC_VALUE;
    datas.aqi = ERROR_AQI_VALUE;
  }
  
  // Compter lectures valides
  if (datas.validDHT || datas.validAHT || datas.validENS160) {
    validReadingsCount++;
  }
  
  // Diagnostic périodique
  if (totalReadingsCount % 50 == 0) {
    float successRate = (float)validReadingsCount / totalReadingsCount * 100;
    Serial.printf("Taux succes capteurs: %.1f%% (%d/%d)\n", 
                 successRate, validReadingsCount, totalReadingsCount);
  }
}

void monitor_i2c_health_enhanced() {
  unsigned long now = millis();
  
  // Scan I2C périodique
  if (now - lastI2CScan > I2C_SCAN_INTERVAL_MS) {
    lastI2CScan = now;
    scan_i2c_enhanced();
  }
  
  // Reset I2C si trop d'erreurs
  if (now - lastI2CReset > I2C_RESET_INTERVAL_MS) {
    if (i2cErrorCount > I2C_ERROR_THRESHOLD) {
      Serial.printf("I2C instable (%d erreurs), reset complet...\n", i2cErrorCount);
      
      Wire.end();
      delay(1000);
      init_i2c();
      delay(2000);
      
      init_sensors_robust();
      
      i2cErrorCount = 0;
      lastI2CReset = now;
      
      Serial.println("Reset I2C termine");
    }
  }
}

void scan_i2c_enhanced() {
  Serial.println("Scan I2C detaille...");
  int deviceCount = 0;
  bool currentDevices[128] = {false};
  
  for (byte address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    byte error = Wire.endTransmission();
    
    if (error == 0) {
      currentDevices[address] = true;
      deviceCount++;
      
      String deviceName = "Inconnu";
      if (address == 0x38) deviceName = "AHT21";
      else if (address == 0x53) deviceName = "ENS160";
      
      if (!i2cDevicesDetected[address]) {
        Serial.printf("Nouveau device: 0x%02X (%s)\n", address, deviceName.c_str());
      }
      
      Serial.printf("Device 0x%02X (%s) - OK\n", address, deviceName.c_str());
    } else {
      if (i2cDevicesDetected[address]) {
        Serial.printf("Device perdu: 0x%02X\n", address);
      }
    }
    
    delay(10);
  }
  
  memcpy(i2cDevicesDetected, currentDevices, sizeof(currentDevices));
  
  Serial.printf("Scan termine: %d device(s) trouve(s)\n", deviceCount);
  
  // Vérifier capteurs critiques
  if (!currentDevices[0x38] && ahtInitialized) {
    Serial.println("AHT21 deconnecte !");
    ahtInitialized = false;
  }
  if (!currentDevices[0x53] && ens160Initialized) {
    Serial.println("ENS160 deconnecte !");
    ens160Initialized = false;
  }
}

// ===== GESTION CARTE SD ULTRA-ROBUSTE =====

void init_sd_card_robust() {
  Serial.println("Initialisation carte SD robuste...");
  
  for (int attempt = 0; attempt < 3; attempt++) {
    if (SD.begin(SD_CS_PIN)) {
      sdCardInitialized = true;
      
      sdTotalSpace = SD.cardSize() / (1024 * 1024);
      sdUsedSpace = SD.usedBytes() / (1024 * 1024);
      float usagePercent = (float)sdUsedSpace / sdTotalSpace * 100;
      
      Serial.printf("SD initialisee: %lluMB total, %lluMB utilises (%.1f%%)\n", 
                   sdTotalSpace, sdUsedSpace, usagePercent);
      
      if (test_sd_write()) {
        Serial.println("Test ecriture SD reussi");
        sdHealthy = true;
      } else {
        Serial.println("Test ecriture SD echoue");
        sdHealthy = false;
      }
      
      return;
    } else {
      Serial.printf("Tentative SD %d/3 echouee\n", attempt + 1);
      delay(1000);
    }
  }
  
  sdCardInitialized = false;
  sdHealthy = false;
  Serial.println("Echec initialisation carte SD");
}

bool test_sd_write() {
  File testFile = SD.open("/test_write.tmp", FILE_WRITE);
  if (testFile) {
    testFile.println("Test write");
    testFile.close();
    
    if (SD.remove("/test_write.tmp")) {
      return true;
    }
  }
  return false;
}

void save_on_sd_card_robust(const SensorData& datas) {
  if (!sdCardInitialized || !sdHealthy) return;
  
  if (!datas.validDHT && !datas.validAHT && !datas.validENS160) {
    Serial.println("Aucune donnee valide - pas de sauvegarde");
    return;
  }
  
  String filename = "/" + datas.date + ".csv";
  bool newFile = !SD.exists(filename);

  File dataFile = SD.open(filename, FILE_APPEND);
  if (dataFile) {
    if (newFile) {
      dataFile.println("Date,Heure,Temp_DHT22,Hum_DHT22,Temp_AHT21,Hum_AHT21,eCO2_ppm,TVOC_ppb,AQI,Status_DHT,Status_AHT,Status_ENS,WiFi_RSSI,Free_Heap");
    }
    
    dataFile.printf("%s,%s,%.2f,%.1f,%.2f,%.1f,%u,%u,%u,%s,%s,%s,%d,%u\n",
                    datas.date.c_str(),
                    datas.time.c_str(),
                    datas.validDHT ? datas.temperatureDHT : ERROR_TEMP_VALUE,
                    datas.validDHT ? datas.humidityDHT : ERROR_HUM_VALUE,
                    datas.validAHT ? datas.temperatureAHT : ERROR_TEMP_VALUE,
                    datas.validAHT ? datas.humidityAHT : ERROR_HUM_VALUE,
                    datas.validENS160 ? datas.eco2 : ERROR_CO2_VALUE,
                    datas.validENS160 ? datas.tvoc : ERROR_TVOC_VALUE,
                    datas.validENS160 ? datas.aqi : ERROR_AQI_VALUE,
                    datas.validDHT ? "OK" : "ERR",
                    datas.validAHT ? "OK" : "ERR",
                    datas.validENS160 ? "OK" : "ERR",
                    WiFi.RSSI(),
                    ESP.getFreeHeap());
    
    dataFile.close();
    Serial.printf("Sauvegarde dans %s\n", filename.c_str());
    
  } else {
    Serial.println("Erreur ouverture fichier SD");
    sdHealthy = false;
  }
}

void monitor_sd_space() {
  unsigned long now = millis();
  
  if (now - sdLastCheck > SD_SPACE_CHECK_INTERVAL_MS) {
    sdLastCheck = now;
    
    uint64_t newUsedSpace = SD.usedBytes() / (1024 * 1024);
    float usagePercent = (float)newUsedSpace / sdTotalSpace * 100;
    
    Serial.printf("Espace SD: %lluMB/%lluMB (%.1f%%)\n", 
                 newUsedSpace, sdTotalSpace, usagePercent);
    
    if (usagePercent > SD_CRITICAL_SPACE_PERCENT) {
      Serial.println("Espace SD critique ! Nettoyage force...");
      force_cleanup_sd();
    }
    
    sdUsedSpace = newUsedSpace;
  }
}

void clean_old_files_robust() {
  static unsigned long lastCleanup = 0;
  unsigned long now = millis();
  
  if (now - lastCleanup < SD_CLEANUP_INTERVAL_MS) return;
  lastCleanup = now;
  
  if (!sdCardInitialized || !wifiConnected) {
    Serial.println("Nettoyage reporte (SD ou WiFi indisponible)");
    return;
  }
  
  perform_cleanup(SD_CLEANUP_DAYS);
}

void force_cleanup_sd() {
  Serial.println("Nettoyage force de la carte SD...");
  perform_cleanup(3);
}

void perform_cleanup(int daysToKeep) {
  Serial.printf("Nettoyage fichiers > %d jours...\n", daysToKeep);
  
  if (!timeClient.update()) {
    for (int i = 0; i < 3; i++) {
      if (timeClient.forceUpdate()) break;
      delay(1000);
    }
  }
  
  unsigned long epochTime = timeClient.getEpochTime();
  if (epochTime < 1609459200) {
    Serial.println("Heure NTP invalide - nettoyage annule");
    return;
  }
  
  unsigned long cutoffTime = epochTime - (daysToKeep * 24UL * 3600UL);
  
  time_t cutoff_t = (time_t)cutoffTime;
  struct tm* cutoff_tm = localtime(&cutoff_t);
  Serial.printf("Suppression fichiers avant: %04d-%02d-%02d\n", 
                cutoff_tm->tm_year + 1900, cutoff_tm->tm_mon + 1, cutoff_tm->tm_mday);
  
  File root = SD.open("/");
  if (!root) {
    Serial.println("Impossible d'ouvrir repertoire racine");
    return;
  }
  
  int filesChecked = 0;
  int filesDeleted = 0;
  uint64_t spaceFreed = 0;
  
  File file = root.openNextFile();
  while (file) {
    String filename = file.name();
    
    if (filename.endsWith(".csv") && filename.length() >= 14) {
      filesChecked++;
      
      String dateStr = extractDateFromFilename(filename);
      
      if (isValidDateFormat(dateStr)) {
        time_t fileTime = parseFileDate(dateStr);
        
        if (fileTime > 0 && (unsigned long)fileTime < cutoffTime) {
          size_t fileSize = file.size();
          file.close();
          
          if (SD.remove(filename)) {
            filesDeleted++;
            spaceFreed += fileSize;
            Serial.printf("Supprime: %s (%d octets)\n", filename.c_str(), fileSize);
          } else {
            Serial.printf("Echec suppression: %s\n", filename.c_str());
          }
          
          root.close();
          root = SD.open("/");
          file = root.openNextFile();
          continue;
        }
      }
    }
    
    file.close();
    file = root.openNextFile();
  }
  
  root.close();
  Serial.printf("Nettoyage termine: %d fichiers verifies, %d supprimes, %llu octets liberes\n", 
               filesChecked, filesDeleted, spaceFreed);
}

String extractDateFromFilename(const String& filename) {
  if (filename.startsWith("/") && filename.length() >= 15) {
    return filename.substring(1, 11);
  } else if (filename.length() >= 14) {
    return filename.substring(0, 10);
  }
  return "";
}

bool isValidDateFormat(const String& dateStr) {
  return (dateStr.length() == 10 && 
          dateStr.charAt(4) == '-' && 
          dateStr.charAt(7) == '-');
}

time_t parseFileDate(const String& dateStr) {
  int year = dateStr.substring(0, 4).toInt();
  int month = dateStr.substring(5, 7).toInt();
  int day = dateStr.substring(8, 10).toInt();
  
  if (year >= 2020 && year <= 2030 && 
      month >= 1 && month <= 12 && 
      day >= 1 && day <= 31) {
    
    struct tm fileDate = {0};
    fileDate.tm_year = year - 1900;
    fileDate.tm_mon = month - 1;
    fileDate.tm_mday = day;
    fileDate.tm_hour = 12;
    
    return mktime(&fileDate);
  }
  
  return 0;
}

// ===== GESTION TEMPS ROBUSTE =====

String catch_time_robust() {
  unsigned long now = millis();
  
  // Si pas de WiFi, utiliser temps système
  if (!wifiConnected) {
    unsigned long uptime = now / 1000;
    unsigned long days = uptime / 86400;
    unsigned long hours = (uptime % 86400) / 3600;
    unsigned long minutes = (uptime % 3600) / 60;
    unsigned long seconds = uptime % 60;
    
    char buffer[32];
    sprintf(buffer, "1970-01-%02lu %02lu:%02lu:%02lu", days + 1, hours, minutes, seconds);
    return String(buffer);
  }
  
  // Synchroniser NTP seulement si nécessaire
  bool needSync = false;
  
  if (!timeInitialized) {
    // Première synchronisation
    needSync = true;
  } else if (now - lastNTPSync > 300000) {
    // Resync toutes les 5 minutes seulement
    needSync = true;
  }
  
  if (needSync) {
    Serial.println("Synchronisation NTP...");
    
    // Essayer une seule fois rapidement
    if (timeClient.forceUpdate()) {
      lastNTPSync = now;
      timeInitialized = true;
      Serial.println("NTP sync reussie");
    } else {
      Serial.println("NTP sync echouee - utilisation derniere heure connue");
      
      // Si échec et pas encore initialisé, essayer une fois de plus
      if (!timeInitialized) {
        delay(500);
        if (timeClient.forceUpdate()) {
          lastNTPSync = now;
          timeInitialized = true;
          Serial.println("NTP sync reussie (2eme tentative)");
        }
      }
    }
  }
  
  // Obtenir l'heure actuelle
  unsigned long epochTime = timeClient.getEpochTime();
  
  // Vérifier validité
  if (epochTime < 1609459200) { // 1er janvier 2021
    if (timeInitialized) {
      // Calculer temps approximatif basé sur la dernière sync
      unsigned long elapsedSinceSync = (now - lastNTPSync) / 1000;
      epochTime = timeClient.getEpochTime() + elapsedSinceSync;
      
      // Si toujours invalide, retourner temps système
      if (epochTime < 1609459200) {
        Serial.println("Heure NTP invalide - temps systeme");
        return lastKnownTime;
      }
    } else {
      Serial.println("NTP non initialise - temps systeme");
      return "1970-01-01 00:00:00";
    }
  }

  // Convertir en format lisible
  time_t rawTime = (time_t) epochTime;
  struct tm *ptm = localtime(&rawTime);

  char buffer[32];
  sprintf(buffer, "%04d-%02d-%02d %02d:%02d:%02d",
          (ptm->tm_year+1900),
          (ptm->tm_mon+1),
          ptm->tm_mday,
          ptm->tm_hour,
          ptm->tm_min,
          ptm->tm_sec);

  lastKnownTime = String(buffer);
  return lastKnownTime;
}

// ===== FONCTIONS DE BASE =====

void init_i2c() {
  Serial.println("Initialisation I2C...");
  Wire.end();
  delay(100);
  
  if (!Wire.begin(SDA_PIN, SCL_PIN)) {
    Serial.println("Echec initialisation I2C");
    return;
  }
  
  Wire.setClock(I2C_FREQUENCY);
  delay(500);
  Serial.printf("I2C initialise (%d Hz)\n", I2C_FREQUENCY);
}

void init_dht() {
  Serial.println("Initialisation DHT22...");
  dht.begin();
  delay(DHT_READ_DELAY_MS);
  
  float test_temp = dht.readTemperature();
  float test_hum = dht.readHumidity();
  
  if (!isnan(test_temp) && !isnan(test_hum)) {
    Serial.printf("DHT22 operationnel: T=%.1f°C, H=%.1f%%\n", test_temp, test_hum);
  } else {
    Serial.println("DHT22 initialise mais lecture echouee");
  }
}

void init_sensors_robust() {
  Serial.println("Initialisation capteurs robuste...");
  scan_i2c_enhanced();
  
  // AHT21 avec retry
  ahtInitialized = false;
  for (int attempt = 0; attempt < 3; attempt++) {
    if (aht.begin()) {
      ahtInitialized = true;
      Serial.println("AHT21 detecte et initialise");
      break;
    } else {
      Serial.printf("Tentative AHT21 %d/3 echouee\n", attempt + 1);
      delay(1000);
      if (attempt < 2) {
        Wire.end();
        delay(500);
        Wire.begin(SDA_PIN, SCL_PIN);
        Wire.setClock(I2C_FREQUENCY);
        delay(500);
      }
    }
  }
  
  if (!ahtInitialized) {
    Serial.println("AHT21 non detecte");
  }
  
  delay(1000);
  
  // ENS160 avec retry
  ens160Initialized = false;
  for (int attempt = 0; attempt < 3; attempt++) {
    if (ens160.begin() && ens160.available()) {
      ens160Initialized = true;
      Serial.println("ENS160 detecte et initialise");
      Serial.printf("ENS160 Rev: %d.%d.%d\n", 
                    ens160.getMajorRev(), 
                    ens160.getMinorRev(), 
                    ens160.getBuild());
      
      if (ens160.setMode(ENS160_OPMODE_STD)) {
        Serial.println("ENS160 en mode standard");
      } else {
        Serial.println("Erreur mode standard ENS160");
      }
      break;
    } else {
      Serial.printf("Tentative ENS160 %d/3 echouee\n", attempt + 1);
      delay(1000);
    }
  }
  
  if (!ens160Initialized) {
    Serial.println("ENS160 non detecte");
  }
}

float catch_temp() {
  float temperature = dht.readTemperature();
  if (isnan(temperature)) {
    Serial.println("Echec lecture temperature DHT22");
    return ERROR_TEMP_VALUE;
  }
  return temperature;
}

float catch_hum() {
  float humidity = dht.readHumidity();
  if (isnan(humidity)) {
    Serial.println("Echec lecture humidite DHT22");
    return ERROR_HUM_VALUE;
  }
  return humidity;
}