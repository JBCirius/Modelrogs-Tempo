#include <Arduino.h>
#include <SPI.h>
#include <EEPROM.h>

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266HTTPUpdateServer.h>
#include <WiFiClientSecureBearSSL.h>

#include <ArduinoJson.h>
#include <time.h>
#include <DNSServer.h>

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans18pt7b.h>
#include <Fonts/FreeSans24pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>

// =====================================================
// GeekM-Tempo - TEST 12
// ESP8266 + ST7789 240x240 + Wi-Fi + NTP + RTE Tempo
// Version météo stable - mise à jour horaire, redessin icône isolé
// =====================================================

#define TFT_CS   4
#define TFT_DC   0
#define TFT_RST  2
#define TFT_BL   5

// =====================================================
// Point d'accès de configuration
// =====================================================

const char* AP_SSID = "MODELROGS Tempo";
const char* AP_PASSWORD = "";

// =====================================================
// EEPROM
// =====================================================

#define EEPROM_SIZE 512

struct Config {
  char ssid[64];
  char password[64];
  char rteClientId[64];
  char rteClientSecret[128];
  char weatherCity[64];
  uint8_t screenBrightness;
  uint8_t nightModeEnabled;
  uint8_t nightBrightness;
  uint8_t nightStartHour;
  uint8_t nightStartMinute;
  uint8_t nightEndHour;
  uint8_t nightEndMinute;
};

Config config;
void saveConfig();
ESP8266WebServer server(80);
DNSServer dnsServer;
ESP8266HTTPUpdateServer httpUpdater;
Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST);

// =====================================================
// Etats
// =====================================================

bool internetOK = false;
bool timeOK = false;
bool rteOK = false;

String tempoToday = "";
String tempoTomorrow = "";
String lastTempoDate = "";

// Dernière récupération RTE réussie.
bool rteLastUpdateOK = false;
String rteLastUpdateTime = "";

// =====================================================
// NTP
// =====================================================

const char* NTP_SERVER_1 = "pool.ntp.org";
const char* NTP_SERVER_2 = "time.nist.gov";
const char* TZ_FRANCE = "CET-1CEST,M3.5.0,M10.5.0/3";

// =====================================================
// RTE / TEMPO - TEST 15 DOUBLE SOURCE
// =====================================================
const char* RTE_TEMPO_LIGHT_URL =
  "https://www.services-rte.com/cms/open_data/v1/tempoLight";
const char* TEMPO_API_TODAY_URL =
  "https://www.api-couleur-tempo.fr/api/jourTempo/today";
const char* TEMPO_API_TOMORROW_URL =
  "https://www.api-couleur-tempo.fr/api/jourTempo/tomorrow";

bool rteTomorrowFound = false;
int lastRteAttemptMinute = -1;
String tempoTodaySource = "";
String tempoTomorrowSource = "";

// =====================================================
// METEO - Open-Meteo
// =====================================================

String weatherCity = "Manosque";
String weatherLat = "43.8356";
String weatherLon = "5.7826";

bool weatherOK = false;
int weatherCodes[5] = {-1, -1, -1, -1, -1};
float weatherTempMax[5] = {NAN, NAN, NAN, NAN, NAN};
float weatherTempMin[5] = {NAN, NAN, NAN, NAN, NAN};

// Code météo actuellement affiché à l’écran.
// Utilisé pour éviter tout redessin inutile de l’icône.
int displayedWeatherCode = -1;

unsigned long lastWeatherUpdate = 0;
unsigned long lastWeatherAttempt = 0;
int lastWeatherDay = -1;
String meteoLastUpdateTime = "";
const unsigned long WEATHER_UPDATE_INTERVAL =
  60UL * 60UL * 1000UL;

// =====================================================
// Gestion de la luminosité / Mode Nuit
// =====================================================
bool screenOn = true;
bool nightModeActive = false;
// Vrai lorsque le produit attend sa première configuration Wi-Fi.
bool wifiSetupMode = false;

// =====================================================
// Prototypes
// =====================================================

String getTimeString();
String getDateString();
bool getTempoLightValues(String &today, String &tomorrow);
bool getTempoApiValue(const char* url, String &color, String &dateValue);
bool testRTE();
bool testInternet();
bool syncTime();
bool connectWiFi();
void displayStatus();
void updateClockDisplay();
void updateTariffDisplay();
void automaticRteUpdate();
void setScreenBacklight(bool on);
void applyScreenBrightness();
bool isNightModeActive();
void automaticScreenSchedule();
void handleTempoDateChange();
void handleRoot();
void handleRTE();
void handleSave();

bool resolveWeatherCity();
bool updateWeather();
String weatherDescription(int code);
void drawWeatherIcon(int code, int cx, int cy, int size);
void drawSmallWeatherIcon(int code, int cx, int cy);
void refreshWeatherIconOnly(int code);

// =====================================================
// Utilitaires
// =====================================================

bool validTempoColor(const String& value) {
  return value == "BLUE" || value == "WHITE" || value == "RED";
}

// =====================================================
// Base64
// =====================================================

String base64Encode(String input) {

  const char* table =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

  String output = "";
  int val = 0;
  int valb = -6;

  for (unsigned char c : input) {
    val = (val << 8) + c;
    valb += 8;

    while (valb >= 0) {
      output += table[(val >> valb) & 0x3F];
      valb -= 6;
    }
  }

  if (valb > -6) {
    output += table[((val << 8) >> (valb + 8)) & 0x3F];
  }

  while (output.length() % 4) {
    output += '=';
  }

  return output;
}

// =====================================================
// Encodage URL simple
// =====================================================

String urlEncode(const String& value) {

  const char hex[] = "0123456789ABCDEF";
  String out;

  for (size_t i = 0; i < value.length(); i++) {

    unsigned char c = value[i];

    if (
      (c >= 'a' && c <= 'z') ||
      (c >= 'A' && c <= 'Z') ||
      (c >= '0' && c <= '9') ||
      c == '-' || c == '_' || c == '.' || c == '~'
    ) {
      out += char(c);
    } else if (c == ' ') {
      out += "%20";
    } else {
      out += '%';
      out += hex[(c >> 4) & 0x0F];
      out += hex[c & 0x0F];
    }
  }

  return out;
}

// =====================================================
// EEPROM
// =====================================================

void loadConfig() {

  EEPROM.begin(EEPROM_SIZE);
  EEPROM.get(0, config);

  if (config.ssid[0] == 0xFF || config.ssid[0] == '\0') {
    memset(&config, 0, sizeof(config));

    strncpy(
      config.weatherCity,
      "Manosque",
      sizeof(config.weatherCity) - 1
    );

    config.weatherCity[
      sizeof(config.weatherCity) - 1
    ] = '\0';

    weatherCity = "Manosque";
    config.screenBrightness = 100;
    config.nightModeEnabled = 1;
    config.nightBrightness = 20;
    config.nightStartHour = 22;
    config.nightStartMinute = 0;
    config.nightEndHour = 7;
    config.nightEndMinute = 0;

    saveConfig();
    Serial.println("EEPROM vierge : paramètres affichage initialisés.");
    return;
  }

  // Compatibilité avec les anciennes versions :
  // la ville météo n'existait pas encore.
  if (
    config.weatherCity[0] == 0xFF ||
    config.weatherCity[0] == '\0'
  ) {
    strncpy(
      config.weatherCity,
      "Manosque",
      sizeof(config.weatherCity) - 1
    );

    config.weatherCity[
      sizeof(config.weatherCity) - 1
    ] = '\0';
  }

  // Migration depuis TEST 11 : les nouveaux octets n'existaient pas.
  // Dans une EEPROM TEST 11, ces octets sont généralement à 0x00
  // (et non à 0xFF), car ils se trouvent après l'ancien Config.
  // Si l'ensemble des nouveaux paramètres est encore à zéro,
  // on initialise les valeurs par défaut du TEST 12.
  bool displaySettingsNeedSave = false;

  bool looksLikeOldTest11 =
    config.screenBrightness == 0 &&
    config.nightModeEnabled == 0 &&
    config.nightBrightness == 0 &&
    config.nightStartHour == 0 &&
    config.nightStartMinute == 0 &&
    config.nightEndHour == 0 &&
    config.nightEndMinute == 0;

  if (looksLikeOldTest11) {
    config.screenBrightness = 100;
    config.nightModeEnabled = 1;
    config.nightBrightness = 20;
    config.nightStartHour = 22;
    config.nightStartMinute = 0;
    config.nightEndHour = 7;
    config.nightEndMinute = 0;
    displaySettingsNeedSave = true;
  }

  if (config.screenBrightness == 0xFF || config.screenBrightness > 100) {
    config.screenBrightness = 100;
    displaySettingsNeedSave = true;
  }
  if (config.nightModeEnabled > 1) {
    config.nightModeEnabled = 1;
    displaySettingsNeedSave = true;
  }
  if (config.nightBrightness == 0xFF || config.nightBrightness > 100) {
    config.nightBrightness = 20;
    displaySettingsNeedSave = true;
  }
  if (config.nightStartHour == 0xFF || config.nightStartHour > 23) {
    config.nightStartHour = 22;
    displaySettingsNeedSave = true;
  }
  if (config.nightStartMinute == 0xFF || config.nightStartMinute > 59) {
    config.nightStartMinute = 0;
    displaySettingsNeedSave = true;
  }
  if (config.nightEndHour == 0xFF || config.nightEndHour > 23) {
    config.nightEndHour = 7;
    displaySettingsNeedSave = true;
  }
  if (config.nightEndMinute == 0xFF || config.nightEndMinute > 59) {
    config.nightEndMinute = 0;
    displaySettingsNeedSave = true;
  }

  if (displaySettingsNeedSave) {
    saveConfig();
    Serial.println("Paramètres d'affichage migrés/initialisés.");
  }

  weatherCity = String(config.weatherCity);

  if (weatherCity.length() == 0) {
    weatherCity = "Manosque";
  }

  Serial.print("Wi-Fi mémorisé : ");
  Serial.println(config.ssid);

  Serial.print("Ville météo : ");
  Serial.println(weatherCity);

  Serial.print("Luminosité normale : ");
  Serial.print(config.screenBrightness);
  Serial.println(" %");
  Serial.print("Mode Nuit : ");
  Serial.println(config.nightModeEnabled ? "ACTIVE" : "DESACTIVE");
  Serial.print("Luminosité Nuit : ");
  Serial.print(config.nightBrightness);
  Serial.println(" %");
  Serial.printf("Nuit : %02u:%02u -> %02u:%02u\n", config.nightStartHour, config.nightStartMinute, config.nightEndHour, config.nightEndMinute);

}

void saveConfig() {
  EEPROM.put(0, config);
  EEPROM.commit();
  Serial.println("Configuration sauvegardée.");
}

// =====================================================
// Wi-Fi
// =====================================================

bool connectWiFi() {

  if (config.ssid[0] == '\0') {
    Serial.println("Aucun Wi-Fi configuré.");
    return false;
  }

  Serial.print("Connexion à : ");
  Serial.println(config.ssid);

  WiFi.begin(config.ssid, config.password);

  int attempts = 0;

  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Wi-Fi CONNECTE !");
    Serial.print("IP : ");
    Serial.println(WiFi.localIP());
    return true;
  }

  Serial.println("Wi-Fi NON CONNECTE.");
  return false;
}

// =====================================================
// Internet
// =====================================================

bool testInternet() {

  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  Serial.println("Test Internet...");

  BearSSL::WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  bool result = false;

  if (http.begin(client, "https://example.com")) {
    http.setTimeout(8000);
    int code = http.GET();

    Serial.print("HTTP Internet : ");
    Serial.println(code);

    if (code > 0 && code < 400) {
      result = true;
    }

    http.end();
  }

  return result;
}

// =====================================================
// NTP
// =====================================================

bool syncTime() {

  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  Serial.println("Synchronisation NTP...");

  configTime(
    TZ_FRANCE,
    NTP_SERVER_1,
    NTP_SERVER_2
  );

  time_t now = time(nullptr);
  int attempts = 0;

  while (now < 100000 && attempts < 30) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
    attempts++;
  }

  Serial.println();

  if (now >= 100000) {
    Serial.println("NTP OK");
    return true;
  }

  Serial.println("NTP ERREUR");
  return false;
}

// =====================================================
// Heure
// =====================================================

String getTimeString() {

  time_t now = time(nullptr);
  struct tm* info = localtime(&now);

  if (info == nullptr) {
    return "--:--";
  }

  char buffer[10];
  strftime(buffer, sizeof(buffer), "%H:%M", info);
  return String(buffer);
}

// =====================================================
// Date
// =====================================================

String getDateString() {

  time_t now = time(nullptr);
  struct tm* info = localtime(&now);

  if (info == nullptr) {
    return "--/--/----";
  }

  char buffer[16];
  strftime(buffer, sizeof(buffer), "%d/%m/%Y", info);
  return String(buffer);
}

// =====================================================
// RTE : SOURCE 1 - tempoLight
// =====================================================
bool getTempoLightValues(String &today, String &tomorrow) {
  today = "";
  tomorrow = "";

  BearSSL::WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  Serial.println();
  Serial.println("===== TEST15 SOURCE 1 : RTE tempoLight =====");
  Serial.println(RTE_TEMPO_LIGHT_URL);

  if (!http.begin(client, RTE_TEMPO_LIGHT_URL)) {
    Serial.println("RTE tempoLight : begin() ECHEC");
    return false;
  }

  http.addHeader("Accept", "application/json");
  http.setTimeout(10000);
  int code = http.GET();
  Serial.print("HTTP : ");
  Serial.println(code);

  if (code <= 0) {
    Serial.print("Erreur transport : ");
    Serial.println(http.errorToString(code));
    http.end();
    return false;
  }

  String response = http.getString();
  Serial.println("Reponse brute :");
  Serial.println(response);
  http.end();

  if (code != HTTP_CODE_OK) return false;

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, response);
  if (error) {
    Serial.print("JSON erreur : ");
    Serial.println(error.c_str());
    return false;
  }

  JsonObject values = doc["values"].as<JsonObject>();
  if (values.isNull()) {
    Serial.println("RTE tempoLight : objet values absent.");
    return false;
  }

  time_t now = time(nullptr);
  struct tm day0 = *localtime(&now);
  day0.tm_hour = 0; day0.tm_min = 0; day0.tm_sec = 0;
  mktime(&day0);
  struct tm day1 = day0;
  day1.tm_mday += 1;
  mktime(&day1);

  char dateToday[11];
  char dateTomorrow[11];
  strftime(dateToday, sizeof(dateToday), "%Y-%m-%d", &day0);
  strftime(dateTomorrow, sizeof(dateTomorrow), "%Y-%m-%d", &day1);

  today = values[dateToday] | "";
  tomorrow = values[dateTomorrow] | "";

  String fallbackKey = String(dateTomorrow) + "-fallback";
  String fallback = values[fallbackKey] | "";
  if (fallback == "true") tomorrow = "";

  Serial.print("Aujourd'hui [RTE] : ");
  Serial.println(today);
  Serial.print("Demain [RTE] : ");
  Serial.println(tomorrow);
  Serial.print("Fallback demain : ");
  Serial.println(fallback);

  return validTempoColor(today) || validTempoColor(tomorrow);
}

// =====================================================
// TEMPO : SOURCE 2 - api-couleur-tempo.fr
// =====================================================
bool getTempoApiValue(const char* url, String &color, String &dateValue) {
  color = "";
  dateValue = "";

  BearSSL::WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;

  Serial.println();
  Serial.println("===== TEST15 SOURCE 2 : API Couleur Tempo =====");
  Serial.println(url);

  if (!http.begin(client, url)) {
    Serial.println("API Tempo : begin() ECHEC");
    return false;
  }

  http.addHeader("Accept", "application/json");
  http.addHeader("User-Agent", "GeekMagic-Tempo/TEST15");
  http.setTimeout(10000);
  int code = http.GET();
  Serial.print("HTTP : ");
  Serial.println(code);

  if (code <= 0) {
    Serial.print("Erreur transport : ");
    Serial.println(http.errorToString(code));
    http.end();
    return false;
  }

  String response = http.getString();
  Serial.println("Reponse brute :");
  Serial.println(response);
  http.end();

  if (code != HTTP_CODE_OK) return false;

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, response);
  if (error) {
    Serial.print("JSON erreur : ");
    Serial.println(error.c_str());
    return false;
  }

  int codeJour = doc["codeJour"] | 0;
  dateValue = doc["dateJour"] | "";

  if (codeJour == 1) color = "BLUE";
  else if (codeJour == 2) color = "WHITE";
  else if (codeJour == 3) color = "RED";

  Serial.print("Date : ");
  Serial.println(dateValue);
  Serial.print("codeJour : ");
  Serial.println(codeJour);
  Serial.print("Couleur : ");
  Serial.println(color);

  return validTempoColor(color);
}

// =====================================================
// TEST RTE COMPLET - DOUBLE SOURCE
// =====================================================
bool testRTE() {
  rteOK = false;

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("RTE : Wi-Fi absent.");
    return false;
  }

  String rteToday;
  String rteTomorrow;
  String apiToday;
  String apiTomorrow;
  String apiTodayDate;
  String apiTomorrowDate;

  bool rteResult = getTempoLightValues(rteToday, rteTomorrow);
  bool apiTodayResult = getTempoApiValue(TEMPO_API_TODAY_URL, apiToday, apiTodayDate);
  bool apiTomorrowResult = getTempoApiValue(TEMPO_API_TOMORROW_URL, apiTomorrow, apiTomorrowDate);

  Serial.println();
  Serial.println("========== RESULTAT TEST15 ==========");
  Serial.print("RTE today     : "); Serial.println(rteToday);
  Serial.print("RTE tomorrow  : "); Serial.println(rteTomorrow);
  Serial.print("API today     : "); Serial.println(apiToday);
  Serial.print("API tomorrow  : "); Serial.println(apiTomorrow);

  // Aujourd'hui : RTE prioritaire, API en secours.
  if (validTempoColor(rteToday)) {
    tempoToday = rteToday;
    tempoTodaySource = "RTE";
  } else if (validTempoColor(apiToday)) {
    tempoToday = apiToday;
    tempoTodaySource = "API";
  }

  // Demain : RTE prioritaire, API en secours.
  if (validTempoColor(rteTomorrow)) {
    tempoTomorrow = rteTomorrow;
    tempoTomorrowSource = "RTE";
  } else if (validTempoColor(apiTomorrow)) {
    tempoTomorrow = apiTomorrow;
    tempoTomorrowSource = "API";
  }

  // Contrôle de cohérence si les deux sources répondent.
  if (validTempoColor(rteToday) && validTempoColor(apiToday) && rteToday != apiToday) {
    Serial.println("ATTENTION : incoherence aujourd'hui entre RTE et API.");
  }
  if (validTempoColor(rteTomorrow) && validTempoColor(apiTomorrow) && rteTomorrow != apiTomorrow) {
    Serial.println("ATTENTION : incoherence demain entre RTE et API.");
  }

  rteOK = validTempoColor(tempoToday) || validTempoColor(tempoTomorrow);

  if (rteOK) {
    rteLastUpdateOK = true;
    rteLastUpdateTime = getTimeString();
  }

  if (validTempoColor(tempoTomorrow)) {
    rteTomorrowFound = true;
  }

  Serial.print("FINAL aujourd'hui : ");
  Serial.print(tempoToday);
  Serial.print(" [");
  Serial.print(tempoTodaySource);
  Serial.println("]");
  Serial.print("FINAL demain      : ");
  Serial.print(tempoTomorrow);
  Serial.print(" [");
  Serial.print(tempoTomorrowSource);
  Serial.println("]");
  Serial.println("=====================================");

  return rteOK;
}

// =====================================================
// Changement de jour
// =====================================================

void handleTempoDateChange() {

  if (!timeOK) {
    return;
  }

  String currentDate = getDateString();

  if (currentDate == "--/--/----") {
    return;
  }

  if (lastTempoDate == "") {
    lastTempoDate = currentDate;
    return;
  }

  if (currentDate == lastTempoDate) {
    return;
  }

  Serial.println();
  Serial.println("==============================");
  Serial.println("CHANGEMENT DE JOUR");
  Serial.print("Ancienne date : ");
  Serial.println(lastTempoDate);
  Serial.print("Nouvelle date : ");
  Serial.println(currentDate);
  Serial.println("==============================");

  // La couleur de demain devient celle d'aujourd'hui.
  if (validTempoColor(tempoTomorrow)) {
    tempoToday = tempoTomorrow;
  }

  tempoTomorrow = "";
  rteTomorrowFound = false;
  lastRteAttemptMinute = -1;
  rteOK = false;

  // La nouvelle journée n'a pas encore eu de MAJ RTE.
  rteLastUpdateOK = false;
  rteLastUpdateTime = "";

  lastTempoDate = currentDate;

  // Rafraîchissement immédiat à 00:00/00:01 :
  // la couleur de demain devient celle d'aujourd'hui et
  // la bulle de demain repasse en gris tant qu'aucune nouvelle
  // couleur Tempo n'a été validée.
  // Aucun accès réseau supplémentaire ici.
  displayStatus();
}

// =====================================================
// Mode Nuit / luminosité automatique
// =====================================================

bool isNightModeActive() {

  if (!timeOK || !config.nightModeEnabled) {
    return false;
  }

  int start = config.nightStartHour * 60 + config.nightStartMinute;
  int end = config.nightEndHour * 60 + config.nightEndMinute;

  if (start == end) {
    return false;
  }

  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);
  if (timeinfo == nullptr) {
    return false;
  }

  int current = timeinfo->tm_hour * 60 + timeinfo->tm_min;

  if (start < end) {
    return current >= start && current < end;
  }

  // Période traversant minuit.
  return current >= start || current < end;
}

void applyScreenBrightness() {

  uint8_t percent = isNightModeActive()
    ? config.nightBrightness
    : config.screenBrightness;

  if (percent > 100) percent = 100;

  // Rétroéclairage actif LOW : 100 % = 0, 0 % = 1023.
  int pwm = 1023 - ((int)percent * 1023 / 100);
  analogWrite(TFT_BL, pwm);
  screenOn = percent > 0;
  nightModeActive = isNightModeActive();

  Serial.print("Affichage : ");
  Serial.print(nightModeActive ? "MODE NUIT" : "NORMAL");
  Serial.print(" - ");
  Serial.print(percent);
  Serial.println(" %");
}

void setScreenBacklight(bool on) {
  if (on) {
    applyScreenBrightness();
  } else {
    analogWrite(TFT_BL, 1023);
    screenOn = false;
  }
}

void automaticScreenSchedule() {

  if (!timeOK) return;

  bool active = isNightModeActive();

  if (active != nightModeActive) {
    nightModeActive = active;
    applyScreenBrightness();
  }
}

void automaticRteUpdate() {
  if (WiFi.status() != WL_CONNECTED || !timeOK) return;

  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);
  if (timeinfo == nullptr) return;

  int hour = timeinfo->tm_hour;
  int minute = timeinfo->tm_min;
  int totalMinutes = hour * 60 + minute;

  if (totalMinutes < 390 || totalMinutes > 690) return; // 06:30 -> 11:30

  if (validTempoColor(tempoTomorrow)) {
    rteTomorrowFound = true;
    return;
  }

  int currentSlot = 390 + ((totalMinutes - 390) / 30) * 30;
  if (currentSlot != totalMinutes) return;
  if (currentSlot == lastRteAttemptMinute) return;

  lastRteAttemptMinute = currentSlot;

  Serial.println();
  Serial.println("==============================");
  Serial.println("RTE AUTOMATIQUE");
  Serial.print("Heure : ");
  Serial.println(getTimeString());
  Serial.println("==============================");

  testRTE();
  displayStatus();
}

// =====================================================
// Page Web
// =====================================================

void handleRoot() {

  String html;

  html += "<!DOCTYPE html>";
  html += "<html lang='fr'>";
  html += "<head>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>MODELROGS TEMPO</title>";

  html += "<style>";
  html += "body{font-family:Arial;background:#111;color:white;padding:20px;}";
  html += ".box{max-width:450px;margin:auto;background:#222;padding:25px;border-radius:15px;}";
  html += "input{box-sizing:border-box;width:100%;padding:12px;margin:7px 0 15px;border-radius:8px;border:0;font-size:16px;}";
  html += "button{width:100%;padding:14px;border:0;border-radius:8px;font-size:16px;margin-top:10px;}";
  html += ".ok{color:#55dd55}.bad{color:#ff5555}";
  html += ".info{background:#333;padding:15px;border-radius:10px;margin-bottom:20px;}";
  html += ".section{margin-top:25px;padding-top:15px;border-top:1px solid #555;}";
  html += "</style></head><body>";

  html += "<div class='box'>";
  html += "<h1>MODELROGS TEMPO</h1>";

  html += "<div class='info'>";

  html += "<b>Wi-Fi :</b> ";
  if (WiFi.status() == WL_CONNECTED) {
    html += "<span class='ok'>● CONNECTÉ</span>";
    html += "<br><br><b>SSID :</b><br>";
    html += WiFi.SSID();
    html += "<br><br><b>IP :</b><br>";
    html += WiFi.localIP().toString();
  } else {
    html += "<span class='bad'>● NON CONNECTÉ</span>";
  }

  html += "<br><br><b>Internet :</b> ";
  if (internetOK) {
    html += "<span class='ok'>● OK</span>";
  } else {
    html += "<span class='bad'>● ERREUR</span>";
  }

  html += "<br><br><b>Heure :</b> ";
  if (timeOK) {
    html += "<span class='ok'>" + getTimeString() + "</span>";
  } else {
    html += "<span class='bad'>NON SYNCHRONISÉE</span>";
  }

  html += "<br><br><b>Tempo aujourd'hui :</b> ";
  if (validTempoColor(tempoToday)) {
    html += "<span class='ok'>" + tempoToday + "</span>";
  } else {
    html += "<span class='bad'>--</span>";
  }

  html += "<br><b>Tempo demain :</b> ";
  if (validTempoColor(tempoTomorrow)) {
    html += "<span class='ok'>" + tempoTomorrow + "</span>";
  } else {
    html += "<span class='bad'>--</span>";
  }

  html += "<br><br><b>Dernière MAJ RTE :</b> ";
  if (rteLastUpdateOK) {
    html += "<span class='ok'>" + rteLastUpdateTime + "</span>";
  } else {
    html += "<span class='bad'>OFF</span>";
  }

  html += "<br><br><b>Affichage :</b> ";
  html += String(config.screenBrightness) + "% normal";
  html += "<br><b>Mode Nuit :</b> ";
  if (config.nightModeEnabled) {
    html += "<span class='ok'>" + String(config.nightBrightness) + "% de ";
    if (config.nightStartHour < 10) html += "0";
    html += String(config.nightStartHour) + ":";
    if (config.nightStartMinute < 10) html += "0";
    html += String(config.nightStartMinute) + " → ";
    if (config.nightEndHour < 10) html += "0";
    html += String(config.nightEndHour) + ":";
    if (config.nightEndMinute < 10) html += "0";
    html += String(config.nightEndMinute) + "</span>";
  } else {
    html += "<span>DESACTIVE</span>";
  }

  html += "<br><br><b>Configuration :</b><br>192.168.4.1";
  html += "</div>";

  html += "<div class='section'>";
  html += "<h2>Configuration</h2>";
  html += "<form method='POST' action='/save'>";

  html += "<h3>Wi-Fi</h3>";
  html += "<label>SSID</label>";
  html += "<input name='ssid' value='" + String(config.ssid) + "' required>";
  html += "<label>Mot de passe Wi-Fi</label>";
  html += "<input name='password' type='password' value='" + String(config.password) + "' autocomplete='off'>";
  html += "<p style='font-size:13px;color:#aaa;'>SSID et mot de passe sont lus depuis l'EEPROM.</p>";
  html += "<h3>Météo</h3>";
  html += "<label>Ville</label>";
  html += "<input name='city' value='" + weatherCity + "' placeholder='Manosque' required>";

  html += "<h3>Affichage écran</h3>";
  html += "<label>Luminosité normale : <strong id='brightnessValue'>" + String(config.screenBrightness) + "%</strong></label>";
  html += "<input type='range' name='brightness' min='0' max='100' value='" + String(config.screenBrightness) + "' oninput='document.getElementById(\"brightnessValue\").innerText=this.value+\"%\"'>";

  html += "<label style='display:block;margin-top:10px;'>";
  html += "<input type='checkbox' name='nightmode' value='1' style='width:auto;margin-right:8px;'";
  if (config.nightModeEnabled) html += " checked";
  html += "> Activer le Mode Nuit</label>";

  html += "<label>Luminosité nuit : <strong id='nightBrightnessValue'>" + String(config.nightBrightness) + "%</strong></label>";
  html += "<input type='range' name='nightbrightness' min='0' max='100' value='" + String(config.nightBrightness) + "' oninput='document.getElementById(\"nightBrightnessValue\").innerText=this.value+\"%\"'>";

  char startTime[6];
  char endTime[6];
  snprintf(startTime, sizeof(startTime), "%02u:%02u", (unsigned)config.nightStartHour, (unsigned)config.nightStartMinute);
  snprintf(endTime, sizeof(endTime), "%02u:%02u", (unsigned)config.nightEndHour, (unsigned)config.nightEndMinute);

  html += "<label>Début du Mode Nuit</label>";
  html += "<input type='time' name='nightstart' value='" + String(startTime) + "'>";
  html += "<label>Fin du Mode Nuit</label>";
  html += "<input type='time' name='nightend' value='" + String(endTime) + "'>";
  html += "<p style='font-size:13px;color:#aaa;'>0 % = rétroéclairage éteint. La période peut traverser minuit. Les réglages d’affichage sont appliqués immédiatement, sans redémarrage. Un redémarrage est effectué uniquement si les identifiants Wi-Fi sont modifiés.</p>";

  html += "<button type='submit'>ENREGISTRER LES PARAMÈTRES</button>";
  html += "</form></div>";

  // -----------------------------------------------------
  // Mise à jour firmware
  // -----------------------------------------------------
  html += "<div class='section'>";
  html += "<h2>Firmware</h2>";
  html += "<form method='GET' action='/update'>";
  html += "<button type='submit'>MISE À JOUR FIRMWARE</button>";
  html += "</form>";
  html += "</div></div>";

  html += "</body></html>";

  server.send(200, "text/html", html);
}

// =====================================================
// Test RTE via Web
// =====================================================

