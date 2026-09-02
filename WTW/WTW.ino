
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

const char* WIFI_SSID = "replace-with-ur-wifi-name";
const char* WIFI_PASSWORD = "replace-with-ur-wifi-passwd";

// Add ur LATITUDE AND LONGITUDE
const float LATITUDE = ;
const float LONGITUDE = ;

#define DHT_PIN 4
#define DHT_TYPE DHT11

#define OLED_SDA 21
#define OLED_SCL 22

#define TOUCH_PIN 32

#define GREEN_LED 25
#define BLUE_LED 26
#define RED_LED 27

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
DHT dht(DHT_PIN, DHT_TYPE);

float internetTemp = NAN;
float internetHumidity = NAN;
float rainProbability = NAN;
int weatherCode = -1;

float localTemp = NAN;
float localHumidity = NAN;

bool internetOK = false;
bool localMode = false;

int touchBaseline = 0;
int touchThreshold = 0;

unsigned long lastWeatherUpdate = 0;
const unsigned long WEATHER_INTERVAL = 10UL * 60UL * 1000UL;

unsigned long lastDHT = 0;
const unsigned long DHT_INTERVAL = 2000;

void setWeatherLEDs(bool green, bool blue, bool red) {
  digitalWrite(GREEN_LED, green ? HIGH : LOW);
  digitalWrite(BLUE_LED, blue ? HIGH : LOW);
  digitalWrite(RED_LED, red ? HIGH : LOW);
}

void startupLEDChase() {
  for (int i = 0; i < 3; i++) {
    digitalWrite(GREEN_LED, LOW);
    digitalWrite(BLUE_LED, LOW);
    digitalWrite(RED_LED, LOW);

    digitalWrite(GREEN_LED, HIGH);
    delay(150);
    digitalWrite(GREEN_LED, LOW);

    digitalWrite(BLUE_LED, HIGH);
    delay(150);
    digitalWrite(BLUE_LED, LOW);

    digitalWrite(RED_LED, HIGH);
    delay(150);
    digitalWrite(RED_LED, LOW);
  }

  setWeatherLEDs(false, false, false);
}

void centerText(const String &text, int y, int size = 1) {
  display.setTextSize(size);
  display.setTextColor(SSD1306_WHITE);

  int16_t x1, y1;
  uint16_t w, h;

  display.getTextBounds(text, 0, y, &x1, &y1, &w, &h);

  display.setCursor((SCREEN_WIDTH - w) / 2, y);
  display.print(text);
}

void showStartup() {
  display.clearDisplay();

  centerText("WTW", 4, 2);
  centerText("What's The Weather", 30, 1);
  centerText("WEATHER STATION", 50, 1);

  display.display();
}

void showConnecting() {
  display.clearDisplay();

  centerText("WTW", 0, 2);
  centerText("Connecting WiFi...", 30, 1);

  display.display();
}

void calibrateTouch() {
  display.clearDisplay();

  centerText("WTW", 4, 2);
  centerText("Calibrating touch", 30, 1);
  centerText("Do not touch", 45, 1);

  display.display();

  delay(1500);

  long total = 0;

  for (int i = 0; i < 40; i++) {
    total += touchRead(TOUCH_PIN);
    delay(25);
  }

  touchBaseline = total / 40;
  touchThreshold = touchBaseline * 0.70;

  if (touchThreshold < 5) {
    touchThreshold = 5;
  }

  Serial.println();
  Serial.println("Touch calibration complete");
  Serial.print("Touch baseline: ");
  Serial.println(touchBaseline);
  Serial.print("Touch threshold: ");
  Serial.println(touchThreshold);
  Serial.println();
}

String weatherDescription(int code) {
  switch (code) {
    case 0:
      return "CLEAR";

    case 1:
      return "MAINLY CLEAR";

    case 2:
      return "PARTLY CLOUDY";

    case 3:
      return "CLOUDY";

    case 45:
    case 48:
      return "FOGGY";

    case 51:
    case 53:
    case 55:
      return "DRIZZLE";

    case 56:
    case 57:
      return "FREEZING DRIZZLE";

    case 61:
    case 63:
    case 65:
      return "RAIN";

    case 66:
    case 67:
      return "FREEZING RAIN";

    case 71:
    case 73:
    case 75:
    case 77:
      return "SNOW";

    case 80:
    case 81:
    case 82:
      return "SHOWERS";

    case 85:
    case 86:
      return "SNOW SHOWERS";

    case 95:
      return "THUNDERSTORM";

    case 96:
    case 99:
      return "STORM + HAIL";

    default:
      return "UNKNOWN";
  }
}

int weatherQuality() {
  if (weatherCode < 0 || isnan(internetTemp)) {
    return 1;
  }

  if (weatherCode >= 51) {
    return 2;
  }

  if (weatherCode == 0 || weatherCode == 1) {
    if (!isnan(rainProbability) && rainProbability > 30) {
      return 1;
    }

    return 0;
  }

  return 1;
}

