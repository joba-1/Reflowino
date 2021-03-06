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
  #define VERSION   NAME " 2.0 " __DATE__ " " __TIME__
#endif

// Syslog
WiFiUDP udp;
Syslog syslog(udp, SYSLOG_PROTO_IETF);

Adafruit_NeoPixel pixels = Adafruit_NeoPixel(NUM_PIXELS, PIXEL_PIN, NEO_CONFIG);

ESP8266WebServer web_server(PORT);

ESP8266HTTPUpdateServer esp_updater;

uint16_t _duty = 100;               // ssr pwm percent. Make oven useful without WLAN
bool _fixed_duty = true;            // true: decouple from temperature control

// Analog read samples
const uint16_t A_samples = A_SAMPLES;
const uint16_t A_max = A_MAX;
uint32_t _a_sum = 0;                // sum of last analog reads

// NTC characteristics (datasheet)
static const uint32_t B = NTC_B;
static const uint32_t R_n = NTC_R_N; // Ohm
static const uint32_t T_n = NTC_T_N; // Celsius

static const uint32_t R_v = NTC_R_V; // Ohm, voltage divider resistor for NTC

uint32_t _r_ntc = 0;                // Ohm, resistance updated with each analog read
double _temp_c = 0;                 // Celsius, calculated from NTC and R_v
uint16_t _temp_target = 0;          // adjust _duty to reach this temperature

// PID stuff
double _pid_kp = PID_K_P;
double _pid_ki = PID_K_I;
double _pid_kd = PID_K_D;

// Temperature history
int16_t _t[8640];      // 1 day centicelsius temperature history in 10s intervals
uint16_t _t_pos;