void handleRTE() {

  testRTE();

  String html;
  html += "<!DOCTYPE html><html lang='fr'><head><meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "</head><body style='font-family:Arial;text-align:center;padding:40px'>";

  if (rteOK) {
    html += "<h1 style='color:green'>RTE OK</h1>";
    html += "<h2>" + tempoToday + "</h2>";
    html += "<p>Une couleur Tempo a été reçue.</p>";
    html += "<p>Dernière MAJ : " + rteLastUpdateTime + "</p>";
  } else {
    html += "<h1 style='color:red'>RTE ERREUR</h1>";
    html += "<p>Impossible de récupérer les données Tempo.</p>";
  }

  html += "<br><a href='/'>Retour</a>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

// =====================================================
// Sauvegarde Web
// =====================================================

void handleSave() {

  String ssid = server.arg("ssid");
  String password = server.arg("password");
  String brightnessArg = server.arg("brightness");
  String nightBrightnessArg = server.arg("nightbrightness");
  String nightModeArg = server.arg("nightmode");
  String nightStartArg = server.arg("nightstart");
  String nightEndArg = server.arg("nightend");

  if (ssid.length() == 0) {
    server.send(400, "text/html", "<h2>SSID manquant</h2>");
    return;
  }

  // Seuls les changements d'identifiants Wi-Fi nécessitent un reboot.
  bool wifiChanged = ssid != String(config.ssid);
  if (password.length() > 0 && password != String(config.password)) {
    wifiChanged = true;
  }

  memset(config.ssid, 0, sizeof(config.ssid));
  ssid.toCharArray(config.ssid, sizeof(config.ssid));

  // Mot de passe Wi-Fi vide = conserver l'ancien.
  if (password.length() > 0) {
    memset(config.password, 0, sizeof(config.password));
    password.toCharArray(config.password, sizeof(config.password));
    Serial.println("Mot de passe Wi-Fi modifié.");
  } else {
    Serial.println("Mot de passe Wi-Fi conservé.");
  }

  if (brightnessArg.length() > 0) {
    int value = brightnessArg.toInt();
    config.screenBrightness = constrain(value, 0, 100);
  }

  config.nightModeEnabled = nightModeArg == "1" ? 1 : 0;

  if (nightBrightnessArg.length() > 0) {
    int value = nightBrightnessArg.toInt();
    config.nightBrightness = constrain(value, 0, 100);
  }

  auto parseTimeField = [](const String& value, uint8_t& hour, uint8_t& minute) -> bool {
    if (value.length() != 5 || value.charAt(2) != ':') return false;
    int h = value.substring(0, 2).toInt();
    int m = value.substring(3, 5).toInt();
    if (h < 0 || h > 23 || m < 0 || m > 59) return false;
    hour = (uint8_t)h;
    minute = (uint8_t)m;
    return true;
  };

  parseTimeField(nightStartArg, config.nightStartHour, config.nightStartMinute);
  parseTimeField(nightEndArg, config.nightEndHour, config.nightEndMinute);

  Serial.printf("Affichage sauvegardé : normal %u%%, nuit %s %u%%, %02u:%02u -> %02u:%02u\n",
    config.screenBrightness,
    config.nightModeEnabled ? "ON" : "OFF",
    config.nightBrightness,
    config.nightStartHour, config.nightStartMinute,
    config.nightEndHour, config.nightEndMinute);

  String city = server.arg("city");
  city.trim();

  if (city.length() > 0) {
    memset(config.weatherCity, 0, sizeof(config.weatherCity));
    city.toCharArray(config.weatherCity, sizeof(config.weatherCity));
    weatherCity = String(config.weatherCity);
    Serial.print("Ville météo enregistrée : ");
    Serial.println(weatherCity);
  } else {
    strncpy(config.weatherCity, "Manosque", sizeof(config.weatherCity) - 1);
    config.weatherCity[sizeof(config.weatherCity) - 1] = '\\0';
    weatherCity = "Manosque";
  }

  saveConfig();

  // Appliquer immédiatement la nouvelle luminosité / programmation Jour-Nuit.
  nightModeActive = isNightModeActive();
  applyScreenBrightness();

  if (wifiChanged) {
    server.send(
      200,
      "text/html",
      "<html><head><meta charset='UTF-8'></head>"
      "<body style='font-family:Arial;text-align:center;padding:40px'>"
      "<h2>Configuration enregistrée</h2>"
      "<p>Les identifiants Wi-Fi ont changé.</p>"
      "<p>Le Modelrogs Tempo redémarre pour rejoindre le nouveau réseau...</p>"
      "</body></html>"
    );
    delay(1200);
    ESP.restart();
    return;
  }

  server.send(
    200,
    "text/html",
    "<html><head><meta charset='UTF-8'></head>"
    "<body style='font-family:Arial;text-align:center;padding:40px'>"
    "<h2>Configuration enregistrée</h2>"
    "<p>Les paramètres d'affichage sont appliqués immédiatement.</p>"
    "<p>Aucun redémarrage nécessaire.</p>"
    "<p><a href='/'>Retour aux paramètres</a></p>"
    "</body></html>"
  );
}

// =====================================================
// Affichage principal
// =====================================================

uint16_t colorTodayBackground() {

  if (tempoToday == "BLUE") {
    return 0x014F;       // bleu moins sombre
  }

  if (tempoToday == "RED") {
    return 0xA800;       // rouge moins sombre
  }

  if (tempoToday == "WHITE") {
    return 0xFFFF;       // blanc
  }

  return 0x4208;         // gris foncé si aucune info
}

uint16_t colorTomorrowBackground() {

  if (tempoTomorrow == "BLUE") {
    return 0x02BF;
  }

  if (tempoTomorrow == "RED") {
    return 0xD000;
  }

  if (tempoTomorrow == "WHITE") {
    return 0xFFFF;
  }

  return 0x7BEF;         // neutre si --
}

uint16_t colorTomorrowText() {
  return tempoTomorrow == "WHITE" ? ST77XX_BLACK : ST77XX_WHITE;
}

uint16_t colorTodayText() {
  return tempoToday == "WHITE" ? ST77XX_BLACK : ST77XX_WHITE;
}

void drawCenteredText(
  const String& text,
  int baselineY
) {

  int16_t x1, y1;
  uint16_t w, h;

  tft.getTextBounds(
    text,
    0,
    baselineY,
    &x1,
    &y1,
    &w,
    &h
  );

  int x = (240 - (int)w) / 2 - x1;
  tft.setCursor(x, baselineY);
  tft.print(text);
}


String weatherDescription(int code) {

  if (code == 0) return "BEAU TEMPS";
  if (code == 1) return "PEU NUAGEUX";
  if (code == 2) return "PART. NUAGEUX";
  if (code == 3) return "NUAGEUX";

  if (code >= 45 && code <= 48) return "BROUILLARD";

  if (code >= 51 && code <= 57) return "BRUINE";
  if (code >= 61 && code <= 67) return "PLUIE";

  if (code >= 71 && code <= 77) return "NEIGE";
  if (code >= 80 && code <= 82) return "AVERSES";
  if (code == 85 || code == 86) return "AVERSES NEIGE";

  if (code >= 95) return "ORAGE";

  return "METEO";
}

// -----------------------------------------------------
// Icônes météo simples dessinées directement sur le TFT
// -----------------------------------------------------

void drawWeatherIcon(int code, int cx, int cy, int size) {

  // ===================================================
  // TEST 11 - Nouvelles icônes météo
  // Style coloré, plein et légèrement "relief",
  // optimisé pour le petit écran 240x240.
  // ===================================================

  const uint16_t SUN      = 0xFBE0; // jaune chaud
  const uint16_t SUN_HI   = 0xFFE7; // reflet soleil
  const uint16_t CLOUD    = 0xE73C; // bleu-gris clair
  const uint16_t CLOUD_HI = 0xF7BE; // lumière du nuage
  const uint16_t SHADOW   = 0x9CF3; // ombre bleutée
  const uint16_t RAIN     = 0x45BF; // bleu cyan
  const uint16_t RAIN_HI  = 0x7DFF; // reflet gouttes
  const uint16_t SNOW     = 0xEFFF; // bleu/blanc
  const uint16_t SNOW_HI  = 0xFFFF; // cœur blanc
  const uint16_t STORM    = 0xFD20; // orange
  const uint16_t STORM_HI = 0xFFE0; // jaune
  const uint16_t FOG      = 0xBDF7; // brume

  if (code < 0) {
    // Icône inconnue : petit symbole neutre.
    tft.drawCircle(cx, cy, size / 3, ST77XX_WHITE);
    tft.drawLine(cx - size / 4, cy - size / 4,
                 cx + size / 4, cy + size / 4, ST77XX_WHITE);
    tft.drawLine(cx + size / 4, cy - size / 4,
                 cx - size / 4, cy + size / 4, ST77XX_WHITE);
    return;
  }

  bool clearSky = code == 0;
  // Codes 1, 2 et 3 utilisent tous l’icône soleil + nuage.
  bool partlyCloudy = code >= 1 && code <= 3;
  bool cloudy = false; // suppression de l’icône nuage seul
  bool fog = code >= 45 && code <= 48;
  bool drizzle = code >= 51 && code <= 57;
  bool rain = (code >= 61 && code <= 67) ||
              (code >= 80 && code <= 82);
  bool snow = (code >= 71 && code <= 77) ||
              code == 85 || code == 86;
  bool storm = code >= 95 && code <= 99;

  // ---------------------------------------------------
  // Soleil : disque lumineux + reflet + rayons courts.
  // ---------------------------------------------------
  if (clearSky || partlyCloudy) {

    int sunX = partlyCloudy ? cx + size / 5 : cx;
    int sunY = partlyCloudy ? cy - size / 6 : cy - size / 10;
    // Soleil légèrement agrandi pour avoir un poids visuel comparable
    // aux autres icônes de même taille.
    int r = max(5, size / 4);

    // Rayons en premier pour passer naturellement derrière le disque.
    for (int i = 0; i < 8; i++) {
      float a = i * 0.785398f;
      int x1 = sunX + cos(a) * (r + 4);
      int y1 = sunY + sin(a) * (r + 4);
      int x2 = sunX + cos(a) * (r + 9);
      int y2 = sunY + sin(a) * (r + 9);
      tft.drawLine(x1, y1, x2, y2, SUN);
    }

    tft.fillCircle(sunX + 2, sunY + 2, r + 1, 0xC5C0); // ombre discrète
    tft.fillCircle(sunX, sunY, r, SUN);
    tft.fillCircle(sunX - r / 3, sunY - r / 3, max(1, r / 4), SUN_HI);
  }

  // ---------------------------------------------------
  // Nuage : ombre + corps + volumes supérieurs.
  // ---------------------------------------------------
  if (partlyCloudy || cloudy || fog || drizzle || rain || snow || storm) {

    int cloudY = cy + size / 9;
    int bodyW = max(26, size * 2 / 3);
    int bodyH = max(8, size / 5);
    int left = cx - bodyW / 2;
    int top = cloudY;

    // Ombre basse décalée de 2 pixels.
    tft.fillRoundRect(left, top + 3, bodyW, bodyH,
                      max(4, bodyH / 2), SHADOW);
    tft.fillCircle(cx - size / 5, top + 2,
                   max(5, size / 6), SHADOW);
    tft.fillCircle(cx + size / 10, top - size / 8 + 2,
                   max(6, size / 5), SHADOW);

    // Corps principal.
    tft.fillRoundRect(left, top, bodyW, bodyH,
                      max(4, bodyH / 2), CLOUD);
    tft.fillCircle(cx - size / 5, top,
                   max(5, size / 6), CLOUD);
    tft.fillCircle(cx + size / 10, top - size / 8,
                   max(6, size / 5), CLOUD);
    tft.fillCircle(cx + size / 3, top + 1,
                   max(4, size / 7), CLOUD);

    // Zone claire donnant le relief.
    tft.fillCircle(cx + size / 10 - 2,
                   top - size / 8 - 2,
                   max(3, size / 8), CLOUD_HI);
    tft.fillRoundRect(left + bodyW / 5, top + 2,
                      max(8, bodyW / 3), max(2, bodyH / 4),
                      2, CLOUD_HI);
  }

  // ---------------------------------------------------
  // Brouillard : même nuage que les autres conditions,
  // avec trois lignes horizontales de brouillard dessous.
  // ---------------------------------------------------
  if (fog) {
    int lineY = cy + size / 3;
    int lineW = size * 2 / 3;

    tft.fillRoundRect(cx - lineW / 2, lineY,
                      lineW, 3, 2, FOG);
    tft.fillRoundRect(cx - lineW / 2 + 5, lineY + 7,
                      lineW - 10, 3, 2, FOG);
    tft.fillRoundRect(cx - lineW / 2 + 10, lineY + 14,
                      lineW - 20, 3, 2, FOG);
  }

  // ---------------------------------------------------
  // Bruine / pluie : gouttes colorées avec petit reflet.
  // ---------------------------------------------------
  if (drizzle || rain || storm) {

    int baseY = cy + size / 2;
    int spacing = max(8, size / 5);

    if (drizzle) {
      // Bruine : une seule goutte, plus lisible sur le petit écran.
      int x = cx;
      int y = baseY + 1;
      int rr = 2;

      tft.fillTriangle(x, y + 7,
                       x - rr, y + 1,
                       x + rr, y + 1, RAIN);
      tft.fillCircle(x, y + 3, rr, RAIN);
    } else {
      // Pluie / averses / orage : trois gouttes.
      for (int i = -1; i <= 1; i++) {
        int x = cx + i * spacing;
        int y = baseY + ((i == 0) ? 1 : 0);
        int rr = 3;

        tft.fillTriangle(x, y + 7,
                         x - rr, y + 1,
                         x + rr, y + 1, RAIN);
        tft.fillCircle(x, y + 3, rr, RAIN);
        tft.fillCircle(x - 1, y + 2, 1, RAIN_HI);
      }
    }
  }

  // ---------------------------------------------------
  // Neige : flocons épais avec petit centre lumineux.
  // ---------------------------------------------------
  if (snow) {

    int baseY = cy + size / 2;
    int spacing = max(9, size / 5);

    for (int i = -1; i <= 1; i++) {
      int x = cx + i * spacing;
      int y = baseY + ((i == 0) ? 1 : 0);
      int arm = max(3, size / 11);

      tft.drawLine(x - arm, y, x + arm, y, SNOW);
      tft.drawLine(x, y - arm, x, y + arm, SNOW);
      tft.drawLine(x - arm + 1, y - arm + 1,
                   x + arm - 1, y + arm - 1, SNOW);
      tft.drawLine(x + arm - 1, y - arm + 1,
                   x - arm + 1, y + arm - 1, SNOW);
      tft.fillCircle(x, y, 1, SNOW_HI);
    }
  }

  // ---------------------------------------------------
  // Orage : éclair jaune avec ombre orange.
  // ---------------------------------------------------
  if (storm) {

    int x = cx + size / 8;
    int y = cy + size / 3;

    // Ombre orange légèrement décalée.
    tft.fillTriangle(x + 2, y + 1,
                     x - size / 9 + 2, y + size / 5 + 1,
                     x + size / 16 + 2, y + size / 5 + 1,
                     STORM);
    tft.fillTriangle(x + size / 16 + 2, y + size / 5 + 1,
                     x - size / 16 + 2, y + size / 2 + 1,
                     x + size / 16 + 2, y + size / 3 + 1,
                     STORM);

    // Face jaune.
    tft.fillTriangle(x, y,
                     x - size / 9, y + size / 5,
                     x + size / 16, y + size / 5,
                     STORM_HI);
    tft.fillTriangle(x + size / 16, y + size / 5,
                     x - size / 16, y + size / 2,
                     x + size / 16, y + size / 3,
                     STORM_HI);
  }
}


void refreshWeatherIconOnly(int code) {

  // La bulle météo est déjà dessinée.
  // On efface uniquement la zone occupée par l’icône.
  const uint16_t pillColor = colorTomorrowBackground();

  // Zone volontairement un peu plus large que l’icône 60 px,
  // y compris lorsque certaines icônes sont remontées de 15 px.
  tft.fillRect(75, 75, 76, 90, pillColor);

  // Pause volontaire : permet de valider séparément l’effacement
  // et le redessin de l’icône, sans refaire tout l’écran.
  delay(2000);

  int iconX = 113;
  int iconY = 122;

  if (code == 45 ||
      code == 51 ||
      code == 61 ||
      code == 71 ||
      code == 80 ||
      code == 85 ||
      code == 95) {
    iconY -= 15;
  }

  drawWeatherIcon(code, iconX, iconY, 60);
  displayedWeatherCode = code;
}

void drawSmallWeatherIcon(
  int code,
  int cx,
  int cy
) {
  drawWeatherIcon(
    code,
    cx,
    cy,
    42
  );
}

// -----------------------------------------------------
// Géocodage de la ville
// -----------------------------------------------------

bool resolveWeatherCity() {

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Météo : Wi-Fi non connecté.");
    return false;
  }

  if (weatherCity.length() == 0) {
    weatherCity = "Manosque";
  }

  Serial.print("Météo : recherche de la ville : ");
  Serial.println(weatherCity);

  BearSSL::WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;

  String url =
    "https://geocoding-api.open-meteo.com/v1/search"
    "?name=" + urlEncode(weatherCity) +
    "&count=1"
    "&language=fr"
    "&format=json";

  Serial.print("Météo géocodage URL : ");
  Serial.println(url);

  if (!http.begin(client, url)) {
    Serial.println("Météo : impossible d'ouvrir le géocodage.");
    return false;
  }

  http.useHTTP10(true);
  http.addHeader("Accept-Encoding", "identity");
  http.addHeader("User-Agent", "GeekM-Tempo/1.0");
  http.setTimeout(10000);

  int httpCode = http.GET();

  Serial.print("Météo géocodage HTTP : ");
  Serial.println(httpCode);

  if (httpCode != HTTP_CODE_OK) {
    Serial.println("Réponse géocodage :");
    Serial.println(http.getString());
    http.end();
    return false;
  }

  int contentLength = http.getSize();

  Serial.print("Météo géocodage taille annoncée : ");
  Serial.println(contentLength);

  // Réponse courte et filtrée : on ne conserve que latitude/longitude.
  JsonDocument filter;
  filter["results"][0]["latitude"] = true;
  filter["results"][0]["longitude"] = true;

  JsonDocument doc;

  WiFiClient* stream = http.getStreamPtr();

  if (stream == nullptr) {
    Serial.println("Météo : flux géocodage indisponible.");
    http.end();
    return false;
  }

  DeserializationError parseError =
    deserializeJson(
      doc,
      *stream,
      DeserializationOption::Filter(filter)
    );

  http.end();

  if (parseError) {
    Serial.print("Météo géocodage JSON erreur : ");
    Serial.println(parseError.c_str());
    return false;
  }

  Serial.println("Météo géocodage JSON : OK.");

  JsonArray results =
    doc["results"].as<JsonArray>();

  if (results.isNull() || results.size() == 0) {
    Serial.println("Météo : ville introuvable.");
    return false;
  }

  JsonObject result =
    results[0].as<JsonObject>();

  double lat =
    result["latitude"] | 0.0;

  double lon =
    result["longitude"] | 0.0;

  if (
    !isfinite(lat) ||
    !isfinite(lon) ||
    (lat == 0.0 && lon == 0.0)
  ) {
    Serial.println("Météo : coordonnées invalides.");
    return false;
  }

  weatherLat = String(lat, 6);
  weatherLon = String(lon, 6);

  Serial.print("Météo : latitude = ");
  Serial.println(weatherLat);

  Serial.print("Météo : longitude = ");
  Serial.println(weatherLon);

  return true;
}

// -----------------------------------------------------
// Récupération Open-Meteo
// -----------------------------------------------------

bool updateWeather() {

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Météo : Wi-Fi non connecté.");
    return false;
  }

  if (
    weatherLat.length() == 0 ||
    weatherLon.length() == 0
  ) {
    Serial.println("Météo : coordonnées absentes.");
    return false;
  }

  Serial.println("Météo : récupération Open-Meteo...");

  BearSSL::WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;

  String url =
    "https://api.open-meteo.com/v1/forecast"
    "?latitude=" + weatherLat +
    "&longitude=" + weatherLon +
    "&daily=weather_code,temperature_2m_max,temperature_2m_min"
    "&timezone=Europe%2FParis"
    "&forecast_days=5";

  Serial.print("Météo URL : ");
  Serial.println(url);

  if (!http.begin(client, url)) {
    Serial.println("Météo : impossible d'ouvrir Open-Meteo.");
    return false;
  }

  http.useHTTP10(true);
  http.addHeader("Accept-Encoding", "identity");
  http.addHeader("User-Agent", "GeekM-Tempo/1.0");
  http.setTimeout(12000);

  int httpCode = http.GET();

  Serial.print("Météo HTTP : ");
  Serial.println(httpCode);

  if (httpCode != HTTP_CODE_OK) {
    Serial.println("Réponse Open-Meteo :");
    Serial.println(http.getString());
    http.end();
    return false;
  }

  int contentLength = http.getSize();

  Serial.print("Météo taille annoncée : ");
  Serial.println(contentLength);

  JsonDocument filter;
  filter["daily"]["weather_code"] = true;
  filter["daily"]["temperature_2m_max"] = true;
  filter["daily"]["temperature_2m_min"] = true;

  JsonDocument doc;

  WiFiClient* stream = http.getStreamPtr();

  if (stream == nullptr) {
    Serial.println("Météo : flux Open-Meteo indisponible.");
    http.end();
    return false;
  }

  DeserializationError parseError =
    deserializeJson(
      doc,
      *stream,
      DeserializationOption::Filter(filter)
    );

  http.end();

  if (parseError) {
    Serial.print("Météo JSON erreur : ");
    Serial.println(parseError.c_str());
    return false;
  }

  Serial.println("Météo JSON : OK.");

  JsonArray codes =
    doc["daily"]["weather_code"].as<JsonArray>();

  JsonArray tempMax =
    doc["daily"]["temperature_2m_max"].as<JsonArray>();

  JsonArray tempMin =
    doc["daily"]["temperature_2m_min"].as<JsonArray>();

  if (
    codes.isNull() ||
    tempMax.isNull() ||
    tempMin.isNull()
  ) {
    Serial.println("Météo : données daily incomplètes.");
    return false;
  }

  int count = 0;

  for (JsonVariant value : codes) {

    if (count >= 5) {
      break;
    }

    weatherCodes[count] = value.as<int>();
    weatherTempMax[count] = tempMax[count].as<float>();
    weatherTempMin[count] = tempMin[count].as<float>();

    Serial.print("Météo J+");
    Serial.print(count);
    Serial.print(" code = ");
    Serial.println(weatherCodes[count]);

    Serial.print("  MAX : ");
    Serial.println(weatherTempMax[count], 1);

    Serial.print("  MIN : ");
    Serial.println(weatherTempMin[count], 1);

    count++;
  }

  if (count < 5) {
    Serial.print("Météo : seulement ");
    Serial.print(count);
    Serial.println(" jours reçus.");
    return false;
  }

  weatherOK = true;
  lastWeatherUpdate = millis();
  meteoLastUpdateTime = getTimeString();

  Serial.print("Météo : dernière MAJ réussie à ");
  Serial.println(meteoLastUpdateTime);

  Serial.println("Météo : données valides.");

  return true;
}

// =====================================================
// Rétroéclairage / luminosité
// =====================================================

// =====================================================
// Symbole EURO dessine - aucune police Unicode ajoutee
// x,y = coin superieur gauche du symbole
// =====================================================
void drawEuroSymbol(int16_t x, int16_t y, uint16_t color, uint8_t scale = 1) {
  if (scale < 1) scale = 1;

  // Forme simplifiee mais lisible du symbole €.
  tft.drawLine(x + 13 * scale, y + 2 * scale,  x + 9 * scale, y + 2 * scale,  color);
  tft.drawLine(x + 9 * scale,  y + 2 * scale,  x + 5 * scale, y + 5 * scale,  color);
  tft.drawLine(x + 5 * scale,  y + 5 * scale,  x + 3 * scale, y + 9 * scale,  color);
  tft.drawLine(x + 3 * scale,  y + 9 * scale,  x + 5 * scale, y + 14 * scale, color);
  tft.drawLine(x + 5 * scale,  y + 14 * scale, x + 9 * scale, y + 17 * scale, color);
  tft.drawLine(x + 9 * scale,  y + 17 * scale, x + 13 * scale, y + 17 * scale, color);

  // Deux barres horizontales du symbole euro.
  tft.drawFastHLine(x + 1 * scale, y + 7 * scale, 11 * scale, scale);
  tft.drawFastHLine(x + 1 * scale, y + 11 * scale, 10 * scale, scale);
}

String tempoHpPrice(const String& color) {
  // Prix HP du kWh Tempo au 1er août 2026.
  // Affichage volontairement limité à 2 décimales.
  if (color == "BLUE")  return "0,17 E/kWh";
  if (color == "WHITE") return "0,19 E/kWh";
  if (color == "RED")   return "0,73 E/kWh";
  return "--";
}

String tempoHcPrice(const String& color) {
  // Prix HC du kWh Tempo au 1er août 2026.
  // Affichage volontairement limité à 2 décimales.
  if (color == "BLUE")  return "0,14 E/kWh";
  if (color == "WHITE") return "0,15 E/kWh";
  if (color == "RED")   return "0,16 E/kWh";
  return "--";
}

String tempoCurrentTariff() {
  if (!validTempoColor(tempoToday)) {
    return "--";
  }

  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);

  if (timeinfo == nullptr) {
    return "--";
  }

  int totalMinutes =
    timeinfo->tm_hour * 60 +
    timeinfo->tm_min;

  // HC : 22:00 -> 07:00 inclus.
  // HP : 07:01 -> 21:59.
  bool heuresCreuses =
    totalMinutes >= (22 * 60) ||
    totalMinutes <= (7 * 60);

  if (heuresCreuses) {
    return "HC : " + tempoHcPrice(tempoToday);
  }

  return "HP : " + tempoHpPrice(tempoToday);
}

void updateTariffDisplay() {

  // Rafraîchissement local du tarif HP/HC sans redessiner l'écran complet.
  // Cette fonction est appelée à chaque changement de minute.
  uint16_t bg = colorTodayBackground();
  uint16_t fg = colorTodayText();

  tft.fillRect(0, 45, 250, 25, bg);

  tft.setTextWrap(false);
  tft.setTextSize(1);
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(fg);

  String currentTariff = tempoCurrentTariff();
  tft.setCursor(10, 65);
  tft.print(currentTariff);

  String todayText = "--";
  if (tempoToday == "BLUE") todayText = "BLEU";
  else if (tempoToday == "WHITE") todayText = "BLANC";
  else if (tempoToday == "RED") todayText = "ROUGE";

  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(todayText, 0, 0, &x1, &y1, &w, &h);
  tft.setCursor(232 - w, 65);
  tft.print(todayText);

  tft.setFont();
}

void updateClockDisplay() {

  // Zone réservée à l'heure uniquement.
  // La date et la ville commencent plus à droite et ne sont donc pas touchées.
  uint16_t bg = colorTodayBackground();
  uint16_t fg = colorTodayText();

  tft.fillRect(0, 0, 125, 45, bg);

  tft.setTextWrap(false);
  tft.setTextSize(1);
  tft.setFont(&FreeSans24pt7b);
  tft.setTextColor(fg);

  String heure = timeOK ? getTimeString() : "--:--";

  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(heure, 0, 0, &x1, &y1, &w, &h);

  tft.setCursor(10 - x1, 10 - y1);
  tft.print(heure);

  tft.setFont();
}

void displayStatus() {

  uint16_t bg = colorTodayBackground();
  uint16_t fg = colorTodayText();

  tft.setTextWrap(false);
  tft.setTextSize(1);
  tft.setFont();
  tft.fillScreen(bg);

  int16_t x1, y1;
  uint16_t w, h;

  // ---------------------------------------------------
  // Heure
  // ---------------------------------------------------
  updateClockDisplay();

  // ---------------------------------------------------
  // Date : NE PAS MODIFIER
  // ---------------------------------------------------
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(fg);

  String date = getDateString();
  // Affichage de la date sans l'année : JJ/MM
  if (date.length() >= 5) {
    date = date.substring(0, 5);
  }

  tft.getTextBounds(
    date, 0, 0, &x1, &y1, &w, &h
  );

  tft.setCursor(232 - w - x1, 13 - y1);
  tft.print(date);

  // Ville météo : ajout uniquement, même police que la date.
  String ville = weatherCity;
  tft.getTextBounds(
    ville, 0, 0, &x1, &y1, &w, &h
  );
  tft.setCursor(232 - w - x1, 28 - y1);
  tft.print(ville);

  // ---------------------------------------------------
  // METEO DE DEMAIN + RTE DEMAIN
  // Tout est regroupé dans une seule grande bulle.
  // Gauche -> droite : MIN / ICONE / MAX
  // ---------------------------------------------------
  String maxTomorrow =
    weatherOK && !isnan(weatherTempMax[1])
      ? String((int)round(weatherTempMax[1])) + "°"
      : "--°";

  String minTomorrow =
    weatherOK && !isnan(weatherTempMin[1])
      ? String((int)round(weatherTempMin[1])) + "°"
      : "--°";

  const uint16_t TEMP_MAX_COLOR = ST77XX_RED;
  const uint16_t TEMP_MIN_COLOR = 0x5D9F;

  // Grande bulle englobant météo + températures + RTE demain.
  const int boxX = 9;
  const int boxY = 75;
  const int boxW = 220;
  const int boxH = 125;
  const int radius = 18;

  uint16_t pillColor = colorTomorrowBackground();
  uint16_t pillText = colorTomorrowText();

  // Ombre portée discrète de la bulle.
  tft.fillRoundRect(
    boxX + 2, boxY + 3, boxW, boxH, radius,
    0x08C5
  );

  tft.fillRoundRect(
    boxX, boxY, boxW, boxH, radius, pillColor
  );

  uint16_t border =
    tempoTomorrow == "WHITE" ? 0x8410 : fg;

  // Contour de la bulle conservé.
  tft.drawRoundRect(
    boxX, boxY, boxW, boxH, radius, border
  );

  // Températures alignées avec le centre de l'icône.
  tft.setFont(&FreeSans18pt7b);
  tft.getTextBounds(
    minTomorrow, 0, 0, &x1, &y1, &w, &h
  );
  // Ombre portée discrète.
  tft.setTextColor(0x08C5);
  tft.setCursor(50 - w / 2 - x1 + 1, 115 - y1 + 1);
  tft.print(minTomorrow);
  tft.setTextColor(TEMP_MIN_COLOR);
  tft.setCursor(50 - w / 2 - x1, 115 - y1);
  tft.print(minTomorrow);

  // Icône météo : +3 px vers la droite.
  // Codes 45, 51, 61, 71, 80, 85, 95 : remontée de 15 px.
  if (weatherOK) {
    int iconX = 113;
    int iconY = 122;

    int testCode = weatherCodes[1];
    if (testCode == 45 ||
        testCode == 51 ||
        testCode == 61 ||
        testCode == 71 ||
        testCode == 80 ||
        testCode == 85 ||
        testCode == 95) {
      iconY -= 15;
    }

    drawWeatherIcon(testCode, iconX, iconY, 60);
    displayedWeatherCode = testCode;
  } else {
    tft.drawCircle(120, 137, 21, fg);
    tft.drawLine(105, 122, 135, 152, fg);
    tft.drawLine(135, 122, 105, 152, fg);
  }

  tft.setFont(&FreeSans18pt7b);
  tft.getTextBounds(
    maxTomorrow, 0, 0, &x1, &y1, &w, &h
  );
  // Ombre portée discrète.
  tft.setTextColor(0x08C5);
  tft.setCursor(190 - w / 2 - x1 + 1, 115 - y1 + 1);
  tft.print(maxTomorrow);
  tft.setTextColor(TEMP_MAX_COLOR);
  tft.setCursor(190 - w / 2 - x1, 115 - y1);
  tft.print(maxTomorrow);

  // MODELROGS
  tft.setFont();
  tft.setTextSize(1);
  tft.setTextColor(fg);
  tft.setCursor(117, 218);
  tft.print("MODELROGS");

  // DEMAIN : inchangé
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(ST77XX_BLACK);
  tft.setCursor(155, 97);
  tft.print("Demain");

  tft.setTextColor(pillText);

  tft.setFont(&FreeSansBold18pt7b);

  String tomorrowText = "--";
  if (tempoTomorrow == "BLUE") tomorrowText = "BLEU";
  else if (tempoTomorrow == "RED") tomorrowText = "ROUGE";
  else if (tempoTomorrow == "WHITE") tomorrowText = "BLANC";

  // Ombre portée discrète sous le nom de la couleur.
  tft.setTextColor(0x08C5);
  drawCenteredText(tomorrowText, 185);
  tft.setTextColor(pillText);
  drawCenteredText(tomorrowText, 182);

  // ---------------------------------------------------
  // HP / HC + COULEUR DU JOUR
  // Tarif dynamique selon l'heure et la couleur d'AUJOURD'HUI.
  // HC : 22:00 -> 07:00 inclus.
  // HP : 07:01 -> 21:59.
  // ---------------------------------------------------
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(fg);

  // Tarif actuel calculé à partir de l'heure locale et
  // de la couleur Tempo d'AUJOURD'HUI.
  String currentTariff = tempoCurrentTariff();

  // Tarif à gauche.
  // On conserve volontairement le symbole "E" pour ce test.
  tft.setCursor(10, 65);
  tft.print(currentTariff);

  // Couleur d'AUJOURD'HUI affichée à droite de la ligne.
  String todayText = "--";
  if (tempoToday == "BLUE") todayText = "BLEU";
  else if (tempoToday == "RED") todayText = "ROUGE";
  else if (tempoToday == "WHITE") todayText = "BLANC";

  tft.getTextBounds(
    todayText, 0, 0, &x1, &y1, &w, &h
  );

  tft.setCursor(222 - w - x1, 52 - y1);
  tft.print(todayText);

  // ---------------------------------------------------
  // RTE MAJ : uniquement point vert + heure
  // ---------------------------------------------------
  tft.setFont(&FreeSans9pt7b);
  tft.setTextColor(fg);

  String label = "RTE MAJ";

  tft.getTextBounds(
    label, 0, 0, &x1, &y1, &w, &h
  );

  tft.setCursor(8 - x1, 215 - y1);
  tft.print(label);

  if (rteLastUpdateOK) {
    tft.fillCircle(92, 221, 5, ST77XX_GREEN);

    String right = rteLastUpdateTime;
    tft.getTextBounds(
      right, 0, 0, &x1, &y1, &w, &h
    );
    tft.setCursor(232 - w - x1, 215 - y1);
    tft.print(right);
  } else {
    tft.fillCircle(92, 221, 5, ST77XX_RED);
  }

  tft.setFont();
}

