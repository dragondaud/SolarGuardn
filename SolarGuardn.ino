/*
  SolarGuardn v0.8.00 PRE-RELEASE 26-Oct-2017
  by David Denney

  Monitors garden conditions, cycles irrigation control as needed, reports data using MQTT.

  This code is offered "as is" with no warranty, expressed or implied, for any purpose,
  and is released to the public domain, while all libraries retain their respective licenses.

  Master repository: https://github.com/dragondaud/SolarGuardn

  See config.h for configurable settings and includes.

  Designed to run on an ESP-12E NodeMCU board with additional hardware,
  this sketch will monitor soil conditions, ambient temperature, humidity
  and atmospheric pressure, then report changes using MQTT.

  Built-in WWW server:
    / : display temperature, humidity, pressure, soil moisture
    /reset : reboot module
    /calibrate : start calibration
    /crash : display any saved crash report

  Telnet server allows remote monitoring and debugging when serial is not practical.

  Board: NodeMCU 1.0, Freq: 80MHz, Flash: 4M (1M SPIFFS), Speed: 115200, Port: serial or OTA IP

  Some code is based on examples from the ESP8266, ArduinoOTA and other libraries.

  Sketch requires ESP8266 library v2.4.0-rc1, docs at: https://arduino-esp8266.readthedocs.io/en/2.4.0-rc1/
  You can use the release candidate by adding  this to File->Preferences->Additional Board Manager URLs:
    https://github.com/esp8266/Arduino/releases/download/2.4.0-rc1/package_esp8266com_index.json
*/

#include "config.h"

void setup() {
  Serial.begin(115200);               // Initialize Serial at 115200bps, to match bootloader
  //Serial.setDebugOutput(true);        // uncomment for extra debugging
  while (!Serial);                    // wait for Serial to become available
  debugOutLN();
  debugOut(F("SolarGuardn v"));
  debugOutLN(VERSION);

  readConfig();                       // mount SPIFFS, config file not implemented yet

  /* WiFi connect */
  WiFi.hostname(HOST);
#ifdef DEBUG
  debugOut("Connecting to ");
  debugOut(WIFI_SSID);
#endif
  WiFi.begin(WIFI_SSID.c_str(), WIFI_PASS.c_str());
  while (WiFi.status() != WL_CONNECTED) {
#ifdef DEBUG
    debugOut(FPSTR(DOT));
#endif
    delay(500);
  }
#ifdef DEBUG
  debugOutLN();
#endif

  /* configTime sntp */
  configTime((TZ * 3600), 0, "pool.ntp.org", "time.nist.gov");
#ifdef DEBUG
  debugOut(F("Synchronize clock with sntp..."));
#endif
  while (!time(nullptr)) {
    delay(1000);                        // wait for ntp time sync
#ifdef DEBUG
    debugOut(FPSTR(DOT));
#endif
  }
#ifdef DEBUG
  debugOutLN();
#endif

#ifdef OTA
  ArduinoOTA.setHostname(HOST.c_str());
  ArduinoOTA.setPort(OTA_PORT);
  if (OTA_PASS) ArduinoOTA.setPassword(OTA_PASS.c_str());
  ArduinoOTA.onStart([]() {
    Serial.println(F("\r\nOTA Start"));
  } );
  ArduinoOTA.onEnd([]() {
    Serial.println(F("\r\nOTA End"));
  } );
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.print("OTA Progress: " + String((progress / (total / 100))) + " \r");
  });
  ArduinoOTA.onError([](ota_error_t error) {
    debugOut("Error[" + String(error) + "]: ");
    if (error == OTA_AUTH_ERROR) Serial.println(F("Auth Failed"));
    else if (error == OTA_BEGIN_ERROR) Serial.println(F("Begin Failed"));
    else if (error == OTA_CONNECT_ERROR) Serial.println(F("Connect Failed"));
    else if (error == OTA_RECEIVE_ERROR) Serial.println(F("Receive Failed"));
    else if (error == OTA_END_ERROR) Serial.println(F("End Failed"));
    else Serial.println(F("unknown error"));
    ESP.restart();
  });
  ArduinoOTA.begin();
#ifdef DEBUG
  debugOutLN(F("OTA handler started"));
#endif
#endif // OTA

#ifdef WWW
  wwwServer.begin(); /* start web server */
#ifdef DEBUG
  debugOutLN(F("WWW server started"));
