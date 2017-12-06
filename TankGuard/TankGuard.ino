/*
   SolarGuardn - TankGuard v0.8.00 PRE-RELEASE 04-Dec-2017
   by David Denney <dragondaud@gmail.com>

   TankGuard monitors water level in tank and reports data using MQTT.

   This code is offered "as is" with no warranty, expressed or implied, for any purpose,
   and is released to the public domain, while all libraries retain their respective licenses.

   Master repository: https://github.com/dragondaud/SolarGuardn

   Designed to run on an NodeMCU board with a BME280 and ultrasonic range finder
   Board: NodeMCU 1.0, Freq: 80MHz, Flash: 4M (1M SPIFFS), Speed: 115200, Port: serial or OTA IP

*/

#include <ESP8266WiFi.h>          // Arduino IDE ESP8266 from https://github.com/esp8266/Arduino
#include <ESP8266HTTPClient.h>    // included
#include <ArduinoOTA.h>           // included
#include <time.h>                 // included
#include <Wire.h>                 // included
#include <ArduinoJson.h>          // install ArduinoJson using library manager, https://github.com/bblanchon/ArduinoJson/
#include <BME280I2C.h>            // install BME280 using library manager, https://github.com/finitespace/BME280
#include <EspSaveCrash.h>         // install EspSaveCrash using library manager, https://github.com/krzychb/EspSaveCrash
#include <PubSubClient.h>         // install PubSubClient using library manager, https://github.com/knolleary/pubsubclient

#define USERCONFIG

#ifdef USERCONFIG
#include userconfig.h
#else
#define WIFI_SSID "SSID"
#define WIFI_PASS "PASS"
#define MQTT_SERV "mqtt.local"
#define MQTT_PORT 1883
#define MQTT_TOPIC "SolarGuardn"
#define MQTT_USER ""
#define MQTT_PASS ""
#define BETWEEN 60000             // delay between readings in loop()
#endif

#define POW D4
#define GND D5
#define SCL D6
#define SDA D7

#define TRIG D2
#define ECHO D1

WiFiClient    wifiClient;
PubSubClient  MQTTclient(wifiClient);

const char* UserAgent = "SolarGuardn/1.0 (Arduino ESP8266)";

String location;                 // set to postal code or region name to bypass geoIP lookup

String HOST;
String PUB_IP;

// openssl s_client -connect maps.googleapis.com:443 | openssl x509 -fingerprint -noout
const char* gMapsCrt = "‎‎67:7B:99:A4:E5:A7:AE:E4:F0:92:01:EF:F5:58:B8:0B:49:CF:53:D4";
const char* gMapsKey = "AIzaSyChydnQOGtnS-G1BH0ZVNtKpItRfwO23aY"; // https://developers.google.com/maps/documentation/timezone/intro

time_t  TWOAM;
time_t  UPTIME;
long    TIMER;

bool    isBME = false;

BME280I2C::Settings settings(
  BME280::OSR_X4,  // tempOSR
  BME280::OSR_X4,  // humOSR
  BME280::OSR_X1,  // presOSR
  BME280::Mode_Forced,
  BME280::StandbyTime_1000ms,
  BME280::Filter_Off,
  BME280::SpiEnable_False,
  0x76 // I2C address
);

BME280I2C bme(settings);