// =====================================================
// IMAGE DE DEMARRAGE MODELROGS
// 240x240, palette RGB565 indexée
// =====================================================

#define STARTUP_IMAGE_WIDTH  240
#define STARTUP_IMAGE_HEIGHT 240
#define STARTUP_IMAGE_PALETTE_SIZE 200

const uint16_t STARTUP_IMAGE_PALETTE[STARTUP_IMAGE_PALETTE_SIZE] PROGMEM = {
  0x0000, 0x4C53, 0x7DF3, 0x4D0D, 0x552E, 0x458D, 0x2D4C, 0x1D69, 0x156F, 0x0264, 0x0C10, 0x1E99,
  0x1596, 0x15F8, 0x1578, 0x14F6, 0x1534, 0x14F3, 0x0CB4, 0x0125, 0x0B04, 0xAF7A, 0x8F79, 0x7FB7,
  0x6795, 0x5FB3, 0x4FD4, 0x3FF3, 0x2FD2, 0x27B0, 0x27B3, 0x37F5, 0x3ED5, 0x0BEB, 0x024C, 0x0C33,
  0x27FF, 0x1F5F, 0x1EFF, 0x01EA, 0x00A2, 0x0308, 0x26EB, 0x2268, 0x4FB1, 0x3F90, 0x2FB0, 0x276F,
  0x6592, 0x0B67, 0x0C0D, 0x01C7, 0x0BD4, 0x1EFD, 0x1E9E, 0x1E5D, 0x02AD, 0x0020, 0x0184, 0x14A7,
  0x43A9, 0x4676, 0x0268, 0x14CA, 0x2731, 0x5EF5, 0x0061, 0x0BB1, 0x02EF, 0x161D, 0x3F33, 0x14AE,
  0x0351, 0x6F17, 0x4F34, 0x4ECE, 0x3771, 0x2F30, 0x26CF, 0x0B90, 0x15DC, 0x0861, 0x0801, 0x032B,
  0x2945, 0x0330, 0x34AC, 0x36CD, 0x2EAB, 0x5ACB, 0xD6BA, 0xCE59, 0xCE79, 0x4208, 0x632C, 0xDEDB,
  0x9492, 0x1ED4, 0x2735, 0x2796, 0x27F7, 0x1E12, 0xD69A, 0xC638, 0xB596, 0x0B8C, 0xFFFF, 0xEF7D,
  0x10A2, 0x1E72, 0x27F9, 0x2753, 0xA514, 0xF7BE, 0x7BCF, 0x3F4E, 0x8410, 0xDEFB, 0xF77D, 0xF79E,
  0xEF5D, 0xE75D, 0xE73C, 0x738E, 0xBDD7, 0x4772, 0xAD55, 0x6B6D, 0xFFDF, 0xEF3D, 0xE71C, 0x26F1,
  0xEF3C, 0x2F52, 0x2104, 0x9CD3, 0x0C92, 0x1ED1, 0x0C37, 0x02A5, 0x0C78, 0x1594, 0x1573, 0x1904,
  0x2ED4, 0xE75C, 0x151A, 0x4755, 0x2673, 0x1E95, 0x2654, 0x2F14, 0x1E53, 0x5E97, 0x2E16, 0x7E99,
  0x1DD3, 0x1AE6, 0x9CF3, 0x2FF6, 0x25F4, 0x45F6, 0x55F7, 0x1D74, 0x1E15, 0x2595, 0x3D35, 0x35B5,
  0x2738, 0x7639, 0x2514, 0x1CD3, 0x9639, 0x5536, 0x0C12, 0x1C73, 0x34B4, 0x75B8, 0x25B7, 0x1C32,
  0x1CB4, 0x6D36, 0x2C53, 0x13D1, 0x23D2, 0x5494, 0x3412, 0x1B90, 0x6CD4, 0x1350, 0x3391, 0x4BD1,
  0xEF5C, 0x2B2F, 0x130F, 0x12AD, 0x126C, 0x8536, 0x262A, 0xA5F8,
};

