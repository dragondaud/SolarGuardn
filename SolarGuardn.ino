/**
  SolarGuardn v0.7.01
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
  Install Arduino core for ESP8266 from: https://github.com/esp8266/Arduino
*/

#include "config.h"

void setup() {
  Serial.begin(115200);             // Initialize Serial at 115200bps, to match bootloader
  //Serial.setDebugOutput(true);
  while (!Serial);                  // wait for Serial to become available
  Serial.println();
  Serial.print("SolarGuardn v");
  Serial.println(VERSION);

  /* mount flash filesystem for configuration file */
  if (SPIFFS.begin()) {   // initialize SPIFFS for config file
#ifdef DEBUG
    Serial.println("SPIFFS mounted.");
#endif
  }
  else {
    Serial.println("Could not mount file system!");
    ESP.restart();
  }
  if (!SPIFFS.exists(CONFIG)) Serial.println("Config file not found");
  else {
    File f = SPIFFS.open(CONFIG, "r");
#ifdef DEBUG
    Serial.print("CONFIG: ");
    Serial.print(f.size());
    Serial.println(" bytes");
#endif
    int c = 0;
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
  Serial.println(" free program space");
#endif

  /* AdafruitIO */
#ifdef DEBUG
  Serial.print("Connecting to Adafruit IO");
#endif
  /* add runtime config of AdafruitIO_WiFi io(IO_USERNAME.c_str(), IO_KEY.c_str(), WIFI_SSID.c_str(), WIFI_PASS.c_str()); */
  WiFi.hostname(host.c_str());
  io.connect();
  while (io.status() < AIO_CONNECTED) {
#ifdef DEBUG
    Serial.print(".");
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
  Serial.print("\nSyncronize clock with sntp...");
#endif
  while (!time(nullptr)) {
    delay(1000);
#ifdef DEBUG
    Serial.print(".");
#endif
  }
#ifdef DEBUG
  Serial.println();
  time_t now = time(nullptr);
  Serial.println(ctime(&now));
#endif

  /* ArduinoOTA config */
  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname(host.c_str());
  // Port defaults to 8266
  ArduinoOTA.setPort(8266);
  // No authentication by default
  if (OTA_PASS) ArduinoOTA.setPassword(OTA_PASS.c_str());
  ArduinoOTA.onStart([]() {
    Serial.println("\r\nOTA Start");
  } );
  ArduinoOTA.onEnd([]() {
    Serial.println("\r\nOTA End");
  } );
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("OTA Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
    else Serial.println("unknown error");
    ESP.restart();
  });
  ArduinoOTA.begin();
  /* end ArduinoOTA */

#ifdef DEBUG
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
#endif

  server.begin(); /* start web server */
#ifdef DEBUG
  Serial.println("WWW server started");
#endif

  Wire.begin(I2C_DAT, I2C_CLK);
  Wire.setClock(100000);
  if (!bme.begin(BMEid)) {
    Serial.println("Could not find a valid BME280 sensor, check wiring!");
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
  Serial.println("Ready");
  delay(1000);
}

void readConfig(String input) {
  int e = input.indexOf("=") + 1;
#ifdef DEBUG
  if ((e == 1) || (e >= input.length())) {
    Serial.print("#");
    Serial.println(input);
    return;
  }
  else Serial.println(input);
#endif
  if (input.startsWith("host")) host = input.substring(e);
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
    Serial.println("Config write failed.");
    return;
  }
  Serial.println("Writing config to flash...");
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
  Serial.println("Success!");
}

void handleMessage(AdafruitIO_Data *data) {				// handle subscribed relay message
  String i = String(data->value());
#ifdef DEBUG
  Serial.print("RELAY = ");
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
  Serial.print(" ");
  Serial.print(stat);
#endif
  if (relay != false) {
#ifdef DEBUG
    Serial.println(", relay open (OFF).");
#endif
    sonoff("off");
    relay = false;
    IOwater->save(stat);
  }
#ifdef DEBUG
  else Serial.println(".");
#endif
}

void calibrate() {
  int sensorValue = 0;
  float voltage;
  Serial.print(caliCount--);
  sensorValue = readMoisture();
  voltage = sensorValue * (3.3 / 1023.0);
  Serial.print(" ON:  ");
  Serial.print(sensorValue);
  Serial.print(", ");
  Serial.println(voltage);
  sensorValue = analogRead(MOIST);
  voltage = sensorValue * (3.3 / 1023.0);
  Serial.print("OFF: ");
  Serial.print(sensorValue);
  Serial.print(", ");
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
  if (Fahrenheit) Serial.print("°F, ");
  else Serial.print("°C, ");
  Serial.print(humid);
  Serial.print("% RH, ");
  Serial.print(pressure / 100.0F, 2);
  Serial.print(" inHg, ");
#endif
  soil = readMoisture();
#ifdef DEBUG
  Serial.print(soil);
#endif
  if (soil > (Water - interval)) relayOff("Soaked");
  else if (soil > (Air + interval)) relayOff("Wet");
  else {
#ifdef DEBUG
    Serial.print(" Dry");
#endif
    if (relay != true) {
#ifdef DEBUG
      Serial.println(", relay closed (ON).");
#endif
      sonoff("on");
      relay = true;
      IOwater->save("Dry");
    }
#ifdef DEBUG
    else Serial.println(".");
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
    if (Fahrenheit) Serial.println("°F");
    else Serial.println("°C");
#endif
    IOtemp->save(t);                      // store rounded temp
    temp_l = t;
  }

  int h = round(humid);                   // round off humidity
  if (h != humid_l && h <= 100) {         // if humidity has changed and is valid then
#ifdef DEBUG
    Serial.printf("save humidity %d", h);
    Serial.println("%RH");
#endif
    IOhumid->save(h);                     // store rounded humidity
    humid_l = h;
  }

  if (pressure != pressure_l && pressure <= 3000) { // if pressure has changed and is valid then
    char p[10];
    dtostrf(pressure / 100.0F, 5, 2, p);            // convert to string with two decimal places
#ifdef DEBUG
    Serial.print("save pressure ");
    Serial.print(p);
    Serial.println(" inHg");
#endif
    IOpressure->save(p);                            // store inHg pressure
    pressure_l = pressure;
  }

  digitalWrite(LED_BUILTIN, HIGH);          // turn off LED before sleep loop

  for (int x = 0; x < 12; x++) {  // sleeping loop between moisture readings to save power
#ifdef DEBUG
    time_t now = time(nullptr);
    String t = ctime(&now);
    t.trim();
    Serial.print(t);
    Serial.print(" (");
    Serial.print(ESP.getFreeHeap(), DEC);
    Serial.print(" free) \r");           // Arduino serial monitor does not support CR, use PuTTY
#endif
    doMe();
    delay(5000);                  // delay() allows background tasks to run each invocation
  }
}

void handleWWW(WiFiClient client) {         // Any request serves up status page
  if (!client.available()) return;
  String req = client.readStringUntil('\r');
  req.toUpperCase();
  if (req.startsWith("GET /FAVICON.ICO")) { // except ignore favicon.ico
    client.stop();
    return;
  }
  else if (req.startsWith("GET /RESET")) ESP.restart();  // and RESET will restart ESP
#ifdef DEBUG
  Serial.println(req);
#endif
  client.flush();
  char buf[1000];
  char p[10];
  dtostrf(pressure / 100.0F, 5, 2, p);
  time_t now = time(nullptr);
  String t = ctime(&now);
  t.trim();
  snprintf(buf, sizeof(buf),
           "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<!DOCTYPE HTML>\r\n\
<html>\
  <head>\
    <meta http-equiv='refresh' content='60'/>\
    <title>SolarGuardn</title>\
    <style>\
      body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }\
    </style>\
  </head>\
  <body>\
    <h1>SolarGuardn %s </h1>\
    <p>Current time: %s </p>\
    <p>Temperature: %u &deg;F</p>\
    <p>Humidity: %u%% RH</p>\
    <p>Abs Pressure: %s inHg</p>\
    <p>Soil Moisture: %u </p>\
  </body>\
</html>",
           VERSION, t.c_str(), temp_l, humid_l, p, soil);
  client.flush();
  client.print(buf);
#ifdef DEBUG
  Serial.print(client.remoteIP());
  Serial.printf(" GET status (%d bytes) at %s, freemem: ", strlen(buf), t.c_str());
  Serial.println(ESP.getFreeHeap(), DEC);
#endif
  client.stop();
}

