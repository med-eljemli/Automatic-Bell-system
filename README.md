# Sonnette WiFi Only Low

An Arduino-based WiFi-enabled bell/schedule system suitable for all schools and environments that need automated bell scheduling.

## Features

- WiFi connectivity with multiple network fallback
- NTP time synchronization with configurable UTC offset
- Customizable bell schedules (normal, Ramadan, custom)
- Standby mode (screen off during sleeping hours)
- LCD display with date/time, temperature, greetings, and scrolling messages
- Web configuration portal accessible via ESP8266 soft AP
- EEPROM storage for settings and schedules
- Relay control for bell activation
- Temperature updates from OpenWeatherMap

## Hardware Requirements

- ESP8266 (e.g., NodeMCU, Wemos D1 Mini)
- I2C LCD 20x4 display
- DS1302 RTC module (via virtuabotixRTC library)
- Relay module (active low)
- Optional: Temperature sensor (uses OpenWeatherMap API)

## Libraries Required

- Wire.h (built-in)
- LiquidCrystal_I2C
- virtuabotixRTC
- ESP8266WiFi
- ESP8266WebServer
- NTPClient
- EEPROM (built-in)

## Installation

Before installing the libraries, you need to add the ESP8266 board support to Arduino IDE:
1. Open Arduino IDE and go to File → Preferences
2. In the "Additional Boards Manager URLs" field, add: `http://arduino.esp8266.com/stable/package_esp8266com_index.json`
3. Go to Tools → Board → Boards Manager, search for "ESP8266" and install the package
4. Install the required libraries via Arduino Library Manager.
5. Copy `src/Sonnette_wifionly_low.ino` to your Arduino sketchbook.
6. Select the appropriate ESP8266 board and port.
7. Upload the sketch.
8. Connect to the access point `CPGE_BELL` (password: `XXXXXXX` - these credentials can be changed in the source code or through the configuration portal after initial setup). 
9. Open `http://192.168.4.1` in a browser to configure.

## Configuration

The web interface allows setting:
- Date and time (manual or NTP sync)
- Bell duration and enabled/disabled
- Program mode: Normal, Ramadan, Custom
- Standby (sleep/wake) times
- Custom bell schedules (time and days)
- Custom scrolling message (with animation option)
- UTC offset for timezone adjustment

## File Structure

```
Sonnette_wifionly_low_organized/
├── README.md
├── LICENSE
├── .gitignore
├── src/
│   └── Sonnette_wifionly_low.ino
└── docs/
    └── utc_offset_interface.html
```

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Acknowledgements

- Based on original work for CPGE Salmane Alfarissi.
- Developed by MOHAMED EL JEMLI, ENSIAS student.
- Uses various open-source libraries.