const uint8_t STARTUP_IMAGE_RLE[] PROGMEM = {
  255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0,
  255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 113, 0, 1, 1, 1, 2, 1, 3, 5, 4,
  1, 5, 1, 4, 1, 5, 1, 3, 1, 5, 1, 3, 3, 5, 13, 6, 1, 7, 1, 6, 1, 7, 1, 8, 1, 9, 95, 0, 1, 10, 1, 11,
  6, 12, 1, 13, 11, 12, 1, 14, 1, 12, 1, 15, 1, 12, 1, 15, 1, 16, 2, 15, 1, 17, 1, 15, 2, 18, 1, 15, 1, 13, 1, 19,
  74, 0, 1, 20, 1, 3, 1, 21, 1, 22, 6, 23, 2, 24, 1, 25, 1, 26, 1, 25, 5, 26, 3, 27, 1, 28, 1, 27, 4, 28, 1, 29,
  3, 30, 1, 31, 1, 32, 1, 33, 1, 19, 93, 0, 1, 34, 1, 35, 25, 36, 4, 37, 1, 38, 1, 37, 1, 36, 1, 15, 1, 39, 1, 40,
  72, 0, 1, 41, 1, 42, 1, 20, 1, 43, 1, 21, 3, 23, 1, 25, 1, 23, 3, 25, 1, 26, 1, 25, 2, 26, 1, 44, 1, 27, 1, 45,
  1, 27, 1, 45, 6, 46, 1, 29, 1, 47, 3, 29, 1, 28, 1, 48, 1, 9, 1, 49, 1, 50, 1, 40, 91, 0, 1, 51, 1, 52, 1, 51,
  1, 39, 1, 37, 8, 36, 1, 37, 1, 36, 5, 37, 1, 53, 1, 37, 1, 53, 1, 38, 1, 53, 5, 54, 2, 55, 1, 54, 1, 36, 1, 10,
  1, 0, 1, 39, 1, 56, 1, 57, 70, 0, 1, 58, 1, 42, 1, 7, 1, 59, 1, 20, 1, 60, 1, 21, 1, 23, 1, 25, 1, 23, 6, 25,
  1, 44, 1, 26, 1, 44, 1, 27, 1, 45, 1, 46, 1, 45, 4, 46, 1, 29, 5, 47, 1, 28, 1, 61, 1, 58, 1, 62, 1, 49, 1, 50,
  1, 33, 90, 0, 1, 19, 1, 35, 1, 56, 1, 34, 1, 19, 1, 34, 7, 36, 1, 37, 1, 36, 4, 37, 1, 53, 1, 37, 1, 38, 1, 53,
  1, 38, 2, 53, 1, 54, 1, 53, 1, 55, 1, 54, 3, 55, 1, 36, 1, 18, 1, 0, 1, 19, 1, 51, 1, 34, 1, 39, 69, 0, 1, 57,
  1, 63, 1, 7, 1, 59, 2, 7, 1, 20, 1, 60, 1, 21, 2, 23, 1, 25, 1, 23, 2, 25, 4, 26, 1, 44, 1, 27, 1, 45, 1, 27,
  4, 46, 1, 29, 3, 47, 2, 64, 1, 29, 1, 65, 1, 43, 1, 9, 3, 41, 1, 63, 1, 41, 88, 0, 1, 66, 1, 67, 1, 68, 3, 56,
  1, 19, 1, 56, 7, 36, 5, 37, 1, 53, 1, 37, 1, 53, 1, 38, 1, 53, 3, 54, 1, 55, 1, 54, 2, 55, 1, 69, 1, 36, 1, 15,
  1, 0, 1, 19, 1, 51, 1, 19, 1, 51, 1, 34, 1, 51, 68, 0, 1, 66, 2, 7, 3, 59, 1, 7, 1, 9, 1, 60, 1, 21, 3, 23,
  1, 24, 1, 25, 4, 26, 4, 27, 5, 28, 5, 29, 1, 70, 1, 43, 1, 9, 1, 49, 4, 41, 1, 71, 1, 58, 86, 0, 1, 57, 2, 72,
  5, 34, 1, 66, 1, 68, 12, 36, 5, 37, 4, 38, 2, 54, 1, 37, 1, 12, 1, 0, 1, 19, 1, 39, 1, 19, 1, 51, 1, 19, 1, 34,
  1, 39, 68, 0, 1, 66, 1, 63, 1, 7, 1, 59, 1, 7, 3, 59, 1, 58, 1, 60, 1, 21, 1, 73, 1, 65, 1, 73, 1, 65, 1, 73,
  2, 65, 2, 74, 1, 75, 2, 70, 1, 76, 2, 77, 7, 78, 1, 9, 1, 58, 1, 41, 1, 49, 1, 33, 1, 71, 1, 50, 2, 71, 1, 8,
  1, 62, 85, 0, 1, 79, 1, 14, 1, 35, 1, 52, 1, 79, 1, 68, 1, 56, 2, 34, 1, 57, 1, 79, 9, 53, 1, 11, 2, 55, 1, 11,
  5, 55, 2, 69, 1, 80, 1, 54, 1, 12, 1, 0, 1, 40, 1, 39, 1, 19, 1, 39, 2, 19, 1, 34, 1, 51, 68, 0, 1, 66, 1, 63,
  1, 7, 6, 59, 1, 20, 1, 81, 3, 82, 1, 81, 5, 82, 1, 81, 3, 82, 8, 57, 2, 0, 2, 57, 1, 66, 1, 19, 1, 58, 1, 9,
  1, 58, 1, 51, 1, 62, 1, 40, 85, 0, 1, 58, 1, 39, 1, 62, 1, 51, 2, 19, 1, 40, 3, 57, 2, 0, 1, 57, 1, 0, 3, 57,
  2, 0, 11, 57, 3, 66, 1, 0, 1, 19, 1, 39, 1, 19, 1, 39, 1, 19, 1, 39, 1, 51, 1, 39, 1, 51, 68, 0, 1, 57, 3, 7,
  5, 59, 1, 7, 1, 57, 151, 0, 1, 40, 1, 39, 2, 51, 1, 19, 1, 39, 1, 19, 3, 39, 68, 0, 1, 66, 1, 63, 1, 7, 1, 59,
  1, 63, 1, 59, 1, 63, 1, 59, 1, 63, 1, 7, 1, 40, 151, 0, 1, 19, 1, 39, 1, 51, 2, 39, 1, 19, 1, 39, 1, 19, 1, 34,
  1, 51, 68, 0, 1, 66, 1, 63, 1, 7, 1, 63, 1, 59, 1, 63, 1, 59, 2, 63, 1, 7, 1, 58, 151, 0, 1, 51, 2, 39, 2, 51,
  1, 39, 1, 51, 1, 39, 1, 34, 1, 39, 68, 0, 1, 40, 1, 63, 1, 7, 1, 63, 1, 59, 4, 63, 1, 7, 1, 58, 151, 0, 1, 39,
  1, 34, 2, 39, 1, 51, 2, 39, 1, 51, 1, 34, 1, 39, 68, 0, 1, 66, 1, 63, 1, 7, 3, 63, 1, 59, 2, 63, 1, 7, 1, 58,
  151, 0, 2, 34, 1, 39, 1, 51, 3, 39, 1, 51, 1, 34, 1, 39, 68, 0, 1, 40, 8, 63, 1, 7, 1, 58, 151, 0, 1, 56, 1, 34,
  6, 39, 1, 56, 1, 39, 68, 0, 1, 66, 1, 63, 1, 8, 3, 63, 1, 33, 1, 63, 1, 33, 1, 7, 1, 58, 151, 0, 1, 83, 1, 56,
  6, 39, 1, 56, 1, 39, 68, 0, 1, 40, 3, 63, 1, 50, 1, 63, 1, 33, 1, 63, 1, 33, 1, 6, 1, 58, 151, 0, 1, 68, 1, 56,
  1, 39, 1, 34, 1, 39, 1, 34, 1, 39, 1, 34, 1, 68, 1, 39, 68, 0, 1, 66, 1, 71, 3, 63, 1, 33, 1, 63, 2, 33, 1, 63,
  1, 84, 151, 0, 1, 83, 1, 68, 2, 34, 1, 39, 2, 34, 1, 39, 1, 85, 1, 34, 68, 0, 1, 40, 1, 71, 1, 63, 1, 50, 1, 33,
  1, 63, 3, 33, 1, 86, 1, 84, 151, 0, 1, 85, 1, 68, 6, 34, 1, 85, 1, 34, 68, 0, 1, 66, 1, 71, 1, 63, 1, 50, 1, 63,
  4, 33, 1, 86, 1, 58, 151, 0, 1, 85, 1, 68, 1, 34, 1, 56, 4, 34, 1, 72, 1, 39, 68, 0, 1, 40, 1, 50, 1, 63, 1, 50,
  1, 33, 1, 50, 3, 33, 1, 86, 1, 84, 9, 0, 1, 57, 79, 0, 1, 57, 1, 19, 1, 58, 1, 51, 3, 58, 1, 40, 54, 0, 2, 85,
  2, 56, 1, 34, 2, 56, 1, 34, 1, 72, 1, 34, 68, 0, 1, 40, 2, 50, 1, 33, 1, 50, 4, 33, 1, 50, 1, 84, 8, 0, 1, 5,
  7, 75, 1, 87, 1, 75, 6, 87, 1, 88, 2, 87, 1, 77, 1, 7, 6, 0, 1, 89, 1, 90, 4, 91, 1, 92, 1, 90, 1, 93, 20, 0,
  1, 94, 1, 95, 1, 91, 2, 92, 2, 91, 1, 95, 1, 96, 13, 0, 1, 51, 1, 49, 1, 8, 1, 97, 1, 98, 2, 99, 1, 100, 2, 99,
  1, 98, 1, 101, 1, 50, 1, 9, 1, 57, 12, 0, 1, 94, 1, 95, 1, 102, 10, 92, 1, 102, 1, 92, 1, 103, 1, 104, 1, 96, 1, 89,
  1, 84, 18, 0, 1, 105, 1, 85, 3, 56, 1, 34, 1, 56, 1, 34, 1, 72, 1, 34, 68, 0, 1, 40, 1, 50, 1, 63, 1, 50, 1, 33,
  1, 50, 2, 33, 1, 105, 1, 86, 1, 58, 8, 0, 1, 25, 1, 23, 2, 25, 1, 44, 1, 25, 6, 44, 1, 45, 4, 27, 1, 28, 2, 27,
  1, 77, 6, 0, 1, 96, 7, 106, 1, 107, 1, 108, 18, 0, 1, 93, 8, 106, 1, 103, 10, 0, 1, 66, 1, 33, 1, 109, 1, 100, 2, 110,
  1, 100, 3, 99, 1, 98, 3, 99, 1, 100, 1, 110, 1, 100, 1, 111, 1, 50, 1, 19, 10, 0, 1, 112, 18, 106, 1, 113, 1, 103, 1, 114,
  1, 57, 15, 0, 1, 79, 1, 72, 6, 56, 1, 67, 1, 34, 68, 0, 1, 66, 1, 71, 3, 50, 3, 33, 1, 105, 1, 50, 1, 58, 8, 0,
  1, 75, 1, 25, 7, 44, 3, 45, 1, 76, 1, 115, 2, 76, 1, 77, 1, 76, 1, 47, 1, 28, 1, 78, 6, 0, 1, 116, 1, 106, 6, 107,
  1, 106, 1, 117, 1, 57, 16, 0, 1, 108, 1, 107, 1, 106, 1, 118, 5, 119, 1, 106, 1, 104, 9, 0, 1, 105, 1, 98, 2, 110, 1, 99,
  12, 98, 1, 99, 1, 100, 1, 110, 1, 100, 1, 50, 9, 0, 1, 96, 1, 106, 6, 119, 2, 118, 4, 107, 3, 120, 1, 121, 1, 120, 1, 107,
  2, 106, 1, 122, 1, 123, 14, 0, 2, 79, 2, 68, 2, 56, 1, 68, 1, 56, 1, 52, 1, 34, 68, 0, 1, 40, 5, 50, 1, 33, 1, 105,
  1, 49, 1, 71, 1, 58, 8, 0, 1, 75, 2, 25, 5, 44, 2, 45, 1, 76, 1, 115, 2, 76, 1, 77, 1, 76, 3, 77, 1, 28, 1, 87,
  6, 0, 1, 116, 1, 106, 7, 107, 1, 106, 1, 124, 16, 0, 1, 90, 1, 106, 7, 119, 1, 106, 1, 104, 8, 0, 1, 101, 1, 110, 1, 100,
  18, 98, 1, 100, 1, 110, 1, 101, 1, 57, 7, 0, 1, 96, 1, 106, 5, 119, 3, 118, 4, 107, 4, 120, 1, 121, 3, 122, 2, 106, 1, 124,
  13, 0, 1, 79, 1, 67, 5, 68, 1, 56, 1, 52, 1, 34, 68, 0, 1, 40, 4, 50, 1, 33, 3, 105, 1, 50, 1, 58, 8, 0, 1, 75,
  1, 25, 1, 44, 1, 75, 1, 44, 1, 115, 1, 44, 1, 115, 1, 125, 1, 44, 1, 115, 1, 76, 1, 47, 2, 76, 1, 77, 1, 76, 2, 77,
  1, 28, 1, 78, 1, 57, 5, 0, 1, 116, 1, 106, 7, 107, 1, 118, 1, 106, 1, 96, 14, 0, 1, 126, 1, 106, 1, 113, 4, 119, 2, 113,
  1, 119, 1, 106, 1, 104, 6, 0, 1, 66, 1, 97, 1, 110, 22, 98, 1, 110, 1, 97, 1, 58, 6, 0, 1, 96, 1, 106, 6, 119, 1, 118,
  6, 107, 3, 120, 2, 121, 1, 120, 1, 121, 1, 122, 1, 120, 1, 106, 1, 91, 1, 57, 11, 0, 1, 79, 1, 67, 1, 85, 5, 68, 1, 52,
  1, 34, 68, 0, 1, 66, 1, 71, 3, 50, 1, 105, 1, 33, 2, 105, 1, 71, 1, 58, 8, 0, 1, 75, 1, 44, 1, 25, 1, 23, 4, 25,
  1, 23, 1, 74, 1, 23, 1, 125, 1, 25, 1, 76, 1, 125, 1, 24, 1, 74, 1, 47, 1, 77, 1, 30, 1, 88, 1, 57, 5, 0, 1, 116,
  1, 106, 8, 107, 1, 113, 1, 106, 1, 127, 12, 0, 1, 123, 1, 106, 1, 128, 1, 119, 7, 113, 1, 106, 1, 104, 6, 0, 1, 97, 1, 110,
  8, 98, 1, 99, 6, 110, 1, 100, 8, 98, 1, 110, 1, 98, 1, 66, 5, 0, 1, 96, 1, 106, 5, 119, 1, 107, 8, 106, 1, 119, 1, 121,
  1, 129, 3, 120, 1, 121, 1, 122, 1, 130, 1, 106, 1, 92, 11, 0, 1, 79, 1, 52, 4, 85, 2, 68, 1, 52, 1, 34, 68, 0, 1, 40,
  3, 50, 1, 105, 1, 50, 3, 105, 1, 50, 1, 58, 8, 0, 1, 75, 1, 44, 1, 23, 2, 21, 1, 23, 1, 24, 1, 22, 1, 23, 1, 115,
  1, 22, 1, 23, 1, 22, 1, 24, 1, 74, 1, 24, 1, 23, 1, 77, 1, 64, 1, 46, 1, 131, 1, 57, 5, 0, 1, 116, 1, 106, 9, 107,
  2, 106, 1, 93, 10, 0, 1, 93, 2, 106, 1, 119, 8, 113, 1, 106, 1, 104, 5, 0, 1, 8, 1, 110, 4, 98, 1, 99, 2, 98, 2, 110,
  1, 98, 1, 8, 4, 71, 1, 8, 1, 98, 2, 110, 2, 98, 1, 99, 4, 98, 1, 110, 1, 101, 5, 0, 1, 96, 1, 106, 5, 119, 1, 113,
  1, 91, 1, 126, 5, 104, 1, 91, 1, 113, 2, 106, 3, 120, 1, 132, 3, 122, 1, 106, 1, 126, 10, 0, 1, 79, 1, 67, 4, 85, 2, 68,
  1, 52, 1, 34, 68, 0, 1, 40, 1, 71, 2, 50, 1, 105, 1, 50, 1, 105, 1, 50, 1, 105, 1, 71, 1, 58, 8, 0, 1, 75, 1, 44,
  3, 25, 1, 44, 1, 115, 1, 44, 1, 115, 1, 44, 1, 25, 1, 74, 1, 115, 1, 125, 1, 74, 1, 76, 1, 74, 1, 77, 1, 64, 1, 133,
  1, 78, 1, 66, 5, 0, 1, 116, 1, 106, 10, 107, 1, 106, 1, 119, 1, 108, 8, 0, 1, 134, 2, 106, 1, 119, 9, 113, 1, 106, 1, 104,
  4, 0, 1, 41, 1, 110, 1, 99, 5, 98, 1, 99, 1, 110, 1, 109, 1, 33, 1, 40, 6, 0, 1, 40, 1, 105, 1, 97, 1, 110, 1, 99,
  2, 98, 1, 99, 3, 98, 1, 110, 1, 49, 4, 0, 1, 96, 1, 106, 4, 119, 1, 128, 1, 106, 1, 84, 7, 0, 1, 84, 1, 114, 1, 130,
  1, 106, 1, 107, 1, 122, 4, 121, 1, 107, 1, 106, 1, 89, 9, 0, 1, 79, 1, 52, 1, 85, 2, 72, 1, 85, 1, 72, 1, 68, 1, 52,
  1, 34, 68, 0, 1, 40, 3, 50, 1, 105, 1, 50, 1, 105, 1, 50, 1, 105, 1, 10, 1, 58, 8, 0, 1, 75, 1, 25, 1, 45, 5, 115,
  2, 76, 3, 77, 1, 78, 1, 77, 2, 78, 1, 77, 1, 131, 1, 111, 1, 78, 1, 57, 5, 0, 1, 116, 1, 106, 9, 107, 2, 118, 1, 106,
  1, 90, 8, 0, 1, 117, 1, 106, 11, 113, 1, 106, 1, 104, 3, 0, 1, 57, 1, 98, 1, 110, 5, 98, 1, 99, 1, 110, 1, 71, 12, 0,
  1, 71, 1, 110, 1, 99, 5, 98, 1, 100, 1, 98, 1, 66, 3, 0, 1, 96, 1, 106, 4, 119, 1, 128, 1, 106, 1, 93, 9, 0, 1, 81,
  1, 112, 1, 106, 1, 120, 1, 122, 3, 120, 1, 122, 1, 106, 1, 102, 9, 0, 1, 79, 1, 52, 2, 72, 1, 85, 1, 72, 2, 68, 1, 52,
  1, 34, 68, 0, 1, 40, 2, 10, 1, 50, 1, 10, 1, 50, 1, 10, 2, 105, 1, 71, 1, 58, 7, 0, 1, 57, 1, 75, 2, 44, 1, 115,
  1, 46, 1, 115, 2, 125, 1, 47, 1, 77, 1, 115, 1, 76, 1, 77, 1, 78, 1, 64, 1, 77, 2, 131, 1, 78, 1, 133, 1, 78, 1, 66,
  5, 0, 1, 116, 1, 106, 9, 107, 1, 119, 2, 118, 1, 106, 1, 124, 6, 0, 1, 104, 1, 106, 1, 113, 1, 119, 10, 113, 1, 106, 1, 104,
  3, 0, 1, 41, 1, 110, 5, 98, 1, 99, 1, 110, 1, 50, 14, 0, 1, 50, 1, 110, 1, 99, 5, 98, 1, 110, 1, 33, 3, 0, 1, 96,
  1, 106, 4, 119, 1, 128, 1, 106, 1, 93, 11, 0, 1, 135, 1, 106, 4, 120, 1, 121, 1, 107, 1, 106, 1, 89, 8, 0, 1, 85, 1, 35,
  5, 72, 1, 85, 1, 52, 1, 34, 68, 0, 1, 40, 1, 71, 5, 50, 2, 105, 1, 136, 1, 58, 7, 0, 1, 57, 1, 75, 1, 46, 4, 25,
  1, 24, 1, 44, 1, 73, 1, 125, 1, 74, 1, 70, 1, 74, 1, 125, 1, 70, 1, 131, 1, 64, 1, 131, 1, 137, 1, 111, 1, 78, 1, 81,
  5, 0, 1, 116, 1, 106, 7, 107, 3, 118, 2, 107, 1, 119, 1, 106, 1, 116, 4, 0, 1, 116, 1, 106, 1, 128, 1, 119, 11, 113, 1, 106,
  1, 104, 3, 0, 1, 101, 1, 100, 5, 98, 1, 110, 1, 101, 16, 0, 1, 101, 1, 100, 4, 98, 1, 97, 1, 100, 1, 109, 3, 0, 1, 96,
  1, 106, 4, 119, 1, 128, 1, 106, 1, 93, 12, 0, 1, 92, 1, 106, 4, 120, 1, 121, 1, 106, 1, 104, 8, 0, 1, 85, 1, 138, 5, 72,
  1, 85, 1, 52, 1, 56, 68, 0, 1, 66, 1, 71, 2, 10, 1, 50, 1, 67, 1, 50, 1, 10, 1, 105, 1, 71, 1, 58, 8, 0, 1, 87,
  1, 45, 1, 25, 1, 24, 1, 22, 2, 23, 1, 24, 1, 23, 2, 74, 1, 78, 3, 24, 1, 137, 1, 131, 1, 137, 1, 131, 1, 64, 1, 78,
  1, 40, 5, 0, 1, 116, 1, 106, 6, 107, 1, 106, 1, 119, 2, 107, 1, 118, 2, 119, 2, 106, 1, 89, 2, 0, 1, 89, 2, 106, 1, 119,
  3, 113, 1, 119, 2, 106, 1, 113, 1, 119, 4, 113, 1, 106, 1, 104, 2, 0, 1, 40, 2, 99, 4, 98, 2, 99, 1, 40, 16, 0, 1, 58,
  1, 100, 6, 98, 1, 100, 1, 58, 2, 0, 1, 96, 1, 106, 4, 119, 1, 128, 1, 106, 1, 93, 12, 0, 1, 89, 1, 106, 1, 107, 4, 120,
  1, 106, 1, 90, 8, 0, 1, 85, 1, 138, 1, 52, 5, 72, 1, 138, 1, 34, 68, 0, 1, 66, 2, 136, 1, 50, 1, 10, 1, 50, 1, 10,
  1, 105, 1, 10, 1, 136, 1, 58, 8, 0, 1, 75, 1, 27, 1, 44, 1, 125, 2, 115, 1, 74, 1, 115, 1, 87, 1, 131, 3, 70, 1, 77,
  1, 74, 4, 137, 1, 64, 1, 131, 1, 40, 5, 0, 1, 116, 1, 106, 6, 107, 1, 130, 1, 106, 1, 107, 1, 118, 3, 119, 1, 107, 2, 106,
  2, 57, 2, 106, 1, 119, 5, 113, 1, 106, 1, 130, 1, 119, 5, 113, 1, 106, 1, 104, 2, 0, 1, 9, 1, 100, 1, 99, 4, 98, 1, 110,
  1, 101, 18, 0, 1, 101, 1, 100, 3, 98, 1, 97, 1, 98, 1, 100, 1, 139, 2, 0, 1, 96, 1, 106, 4, 119, 1, 128, 1, 106, 1, 93,
  13, 0, 1, 95, 1, 106, 4, 120, 2, 119, 1, 84, 7, 0, 1, 68, 1, 138, 1, 72, 1, 67, 1, 72, 1, 52, 2, 72, 1, 138, 1, 34,
  68, 0, 1, 66, 1, 17, 1, 71, 1, 136, 3, 10, 1, 50, 1, 10, 1, 71, 1, 58, 7, 0, 1, 57, 1, 87, 1, 27, 1, 76, 1, 115,
  1, 76, 3, 77, 1, 78, 1, 131, 1, 78, 1, 64, 1, 78, 1, 137, 1, 78, 1, 137, 1, 131, 2, 137, 1, 97, 1, 78, 1, 40, 5, 0,
  1, 116, 1, 106, 4, 107, 1, 119, 1, 106, 1, 94, 1, 96, 1, 106, 6, 119, 1, 106, 1, 102, 1, 92, 1, 106, 2, 119, 4, 113, 1, 106,
  1, 135, 1, 93, 1, 106, 1, 128, 4, 113, 1, 106, 1, 104, 2, 0, 1, 41, 1, 110, 5, 98, 1, 110, 1, 50, 18, 0, 1, 33, 1, 110,
  5, 98, 1, 110, 1, 50, 2, 0, 1, 96, 1, 106, 4, 119, 1, 128, 1, 106, 1, 93, 13, 0, 1, 112, 1, 106, 3, 120, 2, 121, 1, 106,
  1, 94, 7, 0, 1, 68, 1, 138, 2, 52, 2, 72, 1, 52, 1, 72, 1, 140, 1, 68, 68, 0, 1, 57, 1, 17, 1, 141, 6, 10, 1, 136,
  1, 58, 8, 0, 1, 87, 1, 26, 1, 76, 1, 125, 2, 76, 2, 77, 1, 78, 2, 64, 2, 78, 8, 137, 1, 19, 5, 0, 1, 116, 1, 106,
  4, 107, 1, 118, 1, 106, 1, 89, 1, 0, 1, 95, 1, 106, 1, 118, 5, 119, 2, 106, 4, 119, 1, 113, 1, 119, 1, 106, 1, 122, 1, 0,
  1, 93, 1, 106, 5, 113, 1, 106, 1, 104, 2, 0, 1, 33, 1, 110, 1, 99, 4, 98, 1, 110, 1, 41, 18, 0, 1, 139, 1, 100, 5, 98,
  1, 100, 1, 71, 2, 0, 1, 96, 1, 106, 4, 119, 1, 128, 1, 106, 1, 93, 13, 0, 1, 96, 1, 106, 2, 120, 3, 121, 1, 106, 1, 114,
  7, 0, 1, 56, 1, 138, 1, 52, 1, 72, 1, 52, 1, 72, 1, 52, 1, 72, 1, 140, 1, 39, 69, 0, 1, 19, 1, 16, 1, 142, 1, 50,
  1, 136, 1, 71, 2, 10, 1, 71, 1, 58, 7, 0, 1, 57, 1, 87, 1, 27, 3, 125, 1, 74, 1, 78, 1, 77, 3, 74, 1, 70, 1, 65,
  1, 131, 2, 137, 1, 109, 1, 137, 1, 109, 1, 137, 1, 78, 1, 58, 5, 0, 1, 116, 1, 106, 5, 107, 1, 106, 1, 116, 1, 0, 1, 81,
  1, 119, 1, 106, 12, 119, 1, 106, 1, 118, 1, 143, 1, 0, 1, 94, 1, 106, 5, 113, 1, 106, 1, 104, 2, 0, 1, 33, 1, 110, 1, 99,
  3, 98, 1, 99, 1, 100, 1, 62, 18, 0, 1, 9, 1, 100, 5, 98, 1, 100, 1, 8, 2, 0, 1, 96, 1, 106, 4, 119, 1, 128, 1, 106,
  1, 93, 13, 0, 1, 96, 1, 106, 3, 120, 2, 121, 1, 106, 1, 114, 7, 0, 1, 56, 1, 138, 4, 52, 1, 72, 1, 140, 1, 39, 71, 0,
  1, 19, 1, 141, 1, 12, 4, 10, 1, 17, 1, 58, 7, 0, 1, 57, 1, 87, 1, 28, 1, 24, 1, 125, 2, 70, 1, 78, 1, 77, 1, 22,
  1, 65, 1, 23, 1, 65, 1, 22, 1, 144, 2, 137, 3, 109, 1, 137, 1, 109, 1, 40, 5, 0, 1, 116, 1, 106, 5, 107, 1, 106, 1, 114,
  2, 0, 1, 93, 2, 106, 1, 107, 9, 119, 2, 106, 1, 93, 2, 0, 1, 94, 1, 106, 5, 113, 1, 106, 1, 104, 2, 0, 1, 33, 1, 110,
  4, 98, 1, 99, 1, 110, 1, 41, 18, 0, 1, 139, 1, 100, 3, 98, 1, 97, 1, 98, 1, 110, 1, 71, 2, 0, 1, 96, 1, 106, 4, 119,
  1, 128, 1, 106, 1, 93, 13, 0, 1, 135, 1, 106, 3, 120, 1, 121, 1, 145, 1, 106, 1, 114, 7, 0, 1, 56, 1, 140, 2, 52, 1, 72,
  1, 52, 1, 140, 1, 68, 73, 0, 1, 66, 2, 141, 3, 10, 1, 17, 1, 58, 7, 0, 1, 57, 1, 87, 1, 28, 1, 125, 1, 70, 1, 76,
  1, 131, 1, 77, 1, 64, 3, 70, 1, 131, 1, 144, 1, 131, 1, 109, 1, 137, 4, 109, 1, 137, 1, 58, 5, 0, 1, 116, 1, 106, 1, 118,
  4, 107, 1, 106, 1, 114, 3, 0, 1, 127, 1, 106, 1, 128, 5, 119, 1, 113, 2, 119, 1, 128, 1, 106, 1, 123, 3, 0, 1, 94, 1, 106,
  1, 113, 1, 119, 3, 113, 1, 106, 1, 104, 2, 0, 1, 139, 1, 110, 1, 99, 4, 98, 1, 110, 1, 8, 18, 0, 1, 71, 1, 110, 5, 98,
  1, 100, 1, 33, 2, 0, 1, 96, 1, 106, 4, 119, 1, 128, 1, 106, 1, 93, 13, 0, 1, 124, 1, 106, 1, 120, 2, 121, 1, 145, 1, 121,
  1, 106, 1, 127, 7, 0, 1, 34, 1, 140, 3, 52, 1, 146, 1, 56, 75, 0, 1, 57, 1, 141, 1, 16, 1, 10, 1, 136, 1, 17, 1, 58,
  7, 0, 1, 57, 1, 131, 1, 27, 1, 47, 2, 64, 1, 77, 2, 131, 1, 64, 5, 137, 1, 109, 1, 137, 4, 109, 1, 137, 1, 58, 5, 0,
  1, 116, 1, 106, 5, 107, 1, 106, 1, 116, 4, 0, 1, 135, 1, 106, 1, 113, 6, 119, 1, 113, 1, 106, 1, 96, 4, 0, 1, 94, 1, 106,
  1, 113, 3, 119, 1, 113, 1, 106, 1, 104, 2, 0, 1, 58, 1, 100, 1, 99, 4, 98, 1, 100, 1, 97, 18, 0, 1, 97, 1, 100, 5, 98,
  1, 100, 1, 9, 2, 0, 1, 96, 1, 106, 4, 119, 1, 128, 1, 106, 1, 93, 12, 0, 1, 143, 1, 118, 1, 119, 2, 121, 1, 122, 1, 121,
  1, 107, 1, 128, 1, 93, 7, 0, 1, 34, 1, 140, 2, 52, 1, 146, 1, 56, 77, 0, 1, 40, 1, 16, 1, 142, 1, 10, 1, 17, 1, 58,
  7, 0, 1, 57, 1, 87, 1, 28, 2, 77, 2, 64, 1, 77, 2, 131, 3, 137, 1, 97, 1, 131, 6, 109, 1, 137, 1, 58, 5, 0, 1, 116,
  1, 106, 1, 118, 4, 107, 1, 106, 1, 114, 5, 0, 1, 124, 1, 106, 5, 119, 1, 113, 1, 106, 1, 124, 5, 0, 1, 94, 1, 106, 1, 113,
  4, 119, 1, 106, 1, 104, 2, 0, 1, 57, 1, 98, 1, 100, 5, 98, 1, 110, 1, 41, 16, 0, 1, 41, 1, 100, 5, 98, 1, 99, 1, 100,
  1, 40, 2, 0, 1, 96, 1, 106, 4, 119, 1, 128, 1, 106, 1, 93, 12, 0, 1, 116, 1, 106, 1, 122, 4, 121, 1, 128, 1, 117, 1, 57,
  7, 0, 1, 34, 1, 15, 1, 138, 1, 146, 1, 34, 79, 0, 1, 57, 1, 17, 1, 12, 1, 17, 1, 58, 7, 0, 1, 57, 1, 131, 1, 29,
  1, 74, 1, 23, 1, 22, 1, 147, 1, 24, 1, 144, 2, 32, 2, 65, 1, 73, 1, 74, 1, 65, 1, 32, 1, 73, 1, 148, 1, 101, 1, 109,
  1, 137, 1, 58, 5, 0, 1, 116, 1, 106, 5, 107, 1, 106, 1, 114, 6, 0, 1, 102, 1, 106, 4, 119, 1, 106, 1, 117, 6, 0, 1, 94,
  1, 106, 4, 113, 1, 119, 1, 106, 1, 104, 3, 0, 1, 71, 1, 110, 5, 98, 2, 99, 1, 40, 15, 0, 1, 98, 1, 100, 5, 98, 1, 110,
  1, 8, 3, 0, 1, 96, 1, 106, 4, 119, 1, 128, 1, 106, 1, 93, 11, 0, 1, 84, 1, 119, 1, 118, 5, 122, 1, 106, 1, 103, 8, 0,
  1, 34, 2, 146, 1, 34, 81, 0, 1, 57, 1, 16, 1, 149, 1, 58, 7, 0, 1, 57, 1, 131, 1, 29, 1, 147, 1, 22, 1, 21, 1, 23,
  1, 24, 1, 70, 1, 32, 4, 73, 1, 32, 2, 65, 1, 22, 1, 150, 2, 109, 1, 137, 1, 58, 5, 0, 1, 116, 1, 106, 2, 118, 3, 107,
  1, 106, 1, 116, 6, 0, 1, 108, 1, 128, 1, 106, 2, 119, 1, 106, 1, 128, 1, 143, 6, 0, 1, 94, 1, 106, 1, 113, 1, 119, 3, 113,
  1, 106, 1, 104, 3, 0, 1, 58, 1, 110, 1, 99, 5, 98, 1, 110, 1, 97, 1, 66, 13, 0, 1, 101, 1, 100, 6, 98, 1, 110, 1, 139,
  3, 0, 1, 96, 1, 106, 4, 119, 1, 128, 1, 106, 1, 93, 10, 0, 1, 84, 1, 120, 1, 106, 1, 130, 3, 122, 1, 145, 1, 121, 1, 106,
  1, 127, 8, 0, 1, 56, 1, 80, 1, 34, 83, 0, 1, 57, 1, 16, 1, 62, 7, 0, 1, 57, 1, 131, 1, 28, 1, 133, 1, 77, 1, 133,
  1, 151, 1, 144, 1, 131, 1, 32, 2, 144, 1, 131, 1, 144, 1, 148, 1, 150, 2, 148, 1, 101, 2, 109, 1, 97, 1, 58, 5, 0, 1, 116,
  1, 106, 2, 118, 3, 107, 1, 106, 1, 116, 7, 0, 1, 93, 1, 106, 1, 128, 2, 106, 1, 93, 7, 0, 1, 94, 1, 106, 1, 113, 1, 119,
  3, 113, 1, 106, 1, 104, 4, 0, 1, 8, 1, 110, 6, 98, 1, 100, 1, 97, 1, 139, 10, 0, 1, 9, 1, 109, 1, 110, 6, 98, 1, 110,
  1, 152, 4, 0, 1, 96, 1, 106, 4, 119, 1, 128, 1, 106, 1, 84, 8, 0, 1, 57, 1, 123, 1, 107, 1, 106, 1, 130, 3, 145, 1, 121,
  1, 122, 1, 106, 1, 117, 9, 0, 1, 85, 1, 67, 85, 0, 2, 40, 7, 0, 1, 57, 1, 137, 1, 28, 2, 64, 2, 78, 2, 137, 1, 78,
  4, 137, 1, 109, 1, 101, 1, 109, 3, 101, 1, 152, 1, 97, 1, 58, 5, 0, 1, 116, 1, 106, 1, 119, 1, 118, 2, 107, 1, 118, 1, 106,
  1, 116, 8, 0, 1, 123, 2, 106, 1, 127, 8, 0, 1, 94, 1, 106, 1, 113, 1, 119, 3, 113, 1, 106, 1, 104, 4, 0, 1, 66, 1, 99,
  1, 110, 6, 98, 2, 100, 1, 101, 1, 33, 1, 9, 1, 66, 2, 0, 1, 57, 1, 51, 1, 41, 1, 8, 2, 100, 6, 98, 1, 99, 1, 110,
  1, 58, 4, 0, 1, 96, 1, 106, 4, 119, 1, 113, 1, 128, 1, 116, 1, 93, 5, 89, 1, 94, 1, 96, 1, 95, 1, 106, 1, 128, 1, 130,
  1, 121, 3, 145, 1, 121, 1, 107, 1, 106, 1, 89, 9, 0, 1, 19, 1, 57, 84, 0, 1, 66, 1, 40, 1, 66, 7, 0, 1, 57, 1, 78,
  1, 133, 1, 64, 1, 137, 2, 144, 1, 78, 1, 137, 1, 144, 1, 32, 1, 137, 2, 109, 2, 101, 1, 78, 1, 109, 2, 148, 2, 109, 1, 58,
  5, 0, 1, 116, 1, 106, 1, 118, 2, 107, 1, 118, 1, 107, 1, 106, 1, 114, 9, 0, 2, 104, 9, 0, 1, 94, 1, 106, 1, 113, 1, 119,
  2, 113, 1, 119, 1, 106, 1, 104, 5, 0, 1, 62, 1, 110, 1, 99, 5, 98, 1, 97, 1, 98, 3, 100, 1, 97, 1, 109, 1, 101, 1, 137,
  3, 100, 7, 98, 1, 99, 1, 110, 1, 33, 5, 0, 1, 96, 1, 106, 5, 119, 1, 107, 9, 106, 1, 128, 1, 121, 1, 122, 1, 121, 1, 145,
  2, 121, 2, 122, 1, 106, 1, 112, 9, 0, 1, 57, 2, 40, 1, 57, 83, 0, 1, 40, 1, 58, 1, 40, 7, 0, 1, 57, 1, 137, 1, 30,
  1, 147, 1, 74, 2, 73, 1, 65, 1, 131, 1, 151, 1, 65, 1, 150, 2, 61, 1, 75, 1, 153, 1, 73, 1, 32, 1, 148, 1, 150, 1, 101,
  1, 97, 1, 58, 5, 0, 1, 116, 1, 106, 1, 118, 4, 107, 1, 106, 1, 114, 20, 0, 1, 94, 1, 106, 1, 113, 1, 119, 2, 113, 1, 119,
  1, 106, 1, 104, 6, 0, 1, 33, 1, 110, 1, 99, 9, 98, 1, 99, 2, 100, 2, 99, 9, 98, 1, 110, 1, 8, 6, 0, 1, 96, 1, 106,
  5, 119, 2, 118, 6, 107, 1, 120, 2, 121, 1, 122, 2, 121, 1, 122, 1, 121, 1, 122, 1, 120, 1, 106, 1, 124, 10, 0, 1, 57, 1, 58,
  1, 19, 1, 57, 83, 0, 1, 40, 1, 51, 1, 40, 7, 0, 1, 57, 1, 137, 1, 30, 1, 74, 1, 24, 1, 70, 1, 32, 1, 22, 1, 147,
  1, 151, 1, 32, 1, 61, 1, 65, 1, 73, 2, 153, 1, 65, 1, 150, 1, 101, 1, 154, 1, 101, 1, 97, 1, 58, 5, 0, 1, 116, 1, 106,
  1, 118, 4, 107, 1, 106, 1, 114, 20, 0, 1, 94, 1, 106, 1, 113, 1, 119, 1, 113, 2, 119, 1, 106, 1, 104, 7, 0, 1, 33, 2, 110,
  20, 98, 1, 100, 1, 110, 1, 71, 7, 0, 1, 96, 1, 106, 5, 119, 1, 118, 4, 107, 5, 120, 4, 121, 1, 122, 1, 130, 1, 128, 1, 106,
  1, 126, 11, 0, 1, 57, 1, 58, 1, 51, 1, 57, 83, 0, 1, 19, 1, 51, 1, 19, 7, 0, 1, 57, 1, 137, 1, 30, 1, 111, 1, 133,
  1, 137, 1, 148, 1, 65, 1, 131, 1, 144, 1, 137, 1, 148, 1, 109, 2, 101, 2, 148, 1, 152, 3, 101, 1, 97, 1, 58, 5, 0, 1, 116,
  1, 106, 5, 107, 1, 106, 1, 116, 20, 0, 1, 94, 1, 106, 1, 113, 1, 119, 2, 113, 1, 119, 1, 106, 1, 104, 8, 0, 1, 62, 1, 97,
  1, 110, 1, 99, 17, 98, 1, 110, 1, 100, 1, 83, 8, 0, 1, 96, 1, 106, 4, 119, 1, 118, 5, 107, 5, 120, 3, 121, 1, 122, 1, 113,
  1, 106, 1, 119, 1, 114, 12, 0, 1, 57, 2, 51, 1, 57, 83, 0, 1, 19, 1, 62, 1, 19, 7, 0, 1, 57, 1, 137, 1, 30, 5, 137,
  2, 109, 1, 137, 2, 109, 7, 101, 1, 152, 1, 149, 1, 58, 5, 0, 1, 96, 1, 106, 5, 119, 1, 106, 1, 116, 20, 0, 1, 127, 1, 106,
  5, 128, 1, 106, 1, 104, 9, 0, 1, 57, 1, 71, 1, 99, 1, 110, 1, 100, 1, 99, 10, 98, 1, 99, 3, 100, 1, 8, 1, 58, 9, 0,
  1, 135, 1, 106, 5, 113, 4, 119, 2, 118, 4, 107, 1, 119, 1, 128, 2, 106, 1, 107, 1, 112, 1, 134, 13, 0, 1, 57, 2, 51, 1, 57,
  83, 0, 1, 19, 1, 62, 1, 58, 7, 0, 1, 57, 1, 137, 1, 30, 2, 131, 2, 137, 1, 151, 4, 148, 1, 101, 1, 148, 1, 109, 6, 101,
  1, 149, 1, 58, 5, 0, 1, 116, 7, 106, 1, 116, 20, 0, 1, 94, 7, 106, 1, 124, 11, 0, 1, 58, 1, 50, 1, 152, 1, 99, 1, 100,
  2, 110, 5, 100, 1, 110, 2, 100, 1, 97, 1, 63, 1, 51, 11, 0, 1, 135, 15, 106, 1, 128, 1, 119, 1, 130, 1, 104, 1, 123, 1, 134,
  15, 0, 1, 57, 2, 51, 1, 57, 83, 0, 1, 58, 1, 62, 1, 58, 7, 0, 1, 57, 1, 109, 1, 30, 1, 74, 1, 73, 1, 65, 1, 32,
  1, 74, 1, 73, 1, 153, 1, 101, 1, 150, 1, 61, 1, 155, 2, 61, 2, 153, 3, 101, 1, 152, 1, 58, 5, 0, 1, 108, 7, 93, 1, 108,
  20, 0, 1, 81, 6, 93, 1, 89, 1, 84, 14, 0, 1, 58, 1, 139, 1, 71, 2, 8, 3, 101, 1, 8, 1, 71, 1, 41, 1, 51, 14, 0,
  1, 143, 16, 93, 1, 84, 1, 81, 18, 0, 1, 57, 2, 51, 1, 66, 83, 0, 1, 58, 1, 56, 1, 58, 7, 0, 1, 57, 1, 137, 1, 111,
  1, 147, 1, 73, 1, 23, 1, 73, 1, 151, 1, 32, 1, 65, 1, 101, 1, 150, 1, 65, 1, 153, 1, 150, 1, 61, 1, 153, 1, 154, 1, 101,
  1, 156, 1, 101, 1, 149, 1, 58, 120, 0, 1, 57, 1, 39, 1, 51, 1, 66, 83, 0, 1, 58, 1, 41, 1, 58, 7, 0, 1, 57, 1, 109,
  1, 111, 1, 97, 1, 137, 1, 97, 1, 151, 1, 97, 1, 109, 5, 101, 1, 148, 4, 101, 1, 142, 1, 101, 1, 152, 1, 51, 120, 0, 1, 57,
  1, 51, 1, 62, 1, 57, 83, 0, 1, 58, 1, 83, 1, 51, 7, 0, 1, 57, 1, 109, 1, 111, 1, 137, 1, 97, 1, 137, 5, 109, 2, 101,
  1, 152, 4, 101, 1, 156, 1, 101, 1, 156, 1, 149, 1, 58, 120, 0, 1, 66, 1, 39, 1, 62, 1, 66, 83, 0, 1, 51, 1, 83, 1, 58,
  7, 0, 1, 57, 1, 109, 1, 111, 1, 98, 1, 151, 2, 148, 2, 109, 1, 152, 2, 148, 5, 101, 4, 156, 1, 152, 1, 51, 6, 0, 25, 81,
  1, 57, 4, 0, 7, 81, 40, 0, 1, 40, 2, 157, 1, 43, 2, 157, 1, 43, 3, 157, 1, 43, 1, 139, 4, 157, 1, 139, 1, 157, 1, 139,
  1, 157, 1, 139, 1, 157, 1, 139, 1, 157, 3, 139, 1, 41, 1, 58, 8, 0, 1, 66, 2, 62, 1, 66, 83, 0, 1, 58, 1, 83, 1, 51,
  7, 0, 1, 57, 1, 109, 1, 111, 1, 151, 1, 32, 1, 144, 1, 61, 1, 101, 1, 148, 1, 65, 1, 153, 1, 21, 1, 148, 1, 101, 1, 156,
  1, 101, 1, 156, 1, 101, 3, 156, 1, 149, 1, 58, 5, 0, 1, 94, 1, 119, 1, 122, 2, 130, 1, 117, 5, 130, 1, 122, 9, 130, 3, 122,
  1, 132, 1, 113, 1, 158, 3, 0, 1, 116, 1, 119, 1, 122, 1, 120, 1, 122, 1, 130, 1, 120, 1, 119, 1, 89, 39, 0, 1, 157, 1, 23,
  1, 25, 1, 26, 1, 25, 5, 26, 5, 27, 1, 31, 4, 28, 7, 30, 1, 159, 1, 8, 8, 0, 1, 66, 2, 62, 1, 66, 83, 0, 1, 51,
  1, 83, 1, 51, 7, 0, 1, 57, 1, 152, 1, 111, 2, 32, 1, 144, 1, 101, 1, 109, 1, 148, 1, 61, 2, 153, 1, 150, 2, 101, 4, 156,
  1, 142, 1, 156, 1, 152, 1, 51, 5, 0, 1, 116, 25, 106, 1, 124, 3, 0, 1, 135, 7, 106, 1, 123, 39, 0, 1, 157, 1, 25, 3, 44,
  1, 46, 2, 45, 3, 47, 2, 77, 2, 64, 1, 78, 2, 64, 1, 78, 4, 137, 1, 109, 1, 137, 2, 109, 1, 99, 1, 8, 8, 0, 1, 66,
  1, 62, 1, 34, 1, 66, 83, 0, 1, 51, 1, 83, 1, 51, 7, 0, 1, 57, 1, 109, 1, 98, 2, 97, 1, 148, 3, 152, 1, 109, 1, 152,
  3, 101, 1, 156, 1, 101, 2, 156, 1, 142, 2, 156, 1, 152, 1, 51, 5, 0, 1, 123, 1, 106, 8, 107, 1, 118, 2, 107, 4, 118, 8, 119,
  1, 106, 1, 126, 3, 0, 1, 96, 1, 106, 1, 113, 1, 119, 3, 113, 1, 106, 1, 94, 39, 0, 1, 60, 1, 25, 1, 44, 1, 45, 1, 25,
  1, 23, 1, 125, 1, 46, 1, 125, 1, 44, 2, 74, 1, 125, 1, 74, 2, 70, 1, 65, 2, 70, 1, 131, 3, 137, 2, 109, 1, 97, 1, 109,
  1, 30, 1, 71, 8, 0, 1, 66, 2, 62, 1, 66, 83, 0, 1, 51, 1, 83, 1, 51, 7, 0, 1, 57, 1, 152, 1, 98, 2, 109, 1, 152,
  2, 109, 3, 152, 1, 101, 5, 156, 1, 8, 1, 156, 2, 142, 1, 152, 1, 51, 5, 0, 1, 123, 1, 106, 10, 107, 4, 118, 9, 119, 1, 106,
  1, 126, 3, 0, 1, 96, 1, 106, 1, 119, 3, 113, 1, 128, 1, 106, 1, 94, 39, 0, 1, 157, 1, 25, 1, 44, 1, 46, 1, 23, 1, 21,
  2, 23, 1, 125, 2, 23, 3, 24, 1, 65, 1, 74, 1, 21, 2, 22, 1, 74, 1, 109, 1, 137, 1, 109, 1, 137, 3, 109, 1, 99, 1, 71,
  8, 0, 1, 66, 1, 62, 1, 56, 1, 66, 83, 0, 1, 51, 1, 105, 1, 51, 7, 0, 1, 57, 1, 109, 1, 98, 1, 148, 1, 101, 1, 152,
  1, 109, 1, 149, 1, 144, 2, 101, 2, 156, 1, 101, 1, 160, 1, 156, 1, 101, 1, 161, 1, 142, 1, 156, 1, 141, 1, 152, 1, 51, 5, 0,
  1, 123, 1, 106, 7, 107, 1, 118, 1, 107, 1, 118, 1, 119, 2, 118, 10, 119, 1, 106, 1, 126, 3, 0, 1, 96, 1, 106, 4, 113, 1, 128,
  1, 106, 1, 94, 39, 0, 1, 157, 1, 25, 1, 44, 2, 45, 1, 25, 2, 44, 4, 125, 1, 70, 1, 77, 3, 70, 1, 74, 1, 32, 1, 131,
  1, 137, 6, 109, 1, 98, 1, 71, 8, 0, 1, 40, 2, 62, 1, 66, 83, 0, 1, 51, 1, 105, 1, 51, 7, 0, 1, 57, 1, 152, 1, 111,
  1, 153, 1, 21, 1, 155, 2, 73, 1, 155, 1, 153, 1, 61, 1, 153, 1, 61, 1, 22, 1, 162, 1, 142, 1, 163, 1, 153, 1, 156, 2, 142,
  1, 164, 1, 58, 5, 0, 1, 123, 1, 106, 1, 107, 1, 120, 3, 107, 1, 120, 18, 106, 1, 103, 3, 0, 1, 96, 1, 106, 4, 113, 1, 128,
  1, 106, 1, 94, 39, 0, 1, 157, 1, 26, 2, 44, 1, 46, 2, 47, 1, 77, 1, 47, 1, 64, 2, 78, 4, 64, 1, 78, 3, 137, 6, 109,
  1, 152, 1, 98, 1, 71, 8, 0, 1, 66, 1, 41, 1, 56, 1, 40, 83, 0, 1, 51, 1, 33, 1, 51, 7, 0, 1, 57, 1, 101, 1, 98,
  1, 32, 1, 22, 1, 73, 2, 153, 1, 61, 1, 161, 1, 150, 2, 61, 1, 162, 1, 61, 1, 154, 2, 160, 1, 156, 2, 142, 1, 164, 1, 51,
  5, 0, 1, 123, 1, 106, 1, 107, 1, 120, 3, 107, 1, 119, 1, 102, 1, 104, 15, 124, 1, 91, 1, 116, 3, 0, 1, 96, 1, 106, 4, 113,
  1, 128, 1, 106, 1, 94, 39, 0, 1, 157, 1, 26, 1, 44, 1, 76, 2, 25, 1, 133, 2, 125, 1, 76, 4, 70, 1, 147, 1, 70, 1, 131,
  2, 137, 7, 109, 1, 152, 1, 98, 1, 71, 8, 0, 1, 66, 1, 34, 1, 41, 1, 66, 83, 0, 1, 51, 1, 105, 1, 62, 7, 0, 1, 57,
  1, 152, 1, 98, 2, 109, 1, 149, 1, 152, 4, 101, 2, 156, 1, 8, 1, 156, 1, 160, 2, 156, 3, 142, 1, 164, 1, 51, 5, 0, 1, 123,
  1, 106, 2, 107, 1, 120, 1, 107, 1, 118, 1, 106, 1, 123, 21, 0, 1, 96, 1, 106, 4, 113, 1, 128, 1, 106, 1, 94, 39, 0, 1, 157,
  1, 26, 1, 45, 1, 47, 1, 23, 1, 21, 1, 25, 2, 24, 1, 73, 1, 23, 1, 65, 1, 70, 1, 131, 1, 74, 1, 32, 1, 144, 1, 131,
  6, 109, 1, 152, 1, 109, 1, 152, 1, 98, 1, 71, 8, 0, 1, 66, 1, 41, 1, 56, 1, 66, 83, 0, 1, 62, 1, 105, 1, 51, 7, 0,
  1, 57, 1, 152, 1, 98, 1, 149, 1, 109, 6, 101, 4, 156, 6, 142, 1, 152, 1, 51, 5, 0, 1, 123, 1, 106, 5, 107, 1, 106, 1, 114,
  21, 0, 1, 96, 1, 106, 5, 113, 1, 106, 1, 94, 39, 0, 1, 157, 1, 26, 1, 45, 1, 46, 1, 125, 1, 24, 2, 125, 2, 70, 1, 77,
  1, 133, 1, 147, 1, 70, 1, 32, 1, 144, 1, 131, 1, 148, 1, 97, 3, 109, 1, 152, 1, 109, 3, 152, 1, 98, 1, 71, 8, 0, 1, 66,
  1, 34, 1, 41, 1, 40, 83, 0, 1, 51, 1, 50, 1, 62, 7, 0, 1, 57, 1, 152, 1, 97, 1, 148, 1, 32, 1, 152, 1, 101, 1, 150,
  1, 160, 2, 150, 2, 160, 1, 150, 1, 160, 1, 165, 1, 163, 1, 160, 3, 142, 1, 164, 1, 51, 5, 0, 1, 123, 1, 106, 3, 107, 2, 120,
  1, 106, 1, 116, 21, 0, 1, 96, 1, 106, 5, 113, 1, 106, 1, 94, 39, 0, 1, 157, 1, 26, 1, 46, 1, 125, 1, 46, 1, 47, 1, 64,
  1, 111, 4, 64, 1, 111, 1, 131, 3, 137, 1, 148, 2, 109, 1, 149, 6, 152, 1, 97, 1, 71, 8, 0, 1, 66, 1, 41, 1, 83, 1, 66,
  83, 0, 1, 51, 1, 50, 1, 51, 7, 0, 1, 57, 1, 152, 1, 97, 1, 153, 1, 155, 1, 65, 1, 153, 1, 155, 1, 162, 1, 153, 2, 155,
  1, 153, 1, 155, 1, 154, 1, 153, 1, 166, 1, 21, 1, 167, 1, 142, 1, 16, 1, 164, 1, 51, 5, 0, 1, 123, 1, 106, 1, 107, 2, 120,
  1, 107, 1, 120, 1, 106, 1, 127, 21, 0, 1, 96, 1, 106, 5, 113, 1, 106, 1, 94, 39, 0, 1, 49, 1, 26, 1, 27, 1, 76, 1, 111,
  1, 125, 1, 46, 2, 133, 1, 64, 2, 131, 3, 137, 1, 131, 1, 97, 2, 109, 1, 152, 1, 109, 4, 152, 1, 101, 1, 164, 1, 97, 1, 71,
  8, 0, 1, 66, 1, 34, 1, 83, 1, 40, 83, 0, 1, 62, 1, 50, 1, 51, 7, 0, 1, 57, 1, 152, 1, 98, 1, 32, 2, 153, 1, 73,
  1, 153, 1, 61, 1, 161, 1, 61, 2, 154, 1, 61, 1, 101, 1, 162, 1, 161, 1, 162, 1, 163, 1, 17, 1, 142, 1, 164, 1, 51, 5, 0,
  1, 123, 1, 106, 1, 107, 1, 120, 2, 107, 1, 120, 1, 128, 1, 126, 2, 89, 1, 94, 1, 89, 1, 94, 1, 89, 1, 94, 1, 89, 7, 94,
  1, 57, 5, 0, 1, 96, 1, 106, 5, 113, 1, 106, 1, 94, 39, 0, 1, 157, 1, 31, 1, 46, 1, 133, 1, 46, 2, 147, 1, 24, 1, 111,
  1, 137, 1, 65, 1, 73, 2, 32, 1, 73, 1, 74, 2, 109, 6, 152, 1, 101, 1, 152, 1, 101, 1, 149, 1, 71, 8, 0, 1, 66, 2, 83,
  1, 66, 83, 0, 1, 62, 1, 50, 1, 62, 7, 0, 1, 57, 1, 152, 1, 98, 1, 97, 2, 149, 1, 152, 1, 149, 1, 101, 1, 156, 2, 142,
  2, 156, 4, 142, 1, 17, 1, 142, 1, 16, 1, 164, 1, 51, 5, 0, 1, 123, 1, 106, 1, 107, 1, 120, 3, 107, 1, 120, 16, 106, 1, 93,
  5, 0, 1, 96, 1, 106, 5, 113, 1, 106, 1, 94, 39, 0, 1, 157, 1, 31, 1, 46, 1, 30, 1, 147, 1, 125, 2, 147, 2, 64, 1, 147,
  1, 151, 1, 74, 1, 65, 2, 32, 1, 109, 1, 152, 1, 109, 4, 152, 1, 101, 1, 164, 1, 101, 1, 164, 1, 97, 1, 71, 8, 0, 1, 66,
  2, 83, 1, 40, 83, 0, 1, 62, 1, 50, 1, 62, 7, 0, 1, 57, 1, 152, 1, 97, 1, 149, 2, 152, 1, 101, 1, 164, 5, 156, 7, 142,
  1, 16, 1, 164, 1, 51, 5, 0, 1, 123, 1, 106, 1, 107, 2, 120, 7, 107, 3, 118, 5, 119, 2, 113, 2, 106, 1, 93, 5, 0, 1, 96,
  1, 106, 5, 113, 1, 106, 1, 94, 39, 0, 1, 41, 1, 31, 1, 46, 3, 133, 1, 111, 1, 64, 3, 97, 1, 64, 3, 109, 1, 97, 1, 109,
  4, 152, 1, 101, 1, 152, 1, 164, 1, 101, 2, 156, 1, 149, 1, 71, 8, 0, 1, 66, 2, 83, 1, 40, 83, 0, 1, 62, 1, 50, 1, 62,
  7, 0, 1, 57, 1, 164, 1, 97, 1, 152, 1, 164, 1, 101, 1, 164, 1, 156, 1, 164, 1, 101, 1, 156, 2, 142, 1, 141, 1, 142, 2, 141,
  2, 142, 2, 16, 1, 164, 1, 51, 5, 0, 1, 123, 1, 106, 1, 107, 3, 120, 1, 107, 1, 120, 8, 107, 1, 118, 5, 119, 1, 128, 1, 106,
  1, 84, 5, 0, 1, 96, 1, 106, 5, 113, 1, 106, 1, 94, 39, 0, 1, 41, 1, 31, 1, 30, 1, 133, 2, 64, 1, 111, 1, 64, 2, 137,
  1, 97, 1, 137, 2, 109, 1, 97, 1, 109, 1, 152, 1, 109, 1, 152, 1, 101, 2, 152, 2, 101, 1, 164, 2, 156, 1, 149, 1, 71, 8, 0,
  1, 66, 2, 83, 1, 40, 83, 0, 1, 62, 1, 50, 1, 62, 7, 0, 1, 57, 1, 152, 1, 97, 1, 61, 1, 153, 1, 156, 1, 164, 1, 61,
  1, 153, 1, 61, 1, 161, 1, 155, 1, 161, 4, 142, 4, 16, 1, 164, 1, 51, 5, 0, 1, 123, 1, 106, 1, 107, 2, 120, 9, 107, 2, 118,
  6, 119, 1, 128, 1, 106, 1, 93, 5, 0, 1, 96, 1, 106, 5, 113, 1, 106, 1, 94, 39, 0, 1, 41, 1, 31, 1, 133, 1, 111, 1, 147,
  1, 23, 1, 24, 1, 32, 1, 147, 1, 32, 1, 65, 1, 73, 1, 32, 2, 65, 2, 73, 2, 61, 1, 150, 1, 101, 1, 164, 1, 156, 1, 164,
  3, 156, 1, 149, 1, 71, 8, 0, 1, 40, 1, 83, 1, 105, 1, 40, 83, 0, 1, 62, 1, 50, 1, 62, 7, 0, 1, 57, 1, 164, 1, 168,
  1, 150, 1, 161, 2, 156, 1, 61, 1, 164, 1, 155, 1, 162, 1, 153, 1, 167, 1, 16, 1, 142, 1, 141, 2, 142, 2, 16, 1, 17, 1, 164,
  1, 51, 5, 0, 1, 123, 1, 106, 1, 107, 2, 120, 3, 107, 1, 118, 2, 119, 5, 113, 6, 128, 2, 106, 1, 93, 5, 0, 1, 96, 1, 106,
  5, 113, 1, 106, 1, 94, 39, 0, 1, 41, 1, 31, 1, 30, 1, 64, 1, 74, 1, 22, 2, 24, 1, 147, 5, 73, 1, 153, 1, 73, 1, 22,
  2, 153, 1, 61, 1, 101, 1, 164, 1, 101, 4, 156, 1, 149, 1, 71, 8, 0, 1, 40, 2, 83, 1, 40, 83, 0, 1, 62, 1, 71, 1, 62,
  7, 0, 1, 57, 1, 101, 1, 97, 1, 164, 7, 156, 1, 141, 4, 142, 5, 16, 1, 164, 1, 51, 5, 0, 1, 123, 1, 106, 2, 107, 1, 120,
  3, 107, 1, 113, 15, 106, 1, 93, 5, 0, 1, 96, 1, 106, 5, 113, 1, 106, 1, 94, 39, 0, 1, 41, 1, 159, 1, 30, 1, 64, 1, 111,
  1, 147, 1, 151, 2, 144, 2, 151, 1, 144, 1, 148, 1, 144, 1, 150, 1, 101, 1, 150, 1, 148, 1, 150, 1, 101, 6, 156, 1, 141, 1, 149,
  1, 71, 8, 0, 1, 40, 1, 83, 1, 105, 1, 40, 83, 0, 1, 62, 1, 71, 1, 62, 7, 0, 1, 66, 1, 164, 1, 149, 1, 164, 1, 152,
  1, 164, 3, 156, 1, 142, 1, 141, 1, 156, 1, 141, 3, 142, 4, 16, 1, 17, 1, 164, 1, 51, 5, 0, 1, 123, 1, 106, 5, 107, 1, 106,
  1, 135, 15, 93, 1, 57, 5, 0, 1, 96, 1, 106, 5, 113, 1, 106, 1, 94, 39, 0, 1, 41, 1, 159, 4, 111, 1, 64, 3, 137, 1, 109,
  1, 152, 2, 109, 1, 152, 2, 101, 1, 152, 1, 101, 1, 156, 1, 101, 5, 156, 1, 141, 1, 149, 1, 71, 8, 0, 1, 40, 1, 83, 1, 105,
  1, 40, 83, 0, 1, 62, 1, 71, 1, 62, 7, 0, 1, 57, 1, 156, 1, 149, 1, 152, 2, 150, 1, 154, 1, 156, 1, 142, 2, 163, 1, 142,
  2, 141, 1, 142, 4, 16, 2, 17, 1, 164, 1, 51, 5, 0, 1, 123, 1, 106, 5, 107, 1, 106, 1, 123, 21, 0, 1, 96, 1, 106, 5, 113,
  1, 106, 1, 94, 39, 0, 1, 41, 1, 159, 3, 111, 1, 151, 1, 97, 1, 137, 1, 32, 1, 147, 1, 144, 1, 148, 1, 32, 1, 144, 1, 150,
  2, 101, 1, 164, 1, 101, 1, 164, 4, 156, 1, 141, 1, 156, 1, 141, 1, 164, 1, 17, 8, 0, 1, 40, 2, 105, 1, 40, 83, 0, 1, 62,
  1, 71, 1, 62, 7, 0, 1, 57, 1, 156, 1, 149, 1, 154, 1, 61, 1, 150, 1, 161, 1, 142, 1, 156, 2, 162, 1, 169, 1, 161, 1, 153,
  1, 163, 1, 17, 2, 16, 3, 17, 1, 164, 1, 51, 5, 0, 1, 123, 1, 106, 5, 107, 1, 106, 1, 116, 21, 0, 1, 96, 1, 106, 5, 113,
  1, 106, 1, 94, 39, 0, 1, 41, 1, 159, 2, 111, 1, 147, 1, 73, 1, 65, 3, 73, 1, 144, 1, 152, 1, 73, 1, 153, 1, 150, 1, 61,
  1, 101, 6, 156, 1, 141, 1, 156, 2, 141, 1, 149, 1, 71, 8, 0, 1, 66, 2, 105, 1, 40, 83, 0, 1, 62, 1, 71, 1, 62, 7, 0,
  1, 57, 1, 156, 1, 152, 1, 61, 1, 150, 1, 154, 1, 141, 1, 142, 1, 163, 1, 161, 1, 61, 1, 155, 1, 167, 1, 162, 1, 170, 2, 16,
  4, 17, 1, 12, 1, 51, 5, 0, 1, 123, 1, 106, 3, 107, 1, 120, 1, 107, 1, 106, 1, 116, 21, 0, 1, 96, 1, 106, 5, 113, 1, 106,
  1, 94, 39, 0, 1, 41, 1, 100, 2, 111, 1, 64, 1, 74, 2, 147, 1, 151, 4, 32, 1, 154, 1, 61, 1, 32, 1, 150, 1, 160, 4, 156,
  1, 141, 1, 156, 2, 141, 1, 142, 1, 164, 1, 17, 8, 0, 1, 40, 2, 105, 1, 40, 83, 0, 1, 62, 1, 71, 1, 62, 7, 0, 1, 57,
  1, 156, 1, 149, 2, 156, 1, 160, 2, 156, 2, 141, 3, 142, 2, 17, 2, 16, 3, 17, 1, 136, 1, 156, 1, 51, 5, 0, 1, 123, 1, 106,
  2, 107, 3, 120, 1, 106, 1, 127, 21, 0, 1, 96, 1, 106, 5, 113, 1, 106, 1, 89, 39, 0, 1, 41, 1, 100, 1, 111, 1, 98, 1, 97,
  1, 109, 3, 97, 1, 101, 2, 149, 1, 152, 1, 101, 1, 156, 2, 152, 5, 156, 1, 141, 1, 156, 3, 141, 1, 164, 1, 71, 8, 0, 1, 66,
  2, 105, 1, 40, 83, 0, 1, 62, 1, 71, 1, 62, 7, 0, 1, 57, 1, 156, 1, 149, 1, 156, 1, 142, 1, 141, 1, 164, 1, 141, 1, 156,
  4, 16, 1, 17, 1, 16, 5, 17, 1, 136, 1, 12, 1, 51, 5, 0, 1, 123, 1, 106, 1, 107, 4, 120, 1, 113, 1, 103, 1, 96, 7, 135,
  8, 158, 1, 126, 1, 94, 3, 0, 1, 96, 1, 106, 5, 113, 1, 128, 1, 124, 1, 96, 1, 135, 11, 158, 1, 135, 1, 112, 1, 135, 1, 81,
  22, 0, 1, 41, 1, 100, 2, 111, 4, 97, 1, 144, 2, 152, 1, 101, 2, 152, 1, 101, 5, 156, 1, 141, 1, 156, 2, 141, 1, 142, 1, 141,
  1, 142, 1, 164, 1, 136, 8, 0, 1, 40, 2, 105, 1, 40, 83, 0, 1, 62, 1, 71, 1, 62, 7, 0, 1, 57, 1, 156, 1, 149, 1, 154,
  1, 160, 1, 165, 1, 156, 3, 154, 2, 163, 1, 17, 1, 163, 1, 160, 1, 163, 2, 171, 3, 17, 1, 156, 1, 51, 5, 0, 1, 123, 1, 106,
  1, 107, 4, 120, 1, 132, 18, 106, 1, 91, 3, 0, 1, 96, 1, 106, 4, 113, 1, 119, 1, 118, 17, 106, 1, 84, 22, 0, 1, 41, 1, 100,
  1, 111, 1, 97, 1, 147, 1, 22, 1, 32, 1, 151, 1, 32, 2, 73, 4, 153, 1, 154, 1, 162, 1, 61, 1, 153, 1, 160, 5, 141, 1, 142,
  1, 141, 1, 164, 1, 71, 8, 0, 1, 40, 2, 105, 1, 40, 83, 0, 1, 62, 1, 71, 1, 62, 7, 0, 1, 66, 2, 156, 1, 153, 1, 22,
  1, 155, 1, 162, 1, 155, 1, 162, 1, 155, 1, 162, 1, 167, 1, 170, 1, 161, 1, 167, 1, 172, 2, 166, 1, 71, 1, 17, 1, 136, 1, 12,
  1, 62, 5, 0, 1, 123, 1, 106, 3, 120, 3, 107, 2, 120, 9, 107, 2, 118, 1, 119, 1, 118, 2, 119, 1, 106, 1, 104, 3, 0, 1, 96,
  1, 106, 2, 119, 1, 113, 15, 119, 3, 118, 1, 128, 1, 119, 1, 84, 22, 0, 1, 41, 1, 110, 1, 111, 1, 97, 1, 74, 1, 22, 2, 73,
  1, 151, 2, 73, 1, 154, 1, 61, 1, 155, 3, 153, 1, 167, 1, 155, 1, 154, 1, 142, 3, 141, 2, 142, 1, 16, 1, 164, 1, 136, 8, 0,
  1, 40, 1, 105, 1, 33, 1, 40, 83, 0, 1, 62, 1, 71, 1, 62, 7, 0, 1, 66, 2, 156, 1, 161, 2, 155, 1, 153, 1, 162, 1, 161,
  1, 162, 1, 167, 2, 161, 1, 162, 1, 166, 1, 162, 1, 167, 1, 162, 3, 136, 1, 12, 1, 51, 5, 0, 1, 123, 1, 106, 1, 107, 3, 120,
  1, 107, 1, 120, 1, 107, 1, 120, 7, 107, 2, 118, 5, 119, 1, 118, 1, 106, 1, 104, 3, 0, 1, 96, 1, 106, 2, 119, 1, 113, 14, 119,
  3, 118, 1, 107, 1, 128, 1, 118, 1, 134, 22, 0, 1, 41, 1, 110, 1, 111, 1, 98, 1, 97, 1, 144, 1, 98, 1, 97, 1, 150, 1, 154,
  1, 148, 1, 152, 2, 101, 1, 160, 1, 150, 1, 61, 1, 156, 1, 163, 1, 160, 5, 141, 2, 16, 1, 164, 1, 71, 8, 0, 1, 40, 1, 105,
  1, 50, 1, 40, 83, 0, 1, 62, 1, 71, 1, 41, 7, 0, 1, 66, 1, 156, 1, 164, 1, 156, 1, 141, 1, 156, 1, 12, 1, 142, 1, 16,
  1, 17, 1, 16, 1, 167, 1, 163, 1, 165, 1, 17, 1, 136, 1, 171, 1, 165, 3, 136, 1, 12, 1, 51, 5, 0, 1, 123, 1, 106, 2, 107,
  1, 120, 1, 107, 2, 120, 2, 107, 1, 120, 5, 107, 3, 118, 6, 119, 1, 106, 1, 126, 3, 0, 1, 135, 1, 106, 1, 113, 15, 119, 4, 118,
  1, 107, 1, 128, 1, 107, 1, 134, 22, 0, 1, 41, 1, 110, 1, 98, 4, 97, 1, 149, 2, 152, 1, 101, 2, 152, 1, 156, 1, 8, 3, 156,
  3, 142, 2, 141, 4, 16, 1, 12, 1, 136, 8, 0, 1, 40, 1, 105, 1, 33, 1, 40, 83, 0, 1, 62, 1, 71, 1, 62, 7, 0, 1, 66,
  1, 141, 1, 149, 1, 141, 4, 142, 1, 16, 1, 142, 1, 17, 1, 136, 3, 17, 6, 136, 1, 12, 1, 51, 5, 0, 1, 116, 1, 106, 10, 113,
  6, 128, 8, 106, 1, 104, 3, 0, 1, 158, 8, 106, 1, 128, 4, 106, 7, 128, 1, 113, 1, 128, 1, 106, 1, 128, 1, 84, 22, 0, 1, 41,
  1, 100, 1, 98, 2, 97, 1, 32, 2, 144, 1, 149, 1, 152, 1, 150, 1, 160, 1, 61, 1, 150, 1, 161, 1, 160, 1, 142, 3, 141, 2, 142,
  1, 141, 1, 16, 1, 142, 2, 16, 1, 12, 1, 71, 8, 0, 1, 40, 1, 105, 1, 50, 1, 40, 83, 0, 1, 62, 1, 71, 1, 41, 7, 0,
  1, 66, 1, 142, 1, 164, 1, 141, 1, 142, 1, 16, 1, 17, 1, 163, 1, 141, 1, 142, 1, 163, 1, 171, 1, 50, 1, 170, 1, 167, 1, 165,
  4, 136, 1, 10, 1, 12, 1, 51, 5, 0, 1, 127, 1, 106, 1, 119, 7, 107, 1, 118, 2, 107, 2, 118, 6, 119, 3, 113, 1, 119, 1, 106,
  1, 112, 3, 0, 1, 96, 1, 106, 2, 128, 9, 106, 7, 128, 2, 113, 1, 119, 1, 106, 1, 113, 1, 134, 22, 0, 1, 41, 1, 110, 2, 97,
  1, 144, 1, 32, 1, 150, 1, 61, 1, 149, 1, 152, 1, 22, 1, 155, 1, 161, 1, 61, 1, 155, 1, 161, 1, 142, 1, 156, 3, 141, 2, 16,
  1, 142, 1, 16, 2, 17, 1, 141, 1, 136, 8, 0, 1, 40, 1, 33, 1, 50, 1, 40, 83, 0, 1, 62, 1, 71, 1, 62, 7, 0, 1, 66,
  1, 142, 1, 156, 1, 167, 2, 161, 1, 162, 4, 161, 1, 169, 2, 166, 1, 171, 1, 162, 1, 171, 4, 136, 1, 12, 1, 51, 6, 0, 2, 84,
  3, 134, 1, 84, 4, 134, 1, 84, 4, 134, 2, 84, 1, 134, 1, 84, 1, 134, 5, 84, 1, 81, 3, 0, 1, 81, 5, 84, 1, 93, 3, 84,
  1, 93, 14, 84, 23, 0, 1, 41, 1, 110, 1, 98, 1, 97, 1, 149, 1, 150, 1, 152, 1, 149, 1, 152, 1, 149, 1, 153, 1, 161, 4, 160,
  2, 141, 1, 142, 2, 141, 1, 142, 4, 16, 1, 17, 1, 12, 1, 71, 8, 0, 1, 40, 2, 50, 1, 40, 83, 0, 1, 62, 1, 50, 1, 62,
  7, 0, 1, 66, 1, 141, 1, 142, 1, 162, 1, 169, 2, 155, 1, 167, 2, 161, 1, 166, 1, 162, 1, 161, 1, 170, 1, 71, 1, 173, 1, 171,
  3, 136, 1, 174, 1, 141, 1, 51, 83, 0, 1, 41, 1, 110, 2, 97, 2, 149, 2, 152, 1, 164, 2, 152, 1, 101, 1, 156, 2, 142, 1, 156,
  2, 141, 1, 16, 1, 142, 5, 16, 2, 17, 1, 141, 1, 136, 8, 0, 1, 40, 2, 50, 1, 40, 83, 0, 1, 62, 1, 50, 1, 62, 7, 0,
  1, 66, 1, 142, 1, 156, 1, 160, 1, 156, 1, 141, 1, 142, 1, 163, 1, 17, 1, 136, 1, 171, 1, 17, 1, 16, 2, 170, 1, 171, 4, 136,
  1, 174, 1, 12, 1, 51, 83, 0, 1, 41, 1, 110, 1, 97, 2, 149, 1, 109, 1, 149, 2, 101, 1, 164, 1, 154, 1, 160, 2, 156, 2, 160,
  1, 141, 2, 163, 1, 16, 2, 163, 2, 16, 3, 17, 1, 16, 1, 71, 8, 0, 1, 40, 2, 50, 1, 19, 83, 0, 1, 62, 1, 50, 1, 139,
  7, 0, 1, 66, 1, 142, 1, 156, 1, 141, 1, 16, 1, 141, 2, 17, 1, 136, 4, 17, 1, 136, 1, 17, 1, 10, 1, 136, 1, 10, 2, 136,
  1, 10, 1, 16, 1, 51, 83, 0, 1, 41, 1, 110, 2, 149, 2, 32, 1, 153, 1, 61, 3, 154, 1, 167, 1, 161, 1, 154, 1, 153, 1, 165,
  1, 167, 1, 170, 2, 161, 1, 162, 2, 16, 4, 17, 1, 16, 1, 136, 8, 0, 1, 40, 2, 50, 1, 40, 83, 0, 1, 62, 1, 71, 1, 62,
  7, 0, 1, 66, 1, 16, 1, 156, 1, 141, 1, 163, 1, 142, 1, 71, 1, 170, 1, 136, 1, 16, 2, 163, 1, 136, 2, 171, 1, 10, 1, 175,
  1, 171, 1, 136, 1, 10, 1, 174, 1, 12, 1, 51, 6, 0, 5, 108, 1, 81, 10, 108, 1, 143, 1, 108, 1, 81, 26, 0, 1, 57, 1, 66,
  1, 19, 4, 51, 1, 19, 1, 57, 23, 0, 1, 41, 1, 100, 2, 149, 1, 153, 1, 21, 4, 61, 1, 160, 1, 161, 1, 155, 1, 153, 1, 161,
  1, 170, 1, 162, 1, 167, 1, 61, 2, 162, 1, 17, 1, 16, 4, 17, 1, 16, 1, 136, 8, 0, 1, 40, 2, 50, 1, 40, 83, 0, 1, 62,
  1, 71, 1, 62, 7, 0, 1, 66, 1, 142, 1, 141, 1, 161, 1, 169, 1, 162, 1, 167, 1, 162, 1, 166, 1, 155, 1, 167, 1, 166, 1, 172,
  1, 161, 1, 175, 1, 176, 1, 162, 1, 173, 1, 10, 1, 136, 1, 10, 1, 16, 1, 62, 5, 0, 1, 94, 1, 128, 1, 121, 3, 122, 1, 130,
  4, 122, 1, 129, 5, 120, 2, 107, 1, 117, 1, 90, 1, 103, 1, 116, 1, 143, 19, 0, 1, 51, 1, 174, 1, 13, 1, 80, 1, 69, 1, 54,
  1, 38, 1, 54, 1, 38, 1, 54, 1, 55, 1, 80, 1, 13, 1, 15, 1, 85, 1, 58, 19, 0, 1, 41, 1, 99, 3, 149, 1, 150, 1, 149,
  2, 164, 1, 165, 1, 160, 2, 164, 1, 141, 1, 142, 1, 163, 1, 165, 1, 160, 1, 163, 1, 17, 1, 165, 1, 170, 4, 17, 1, 136, 1, 16,
  1, 10, 8, 0, 1, 40, 2, 50, 1, 40, 83, 0, 1, 62, 1, 50, 1, 62, 7, 0, 1, 57, 2, 141, 1, 167, 1, 161, 1, 169, 1, 162,
  1, 167, 2, 162, 1, 176, 1, 170, 1, 177, 1, 166, 1, 171, 2, 166, 1, 173, 1, 10, 2, 174, 1, 12, 1, 51, 5, 0, 1, 114, 1, 106,
  1, 128, 2, 113, 8, 128, 10, 106, 1, 113, 1, 96, 16, 0, 1, 85, 1, 80, 1, 38, 1, 37, 2, 38, 7, 54, 2, 38, 2, 37, 1, 54,
  1, 15, 1, 51, 17, 0, 1, 62, 1, 110, 2, 149, 1, 152, 1, 164, 2, 156, 1, 164, 1, 156, 1, 142, 1, 156, 1, 160, 1, 142, 1, 163,
  1, 141, 1, 142, 1, 16, 7, 17, 2, 136, 1, 16, 1, 136, 8, 0, 1, 40, 1, 33, 1, 50, 1, 40, 83, 0, 1, 62, 1, 71, 1, 62,
  7, 0, 1, 66, 2, 141, 1, 16, 1, 171, 1, 17, 1, 12, 1, 163, 1, 16, 1, 136, 1, 175, 1, 171, 1, 10, 1, 175, 1, 171, 2, 136,
  1, 175, 3, 10, 1, 16, 1, 51, 5, 0, 1, 123, 1, 106, 7, 120, 11, 107, 1, 118, 1, 107, 1, 120, 2, 106, 1, 95, 1, 84, 12, 0,
  1, 56, 1, 69, 1, 37, 1, 38, 15, 55, 1, 54, 2, 37, 1, 15, 1, 40, 15, 0, 1, 62, 1, 99, 2, 149, 2, 164, 1, 154, 1, 153,
  1, 154, 1, 61, 1, 161, 1, 162, 1, 161, 1, 163, 1, 161, 1, 154, 1, 163, 2, 17, 1, 16, 4, 17, 3, 136, 1, 16, 1, 10, 8, 0,
  1, 40, 1, 33, 1, 50, 1, 40, 83, 0, 1, 62, 1, 71, 1, 62, 7, 0, 1, 66, 2, 141, 1, 17, 2, 16, 1, 17, 1, 16, 2, 17,
  2, 136, 1, 17, 1, 10, 2, 136, 2, 10, 3, 174, 1, 16, 1, 51, 5, 0, 1, 123, 1, 106, 8, 120, 8, 107, 2, 118, 1, 119, 1, 118,
  2, 107, 1, 120, 1, 106, 1, 119, 1, 84, 10, 0, 1, 18, 1, 37, 1, 38, 5, 55, 1, 54, 6, 55, 1, 54, 6, 55, 1, 54, 1, 37,
  1, 54, 1, 34, 14, 0, 1, 41, 1, 168, 2, 149, 1, 164, 1, 101, 1, 169, 1, 21, 2, 153, 1, 155, 1, 169, 1, 161, 1, 160, 1, 154,
  1, 178, 1, 167, 1, 170, 4, 17, 1, 18, 2, 17, 2, 136, 1, 16, 1, 10, 8, 0, 1, 40, 1, 33, 1, 50, 1, 40, 83, 0, 1, 62,
  1, 71, 1, 62, 7, 0, 1, 66, 1, 16, 2, 141, 1, 163, 1, 16, 1, 170, 1, 17, 1, 16, 2, 136, 1, 17, 1, 136, 2, 171, 1, 174,
  1, 136, 1, 174, 1, 136, 1, 10, 1, 174, 1, 16, 1, 39, 5, 0, 1, 123, 1, 106, 5, 120, 2, 107, 1, 120, 6, 107, 2, 118, 1, 107,
  2, 118, 4, 107, 1, 120, 1, 106, 1, 117, 9, 0, 1, 14, 1, 36, 1, 54, 9, 55, 1, 54, 11, 55, 1, 69, 1, 38, 1, 37, 1, 34,
  13, 0, 1, 62, 1, 168, 1, 149, 2, 164, 1, 152, 2, 164, 1, 154, 1, 160, 1, 141, 1, 165, 2, 160, 1, 165, 2, 163, 1, 170, 1, 16,
  3, 17, 5, 136, 1, 16, 1, 10, 8, 0, 1, 40, 1, 33, 1, 50, 1, 40, 83, 0, 1, 62, 1, 71, 1, 62, 7, 0, 1, 66, 1, 16,
  1, 141, 1, 165, 1, 167, 1, 170, 1, 166, 1, 136, 1, 17, 1, 172, 1, 169, 1, 162, 1, 173, 1, 169, 1, 176, 1, 10, 4, 174, 1, 67,
  1, 16, 1, 51, 5, 0, 1, 123, 1, 106, 5, 120, 1, 132, 11, 106, 1, 119, 1, 107, 1, 118, 3, 107, 1, 118, 1, 107, 1, 106, 1, 96,
  7, 0, 1, 14, 1, 37, 8, 55, 1, 54, 1, 38, 5, 37, 1, 38, 1, 54, 8, 55, 1, 54, 1, 37, 1, 34, 12, 0, 1, 62, 1, 168,
  1, 152, 1, 164, 1, 150, 1, 149, 1, 164, 1, 156, 3, 141, 1, 142, 1, 141, 1, 142, 4, 16, 2, 17, 1, 136, 1, 17, 1, 136, 1, 18,
  3, 136, 1, 16, 1, 10, 8, 0, 1, 40, 1, 105, 1, 50, 1, 40, 83, 0, 1, 62, 1, 50, 1, 62, 7, 0, 1, 66, 2, 16, 1, 167,
  2, 165, 1, 171, 1, 17, 1, 136, 1, 48, 3, 162, 1, 166, 1, 179, 1, 10, 1, 174, 1, 136, 1, 10, 1, 174, 1, 10, 1, 15, 1, 51,
  5, 0, 1, 123, 1, 106, 5, 120, 1, 119, 1, 103, 1, 135, 8, 158, 1, 103, 2, 106, 4, 107, 1, 118, 1, 107, 1, 106, 1, 118, 1, 81,
  5, 0, 1, 67, 1, 36, 1, 55, 1, 69, 4, 55, 1, 69, 1, 54, 1, 37, 1, 38, 1, 14, 1, 18, 1, 35, 1, 174, 1, 35, 1, 174,
  1, 15, 1, 55, 1, 37, 1, 38, 5, 55, 2, 69, 2, 54, 1, 19, 11, 0, 1, 62, 1, 168, 2, 164, 1, 149, 1, 153, 1, 156, 1, 12,
  1, 156, 4, 141, 3, 16, 4, 17, 1, 18, 6, 136, 1, 17, 1, 10, 8, 0, 1, 40, 2, 33, 1, 40, 83, 0, 1, 62, 1, 50, 1, 62,
  7, 0, 1, 66, 1, 16, 1, 141, 1, 17, 1, 16, 1, 163, 3, 17, 1, 35, 1, 136, 1, 17, 1, 136, 2, 10, 1, 136, 3, 174, 1, 10,
  1, 67, 1, 16, 1, 51, 5, 0, 1, 123, 1, 106, 5, 120, 1, 106, 1, 123, 10, 0, 1, 94, 1, 113, 1, 128, 1, 107, 1, 118, 1, 107,
  1, 118, 2, 119, 1, 106, 1, 94, 4, 0, 1, 19, 1, 37, 1, 54, 5, 55, 1, 69, 2, 38, 1, 18, 1, 51, 7, 0, 1, 40, 1, 79,
  1, 55, 1, 37, 1, 55, 1, 69, 3, 55, 2, 69, 1, 37, 1, 15, 11, 0, 1, 62, 1, 168, 1, 152, 2, 164, 1, 162, 1, 160, 1, 156,
  4, 141, 4, 16, 5, 17, 1, 136, 1, 18, 3, 136, 1, 35, 1, 17, 1, 136, 8, 0, 1, 40, 1, 105, 1, 50, 1, 40, 83, 0, 1, 62,
  1, 50, 1, 62, 7, 0, 1, 66, 1, 16, 1, 141, 1, 16, 5, 17, 2, 136, 1, 35, 1, 136, 1, 174, 1, 35, 1, 174, 4, 10, 1, 79,
  1, 17, 1, 51, 5, 0, 1, 123, 1, 106, 2, 120, 1, 132, 2, 120, 1, 106, 1, 116, 11, 0, 1, 89, 1, 128, 1, 119, 3, 118, 2, 119,
  1, 106, 1, 116, 4, 0, 1, 14, 1, 37, 1, 69, 4, 55, 1, 69, 1, 37, 1, 80, 1, 51, 11, 0, 1, 66, 1, 18, 1, 38, 1, 55,
  2, 69, 1, 55, 1, 69, 2, 55, 1, 38, 1, 51, 10, 0, 1, 62, 1, 168, 2, 164, 2, 160, 1, 12, 4, 141, 4, 16, 4, 17, 1, 18,
  1, 136, 1, 18, 4, 136, 1, 174, 1, 17, 1, 10, 8, 0, 1, 40, 2, 105, 1, 40, 83, 0, 1, 62, 1, 50, 1, 62, 7, 0, 1, 57,
  1, 16, 1, 141, 1, 163, 1, 16, 1, 170, 1, 166, 1, 136, 1, 18, 1, 170, 1, 180, 1, 175, 1, 174, 1, 180, 1, 136, 1, 174, 1, 10,
  1, 179, 1, 67, 1, 10, 1, 67, 1, 18, 1, 51, 5, 0, 1, 123, 1, 106, 2, 120, 1, 121, 2, 120, 1, 106, 1, 116, 12, 0, 1, 92,
  1, 106, 2, 119, 2, 118, 1, 119, 1, 106, 1, 135, 3, 0, 1, 51, 1, 38, 1, 54, 4, 55, 1, 69, 1, 37, 1, 69, 1, 57, 14, 0,
  1, 79, 1, 38, 2, 55, 4, 69, 1, 37, 1, 15, 10, 0, 1, 62, 1, 168, 2, 164, 1, 156, 1, 16, 1, 142, 3, 141, 2, 16, 1, 141,
  1, 17, 1, 136, 2, 17, 2, 136, 1, 171, 1, 17, 3, 136, 1, 35, 1, 136, 1, 35, 1, 17, 1, 10, 8, 0, 1, 40, 2, 105, 1, 40,
  83, 0, 1, 62, 1, 33, 1, 62, 7, 0, 1, 66, 2, 17, 1, 166, 1, 161, 2, 162, 1, 10, 1, 136, 1, 162, 1, 180, 1, 176, 1, 10,
  1, 181, 1, 170, 1, 67, 1, 176, 1, 173, 1, 83, 1, 67, 1, 79, 1, 18, 1, 51, 5, 0, 1, 123, 1, 106, 3, 120, 1, 129, 1, 120,
  1, 106, 1, 116, 12, 0, 1, 103, 1, 106, 2, 119, 1, 107, 1, 118, 1, 119, 1, 106, 1, 135, 3, 0, 1, 35, 1, 37, 5, 55, 1, 54,
  1, 38, 1, 19, 16, 0, 1, 14, 1, 37, 5, 69, 1, 54, 1, 69, 1, 66, 9, 0, 1, 62, 1, 168, 2, 164, 1, 154, 1, 155, 2, 163,
  1, 61, 1, 162, 1, 167, 2, 161, 2, 162, 1, 166, 2, 162, 1, 166, 1, 180, 5, 136, 2, 174, 1, 18, 1, 136, 8, 0, 1, 40, 2, 105,
  1, 40, 83, 0, 1, 51, 1, 50, 1, 62, 7, 0, 1, 66, 2, 16, 1, 171, 1, 167, 1, 170, 1, 171, 1, 136, 1, 10, 1, 173, 1, 180,
  1, 166, 1, 171, 1, 173, 1, 182, 1, 79, 1, 176, 1, 173, 1, 79, 1, 67, 1, 79, 1, 17, 1, 51, 5, 0, 1, 123, 1, 106, 5, 120,
  1, 106, 1, 116, 12, 0, 1, 130, 1, 106, 1, 119, 1, 118, 2, 107, 1, 118, 1, 106, 1, 96, 3, 0, 1, 14, 1, 38, 4, 55, 1, 69,
  1, 37, 1, 35, 17, 0, 1, 51, 1, 37, 1, 55, 5, 69, 1, 38, 1, 68, 9, 0, 1, 62, 1, 168, 1, 164, 1, 142, 1, 162, 1, 22,
  1, 153, 1, 162, 1, 169, 1, 155, 2, 169, 2, 162, 1, 155, 1, 169, 2, 172, 1, 162, 1, 180, 2, 136, 1, 35, 1, 136, 1, 35, 1, 136,
  1, 174, 1, 18, 1, 10, 8, 0, 1, 66, 2, 105, 1, 40, 83, 0, 1, 62, 1, 50, 1, 51, 7, 0, 1, 66, 2, 16, 1, 17, 2, 136,
  1, 17, 1, 18, 1, 136, 1, 10, 1, 180, 1, 175, 1, 171, 1, 35, 1, 79, 1, 10, 1, 174, 1, 175, 1, 10, 1, 67, 1, 79, 1, 18,
  1, 51, 5, 0, 1, 123, 1, 106, 5, 120, 1, 106, 1, 123, 11, 0, 1, 135, 1, 106, 2, 119, 2, 118, 1, 119, 1, 118, 1, 106, 1, 127,
  2, 0, 1, 19, 1, 54, 6, 55, 1, 38, 1, 51, 18, 0, 1, 146, 1, 54, 5, 69, 1, 37, 1, 174, 9, 0, 1, 62, 1, 168, 2, 156,
  1, 12, 1, 161, 1, 13, 1, 165, 1, 163, 1, 161, 1, 163, 1, 171, 1, 167, 1, 178, 1, 167, 1, 180, 1, 170, 1, 171, 1, 166, 1, 170,
  1, 174, 1, 136, 1, 35, 1, 174, 1, 136, 2, 174, 1, 18, 1, 10, 8, 0, 1, 40, 2, 105, 1, 40, 83, 0, 1, 51, 1, 50, 1, 62,
  7, 0, 1, 66, 3, 16, 1, 17, 1, 136, 1, 174, 2, 136, 1, 174, 1, 10, 1, 174, 1, 10, 1, 174, 1, 175, 1, 174, 2, 79, 1, 67,
  1, 10, 1, 79, 1, 18, 1, 51, 5, 0, 1, 123, 1, 106, 4, 120, 1, 107, 1, 106, 1, 116, 2, 0, 1, 57, 1, 0, 3, 57, 1, 0,
  1, 57, 1, 84, 1, 126, 1, 106, 1, 113, 1, 118, 1, 119, 1, 118, 2, 119, 1, 106, 1, 119, 1, 108, 2, 0, 1, 34, 1, 38, 5, 55,
  1, 38, 1, 80, 19, 0, 1, 67, 1, 38, 5, 69, 1, 38, 1, 18, 9, 0, 1, 62, 1, 11, 2, 164, 1, 156, 3, 141, 2, 16, 1, 17,
  3, 136, 1, 17, 3, 136, 1, 10, 2, 136, 1, 174, 1, 136, 1, 35, 3, 174, 1, 18, 1, 10, 8, 0, 1, 40, 1, 83, 1, 105, 1, 40,
  83, 0, 1, 62, 1, 105, 1, 62, 7, 0, 1, 66, 2, 17, 1, 16, 1, 17, 2, 180, 1, 175, 1, 136, 1, 170, 1, 171, 1, 175, 1, 183,
  1, 182, 1, 176, 1, 173, 1, 10, 1, 183, 2, 67, 1, 79, 1, 18, 1, 51, 5, 0, 1, 123, 1, 106, 1, 107, 4, 120, 1, 107, 1, 130,
  1, 102, 1, 90, 7, 95, 1, 128, 1, 106, 1, 119, 1, 107, 1, 118, 1, 107, 2, 119, 1, 118, 1, 106, 1, 112, 3, 0, 1, 68, 1, 37,
  5, 55, 1, 38, 1, 14, 19, 0, 1, 85, 1, 37, 5, 69, 1, 38, 1, 18, 9, 0, 1, 62, 1, 11, 1, 156, 1, 164, 1, 141, 1, 178,
  2, 154, 1, 12, 1, 16, 1, 165, 1, 162, 1, 170, 1, 176, 2, 17, 1, 16, 3, 136, 1, 35, 1, 136, 5, 174, 1, 136, 1, 10, 8, 0,
  1, 40, 1, 83, 1, 105, 1, 40, 83, 0, 1, 51, 1, 50, 1, 51, 7, 0, 1, 66, 4, 17, 1, 172, 1, 162, 1, 181, 1, 169, 1, 177,
  1, 181, 1, 177, 1, 176, 1, 182, 1, 83, 1, 173, 2, 176, 3, 79, 1, 18, 1, 51, 5, 0, 1, 123, 1, 106, 5, 120, 1, 121, 1, 113,
  9, 106, 1, 128, 1, 118, 1, 119, 1, 118, 2, 107, 1, 119, 1, 107, 2, 106, 1, 108, 3, 0, 1, 56, 1, 38, 5, 55, 1, 38, 1, 14,
  19, 0, 1, 85, 1, 37, 5, 69, 1, 38, 1, 18, 9, 0, 1, 62, 1, 11, 1, 141, 1, 12, 1, 163, 1, 154, 1, 178, 1, 161, 2, 16,
  1, 169, 1, 155, 1, 169, 1, 173, 1, 162, 1, 166, 1, 136, 1, 18, 3, 136, 1, 174, 1, 136, 4, 174, 1, 35, 1, 10, 8, 0, 1, 40,
  1, 83, 1, 105, 1, 40, 83, 0, 1, 62, 1, 105, 1, 62, 7, 0, 1, 66, 1, 17, 1, 16, 1, 18, 1, 136, 2, 166, 1, 170, 2, 176,
  1, 175, 1, 166, 2, 180, 1, 184, 1, 182, 1, 184, 1, 176, 2, 79, 1, 85, 1, 18, 1, 51, 5, 0, 1, 123, 1, 106, 7, 120, 10, 107,
  4, 118, 2, 107, 2, 106, 1, 94, 4, 0, 1, 56, 1, 38, 5, 55, 1, 38, 1, 14, 19, 0, 1, 67, 1, 37, 1, 80, 4, 69, 1, 38,
  1, 138, 9, 0, 1, 62, 1, 11, 1, 156, 1, 141, 1, 178, 1, 160, 1, 13, 2, 12, 1, 16, 1, 167, 3, 170, 1, 165, 1, 180, 3, 136,
  1, 35, 7, 174, 1, 35, 1, 10, 8, 0, 1, 40, 1, 83, 1, 105, 1, 66, 83, 0, 1, 51, 1, 50, 1, 51, 7, 0, 1, 66, 1, 17,
  1, 16, 3, 136, 1, 174, 1, 136, 1, 10, 1, 67, 1, 10, 1, 67, 3, 174, 5, 79, 1, 85, 1, 18, 1, 51, 5, 0, 1, 123, 1, 106,
  1, 107, 6, 120, 1, 107, 3, 120, 8, 107, 1, 118, 1, 119, 1, 128, 2, 106, 1, 123, 5, 0, 1, 62, 1, 38, 4, 55, 1, 69, 1, 54,
  1, 69, 1, 57, 18, 0, 1, 52, 1, 38, 1, 69, 1, 80, 3, 69, 1, 38, 1, 18, 9, 0, 1, 62, 1, 11, 2, 141, 1, 12, 1, 141,
  1, 142, 3, 16, 1, 17, 2, 136, 1, 17, 2, 136, 1, 18, 3, 136, 1, 35, 4, 174, 1, 67, 1, 10, 1, 35, 1, 10, 8, 0, 1, 40,
  2, 83, 1, 40, 83, 0, 1, 51, 1, 105, 1, 51, 7, 0, 1, 66, 1, 17, 1, 18, 1, 180, 1, 170, 2, 136, 5, 174, 1, 10, 4, 67,
  3, 79, 1, 85, 1, 136, 1, 51, 5, 0, 1, 123, 1, 106, 9, 120, 11, 107, 2, 119, 1, 103, 1, 93, 6, 0, 1, 66, 1, 69, 1, 54,
  4, 55, 1, 69, 1, 38, 1, 34, 17, 0, 1, 57, 1, 80, 1, 55, 1, 80, 4, 69, 1, 38, 1, 67, 9, 0, 1, 62, 1, 11, 1, 12,
  2, 141, 1, 165, 1, 167, 1, 162, 1, 170, 1, 178, 1, 170, 1, 136, 1, 170, 1, 16, 1, 171, 1, 175, 1, 136, 2, 35, 6, 174, 1, 67,
  1, 174, 1, 35, 1, 10, 8, 0, 1, 40, 2, 83, 1, 40, 83, 0, 1, 51, 1, 105, 1, 62, 7, 0, 1, 66, 2, 17, 1, 176, 1, 162,
  1, 174, 1, 136, 6, 174, 1, 67, 1, 10, 1, 67, 4, 79, 1, 85, 1, 35, 1, 51, 5, 0, 1, 123, 1, 106, 5, 120, 1, 121, 7, 106,
  6, 107, 1, 128, 1, 120, 1, 84, 9, 0, 1, 15, 1, 38, 4, 55, 1, 69, 1, 38, 1, 14, 17, 0, 1, 68, 1, 38, 2, 80, 4, 69,
  1, 54, 1, 51, 9, 0, 1, 62, 1, 11, 2, 141, 1, 161, 1, 169, 2, 162, 1, 167, 1, 169, 1, 162, 1, 167, 1, 162, 1, 173, 1, 180,
  1, 176, 1, 136, 1, 174, 1, 136, 1, 174, 1, 35, 3, 174, 3, 67, 1, 174, 1, 10, 8, 0, 1, 40, 2, 83, 1, 40, 83, 0, 1, 51,
  1, 105, 1, 62, 7, 0, 1, 66, 1, 136, 1, 17, 1, 166, 1, 173, 1, 174, 1, 136, 3, 174, 1, 10, 1, 174, 3, 67, 5, 79, 1, 68,
  1, 35, 1, 51, 5, 0, 1, 123, 1, 106, 1, 107, 4, 120, 1, 113, 1, 91, 1, 96, 4, 135, 1, 122, 1, 106, 1, 120, 4, 107, 1, 118,
  1, 128, 1, 135, 9, 0, 1, 79, 1, 37, 2, 55, 1, 69, 2, 55, 1, 69, 1, 37, 1, 68, 15, 0, 1, 40, 1, 54, 1, 55, 1, 69,
  1, 80, 1, 69, 1, 80, 1, 69, 1, 54, 1, 80, 10, 0, 1, 62, 1, 11, 1, 141, 1, 12, 1, 178, 1, 167, 1, 170, 1, 16, 1, 162,
  1, 169, 1, 18, 1, 170, 1, 163, 1, 175, 1, 176, 1, 166, 1, 35, 1, 174, 1, 35, 5, 174, 1, 67, 1, 10, 1, 67, 1, 174, 1, 10,
  8, 0, 1, 66, 2, 83, 1, 40, 83, 0, 1, 62, 1, 105, 1, 51, 7, 0, 1, 66, 1, 136, 1, 18, 1, 170, 1, 171, 4, 174, 1, 67,
  1, 174, 1, 67, 1, 10, 3, 67, 4, 79, 1, 85, 1, 136, 1, 51, 5, 0, 1, 123, 1, 106, 5, 120, 1, 106, 1, 123, 5, 0, 1, 93,
  2, 106, 1, 120, 3, 107, 1, 120, 1, 119, 1, 106, 1, 96, 8, 0, 1, 40, 1, 55, 1, 54, 2, 55, 1, 69, 1, 55, 1, 69, 1, 54,
  1, 38, 1, 56, 13, 0, 1, 19, 1, 69, 1, 54, 2, 80, 1, 69, 1, 80, 2, 69, 1, 38, 1, 67, 10, 0, 1, 62, 1, 11, 1, 141,
  1, 12, 1, 16, 2, 17, 1, 16, 1, 12, 1, 17, 1, 136, 1, 18, 4, 136, 2, 174, 1, 10, 3, 174, 1, 67, 1, 174, 2, 67, 1, 79,
  1, 174, 1, 79, 8, 0, 1, 66, 2, 83, 1, 40, 83, 0, 1, 51, 1, 105, 1, 51, 7, 0, 1, 66, 1, 136, 1, 18, 1, 136, 2, 174,
  1, 10, 3, 174, 3, 67, 1, 79, 1, 67, 4, 79, 2, 85, 1, 35, 1, 51, 5, 0, 1, 123, 1, 106, 5, 120, 1, 106, 1, 116, 6, 0,
  1, 89, 1, 106, 1, 119, 1, 120, 4, 107, 1, 119, 1, 106, 1, 123, 8, 0, 1, 67, 1, 37, 2, 69, 1, 55, 3, 69, 1, 54, 1, 38,
  1, 174, 11, 0, 1, 34, 1, 69, 1, 54, 4, 80, 1, 69, 1, 80, 1, 54, 1, 55, 11, 0, 1, 62, 1, 11, 2, 141, 1, 12, 2, 170,
  1, 17, 1, 16, 2, 171, 1, 16, 1, 180, 1, 175, 1, 180, 1, 18, 1, 175, 1, 10, 1, 182, 2, 174, 1, 67, 1, 174, 3, 67, 1, 79,
  1, 174, 1, 79, 8, 0, 1, 66, 2, 83, 1, 66, 83, 0, 1, 51, 1, 105, 1, 51, 7, 0, 1, 66, 1, 136, 1, 18, 1, 174, 2, 10,
  1, 67, 2, 174, 1, 67, 1, 10, 1, 79, 2, 85, 2, 79, 1, 85, 1, 68, 1, 79, 2, 85, 1, 35, 1, 51, 5, 0, 1, 123, 1, 106,
  5, 120, 1, 106, 1, 114, 7, 0, 1, 116, 1, 106, 1, 118, 5, 107, 1, 128, 1, 106, 1, 89, 7, 0, 1, 57, 1, 55, 1, 38, 2, 69,
  1, 55, 3, 69, 1, 55, 1, 38, 1, 80, 1, 79, 1, 19, 1, 66, 3, 0, 1, 57, 1, 19, 1, 34, 1, 14, 1, 54, 1, 55, 7, 80,
  1, 37, 1, 56, 11, 0, 1, 51, 1, 11, 1, 12, 1, 16, 1, 161, 1, 172, 5, 162, 2, 166, 1, 173, 1, 177, 1, 173, 2, 176, 1, 172,
  1, 182, 6, 67, 1, 79, 1, 174, 1, 79, 8, 0, 1, 66, 2, 83, 1, 40, 83, 0, 1, 51, 1, 105, 1, 51, 7, 0, 1, 66, 1, 17,
  1, 174, 1, 176, 1, 173, 1, 175, 1, 180, 1, 173, 1, 176, 1, 173, 2, 179, 1, 185, 1, 186, 1, 179, 1, 184, 1, 187, 1, 186, 1, 68,
  1, 79, 1, 68, 1, 136, 1, 51, 5, 0, 1, 123, 1, 106, 5, 120, 1, 106, 1, 116, 8, 0, 1, 104, 1, 106, 1, 120, 4, 107, 1, 120,
  1, 106, 1, 128, 1, 84, 7, 0, 1, 39, 1, 37, 1, 54, 7, 69, 1, 54, 1, 38, 1, 55, 1, 80, 3, 15, 1, 14, 1, 69, 1, 38,
  1, 54, 8, 80, 1, 38, 1, 15, 12, 0, 1, 62, 1, 11, 2, 16, 1, 178, 1, 162, 1, 167, 3, 166, 1, 162, 1, 166, 1, 170, 1, 173,
  1, 166, 2, 176, 1, 166, 1, 173, 1, 179, 4, 67, 3, 79, 1, 174, 1, 79, 8, 0, 1, 66, 2, 83, 1, 66, 83, 0, 1, 51, 1, 83,
  1, 51, 7, 0, 1, 57, 1, 17, 1, 136, 1, 173, 2, 177, 1, 169, 1, 173, 2, 181, 1, 185, 1, 176, 1, 181, 1, 177, 1, 185, 1, 173,
  1, 188, 1, 172, 1, 189, 1, 85, 1, 68, 1, 35, 1, 51, 5, 0, 1, 123, 1, 106, 5, 120, 1, 106, 1, 116, 9, 0, 1, 90, 1, 106,
  1, 120, 4, 107, 1, 120, 1, 106, 1, 113, 1, 108, 7, 0, 1, 67, 1, 37, 1, 55, 9, 69, 1, 55, 4, 54, 1, 69, 9, 80, 1, 38,
  1, 14, 1, 57, 12, 0, 1, 51, 1, 11, 2, 16, 1, 12, 1, 16, 1, 18, 1, 16, 1, 15, 3, 18, 1, 10, 1, 174, 1, 35, 1, 10,
  3, 174, 1, 67, 1, 10, 1, 67, 1, 79, 1, 67, 3, 79, 1, 67, 1, 79, 8, 0, 1, 40, 1, 62, 1, 83, 1, 40, 83, 0, 1, 51,
  1, 83, 1, 51, 7, 0, 1, 66, 1, 136, 1, 18, 1, 180, 3, 182, 1, 179, 3, 182, 1, 176, 1, 179, 1, 173, 1, 79, 1, 184, 1, 187,
  1, 190, 1, 189, 2, 68, 1, 35, 1, 51, 5, 0, 1, 123, 1, 106, 4, 120, 1, 129, 1, 106, 1, 116, 9, 0, 1, 108, 2, 106, 1, 120,
  3, 107, 2, 120, 1, 106, 1, 92, 8, 0, 1, 67, 1, 37, 1, 54, 9, 69, 1, 80, 1, 69, 1, 80, 1, 69, 9, 80, 1, 38, 1, 14,
  1, 57, 13, 0, 1, 62, 1, 13, 2, 16, 2, 165, 1, 15, 1, 16, 1, 18, 1, 10, 1, 136, 1, 10, 2, 180, 1, 175, 1, 10, 1, 67,
  2, 174, 4, 67, 3, 79, 1, 85, 2, 67, 8, 0, 1, 66, 1, 83, 1, 56, 1, 66, 83, 0, 1, 51, 1, 83, 1, 51, 7, 0, 1, 66,
  1, 174, 1, 136, 1, 35, 1, 10, 4, 79, 1, 67, 3, 79, 2, 85, 2, 68, 1, 56, 1, 68, 1, 85, 1, 68, 1, 35, 1, 51, 5, 0,
  1, 123, 1, 106, 4, 120, 1, 122, 1, 106, 1, 116, 10, 0, 1, 89, 1, 106, 1, 113, 3, 120, 2, 107, 1, 120, 1, 106, 1, 126, 8, 0,
  1, 68, 2, 38, 1, 69, 1, 80, 4, 69, 1, 80, 1, 69, 11, 80, 1, 54, 1, 38, 1, 18, 15, 0, 1, 62, 1, 13, 2, 16, 1, 170,
  1, 166, 1, 178, 1, 162, 1, 176, 1, 173, 1, 169, 1, 166, 1, 176, 1, 180, 2, 182, 1, 179, 1, 35, 3, 67, 6, 79, 1, 67, 1, 79,
  8, 0, 1, 66, 1, 62, 1, 83, 1, 40, 83, 0, 1, 58, 1, 83, 1, 58, 7, 0, 1, 66, 1, 136, 1, 35, 2, 174, 1, 35, 2, 10,
  1, 67, 1, 85, 3, 79, 2, 68, 1, 56, 3, 85, 1, 68, 1, 56, 1, 174, 1, 51, 5, 0, 1, 114, 1, 106, 5, 107, 1, 106, 1, 96,
  11, 0, 1, 114, 1, 106, 1, 119, 1, 107, 3, 119, 1, 107, 1, 119, 1, 106, 1, 96, 8, 0, 1, 19, 1, 15, 1, 38, 1, 37, 1, 54,
  1, 69, 1, 80, 2, 69, 10, 80, 1, 54, 1, 38, 1, 80, 1, 34, 16, 0, 1, 51, 1, 13, 1, 16, 1, 15, 1, 18, 1, 170, 1, 167,
  1, 162, 1, 166, 1, 161, 1, 169, 1, 173, 1, 176, 1, 175, 3, 182, 1, 179, 1, 67, 5, 79, 1, 72, 2, 85, 1, 67, 1, 79, 8, 0,
  1, 66, 1, 56, 1, 41, 1, 66, 83, 0, 1, 58, 1, 56, 1, 51, 7, 0, 1, 66, 1, 174, 1, 136, 1, 174, 1, 180, 1, 175, 1, 176,
  2, 67, 1, 182, 1, 184, 1, 79, 1, 183, 1, 182, 1, 187, 1, 184, 2, 85, 1, 68, 1, 85, 1, 56, 1, 174, 1, 51, 5, 0, 1, 123,
  7, 106, 1, 116, 12, 0, 1, 126, 9, 106, 1, 123, 9, 0, 1, 39, 1, 18, 1, 80, 3, 38, 1, 54, 1, 55, 5, 69, 3, 54, 1, 55,
  1, 146, 1, 85, 18, 0, 1, 62, 1, 13, 2, 16, 1, 17, 1, 15, 1, 170, 1, 15, 2, 18, 1, 174, 1, 67, 1, 175, 1, 18, 1, 180,
  1, 174, 1, 183, 3, 67, 1, 79, 1, 72, 1, 79, 1, 85, 1, 79, 2, 85, 2, 79, 8, 0, 1, 66, 1, 62, 1, 56, 1, 40, 83, 0,
  1, 58, 1, 56, 1, 58, 7, 0, 1, 57, 2, 174, 1, 166, 1, 176, 2, 182, 1, 79, 1, 183, 1, 177, 2, 185, 1, 188, 1, 181, 2, 185,
  1, 85, 3, 68, 1, 56, 1, 174, 1, 51, 5, 0, 1, 81, 7, 93, 1, 108, 13, 0, 8, 93, 1, 89, 1, 93, 11, 0, 1, 57, 1, 34,
  1, 79, 1, 35, 1, 14, 5, 80, 1, 14, 1, 15, 1, 67, 1, 68, 1, 51, 20, 0, 1, 51, 1, 13, 1, 16, 1, 15, 2, 16, 1, 18,
  1, 174, 1, 175, 1, 35, 1, 174, 1, 136, 1, 35, 1, 18, 1, 180, 1, 67, 2, 79, 1, 67, 3, 79, 1, 72, 4, 85, 1, 72, 1, 79,
  8, 0, 1, 66, 1, 62, 1, 41, 1, 57, 83, 0, 1, 58, 1, 34, 1, 58, 7, 0, 1, 66, 1, 174, 1, 67, 1, 180, 1, 175, 1, 179,
  1, 79, 1, 67, 1, 10, 2, 182, 1, 185, 3, 189, 1, 187, 1, 85, 3, 68, 1, 56, 1, 174, 1, 51, 53, 0, 1, 66, 4, 40, 1, 57,
  24, 0, 1, 62, 1, 13, 1, 17, 1, 18, 1, 178, 1, 169, 1, 176, 1, 182, 1, 180, 1, 173, 1, 162, 1, 166, 1, 173, 2, 182, 1, 173,
  1, 181, 2, 173, 1, 187, 1, 85, 2, 79, 1, 72, 3, 85, 1, 79, 1, 85, 8, 0, 1, 66, 1, 62, 1, 34, 1, 66, 83, 0, 1, 19,
  1, 62, 1, 58, 7, 0, 1, 66, 2, 174, 5, 67, 2, 79, 1, 174, 1, 183, 1, 68, 3, 56, 2, 85, 2, 68, 1, 56, 1, 174, 1, 51,
  83, 0, 1, 51, 1, 13, 1, 18, 1, 136, 1, 166, 1, 169, 2, 173, 1, 180, 1, 162, 1, 172, 1, 177, 1, 173, 2, 179, 1, 173, 1, 177,
  1, 173, 1, 185, 1, 186, 1, 68, 1, 79, 4, 85, 1, 68, 1, 72, 1, 79, 8, 0, 1, 66, 1, 34, 1, 62, 1, 66, 83, 0, 1, 58,
  1, 34, 1, 19, 7, 0, 1, 66, 2, 174, 1, 79, 1, 85, 1, 79, 1, 67, 1, 79, 2, 85, 1, 68, 1, 56, 1, 85, 1, 68, 4, 56,
  2, 68, 1, 56, 1, 67, 1, 51, 83, 0, 1, 51, 1, 13, 1, 17, 4, 18, 1, 15, 1, 180, 1, 175, 1, 174, 1, 179, 1, 35, 1, 179,
  1, 175, 1, 174, 1, 183, 1, 85, 3, 79, 5, 85, 1, 68, 2, 85, 8, 0, 1, 66, 2, 62, 1, 66, 83, 0, 1, 19, 1, 62, 1, 19,
  7, 0, 1, 66, 1, 10, 1, 67, 1, 182, 2, 176, 1, 183, 1, 176, 1, 184, 1, 186, 1, 187, 1, 186, 1, 184, 1, 1, 1, 182, 1, 186,
  1, 189, 1, 190, 1, 34, 2, 56, 1, 174, 1, 51, 15, 0, 1, 84, 1, 89, 1, 94, 3, 127, 1, 123, 2, 127, 1, 123, 11, 127, 1, 114,
  1, 93, 12, 0, 1, 84, 1, 89, 6, 127, 4, 94, 1, 127, 3, 94, 1, 127, 2, 94, 1, 123, 1, 89, 12, 0, 1, 51, 1, 13, 2, 18,
  1, 136, 1, 35, 1, 17, 2, 18, 1, 174, 1, 67, 1, 79, 1, 85, 1, 79, 1, 174, 1, 67, 2, 79, 2, 85, 1, 72, 4, 85, 2, 68,
  1, 72, 1, 79, 8, 0, 1, 66, 2, 62, 1, 66, 83, 0, 1, 19, 1, 62, 1, 19, 7, 0, 1, 66, 1, 10, 1, 85, 1, 173, 2, 172,
  1, 177, 1, 172, 1, 1, 1, 185, 1, 188, 2, 181, 1, 172, 1, 185, 1, 181, 1, 191, 1, 1, 1, 34, 1, 56, 1, 34, 1, 67, 1, 51,
  12, 0, 1, 89, 1, 126, 1, 103, 1, 107, 21, 106, 1, 124, 9, 0, 1, 93, 1, 135, 1, 103, 1, 113, 19, 106, 1, 107, 12, 0, 1, 51,
  1, 14, 1, 17, 2, 18, 1, 166, 2, 170, 1, 18, 1, 174, 2, 176, 1, 185, 1, 176, 1, 79, 2, 67, 2, 79, 2, 85, 1, 72, 3, 85,
  2, 68, 2, 85, 8, 0, 1, 57, 1, 62, 1, 39, 1, 57, 83, 0, 1, 40, 1, 51, 1, 40, 7, 0, 1, 66, 1, 174, 1, 79, 1, 182,
  1, 176, 1, 173, 1, 176, 1, 1, 1, 179, 5, 186, 1, 184, 1, 1, 1, 187, 1, 191, 2, 56, 1, 34, 1, 67, 1, 51, 10, 0, 1, 94,
  1, 92, 3, 106, 1, 119, 3, 120, 13, 107, 3, 120, 1, 107, 1, 106, 1, 112, 8, 0, 1, 158, 3, 106, 1, 118, 1, 120, 2, 145, 1, 121,
  1, 122, 2, 132, 1, 129, 8, 120, 1, 132, 1, 122, 1, 106, 1, 102, 12, 0, 1, 51, 1, 13, 2, 18, 1, 165, 1, 176, 1, 180, 1, 176,
  1, 174, 1, 183, 2, 173, 1, 177, 1, 173, 1, 85, 3, 79, 6, 85, 3, 68, 2, 85, 8, 0, 1, 57, 1, 51, 1, 62, 1, 57, 83, 0,
  1, 40, 1, 51, 1, 40, 7, 0, 1, 57, 2, 174, 3, 79, 1, 85, 1, 68, 2, 85, 3, 68, 4, 56, 1, 34, 1, 68, 1, 56, 1, 34,
  1, 67, 1, 51, 9, 0, 1, 104, 2, 106, 1, 120, 1, 121, 5, 120, 13, 107, 2, 120, 2, 107, 1, 106, 1, 158, 7, 0, 1, 124, 1, 106,
  1, 119, 4, 122, 2, 121, 2, 122, 3, 121, 3, 120, 3, 121, 1, 129, 1, 120, 1, 121, 1, 122, 1, 128, 1, 92, 12, 0, 1, 51, 1, 14,
  1, 35, 1, 18, 1, 170, 1, 18, 1, 35, 5, 174, 3, 67, 1, 79, 1, 85, 1, 79, 1, 72, 2, 85, 1, 68, 1, 85, 4, 68, 2, 85,
  8, 0, 1, 57, 2, 51, 1, 57, 83, 0, 1, 40, 1, 19, 1, 66, 7, 0, 1, 66, 1, 67, 1, 174, 1, 79, 6, 85, 1, 79, 1, 85,
  2, 68, 6, 56, 1, 34, 1, 67, 1, 51, 7, 0, 1, 57, 1, 91, 1, 106, 1, 118, 10, 120, 15, 107, 1, 106, 1, 112, 6, 0, 1, 112,
  1, 106, 1, 130, 1, 122, 1, 121, 1, 129, 2, 122, 1, 132, 1, 120, 1, 121, 1, 145, 2, 121, 5, 120, 2, 121, 1, 129, 2, 120, 1, 121,
  1, 128, 1, 92, 1, 57, 11, 0, 1, 51, 1, 12, 2, 18, 1, 35, 1, 18, 1, 174, 1, 67, 2, 35, 1, 174, 1, 67, 2, 85, 2, 79,
  1, 85, 1, 72, 3, 85, 5, 68, 1, 56, 2, 85, 8, 0, 1, 57, 1, 19, 1, 58, 1, 57, 84, 0, 1, 58, 1, 19, 7, 0, 1, 57,
  1, 174, 1, 67, 1, 187, 1, 79, 2, 68, 1, 189, 1, 85, 1, 186, 1, 179, 1, 187, 1, 56, 1, 68, 6, 56, 1, 34, 1, 67, 1, 51,
  7, 0, 1, 92, 1, 106, 1, 122, 10, 120, 15, 107, 1, 120, 1, 106, 1, 126, 5, 0, 1, 93, 1, 106, 1, 119, 2, 122, 1, 121, 1, 129,
  1, 145, 2, 122, 1, 121, 1, 132, 2, 122, 1, 132, 2, 120, 2, 192, 2, 121, 1, 120, 2, 121, 2, 122, 1, 128, 1, 102, 12, 0, 1, 51,
  1, 14, 1, 18, 2, 35, 1, 175, 1, 182, 1, 173, 2, 174, 1, 180, 1, 176, 1, 183, 1, 184, 1, 68, 1, 187, 2, 67, 1, 184, 1, 189,
  6, 68, 1, 56, 1, 68, 1, 85, 8, 0, 1, 57, 1, 51, 1, 40, 84, 0, 1, 19, 1, 14, 1, 62, 7, 0, 1, 66, 1, 67, 1, 79,
  1, 185, 1, 172, 1, 188, 1, 181, 2, 185, 1, 173, 1, 187, 1, 186, 1, 193, 1, 34, 1, 56, 1, 68, 4, 56, 1, 34, 1, 79, 1, 51,
  6, 0, 1, 112, 1, 106, 2, 132, 6, 120, 1, 128, 20, 106, 1, 124, 5, 0, 1, 135, 1, 106, 1, 122, 2, 121, 1, 132, 2, 122, 1, 107,
  18, 106, 1, 120, 12, 0, 1, 51, 1, 14, 2, 35, 1, 175, 1, 182, 1, 173, 1, 169, 2, 67, 1, 176, 1, 182, 2, 173, 1, 181, 1, 185,
  1, 177, 1, 182, 1, 185, 1, 186, 1, 56, 3, 68, 3, 56, 2, 68, 9, 0, 1, 3, 1, 48, 83, 0, 1, 19, 1, 14, 1, 69, 1, 51,
  7, 0, 1, 57, 2, 79, 1, 179, 1, 188, 1, 173, 2, 181, 1, 186, 1, 176, 1, 187, 1, 193, 1, 194, 6, 56, 2, 34, 1, 79, 1, 51,
  5, 0, 1, 93, 1, 106, 1, 118, 5, 120, 1, 121, 1, 113, 1, 106, 1, 120, 1, 91, 4, 104, 1, 126, 3, 104, 1, 126, 1, 104, 1, 126,
  4, 104, 1, 126, 2, 104, 1, 124, 1, 127, 5, 0, 1, 124, 1, 106, 1, 120, 1, 129, 1, 120, 1, 121, 1, 122, 1, 107, 1, 128, 1, 124,
  2, 116, 2, 96, 1, 135, 4, 96, 1, 135, 4, 158, 1, 135, 1, 158, 1, 126, 1, 116, 12, 0, 1, 51, 1, 14, 3, 35, 1, 180, 1, 182,
  1, 180, 1, 67, 1, 52, 1, 182, 1, 184, 1, 186, 1, 182, 1, 185, 1, 179, 1, 182, 1, 187, 1, 190, 1, 194, 3, 68, 4, 56, 2, 68,
  9, 0, 1, 171, 1, 23, 1, 4, 81, 0, 1, 51, 1, 14, 2, 146, 1, 39, 7, 0, 1, 57, 2, 67, 1, 85, 2, 68, 1, 85, 1, 79,
  1, 56, 1, 85, 1, 194, 2, 56, 1, 68, 4, 56, 3, 34, 1, 79, 1, 51, 5, 0, 1, 92, 1, 106, 1, 122, 4, 120, 1, 130, 1, 106,
  1, 113, 1, 96, 1, 108, 25, 0, 1, 81, 1, 130, 1, 113, 1, 120, 2, 132, 1, 145, 1, 120, 1, 106, 1, 114, 31, 0, 1, 51, 1, 15,
  4, 35, 1, 52, 3, 67, 1, 79, 1, 85, 1, 56, 1, 85, 2, 68, 1, 34, 2, 56, 2, 68, 5, 56, 1, 34, 1, 56, 1, 68, 9, 0,
  1, 86, 1, 26, 1, 23, 1, 3, 79, 0, 1, 34, 1, 80, 1, 146, 1, 140, 1, 146, 1, 51, 7, 0, 1, 57, 2, 79, 4, 85, 2, 68,
  1, 34, 1, 56, 1, 34, 1, 68, 2, 56, 2, 34, 1, 39, 3, 34, 1, 72, 1, 51, 4, 0, 1, 93, 1, 106, 1, 120, 1, 122, 1, 121,
  2, 120, 1, 132, 1, 106, 1, 122, 1, 84, 27, 0, 1, 84, 2, 119, 1, 120, 1, 132, 2, 122, 1, 113, 1, 130, 32, 0, 1, 51, 1, 15,
  3, 35, 1, 18, 1, 174, 1, 67, 1, 183, 1, 187, 1, 72, 1, 174, 1, 182, 1, 184, 1, 85, 1, 190, 1, 186, 1, 68, 1, 184, 1, 189,
  1, 190, 1, 189, 1, 68, 1, 56, 1, 34, 1, 56, 1, 34, 1, 56, 1, 68, 9, 0, 1, 86, 3, 26, 1, 48, 77, 0, 1, 83, 1, 80,
  3, 140, 1, 146, 1, 51, 7, 0, 1, 57, 2, 79, 1, 187, 1, 182, 1, 85, 1, 68, 1, 179, 1, 187, 1, 194, 1, 195, 2, 189, 1, 187,
  2, 194, 1, 195, 1, 193, 1, 56, 2, 34, 1, 79, 1, 51, 4, 0, 1, 126, 1, 106, 2, 132, 2, 120, 1, 129, 1, 119, 1, 106, 1, 84,
  28, 0, 1, 143, 1, 119, 1, 113, 1, 132, 3, 122, 1, 107, 1, 128, 1, 93, 31, 0, 1, 51, 1, 15, 1, 35, 1, 174, 1, 18, 2, 35,
  1, 85, 1, 185, 1, 181, 2, 177, 2, 173, 2, 1, 1, 177, 1, 185, 1, 186, 1, 56, 1, 191, 1, 190, 1, 189, 1, 34, 1, 56, 2, 34,
  1, 56, 1, 68, 9, 0, 1, 86, 2, 26, 1, 27, 1, 26, 1, 61, 1, 57, 74, 0, 1, 79, 1, 80, 4, 140, 1, 146, 1, 51, 7, 0,
  1, 57, 1, 79, 1, 68, 1, 185, 1, 177, 2, 181, 1, 185, 1, 193, 2, 188, 1, 177, 1, 1, 1, 184, 3, 1, 1, 188, 1, 39, 2, 34,
  1, 85, 1, 51, 4, 0, 1, 90, 1, 106, 4, 120, 1, 122, 1, 106, 1, 96, 30, 0, 1, 102, 1, 106, 4, 122, 1, 130, 1, 119, 1, 120,
  1, 123, 2, 93, 3, 84, 1, 143, 1, 108, 1, 57, 22, 0, 1, 51, 1, 15, 2, 174, 2, 35, 1, 52, 1, 67, 1, 176, 1, 182, 2, 186,
  1, 189, 2, 179, 1, 184, 1, 187, 1, 193, 1, 186, 1, 184, 1, 190, 1, 195, 1, 194, 1, 34, 1, 56, 2, 34, 2, 56, 9, 0, 1, 86,
  1, 24, 3, 44, 1, 26, 1, 75, 1, 40, 72, 0, 1, 35, 1, 80, 5, 140, 1, 146, 1, 39, 7, 0, 1, 57, 2, 79, 1, 182, 1, 186,
  2, 185, 1, 187, 1, 193, 1, 1, 1, 186, 1, 173, 2, 190, 1, 186, 1, 195, 1, 193, 1, 191, 1, 39, 2, 34, 1, 85, 1, 51, 3, 0,
  1, 84, 1, 119, 1, 107, 1, 129, 3, 120, 1, 119, 1, 118, 1, 108, 10, 0, 1, 127, 1, 112, 11, 135, 1, 158, 1, 135, 1, 81, 4, 0,
  1, 126, 1, 106, 1, 130, 3, 122, 1, 130, 1, 117, 1, 119, 1, 106, 1, 113, 2, 119, 1, 107, 1, 120, 1, 130, 1, 117, 1, 102, 1, 91,
  1, 124, 1, 112, 1, 114, 1, 89, 17, 0, 1, 51, 1, 15, 1, 52, 1, 174, 1, 52, 2, 174, 1, 67, 1, 52, 1, 85, 2, 56, 1, 68,
  2, 85, 1, 56, 2, 34, 1, 68, 1, 194, 1, 56, 2, 34, 1, 56, 3, 34, 2, 56, 9, 0, 1, 86, 2, 26, 1, 44, 1, 125, 1, 45,
  1, 26, 1, 147, 1, 58, 69, 0, 1, 57, 1, 52, 1, 80, 1, 138, 5, 140, 1, 146, 1, 51, 7, 0, 1, 66, 1, 79, 1, 67, 1, 85,
  1, 68, 2, 56, 4, 68, 1, 56, 1, 68, 2, 56, 1, 34, 2, 39, 3, 34, 1, 72, 1, 58, 3, 0, 1, 89, 1, 106, 1, 120, 1, 121,
  3, 120, 1, 106, 1, 104, 11, 0, 1, 102, 14, 106, 1, 84, 4, 0, 1, 89, 1, 106, 1, 120, 3, 122, 3, 130, 1, 122, 1, 129, 2, 120,
  3, 107, 1, 118, 1, 119, 1, 128, 4, 106, 1, 91, 1, 123, 15, 0, 1, 51, 1, 15, 1, 67, 1, 35, 2, 67, 1, 79, 1, 85, 1, 79,
  1, 72, 1, 67, 1, 183, 1, 67, 1, 72, 1, 85, 2, 194, 1, 68, 1, 56, 1, 34, 3, 56, 4, 34, 2, 56, 9, 0, 1, 3, 1, 26,
  3, 44, 2, 45, 1, 26, 1, 44, 1, 58, 68, 0, 1, 40, 1, 146, 7, 140, 1, 146, 1, 39, 7, 0, 1, 57, 3, 79, 2, 85, 6, 68,
  1, 56, 2, 34, 1, 56, 4, 34, 1, 39, 1, 85, 1, 51, 3, 0, 1, 123, 1, 106, 1, 120, 1, 121, 1, 129, 2, 120, 1, 106, 1, 158,
  11, 0, 1, 124, 1, 106, 8, 120, 2, 132, 1, 120, 1, 119, 1, 122, 1, 143, 5, 0, 1, 103, 1, 106, 4, 130, 4, 122, 3, 130, 1, 122,
  4, 130, 1, 122, 3, 130, 2, 106, 1, 103, 1, 81, 13, 0, 1, 51, 1, 15, 2, 67, 1, 176, 1, 181, 1, 182, 1, 184, 1, 176, 1, 185,
  1, 173, 1, 179, 1, 186, 2, 185, 1, 193, 1, 186, 1, 188, 1, 1, 1, 195, 1, 34, 1, 56, 6, 34, 1, 56, 9, 0, 1, 86, 1, 26,
  1, 44, 3, 45, 1, 125, 1, 45, 1, 31, 1, 33, 68, 0, 1, 66, 3, 140, 1, 138, 4, 140, 1, 146, 1, 51, 7, 0, 1, 57, 1, 85,
  2, 79, 1, 67, 1, 187, 1, 183, 2, 68, 1, 79, 1, 194, 1, 34, 1, 39, 3, 34, 1, 56, 3, 34, 1, 39, 1, 68, 1, 19, 3, 0,
  1, 114, 1, 106, 4, 120, 1, 121, 1, 106, 1, 135, 11, 0, 1, 124, 1, 106, 9, 120, 1, 129, 1, 121, 1, 119, 1, 120, 1, 134, 5, 0,
  1, 143, 1, 122, 1, 106, 3, 130, 2, 122, 1, 130, 2, 122, 2, 130, 4, 122, 7, 130, 1, 106, 1, 95, 1, 81, 12, 0, 1, 51, 1, 15,
  1, 67, 1, 72, 1, 176, 1, 177, 1, 173, 1, 185, 1, 1, 1, 181, 1, 173, 1, 182, 1, 186, 1, 173, 1, 1, 1, 56, 1, 185, 1, 1,
  1, 188, 1, 195, 1, 34, 1, 56, 4, 34, 1, 39, 2, 56, 9, 0, 1, 3, 1, 26, 1, 44, 5, 45, 1, 26, 1, 49, 68, 0, 1, 66,
  1, 138, 4, 140, 1, 138, 1, 140, 1, 138, 1, 146, 1, 51, 7, 0, 1, 57, 1, 79, 1, 72, 1, 183, 1, 184, 1, 187, 1, 1, 1, 56,
  1, 189, 1, 181, 1, 1, 2, 191, 1, 185, 1, 190, 1, 185, 1, 195, 3, 34, 1, 39, 1, 85, 1, 19, 3, 0, 1, 123, 1, 106, 4, 120,
  1, 121, 1, 106, 1, 135, 11, 0, 1, 103, 1, 106, 6, 120, 2, 129, 1, 132, 1, 129, 1, 120, 1, 119, 1, 120, 1, 143, 6, 0, 1, 108,
  1, 91, 2, 106, 2, 122, 2, 130, 10, 122, 5, 130, 1, 117, 1, 106, 1, 104, 12, 0, 1, 51, 1, 15, 1, 67, 1, 52, 1, 183, 1, 179,
  2, 72, 1, 79, 1, 72, 1, 189, 1, 184, 1, 72, 1, 187, 1, 68, 1, 56, 1, 189, 1, 56, 1, 195, 7, 34, 1, 39, 1, 34, 1, 56,
  9, 0, 1, 3, 1, 26, 3, 45, 1, 115, 1, 45, 1, 115, 1, 27, 1, 49, 68, 0, 1, 66, 6, 140, 1, 138, 1, 140, 1, 146, 1, 51,
  7, 0, 1, 57, 1, 79, 1, 85, 1, 182, 2, 187, 1, 189, 1, 56, 1, 187, 1, 1, 2, 191, 1, 1, 1, 191, 1, 190, 1, 191, 1, 195,
  3, 34, 1, 39, 1, 85, 1, 58, 3, 0, 1, 89, 1, 106, 2, 120, 3, 132, 1, 106, 1, 124, 11, 0, 1, 91, 6, 106, 1, 113, 1, 122,
  1, 132, 1, 121, 1, 129, 1, 120, 1, 119, 1, 120, 1, 143, 8, 0, 1, 116, 1, 102, 5, 106, 2, 128, 1, 113, 3, 119, 1, 118, 1, 107,
  1, 132, 6, 130, 1, 120, 1, 106, 1, 84, 11, 0, 1, 51, 1, 15, 2, 67, 1, 52, 1, 174, 1, 79, 1, 68, 1, 72, 1, 85, 1, 68,
  1, 34, 1, 56, 1, 68, 2, 34, 2, 56, 8, 34, 1, 39, 1, 34, 1, 56, 9, 0, 1, 3, 1, 26, 1, 115, 1, 45, 1, 115, 1, 45,
  1, 115, 1, 45, 1, 27, 1, 49, 68, 0, 1, 66, 1, 138, 2, 140, 1, 138, 1, 140, 3, 138, 1, 146, 1, 39, 7, 0, 1, 57, 1, 85,
  1, 79, 1, 72, 1, 85, 1, 194, 3, 68, 1, 56, 3, 34, 1, 56, 2, 39, 4, 34, 1, 39, 1, 68, 1, 19, 3, 0, 1, 108, 1, 120,
  1, 113, 1, 120, 3, 132, 1, 119, 1, 113, 1, 134, 10, 0, 1, 112, 1, 122, 1, 90, 2, 95, 1, 90, 1, 102, 1, 95, 1, 120, 1, 121,
  2, 120, 1, 121, 1, 119, 1, 120, 1, 134, 10, 0, 1, 89, 1, 123, 1, 96, 1, 104, 1, 103, 1, 91, 1, 92, 1, 90, 1, 117, 1, 130,
  1, 122, 1, 132, 1, 107, 1, 106, 1, 107, 1, 117, 5, 130, 1, 106, 1, 96, 11, 0, 1, 51, 1, 138, 1, 67, 1, 52, 1, 79, 1, 182,
  1, 184, 1, 182, 1, 72, 1, 85, 1, 185, 1, 1, 1, 186, 3, 190, 1, 186, 1, 194, 6, 34, 3, 39, 1, 34, 1, 56, 9, 0, 1, 3,
  1, 26, 6, 115, 1, 27, 1, 49, 68, 0, 1, 66, 3, 140, 5, 138, 1, 146, 1, 51, 7, 0, 1, 57, 2, 79, 1, 68, 1, 85, 1, 68,
  1, 85, 4, 56, 1, 34, 2, 56, 2, 34, 2, 39, 2, 34, 1, 39, 1, 68, 1, 19, 4, 0, 1, 102, 1, 106, 2, 132, 2, 120, 1, 132,
  1, 106, 1, 96, 11, 0, 1, 57, 1, 0, 3, 57, 1, 0, 1, 89, 1, 128, 3, 120, 1, 121, 1, 119, 1, 120, 1, 134, 17, 0, 1, 57,
  1, 81, 1, 143, 1, 134, 2, 84, 1, 89, 1, 130, 1, 107, 5, 130, 1, 106, 1, 91, 11, 0, 1, 51, 1, 18, 1, 67, 1, 72, 1, 179,
  1, 184, 1, 183, 1, 182, 1, 85, 1, 72, 1, 185, 1, 1, 1, 188, 1, 190, 1, 185, 1, 186, 1, 190, 1, 193, 5, 34, 1, 39, 1, 34,
  2, 39, 1, 34, 1, 56, 9, 0, 1, 3, 1, 26, 6, 115, 1, 46, 1, 49, 68, 0, 1, 66, 1, 138, 1, 140, 6, 138, 1, 146, 1, 51,
  7, 0, 1, 57, 2, 85, 1, 56, 1, 85, 1, 68, 1, 56, 1, 194, 1, 56, 2, 34, 2, 56, 3, 34, 2, 196, 1, 39, 1, 34, 1, 39,
  1, 68, 1, 19, 4, 0, 1, 112, 1, 106, 1, 122, 4, 120, 1, 113, 1, 128, 1, 134, 16, 0, 1, 93, 1, 113, 1, 107, 2, 132, 1, 120,
  1, 119, 1, 120, 1, 134, 24, 0, 1, 89, 1, 106, 5, 130, 1, 106, 1, 91, 11, 0, 1, 51, 1, 138, 1, 67, 1, 72, 1, 183, 1, 72,
  1, 79, 1, 67, 1, 85, 1, 72, 1, 85, 1, 68, 1, 56, 1, 194, 2, 68, 1, 194, 1, 195, 4, 34, 1, 39, 1, 34, 3, 39, 2, 34,
  9, 0, 1, 4, 1, 44, 6, 115, 1, 46, 1, 49, 68, 0, 1, 40, 1, 138, 1, 140, 5, 138, 1, 52, 1, 146, 1, 51, 7, 0, 1, 57,
  1, 85, 1, 68, 1, 186, 1, 1, 1, 173, 1, 185, 2, 191, 1, 194, 1, 190, 1, 1, 1, 186, 1, 188, 1, 195, 1, 39, 1, 191, 1, 197,
  1, 195, 1, 34, 1, 39, 1, 68, 1, 19, 4, 0, 1, 93, 1, 106, 1, 107, 4, 120, 1, 122, 1, 106, 1, 122, 1, 84, 15, 0, 1, 93,
  1, 106, 1, 107, 2, 122, 1, 121, 1, 119, 1, 120, 1, 134, 24, 0, 1, 108, 1, 119, 1, 121, 4, 130, 1, 128, 1, 103, 11, 0, 1, 51,
  1, 35, 1, 72, 1, 67, 2, 85, 1, 72, 2, 85, 2, 56, 2, 34, 2, 68, 1, 56, 5, 34, 1, 39, 1, 34, 5, 39, 1, 34, 9, 0,
  1, 4, 1, 44, 2, 115, 1, 87, 1, 115, 1, 88, 1, 115, 1, 45, 1, 49, 68, 0, 1, 66, 1, 35, 1, 140, 3, 138, 1, 52, 1, 138,
  1, 52, 1, 146, 1, 51, 7, 0, 1, 57, 1, 85, 1, 68, 1, 1, 1, 184, 1, 188, 1, 185, 1, 193, 1, 190, 2, 1, 1, 190, 1, 194,
  1, 185, 1, 195, 1, 39, 1, 190, 1, 188, 1, 193, 1, 196, 1, 39, 1, 56, 1, 19, 5, 0, 1, 92, 1, 106, 2, 120, 1, 129, 2, 121,
  1, 122, 1, 106, 1, 113, 1, 114, 1, 143, 13, 0, 1, 89, 1, 106, 1, 120, 1, 132, 2, 122, 1, 107, 1, 119, 1, 84, 24, 0, 1, 96,
  1, 106, 5, 130, 1, 106, 1, 103, 11, 0, 1, 19, 1, 138, 1, 79, 1, 85, 1, 182, 1, 181, 1, 176, 1, 186, 2, 190, 1, 1, 2, 185,
  1, 190, 1, 191, 2, 193, 1, 194, 1, 1, 1, 196, 1, 39, 1, 34, 6, 39, 1, 34, 9, 0, 1, 4, 1, 44, 1, 115, 1, 87, 1, 115,
  2, 87, 1, 42, 1, 46, 1, 49, 68, 0, 1, 66, 3, 138, 1, 52, 1, 138, 1, 52, 1, 138, 1, 52, 1, 146, 1, 62, 7, 0, 1, 57,
  1, 85, 1, 72, 1, 85, 1, 68, 2, 56, 2, 68, 1, 195, 2, 56, 1, 34, 1, 39, 3, 34, 1, 39, 2, 196, 1, 39, 1, 68, 1, 19,
  5, 0, 1, 93, 1, 106, 1, 118, 2, 132, 1, 121, 2, 120, 1, 122, 1, 119, 1, 106, 1, 120, 1, 104, 2, 112, 1, 126, 9, 112, 1, 102,
  1, 107, 1, 122, 1, 132, 1, 122, 1, 130, 1, 119, 1, 120, 1, 143, 4, 0, 1, 93, 1, 135, 4, 96, 1, 116, 6, 96, 1, 116, 3, 96,
  1, 116, 1, 96, 1, 104, 1, 106, 1, 122, 5, 130, 1, 106, 1, 135, 11, 0, 1, 19, 1, 35, 1, 72, 1, 68, 1, 185, 1, 172, 2, 185,
  1, 186, 1, 185, 1, 181, 1, 197, 1, 181, 1, 193, 2, 191, 1, 1, 1, 193, 1, 1, 1, 193, 8, 39, 1, 34, 9, 0, 1, 4, 1, 45,
  1, 115, 2, 87, 1, 42, 1, 87, 1, 88, 1, 46, 1, 20, 68, 0, 1, 66, 1, 35, 1, 138, 2, 52, 1, 138, 3, 52, 1, 146, 1, 51,
  8, 0, 1, 68, 1, 79, 5, 68, 4, 56, 6, 34, 3, 39, 1, 56, 1, 19, 6, 0, 1, 112, 1, 106, 1, 130, 1, 132, 4, 120, 2, 121,
  1, 113, 13, 106, 1, 119, 1, 130, 4, 122, 1, 106, 1, 124, 5, 0, 1, 114, 19, 106, 6, 130, 1, 122, 1, 106, 1, 93, 11, 0, 1, 19,
  1, 35, 2, 72, 1, 67, 1, 184, 2, 189, 2, 187, 2, 189, 1, 34, 1, 196, 1, 195, 1, 194, 1, 187, 2, 195, 1, 196, 2, 39, 1, 34,
  5, 39, 1, 34, 9, 0, 1, 4, 1, 44, 2, 88, 1, 87, 3, 88, 1, 46, 1, 20, 68, 0, 1, 40, 1, 52, 2, 138, 5, 52, 1, 15,
  1, 51, 7, 0, 1, 57, 1, 68, 2, 85, 2, 68, 1, 56, 1, 68, 4, 56, 5, 34, 1, 39, 1, 34, 2, 39, 1, 56, 1, 19, 7, 0,
  1, 92, 1, 106, 1, 122, 6, 120, 1, 121, 1, 132, 5, 120, 1, 192, 6, 132, 2, 122, 2, 130, 2, 122, 1, 106, 1, 116, 5, 0, 1, 127,
  1, 106, 23, 130, 1, 117, 1, 106, 1, 103, 12, 0, 1, 51, 1, 52, 1, 79, 1, 72, 1, 85, 1, 56, 3, 68, 1, 34, 2, 39, 1, 56,
  1, 194, 1, 68, 1, 34, 1, 39, 1, 34, 2, 39, 1, 34, 8, 39, 9, 0, 1, 4, 1, 45, 1, 87, 5, 88, 1, 47, 1, 20, 68, 0,
  1, 66, 1, 35, 1, 138, 6, 52, 1, 140, 1, 62, 8, 0, 1, 68, 1, 79, 4, 68, 4, 56, 5, 34, 3, 39, 1, 34, 1, 39, 1, 56,
  1, 19, 7, 0, 1, 81, 1, 102, 1, 106, 1, 107, 1, 122, 12, 120, 2, 132, 1, 120, 3, 132, 3, 122, 2, 130, 1, 106, 1, 107, 1, 108,
  5, 0, 1, 127, 1, 106, 1, 130, 2, 122, 3, 130, 2, 122, 7, 130, 1, 122, 5, 130, 1, 122, 1, 130, 1, 106, 1, 128, 1, 84, 12, 0,
  1, 19, 1, 52, 1, 72, 1, 85, 1, 187, 1, 186, 1, 187, 1, 184, 2, 189, 1, 191, 1, 190, 1, 186, 1, 187, 1, 186, 1, 193, 1, 39,
  1, 196, 8, 39, 1, 51, 2, 39, 9, 0, 1, 4, 1, 45, 3, 88, 3, 198, 1, 115, 1, 20, 68, 0, 1, 40, 7, 52, 1, 67, 1, 140,
  1, 51, 8, 0, 1, 68, 1, 85, 3, 68, 6, 56, 4, 34, 1, 39, 1, 34, 3, 39, 1, 56, 1, 19, 9, 0, 1, 104, 2, 106, 1, 120,
  1, 122, 13, 120, 2, 132, 5, 122, 2, 106, 1, 93, 6, 0, 1, 127, 1, 106, 1, 130, 1, 122, 2, 130, 1, 122, 1, 130, 3, 122, 9, 130,
  2, 122, 1, 130, 1, 121, 1, 106, 1, 113, 1, 93, 13, 0, 1, 19, 1, 52, 1, 72, 1, 56, 1, 190, 1, 177, 1, 185, 1, 188, 2, 1,
  2, 188, 1, 186, 1, 194, 1, 190, 1, 193, 1, 195, 1, 196, 7, 39, 1, 51, 3, 39, 9, 0, 1, 4, 1, 115, 1, 88, 5, 198, 1, 42,
  1, 20, 68, 0, 1, 40, 2, 52, 1, 72, 1, 52, 1, 72, 1, 52, 2, 72, 1, 138, 1, 62, 8, 0, 1, 68, 1, 85, 3, 68, 4, 56,
  1, 34, 1, 56, 3, 34, 1, 39, 1, 34, 4, 39, 1, 56, 1, 19, 10, 0, 1, 94, 1, 102, 3, 106, 1, 128, 1, 113, 1, 119, 1, 113,
  2, 119, 1, 113, 1, 119, 1, 113, 5, 119, 2, 118, 1, 113, 2, 106, 1, 122, 1, 84, 7, 0, 1, 123, 1, 106, 1, 119, 3, 118, 1, 119,
  1, 118, 3, 119, 1, 118, 2, 107, 2, 118, 2, 107, 1, 118, 2, 119, 1, 128, 2, 106, 1, 92, 1, 134, 14, 0, 1, 19, 1, 52, 3, 85,
  2, 187, 1, 194, 1, 195, 1, 68, 1, 195, 1, 56, 1, 194, 2, 189, 1, 195, 1, 196, 2, 39, 1, 34, 4, 39, 1, 51, 1, 39, 1, 19,
  2, 39, 9, 0, 1, 4, 1, 115, 6, 198, 1, 42, 1, 20, 68, 0, 1, 40, 2, 52, 1, 72, 1, 52, 2, 72, 1, 52, 1, 72, 1, 35,
  1, 51, 8, 0, 1, 68, 1, 67, 1, 79, 3, 85, 5, 68, 3, 56, 6, 34, 1, 56, 1, 40, 12, 0, 1, 89, 1, 112, 1, 91, 1, 95,
  1, 120, 9, 107, 2, 120, 1, 132, 2, 120, 1, 117, 1, 91, 1, 116, 1, 57, 8, 0, 1, 127, 1, 106, 1, 120, 2, 122, 2, 132, 4, 120,
  1, 145, 1, 132, 1, 121, 2, 122, 1, 132, 1, 122, 1, 132, 1, 120, 1, 130, 1, 90, 1, 104, 1, 89, 16, 0, 1, 19, 1, 67, 1, 85,
  1, 72, 1, 68, 1, 34, 4, 56, 1, 34, 1, 51, 1, 39, 1, 34, 2, 39, 1, 51, 1, 19, 6, 39, 1, 51, 4, 39, 9, 0, 1, 4,
  1, 115, 6, 198, 1, 42, 1, 20, 68, 0, 1, 40, 1, 67, 4, 72, 1, 52, 2, 72, 1, 138, 1, 51, 8, 0, 1, 66, 1, 19, 14, 40,
  4, 66, 1, 57, 16, 0, 1, 57, 1, 134, 7, 84, 1, 134, 1, 84, 3, 134, 2, 84, 1, 81, 11, 0, 1, 57, 1, 84, 5, 134, 1, 84,
  12, 134, 1, 108, 1, 57, 18, 0, 1, 40, 1, 72, 1, 85, 1, 68, 1, 187, 1, 194, 1, 34, 2, 68, 1, 56, 1, 195, 1, 1, 2, 193,
  1, 191, 1, 196, 1, 195, 1, 188, 1, 193, 1, 51, 4, 39, 1, 51, 1, 39, 1, 19, 2, 39, 9, 0, 1, 3, 1, 87, 2, 198, 1, 7,
  1, 198, 2, 7, 1, 42, 1, 139, 68, 0, 1, 66, 1, 52, 6, 72, 1, 85, 1, 35, 1, 51, 113, 0, 1, 40, 1, 72, 1, 85, 1, 56,
  1, 194, 1, 188, 1, 185, 1, 34, 1, 56, 1, 34, 1, 39, 3, 188, 1, 199, 1, 197, 1, 188, 1, 193, 1, 195, 5, 39, 1, 51, 3, 39,
  1, 51, 9, 0, 1, 3, 1, 88, 1, 198, 1, 7, 1, 198, 3, 7, 1, 198, 1, 139, 68, 0, 1, 40, 1, 67, 5, 72, 2, 85, 1, 52,
  1, 51, 113, 0, 1, 66, 1, 85, 2, 68, 1, 56, 2, 197, 1, 34, 1, 56, 1, 68, 1, 191, 1, 188, 1, 191, 1, 96, 1, 199, 2, 197,
  1, 193, 1, 195, 1, 51, 2, 39, 1, 51, 1, 39, 1, 19, 1, 39, 1, 19, 2, 39, 9, 0, 1, 86, 1, 87, 1, 198, 5, 7, 1, 42,
  1, 20, 68, 0, 1, 40, 2, 72, 1, 85, 1, 72, 1, 85, 1, 72, 2, 85, 1, 52, 1, 51, 113, 0, 1, 66, 1, 85, 2, 68, 1, 189,
  1, 190, 2, 34, 2, 56, 1, 188, 1, 197, 1, 196, 1, 193, 1, 191, 1, 196, 1, 190, 1, 112, 1, 195, 1, 19, 2, 39, 1, 51, 3, 39,
  1, 51, 2, 39, 9, 0, 1, 86, 1, 88, 6, 7, 1, 198, 1, 139, 68, 0, 1, 40, 1, 79, 1, 85, 1, 68, 1, 72, 1, 68, 1, 85,
  2, 68, 1, 52, 1, 51, 113, 0, 1, 40, 3, 72, 1, 68, 1, 56, 1, 85, 2, 68, 1, 56, 3, 34, 6, 39, 1, 34, 9, 39, 9, 0,
  1, 86, 1, 198, 6, 7, 1, 198, 1, 9, 68, 0, 1, 40, 2, 72, 1, 68, 1, 85, 4, 68, 1, 67, 1, 51, 113, 0, 1, 57, 7, 34,
  9, 39, 1, 19, 1, 39, 1, 19, 1, 39, 1, 19, 1, 39, 4, 19, 1, 51, 1, 19, 9, 0, 1, 86, 1, 198, 1, 6, 3, 7, 2, 59,
  1, 198, 1, 139, 68, 0, 1, 40, 1, 79, 6, 68, 1, 56, 1, 72, 1, 19, 151, 0, 1, 59, 1, 198, 1, 7, 3, 59, 1, 7, 1, 59,
  1, 198, 1, 9, 68, 0, 1, 40, 1, 85, 3, 56, 1, 68, 1, 56, 1, 68, 1, 56, 1, 85, 1, 40, 151, 0, 1, 20, 1, 198, 1, 7,
  1, 59, 1, 7, 3, 59, 1, 7, 1, 9, 68, 0, 1, 40, 1, 85, 6, 56, 1, 68, 1, 56, 7, 0, 1, 57, 1, 0, 2, 57, 1, 0,
  1, 57, 1, 0, 1, 57, 7, 0, 1, 57, 1, 66, 1, 40, 1, 19, 8, 51, 1, 66, 83, 0, 1, 58, 1, 62, 5, 51, 1, 62, 1, 51,
  1, 62, 1, 51, 1, 58, 1, 19, 1, 66, 1, 40, 8, 19, 1, 143, 1, 19, 1, 134, 1, 19, 1, 134, 1, 58, 1, 134, 1, 58, 2, 143,
  1, 40, 1, 139, 1, 7, 1, 59, 1, 7, 4, 59, 1, 7, 1, 9, 68, 0, 1, 40, 1, 68, 7, 56, 1, 40, 1, 79, 6, 13, 4, 178,
  1, 163, 2, 12, 1, 16, 1, 15, 1, 17, 1, 18, 1, 136, 2, 174, 1, 79, 1, 56, 1, 85, 1, 79, 1, 183, 1, 10, 1, 174, 2, 10,
  1, 79, 1, 85, 1, 79, 1, 174, 1, 40, 83, 0, 1, 83, 1, 13, 1, 71, 1, 17, 1, 71, 3, 17, 1, 16, 1, 163, 1, 141, 1, 17,
  1, 142, 5, 160, 2, 167, 1, 48, 1, 167, 8, 48, 1, 188, 1, 48, 2, 4, 1, 87, 7, 59, 1, 7, 1, 9, 68, 0, 1, 66, 1, 68,
  5, 56, 1, 68, 1, 57, 1, 68, 1, 37, 1, 38, 1, 54, 2, 69, 2, 80, 3, 14, 4, 15, 3, 18, 3, 35, 1, 18, 1, 35, 1, 68,
  1, 39, 1, 34, 2, 51, 1, 39, 3, 51, 1, 39, 1, 56, 1, 40, 85, 0, 1, 34, 1, 16, 3, 105, 1, 49, 1, 41, 4, 62, 1, 71,
  1, 8, 2, 63, 1, 50, 1, 59, 1, 49, 1, 33, 4, 49, 1, 20, 1, 157, 2, 139, 5, 9, 1, 84, 1, 75, 1, 115, 7, 59, 1, 9,
  68, 0, 1, 66, 1, 68, 4, 56, 1, 68, 1, 40, 1, 56, 1, 37, 1, 53, 2, 80, 1, 13, 4, 14, 3, 15, 2, 18, 2, 35, 2, 174,
  3, 67, 1, 79, 1, 67, 1, 10, 1, 56, 3, 39, 1, 34, 3, 39, 1, 72, 1, 19, 87, 0, 1, 83, 1, 142, 1, 33, 4, 105, 1, 83,
  1, 41, 1, 50, 2, 63, 1, 50, 1, 63, 3, 33, 4, 49, 1, 41, 1, 20, 1, 139, 1, 20, 2, 139, 4, 9, 1, 58, 1, 40, 1, 5,
  1, 87, 1, 49, 2, 59, 1, 33, 1, 59, 1, 7, 1, 9, 68, 0, 1, 66, 1, 85, 3, 56, 1, 68, 1, 19, 1, 39, 1, 37, 1, 38,
  1, 80, 1, 69, 1, 13, 1, 80, 3, 14, 3, 15, 2, 18, 3, 35, 1, 174, 3, 67, 1, 79, 1, 67, 1, 79, 1, 67, 1, 79, 1, 34,
  1, 39, 2, 34, 2, 39, 1, 85, 1, 51, 89, 0, 1, 67, 1, 17, 4, 105, 1, 83, 1, 50, 1, 8, 1, 71, 1, 63, 2, 50, 3, 33,
  1, 49, 1, 33, 3, 49, 1, 20, 1, 41, 3, 139, 5, 9, 2, 58, 1, 198, 1, 75, 1, 20, 1, 49, 1, 59, 1, 49, 1, 7, 1, 9,
  69, 0, 1, 56, 1, 72, 2, 56, 1, 40, 1, 19, 1, 37, 1, 38, 1, 69, 1, 55, 3, 80, 3, 14, 2, 15, 2, 18, 2, 35, 2, 174,
  5, 67, 4, 79, 1, 85, 4, 34, 1, 85, 1, 39, 91, 0, 1, 10, 1, 17, 1, 33, 2, 105, 1, 50, 1, 8, 1, 63, 2, 71, 1, 63,
  1, 33, 1, 59, 3, 33, 2, 49, 1, 41, 1, 49, 1, 41, 1, 20, 4, 139, 6, 9, 1, 58, 1, 87, 1, 198, 1, 20, 2, 59, 1, 7,
  1, 40, 69, 0, 1, 57, 1, 85, 1, 52, 2, 19, 1, 38, 1, 37, 3, 69, 1, 13, 1, 80, 1, 13, 2, 14, 3, 15, 2, 18, 2, 35,
  2, 174, 3, 67, 7, 79, 1, 83, 1, 39, 1, 34, 1, 85, 1, 34, 93, 0, 1, 17, 1, 8, 2, 105, 1, 71, 1, 8, 2, 71, 1, 63,
  2, 50, 3, 33, 4, 49, 1, 41, 1, 20, 1, 41, 1, 20, 2, 139, 7, 9, 2, 58, 1, 88, 1, 7, 1, 49, 1, 7, 1, 9, 71, 0,
  1, 57, 1, 34, 1, 68, 1, 37, 1, 36, 2, 54, 1, 53, 2, 55, 3, 80, 3, 14, 2, 15, 1, 18, 3, 35, 2, 174, 1, 10, 2, 67,
  1, 10, 6, 79, 1, 68, 1, 79, 1, 34, 10, 0, 11, 84, 4, 0, 9, 84, 4, 0, 2, 84, 11, 0, 1, 84, 5, 0, 6, 84, 12, 0,
  3, 114, 1, 93, 15, 0, 1, 57, 2, 16, 4, 8, 2, 71, 3, 63, 4, 33, 3, 49, 1, 41, 1, 20, 1, 41, 1, 20, 4, 139, 5, 9,
  1, 58, 1, 9, 1, 88, 1, 42, 1, 20, 74, 0, 1, 13, 1, 36, 1, 54, 1, 53, 3, 55, 1, 11, 1, 69, 2, 13, 3, 14, 3, 15,
  2, 18, 1, 35, 4, 174, 2, 67, 1, 79, 1, 10, 4, 79, 1, 10, 1, 68, 10, 0, 1, 84, 11, 106, 1, 117, 2, 0, 1, 126, 9, 106,
  1, 84, 2, 0, 1, 84, 2, 106, 1, 93, 9, 0, 1, 126, 1, 106, 1, 126, 3, 0, 1, 84, 6, 106, 1, 119, 1, 92, 1, 114, 7, 0,
  1, 126, 1, 119, 4, 106, 1, 92, 1, 89, 14, 0, 1, 19, 1, 142, 1, 156, 3, 8, 4, 71, 3, 63, 4, 33, 4, 49, 4, 20, 3, 139,
  1, 9, 1, 139, 1, 9, 1, 20, 1, 58, 1, 20, 1, 59, 75, 0, 1, 57, 3, 40, 9, 19, 1, 40, 1, 19, 9, 40, 1, 66, 1, 40,
  2, 66, 1, 40, 1, 66, 3, 40, 12, 0, 5, 117, 1, 106, 1, 119, 3, 117, 1, 95, 1, 124, 2, 0, 1, 126, 1, 106, 1, 120, 7, 117,
  3, 0, 1, 84, 2, 106, 1, 126, 9, 0, 1, 119, 1, 106, 1, 126, 3, 0, 1, 84, 2, 106, 5, 117, 2, 106, 1, 158, 5, 0, 1, 117,
  1, 106, 1, 119, 1, 124, 2, 126, 1, 92, 1, 106, 1, 119, 1, 89, 15, 0, 9, 40, 2, 66, 5, 40, 8, 66, 1, 57, 6, 66, 126, 0,
  1, 84, 1, 106, 1, 117, 7, 0, 1, 126, 1, 106, 1, 126, 10, 0, 1, 84, 2, 106, 1, 120, 8, 0, 1, 158, 2, 106, 1, 126, 3, 0,
  1, 84, 2, 106, 1, 84, 4, 0, 1, 127, 1, 119, 1, 106, 1, 114, 3, 0, 1, 126, 1, 106, 1, 117, 1, 84, 4, 0, 1, 158, 1, 106,
  1, 119, 1, 84, 171, 0, 1, 84, 1, 106, 1, 117, 7, 0, 1, 126, 1, 106, 1, 126, 10, 0, 1, 84, 3, 106, 1, 158, 7, 0, 1, 92,
  2, 106, 1, 126, 3, 0, 1, 84, 2, 106, 1, 84, 5, 0, 1, 158, 1, 106, 1, 124, 2, 0, 1, 93, 1, 106, 1, 119, 1, 84, 6, 0,
  1, 103, 1, 106, 1, 158, 171, 0, 1, 84, 1, 106, 1, 117, 7, 0, 1, 126, 1, 106, 1, 126, 10, 0, 1, 84, 1, 106, 1, 117, 1, 119,
  1, 92, 6, 0, 1, 127, 1, 106, 1, 117, 1, 106, 1, 126, 3, 0, 1, 84, 2, 106, 1, 84, 5, 0, 1, 114, 1, 106, 1, 95, 2, 0,
  1, 158, 1, 106, 1, 124, 7, 0, 1, 127, 1, 106, 1, 117, 171, 0, 1, 84, 1, 106, 1, 117, 7, 0, 1, 126, 1, 106, 1, 126, 10, 0,
  1, 84, 1, 106, 1, 95, 1, 124, 1, 106, 1, 89, 5, 0, 1, 103, 1, 106, 1, 158, 1, 106, 1, 126, 3, 0, 1, 84, 2, 106, 1, 84,
  5, 0, 1, 114, 1, 106, 1, 117, 2, 0, 1, 126, 1, 106, 1, 96, 7, 0, 1, 84, 2, 106, 1, 84, 170, 0, 1, 84, 1, 106, 1, 117,
  7, 0, 1, 126, 1, 106, 1, 126, 6, 84, 4, 0, 1, 84, 1, 106, 1, 117, 1, 89, 1, 106, 1, 103, 4, 0, 1, 84, 1, 119, 1, 103,
  1, 114, 1, 106, 1, 126, 3, 0, 1, 84, 2, 106, 1, 84, 5, 0, 1, 158, 1, 106, 1, 124, 2, 0, 1, 126, 1, 106, 1, 114, 8, 0,
  1, 117, 1, 106, 1, 84, 170, 0, 1, 84, 1, 106, 1, 117, 7, 0, 1, 126, 8, 106, 1, 126, 3, 0, 1, 84, 1, 106, 1, 117, 1, 0,
  1, 103, 1, 119, 1, 84, 3, 0, 1, 158, 1, 106, 2, 114, 1, 106, 1, 126, 3, 0, 1, 84, 2, 106, 1, 84, 3, 0, 1, 84, 1, 158,
  1, 119, 1, 106, 1, 89, 2, 0, 1, 126, 1, 106, 1, 114, 8, 0, 1, 117, 1, 106, 1, 84, 170, 0, 1, 84, 1, 106, 1, 117, 7, 0,
  1, 126, 1, 106, 1, 120, 6, 117, 1, 158, 3, 0, 1, 84, 1, 106, 1, 117, 1, 0, 1, 114, 1, 106, 1, 158, 3, 0, 1, 120, 1, 117,
  1, 0, 1, 114, 1, 106, 1, 126, 3, 0, 1, 84, 2, 106, 4, 117, 3, 106, 1, 158, 3, 0, 1, 126, 1, 106, 1, 114, 8, 0, 1, 117,
  1, 106, 1, 84, 170, 0, 1, 84, 1, 106, 1, 117, 7, 0, 1, 126, 1, 106, 1, 126, 10, 0, 1, 84, 1, 106, 1, 117, 2, 0, 1, 117,
  1, 120, 2, 0, 1, 96, 1, 106, 1, 158, 1, 0, 1, 114, 1, 106, 1, 126, 3, 0, 1, 84, 2, 106, 5, 117, 1, 103, 1, 89, 4, 0,
  1, 126, 1, 106, 1, 114, 8, 0, 1, 117, 1, 106, 1, 84, 170, 0, 1, 84, 1, 106, 1, 117, 7, 0, 1, 126, 1, 106, 1, 126, 10, 0,
  1, 84, 1, 106, 1, 117, 2, 0, 1, 158, 1, 106, 1, 114, 1, 0, 1, 92, 1, 113, 2, 0, 1, 114, 1, 106, 1, 126, 3, 0, 1, 84,
  2, 106, 1, 84, 10, 0, 1, 126, 1, 106, 1, 96, 7, 0, 1, 84, 2, 106, 1, 84, 170, 0, 1, 84, 1, 106, 1, 117, 7, 0, 1, 126,
  1, 106, 1, 126, 10, 0, 1, 84, 1, 106, 1, 117, 3, 0, 1, 113, 1, 92, 1, 89, 1, 106, 1, 126, 2, 0, 1, 114, 1, 106, 1, 126,
  3, 0, 1, 84, 2, 106, 1, 84, 10, 0, 1, 158, 1, 106, 1, 124, 7, 0, 1, 89, 1, 106, 1, 117, 171, 0, 1, 84, 1, 106, 1, 117,
  7, 0, 1, 126, 1, 106, 1, 126, 10, 0, 1, 84, 1, 106, 1, 117, 3, 0, 1, 126, 1, 106, 1, 92, 1, 106, 1, 89, 2, 0, 1, 114,
  1, 106, 1, 126, 3, 0, 1, 84, 2, 106, 1, 84, 10, 0, 1, 89, 1, 106, 1, 121, 1, 84, 6, 0, 1, 126, 1, 106, 1, 126, 171, 0,
  1, 84, 1, 106, 1, 117, 7, 0, 1, 126, 1, 106, 1, 126, 10, 0, 1, 84, 1, 106, 1, 117, 3, 0, 1, 89, 2, 106, 1, 103, 3, 0,
  1, 114, 1, 106, 1, 126, 3, 0, 1, 84, 2, 106, 1, 84, 11, 0, 1, 103, 1, 106, 1, 124, 1, 84, 4, 0, 1, 158, 1, 106, 1, 119,
  1, 84, 171, 0, 1, 84, 1, 106, 1, 117, 7, 0, 1, 126, 1, 106, 1, 120, 7, 117, 3, 0, 1, 84, 1, 106, 1, 117, 4, 0, 1, 126,
  1, 117, 1, 114, 3, 0, 1, 114, 1, 106, 1, 126, 3, 0, 1, 84, 2, 106, 1, 84, 11, 0, 1, 84, 1, 117, 1, 106, 1, 121, 1, 126,
  1, 96, 1, 126, 1, 92, 2, 106, 1, 96, 172, 0, 1, 84, 1, 106, 1, 117, 7, 0, 1, 126, 9, 106, 1, 84, 2, 0, 1, 84, 1, 106,
  1, 117, 10, 0, 1, 114, 1, 106, 1, 126, 3, 0, 1, 84, 2, 106, 1, 84, 12, 0, 1, 84, 1, 103, 5, 106, 1, 92, 1, 127, 174, 0,
  1, 84, 9, 0, 9, 84, 4, 0, 1, 84, 12, 0, 1, 84, 5, 0, 2, 84, 15, 0, 1, 93, 3, 114, 1, 89, 255, 0, 255, 0, 255, 0,
  255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0, 255, 0,
  255, 0, 255, 0, 255, 0, 238, 0,
};

