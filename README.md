# SolarGuardn
## beta 0.7.00 09/09/2017
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
- Pump control with SONOFF/Tasmota
- SPIFFS config file

My code is released to public domain, while libraries retain their respective licenses.

Designed to run on an ESP-12E NodeMCU board, the SolarGuardn monitors soil conditions,
ambient temperature, humidity and atmospheric pressure. It reports collected data,
using MQTT, either directly to AdafruitIO or through a local MQTT broker. Webserver
provides direct access to current data, as well as firmware updating.

Press FLASH button on NodeMCU to enter moisture sensor calibration mode, adjust input pot, monitor serial

Press FLASH button twice rapidly to store current running config to SPIFFS

**Board: NodeMCU 1.0, Freq: 80MHz, Flash: 4M (1M SPIFFS), Speed: 115200, Port: serial or OTA IP**

Some of the code is based on examples from the ESP8266 core and other libraries.

Install Arduino core for ESP8266 from: https://github.com/esp8266/Arduino
