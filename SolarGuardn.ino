/**
  SolarGuardn v0.7.02
  by David Denney

  My code is released to public domain, while libraries retain their respective licenses.
  Master repository: https://github.com/dragondaud/SolarGuardn

  See config.h for configurable settings and all includes.

  Designed to run on an ESP-12E NodeMCU board, the SolarGuardn monitors soil conditions,
  ambient temperature, humidity and atmospheric pressure. Reports collected data,
  using MQTT, to AdafruitIO. Webserver provides direct access to current data.

  Press FLASH button on NodeMCU to enter moisture sensor calibration mode, adjust input pot, monitor serial
  Press FLASH button twice rapidly to store current running config to SPIFFS

  Board: NodeMCU 1.0, Freq: 80MHz, Flash: 4M (1M SPIFFS), Speed: 115200, Port: serial or OTA IP

  some of this code is based on examples from the ESP8266, ArduinoOTA and other libraries
  Install Arduino core for ESP8266 'git version' from: https://github.com/esp8266/Arduino#using-git-version
*/

#include "config.h"

void setup() {
  Serial.begin(115200);             // Initialize Serial at 115200bps, to match bootloader
  // Serial.setDebugOutput(true);   // uncomment for extra library debugging
  while (!Serial);                  // wait for Serial to become available
  Serial.println();
  Serial.print(F("SolarGuardn v"));
  Serial.println(VERSION);

  /* mount flash filesystem for configuration file */
  if (SPIFFS.begin()) {   // initialize SPIFFS for config file
#ifdef DEBUG
    Serial.println(F("SPIFFS mounted."));
#endif
  }
  else {
    Serial.println(F("Could not mount file system!"));
    ESP.restart();
  }
  if (!SPIFFS.exists(CONFIG)) Serial.println(F("Config file not found"));
  else {
    File f = SPIFFS.open(CONFIG, "r");
#ifdef DEBUG
    Serial.print(F("CONFIG: "));
    Serial.print(f.size());
    Serial.println(F(" bytes"));
#endif
    while (f.available()) {
      String t = f.readStringUntil('\n');
      t.trim();
      t.replace("\"", "");
      readConfig(t);
    }
    f.close();
  }

#ifdef DEBUG
  Serial.print(ESP.getFreeSketchSpace());
  Serial.println(F(" free program space"));
#endif

  /* AdafruitIO */
#ifdef DEBUG
  Serial.print(F("Connecting to Adafruit IO"));
#endif
  /* add runtime config of AdafruitIO_WiFi io(IO_USERNAME.c_str(), IO_KEY.c_str(), WIFI_SSID.c_str(), WIFI_PASS.c_str()); */
  WiFi.hostname(host.c_str());
  io.connect();
  while (io.status() < AIO_CONNECTED) {
#ifdef DEBUG
    Serial.print(FPSTR(DOT));
#endif
    delay(500);
  }
#ifdef DEBUG
  Serial.println();
  Serial.println(io.statusText());
#endif

  IOrelay->onMessage(handleMessage);		// subscribe to relay

  /* configTime sntp */
  configTime((TZ * 3600), 0, "pool.ntp.org", "time.nist.gov");
#ifdef DEBUG
  Serial.print(F("Syncronize clock with sntp..."));
#endif
  while (!time(nullptr)) {
    delay(1000);
#ifdef DEBUG
    Serial.print(FPSTR(DOT));
#endif
  }
#ifdef DEBUG
  Serial.println();
  Serial.println(ttime());
#endif

  /* ArduinoOTA config */
  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname(host.c_str());
  // Port defaults to 8266
  ArduinoOTA.setPort(8266);
  // No authentication by default
  if (OTA_PASS) ArduinoOTA.setPassword(OTA_PASS.c_str());
  ArduinoOTA.onStart([]() {
    Serial.println(F("\r\nOTA Start"));
  } );
  ArduinoOTA.onEnd([]() {
    Serial.println(F("\r\nOTA End"));
  } );
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("OTA Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println(F("Auth Failed"));
    else if (error == OTA_BEGIN_ERROR) Serial.println(F("Begin Failed"));
    else if (error == OTA_CONNECT_ERROR) Serial.println(F("Connect Failed"));
    else if (error == OTA_RECEIVE_ERROR) Serial.println(F("Receive Failed"));
    else if (error == OTA_END_ERROR) Serial.println(F("End Failed"));
    else Serial.println(F("unknown error"));
    ESP.restart();
  });
  ArduinoOTA.begin();
  /* end ArduinoOTA */