// Buffer de lignes : évite de consommer 115 kB de RAM pour l'image.
static uint16_t startupImageBuffer[STARTUP_IMAGE_WIDTH * 8];

void drawStartupImage() {

  uint16_t outX = 0;
  uint16_t outY = 0;
  size_t pos = 0;

  while (pos < sizeof(STARTUP_IMAGE_RLE) && outY < STARTUP_IMAGE_HEIGHT) {
    uint8_t count = pgm_read_byte(&STARTUP_IMAGE_RLE[pos++]);
    uint8_t paletteIndex = pgm_read_byte(&STARTUP_IMAGE_RLE[pos++]);
    uint16_t color = pgm_read_word(&STARTUP_IMAGE_PALETTE[paletteIndex]);

    while (count-- && outY < STARTUP_IMAGE_HEIGHT) {
      startupImageBuffer[outX++] = color;
      if (outX >= STARTUP_IMAGE_WIDTH) {
        tft.drawRGBBitmap(0, outY, startupImageBuffer, STARTUP_IMAGE_WIDTH, 1);
        outX = 0;
        outY++;
      }
    }
  }
}

// =====================================================
// Ecran de démarrage
// =====================================================

void startupScreen() {

  tft.setFont();
  tft.setTextWrap(false);
  tft.fillScreen(ST77XX_BLACK);

  // Logo MODELROGS plein écran 240x240.
  drawStartupImage();
}

