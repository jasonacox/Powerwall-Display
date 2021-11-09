/*
  Powerwall-Display
  ESP8266 with Three TM1637 7-Segment Displays - STATIC WIFI SETTINGS
  
  Tesla Powerwall display based on 7-segment displays and ESP8266 to show 
  current solar generation, powerwall (power and percentage), grid and 
  load power usage.
  
  Description
     Three displays are set up to display metrics from Powerwall:
       Display1 (4-digit): Solar Power Generated (W)
       Display2 (6-digit): Rotates display of the following Power metrics:
                             (H)ouse Load (W)
                             (E)lectric Utility (W) + direction animation
                             (P)owerwall Battery (W) + direction animation
       Display3 (4-digit): Powerwall Battery (% Full) + animation

  This sketch requires pyPowerwall proxy - http://<proxy-address>:8675/csv

     Proxy Install Instructions:
          docker run \
              -d \
              -p 8675:8675 \
              -e PW_PASSWORD='your-powerwall-password' \
              -e PW_EMAIL='your-powerwall-email' \
              -e PW_HOST='your-powerwall-ip-address' \
              -e PW_TIMEZONE='America/Los_Angeles' \
              -e PW_DEBUG='yes' \
              -e PW_CACHE_EXPIRE='20' \
              --name pypowerwall \
              --restart unless-stopped \
              jasonacox/pypowerwall

  Author: Jason A. Cox - @jasonacox
  Date: 1 November 2021
*/

#define VERSION 1.00
#define SENSORID 101 

// Configuration Settings           
#define FILTER 50               // Zero out power values less than this
#define PROXYHOST "10.0.x.x"    // Address of Proxy Server
#define PROXYPORT 8675          // Port of Proxy Server (default=8675)

// Wifi Configuration 
const char* WIFI_SSID = "WIFI_SSID";
const char* WIFI_PWD  = "WIFI_PASSWORD";

// Display GPIO Pins (Clock and Data)
#define CLK1 5    // Display 1 - 4-digit
#define DIO1 4
#define CLK2 14   // Display 2 - 6-digit
#define DIO2 12
#define CLK3 13   // Display 3 - 4-digit
#define DIO3 16

// Includes
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <TM1637TinyDisplay.h>
#include <TM1637TinyDisplay6.h>

// INITIALIZE TM1637TinyDisplay
TM1637TinyDisplay display1(CLK1, DIO1);   // Solar
TM1637TinyDisplay display2(CLK2, DIO2);   // Battery Level
TM1637TinyDisplay6 display3(CLK3, DIO3);  // Power for Grid, House, Bat

// WIFI
ESP8266WiFiMulti WiFiMulti;
WiFiClient client;
HTTPClient http;

// Globals
const uint8_t ANIMATION[36][4] PROGMEM = { 
  /* Animation Data - HGFEDCBA Map */
  { 0x00, 0x00, 0x00, 0x00 },  // Frame 0
  { 0x01, 0x00, 0x00, 0x00 },  // Frame 1
  { 0x40, 0x01, 0x00, 0x00 },  // Frame 2
  { 0x08, 0x40, 0x00, 0x01 },  // Frame 3
  { 0x00, 0x08, 0x01, 0x40 },  // Frame 4
  { 0x01, 0x00, 0x40, 0x08 },  // Frame 5
  { 0x40, 0x01, 0x08, 0x00 },  // Frame 6
  { 0x08, 0x40, 0x00, 0x01 },  // Frame 7
  { 0x00, 0x08, 0x01, 0x40 },  // Frame 8
  { 0x01, 0x01, 0x40, 0x08 },  // Frame 9
  { 0x40, 0x40, 0x09, 0x00 },  // Frame 10
  { 0x08, 0x08, 0x40, 0x01 },  // Frame 11
  { 0x01, 0x00, 0x08, 0x40 },  // Frame 12
  { 0x40, 0x01, 0x00, 0x08 },  // Frame 13
  { 0x08, 0x40, 0x01, 0x00 },  // Frame 14
  { 0x01, 0x09, 0x41, 0x01 },  // Frame 15
  { 0x40, 0x40, 0x48, 0x40 },  // Frame 16
  { 0x08, 0x08, 0x08, 0x08 },  // Frame 17
  { 0x00, 0x00, 0x00, 0x00 },  // Frame 18
  { 0x00, 0x08, 0x00, 0x00 },  // Frame 19
  { 0x00, 0x00, 0x08, 0x00 },  // Frame 20
  { 0x00, 0x00, 0x00, 0x08 },  // Frame 21
  { 0x00, 0x00, 0x00, 0x04 },  // Frame 22
  { 0x00, 0x00, 0x00, 0x02 },  // Frame 23
  { 0x00, 0x00, 0x00, 0x01 },  // Frame 24
  { 0x00, 0x00, 0x01, 0x00 },  // Frame 25
  { 0x00, 0x01, 0x00, 0x00 },  // Frame 26
  { 0x01, 0x00, 0x00, 0x00 },  // Frame 27
  { 0x20, 0x00, 0x00, 0x00 },  // Frame 28
  { 0x10, 0x00, 0x00, 0x00 },  // Frame 29
  { 0x08, 0x00, 0x00, 0x00 },  // Frame 30
  { 0x00, 0x08, 0x00, 0x00 },  // Frame 31
  { 0x00, 0x00, 0x08, 0x00 },  // Frame 32
  { 0x00, 0x00, 0x00, 0x08 },  // Frame 33
  { 0x00, 0x00, 0x00, 0x80 },  // Frame 34
  { 0x00, 0x00, 0x00, 0x3f }   // Frame 35
};
char line[1500];
char host[20];
char ver[20];
float grid, house, solar, battery, batterylevel;
char dgrid[100], dhouse[100], dsolar[100], dbattery[100], dbatterylevel[100];
unsigned long previousMillis = 0;         // prior counter
const long interval = 5000;               // interval counter in milliseconds
const int fetch = 20 / (interval / 1000); // update data from server in seconds
unsigned long previousSecond = 0;
int counter = 1;
int count = fetch;
int state = 0;
int dispstate = 0;
time_t today;
int animation = 0;     // frame counter for animation (1 per sec)
unsigned char sym[6];
char url[100];