// Default html menu page
void send_menu( const char *msg ) {
  static const char header[] = "<!doctype html>\n"
    "<html lang=\"en\">\n"
      "<head>\n"
        "<meta charset=\"utf-8\">\n"
        "<meta name=\"keywords\" content=\"Reflowino, SSR, remote, meta\">\n"
        "<meta http-equiv=\"refresh\" content=\"10;url=/\">\n"
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
          "<form action=\"/target\" mode=\"POST\">\n"
            "<td><label for=\"celsius\">Target</label></td><td>%u&#8451;</td>\n"
            "<td>0&#8451;</td><td colspan=\"2\"><input id=\"celsius\", name=\"celsius\" type=\"range\" min=\"0\" max=\"250\" value=\"%u\"/></td><td>250&#8451;</td>\n"
            "<td><button>Set</button></td>\n"
          "</form></tr><tr>\n"
          "<form action=\"/duty\" mode=\"POST\">\n"
            "<td><label for=\"percent\">Duty</label></td><td>%u%%</td>\n"
            "<td>0%%</td><td colspan=\"2\"><input id=\"percent\", name=\"percent\" type=\"range\" min=\"0\" max=\"100\" value=\"%u\"/></td><td>100%%</td>\n"
            "<td><button>Set</button></td>\n"
          "</form></tr><tr>\n"
          "<td colspan=\"2\">PID Parameters</td></tr><tr>\n"
          "<form action=\"/kp\" mode=\"POST\">\n"
            "<td><label for=\"kp\">Kp</label></td><td>%4.2f</td>\n"
            "<td>0.00</td><td colspan=\"2\"><input id=\"kp\", name=\"kp\" type=\"range\" min=\"0.00\" max=\"10.00\" step=\"0.01\" value=\"%4.2f\"/></td><td>10.00</td>\n"
            "<td><button>Set</button></td>\n"
          "</form></tr><tr>\n"
          "<form action=\"/ki\" mode=\"POST\">\n"
            "<td><label for=\"ki\">Ki</label></td><td>%4.2f</td>\n"
            "<td>0.00</td><td colspan=\"2\"><input id=\"ki\", name=\"ki\" type=\"range\" min=\"0.00\" max=\"10.00\" step=\"0.01\" value=\"%4.2f\"/></td><td>10.00</td>\n"
            "<td><button>Set</button></td>\n"
          "</form></tr><tr>\n"
          "<form action=\"/kd\" mode=\"POST\">\n"
            "<td><label for=\"kd\">Kd</label></td><td>%4.2f</td>\n"
            "<td>0.00</td><td colspan=\"2\"><input id=\"kd\", name=\"kd\" type=\"range\" min=\"0.00\" max=\"10.00\" step=\"0.01\" value=\"%4.2f\"/></td><td>10.00</td>\n"
            "<td><button>Set</button></td>\n"
          "</form></tr><tr><td>\n";
  static const char footer[] =
          "<form action=\"/on\" mode=\"POST\">\n"
            "<button>ON</button>\n"
          "</form></td><td>\n"
          "<form action=\"/off\" mode=\"POST\">\n"
            "<button>OFF</button>\n"
          "</form></td><td>\n"
          "<form action=\"/reset\" mode=\"POST\">\n"
            "<button>Reset</button>\n"
          "</form></td><td>\n"
         "<form action=\"/version\" mode=\"POST\">\n"
            "<button>Version</button>\n"
          "</form></td></tr>\n"
        "</table>\n"
      "</body>\n"
    "</html>\n";
  static char page[sizeof(form)+100]; // form + variables

  size_t len = sizeof(header) + sizeof(footer) - 2;
  len += snprintf(page, sizeof(page), form, msg, _temp_c, _r_ntc, _a_sum, 
    _temp_target, _temp_target, _duty, _duty, _pid_kp, _pid_kp, _pid_ki, _pid_ki, _pid_kd, _pid_kd);

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
    snprintf(msg, sizeof(msg), "%5.1f", _temp_c);
    web_server.send(200, "text/plain", msg);
  });

  // TODO crashes...
  web_server.on("/history.bin", []() {
    char msg[30];
    int len = snprintf(msg, sizeof(msg), "int16 centicelsius[%5u]:", sizeof(_t)/sizeof(*_t));
    web_server.setContentLength(CONTENT_LENGTH_UNKNOWN); // len + sizeof(t)
    web_server.send(200, "application/octet-stream", "");
      web_server.sendContent(msg, len);
    unsigned chunk = 1024;
    char *pos = (char *)_t;
    char *end = pos + sizeof(_t);
    while( pos < end - chunk ) {
      web_server.sendContent(pos, chunk);
      pos += chunk;
    }
    web_server.sendContent(pos, end - pos);
    web_server.sendContent("");
  });

  // Set duty cycle
  web_server.on("/duty", []() {
    if( web_server.arg("percent") != "" ) {
      long i = web_server.arg("percent").toInt();
      if( i >= 0 && i <= 100 ) {
        _duty = (unsigned)i;
        _fixed_duty = true;
        char msg[20];
        snprintf(msg, sizeof(msg), "Set duty: %u", _duty);
        send_menu(msg);
      }
      else {
        send_menu("ERROR: Set duty percentage out of range (0-100)");
      }
    }
    else {
      send_menu("ERROR: Duty without percentage");
    }
    syslog.logf(LOG_NOTICE, "DUTY %u", _duty);
  });

  // Set target temperature
  web_server.on("/target", []() {
    if( web_server.arg("celsius") != "" ) {
      long c = web_server.arg("celsius").toInt();
      if( c >= 0 && c <= 300 ) {
        _temp_target = (uint16_t)c;
        if( _temp_target == 0 ) {
          _duty = 0;
          _fixed_duty = true;
        }
        else {
          _fixed_duty = false;
        }
        char msg[40];
        snprintf(msg, sizeof(msg), "Set target: %u degrees celsius", _temp_target);
        send_menu(msg);
      }
      else {
        send_menu("ERROR: Set target temperature out of range (-100-300)");
      }
    }
    else {
      send_menu("ERROR: Target without value");
    }
    syslog.logf(LOG_NOTICE, "TARGET %u", _temp_target);
  });

  // Set pid Kp
  web_server.on("/kp", []() {
    if( web_server.arg("kp") != "" ) {
      double kp = web_server.arg("kp").toDouble();
      if( kp >= 0.0 && kp <= 10.0 ) {
        _pid_kp = kp; // TODO: store in EPROM
        char msg[40];
        snprintf(msg, sizeof(msg), "Set Kp: %5.2f", _pid_kp);
        send_menu(msg);
      }
      else {
        send_menu("ERROR: Set Kp out of range (0.0-10.0)");
      }
    }
    else {
      send_menu("ERROR: Kp without value");
    }
    syslog.logf(LOG_NOTICE, "Kp %5.2f", _pid_kp);
  });

  // Set pid Ki
  web_server.on("/ki", []() {
    if( web_server.arg("ki") != "" ) {
      double ki = web_server.arg("ki").toDouble();
      if( ki >= 0.0 && ki <= 10.0 ) {
        _pid_ki = ki; // TODO: store in EPROM
        char msg[40];
        snprintf(msg, sizeof(msg), "Set Ki: %5.2f", _pid_ki);
        send_menu(msg);
      }
      else {
        send_menu("ERROR: Set Ki out of range (0.0-10.0)");
      }
    }
    else {
      send_menu("ERROR: Ki without value");
    }
    syslog.logf(LOG_NOTICE, "Ki %5.2f", _pid_ki);
  });

  // Set pid Kd
  web_server.on("/kd", []() {
    if( web_server.arg("kd") != "" ) {
      double kd = web_server.arg("kd").toDouble();
      if( kd >= 0.0 && kd <= 10.0 ) {
        _pid_kd = kd; // TODO: store in EPROM
        char msg[40];
        snprintf(msg, sizeof(msg), "Set Kd: %5.2f", _pid_kd);
        send_menu(msg);
      }
      else {
        send_menu("ERROR: Set Kd out of range (0.0-10.0)");
      }
    }
    else {
      send_menu("ERROR: Kd without value");
    }
    syslog.logf(LOG_NOTICE, "Kd %5.2f", _pid_kd);
  });

  // Call this page to see the ESPs firmware version
  web_server.on("/on", []() {
    _duty = 100;
    _fixed_duty = true;
    send_menu("On");
    syslog.log(LOG_NOTICE, "ON");
  });

  // Call this page to see the ESPs firmware version
  web_server.on("/off", []() {
    _duty = 0;
    _fixed_duty = true;
    send_menu("Off");
    syslog.log(LOG_NOTICE, "OFF");
  });

  // Call this page to reset the ESP
  web_server.on("/reset", []() {
    syslog.log(LOG_NOTICE, "RESET");
    send_menu("Resetting...");
    delay(200);
    ESP.restart();
  });

  // This page configures all settings (/cfg?name=value{&name=value...})
  web_server.on("/", []() {
    char msg[80];
    snprintf(msg, sizeof(msg), "Welcome! (Set %u&#8451;, Duty %u%%%s)", _temp_target, _duty, _fixed_duty ? " locked" : "");
    send_menu(msg);
  });

  // Catch all page, gives a hint on valid URLs
  web_server.onNotFound([]() {
    web_server.send(404, "text/plain", "error: use "
      "/on, /off, /reset, /version, /temperature, /history.bin, /duty, /target or "
      "post image to /update\n");
  });

  web_server.begin();

  MDNS.addService("http", "tcp", PORT);
  syslog.logf(LOG_NOTICE, "Serving HTTP on port %d", PORT);
}


