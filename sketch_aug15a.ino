#include <DHT.h>
#include <WiFi.h>
#include <SPI.h>
#include <SD.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <Adafruit_AHTX0.h>
#include <ScioSense_ENS160.h>

// Inclure le fichier de configuration (non versionné)
#include "config.h"

WiFiUDP ntpUDP;                
NTPClient timeClient(ntpUDP, ntp_server, ntp_offset, ntp_update_interval);  

DHT dht(DHTPIN, DHTTYPE);  
Adafruit_AHTX0 aht;                 
ScioSense_ENS160 ens160(ENS160_I2CADDR_1);

bool ledState = LOW;  
bool wifiConnected = false;
bool sdCardInitialized = false;
bool ahtInitialized = false;
bool ens160Initialized = false;

// Variables pour la gestion des erreurs I2C
unsigned long lastI2CError = 0;
int i2cErrorCount = 0;

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
};

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("=== DÉMARRAGE ESP32 ===");
  
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  
  init_i2c();
  init_wifi();
  init_dht();
  init_sd_card();
  init_sensors();
  
  Serial.println("=== SETUP TERMINÉ ===");
}

void loop() {
  unsigned long startTime = millis();
  
  check_wifi_connection();
  led();

  SensorData datas;
  String fullTime = catch_time();
  datas.date = fullTime.substring(0, 10);
  datas.time = fullTime.substring(11);

  read_all_sensors(datas);
  
  if (sdCardInitialized) {
    save_on_sd_card(datas);
    clean_old_files();
  }
  
  monitor_i2c_health();
  
  unsigned long loopTime = millis() - startTime;
  Serial.printf("Temps de loop: %lu ms\n", loopTime);
  
  delay(2000 * TIME_SCALE);
}

void init_i2c() {
  Serial.println("Initialisation I2C...");
  
  Wire.end();
  delay(100);
  
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(I2C_FREQUENCY);
  
  delay(500);
  Serial.println("I2C initialisé (100kHz)");
}

void init_wifi() {
  Serial.println("Initialisation WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < MAX_WIFI_RETRIES) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println("\nWiFi connecté !");
    Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
    timeClient.begin();
  } else {
    wifiConnected = false;
    Serial.println("\nWiFi non connecté - mode offline");
  }
}

void check_wifi_connection() {
  static unsigned long lastCheck = 0;
  unsigned long now = millis();
  
  if (now - lastCheck > 30000) {
    lastCheck = now;
    
    if (WiFi.status() != WL_CONNECTED && !wifiConnected) {
      Serial.println("Tentative de reconnexion WiFi...");
      WiFi.reconnect();
      delay(1000);
      
      if (WiFi.status() == WL_CONNECTED) {
        wifiConnected = true;
        timeClient.begin();
        Serial.println("WiFi reconnecté !");
      }
    } else if (WiFi.status() != WL_CONNECTED && wifiConnected) {
      wifiConnected = false;
      Serial.println("Connexion WiFi perdue");
    }
  }
}

void init_dht() {
  Serial.println("Initialisation DHT22...");
  dht.begin();
  delay(2000);
  Serial.println("DHT22 initialisé");
}

void init_sd_card() {
  Serial.println("Initialisation carte SD...");
  if (SD.begin(SD_CS_PIN)) {
    sdCardInitialized = true;
    Serial.println("Carte SD initialisée");
  } else {
    sdCardInitialized = false;
    Serial.println("Échec initialisation carte SD");
  }
}

void init_sensors() {
  Serial.println("Initialisation capteurs I2C...");
  
  scan_i2c();
  
  if (aht.begin()) {
    ahtInitialized = true;
    Serial.println("AHT21 détecté et initialisé");
  } else {
    ahtInitialized = false;
    Serial.println("AHT21 non détecté");
  }
  
  delay(1000);
  
  if (ens160.begin() && ens160.available()) {
    ens160Initialized = true;
    Serial.println("ENS160 détecté et initialisé");
    Serial.printf("ENS160 Rev: %d.%d.%d\n", 
                  ens160.getMajorRev(), 
                  ens160.getMinorRev(), 
                  ens160.getBuild());
    
    if (ens160.setMode(ENS160_OPMODE_STD)) {
      Serial.println("ENS160 en mode standard");
    } else {
      Serial.println("Erreur mode standard ENS160");
    }
  } else {
    ens160Initialized = false;
    Serial.println("ENS160 non détecté");
  }
}

void scan_i2c() {
  Serial.println("Scan I2C...");
  int deviceCount = 0;
  
  for (byte address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    byte error = Wire.endTransmission();
    
    if (error == 0) {
      Serial.printf("Device trouvé à l'adresse 0x%02X\n", address);
      deviceCount++;
    }
    delay(10);
  }
  
  if (deviceCount == 0) {
    Serial.println("Aucun device I2C trouvé");
  } else {
    Serial.printf("%d device(s) I2C trouvé(s)\n", deviceCount);
  }
}