#endif
#endif // WWW

  Wire.begin(I2C_DAT, I2C_CLK); /** start I2C for BME280 weather sensor **/
  Wire.setClock(100000);
  BME = bme.begin(BMEid);
  if (!BME) debugOutLN(F("Could not find a valid BME280 sensor"));
  bme.setSampling(Adafruit_BME280::MODE_FORCED,
                  Adafruit_BME280::SAMPLING_X4,  // temperature
                  Adafruit_BME280::SAMPLING_X4,  // pressure
                  Adafruit_BME280::SAMPLING_X4,  // humidity
                  Adafruit_BME280::FILTER_OFF   );

  pinMode(LED_BUILTIN, OUTPUT);                   // enable onboard LED output
  pinMode(MPOW, OUTPUT);                          // moisture sensor power
  digitalWrite(MPOW, LOW);                        //  - initially off
  pinMode(BUTTON, INPUT);                         // flash button for calibration
  controlWater(false);                            // start with water control off

#ifdef TELNET
#ifdef DEBUG
  debugOutLN(F("telnet server started"));
#endif
  telnetServer.begin();
  //telnetServer.setNoDelay(true); // drops chars if set true
#endif // TELNET

#ifdef DEBUG
  SaveCrash.print();
  espStats();
  publish("debug", WiFi.localIP().toString() + " " + ESP.getResetReason());
  debugOut(F("Ready, at "));
  debugOutLN(ttime());
  debugOutLN();
#endif
  delay(5000);  // wait for NTP to stabilize
} // setup()

bool publish (String t, String m) {
  String p = "{" + t + "," + m + "}";
  return MQTTclient.publish(MQTT_TOPIC.c_str(), p.c_str());
} // publish()

template <typename T> void debugOut(const T x) {
  Serial.print(x);
#ifdef TELNET
  if (telnetClient && telnetClient.connected()) {
    telnetClient.print(x);
  }
#endif
  yield();
} // debugOut()

void debugOutLN() {
  debugOut(FPSTR(EOL));
}

template <typename T> void debugOutLN(const T x) {
  debugOut(x);
  debugOut(FPSTR(EOL));
} // debugOutLN()

#ifdef TELNET
void handleTelnet(void) {
  if (telnetServer.hasClient()) {
    if (!telnetClient || !telnetClient.connected()) {
      if (telnetClient) {
        telnetClient.stop();
      }
      telnetClient = telnetServer.available();
      telnetClient.flush();
      yield();
#ifdef DEBUG
      debugOut(F("\r\ntelnet connected from "));
      debugOutLN(telnetClient.remoteIP());
      publish("debug", "telnet " + telnetClient.remoteIP().toString());
      SaveCrash.print(telnetClient);
      SaveCrash.clear();
      espStats();
#endif
    } else {
      telnetServer.available().stop();
    }
  }
} // handleTelnet()
#endif // TELNET

String upTime() {
  long t = millis() / 1000;
  long s = t % 60;
  long m = (t / 60) % 60;
  long h = (t / (60 * 60)) % 24;
  char ut[10];
  snprintf(ut, sizeof(ut), "%d:%02d:%02d", h, m, s);
  return String(ut);
} // upTime()

void espStats() {
  debugOut(F("last reset: "));
  debugOutLN(ESP.getResetReason());
  debugOut(F("WiFi Hostname: "));
  debugOutLN(WiFi.hostname());
  debugOut(F("WiFi IP addr: "));
  debugOutLN(WiFi.localIP());
  debugOut(F("WiFi MAC addr: "));
  debugOutLN(WiFi.macAddress());
  debugOut(F("WiFi Strength: "));
  debugOutLN(WiFi.RSSI());
  debugOut(F("ESP core version: "));
  debugOutLN(ESP.getCoreVersion());
  debugOut(F("ESP SDK version: "));
  debugOutLN(ESP.getSdkVersion());
  debugOut(F("ESP sketch size: "));
  debugOutLN(ESP.getSketchSize());
  debugOut(F("ESP free flash: "));
  debugOutLN(ESP.getFreeSketchSpace());
  debugOut(F("ESP free RAM: "));
  debugOutLN(ESP.getFreeHeap());
  debugOut(F("ESP uptime: "));
  debugOutLN(upTime());
  debugOutLN("Moisture range: " + String(WATER) + " > Soaked > " + String(WATER - interval) \
             + " > Wet > " + String(AIR + interval) + " > Dry > " + String(AIR));
} // espStats()

