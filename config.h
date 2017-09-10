/* SolarGuardn config.h */

#define VERSION     "0.7.01"
#define DEBUG
#define USERCONFIG  "/temp/userconfig.h"  // include user config from outside project directory

#include <ESP8266WiFi.h>        // Install Arduino core for ESP8266 from:
#include <ESP8266mDNS.h>        // https://github.com/esp8266/Arduino
#include <ESP8266HTTPClient.h>  //
#include <WiFiUdp.h>            //
#include <ArduinoOTA.h>         // provides each the above libraries
#include <time.h>
#include <Math.h>
#include "FS.h"

WiFiServer server(80);

/** user config **/
#ifdef USERCONFIG
#include USERCONFIG
#else
String host = "SolarGuardn";
String WIFI_SSID = "SSID";        // set WiFi and AIO here
String WIFI_PASS = "PASSWORD";    // still working on runtime config
String IO_USERNAME = "AIO-user";  // https://io.adafruit.com/
String IO_KEY = "AIO-key";
String OTA_PASS = "";
String onURL = "http://sonoff.fqdn/cm?cmnd=Power%20on";
String offURL = "http://sonoff.fqdn/cm?cmnd=Power%20off";
#endif

int TZ = -6;
int Air = 400;                    // value in air
int Water = 800;                  // value in water
int interval = (Water - Air) / 3; // split into dry, wet, soaked
int numReads = 10;                // number of samples to average
bool Fahrenheit = true;           // display Temp in Fahrenheit

/* BME280 config **/
#include <Adafruit_Sensor.h>      // install Adafruit_Sensor and Adafruit_BME280 using library manager
#include <Adafruit_BME280.h>
#include <Wire.h>
#define BMEid 0x76
Adafruit_BME280 bme; // I2C

/** Adafruit IO Config **/
#include "AdafruitIO_WiFi.h"        // install AdafruitIO and Adafruit_MQTT using Library manager
#include "Adafruit_MQTT.h"          /* <-- modify #define MAXSUBSCRIPTIONS 10 */
#include "Adafruit_MQTT_Client.h"
AdafruitIO_WiFi io(IO_USERNAME.c_str(), IO_KEY.c_str(), WIFI_SSID.c_str(), WIFI_PASS.c_str());
AdafruitIO_Feed *IOtemp =     io.feed("temperature");
AdafruitIO_Feed *IOhumid =    io.feed("humidity");
AdafruitIO_Feed *IOmoist =    io.feed("moisture");
AdafruitIO_Feed *IOpressure = io.feed("pressure");
AdafruitIO_Feed *IOwater =    io.feed("watering");
AdafruitIO_Feed *IOrelay =    io.feed("relay");
AdafruitIO_Feed *IOfeed07 =   io.feed("feed07");
AdafruitIO_Feed *IOfeed08 =   io.feed("feed08");
AdafruitIO_Feed *IOfeed09 =   io.feed("feed09");
AdafruitIO_Feed *IOfeed10 =   io.feed("feed10");

/** pin defs **/
#define MOIST     A0    // analog input from soil moisture sensor
#define MPOW      D2    // power output to sensors (Q1)
#define BUTTON    D3    // flash button interupt
#define RELAY     D4    // Relay output (Q2)
#define I2C_CLK   D5    // I2C clock (SCK)
#define I2C_DAT   D6    // I2C data (SDI)

#define CONFIG  "/config.txt"

// initialize vars

int soil = 0, soil_l = 0;
float temp = 0, humid = 0;
int temp_l = 0, humid_l = 0;
int relay = 0, water = 0;
int pressure = 0, pressure_l = 0;
int caliCount = 0;
int deBounce = 0;

volatile int buttonState = HIGH;