// =====================================================
// Redémarrage quotidien à 02h15 (heure locale France)
// La date du dernier redémarrage est conservée en EEPROM afin
// d'éviter une boucle de redémarrage après le reboot.
// =====================================================
#define DAILY_REBOOT_EEPROM_OFFSET 480

void checkDailyReboot() {
  if (!timeOK) return;

  time_t now = time(nullptr);
  struct tm* ti = localtime(&now);
  if (ti == nullptr) return;
  if (ti->tm_hour < 2 || (ti->tm_hour == 2 && ti->tm_min < 15)) return;

  int32_t dayKey = (int32_t)(ti->tm_year + 1900) * 366 + ti->tm_yday;
  int32_t lastRebootDay = -1;
  EEPROM.get(DAILY_REBOOT_EEPROM_OFFSET, lastRebootDay);

  if (lastRebootDay == dayKey) return;

  Serial.println("Redemarrage quotidien programme (02h15) : sauvegarde puis reboot.");
  EEPROM.put(DAILY_REBOOT_EEPROM_OFFSET, dayKey);
  EEPROM.commit();
  delay(250);
  ESP.restart();
}

// =====================================================
// SETUP
// =====================================================

void setup() {

  Serial.begin(115200);
  delay(500);

  Serial.println();
  Serial.println("==============================");
  Serial.println(" GeekMagic Tempo V5");
  Serial.println(" Wi-Fi + Internet + NTP + RTE");
  Serial.println("==============================");

  // ---------------------------------------------------
  // Rétroéclairage
  // ---------------------------------------------------

  pinMode(TFT_BL, OUTPUT);
  analogWriteRange(1023);
  analogWriteFreq(1000);
  analogWrite(TFT_BL, 0);
  screenOn = true;

  delay(300);

  // ---------------------------------------------------
  // Ecran
  // ---------------------------------------------------

  tft.init(240, 240, SPI_MODE3);
  // Rotation 180° : écran monté tête en bas.
  tft.setRotation(2);
  startupScreen();

  // ---------------------------------------------------
  // Configuration
  // ---------------------------------------------------

  loadConfig();

  // ---------------------------------------------------
  // AP + Wi-Fi
  // ---------------------------------------------------

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID);

  // DNS captif : les noms de domaine demandés par le téléphone
  // sont redirigés vers la page locale du GeekMagic.
  dnsServer.start(53, "*", WiFi.softAPIP());

  Serial.print("AP IP : ");
  Serial.println(WiFi.softAPIP());

  bool connected = connectWiFi();

  // ---------------------------------------------------
  // Ecran réseau
  // ---------------------------------------------------

  tft.fillScreen(ST77XX_BLACK);

  if (connected) {

    tft.setTextColor(ST77XX_GREEN);
    tft.setFont(&FreeSansBold18pt7b);
    drawCenteredText("WIFI OK", 82);

    tft.setTextColor(ST77XX_WHITE);
    tft.setFont(&FreeSans18pt7b);
    drawCenteredText(WiFi.localIP().toString(), 125);

  } else {

    wifiSetupMode = true;

    tft.setTextColor(ST77XX_RED);
    tft.setFont(&FreeSansBold18pt7b);
    drawCenteredText("WIFI KO", 58);

    tft.setTextColor(ST77XX_WHITE);
    tft.setFont(&FreeSans12pt7b);
    drawCenteredText("Connectez-vous au Wi-Fi :", 98);
    drawCenteredText("MODELROGS Tempo", 127);
    drawCenteredText("192.168.4.1", 157);

    tft.setTextColor(ST77XX_CYAN);
    tft.setFont(&FreeSans9pt7b);
    drawCenteredText("Configuration via Wifi :", 190);
    tft.setFont(&FreeSans12pt7b);
    drawCenteredText("Modelrogs Tempo", 218);
  }

  // Pas de temporisation après l'affichage Wi-Fi / IP.

  // ---------------------------------------------------
  // Internet
  // ---------------------------------------------------

  if (connected) {
    internetOK = testInternet();
  }

  // ---------------------------------------------------
  // NTP
  // ---------------------------------------------------

  if (internetOK) {
    timeOK = syncTime();
  }

  // ---------------------------------------------------
  // Météo : recherche de la ville + première récupération
  // ---------------------------------------------------

  if (internetOK && timeOK) {

    Serial.println();
    Serial.println("==============================");
    Serial.println(" METEO : démarrage");
    Serial.println("==============================");

    weatherOK = false;

    if (resolveWeatherCity()) {

      if (updateWeather()) {
        Serial.println("Météo : OK.");
      } else {
        Serial.println("Météo : échec des prévisions.");
      }

    } else {

      Serial.println(
        "Météo : échec du géocodage."
      );
    }

    // Une tentative météo a déjà eu lieu au démarrage.
    // Même en cas d'échec, on attend 30 minutes avant de retenter.
    lastWeatherAttempt = millis();

    // La météo a déjà été récupérée au démarrage pour ce jour.
    time_t weatherNow = time(nullptr);
    struct tm* weatherTimeInfo = localtime(&weatherNow);
    if (weatherTimeInfo != nullptr) {
      lastWeatherDay = weatherTimeInfo->tm_yday;
    }
  }

  // ---------------------------------------------------
  // RTE : jusqu'à 3 tentatives au démarrage
  // ---------------------------------------------------

  rteOK = false;
  rteTomorrowFound = false;

  if (internetOK && timeOK) {

    for (int attempt = 1; attempt <= 3; attempt++) {

      Serial.println();
      Serial.println("==============================");
      Serial.print("RTE : tentative démarrage ");
      Serial.print(attempt);
      Serial.println("/3");
      Serial.println("==============================");

      if (testRTE()) {

        if (validTempoColor(tempoTomorrow)) {
          rteTomorrowFound = true;
          Serial.println("RTE : demain récupéré.");
          break;
        }

        Serial.println(
          "RTE : aujourd'hui récupéré, demain pas encore publié."
        );

      } else {
        Serial.println("RTE : échec de la tentative.");
      }

      if (attempt < 3) {
        Serial.println("Nouvelle tentative dans 5 secondes...");
        delay(5000);
      }
    }
  }

  lastTempoDate = getDateString();

  // ---------------------------------------------------
  // Serveur Web
  // ---------------------------------------------------

  server.on("/", HTTP_GET, handleRoot);
  server.on("/rte", HTTP_GET, handleRTE);
  server.on("/save", HTTP_POST, handleSave);

  httpUpdater.setup(&server, "/update");

  // Toute URL inconnue est renvoyée vers l'interface locale.
  // L'OTA /update reste gérée par ESP8266HTTPUpdateServer.
  server.onNotFound([]() {
    server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
    server.send(302, "text/plain", "Redirection vers MODELROGS TEMPO");
  });

  server.begin();

  Serial.println("Serveur Web OK.");

  // Pas de temporisation après la validation RTE.

  if (!wifiSetupMode) {
    displayStatus();
  }

  // Applique immédiatement la politique de luminosité au démarrage.
  if (timeOK) {
    nightModeActive = isNightModeActive();
    applyScreenBrightness();
  } else {
    analogWrite(TFT_BL, 1023 - ((int)config.screenBrightness * 1023 / 100));
    screenOn = config.screenBrightness > 0;
  }
}

