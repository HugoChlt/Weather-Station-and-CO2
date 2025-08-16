#include <DHT.h>
#include <WiFi.h>
#include <SPI.h>
#include <SD.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

// Paramètres du Wi-Fi
const char* ssid = "Moi";          // Remplace par le nom de ton réseau Wi-Fi
const char* password = "Montlays";  // Remplace par ton mot de passe Wi-Fi

// Pin SD
#define SD_CS_PIN 5  // CS de la carte SD (tu as mentionné GPIO5)

// Pins DHT
#define DHTPIN 4        // Le pin GPIO auquel est connecté le DHT22
#define DHTTYPE DHT22   // DHT22 (AM2302)

// Pin LED
#define LED_PIN 2       // Pin de la LED (GPIO 2)

#define TIME_SCALE 1

WiFiUDP ntpUDP;                // Objet pour UDP pour NTP
NTPClient timeClient(ntpUDP, "pool.ntp.org", 7200, 60000);  // NTP avec 1 heure de décalage et mise à jour toutes les 60s

DHT dht(DHTPIN, DHTTYPE);  // Initialise le capteur DHT

bool ledState = LOW;  // État de la LED

struct SensorData {
  String currentTime;   // Heure actuelle
  float temperature;    // Température
  float humidity;       // Humidité
};

void setup() {
  // Initialisation de la communication série
  Serial.begin(115200);
  delay(1000);  // Attends un peu pour que le moniteur série soit prêt

  pinMode(LED_PIN, OUTPUT);

  connect_wifi();

  // Initialisation du client NTP pour récupérer l'heure
  timeClient.begin();

  // Initialisation du capteur DHT
  dht.begin();
  
  init_sd_card();
}

void loop() {
  // Attend quelques secondes entre les lectures
  delay(2000 * TIME_SCALE);

  led();

  SensorData datas;
  datas.currentTime = catch_time();
  datas.temperature = catch_temp();
  datas.humidity = catch_hum();

  save_on_sd_card(datas);
}

void connect_wifi(){
  // Connexion Wi-Fi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connexion au WiFi...");
  }
  Serial.println("Connecté au WiFi !");
  Serial.print("Adresse IP: ");
  Serial.println(WiFi.localIP());
}

void init_sd_card(){
  // Initialisation de la carte SD
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("Échec de l'initialisation de la carte SD !");
    return;
  }
  Serial.println("Carte SD initialisée.");
}

String catch_time(){
  // Récupérer l'heure actuelle via NTP
  timeClient.update();  // Met à jour l'heure
  String currentTime = timeClient.getFormattedTime();

  Serial.print("Heure actuelle : ");
  Serial.println(currentTime);

  return currentTime;
}

float catch_temp(){
  float temperature = dht.readTemperature();

  // Vérifie si les lectures échouent
  if (isnan(temperature)) {
    Serial.println("Échec de la lecture du capteur DHT !");
    return -1.0;
  }

  Serial.print("Température : ");
  Serial.print(temperature);
  Serial.print(" °C  ");

  return temperature;
}

float catch_hum(){
  float humidity = dht.readHumidity();

  // Vérifie si les lectures échouent
  if (isnan(humidity)) {
    Serial.println("Échec de la lecture du capteur DHT !");
    return -1.0;
  }

  Serial.print("Humidité : ");
  Serial.print(humidity);
  Serial.println(" %");

  return humidity;
}

void save_on_sd_card(SensorData datas){
  // Crée/ouvre le fichier sur la carte SD en mode ajout (append)
  File dataFile = SD.open("/donnees.txt", FILE_APPEND);
  if (dataFile) {
    // Écrit la température, l'humidité et l'heure dans le fichier
    dataFile.print("Heure: ");
    dataFile.print(datas.currentTime);
    dataFile.print(" Temp: ");
    dataFile.print(datas.temperature);
    dataFile.print(" °C Hum: ");
    dataFile.print(datas.humidity);
    dataFile.println(" %");
    dataFile.close();  // Ferme le fichier
    Serial.println("Données enregistrées sur la carte SD.");
  } else {
    Serial.println("Erreur d'ouverture du fichier.");
  }
}

// void read_sd_card(){

// }

void led(){
  // Inversion de l'état de la LED
  if (ledState == HIGH) {
    digitalWrite(LED_PIN, LOW);  // Éteindre la LED
    ledState = LOW;  // Met à jour l'état de la LED à éteint
    Serial.println("LED éteinte");
  } else {
    digitalWrite(LED_PIN, HIGH);  // Allumer la LED
    ledState = HIGH;  // Met à jour l'état de la LED à allumé
    Serial.println("LED allumée");
  }
}