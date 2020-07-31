#ifndef CONFIG_H
#define CONFIG_H

// Network stuff, might already be defined by the build tools
#ifdef WLANCONFIG
  #include <WlanConfig.h>
#endif
#ifndef SSID
  #define SSID      WlanConfig::Ssid
#endif
#ifndef PASS
  #define PASS      WlanConfig::Password
#endif
#ifndef NAME
  #define NAME      "Reflow"
#endif
#ifndef PORT
  #define PORT      80
#endif

// Syslog server connection info
#define SYSLOG_SERVER "192.168.1.4"
#define SYSLOG_PORT 514

#define ONLINE_LED_PIN   D4

// Switch stuff
#define SWITCH_PIN       D3
#define DUTY_CYCLE_MS    100

// Neopixel stuff
#define PIXEL_PIN        D5
#define NUM_PIXELS       2
#define NEO_CONFIG       (NEO_RGB+NEO_KHZ800)

#endif