void controlWater(bool cmd) {         // control Sonoff module running ESPurna
  HTTPClient http;
  if (cmd) {                          // true == turn on pump
    if ((wTime) && (((millis() - wTime) / 1000) < MINWAIT)) return; // wait before turning pump back on
    http.begin(onURL);
#ifdef DEBUG
    debugOut(F("Water ON"));
#endif
  } else {
    http.begin(offURL);
#ifdef DEBUG
    debugOut(F("Water off"));
#endif
  }
  int httpCode = http.GET();
  if (httpCode > 0) {
#ifdef DEBUG
    debugOutLN("; [HTTP] GET: " + String(httpCode));
#endif
    if (httpCode == HTTP_CODE_OK) {             // only change pump state if successful
      if ((water) || (cmd)) wTime = millis();   // track pump time
      water = cmd;
      if (cmd) publish("water", "ON");
      else publish("water", "OFF");
    }
  } else {
    publish("debug", http.errorToString(httpCode));
    debugOutLN(" [HTTP] GET failed: " + http.errorToString(httpCode));
  }
  http.end();
}

void calibrate() {
  debugOutLN(F("==Calibrate Start=="));
  for (int i = 1; i <= 25; i++) {
    yield();
    digitalWrite(MPOW, HIGH);
    delay(i * 10);
    debugOut(String(i * 10) + ":");
    int r = 1023 - analogRead(MOIST);
    digitalWrite(MPOW, LOW);
    debugOut(r);
    delay(i * 12);
    if ((i == 10) || (i == 19) || (i == 25)) debugOut(FPSTR(EOL));
    else debugOut(F(", "));
  }
  debugOutLN(F("==Calibrate End=="));
  startCalibrate = false;
} // calibrate()

int readMoisture(bool VERBOSE) {      // analog input smoothing
  int s = 0;
  for (int i = 0; i < nREAD; i++) {
    int r = 0, x = 0;
    do {
      digitalWrite(MPOW, HIGH);       // power to moisture sensor
      delay(STIME);
      r = 1023 - analogRead(MOIST);   // read analog value from moisture sensor (invert for capacitance sensor)
      digitalWrite(MPOW, LOW);        // turn off moisture sensor
      delay(STIME * 1.2);
      x++;
    } while (((r < AIR) || (r > WATER)) && x < nREAD);  // skip invalid values
    s += r;
    if (VERBOSE) {                    // during calibration, output all values
      debugOut(r);
      debugOut(FPSTR(COMMA));
    }
  }
  int r = round((float)s / (float)nREAD);
  if (VERBOSE) {
    debugOut(F("\b\b = "));
    debugOutLN(r);
  }
  return r;
} // readMoisture()

void readBME() {
  if (!BME) return;
  bme.takeForcedMeasurement();
  temp = bme.readTemperature();                   // read Temp in C
  if (FAHRENHEIT) temp = temp * 1.8F + 32.0F;     // convert to Fahrenheit
  humid = bme.readHumidity();                     // read Humidity
  pressure = round(bme.readPressure() * 0.02953); // read barometric pressure and convert to inHg
}                                                 /*-- add configure option for hPa or inHg --*/

void doMe() {                             // called every 5 seconds to handle background tasks
#ifdef TELNET
  handleTelnet();                         // handle telnet server
  yield();                                // process background tasks
#endif
#ifdef OTA
  ArduinoOTA.handle();                    // handle OTA update requests every 5 seconds
  yield();
#endif
  if (!MQTTclient.connected()) {          // reconnect MQTT if needed
    MQTTclient.connect("TEST");
  }
  MQTTclient.loop();                      // handle MQTT messages
  yield();
#ifdef WWW
  WiFiClient client = wwwServer.available(); // serve web requests
  if (client) handleWWW(client);
  yield();
#endif
  if (startCalibrate) calibrate();
  yield();
} // doMe()