// ------------------------------------ SETUP ------------------------------------
// ------------------------------------ SETUP ------------------------------------
// ------------------------------------ SETUP ------------------------------------

void setup() {
  time_t timevar;

  display1.setBrightness(2);
  display2.setBrightness(BRIGHT_HIGH);
  display3.setBrightness(BRIGHT_HIGH);

  display1.clear();
  display3.showString("TESLA");
  display2.showString("boot");

  sprintf(host, "Solar-%d", SENSORID);
  sprintf(ver, "v%0.2f", VERSION);
  sprintf(url, "http://%s:%d/csv", PROXYHOST, PROXYPORT);

  display1.showString(host);
  display2.showString(ver);
  display3.showString("TESLA");
  display2.showString("boot");
  delay(100);
  display1.showAnimation_P(ANIMATION, FRAMES(ANIMATION), TIME_MS(10));

  // Start up Serial
  Serial.begin(115200);
  delay(1000);
  while (!Serial);   // time to get serial running
  Serial.println();
  Serial.printf("SolarStation %s [%s]\n", host, ver);
  Serial.println("Starting up WiFi...");

  // Initialize the WiFi system
  WiFi.mode(WIFI_STA);              // remove Fairylink default AP
  WiFi.begin(WIFI_SSID, WIFI_PWD);

  int counter = 0;
  WiFi.hostname(host);
  display3.showString("SCAN..");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    // display1.showNumber(counter);
    if ((counter % 4) == 0) display1.showString("-   ");
    if ((counter % 4) == 1) display1.showString(" -  ");
    if ((counter % 4) == 2) display1.showString("  - ");
    if ((counter % 4) == 3) display1.showString("   -");
    display2.showNumber(counter);
    counter++;
    if (counter > 600) {
      // reboot
      ESP.restart();
    }
  }
  display3.showString("ONLINE");
  delay(1000);
  display1.clear();
  display3.clear();
  display2.clear();

}


// ------------------------------------ LOOP ------------------------------------
// ------------------------------------ LOOP ------------------------------------
// ------------------------------------ LOOP ------------------------------------

