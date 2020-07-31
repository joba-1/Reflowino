#include <Arduino.h>

// Neopixel strip
#include <Adafruit_NeoPixel.h>

// Web Updater
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>

#include <WiFiUdp.h>
#include <Syslog.h>

#include "config.h"

#ifndef VERSION
  #define VERSION   NAME " 1.2 " __DATE__ " " __TIME__
#endif

// Syslog
// WiFiUDP udp;
//Syslog syslog(udp, SYSLOG_PROTO_IETF);

Adafruit_NeoPixel pixels = Adafruit_NeoPixel(NUM_PIXELS, PIXEL_PIN, NEO_CONFIG);

ESP8266WebServer web_server(PORT);

ESP8266HTTPUpdateServer esp_updater;

uint16_t duty = 0;

int a[16] = { 0 }; // last analog reads
long a_sum = 0;    // sum of last analog reads


// Default html menu page
void send_menu( const char *msg ) {
  static const char header[] = "<!doctype html>\n"
    "<html lang=\"en\">\n"
      "<head>\n"
        "<meta charset=\"utf-8\">\n"
        "<meta name=\"keywords\" content=\"Reflowino, SSR, remote, meta\">\n"
        "<title>Reflowino Web Remote Control</title>\n"
        "<style>\n"
          ".slidecontainer { width: 80%; }\n"
          ".slider {\n"
            "-webkit-appearance: none;\n"
            "width: 100%;\n"
            "height: 15px;\n"
            "border-radius: 5px;\n"
            "background: #d3d3d3;\n"
            "outline: none;\n"
            "opacity: 0.7;\n"
            "-webkit-transition: .2s;\n"
            "transition: opacity .2s; }\n"
          ".slider:hover { opacity: 1; }\n"
          ".slider::-webkit-slider-thumb {\n"
            "-webkit-appearance: none;\n"
            "appearance: none;\n"
            "width: 25px;\n"
            "height: 25px;\n"
            "border-radius: 50%;\n" 
            "background: #4CAF50;\n"
            "cursor: pointer; }\n"
          ".slider::-moz-range-thumb {\n"
            "width: 25px;\n"
            "height: 25px;\n"
            "border-radius: 50%;\n"
            "background: #4CAF50;\n"
            "cursor: pointer; }\n"
        "</style>\n"
      "</head>\n"
      "<body>\n"
        "<h1>Reflowino Web Remote Control</h1>\n"
        "<p>Control the Reflow Oven</p>\n";
  static const char form[] = "<p>%s</p>\n"
        "<p>Analog: %ld</p>\n"
        "<table><tr><td>\n"
          "<form action=\"/set\">\n"
            "<label for=\"duty\">Duty [%%]:</label>\n"
            "<input id=\"duty\", name=\"duty\" type=\"range\" min=\"0\" max=\"100\" value=\"%u\"/>\n"
            "<button>Set</button>\n"
          "</form></td><td>\n";
  static const char footer[] =
          "<form action=\"/on\">\n"
            "<button>ON</button>\n"
          "</form></td><td>\n"
          "<form action=\"/off\">\n"
            "<button>OFF</button>\n"
          "</form></td><td>\n"
          "<form action=\"/reset\">\n"
            "<button>Reset</button>\n"
          "</form></td><td>\n"
         "<form action=\"/version\">\n"
            "<button>Version</button>\n"
          "</form></td></tr>\n"
        "</table>\n"
      "</body>\n"
    "</html>\n";
  static char page[sizeof(form)+256]; // form + variables

  size_t len = sizeof(header) + sizeof(footer) - 2;
  len += snprintf(page, sizeof(page), form, msg, a_sum, duty);

  web_server.setContentLength(len);
  web_server.send(200, "text/html", header);
  web_server.sendContent(page);
  web_server.sendContent(footer);
}


// Initiate connection to Wifi but dont wait for it to be established
void setup_Wifi() {
  WiFi.mode(WIFI_STA);
  WiFi.hostname(NAME);
  WiFi.begin(SSID, PASS);
  pinMode(ONLINE_LED_PIN, OUTPUT);
  digitalWrite(ONLINE_LED_PIN, HIGH);
}