#ifdef DEBUG
  Serial.print(F("IP address: "));
  Serial.println(WiFi.localIP());
#endif

  server.begin(); /* start web server */
#ifdef DEBUG
  Serial.println(F("WWW server started"));
#endif

  Wire.begin(I2C_DAT, I2C_CLK);
  Wire.setClock(100000);
  if (!bme.begin(BMEid)) {
    Serial.println(F("Could not find a valid BME280 sensor, check wiring!"));
    ESP.restart();
  }

  pinMode(LED_BUILTIN, OUTPUT);   // enable onboard LED output
  pinMode(MPOW, OUTPUT);          // Q2 control for moisture sensor power
  digitalWrite(MPOW, LOW);        // set moisture sensor initially off
  pinMode(RELAY, OUTPUT);         // Q3 control for ext relay control
  digitalWrite(RELAY, LOW);       // set relay initially off/open

  pinMode(BUTTON, INPUT);                         // flash button
  attachInterrupt(BUTTON, handleButton, CHANGE);  // handle button by interupt each press

#ifdef DEBUG
  Serial.printf("Moisture: %d > Soaked > %d > Wet > %d > Dry > %d\r\n", Water, Water - interval, Air + interval, Air);
#endif
  Serial.print(F("Ready, freemem: "));
  Serial.println(ESP.getFreeHeap(), DEC);
  delay(1000);
  Serial.println();
}

void readConfig(String input) {
  int e = input.indexOf("=") + 1;
#ifdef DEBUG
  if ((e == 1) || (e >= input.length())) {
    Serial.print(F("#"));
    Serial.println(input);
    return;
  }
  else Serial.println(input);
#endif
  if (input.startsWith(F("host"))) host = input.substring(e);
  else if (input.startsWith("TZ")) TZ = input.substring(e).toInt();
  else if (input.startsWith("Air")) Air = input.substring(e).toInt();
  else if (input.startsWith("Water")) Water = input.substring(e).toInt();
  else if (input.startsWith("Fahrenheit")) Fahrenheit = input.substring(e).toInt();
  else if (input.startsWith("WIFI_SSID")) WIFI_SSID = input.substring(e);
  else if (input.startsWith("WIFI_PASS")) WIFI_PASS = input.substring(e);
  else if (input.startsWith("OTA_PASS")) OTA_PASS = input.substring(e);
  else if (input.startsWith("IO_USERNAME")) IO_USERNAME = input.substring(e);
  else if (input.startsWith("IO_KEY")) IO_KEY = input.substring(e);
  else if (input.startsWith("onURL")) onURL = input.substring(e);
  else if (input.startsWith("offURL")) offURL = input.substring(e);
}

void writeConfig() {
  File f = SPIFFS.open(CONFIG, "w");
  int c = 0;
  if (!f) {
    Serial.println(F("Config write failed."));
    return;
  }
  Serial.println(F("Writing config to flash..."));
  f.printf("host=%s\n", host.c_str());
  f.printf("TZ=%d\n", TZ);
  f.printf("Air=%d\n", Air);
  f.printf("Water=%d\n", Water);
  f.printf("Fahrenheit=%u\n", Fahrenheit);
  f.printf("WIFI_SSID=%s\n", WIFI_SSID.c_str());
  f.printf("WIFI_PASS=%s\n", WIFI_PASS.c_str());
  f.printf("OTA_PASS=%s\n", OTA_PASS.c_str());
  f.printf("IO_USERNAME=%s\n", IO_USERNAME.c_str());
  f.printf("IO_KEY=%s\n", IO_KEY.c_str());
  f.printf("onURL=%s\n", onURL.c_str());
  f.printf("offURL=%s\n", offURL.c_str());
  f.close();
  Serial.println(F("Success!"));
}