void loop() {
  doMe();
  digitalWrite(LED_BUILTIN, LOW);   // blink LED_BUILTIN (NodeMCU LOW = ON)
  readBME();
  soil = readMoisture(false);
#ifdef DEBUG
  debugOut(String(temp, 2));
  if (FAHRENHEIT) debugOut(F("°F, "));
  else debugOut(F("°C, "));
  debugOut(String(humid, 2));
  debugOut(F("% RH, "));
  debugOut(String(pressure / 100.0F, 2));
  debugOut(F(" inHg, "));
  debugOut(soil);
#endif
  if (soil > (AIR + interval)) {
#ifdef DEBUG
    if (soil > (WATER - interval)) debugOutLN(F(" soaked."));
    else debugOutLN(F(" wet."));
#endif
    if (water) {
      if (soil > (WATER - interval)) {
        publish("debug", "soaked off");
      } else publish("debug", "wet off");
      controlWater(false);
    }
  } else {
#ifdef DEBUG
    debugOutLN(F(" dry."));
#endif
    if (!water) controlWater(true);
  }
  if (soil != soil_l) {                   // if moisture level has changed then
#ifdef DEBUG
    debugOutLN("save moisture " + String(soil));
#endif
    publish("moist", String(soil));    // store soil moisture
    soil_l = soil;
  }
  int tt = round(temp);                   // round off temperature
  if (tt != temp_l && tt < 120) {         // if temp has changed and is valid then
#ifdef DEBUG
    debugOut("save temperature " + String(tt));
    if (FAHRENHEIT) debugOutLN(F("°F"));
    else debugOutLN(F("°C"));
#endif
    publish("temp", String(tt));       // store rounded temp
    temp_l = tt;
  }
  int hh = round(humid);                  // round off humidity
  if (hh != humid_l && hh <= 100) {       // if humidity has changed and is valid then
#ifdef DEBUG
    debugOutLN("save humidity " + String(hh) + "%RH");
#endif
    publish("humid", String(hh));      // store rounded humidity
    humid_l = hh;
  }
  if (pressure != pressure_l && pressure <= 3000) { // if pressure has changed and is valid then
    char p[10];
    dtostrf(pressure / 100.0F, 5, 2, p);  // convert to string with two decimal places
#ifdef DEBUG
    debugOut(F("save pressure "));
    debugOut(p);
    debugOutLN(F(" inHg"));
#endif
    publish("pressure", String(p));    // store inHg pressure
    pressure_l = pressure;
  }
  digitalWrite(LED_BUILTIN, HIGH);        // turn off LED before sleep loop
  for (int x = 0; x < 12; x++) {          // sleep 1 minute between moisture readings to save power
    doMe();                               // while allowing background tasks to run every 5 seconds
    if ((water) && ((readMoisture(false) > AIR + interval) || (((millis() - wTime) / 1000) > MAXWATER))) {
      publish("debug", "time off");
      controlWater(false);
    }
#ifdef DEBUG
    debugOut(ttime() + " (" + ESP.getFreeHeap() + " free) \033[K\r");
#endif
    delay(5000);                          // delay() allows background tasks to run each invocation
  } // for x
} // loop()

String ttime() {
  time_t now = time(nullptr);
  String t = ctime(&now);
  t.trim();                               // formated time contains crlf
  return t;
} // ttime()

#ifdef WWW
void handleWWW(WiFiClient client) {                        // default request serves STATUS page
  if (!client.available()) return;
  char buf[1000];
  char p[10];
  String req = client.readStringUntil('\r');
  String tim = ttime(), upt = upTime();
  client.flush();
#ifdef DEBUG
  //publish("debug", "WWW " + client.remoteIP().toString());
  debugOut(req);
  debugOut(F(" from "));
  debugOut(client.remoteIP());
#endif
  req.toUpperCase();
  if (req.startsWith("GET /FAV")) {                       // send favicon.ico from data directory
    File f = SPIFFS.open("/favicon.ico", "r");
    if (!f) return;
#ifdef DEBUG
    debugOut(F(" send favicon.ico at "));
    debugOutLN(tim);
#endif
    client.println(FPSTR(HTTPOK));
    client.println(F("Content-Type: image/x-icon"));
    client.print(F("Content-Length: "));
    client.println(f.size());
    client.println();
    client.write(f);  // ESP8266 Arduino 2.4.0 library automatically buffers file by just passing handle
    client.flush();
    client.stop();
    f.close();
    return;
  }
  else if (req.startsWith("GET /ROBOT")) {                  // robots.txt just in case
#ifdef DEBUG
    debugOut(F(" send robots.txt at "));
    debugOutLN(tim);
#endif
    client.println(FPSTR(HTTPOK));
    client.println(F("Content-Type: text/plain"));
    client.println();
    client.println(F("User-agent: *"));
    client.println(F("Disallow: /"));
    client.flush();
    client.stop();
    return;
  }
  else if (req.startsWith("GET /CAL")) {                  // CALIBRATE will start moisture sensor calibration
#ifdef DEBUG
    debugOut(F(" start calibration at "));
    debugOutLN(tim);
#endif
    client.println(F("HTTP/1.1 204 No Content"));
    client.println();
    client.flush();
    client.stop();
    startCalibrate = true;
    return;
  }
  else if (req.startsWith("GET /RESET")) {                // RESET will restart ESP
    client.println(F("HTTP/1.1 204 No Content"));
    client.println();
    client.flush();
    client.stop();
    debugOut(F(" restart ESP at "));
    debugOutLN(tim);
    delay(500);                                           // delay to close connection before reset
    ESP.restart();
  }
  dtostrf(pressure / 100.0F, 5, 2, p);
  snprintf_P(buf, sizeof(buf), WWWSTAT, VERSION, tim.c_str(), upt.c_str(), temp_l, humid_l, p, soil);
  client.println(buf);
  if (req.startsWith("GET /CRASH")) {
    client.println("<pre>");
    SaveCrash.print(client);
    client.println("</pre>");
    SaveCrash.clear();
  }
  client.println("</body></html>");
  client.flush();
  client.stop();
#ifdef DEBUG
  debugOutLN(" send status (" + String(strlen(buf)) + " bytes) at " + tim);
#endif
} // handleWWW()
#endif // WWW