void read_all_sensors(SensorData &datas) {
  datas.temperatureDHT = catch_temp();
  datas.humidityDHT = catch_hum();
  
  delay(500);
  
  if (ahtInitialized) {
    sensors_event_t humEvent, tempEvent;
    if (aht.getEvent(&humEvent, &tempEvent)) {
      datas.temperatureAHT = tempEvent.temperature;
      datas.humidityAHT = humEvent.relative_humidity;
      Serial.printf("AHT21 => T=%.2f°C, H=%.1f%%\n", 
                    datas.temperatureAHT, datas.humidityAHT);
    } else {
      datas.temperatureAHT = -999.0;
      datas.humidityAHT = -999.0;
      Serial.println("Erreur lecture AHT21");
      i2cErrorCount++;
    }
  } else {
    datas.temperatureAHT = -999.0;
    datas.humidityAHT = -999.0;
  }
  
  delay(500);
  
  if (ens160Initialized) {
    if (ens160.available()) {
      ens160.measure(true);
      ens160.measureRaw(true);
      
      datas.eco2 = ens160.geteCO2();
      datas.tvoc = ens160.getTVOC();
      datas.aqi = ens160.getAQI();
      
      if (datas.eco2 == 65535 || datas.tvoc == 65535) {
        Serial.println("ENS160 données invalides");
        datas.eco2 = 0;
        datas.tvoc = 0;
        datas.aqi = 0;
        i2cErrorCount++;
      } else {
        Serial.printf("ENS160 => eCO2=%u ppm, TVOC=%u ppb, AQI=%u\n", 
                      datas.eco2, datas.tvoc, datas.aqi);
      }
    } else {
      Serial.println("ENS160 non disponible");
      datas.eco2 = 0;
      datas.tvoc = 0;
      datas.aqi = 0;
      i2cErrorCount++;
    }
  } else {
    datas.eco2 = 0;
    datas.tvoc = 0;
    datas.aqi = 0;
  }
}

void monitor_i2c_health() {
  static unsigned long lastCheck = 0;
  unsigned long now = millis();
  
  if (now - lastCheck > 60000) {
    lastCheck = now;
    
    if (i2cErrorCount > I2C_ERROR_THRESHOLD) {
      Serial.printf("Trop d'erreurs I2C (%d), reinitialisation...\n", i2cErrorCount);
      init_i2c();
      delay(1000);
      init_sensors();
      i2cErrorCount = 0;
    }
  }
}

String catch_time() {
  if (!wifiConnected) {
    return "1970-01-01 00:00:00";
  }
  
  int attempts = 0;
  while (!timeClient.update() && attempts < 3) {
    timeClient.forceUpdate();
    delay(500);
    attempts++;
  }
  
  unsigned long epochTime = timeClient.getEpochTime();
  
  if (epochTime < 100000) {
    return "1970-01-01 00:00:00";
  }

  time_t rawTime = (time_t) epochTime;
  struct tm *ptm = gmtime(&rawTime);

  char buffer[32];
  sprintf(buffer, "%04d-%02d-%02d %02d:%02d:%02d",
          (ptm->tm_year+1900),
          (ptm->tm_mon+1),
          ptm->tm_mday,
          ptm->tm_hour,
          ptm->tm_min,
          ptm->tm_sec);

  String currentTime(buffer);
  Serial.printf("Heure: %s\n", currentTime.c_str());
  return currentTime;
}

float catch_temp() {
  float temperature = dht.readTemperature();
  if (isnan(temperature)) {
    Serial.println("Échec lecture température DHT22");
    return -999.0;
  }
  Serial.printf("DHT22 => T=%.2f°C\n", temperature);
  return temperature;
}

float catch_hum() {
  float humidity = dht.readHumidity();
  if (isnan(humidity)) {
    Serial.println("Échec lecture humidité DHT22");
    return -999.0;
  }
  Serial.printf("DHT22 => H=%.1f%%\n", humidity);
  return humidity;
}

void save_on_sd_card(SensorData datas) {
  if (!sdCardInitialized) return;
  
  String filename = "/" + datas.date + ".csv";
  bool newFile = !SD.exists(filename);

  File dataFile = SD.open(filename, FILE_APPEND);
  if (dataFile) {
    if (newFile) {
      dataFile.println("Date,Heure,Température_DHT,Humidité_DHT,Température_AHT,Humidité_AHT,eCO2,TVOC,AQI");
    }
    dataFile.printf("%s,%s,%.2f,%.1f,%.2f,%.1f,%u,%u,%u\n",
                    datas.date.c_str(),
                    datas.time.c_str(),
                    datas.temperatureDHT,
                    datas.humidityDHT,
                    datas.temperatureAHT,
                    datas.humidityAHT,
                    datas.eco2,
                    datas.tvoc,
                    datas.aqi);
    
    dataFile.close();
    Serial.println("Données sauvées: " + filename);
  } else {
    Serial.println("Erreur ouverture fichier SD");
  }
}

void clean_old_files() {
  if (!wifiConnected || !sdCardInitialized) return;
  
  timeClient.update();
  unsigned long epochTime = timeClient.getEpochTime();
  unsigned long sevenDaysAgo = epochTime - (7UL * 24UL * 3600UL);

  File root = SD.open("/");
  File file = root.openNextFile();
  while (file) {
    String filename = file.name();
    if (filename.endsWith(".csv") && filename.length() == 14) {
      String dateStr = filename.substring(1, 11);
      int year = dateStr.substring(0, 4).toInt();
      int month = dateStr.substring(5, 7).toInt();
      int day = dateStr.substring(8, 10).toInt();

      struct tm fileDate = {0};
      fileDate.tm_year = year - 1900;
      fileDate.tm_mon = month - 1;
      fileDate.tm_mday = day;
      time_t fileTime = mktime(&fileDate);

      if ((unsigned long)fileTime < sevenDaysAgo) {
        Serial.println("Suppression: " + filename);
        SD.remove(filename);
      }
    }
    file = root.openNextFile();
  }
  root.close();
}

void led() {
  ledState = !ledState;
  digitalWrite(LED_PIN, ledState);
  Serial.printf("LED %s\n", ledState ? "ON" : "OFF");
}