void setup() {
  Serial.begin(115200);
  //Serial.setDebugOutput(true);
  while (!Serial);
  Serial.flush();
  delay(100);
  Serial.println();
  Serial.print(F("setup: WiFi connecting to "));
  Serial.print(WIFI_SSID);
  Serial.print(F("..."));
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(F("."));
    delay(500);
  }
  Serial.println(F(" OK"));

  if (location == "") {
    location = getIPlocation();
  } else {
    getIPlocation();
    location = getLocation(location, gMapsKey);
  }
  setNTP();
  HOST = WiFi.hostname();

  ArduinoOTA.onStart([]() {
    mqttPublish("debug", "OTA UPDATE");
    Serial.println(F("\nOTA: Start"));
  } );
  ArduinoOTA.onEnd([]() {
    Serial.println(F("\nOTA: End"));
    delay(500);
  } );
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.print("OTA Progress: " + String((progress / (total / 100))) + " \r");
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.print("\nError[" + String(error) + "]: ");
    if (error == OTA_AUTH_ERROR) Serial.println(F("Auth Failed"));
    else if (error == OTA_BEGIN_ERROR) Serial.println(F("Begin Failed"));
    else if (error == OTA_CONNECT_ERROR) Serial.println(F("Connect Failed"));
    else if (error == OTA_RECEIVE_ERROR) Serial.println(F("Receive Failed"));
    else if (error == OTA_END_ERROR) Serial.println(F("End Failed"));
    else Serial.println(F("unknown error"));
    delay(5000);
    ESP.restart();
  });
  ArduinoOTA.begin();

  MQTTclient.setServer(MQTT_SERV, MQTT_PORT);
  mqttConnect();

  pinMode(BUILTIN_LED, OUTPUT);

  pinMode(TRIG, OUTPUT);
  digitalWrite(TRIG, LOW);
  pinMode(ECHO, INPUT);

  pinMode(GND, OUTPUT);
  digitalWrite(GND, LOW);
  pinMode(POW, OUTPUT);
  digitalWrite(POW, HIGH);

  Wire.begin(SDA, SCL);
  delay(100);
  isBME = bme.begin();
  if (!isBME) {
    Serial.println(F("cannot find BME280 sensor"));
    delay(5000);
    ESP.restart();
  }

  delay(1000);
  UPTIME = time(nullptr);

  Serial.print(F("Last reset reason: "));
  Serial.println(ESP.getResetReason());
  Serial.print(F("WiFi Hostname: "));
  Serial.println(HOST);
  Serial.print(F("WiFi IP addr: "));
  Serial.println(WiFi.localIP());
  Serial.print(F("WiFi gw addr: "));
  Serial.println(WiFi.gatewayIP());
  Serial.print(F("WiFi MAC addr: "));
  Serial.println(WiFi.macAddress());
  Serial.print(F("ESP Sketch size: "));
  Serial.println(ESP.getSketchSize());
  Serial.print(F("ESP Flash free: "));
  Serial.println(ESP.getFreeSketchSpace());
  Serial.print(F("ESP Flash Size: "));
  Serial.println(ESP.getFlashChipRealSize());
  SaveCrash.print(Serial);
} // setup

void loop() {
  checkSer();
  ArduinoOTA.handle();
  MQTTclient.loop();
  if (millis() > TIMER) {
    TIMER = millis() + BETWEEN;
  } else {
    delay(5000);
    return;
  }
  analogWrite(BUILTIN_LED, 1);
  float temp, humid, pressure;
  int heap = ESP.getFreeHeap();
  time_t now = time(nullptr);
  if (now > TWOAM) {
    Serial.println();
    setNTP();
  }
  String t = ctime(&now);
  t.trim(); // ctime returns extra whitespace
  String u = upTime(now);
  int range = getDist();
  if (isBME) {
    bme.read(pressure, temp, humid);
    temp = temp * 1.8F + 32.0F;
    pressure = pressure * 0.02953;
    Serial.printf("%s, %d°F, %d%%RH, %d.%d inHg, %d mm, %s uptime, %d heap \r", t.c_str(), \
                  round(temp), round(humid), round(pressure / 100), int(pressure) % 100, range, u.c_str(), heap);
  } else {
    Serial.printf("%s, %d mm, %s uptime, %d heap \r", t.c_str(), range, u.c_str(), heap);
  }
  t = "{\"temp\": " + String(round(temp)) + ", \"humid\": " + String(round(humid)) + \
      ", \"range\": " + String(range) + ", \"timestamp\": " + String(now) + ", \"freeheap\": " + String(heap) + "}";
  mqttPublish("data", t);
  for (int i = 23; i < 1023; i++) {
    analogWrite(BUILTIN_LED, i);
    delay(2);
  }
  analogWrite(BUILTIN_LED, 0);
  digitalWrite(BUILTIN_LED, HIGH);
} // loop

