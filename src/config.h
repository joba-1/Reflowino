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
#define SWITCH_PIN       D8
#define DUTY_CYCLE_MS    1000

// PID stuff
#define PID_K_P          0.6
#define PID_K_I          0.1
#define PID_K_D          0.8

// Analog samples for averaging
#define A_SAMPLES        4000
#define A_MAX            1023

// NTC parameters and voltage divider resistor
#define NTC_B            3999
#define NTC_R_N          100000
#define NTC_T_N          25
#define NTC_R_V          10000              

// Neopixel stuff
#define PIXEL_PIN        D5
#define NUM_PIXELS       2
#define NEO_CONFIG       (NEO_RGB+NEO_KHZ800)

#endif