void handleMessage(AdafruitIO_Data *data) {				// handle subscribed relay message
  String i = String(data->value());
#ifdef DEBUG
  Serial.print(F("RELAY = "));
  Serial.print(i);
  Serial.println();
#endif
  if (i == "ON") sonoff("on");
  else sonoff("off");
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

void sonoff(String cmd) {         // control sonoff module running Sonoff-Tasmota
  HTTPClient http;
  if (cmd == "on") http.begin(onURL);
  else http.begin(offURL);
  int httpCode = http.GET();
#ifdef DEBUG
  if (httpCode > 0) {
    Serial.printf("Relay %s [HTTP] GET: %d\r\n", cmd.c_str(), httpCode);
  } else {
    Serial.printf("[HTTP] GET failed: %d\r\n", http.errorToString(httpCode).c_str());
  }
#endif
  http.end();
}

void relayOff(String stat) {
#ifdef DEBUG
  Serial.print(F(" "));
  Serial.print(stat);
#endif
  if (relay != false) {
#ifdef DEBUG
    Serial.println(F(", relay open (OFF)."));
#endif
    sonoff("off");
    relay = false;
    IOwater->save(stat);
  }
#ifdef DEBUG
  else Serial.println(FPSTR(DOT));
#endif
}

void calibrate() {
  int sensorValue = 0;
  float voltage;
  Serial.print(caliCount--);
  sensorValue = readMoisture();
  voltage = sensorValue * (3.3 / 1023.0);
  Serial.print(F(" ON:  "));
  Serial.print(sensorValue);
  Serial.print(FPSTR(COMMA));
  Serial.println(voltage);
  sensorValue = analogRead(MOIST);
  voltage = sensorValue * (3.3 / 1023.0);
  Serial.print(F("OFF: "));
  Serial.print(sensorValue);
  Serial.print(FPSTR(COMMA));
  Serial.println(voltage);
}

int readMoisture() {           // analog input smoothing
  int s = 0;
  digitalWrite(MPOW, HIGH);     // turn on moisture sensor
  for (int i = 0; i < numReads; i++) {
    delay(10);
    s += analogRead(MOIST);     // read analog value from moisture sensor
  }
  digitalWrite(MPOW, LOW);      // turn off moisture sensor
  float a = s / numReads;
  return round(a);
}

void readBME() {
  temp = bme.readTemperature();                   // read Temp in C
  if (Fahrenheit) temp = temp * 1.8F + 32.0F;     // convert to Fahrenheit
  humid = bme.readHumidity();                     // read Humidity
  pressure = round(bme.readPressure() * 0.02953); // read barometric pressure and convert to inHg
}                                                 /*-- add configure option for hPa or inHg --*/

void doMe() {                             // called every 5 seconds to handle background tasks
  ArduinoOTA.handle();                    // handle OTA update requests every 5 seconds
  io.run();                               // and handle AdafruitIO messages
  WiFiClient client = server.available(); // and serve web requests
  if (client) handleWWW(client);
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
  if (caliCount > 0) calibrate();
}

void loop() {                       /** MAIN LOOP **/
  digitalWrite(LED_BUILTIN, LOW);   // blink LED_BUILTIN (NodeMCU LOW = ON)
  doMe();                           // non-sensor loop stuff
  readBME();
#ifdef DEBUG
  Serial.print(temp);
  if (Fahrenheit) Serial.print(F("°F, "));
  else Serial.print(F("°C, "));
  Serial.print(humid);
  Serial.print(F("% RH, "));
  Serial.print(pressure / 100.0F, 2);
  Serial.print(F(" inHg, "));
#endif
  soil = readMoisture();
#ifdef DEBUG
  Serial.print(soil);
#endif
  if (soil > (Water - interval)) relayOff("Soaked");
  else if (soil > (Air + interval)) relayOff("Wet");
  else {
#ifdef DEBUG
    Serial.print(F(" Dry"));
#endif
    if (relay != true) {
#ifdef DEBUG
      Serial.println(F(", relay closed (ON)."));
#endif
      sonoff("on");
      relay = true;
      IOwater->save("Dry");
    }
#ifdef DEBUG
    else Serial.println(FPSTR(DOT));
#endif
  }

  if (soil != soil_l) {                   // if moisture level has changed then
#ifdef DEBUG
    Serial.printf("save moisture %d", soil);
    Serial.println();
#endif
    IOmoist->save(soil);                  // store soil resistance
    soil_l = soil;
  }

  int t = round(temp);                    // round off temperature
  if (t != temp_l && t < 120) {           // if temp has changed and is valid then
#ifdef DEBUG
    Serial.printf("save temperature %d", t);
    if (Fahrenheit) Serial.println(F("°F"));
    else Serial.println(F("°C"));
#endif
    IOtemp->save(t);                      // store rounded temp
    temp_l = t;
  }

  int h = round(humid);                   // round off humidity
  if (h != humid_l && h <= 100) {         // if humidity has changed and is valid then
#ifdef DEBUG
    Serial.printf("save humidity %d", h);
    Serial.println(F("%RH"));
#endif
    IOhumid->save(h);                     // store rounded humidity
    humid_l = h;
  }

  if (pressure != pressure_l && pressure <= 3000) { // if pressure has changed and is valid then
    char p[10];
    dtostrf(pressure / 100.0F, 5, 2, p);            // convert to string with two decimal places
#ifdef DEBUG
    Serial.print(F("save pressure "));
    Serial.print(p);
    Serial.println(F(" inHg"));
#endif
    IOpressure->save(p);                            // store inHg pressure
    pressure_l = pressure;
  }

  digitalWrite(LED_BUILTIN, HIGH);        // turn off LED before sleep loop

  for (int x = 0; x < 12; x++) {          // sleeping loop between moisture readings to save power
#ifdef DEBUG
    Serial.print(ttime());
    Serial.print(F(" ("));
    Serial.print(ESP.getFreeHeap(), DEC);
    Serial.print(F(" free) \r"));            // Arduino serial monitor does not support CR, use PuTTY
#endif
    doMe();
    delay(5000);                          // delay() allows background tasks to run each invocation
  }
}

String ttime() {
    time_t now = time(nullptr);
    String t = ctime(&now);
    t.trim();
    return t;                             // formated time contains crlf
}

void handleWWW(WiFiClient client) {                        // default request serves STATUS page
  if (!client.available()) return;
  char buf[1000];
  char p[10];
  String req = client.readStringUntil('\r');
  String t = ttime();
#ifdef DEBUG
  Serial.println();
  Serial.print(req);
  Serial.print(F(" from "));
  Serial.print(client.remoteIP());
#endif
  client.flush();
  req.toUpperCase();
  if (req.startsWith("GET /FAV")) {                       // send favicon.ico from data directory
    File f = SPIFFS.open("/favicon.ico", "r");
    if (!f) return;
#ifdef DEBUG
      Serial.print(F(" send favicon.ico at "));
      Serial.println(t);
#endif
    client.println(F("HTTP/1.1 200 OK"));
    client.println(F("Content-Type: image/x-icon"));
    client.print(F("Content-Length: "));
    client.println(f.size());
    client.println();
    client.write(f);  // new pre-release ESP8266 library function automatically buffers file by just passing handle
    client.stop();
    f.close();
    return;
  }
  else if (req.startsWith("GET /RESET")) {                // RESET will restart ESP
    Serial.print(F(" restart ESP at "));
    Serial.println(t);
    ESP.restart();
  }
  dtostrf(pressure / 100.0F, 5, 2, p);
  snprintf_P(buf, sizeof(buf), WWWSTAT, VERSION, t.c_str(), temp_l, humid_l, p, soil);
  client.print(buf);
#ifdef DEBUG
  Serial.printf(" send status (%d bytes) at %s \r\n", strlen(buf), t.c_str());
#endif
  client.stop();
}

