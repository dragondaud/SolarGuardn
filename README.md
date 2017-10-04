# SolarGuardn
## v0.7.05 PRE-RELEASE 04-Oct-2017
### by David Denney <dragondaud@gmail.com>

Master repository: https://github.com/dragondaud/SolarGuardn

### Features
- ESP8266/Arduino platform
- Over-the-Air (OTA) updates from Arduino IDE
- BME280 to read Temperature, Humidity, Pressure
- Soil moisture reading
- AdafruitIO/MQTT data logging
- WiFi client
- WiFi web server
- Web server status page
- Telnet server for remote debugging/monitoring
- Pump control with SONOFF/Espurna
- SPIFFS config file

This code is offered "as is" with no warranty, expressed or implied, for any purpose,
and is released to the public domain, while all libraries retain their respective licenses.

See config.h for configurable settings and all includes.

Designed to run on an ESP-12E NodeMCU board with additional hardware,
this sketch will monitor soil conditions, ambient temperature, humidity
and atmospheric pressure, then report changes using MQTT, to AdafruitIO.

A builtin WWW server provides direct access to current data, /reset request will reboot ESP.

Telnet server allows remote monitoring and debugging when serial is not practical.

Press FLASH button on NodeMCU to enter moisture sensor calibration mode, adjust input pot, monitor serial

Press FLASH button twice rapidly to store current running config to SPIFFS

**Board: NodeMCU 1.0, Freq: 80MHz, Flash: 4M (1M SPIFFS), Speed: 115200, Port: serial or OTA IP**

Some code is based on examples from the ESP8266, ArduinoOTA and other libraries.

Sketch requires ESP8266 library v2.4.0, docs at: https://arduino-esp8266.readthedocs.io/en/2.4.0-rc1/

You can use the release candidate by adding  this to File->Preferences->Additional Board Manager URLs:

https://github.com/esp8266/Arduino/releases/download/2.4.0-rc1/package_esp8266com_index.json
