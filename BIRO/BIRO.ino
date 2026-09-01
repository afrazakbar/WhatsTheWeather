#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_NeoPixel.h>

const char* WIFI_SSID = "WILL-BE-REPLACED-AFTER-LAPSE";
const char* WIFI_PASSWORD = "WILL-BE-REPLACED-AFTER-LAPSE";
const char* API_KEY = "WILL-BE-REPLACED-AFTER-LAPSE";

const float LATITUDE = 28.6139;
const float LONGITUDE = 77.2090;

#define DHT_PIN 4
#define DHT_TYPE DHT11
#define OLED_SDA 21
#define OLED_SCL 22
#define WS_PIN 18
#define WS_COUNT 1
#define TOUCH_PIN 15

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
DHT dht(DHT_PIN, DHT_TYPE);
Adafruit_NeoPixel pixel(WS_COUNT, WS_PIN, NEO_GRB + NEO_KHZ800);

float internetTemp = NAN;
float internetHumidity = NAN;
float rainProbability = NAN;
int weatherCode = -1;

float localTemp = NAN;
float localHumidity = NAN;

bool internetOK = false;
bool localMode = false;

unsigned long lastWeatherUpdate = 0;
const unsigned long WEATHER_INTERVAL = 10UL * 60UL * 1000UL;

void setPixel(uint8_t r, uint8_t g, uint8_t b) {
  pixel.setPixelColor(0, pixel.Color(r, g, b));
  pixel.show();
}

void startupRainbow() {
  for (int j = 0; j < 256; j++) {
    uint32_t color = pixel.gamma32(pixel.ColorHSV(j * 256));
    pixel.setPixelColor(0, color);
    pixel.show();
    delay(8);
  }

  setPixel(0, 0, 0);
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

  centerText("BIRA", 4, 2);
  centerText("BroIsItRainingOut?", 30, 1);
  centerText("WEATHER STATION", 50, 1);

  display.display();
}

void showConnecting() {
  display.clearDisplay();

  centerText("BIRA", 0, 2);
  centerText("Connecting WiFi...", 30, 1);

  display.display();
}

String weatherDescription(int code) {
  switch (code) {
    case 0: return "CLEAR";
    case 1: return "MAINLY CLEAR";
    case 2: return "PARTLY CLOUDY";
    case 3: return "CLOUDY";
    case 45:
    case 48: return "FOGGY";
    case 51:
    case 53:
    case 55: return "DRIZZLE";
    case 56:
    case 57: return "FREEZING DRIZZLE";
    case 61:
    case 63:
    case 65: return "RAIN";
    case 66:
    case 67: return "FREEZING RAIN";
    case 71:
    case 73:
    case 75:
    case 77: return "SNOW";
    case 80:
    case 81:
    case 82: return "SHOWERS";
    case 85:
    case 86: return "SNOW SHOWERS";
    case 95: return "THUNDERSTORM";
    case 96:
    case 99: return "STORM + HAIL";
    default: return "UNKNOWN";
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
    setPixel(100, 40, 0);
    return;
  }

  int quality = weatherQuality();

  if (quality == 2) {
    setPixel(255, 0, 0);
  } else if (quality == 1) {
    setPixel(0, 60, 255);
  } else {
    setPixel(0, 255, 0);
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
  } else {
    display.print(localTemp, 1);
    display.print(" C");
  }

  display.setCursor(0, 34);
  display.print("HUM : ");

  if (isnan(localHumidity)) {
    display.print("--");
  } else {
    display.print(localHumidity, 0);
    display.print(" %");
  }

  display.setCursor(0, 52);
  display.print("TOUCH = ONLINE");

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
  } else {
    display.print(internetTemp, 1);
    display.print(" C");
  }

  display.setCursor(0, 29);
  display.print("RAIN: ");

  if (isnan(rainProbability)) {
    display.print("--");
  } else {
    display.print(rainProbability, 0);
    display.print("%");
  }

  display.setCursor(0, 43);
  display.print(weatherDescription(weatherCode));

  display.setCursor(0, 56);
  display.print("TOUCH = LOCAL");

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
    http.end();
    internetOK = false;
    return false;
  }

  String payload = http.getString();
  http.end();

  JsonDocument doc;

  DeserializationError error = deserializeJson(doc, payload);

  if (error) {
    internetOK = false;
    return false;
  }

  internetTemp = doc["current"]["temperature_2m"] | NAN;
  internetHumidity = doc["current"]["relative_humidity_2m"] | NAN;
  weatherCode = doc["current"]["weather_code"] | -1;
  rainProbability = doc["hourly"]["precipitation_probability"][0] | NAN;

  internetOK = !isnan(internetTemp) && weatherCode >= 0;

  return internetOK;
}

void setup() {
  Serial.begin(115200);

  dht.begin();

  pinMode(TOUCH_PIN, INPUT);

  Wire.begin(OLED_SDA, OLED_SCL);

  display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);

  pixel.begin();
  pixel.setBrightness(60);
  pixel.clear();
  pixel.show();

  startupRainbow();

  showStartup();

  delay(2500);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  showConnecting();

  unsigned long startTime = millis();

  while (
    WiFi.status() != WL_CONNECTED &&
    millis() - startTime < 15000
  ) {
    delay(300);
  }

  if (WiFi.status() == WL_CONNECTED) {
    getInternetWeather();
    updateWeatherLED();
  } else {
    internetOK = false;
    setPixel(100, 40, 0);
  }

  readLocalDHT();
}

void loop() {
  static bool lastTouched = false;

  bool touched = touchRead(TOUCH_PIN) < 30;

  if (touched && !lastTouched) {
    localMode = !localMode;
    delay(300);
  }

  lastTouched = touched;

  static unsigned long lastDHT = 0;

  if (millis() - lastDHT >= 2000) {
    lastDHT = millis();
    readLocalDHT();
  }

  if (
    millis() - lastWeatherUpdate >= WEATHER_INTERVAL ||
    lastWeatherUpdate == 0
  ) {
    lastWeatherUpdate = millis();

    if (WiFi.status() != WL_CONNECTED) {
      WiFi.reconnect();
      delay(1000);
    }

    getInternetWeather();
    updateWeatherLED();
  }

  if (localMode) {
    showLocal();
  } else {
    showInternet();
  }

  delay(100);
}