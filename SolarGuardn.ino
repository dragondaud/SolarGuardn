/*
  SolarGuardn v0.7.05 PRE-RELEASE 04-Oct-2017
  by David Denney

  This code is offered "as is" with no warranty, expressed or implied, for any purpose,
  and is released to the public domain, while all libraries retain their respective licenses.

  Master repository: https://github.com/dragondaud/SolarGuardn

  See config.h for configurable settings and all includes.

  Designed to run on an ESP-12E NodeMCU board with additional hardware,
  this sketch will monitor soil conditions, ambient temperature, humidity
  and atmospheric pressure, then report changes using MQTT, to AdafruitIO.

  A builtin WWW server provides direct access to current data, /reset request will reboot ESP.
  Telnet server allows remote monitoring and debugging when serial is not practical.

  Press FLASH button on NodeMCU to enter moisture sensor calibration mode, adjust input pot, monitor serial
  Press FLASH button twice rapidly to store current running config to SPIFFS

  Board: NodeMCU 1.0, Freq: 80MHz, Flash: 4M (1M SPIFFS), Speed: 115200, Port: serial or OTA IP

  Some code is based on examples from the ESP8266, ArduinoOTA and other libraries.

  Sketch requires ESP8266 library v2.4.0, docs at: https://arduino-esp8266.readthedocs.io/en/2.4.0-rc1/
  You can use the release candidate by adding  this to File->Preferences->Additional Board Manager URLs:
    https://github.com/esp8266/Arduino/releases/download/2.4.0-rc1/package_esp8266com_index.json
*/

#include "config.h"

void setup() {
  Serial.begin(115200);               // Initialize Serial at 115200bps, to match bootloader
  // Serial.setDebugOutput(true);     // uncomment for extra library debugging
  while (!Serial);                    // wait for Serial to become available
  debugOutLN(FPSTR(NIL));
  debugOut(F("SolarGuardn v"));
  debugOutLN(VERSION);

  /* mount flash filesystem to read config file */
  if (SPIFFS.begin()) {   // initialize SPIFFS for config file
#ifdef DEBUG
    debugOutLN(F("SPIFFS mounted."));
#endif
  }
  else {
    debugOutLN(F("Could not mount file system!"));
    ESP.restart();
  }
  if (!SPIFFS.exists(CONFIG)) debugOutLN(F("Config file not found"));
  else {
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
      readConfig(t);
    }
    f.close();
  }

  /* AdafruitIO */
#ifdef DEBUG
  debugOut(F("Connecting to Adafruit IO"));
#endif
  /* add runtime config of AdafruitIO_WiFi io(IO_USERNAME.c_str(), IO_KEY.c_str(), WIFI_SSID.c_str(), WIFI_PASS.c_str()); */
  WiFi.hostname(host.c_str());
  io.connect();
  while (io.status() < AIO_CONNECTED) {
#ifdef DEBUG
    debugOut(FPSTR(DOT));
#endif
    delay(500);
  }
#ifdef DEBUG
  debugOutLN(FPSTR(NIL));
  debugOutLN(io.statusText());
#endif

#ifdef TELNET
#ifdef DEBUG
  debugOutLN(F("telnet server started"));
#endif
  telnetServer.begin();
  //telnetServer.setNoDelay(true); // ESP bug
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
  debugOutLN(FPSTR(NIL));
#endif

#ifdef OTA
  /* ArduinoOTA config */
  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname(host.c_str());
  // Port defaults to 8266
  ArduinoOTA.setPort(OTA_PORT);
  // No authentication by default
  if (OTA_PASS) ArduinoOTA.setPassword(OTA_PASS.c_str());
  ArduinoOTA.onStart([]() {
    debugOutLN(F("\r\nOTA Start"));
  } );
  ArduinoOTA.onEnd([]() {
    debugOutLN(F("\r\nOTA End"));
  } );
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    debugOut("OTA Progress: " + String((progress / (total / 100))) + " \r");
  });
  ArduinoOTA.onError([](ota_error_t error) {
    debugOut("Error[" + String(error) + "]: ");
    if (error == OTA_AUTH_ERROR) debugOutLN(F("Auth Failed"));
    else if (error == OTA_BEGIN_ERROR) debugOutLN(F("Begin Failed"));
    else if (error == OTA_CONNECT_ERROR) debugOutLN(F("Connect Failed"));
    else if (error == OTA_RECEIVE_ERROR) debugOutLN(F("Receive Failed"));
    else if (error == OTA_END_ERROR) debugOutLN(F("End Failed"));
    else debugOutLN(F("unknown error"));
    ESP.restart();
  });
  ArduinoOTA.begin();
#ifdef DEBUG
  debugOutLN(F("OTA handler started"));
#endif
  /* end ArduinoOTA */
