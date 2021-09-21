#include "arduino_secrets.h"
// WiFiNINA - Version: Latest
#include <WiFiNINA.h>

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <Fonts/FreeSans24pt7b.h>
#include <Fonts/FreeMono9pt7b.h>

#define TFT_CS        10
#define TFT_DC         7
#define TFT_RST        8
#define TFT_BACKLIGHT  9
#define TFT_MOSI 11
#define TFT_SCLK 13

#define SCREEN_W 240
#define SCREEN_H 240

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
// Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

// 0brrrrrggggggbbbbb
// 5 bits red
// 6 bits green
// 5 bits blue
#define TO_RGB(r, g, b) (((r & 0b00011111) << 11) | ((g & 0b00111111) << 5) | (b & 0b00011111))

#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define CLAMP(x, lower, upper) (MIN(upper, MAX(x, lower)))

// color definitions
const uint16_t  Display_Color_Black        = TO_RGB(0, 0, 0);
const uint16_t  Display_Color_Blue         = TO_RGB(0, 0, 31);
const uint16_t  Display_Color_Red          = TO_RGB(31, 0, 0);
const uint16_t  Display_Color_Green        = TO_RGB(0, 63, 0);
const uint16_t  Display_Color_Cyan         = TO_RGB(0, 63, 31);
const uint16_t  Display_Color_Magenta      = TO_RGB(31, 0, 31);
const uint16_t  Display_Color_Yellow       = TO_RGB(31, 63, 0);
const uint16_t  Display_Color_White        = TO_RGB(31, 63, 31);
const uint16_t  Display_Color_Gray        =  TO_RGB(15, 31, 15);

// The colors we actually want to use
uint16_t        Display_Text_Color         = Display_Color_Gray;
uint16_t        Display_Backround_Color    = Display_Color_Black;

const size_t    MaxString               = 5;

// wifi
char ssid[] = SECRET_SSID;
char pass[] = SECRET_PASS;
int status = WL_IDLE_STATUS;
WiFiClient client;

const char apiHost[] = "openexchangerates.org";
const char apiPath[] = "/api/latest.json?symbols=PLN&prettyprint=false&app_id=" SECRET_EXCHANGE_APP_ID;

const unsigned long headerUpdateInterval = 5L * 1000L; // 5s
unsigned long lastHeaderUpdateMillis = 0L;

const unsigned long fetchInterval = 60L * 60L * 1000L; // 1h
unsigned long lastFetchMillis = 0L;



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
        hours, minutes
      );
    } else {
      sprintf(
        newTimeString,
        "%02lu %02lu",
        hours, minutes
      );
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

  while (status != WL_CONNECTED) {
    status = WiFi.begin(ssid, pass);
    delay(100);
  }

  tft.init(SCREEN_W, SCREEN_H);
  tft.enableDisplay(true);
  tft.setSPISpeed(1e10);

  tft.fillScreen(Display_Backround_Color);
  tft.setRotation(3);

  Serial.begin(9600);
}

void displayHeader() {
  const unsigned long now = millis();
  if (!lastHeaderUpdateMillis || (now - lastHeaderUpdateMillis >= headerUpdateInterval)) {
    lastHeaderUpdateMillis = now;

    // clean header
    tft.fillRect(0, 0, SCREEN_W, 12, Display_Color_Black);
    displayWifiStrength();    
  }
}

void displayWifiStrength() { // wifi signal strength
  long rssi = WiFi.RSSI();
  const int quality = CLAMP(2 * (100 + rssi), 0, 100);

  uint16_t currentColor = Display_Color_White;

  currentColor = quality > 85 ? Display_Color_White : Display_Color_Gray;
  
  { // first arc
    tft.drawPixel(0, 3, currentColor);
    tft.drawPixel(1, 2, currentColor);
    tft.drawPixel(2, 1, currentColor);
    tft.drawFastHLine(3, 0, 7, currentColor);
    tft.drawPixel(10, 1, currentColor);
    tft.drawPixel(11, 2, currentColor);
    tft.drawPixel(12, 3, currentColor);
  }

  currentColor = quality > 50 ? Display_Color_White : Display_Color_Gray;
  
  { // second arc
    tft.drawPixel(2, 5, currentColor);
    tft.drawPixel(3, 4, currentColor);
    tft.drawFastHLine(4, 3, 5, currentColor);
    tft.drawPixel(9, 4, currentColor);
    tft.drawPixel(10, 5, currentColor);
  }

  currentColor = quality > 30 ? Display_Color_White : Display_Color_Gray;

  { // third arc
    tft.drawPixel(4, 7, currentColor);
    tft.drawFastHLine(5, 6, 3, currentColor);
    tft.drawPixel(8, 7, currentColor);
  }

  { // cross
    tft.drawFastHLine(5, 9, 3, currentColor);
    tft.drawFastVLine(6, 8, 3, currentColor);
  }

  tft.setFont();
  tft.setCursor(15, 3);
  tft.setTextColor(Display_Color_White);
  tft.setTextSize(1);
  tft.print(quality);
}

void fetchHttp() {
  tft.setCursor(0, 30);

  if (!lastFetchMillis || (millis() - lastFetchMillis >= fetchInterval)) {
    client.stop();

	  char url[128] = "";
	  sprintf(url, "GET %s HTTP/1.1", apiPath);
    
    client.connect(apiHost, 80);
    client.println(url);
    client.println("Host: openexchangerates.org");
    client.println("User-Agent: ArduinoWiFi/1.1");
    client.println("Connection: close");
    client.println();

    lastFetchMillis = millis();
  }

  if (client.available()) {
    const int MAX = 255;
    char response[MAX+1] = "";
    response[MAX] = 0;

    bool startParsing = false;
    int i = 0;
    while (client.available() && i < MAX) {
      char c = client.read();

      if (c == '{') {
        startParsing = true;
      }
      
      if (!startParsing) {
        continue;
      }

      response[i] = c;
      ++i;
    }

    char rate[] = "0.00000 ";

    { // JSON parse for the poor
      const char opening[] = "{\"PLN\":";
      const char closing[] = "}";
      const char * openingPtr = strstr(response, opening);
      const char * closingPtr = strstr(openingPtr, closing);

      const char * start = openingPtr + sizeof(opening)/sizeof(opening[0]) - 1;

      strncpy(rate, start, closingPtr - start);
    }

  
    {
      int16_t x;
      int16_t y;
      uint16_t w;
      uint16_t h;
      tft.setFont(&FreeSans24pt7b);
      tft.setTextSize(1);
      tft.getTextBounds(rate, 0, 0, &x, &y, &w, &h);
      int16_t posX = SCREEN_W / 2 - w / 2;
      int16_t posY = SCREEN_H / 2 - h / 2;
      tft.setTextColor(Display_Text_Color);

      const int16_t OFFSET = h * 4 / 3;
      
      tft.setCursor(posX, posY);
      tft.print("USD:");

      tft.setCursor(posX, posY + OFFSET);
      tft.print(rate);
    }

    client.stop();
  }
}


void loop() {
  displayHeader();
  fetchHttp();

  // no need to be in too much of a hurry
  delay(100);
}