void checkSer(void) {
  if (Serial.available() > 0) {
    char inChar = Serial.read();
    switch (inChar) {
      case '0':
        Serial.println(F("\nAttempting to divide by zero ..."));
        int result, zero;
        zero = 0;
        result = 1 / zero;
        Serial.print("Result = ");
        Serial.println(result);
        break;
      case 'e':
        Serial.println(F("\nAttempting to read through a pointer to no object ..."));
        int* nullPointer;
        nullPointer = NULL;
        Serial.print(*nullPointer);
        break;
      case 'c':
        SaveCrash.clear();
        Serial.println(F("\nCrash information cleared"));
        break;
      case 'r':
        Serial.println(F("\nRebooting..."));
        delay(1000);
        ESP.restart();
        break;
      default:
        Serial.println(F("\nc : clear crash information"));
        Serial.println(F("e : attempt to read through a pointer to no object"));
        Serial.println(F("0 : attempt to divide by zero"));
        Serial.println(F("r : restart esp"));
        break;
    }
  }
}

int getDist() {
  int r = 0, c = 0;
  for (int i = 0; i < 10; i++) {
    digitalWrite(TRIG, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG, LOW);
    unsigned long p = pulseIn(ECHO, HIGH, 12000) * 0.34 / 2;
    if (!p == 0 || p > 400) {
      r += p;
      c++;
    }
    delay(20);
  }
  if (!c==0) return round(r / c);
  else return -1;
} // getDist

String UrlEncode(const String url) {
  String e;
  for (int i = 0; i < url.length(); i++) {
    char c = url.charAt(i);
    if (c == 0x20) {
      e += "%20";
    } else if (isalnum(c)) {
      e += c;
    } else {
      e += "%";
      if (c < 0x10) e += "0";
      e += String(c, HEX);
    }
  }
  return e;
} // UrlEncode

String getIPlocation() { // Using freegeoip.net to map public IP's location
  HTTPClient http;
  String URL = "http://freegeoip.net/json/";
  String payload;
  String loc;
  http.setUserAgent(UserAgent);
  if (!http.begin(URL)) {
    Serial.println(F("getIPlocation: [HTTP] connect failed!"));
  } else {
    int stat = http.GET();
    if (stat > 0) {
      if (stat == HTTP_CODE_OK) {
        payload = http.getString();
        DynamicJsonBuffer jsonBuffer;
        JsonObject& root = jsonBuffer.parseObject(payload);
        if (root.success()) {
          String region = root["region_name"];
          String country = root["country_code"];
          String lat = root["latitude"];
          String lng = root["longitude"];
          loc = lat + "," + lng;
          String ip = root["ip"];
          PUB_IP = ip;
          Serial.println("getIPlocation: " + region + ", " + country);
        } else {
          Serial.println(F("getIPlocation: JSON parse failed!"));
          Serial.println(payload);
        }
      } else {
        Serial.printf("getIPlocation: [HTTP] GET reply %d\r\n", stat);
      }
    } else {
      Serial.printf("getIPlocation: [HTTP] GET failed: %s\r\n", http.errorToString(stat).c_str());
    }
  }
  http.end();
  return loc;
} // getIPlocation

String getLocation(const String address, const char* key) { // using google maps API, return location for provided Postal Code
  HTTPClient http;
  String URL = "https://maps.googleapis.com/maps/api/geocode/json?address="
               + UrlEncode(address) + "&key=" + String(key);
  String payload;
  String loc;
  http.setIgnoreTLSVerifyFailure(true);   // https://github.com/esp8266/Arduino/pull/2821
  http.setUserAgent(UserAgent);
  if (!http.begin(URL, gMapsCrt)) {
    Serial.println(F("getLocation: [HTTP] connect failed!"));
  } else {
    int stat = http.GET();
    if (stat > 0) {
      if (stat == HTTP_CODE_OK) {
        payload = http.getString();
        DynamicJsonBuffer jsonBuffer;
        JsonObject& root = jsonBuffer.parseObject(payload);
        if (root.success()) {
          JsonObject& results = root["results"][0];           // http://arduinojson.org/assistant/
          JsonObject& results_geometry = results["geometry"];
          String address = results["formatted_address"];
          String lat = results_geometry["location"]["lat"];
          String lng = results_geometry["location"]["lng"];
          loc = lat + "," + lng;
          Serial.print(F("getLocation: "));
          Serial.println(address);
        } else {
          Serial.println(F("getLocation: JSON parse failed!"));
          Serial.println(payload);
        }
      } else {
        Serial.printf("getLocation: [HTTP] GET reply %d\r\n", stat);
      }
    } else {
      Serial.printf("getLocation: [HTTP] GET failed: %s\r\n", http.errorToString(stat).c_str());
    }
  }
  http.end();
  return loc;
} // getLocation