void updateWeatherLED() {
  if (!internetOK) {
    setWeatherLEDs(false, false, true);
    return;
  }

  int quality = weatherQuality();

  if (quality == 2) {
    setWeatherLEDs(false, false, true);
  }
  else if (quality == 1) {
    setWeatherLEDs(false, true, false);
  }
  else {
    setWeatherLEDs(true, false, false);
  }
}

void showLocal() {
  display.clearDisplay();

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.print("LOCAL SENSOR");

  display.setCursor(0, 18);
  display.print("TEMP: ");

  if (isnan(localTemp)) {
    display.print("--");
  }
  else {
    display.print(localTemp, 1);
    display.print(" C");
  }

  display.setCursor(0, 34);
  display.print("HUM : ");

  if (isnan(localHumidity)) {
    display.print("--");
  }
  else {
    display.print(localHumidity, 0);
    display.print(" %");
  }

  display.display();
}

void showInternet() {
  display.clearDisplay();

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  display.setCursor(0, 0);
  display.print("ONLINE WEATHER");

  display.setCursor(0, 15);
  display.print("TEMP: ");

  if (isnan(internetTemp)) {
    display.print("--");
  }
  else {
    display.print(internetTemp, 1);
    display.print(" C");
  }

  display.setCursor(0, 29);
  display.print("RAIN: ");

  if (isnan(rainProbability)) {
    display.print("--");
  }
  else {
    display.print(rainProbability, 0);
    display.print("%");
  }

  display.setCursor(0, 43);
  display.print(weatherDescription(weatherCode));

  display.display();
}

bool readLocalDHT() {
  localHumidity = dht.readHumidity();
  localTemp = dht.readTemperature();

  return !isnan(localHumidity) && !isnan(localTemp);
}

bool getInternetWeather() {
  if (WiFi.status() != WL_CONNECTED) {
    internetOK = false;
    return false;
  }

  HTTPClient http;

  String url =
    "https://api.open-meteo.com/v1/forecast?"
    "latitude=" + String(LATITUDE, 4) +
    "&longitude=" + String(LONGITUDE, 4) +
    "&current=temperature_2m,relative_humidity_2m,weather_code"
    "&hourly=precipitation_probability"
    "&forecast_days=1"
    "&timezone=auto";

  http.begin(url);
  http.setTimeout(10000);

  int httpCode = http.GET();

  if (httpCode != HTTP_CODE_OK) {
    Serial.print("Weather HTTP error: ");
    Serial.println(httpCode);

    http.end();
    internetOK = false;
    return false;
  }

  String payload = http.getString();

  http.end();

  JsonDocument doc;

  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    Serial.print("JSON error: ");
    Serial.println(error.c_str());

    internetOK = false;
    return false;
  }

  internetTemp =
    doc["current"]["temperature_2m"] | NAN;

  internetHumidity =
    doc["current"]["relative_humidity_2m"] | NAN;

  weatherCode =
    doc["current"]["weather_code"] | -1;

  rainProbability =
    doc["hourly"]["precipitation_probability"][0] | NAN;

  internetOK =
    !isnan(internetTemp) &&
    weatherCode >= 0;

  return internetOK;
}

void checkTouch() {
  static bool lastTouched = false;
  static unsigned long lastTouchTime = 0;

  int touchValue = touchRead(TOUCH_PIN);

  bool touched = touchValue < touchThreshold;

  if (
    touched &&
    !lastTouched &&
    millis() - lastTouchTime > 500
  ) {
    localMode = !localMode;
    lastTouchTime = millis();

    Serial.print("MODE: ");

    if (localMode) {
      Serial.println("LOCAL");
    }
    else {
      Serial.println("ONLINE");
    }
  }

  lastTouched = touched;
}

void setup() {
  Serial.begin(115200);

  delay(500);

  Serial.println();
  Serial.println("=== WTW STARTING ===");

  pinMode(GREEN_LED, OUTPUT);
  pinMode(BLUE_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);

  setWeatherLEDs(false, false, false);

  startupLEDChase();

  dht.begin();

  Wire.begin(OLED_SDA, OLED_SCL);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED initialization failed!");
  }

  showStartup();

  delay(2500);

  calibrateTouch();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  showConnecting();

  Serial.println("Connecting to WiFi...");

  unsigned long startTime = millis();

  while (
    WiFi.status() != WL_CONNECTED &&
    millis() - startTime < 15000
  ) {
    delay(300);
    Serial.print(".");
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());

    getInternetWeather();
    updateWeatherLED();
  }
  else {
    Serial.println("WiFi connection failed.");

    internetOK = false;

    setWeatherLEDs(false, false, true);
  }

  readLocalDHT();

  lastWeatherUpdate = millis();
  lastDHT = millis();

  Serial.println("WTW ready.");
}

void loop() {
  checkTouch();

  if (millis() - lastDHT >= DHT_INTERVAL) {
    lastDHT = millis();
    readLocalDHT();
  }

  if (millis() - lastWeatherUpdate >= WEATHER_INTERVAL) {
    lastWeatherUpdate = millis();

    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi disconnected. Reconnecting...");
      WiFi.reconnect();
    }

    getInternetWeather();
    updateWeatherLED();
  }

  if (localMode) {
    showLocal();
  }
  else {
    showInternet();
  }

  delay(50);
}

