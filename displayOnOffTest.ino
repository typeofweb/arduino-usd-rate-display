#include "arduino_secrets.h"
// WiFiNINA - Version: Latest
#include <WiFiNINA.h>

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <ArduinoHttpClient.h>
#include <SPI.h>
#include <Fonts/FreeSans24pt7b.h>
#include <Fonts/FreeMono9pt7b.h>

#define TFT_CS 10
#define TFT_DC 7
#define TFT_RST 8
#define TFT_BACKLIGHT 9
#define TFT_MOSI 11
#define TFT_SCLK 13

#define SCREEN_W 240
#define SCREEN_H 240
#define HEADER_H 12
#define FOOTER_H 85
#define BODY_H (SCREEN_H - HEADER_H - FOOTER_H)

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
// Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

// 0brrrrrggggggbbbbb
// 5 bits red
// 6 bits green
// 5 bits blue
#define TO_RGB(r, g, b) (((r & 0b00011111) << 11) | ((g & 0b00111111) << 5) | (b & 0b00011111))

#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define CLAMP(x, lower, upper) (MIN(upper, MAX(x, lower)))

// color definitions
const uint16_t Display_Color_Black = TO_RGB(0, 0, 0);
const uint16_t Display_Color_Blue = TO_RGB(0, 0, 31);
const uint16_t Display_Color_Red = TO_RGB(31, 0, 0);
const uint16_t Display_Color_Green = TO_RGB(0, 63, 0);
const uint16_t Display_Color_Cyan = TO_RGB(0, 63, 31);
const uint16_t Display_Color_Magenta = TO_RGB(31, 0, 31);
const uint16_t Display_Color_Yellow = TO_RGB(31, 63, 0);
const uint16_t Display_Color_White = TO_RGB(31, 63, 31);
const uint16_t Display_Color_Gray = TO_RGB(15, 31, 15);

// The colors we actually want to use
uint16_t Display_Text_Color = Display_Color_Gray;
uint16_t Display_Backround_Color = Display_Color_Black;

const size_t MaxString = 5;

// wifi
char ssid[] = SECRET_SSID;
char pass[] = SECRET_PASS;
int status = WL_IDLE_STATUS;

const char apiHost[] = "usd-ticker.vercel.app";
const char apiPath[] = "/api";
WiFiSSLClient netSocket;
HttpClient client(netSocket, apiHost, 443);

const unsigned long headerUpdateInterval = 5L * 1000L;  // 5s
unsigned long lastHeaderUpdateMillis = 0L;

const unsigned long fetchInterval = 5L * 60L * 1000L;  // 5m
unsigned long lastFetchMillis = 0L;

bool showingSalary = false;
const unsigned long salaryShowInterval = 5L * 1000L;  // 5s
unsigned long lastSalaryShown = 0L;

float usdRate = 0.00000f;
size_t ratesN = 0;
float* rates = nullptr;
char date[] = "0000-00-00T00:00:00Z";

unsigned long oldUpSeconds = -1;
char oldTimeString[MaxString] = "";
bool blink = true;
int16_t oldX = 0;
int16_t oldY = 0;

void displayUpTime() {

  // calculate seconds, truncated to the nearest whole second
  unsigned long upSeconds = millis() / 1000;

  // calculate days, truncated to nearest whole day
  unsigned long days = upSeconds / 86400;

  // the remaining hhmmss are
  upSeconds = upSeconds % 86400;

  // calculate hours, truncated to the nearest whole hour
  unsigned long hours = upSeconds / 3600;

  // the remaining mmss are
  upSeconds = upSeconds % 3600;

  // calculate minutes, truncated to the nearest whole minute
  unsigned long minutes = upSeconds / 60;

  // the remaining ss are
  upSeconds = upSeconds % 60;

  if (upSeconds != oldUpSeconds) {
    char newTimeString[MaxString] = "";
    if (blink) {
      sprintf(
        newTimeString,
        "%02lu:%02lu",
        hours, minutes);
    } else {
      sprintf(
        newTimeString,
        "%02lu %02lu",
        hours, minutes);
    }

    tft.setTextColor(Display_Text_Color);
    tft.setTextSize(1);

    tft.setFont(&FreeSans24pt7b);
    tft.setCursor(oldX, oldY);
    tft.setTextColor(Display_Backround_Color);
    tft.print(oldTimeString);

    int16_t x;
    int16_t y;
    uint16_t w;
    uint16_t h;
    tft.getTextBounds(newTimeString, 0, 0, &x, &y, &w, &h);
    int16_t posX = SCREEN_W / 2 - w / 2;
    int16_t posY = SCREEN_H / 2 - h / 2;
    tft.setCursor(posX, posY);
    tft.setTextColor(Display_Text_Color);
    tft.print(newTimeString);

    oldUpSeconds = upSeconds;
    strcpy(oldTimeString, newTimeString);
    oldX = posX;
    oldY = posY;
    blink = !blink;
  }
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  // analogWrite(TFT_BACKLIGHT, 50);

  while (status != WL_CONNECTED) {
    status = WiFi.begin(ssid, pass);
    delay(100);
  }

  lastSalaryShown = millis();

  tft.init(SCREEN_W, SCREEN_H);
  tft.enableDisplay(true);
  tft.setSPISpeed(1e10);

  tft.fillScreen(Display_Backround_Color);
  tft.setRotation(3);

  Serial.begin(9600);
}