#endif

#ifdef WWW
  server.begin(); /* start web server */
#ifdef DEBUG
  debugOutLN(F("WWW server started"));
#endif
#endif

  Wire.begin(I2C_DAT, I2C_CLK);
  Wire.setClock(100000);
  if (!bme.begin(BMEid)) {
    debugOutLN(F("Could not find a valid BME280 sensor, check wiring!"));
    ESP.restart();
  }

  pinMode(LED_BUILTIN, OUTPUT);                   // enable onboard LED output
  pinMode(MPOW, OUTPUT);                          // moisture sensor power
  digitalWrite(MPOW, LOW);                        //  - initially off
  pinMode(BUTTON, INPUT);                         // flash button for calibration
  attachInterrupt(BUTTON, handleButton, CHANGE);  // handle button by interrupt each press

#ifdef DEBUG
  espStats();
  IOdebug->save("START");
  debugOut(F("Ready, at "));
  debugOutLN(ttime());
  debugOutLN(FPSTR(NIL));
#endif
  delay(5000);
}

template <typename T> void debugOut(const T x) {
  Serial.print(x);
#ifdef TELNET
  if (telnetClient && telnetClient.connected()) {
    telnetClient.print(x);
    delay(1);
  }
#endif
}

template <typename T> void debugOutLN(const T x) {
  debugOut(x);
  debugOut(FPSTR(CRLF));
}

#ifdef TELNET
void handleTelnet(void) {
  if (telnetServer.hasClient()) {
    if (!telnetClient || !telnetClient.connected()) {
      if (telnetClient) {
        telnetClient.stop();
#ifdef DEBUG
        debugOutLN(F("\r\ntelnet client stopped"));
#endif
      }
      telnetClient = telnetServer.available();
      telnetClient.flush();
#ifdef DEBUG
      debugOut(F("\r\ntelnet connected from "));
      debugOutLN(telnetClient.remoteIP());
      IOdebug->save("telnet " + telnetClient.remoteIP().toString());
      espStats();
#endif
    } else {
      telnetServer.available().stop();
#ifdef DEBUG
      debugOutLN(F("\r\ntelnet disconnected"));
#endif
    }
  }
}
#endif

String upTime() {
  long t = millis() / 1000;
  long s = t % 60;
  long m = (t / 60) % 60;
  long h = (t / (60 * 60)) % 24;
  char ut[10];
  snprintf(ut, sizeof(ut), "%d:%02d:%02d", h, m, s);
  return String(ut);
}

void espStats() {
  debugOut(F("WiFi Hostname: "));
  debugOutLN(WiFi.hostname());
  debugOut(F("WiFi IP addr: "));
  debugOutLN(WiFi.localIP());
  debugOut(F("WiFi MAC addr: "));
  debugOutLN(WiFi.macAddress());
  debugOut(F("ESP sketch size: "));
  debugOutLN(ESP.getSketchSize());
  debugOut(F("ESP free flash: "));
  debugOutLN(ESP.getFreeSketchSpace());
  debugOut(F("ESP free RAM: "));
  debugOutLN(ESP.getFreeHeap());
  debugOut(F("ESP uptime: "));
  debugOutLN(upTime());
  debugOutLN("Moisture: " + String(Water) + " > Soaked > " + String(Water - interval) \
             + " > Wet > " + String(Air + interval) + " > Dry > " + String(Air));
}

void readConfig(String input) {
  int e = input.indexOf("=") + 1;
  if ((e == 1) || (e >= input.length())) return;
  if (input.startsWith(F("host"))) host = input.substring(e);
  else if (input.startsWith("TZ")) TZ = input.substring(e).toInt();
  else if (input.startsWith("Air")) Air = input.substring(e).toInt();
  else if (input.startsWith("Water")) Water = input.substring(e).toInt();
  else if (input.startsWith("Fahrenheit")) Fahrenheit = input.substring(e).toInt();
  else if (input.startsWith("WIFI_SSID")) WIFI_SSID = input.substring(e);
  else if (input.startsWith("WIFI_PASS")) WIFI_PASS = input.substring(e);
#ifdef OTA
  else if (input.startsWith("OTA_PASS")) OTA_PASS = input.substring(e);
#endif
  else if (input.startsWith("IO_USERNAME")) IO_USERNAME = input.substring(e);
  else if (input.startsWith("IO_KEY")) IO_KEY = input.substring(e);
  else if (input.startsWith("onURL")) onURL = input.substring(e);
  else if (input.startsWith("offURL")) offURL = input.substring(e);
}

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
  f.printf("Air=%d\n", Air);
  f.printf("Water=%d\n", Water);
  f.printf("Fahrenheit=%u\n", Fahrenheit);
  f.printf("WIFI_SSID=%s\n", WIFI_SSID.c_str());
  f.printf("WIFI_PASS=%s\n", WIFI_PASS.c_str());