void readConfig() {       // mount flash filesystem to read SPIFFS config file
  if (SPIFFS.begin()) {
#ifdef DEBUG
    debugOutLN(F("SPIFFS mounted."));
#endif
    return;
  }
  else {
    debugOutLN(F("Could not mount file system!"));
    delay(500);
    ESP.restart();
  }
  if (!SPIFFS.exists(CONFIG)) {
    debugOutLN(F("Config file not found"));
  } else {
    File f = SPIFFS.open(CONFIG, "r");
#ifdef DEBUG
    debugOut(F("config.txt: "));
    debugOut(f.size());
    debugOutLN(F(" bytes"));
#endif
    while (f.available()) {
      String t = f.readStringUntil('\n');
      t.trim();
      t.replace("\"", "");
      getConfig(t);
    }
    f.close();
  }
} // readConfig()

void getConfig(String i) {
  int e = i.indexOf("=") + 1;
  if ((e == 1) || (e >= i.length())) return;
  if (i.startsWith(F("host"))) HOST = i.substring(e);
  else if (i.startsWith("TZ")) TZ = i.substring(e).toInt();
  else if (i.startsWith("AIR")) AIR = i.substring(e).toInt();
  else if (i.startsWith("WATER")) WATER = i.substring(e).toInt();
  else if (i.startsWith("FAHRENHEIT")) FAHRENHEIT = i.substring(e).toInt();
  else if (i.startsWith("WIFI_SSID")) WIFI_SSID = i.substring(e);
  else if (i.startsWith("WIFI_PASS")) WIFI_PASS = i.substring(e);
#ifdef OTA
  else if (i.startsWith("OTA_PASS")) OTA_PASS = i.substring(e);
#endif
  else if (i.startsWith("MQTT_SERV")) MQTT_SERV = i.substring(e);
  else if (i.startsWith("MQTT_PORT")) MQTT_PORT = i.substring(e).toInt();
  else if (i.startsWith("MQTT_TOPIC")) MQTT_TOPIC = i.substring(e);
  else if (i.startsWith("MQTT_USER")) MQTT_USER = i.substring(e);
  else if (i.startsWith("MQTT_PASS")) MQTT_PASS = i.substring(e);
  else if (i.startsWith("onURL")) onURL = i.substring(e);
  else if (i.startsWith("offURL")) offURL = i.substring(e);
} // getConfig()

/*
void writeConfig() {
  File f = SPIFFS.open(CONFIG, "w");
  int c = 0;
  if (!f) {
    debugOutLN(F("Config write failed."));
    return;
  }
  debugOutLN(F("Writing config to flash..."));
  f.printf("host=%s\n", host.c_str());
  f.printf("TZ=%d\n", TZ);
  f.printf("AIR=%d\n", AIR);
  f.printf("WATER=%d\n", WATER);
  f.printf("FAHRENHEIT=%u\n", FAHRENHEIT);
  f.printf("WIFI_SSID=%s\n", WIFI_SSID.c_str());
  f.printf("WIFI_PASS=%s\n", WIFI_PASS.c_str());
#ifdef OTA
  f.printf("OTA_PASS=%s\n", OTA_PASS.c_str());
#endif
  f.printf("MQTT_SERV=%s\n", MQTT_SERV.c_str());
  f.printf("MQTT_PORT=%d\n", MQTT_PORT);
  f.printf("MQTT_TOPIC=%s\n", MQTT_TOPIC.c_str());
  f.printf("MQTT_USER=%s\n", MQTT_USER.c_str());
  f.printf("MQTT_PASS=%s\n", MQTT_PASS.c_str());
  f.printf("onURL=%s\n", onURL.c_str());
  f.printf("offURL=%s\n", offURL.c_str());
  f.close();
  debugOutLN(F("Success!"));
} // writeConfig()
*/
