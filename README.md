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