// =====================================================
// LOOP
// =====================================================

void loop() {

  dnsServer.processNextRequest();
  server.handleClient();

  checkDailyReboot();
  handleTempoDateChange();
  automaticRteUpdate();

  // ---------------------------------------------------
  // Météo : une mise à jour par jour à partir de 00h05.
  // La première récupération est faite au démarrage.
  // Aucune mise à jour avant 00h05.
  // ---------------------------------------------------

  if (internetOK && timeOK) {

    time_t weatherNow = time(nullptr);
    struct tm* weatherTimeInfo = localtime(&weatherNow);

    if (weatherTimeInfo != nullptr) {

      int currentDay = weatherTimeInfo->tm_yday;

      bool after0005 =
        (weatherTimeInfo->tm_hour > 0) ||
        (weatherTimeInfo->tm_hour == 0 && weatherTimeInfo->tm_min >= 5);

      if (after0005 && currentDay != lastWeatherDay) {

        Serial.println();
        Serial.println("==============================");
        Serial.println(" METEO : mise à jour quotidienne");
        Serial.println("==============================");

        if (updateWeather()) {
          Serial.println("Météo : mise à jour quotidienne OK.");
        } else {
          Serial.println("Météo : échec de la mise à jour quotidienne.");
        }

        // Une seule tentative par jour, même en cas d'échec.
        lastWeatherDay = currentDay;
        lastWeatherAttempt = millis();
      }
    }
  }

  // ---------------------------------------------------
  // Gestion du Mode Nuit / luminosité automatique.
  // ---------------------------------------------------
  automaticScreenSchedule();

  // ---------------------------------------------------
  // Actualisation de l'heure sur l'écran principal.
  // ---------------------------------------------------

  static int lastMinute = -1;

  time_t now =
    time(nullptr);

  struct tm* timeinfo =
    localtime(&now);

  if (
    !wifiSetupMode &&
    timeOK &&
    timeinfo != nullptr &&
    timeinfo->tm_min != lastMinute
  ) {

    lastMinute =
      timeinfo->tm_min;

    // Rafraîchissement local : heure + tarif HP/HC uniquement.
    // Aucun redessin complet de l'écran et aucune requête réseau.
    updateClockDisplay();
    updateTariffDisplay();
  }
}