void loop() {

  unsigned long currentMillis = millis();

  // ANIMATION for Power Direction - Run every 250ms
  if (currentMillis - previousSecond >= 250) {
    previousSecond = currentMillis;
    animation++;
    if (animation > 4) {
      animation = 0;
    }
    // Animation for Battery
    sym[0] = animationSeg(battery);
    display2.setSegments(sym, 1, 0);

    // Animation for Grid
    if (state == 2) {
      sym[0] = animationSeg(grid);
      display3.setSegments(sym, 1, 1);
    }
    if (state == 0) {
      sym[0] = animationSeg(battery);
      display3.setSegments(sym, 1, 1);
    }

  }
  
  // DISPLAY Metrics based on State - Run every interval (ms)
  if ((currentMillis - previousMillis >= interval) || (currentMillis < previousMillis) ) {
    previousMillis = currentMillis;

    // FETCH Data from Powerwall Proxy
    if (count >= fetch) {
      count = 0;
      // wait for WiFi connection
      if ((WiFiMulti.run() == WL_CONNECTED)) {
        Serial.print("[HTTP] begin...\n");
        
        // pull solar data from server
        if (http.begin(client, url)) {  // HTTP
          Serial.printf("[HTTP] GET %s\n", url);
          
          // start connection and send HTTP header
          int httpCode = http.GET();
          if (httpCode > 0) {  
            // Sent GET Request
            Serial.printf("[HTTP] GET... code: %d\n", httpCode);
            if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
              // Read response
              String payload = http.getString();
              payload.toCharArray(line, 1400);
              Serial.printf("Payload=%s\n", line);
              // Expected format: grid,house,solar,battery,batterylevel
              int result = sscanf(line, "%f,%f,%f,%f,%f",
                                  &grid, &house, &solar, &battery, &batterylevel);
              Serial.printf("results = %d [%f, %f, %f, %f, %f]\n",
                            result, grid, house, solar, battery, batterylevel);
              if (result < 5) {
                Serial.println("Data Error on GET\n");
              }
              else {
                // convert battery level range to match Tesla App
                batterylevel = ( (batterylevel / 0.95) - (5 / 0.95) );

                // filter out close to zero noise
                if (abs(grid) < FILTER) grid = 0;
                if (abs(solar) < FILTER) solar = 0;
                if (abs(battery) < FILTER) battery = 0;

                // create formatted strings
                sprintf(dgrid, "E%5.0f", (float(abs(grid))));
                sprintf(dhouse, "H%5.0f", (house));
                sprintf(dsolar, "%0.1f", (solar / 1000.0));
                sprintf(dbattery, "P%5.0f", (float(abs(battery))));
                sprintf(dbatterylevel, "%0.0f", batterylevel);

                Serial.printf("Strings = %s, %s, %s, %s, %s\n",
                              dgrid, dhouse, dsolar, dbattery, dbatterylevel);
              }
            }
            else {
              Serial.printf("[HTTP] Server Error %d\n", httpCode);
            }
          } else {
            Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
          }
          http.end();
        } else {
          Serial.printf("[HTTP] Unable to connect\n");
        }
      } // if ((WiFiMulti.run() == WL_CONNECTED))
      else
      {
        // Unable to connect to WiFi - Retry
        counter = 0;
        display3.showString("SCAN.."); // SEArCH
        while (WiFi.status() != WL_CONNECTED) {
          delay(500);
          Serial.print(".");
          if ((counter % 4) == 0) display1.showString("-   ");
          if ((counter % 4) == 1) display1.showString(" -  ");
          if ((counter % 4) == 2) display1.showString("  - ");
          if ((counter % 4) == 3) display1.showString("   -");
          display2.showNumber(counter);
          counter++;
          if (counter > 600) {
            // reboot
            ESP.restart();
          }
        }
        display3.showString("ONLINE");
        display1.clear();
        display2.clear();
        delay(500);
      } // else
    } // if (count >= fetch)

    // Refresh displays

    // Solar Production
    display1.showNumber(int(solar));

    // Battery Levels
    // display2.showLevel(int(batterylevel), false);
    display2.showNumber(int(batterylevel));

    // House, Grid and Battery
    // To add a dot after Letter: display.showString("______",6,0,0b10000000);
    switch (state) {
      case 0:
        // House (H)
        display3.setBrightness(6);
        display3.showString(dhouse, 6, 0);
        state++;
        break;
      case 1:
        // Grid (E)
        display3.setBrightness(4);
        display3.showString(dgrid, 6, 0);
        state++;
        break;
      case 2:
      default:
        // Battery (P)
        display3.setBrightness(2);
        display3.showString(dbattery, 6, 0);
        state = 0;
        break;
    }
    count++;
  }
}

// Function Helper - Return animation frame char
char animationSeg(int value) {
  int frame;
  frame = animation;
  if (value == 0) return (0b00000000); // no flow
  if (value > 0) {
    frame = 4 - animation;
  }
  switch (frame) {
    case 0:
      return (0b00000000);
    case 1:
      return (0b00001000);
    case 2:
      return (0b01000000);
    case 3:
      return (0b00000001);
    case 4:
    default:
      return (0b00000000);
  }
}
