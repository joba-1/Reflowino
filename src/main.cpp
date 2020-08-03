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

#include <math.h> // for log()

#ifndef VERSION
  #define VERSION   NAME " 1.3 " __DATE__ " " __TIME__
#endif

// Syslog
// WiFiUDP udp;
//Syslog syslog(udp, SYSLOG_PROTO_IETF);

Adafruit_NeoPixel pixels = Adafruit_NeoPixel(NUM_PIXELS, PIXEL_PIN, NEO_CONFIG);

ESP8266WebServer web_server(PORT);

ESP8266HTTPUpdateServer esp_updater;

uint16_t duty = 0; // ssr pwm percent

// Analog read samples
const uint16_t a_samples = 4000;
const uint16_t a_max = 1023;
uint32_t a_sum = 0;        // sum of last analog reads

// NTC characteristics (datasheet)
static const uint32_t B = 3950;
static const uint32_t R_n = 100000; // Ohm
static const uint32_t T_n = 25;     // Celsius

static const uint32_t R_v = 8830; // Ohm, voltage divider resistor

uint32_t r_ntc = 0;        // Ohm, resistance updated with each analog read
double temperature_c = 0;  // Celsius, calculated from NTC and R_v

int16_t t[8640*2];         // 2 days centicelsius temperature history in 10s intervals
uint16_t t_pos;

// Default html menu page
void send_menu( const char *msg ) {
  static const char header[] = "<!doctype html>\n"
    "<html lang=\"en\">\n"
      "<head>\n"
        "<meta charset=\"utf-8\">\n"
        "<meta name=\"keywords\" content=\"Reflowino, SSR, remote, meta\">\n"
        "<meta http-equiv=\"refresh\" content=\"10\">\n"
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
        "<p>Temperature: %5.1f &#8451;,  NTC resistance: %d &#8486;,  Analog: %d</p>\n"
        "<table><tr>\n"
          "<form action=\"/set\">\n"
            "<td><label for=\"duty\">Duty %%:</label></td>\n"
            "<td colspan=\"2\"><input id=\"duty\", name=\"duty\" type=\"range\" min=\"0\" max=\"100\" value=\"%u\"/></td>\n"
            "<td><button>Set</button></td>\n"
          "</form></tr><tr><td>\n";
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
  len += snprintf(page, sizeof(page), form, msg, temperature_c, r_ntc, a_sum, duty);

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

  web_server.on("/temperature", []() {
    char msg[10];
    snprintf(msg, sizeof(msg), "%5.1f", temperature_c);
    web_server.send(200, "text/plain", msg);
  });

  // TODO crashes...
  web_server.on("/history.bin", []() {
    char msg[30];
    int len = snprintf(msg, sizeof(msg), "int16 centicelsius[%5u]:", sizeof(t)/sizeof(*t));
    web_server.setContentLength(CONTENT_LENGTH_UNKNOWN); // len + sizeof(t)
    web_server.send(200, "application/octet-stream", "");
      web_server.sendContent(msg, len);
    unsigned chunk = 1024;
    char *pos = (char *)t;
    char *end = pos + sizeof(t);
    while( pos < end - chunk ) {
      web_server.sendContent(pos, chunk);
      pos += chunk;
    }
    web_server.sendContent(pos, end - pos);
    web_server.sendContent("");
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
      "/on, /off, /reset, /version, /temperature, /history.bin or "
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


void print_temperature_table() {
  const uint32_t rv_10k  = 10000;
  const uint32_t rv_100k = 100000;
  double t_10k_prev  = 0.0;
  double t_100k_prev = 0.0;

  for( uint16_t a = 0; a < a_max; a++ ) {
    uint32_t r_ntc_v10k  = rv_10k  * a / (a_max - a);
    uint32_t r_ntc_v100k = rv_100k * a / (a_max - a);

    double t_10k  = 1.0 / (1.0/(273.15+T_n) + log((double)r_ntc_v10k /R_n)/B) - 273.15;
    double t_100k = 1.0 / (1.0/(273.15+T_n) + log((double)r_ntc_v100k/R_n)/B) - 273.15;

    Serial.printf("%4u: t10= %5.1f t10diff= %5.1f t100diff= %5.1f t100= %5.1f\n", 
      a, t_10k, t_10k-t_10k_prev, t_100k-t_100k_prev, t_100k);

    t_10k_prev  = t_10k;
    t_100k_prev = t_100k;

    delay(1);
  }
}


void updateTemperature() {
  // only print if measurement decimals change 
  static int16_t t_prev = 0;

  temperature_c = 1.0 / (1.0/(273.15+T_n) + log((double)r_ntc/R_n)/B) - 273.15;

  int16_t temp = (int16_t)(temperature_c * 100 + 0.5); // rounded centi celsius
  if( (temp - t_prev) * (temp - t_prev) >= 10 * 10 ) { // only report changes >= 0.1
    Serial.printf("Temperature: %01d.%01d degree Celsius\n", temp/100, (temp/10)%10);
    t_prev = temp;
  }
}


void updateResistance() {
  static const uint32_t Max_sum = (uint32_t)a_max * a_samples;

  r_ntc = (int64_t)R_v * a_sum / (Max_sum - a_sum);

  updateTemperature();
}


void handleAnalog() {
  static uint16_t a[a_samples] = { 0 }; // last analog reads
  static uint16_t a_pos = a_samples;    // sample index

  int value = analogRead(A0);

  // first time init
  if( a_pos == a_samples ) {
    while( a_pos-- ) {
      a[a_pos] = (uint16_t)value;
      a_sum += (uint32_t)value;
    }
  }
  else {
    if( ++a_pos >= a_samples ) {
      a_pos = 0;
    }

    if( a_pos % 16 == 0 ) {
      delay(10); // needed for wifi !?
    }

    a_sum -= a[a_pos];
    a[a_pos] = (uint16_t)value;
    a_sum += a[a_pos];
  }

  updateResistance();
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

  // print_temperature_table();

  Serial.println("\nBooted " VERSION);
}


void handleFrequency() {
  static uint32_t start = 0;
  static uint32_t count = 0;

  count++;

  uint32_t now = millis();
  if( now - start > 1000 ) {
    printf("Measuring analog at %u Hz\n", count);
    start = now;
    count = 0;
  }
}


void handleTempHistory() {
  static bool first = true;
  uint16_t temp = (int16_t)(temperature_c * 100 + 0.5); 
  if( first ) {
    t_pos = sizeof(t)/sizeof(*t);
    while( --t_pos > 0 ) {
      t[t_pos] = -30000;
    }
    t[t_pos] = temp;
    first = false;
  }
  else {
    if( ++t_pos >= sizeof(t)/sizeof(*t) ) {
      t_pos = 0;
    }
    t[t_pos] = temp;
  }
}


void loop() {
  handleFrequency();
  handleAnalog();
  handleTempHistory();
  handleDuty(duty);
  handleWifi();
}