void displayChart() {
  tft.fillRect(0, HEADER_H, SCREEN_W, BODY_H, Display_Backround_Color);

  const int16_t marginX = 30;
  const int16_t marginY = 10;
  const int16_t charH = 10;

  const int16_t x = marginX;
  const int16_t y = HEADER_H + marginY;
  const int16_t w = SCREEN_W - 2 * marginX;
  const int16_t h = BODY_H - 2 * marginY;

  // axes
  tft.drawFastVLine(x, y, h, Display_Color_Gray);
  tft.drawFastHLine(x, y + h, w, Display_Color_Gray);

  tft.setFont();
  tft.setTextColor(Display_Color_White);
  tft.setTextSize(1);

  // calculate min and max from values
  float min = INFINITY;
  float max = -INFINITY;
  for (size_t i = 0; i < ratesN; ++i) {
    const float openRate = rates[i];
    min = MIN(openRate, min);
    max = MAX(openRate, max);
  }
  // add 0.2% margin
  const float m = 0.2f;
  min = floor(min * (100.0f - m)) / 100.0f;
  max = ceil(max * (100.0f + m)) / 100.0f;

  // y-axis labels
  const float q2 = (min + max) / 2.0f;
  const float q1 = (min + q2) / 2.0f;
  const float q3 = (max + q2) / 2.0f;

  const int16_t minY = y + h - charH / 2;
  const int16_t maxY = y;
  const int16_t q2Y = (minY + maxY) / 2;
  const int16_t q1Y = (minY + q2Y) / 2;
  const int16_t q3Y = (maxY + q2Y) / 2;

  tft.setCursor(0, minY);
  tft.print(min);
  tft.setCursor(0, maxY);
  tft.print(max);
  tft.setCursor(0, q2Y);
  tft.print(q2);
  tft.setCursor(0, q1Y);
  tft.print(q1);
  tft.setCursor(0, q3Y);
  tft.print(q3);

  {  // draw lines
    const int16_t segment = w / ratesN;

    const float firstOpenRate = rates[0];
    int16_t prevPointX = x;
    int16_t prevPointY = y + h * (firstOpenRate - min) / (max - min);
    for (size_t i = 1; i < ratesN; ++i) {
      const float openRate = rates[i];

      const int16_t pointX = x + w * i / (ratesN - 1);
      const int16_t pointY = h - (h * (openRate - min) / (max - min)) + y;

      tft.drawLine(prevPointX, prevPointY, pointX, pointY, Display_Color_Red);
      // tft.drawPixel(pointX, pointY, Display_Color_Red);
      prevPointX = pointX;
      prevPointY = pointY;
    }
  }
}

void displayHeader() {
  const unsigned long now = millis();
  if (!lastHeaderUpdateMillis || (now - lastHeaderUpdateMillis >= headerUpdateInterval)) {
    lastHeaderUpdateMillis = now;

    // clean header
    tft.fillRect(0, 0, SCREEN_W, HEADER_H, Display_Color_Black);
    displayWifiStrength();
    displayDate();
  }
}

void displayDate() {
  int16_t x;
  int16_t y;
  uint16_t w;
  uint16_t h;
  tft.setFont();
  tft.setTextSize(1);
  tft.setTextColor(Display_Text_Color);

  tft.getTextBounds(date, 0, 0, &x, &y, &w, &h);
  tft.setCursor(SCREEN_W - w, 3);
  tft.print(date);
}