// Handle online web updater, initialize it after Wifi connection is established
void handleWifi() {
  static bool updater_needs_setup = true;
  static bool first_connect = true;

  if( WiFi.status() == WL_CONNECTED ) {
    if( first_connect ) {
      first_connect = false;
      if( _fixed_duty && _duty == 100 ) { // doubler check
        syslog.log(LOG_NOTICE, "WLAN on -> oven OFF");
        _duty = 0; // now controlled via WLAN
      }
    }
    if( updater_needs_setup ) {
      // Init once after connection is (re)established
      digitalWrite(ONLINE_LED_PIN, LOW);
      Serial.printf("WLAN '%s' connected with IP ", SSID);
      Serial.println(WiFi.localIP());
      syslog.logf(LOG_NOTICE, "WLAN '%s' IP %s", SSID, WiFi.localIP().toString().c_str());

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


void handleDuty( const unsigned duty ) {
  static bool state = LOW;
  static uint32_t since = 0;

  uint32_t now = millis();
  if( (now - since) >= DUTY_CYCLE_MS ) {
    since = now;
  }

  if( (now - since) >= (((uint32_t)duty * DUTY_CYCLE_MS) / 100) ) {
    if( state == HIGH ) {
      state = LOW;
      digitalWrite(SWITCH_PIN, state);
    }
  }
  else {
    if( state == LOW ) {
      state = HIGH;
      digitalWrite(SWITCH_PIN, state);
    }
  }
}


void handleControl( const double control, uint16_t &duty ) {
  if( control <= 0 ) {
    duty = 0;
  }
  else if( control >= 100 ) {
    duty = 100;
  }
  else {
    duty = (uint16_t)(control + 0.5); 
  }
}


// Hint: make sure the physical relation between control_variable and current_value is as linear as possible
void handlePid( const double current_value, const double set_point, const double min_error, const double max_sum, double &control_variable ) {
  static double error_sum = 0;
  static uint32_t prev_time = 0;

  double error = set_point - current_value;

  if( (error > 0 && error > min_error) || (error < 0 && error < -min_error) ) { // ignore minimal deviations (probably noise)
    uint32_t now = millis();
    double delta_t = 0.001 * (now - prev_time);
    prev_time = now;
    if( delta_t > 1 ) { // long time no see:
      error_sum = 0;    // ...better start over without wind up
      control_variable = 0;
    }
    else {
      control_variable = _pid_kp * error;
      if( delta_t > 0 ) { // ignore zero time delta if called too fast
        if( (error > 0 && _pid_ki * error_sum < max_sum) || (error < 0 && _pid_ki * error_sum > -max_sum) ) { // limit wind up
          error_sum += error * delta_t;
        }
        control_variable += _pid_ki * error_sum + _pid_kd * error / delta_t;
      }
    }
  }
}


void print_temperature_table() {
  const uint32_t rv_10k  = 10000;
  const uint32_t rv_100k = 100000;
  double t_10k_prev  = 0.0;
  double t_100k_prev = 0.0;

  for( uint16_t a = 0; a < A_max; a++ ) {
    uint32_t r_ntc_v10k  = rv_10k  * a / (A_max - a);
    uint32_t r_ntc_v100k = rv_100k * a / (A_max - a);

    double t_10k  = 1.0 / (1.0/(273.15+T_n) + log((double)r_ntc_v10k /R_n)/B) - 273.15;
    double t_100k = 1.0 / (1.0/(273.15+T_n) + log((double)r_ntc_v100k/R_n)/B) - 273.15;

    Serial.printf("%4u: t10= %5.1f t10diff= %5.1f t100diff= %5.1f t100= %5.1f\n", 
      a, t_10k, t_10k-t_10k_prev, t_100k-t_100k_prev, t_100k);

    t_10k_prev  = t_10k;
    t_100k_prev = t_100k;

    delay(1);
  }
}


// quick and dirty while I do not have an oven to play with...
// cool down linear from 250 to 25 in 12 min
// heat up at 100% duty linear from 25 to 250 in 2 min
void simulate_temp( double &temp_c ) {
  static uint32_t prev = 0;
  static const double cool_step = (250.0 - 25) / (12 * 60 * 1000); // deg/milli 
  static const double heat_step = (250.0 - 25) / (2 * 60 * 1000);  // deg/milli 

  uint32_t now = millis();
  uint32_t elapsed = now - prev;

  if( elapsed < 100 ) {
    return; // don't update too often
  }

  prev = now;

  if( elapsed > 1000 ) {
    return; // first time after switching on or elapsed would be smaller
  }

  temp_c -= cool_step * elapsed;
  temp_c += heat_step * _duty / 100 * elapsed;
}


void updateTemperature( const uint32_t r_ntc, double &temp_c ) {
  // only print if measurement decimals change 
  static int16_t t_prev = 0;

  // if( _fixed_duty ) {
  temp_c = 1.0 / (1.0/(273.15+T_n) + log((double)r_ntc/R_n)/B) - 273.15;
  // }
  // else {
  //   simulate_temp(temp_c);
  // }

  int16_t temp = (int16_t)(temp_c * 100 + 0.5); // rounded centi celsius
  if( (temp - t_prev) * (temp - t_prev) >= 10 * 10 ) { // only report changes >= 0.1
    // Serial.printf("Temperature: %01d.%01d degree Celsius\n", temp/100, (temp/10)%10);
    t_prev = temp;
  }
}


void updateResistance( const uint32_t a_sum, uint32_t &r_ntc, double &temp_c ) {
  static const uint32_t Max_sum = (uint32_t)A_max * A_samples;

  r_ntc = (int64_t)R_v * a_sum / (Max_sum - a_sum);

  updateTemperature(r_ntc, temp_c);
}


void handleAnalog( uint32_t &a_sum, uint32_t &r_ntc, double &temp_c ) {
  static uint16_t a[A_samples] = { 0 }; // last analog reads
  static uint16_t a_pos = A_samples;    // sample index

  if( millis() % 40 > 10 ) { // now and then release analog for wifi
    int value = analogRead(A0);

    // first time init
    if( a_pos == A_samples ) {
      while( a_pos-- ) {
        a[a_pos] = (uint16_t)value;
        a_sum += (uint32_t)value;
      }
    }
    else {
      if( ++a_pos >= A_samples ) {
        a_pos = 0;
      }

      a_sum -= a[a_pos];
      a[a_pos] = (uint16_t)value;
      a_sum += a[a_pos];
    }

    updateResistance(a_sum, r_ntc, temp_c);
  }
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


void handleTempHistory( const double temp_c, int16_t t[], const uint16_t t_entries, uint16_t &t_pos ) {
  static bool first = true;
  uint16_t temp = (int16_t)(temp_c * 100 + 0.5); 
  if( first ) {
    t_pos = t_entries;
    while( --t_pos > 0 ) {
      t[t_pos] = -30000; // mark as clearly invalid because below absolute zero
    }
    t[t_pos] = temp;
    first = false;
  }
  else {
    if( ++t_pos >= t_entries ) {
      t_pos = 0;
    }
    t[t_pos] = temp;
  }
}


void setup() {
  // start with switch off:
  pinMode(SWITCH_PIN, OUTPUT);
  digitalWrite(SWITCH_PIN, LOW);

  Serial.begin(115200);

  // Initiate network connection (but dont wait for it)
  setup_Wifi();

  // Syslog setup
  syslog.server(SYSLOG_SERVER, SYSLOG_PORT);
  syslog.deviceHostname(NAME);
  syslog.appName("Joba1");
  syslog.defaultPriority(LOG_KERN);

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


void loop() {
  // handleFrequency();
  handleAnalog(_a_sum, _r_ntc, _temp_c);
  handleTempHistory(_temp_c, _t, sizeof(_t)/sizeof(*_t), _t_pos);

  static uint32_t prev = 0;
  static uint16_t count = 0;
  uint32_t now = millis();
  if( (_temp_target && !_fixed_duty) && (now - prev > 100) ) {
    double control;
    handlePid(_temp_c, _temp_target, 0.2, 100, control);
    handleControl(control, _duty);
    char msg[80];
    snprintf(msg, sizeof(msg), "Temp=%5.1f, Set=%3u, Control=%5.1f, Duty=%3u", _temp_c, _temp_target, control, _duty);
    Serial.println(msg);
    if( count-- == 0 ) {
      syslog.log(LOG_INFO, msg);
      count = 100;
    }
    prev = now;
  }
  handleDuty(_duty);
  handleWifi();
  delay(1);
}