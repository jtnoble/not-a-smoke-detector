# Not A Smoke Detector

### What?
This is "Not A Smoke Detector", an ESP32 based prank to play on your friends.
You know how when your smoke detector is about to die, it beeps? Well, this way, you can do it remotely!

### Code Setup?
- First things first, you need an Adafruit IO account at io.adafruit.com
- Once you get your username and API key, copy them into the respective `adaUsername` and `adaKey` in `src/main.cpp`, as well as `ADA_USERNAME` and `ADA_KEY` in `lib/send_mqtt_ping.py`.
- Flash your board with platformio!
- Wherever you want to call, an API, a discord bot, just `import send_mqtt_ping`, and add `send_mqtt_ping.main()`!

### ESP32 Setup?
You can get a good gauge of how to do this with the code, but here's what I did.

##### BOM:
- 1x ESP32
- 1x Button
- 1x Passive Buzzer
- 1x LED
- 1x 100ohm Resistor
- 1x 220ohm Resistor

##### Connections:
- ESP32 PIN GND -> LED-
- ESP32 PIN GND -> Buzzer-
- ESP32 PIN GND -> Button-
- ESP32 PIN 25 -> 100ohm Resistor -> Buzzer+
- ESP32 PIN 26 -> 220ohm Resistor -> LED+
- ESP32 PIN 27 -> Button+

### Now What?
Now that everything is flashed and plugged in, you just need to connect to the ESP32 via WiFi.
- With your phone, connect to the new "BEEPER_WIFI" that comes up.
- Once connected, navigate to `192.168.4.1`.
- Type in the relevant SSID, Password, API Key, and Adafruit Username.
- Save.
- The ESP32 will now reboot. Once you hear the beep, its ready!
- Run `python3 send_mqtt_ping.py` just to be sure it all works.

### The Beeps Mason, What Do They Mean?
- 3 beeps of the same tone: Entered Setup Mode
- 3 beeps in a descending tone: The reset button has been pressed
- Single beep: Ready!
- Single (lower pitched) beep: Someone beeped it!

### The STL File!
Yes, there is an STL for a "Not A Fire Alarm". The LED whole is a tad small, but can be resized. Otherwise, just chuck everything in there. It's free real estate.

#### But... There's no hole for the micro-usb!
Good observation, use a battery bank, or batteries. Just don't do what I did a plug 4AA batteries into the 3v3 or you'll fry your board... Whoops.