void displayWifiStrength() {  // wifi signal strength
  long rssi = WiFi.RSSI();
  const int quality = CLAMP(2 * (100 + rssi), 0, 100);

  uint16_t currentColor = Display_Color_White;

  currentColor = quality > 85 ? Display_Color_White : Display_Color_Gray;

  {  // first arc
    tft.drawPixel(0, 3, currentColor);
    tft.drawPixel(1, 2, currentColor);
    tft.drawPixel(2, 1, currentColor);
    tft.drawFastHLine(3, 0, 7, currentColor);
    tft.drawPixel(10, 1, currentColor);
    tft.drawPixel(11, 2, currentColor);
    tft.drawPixel(12, 3, currentColor);
  }

  currentColor = quality > 50 ? Display_Color_White : Display_Color_Gray;

  {  // second arc
    tft.drawPixel(2, 5, currentColor);
    tft.drawPixel(3, 4, currentColor);
    tft.drawFastHLine(4, 3, 5, currentColor);
    tft.drawPixel(9, 4, currentColor);
    tft.drawPixel(10, 5, currentColor);
  }

  currentColor = quality > 30 ? Display_Color_White : Display_Color_Gray;

  {  // third arc
    tft.drawPixel(4, 7, currentColor);
    tft.drawFastHLine(5, 6, 3, currentColor);
    tft.drawPixel(8, 7, currentColor);
  }

  {  // cross
    tft.drawFastHLine(5, 9, 3, currentColor);
    tft.drawFastVLine(6, 8, 3, currentColor);
  }

  tft.setFont();
  tft.setCursor(15, 3);
  tft.setTextColor(Display_Color_White);
  tft.setTextSize(1);
  tft.print(quality);
}

bool fetchUsdRate() {
  if (!lastFetchMillis || (millis() - lastFetchMillis >= fetchInterval)) {
    client.stop();

    client.beginRequest();
    client.get(apiPath);
    client.sendHeader("Content-Type", "text/csv");
    client.endRequest();

    lastFetchMillis = millis();
  }

  if (client.available()) {
    tft.setCursor(0, HEADER_H);

    client.responseStatusCode();
    String body = client.responseBody();
    unsigned int len = body.length();

    const char separator = ',';
    unsigned int fromIndex = 0;
    int toIndex = body.indexOf(separator, fromIndex);

    // count; subtract 1 because count is also included in the count
    ratesN = body.substring(fromIndex, toIndex).toInt() - 1;

    { // USD rate
      fromIndex = toIndex+1;
      toIndex = body.indexOf(separator, fromIndex);
      usdRate = body.substring(fromIndex, toIndex).toFloat();
    }

    { // rates
      if (rates) {
        free(rates);
      }
      // alloc 1 more because we're pushing `usdRate` to the array
      rates = (float*)malloc((ratesN + 1) * sizeof(float));

      for (size_t i = 0; fromIndex < len && i < ratesN; ++i) {
        fromIndex = toIndex+1;
        toIndex = body.indexOf(separator, fromIndex);
        const float rate = body.substring(fromIndex, toIndex).toFloat();
        rates[i] = rate;
      }
      rates[ratesN] = usdRate;
      ratesN++;
    }

    { // date
      fromIndex = toIndex+1;
      const String srcDate = body.substring(fromIndex);
      strcpy(date, srcDate.c_str());
    }

    client.stop();
    return true;
  }
  return false;
}

void displayUsdRate() {
  if (usdRate <= 0.001) {
    return;
  }

  if (!lastSalaryShown || (millis() - lastSalaryShown >= salaryShowInterval)) {
    int16_t x;
    int16_t y;
    uint16_t w;
    uint16_t h;
    tft.setFont(&FreeSans24pt7b);
    tft.setTextSize(1);
    tft.setTextColor(Display_Text_Color);
    tft.fillRect(0, SCREEN_H - FOOTER_H, SCREEN_W, SCREEN_H, Display_Backround_Color);

    const String valueStr = showingSalary ? String(usdRate * SECRET_RATE * 168.0, 2) : String(usdRate, 5);

    tft.getTextBounds(valueStr, 0, 0, &x, &y, &w, &h);
    int16_t posX = SCREEN_W / 2 - w / 2;


    tft.setCursor(posX, SCREEN_H - 5 - h * 4 / 3);
    tft.print(showingSalary ? "Wyplata:" : "USD:");

    tft.setCursor(posX, SCREEN_H - 5);
    tft.print(valueStr);

    showingSalary = !showingSalary;
    lastSalaryShown = millis();
  }
}


void loop() {
  displayHeader();
  const bool shouldUpdate = fetchUsdRate();
  if (shouldUpdate) {
    displayChart();
  }
  displayUsdRate();

  // no need to be in too much of a hurry
  delay(100);
}