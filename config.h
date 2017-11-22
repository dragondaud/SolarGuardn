/* SolarGuardn config.h */

#define VERSION     "0.8.00"
#define EIGHT

#define DEBUG
#define TELNET
#define TELNET_PORT 23
#define MQTT
#define WWW
#define OTA
//#define WATERCON
//#define BME
#define HDC

/** BEGIN USER CONFIG **/
#define USERCONFIG              // include local user config, ignored by git, instead of defaults
#ifdef USERCONFIG
#include "userconfig.h"         // copy the following initializers to userconfig.h and edit as needed
#else
String HOST = "SolarGuardn";    // hostname for DHCP
int TZ = -6;                    // time zone offset, in hours
uint16_t OTA_PORT = 8266;       // default port 8266
String OTA_PASS = "";           // set OTA update password or blank for none
int STIME = 120;                // time delay between sampling analog input in milliseconds
int nREAD = 3;                  // number of samples to average
bool FAHRENHEIT = true;         // display temperature in Fahrenheit
int AIR = 220;                  // sensor value, in air
int WATER = 640;                // sensor value, in water
int MAXWATER = 120;             // max watering time, in seconds
int MINWAIT = 300;              // min time, in seconds, to wait between waterings
String WIFI_SSID = "SSID";
String WIFI_PASS = "PASSWORD";
String MQTT_SERV = "mqtt.local";
uint16_t MQTT_PORT = 1883;
String MQTT_TOPIC = "MQTT";
String MQTT_USER = "";
String MQTT_PASS = "";
String onURL = "http://sonoff.fqdn/api/relay/0?apikey=XXXXX&value=1";
String offURL = "http://sonoff.fqdn/api/relay/0?apikey=XXXXX&value=0";
#endif // USERCONFIG
/** END USER CONFIG **/

#define CONFIG  "/config.txt"     // SPIFFS config file

/* includes */
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <time.h>
#include <math.h>
#include <FS.h>
#include <pgmspace.h>               // for flash constants to save ram

#include "EspSaveCrash.h"             // https://github.com/krzychb/EspSaveCrash
#ifdef SAVE_CRASH_SPACE_SIZE
#undef SAVE_CRASH_SPACE_SIZE
#endif
#define SAVE_CRASH_SPACE_SIZE 0x1000  // FLASH space reserved to store crash data

#ifdef OTA
#include <ArduinoOTA.h>             // Over-the-Air updates
#endif

#ifdef BME  /* BME280 config **/
#include <Adafruit_Sensor.h>        // install Adafruit_Sensor and Adafruit_BME280 using Library Manager
#include <Adafruit_BME280.h>
#include <Wire.h>
#define BMEid 0x76                  // BME280 I2C id, default 0x77, alt is 0x76
Adafruit_BME280 bme;                // using I2C comms
#else /* HDC1080 */
#include <Wire.h>
#include "ClosedCube_HDC1080.h"
ClosedCube_HDC1080 hdc;
#endif
bool BME = false;                   // is sensor present

/* MQTT */
#ifdef MQTT
#include <PubSubClient.h>           // install PubSubClient using Library Manager
WiFiClient wifiClient;
PubSubClient MQTTclient(wifiClient);
#endif

/** Web Server **/
#ifdef WWW
WiFiServer wwwServer(80);
#endif

/** serial or telnet output **/
#ifdef TELNET
WiFiServer  telnetServer(TELNET_PORT);
WiFiClient  telnetClient;
#endif

/** pin defs **/
#define MOIST A0    // moisture sensor analog input
#define MPOW  5     // D1, moisture sensor power
#define MGND  4     // D2, moisture sensor ground

#ifdef BME
#define BCLK  2     // D4, BME280 I2C SCL (clock)
#define BDAT  14    // D5, BME280 I2C SDA (data)
#define BGND  12    // D6, BME280 ground
#define BPOW  13    // D7, BME280 power
#else
#define BCLK  D4    // HDC1080 I2C SCL (clock)
#define BDAT  D5    // HDC1080 I2C SDA (data)
#define BPOW  D6    // HDC1080 power
#define BGND  D7    // HDC1080 ground
#endif

/** initialize vars **/
int DELAY = 5000;
int soil = 0, soil_l = 1023;
float temp = 0, humid = 0;
int temp_l = 0, humid_l = 0;
int relay = 0, water = 0;
int pressure = 0, pressure_l = 0;
int startCalibrate = 0;
long deBounce = 0, wTime = 0;
volatile int buttonState = HIGH;
int interval = (WATER - AIR) / 3;   // split into dry, wet, soaked

/** FLASH constants, to save on RAM **/
static const PROGMEM char DOT[] = ".";
static const PROGMEM char COMMA[] = ", ";
static const PROGMEM char EOL[] = "\033[K\r\n";

#ifdef WWW
static const PROGMEM char HTTPOK[] = "HTTP/1.1 200 OK";
static const PROGMEM char WWWSTAT[] = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<!DOCTYPE HTML>\
<html><head>\
  <meta http-equiv='refresh' content='60;URL=/'/>\
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
  <p>Soil Moisture: %u </p>";
#endif