#ifdef OTA
  f.printf("OTA_PASS=%s\n", OTA_PASS.c_str());
#endif
  f.printf("IO_USERNAME=%s\n", IO_USERNAME.c_str());
  f.printf("IO_KEY=%s\n", IO_KEY.c_str());
  f.printf("onURL=%s\n", onURL.c_str());
  f.printf("offURL=%s\n", offURL.c_str());
  f.close();
  debugOutLN(F("Success!"));
}

void handleButton() {
  if (millis() - deBounce < 50) return;   // debounce button
  buttonState = digitalRead(BUTTON);      // read state of flash button
  if (buttonState == LOW) return;         // button down, do nothing
  else {
    caliCount += 12;                      // each button release, calibrate +12 loops (1 min)
    deBounce = millis();                  // and debounce
  }
}

void sonoff(String cmd) {         // control Sonoff module running ESPurna
  HTTPClient http;
  if (cmd == "ON") {
    http.begin(onURL);
    water = true;
    IOwater->save("ON");
  }
  else {
    http.begin(offURL);
    water = false;
    IOwater->save("OFF");
  }
  int httpCode = http.GET();
#ifdef DEBUG
  if (httpCode > 0) {
    debugOutLN("Sonoff " + cmd + " [HTTP] GET: " + String(httpCode));
  } else {
    debugOutLN("[HTTP] GET failed: " + http.errorToString(httpCode));
  }
#endif
  http.end();
}

void waterControl(String stat) {
#ifdef DEBUG
  debugOut(F(" "));
  debugOut(stat);
#endif
  if (stat == "Dry") {
    if (!water) {
#ifdef DEBUG
      debugOutLN(F(", water turned ON."));
#endif
      sonoff("ON");
    } else {
#ifdef DEBUG
      debugOutLN(F(", water still ON."));
#endif
    }
  } else {
    if (water) {
#ifdef DEBUG
      debugOutLN(F(", water turned OFF."));
#endif
      sonoff("OFF");
    } else {
#ifdef DEBUG
      debugOutLN(FPSTR(DOT));
#endif
    }
  }
}

void calibrate() {
  int sensorValue = 0;
  debugOut(caliCount--);
  debugOut(F(") "));
  sensorValue = readMoisture(true);
}

int readMoisture(bool VERBOSE) {      // analog input smoothing
  int r = 0, s = 0;
  digitalWrite(MPOW, HIGH);           // turn on moisture sensor
  delay(150);                         // overcome soil capacitance
  analogRead(MOIST);                  // throw away first value
  for (int i = 0; i < numReads; i++) {
    delay(10);
    r = analogRead(MOIST);            // read analog value from moisture sensor
    if (VERBOSE) {                    // during calibration, output all values
      debugOut(r);
      debugOut(FPSTR(COMMA));
    }
    s += r;
  }
  digitalWrite(MPOW, LOW);            // turn off moisture sensor
  if (VERBOSE) debugOutLN(F("\b\b "));
  return round((float)s / (float)numReads);
}

void readBME() {
  temp = bme.readTemperature();                   // read Temp in C
  if (Fahrenheit) temp = temp * 1.8F + 32.0F;     // convert to Fahrenheit
  humid = bme.readHumidity();                     // read Humidity
  pressure = round(bme.readPressure() * 0.02953); // read barometric pressure and convert to inHg
}                                                 /*-- add configure option for hPa or inHg --*/

void doMe() {                             // called every 5 seconds to handle background tasks
#ifdef OTA
  ArduinoOTA.handle();                    // handle OTA update requests every 5 seconds
#endif
  io.run();                               // and handle AdafruitIO messages
#ifdef WWW
  WiFiClient client = server.available(); // and serve web requests
  if (client) handleWWW(client);
#endif
#ifdef TELNET
  handleTelnet();
#endif
  yield();
  if (caliCount >= 24 ) {                  // double press flash to save config
    for (int i = 0; i < 10; i++) {
      digitalWrite(LED_BUILTIN, LOW);    // flash LED so we know it worked
      delay(20);
      digitalWrite(LED_BUILTIN, HIGH);
      delay(20);
    }
    writeConfig();
    caliCount = 0;
  }
  else if (caliCount > 0) calibrate();
}