// Define web pages for update, reset or for configuring parameters
void setup_Webserver() {

  // Call this page to see the ESPs firmware version
  web_server.on("/version", []() {
    send_menu(VERSION);
  });

  // Set duty cycle
  web_server.on("/set", []() {
    if( web_server.arg("duty") != "" ) {
      long i = web_server.arg("duty").toInt();
      if( i >= 0 && i <= 100 ) {
        duty = (unsigned)i;
        char msg[10];
        snprintf(msg, sizeof(msg), "Set: %u", duty);
        send_menu(msg);
      }
      else {
        send_menu("ERROR: Set duty percentage out of range (0-100)");
      }
    }
    else {
      send_menu("ERROR: Set without duty percentage");
    }
    // syslog.log(LOG_INFO, "ON");
  });

  // Call this page to see the ESPs firmware version
  web_server.on("/on", []() {
    duty = 100;
    send_menu("On");
    // syslog.log(LOG_INFO, "ON");
  });

  // Call this page to see the ESPs firmware version
  web_server.on("/off", []() {
    duty = 0;
    send_menu("Off");
    // syslog.log(LOG_INFO, "OFF");
  });

  // Call this page to reset the ESP
  web_server.on("/reset", []() {
    send_menu("Resetting...");
    delay(200);
    ESP.restart();
  });

  // This page configures all settings (/cfg?name=value{&name=value...})
  web_server.on("/", []() {
    send_menu("Welcome");
  });

  // Catch all page, gives a hint on valid URLs
  web_server.onNotFound([]() {
    web_server.send(404, "text/plain", "error: use "
      "/on, /off, /reset, /version or "
      "post image to /update\n");
  });

  web_server.begin();

  MDNS.addService("http", "tcp", PORT);
  // syslog.logf(LOG_NOTICE, "Serving HTTP on port %d", PORT);
}


// Handle online web updater, initialize it after Wifi connection is established
void handleWifi() {
  static bool updater_needs_setup = true;

  if( WiFi.status() == WL_CONNECTED ) {
    if( updater_needs_setup ) {
      // Init once after connection is (re)established
      digitalWrite(ONLINE_LED_PIN, LOW);
      Serial.printf("WLAN '%s' connected with IP ", SSID);
      Serial.println(WiFi.localIP());
      // syslog.logf(LOG_NOTICE, "WLAN '%s' IP %s", SSID, WiFi.localIP().toString().c_str());

      MDNS.begin(NAME);

      esp_updater.setup(&web_server);
      setup_Webserver();

      Serial.println("Update with curl -F 'image=@firmware.bin' " NAME ".local/update");

      updater_needs_setup = false;
    }
    web_server.handleClient();
  }
  else {
    if( ! updater_needs_setup ) {
      // Cleanup once after connection is lost
      digitalWrite(ONLINE_LED_PIN, HIGH);
      updater_needs_setup = true;
      Serial.println("Lost connection");
    }
  }
}


void handleDuty( unsigned duty ) {
  static bool state = LOW;
  static uint32_t since = 0;

  if( duty == 100 && state == LOW ) {
    state = HIGH;
    digitalWrite(SWITCH_PIN, state);
  }
  else if( duty == 0 && state == HIGH ) {
    state = LOW;
    digitalWrite(SWITCH_PIN, state);
  }
  else {
    uint32_t now = millis();
    if( (now - since) > DUTY_CYCLE_MS ) {
      state = HIGH;
      digitalWrite(SWITCH_PIN, state);
      since = now;
    }
    else if( (now - since) > (((uint32_t)duty * DUTY_CYCLE_MS) / 100) ) {
      state = LOW;
      digitalWrite(SWITCH_PIN, state);
    }
  }
}


void handleAnalog() {
  static uint16_t pos = 0;

  if( ++pos == sizeof(a)/sizeof(*a) ) {
    pos = 0;
    delay(5); // needed for wifi !?
  }

  a_sum -= a[pos];
  a[pos] = analogRead(A0);
  a_sum += a[pos];
}


void setup() {
  // start with switch off:
  pinMode(SWITCH_PIN, OUTPUT);
  digitalWrite(SWITCH_PIN, LOW);

  Serial.begin(115200);

  // Initiate network connection (but dont wait for it)
  setup_Wifi();

  // Syslog setup
  // syslog.server(SYSLOG_SERVER, SYSLOG_PORT);
  // syslog.deviceHostname(NAME);
  // syslog.appName("Joba1");
  // syslog.defaultPriority(LOG_KERN);

  // Init the neopixels
  pixels.begin();
  pixels.setBrightness(255);

  // Simple neopixel test
  uint32_t colors[] = { 0x000000, 0xff0000, 0x00ff00, 0x0000ff, 0x000000 };
  for( size_t color=0; color<sizeof(colors)/sizeof(*colors); color++ ) {
    for( unsigned pixel=0; pixel<NUM_PIXELS; pixel++ ) {
      pixels.setPixelColor(color&1 ? NUM_PIXELS-1-pixel : pixel, colors[color]);
      pixels.show();
      delay(500/NUM_PIXELS); // Each color iteration lasts 0.5 seconds
    }
  }

  Serial.println("\nBooted " VERSION);
}


void loop() {
  handleAnalog();
  handleDuty(duty);
  handleWifi();
}