int getTimeZone(time_t now, String loc, const char* key) { // using google maps API, return TimeZone for provided timestamp
  HTTPClient http;
  int tz = false;
  String URL = "https://maps.googleapis.com/maps/api/timezone/json?location="
               + UrlEncode(loc) + "&timestamp=" + String(now) + "&key=" + String(key);
  String payload;
  http.setIgnoreTLSVerifyFailure(true);   // https://github.com/esp8266/Arduino/pull/2821
  http.setUserAgent(UserAgent);
  if (!http.begin(URL, gMapsCrt)) {
    Serial.println(F("getTimeZone: [HTTP] connect failed!"));
  } else {
    int stat = http.GET();
    if (stat > 0) {
      if (stat == HTTP_CODE_OK) {
        payload = http.getString();
        DynamicJsonBuffer jsonBuffer;
        JsonObject& root = jsonBuffer.parseObject(payload);
        if (root.success()) {
          tz = (int (root["rawOffset"]) + int (root["dstOffset"])) / 3600;  // combine Offset and dstOffset
          const char* tzname = root["timeZoneName"];
          Serial.printf("getTimeZone: %s (%d)\r\n", tzname, tz);
        } else {
          Serial.println(F("getTimeZone: JSON parse failed!"));
          Serial.println(payload);
        }
      } else {
        Serial.printf("getTimeZone: [HTTP] GET reply %d\r\n", stat);
      }
    } else {
      Serial.printf("getTimeZone: [HTTP] GET failed: %s\r\n", http.errorToString(stat).c_str());
    }
  }
  http.end();
  return tz;
} // getTimeZone

void setNTP () {
  int TZ = getTimeZone(time(nullptr), location, gMapsKey);
  Serial.print(F("setNTP: configure NTP ..."));
  configTime((TZ * 3600), 0, WiFi.gatewayIP().toString().c_str(), "pool.ntp.org", "time.nist.gov");
  while (!time(nullptr)) {
    delay(1000);
    Serial.print(F("."));
  }
  delay(5000);
  time_t now = time(nullptr);
  String t = ctime(&now);
  t.trim();
  Serial.println(t.substring(3));
  struct tm * calendar;
  calendar = localtime(&now);
  calendar->tm_mday++;
  calendar->tm_hour = 2;
  calendar->tm_min = 0;
  calendar->tm_sec = 0;
  TWOAM = mktime(calendar);
  t = ctime(&TWOAM);
  t.trim();
  Serial.print(F("setNTP: next timezone check @ "));
  Serial.println(t);
} // setNTP

String upTime(const time_t now) {
  long t = now - UPTIME;
  long s = t % 60;
  long m = (t / 60) % 60;
  long h = (t / (60 * 60)) % 24;
  char ut[10];
  snprintf(ut, sizeof(ut), "%d:%02d:%02d", h, m, s);
  return String(ut);
} // upTime()

bool mqttConnect() {
  if (MQTTclient.connect(MQTT_TOPIC, MQTT_USER, MQTT_PASS)) {
    time_t now = time(nullptr);
    Serial.print("MQTT connected to ");
    Serial.println(String(MQTT_SERV) + ":" + String(MQTT_PORT));
    String t = "{\"hostname\": \"" + HOST + "\", \"wifi_ip\": \"" + WiFi.localIP().toString() + \
               "\", \"public_ip\": \"" + PUB_IP + "\", \"reset_reason\": \"" + ESP.getResetReason() + \
               "\", \"location\": \"" + location + "\", \"timestamp\": " + String(now) + \
               ", \"freeheap\": " + String(ESP.getFreeHeap()) + "}";
    mqttPublish("debug", t);
  } else {
    Serial.print(MQTTclient.state());
    Serial.println(" MQTT not connected.");
    delay(5000);
    ESP.restart();
  }
} // mqttConnect

bool mqttPublish(String topic, String data) {
  if (!MQTTclient.connected()) mqttConnect();
  int r = MQTTclient.publish((String(MQTT_TOPIC) + "/" + HOST + "/" + topic).c_str(), data.c_str());
  if (!r) Serial.println("MQTT error: " + String(r));
}