void loop() {                       /** MAIN LOOP **/
  digitalWrite(LED_BUILTIN, LOW);   // blink LED_BUILTIN (NodeMCU LOW = ON)
  doMe();                           // non-sensor loop stuff
  readBME();
  soil = readMoisture(false);
#ifdef DEBUG
  debugOut(String(temp, 2));
  if (Fahrenheit) debugOut(F("°F, "));
  else debugOut(F("°C, "));
  debugOut(String(humid, 2));
  debugOut(F("% RH, "));
  debugOut(String(pressure / 100.0F, 2));
  debugOut(F(" inHg, "));
  debugOut(soil);
#endif
  if (soil > (Water - interval)) waterControl("Soaked");
  else if (soil > (Air + interval)) waterControl("Wet");
  else waterControl("Dry");

  if (soil != soil_l) {                   // if moisture level has changed then
#ifdef DEBUG
    debugOutLN("save moisture " + String(soil));
#endif
    IOmoist->save(soil);                  // store soil resistance
    soil_l = soil;
  }

  int tt = round(temp);                    // round off temperature
  if (tt != temp_l && tt < 120) {           // if temp has changed and is valid then
#ifdef DEBUG
    debugOut("save temperature " + String(tt));
    if (Fahrenheit) debugOutLN(F("°F"));
    else debugOutLN(F("°C"));
#endif
    IOtemp->save(tt);                      // store rounded temp
    temp_l = tt;
  }

  int hh = round(humid);                   // round off humidity
  if (hh != humid_l && hh <= 100) {         // if humidity has changed and is valid then
#ifdef DEBUG
    debugOutLN("save humidity " + String(hh) + "%RH");
#endif
    IOhumid->save(hh);                     // store rounded humidity
    humid_l = hh;
  }

  if (pressure != pressure_l && pressure <= 3000) { // if pressure has changed and is valid then
    char p[10];
    dtostrf(pressure / 100.0F, 5, 2, p);            // convert to string with two decimal places
#ifdef DEBUG
    debugOut(F("save pressure "));
    debugOut(p);
    debugOutLN(F(" inHg"));
#endif
    IOpressure->save(p);                            // store inHg pressure
    pressure_l = pressure;
  }

  digitalWrite(LED_BUILTIN, HIGH);        // turn off LED before sleep loop

  for (int x = 0; x < 12; x++) {          // sleeping loop between moisture readings to save power
    doMe();
#ifdef DEBUG
    debugOut(ttime());
    debugOut(F(" ("));
    debugOut(ESP.getFreeHeap());
    debugOut(F(" free) \r"));            // Arduino serial monitor does not support CR, use PuTTY
#endif
    delay(5000);                          // delay() allows background tasks to run each invocation
  }
}

String ttime() {
  time_t now = time(nullptr);
  String t = ctime(&now);
  t.trim();                               // formated time contains crlf
  return t;
}

#ifdef WWW
void handleWWW(WiFiClient client) {                        // default request serves STATUS page
  if (!client.available()) return;
  char buf[1000];
  char p[10];
  String req = client.readStringUntil('\r');
  String tim = ttime(), upt = upTime();
#ifdef DEBUG
  debugOutLN(FPSTR(NIL));
  debugOut(req);
  debugOut(F(" from "));
  debugOut(client.remoteIP());
  IOdebug->save("WWW " + client.remoteIP().toString());
#endif
  yield();
  client.flush();
  req.toUpperCase();
  if (req.startsWith("GET /FAV")) {                       // send favicon.ico from data directory
    File f = SPIFFS.open("/favicon.ico", "r");
    if (!f) return;
#ifdef DEBUG
    debugOut(F(" send favicon.ico at "));
    debugOutLN(tim);
#endif
    client.println(F("HTTP/1.1 200 OK"));
    client.println(F("Content-Type: image/x-icon"));
    client.print(F("Content-Length: "));
    client.println(f.size());
    client.println();
    client.write(f);  // ESP8266 Arduino 2.4.0 library automatically buffers file by just passing handle
    client.stop();
    f.close();
    return;
  }
  else if (req.startsWith("GET /CAL")) {                  // CALIBRATE will start moisture sensor calibration
    client.println(F("HTTP/1.1 204 No Content"));
    client.println();
    client.flush();
    client.stop();
    debugOut(F(" at "));
    debugOutLN(tim);
    caliCount += 12;
    return;
  }
  else if (req.startsWith("GET /RESET")) {                // RESET will restart ESP
    client.println(F("HTTP/1.1 204 No Content"));
    client.println();
    client.flush();
    client.stop();
    debugOut(F(" restart ESP at "));
    debugOutLN(tim);
    delay(500);                                           // requires some delay to close connection before reset
    ESP.restart();
  }
  dtostrf(pressure / 100.0F, 5, 2, p);
  snprintf_P(buf, sizeof(buf), WWWSTAT, VERSION, tim.c_str(), upt.c_str(), temp_l, humid_l, p, soil);
  client.print(buf);
#ifdef DEBUG
  debugOutLN(" send status (" + String(strlen(buf)) + " bytes) at " + tim);
#endif
  yield();
  client.stop();
}
#endif
