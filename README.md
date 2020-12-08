# A peripheral test

- Attaching an SSD1306 128x32 monochrome oled display
- Setting up a wifi AP
- LATER: by auto-generated SSID and random password
- Displaying the wifi connection info as QR-Code and as text
- When a client connects, display an URL both as QR-Code and as text

- Start an HTTPS server that accepts that URL and
- NOW: replies with a static placeholder page

- LATER: provide a captive DNS that resolves every A request to our IP
- LATER: fake responses for Android 'internet detection'

`openssl req -newkey rsa:2048 -nodes -keyout static_data/private.key -x509 -days 3650 -out static_data/server.crt -subj "/CN=ptest"`


## Touch sensor

[**THE** guide](https://github.com/espressif/esp-iot-solution/blob/master/documents/touch_pad_solution/touch_sensor_design_en.md).

- sensor dia: 8..15 mm = 315..590 mil -> 350, 400, 450, **500**, 550, 600 mil
- sensor-gnd gap: 1..2 mm = 40..80 mil -> **50** mil
- trace-gnd gap: 0.5..1 mm = 20..40 mil -> 25, **50** mil
- trace width: <= 7 mil
- via dia: 10 mil
- sensor-sensor dist: >= 5 mm = **200** mil
- gnd x-hatch: 5 mil line, 50 mil space


### Operation

The touch sensor operates **only** two things:
1. (re)start AP mode: a safety sequence
2. in AP mode: turn the screen on for 15 seconds: short tap
3. (for everything else please use the web UI)


* screen-off watchdog: 15 seconds, if expired: screen off
* tap-sequence watchdog: 1 or 5 seconds

The safety sequence:
1. tap starts: screen on, show "keep pressed for AP mode", tap-seq wdt (and a progress bar) starts from 5 sec
2. if the sensor is released while the tap-seq wdt is running: AP mode switch cancelled, tap-seq wdt stopped, prev screen (when already in AP mode) restored
3. if the tap-seq wdt expires: show "release NOW", tap-seq wdt starts from 1 sec
4. if the tap-seq wdt expires: AP mode switch cancelled, tap-seq wdt stopped, etc.
5. if the sensor is released while the tap-seq wdt is running: switch to AP mode


NOTE: As the tap-seq wdt always runs in parallel with a progress bar, and the bar must be displayed
programmatically, the tap-seq wdt is no longer needed, the progress bar handler can just check if it
expired or not.


The event actions:

* on start:
	* APSwitchPhase = Off
	* progress timer is off
	* screen is off

* if screen-off wdt expires:
	* turn the screen off

* if touch sensor goes active:
	* if screen is off then turn it on and restart screen-off wdt
	* APSwitchPhase = KeepPressed
	* show "keep pressed for AP mode"
	* start progress timer from 5 sec

* if the progress timer has expired:
	* if APSwitchPhase == KeepPressed:
		* APSwitchPhase = ReleaseNow
		* show "release NOW"
		* restart progress timer from 1 sec
	* if APSwitchPhase == ReleaseNow:
		* APSwitchPhase = Off
		* re-display previous (FIXME) screen

* if the touch sensor goes inactive:
	* mark the progress status and stop the progress timer
	* if the progress bar wasn't running:
		* ignore the event and return
	* if APSwitchPhase == KeepPressed:
		* APSwitchPhase = Off
		* re-display previous (FIXME) screen
	* if APSwitchPhase == ReleaseNow:
		* APSwitchPhase = Off
		* SWITCH TO AP MODE



