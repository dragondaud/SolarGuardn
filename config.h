/* SolarGuardn config.h */

#define VERSION     "0.7.06"

#define USERCONFIG    // include local user config, ignored by git

/* Compile time options */
#define OTA
#define OTA_PORT 8266
#define DEBUG         // Output messages on serial (and telnet if enabled)
#define TELNET
#define TELNET_PORT 23
#define WWW           // enable WWW server status page
#define STIME 100     // time delay between sampling analog input in milliseconds
#define nREAD 3       // number of samples to average
#define MAXWATER 60   // Max time, in seconds, to water
#define MINWAIT 300   // Wait at least 5 minutes before watering again

/* includes */
#include <ESP8266WiFi.h>        // Install Arduino core for ESP8266 from:
#include <ESP8266mDNS.h>        // https://github.com/esp8266/Arduino
#include <ESP8266HTTPClient.h>  // use current from git, not 2.3.0 release
#include <WiFiUdp.h>            // provides each of these libraries
#ifdef OTA
#include <ArduinoOTA.h>         // Optional Over-the-Air updates
#endif
#include "EspSaveCrash.h"
#include <time.h>
#include <Math.h>
#include <FS.h>
#include <pgmspace.h>           // for flash constants to save ram

#define SAVE_CRASH_SPACE_SIZE   0x07FF  // space reserved to store crash data

/** BEGIN USER CONFIG **/
#ifdef USERCONFIG
#include "userconfig.h"
#else
#define HOST "SolarGuardn"
#define WIFI_SSID "SSID"           // set WiFi and AIO here or in USERCONFIG
#define WIFI_PASS "PASSWORD"       // still working on runtime config
#define IO_USERNAME "AIO-user"     // https://io.adafruit.com/
#define IO_KEY "AIO-key"
#define OTA_PASS ""
#define TZ -6                      // timezone offset from GMT
#define onURL "http://sonoff.fqdn/api/relay/0?apikey=XXXXX&value=1"
#define offURL "http://sonoff.fqdn/api/relay/0?apikey=XXXXX&value=0"
#endif

int Air = 220;                     // sensor value, in air
int Water = 640;                   // sensor value, in water
int interval = (Water - Air) / 3;  // split into dry, wet, soaked
bool Fahrenheit = true;            // display Temp in Fahrenheit
/** END USER CONFIG **/

#define CONFIG  "/config.txt"     // SPIFFS config file

/* BME280 config **/
#include <Adafruit_Sensor.h>      // install Adafruit_Sensor and Adafruit_BME280 using library manager
#include <Adafruit_BME280.h>
#include <Wire.h>
#define BMEid 0x76                // BME280 I2C id, default 0x77, alt is 0x76
Adafruit_BME280 bme; // I2C
bool BME = false;                  // is BME sensor present

/** Adafruit IO Config **/
#include "AdafruitIO_WiFi.h"        // install AdafruitIO and Adafruit_MQTT using Library manager
#include "Adafruit_MQTT.h"          // <-- then modify this file with #define MAXSUBSCRIPTIONS 10
#include "Adafruit_MQTT_Client.h"
AdafruitIO_WiFi io(IO_USERNAME, IO_KEY, WIFI_SSID, WIFI_PASS);
AdafruitIO_Feed *IOtemp =     io.feed("temperature"); // ambient temperature
AdafruitIO_Feed *IOhumid =    io.feed("humidity");    // relative humidity
AdafruitIO_Feed *IOpressure = io.feed("pressure");    // atmospheric pressure
AdafruitIO_Feed *IOmoist =    io.feed("moisture");    // soil moisture content
AdafruitIO_Feed *IOwater =    io.feed("watering");    // state of water pump
AdafruitIO_Feed *IOrelay =    io.feed("relay");       // ext relay contact
AdafruitIO_Feed *IOdebug =    io.feed("debug");       // debugging messages
AdafruitIO_Feed *IOfeed =     io.feed("feed");        // unused

#ifdef WWW
/** Web Server **/
WiFiServer wwwServer(80);
#endif

/** serial or telnet output **/
#ifdef TELNET
WiFiServer  telnetServer(TELNET_PORT);
WiFiClient  telnetClient;
#endif

/** pin defs **/
#define MOIST     A0    // analog input from soil moisture sensor
#define MPOW      D2    // power output to soil moisture sensor
#define BUTTON    D3    // flash button interrupt
#define I2C_CLK   D5    // I2C clock (SCK)
#define I2C_DAT   D6    // I2C data (SDI)

/** initialize vars **/

int soil = 0, soil_l = 0;
float temp = 0, humid = 0;
int temp_l = 0, humid_l = 0;
int relay = 0, water = 0;
int pressure = 0, pressure_l = 0;
int caliCount = 0;
long deBounce = 0, wTime = 0;

volatile int buttonState = HIGH;

/** FLASH constants, to save on RAM **/

static const PROGMEM char NIL[] = "";
static const PROGMEM char DOT[] = ".";
static const PROGMEM char COMMA[] = ", ";
static const PROGMEM char EOL[] = "\033[K\r\n";

#ifdef WWW
static const PROGMEM char WWWSTAT[] = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<!DOCTYPE HTML>\
<html><head>\
  <meta http-equiv='refresh' content='60'/>\
  <link rel=\"shortcut icon\" href=\"fav.ico\" type=\"image/x-icon\" />\
  <title>SolarGuardn</title>\
  <style>\
    body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }\
  </style>\
</head>\
<body>\
  <h1>SolarGuardn %s </h1>\
  <p>%s</p>\
  <p>uptime %s</p>\
  <p>Temperature: %u &deg;F</p>\
  <p>Humidity: %u%% RH</p>\
  <p>Abs Pressure: %s inHg</p>\
  <p>Soil Moisture: %u </p>\
</body>\
</html>";
#endif
