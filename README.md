# Reflowino

use an ESP8266, an ADS1115, two NTCs, an SSR and a cheap electrical toaster grill to build an SMD PCB reflow oven

## Status

Work in progress.

* Built into oven. Works fine. Currently no ADS1115 but internal A0 and only one NTC.
* Temperature can be set via webpage and is maintained by pid loop.
* PID parameters need optimization. 20% overshoot.
* A pwm style fixed duty cycle of the SSR can be controlled via webpage
* OTA is working to avoid touching high voltage stuff
* Syslog works. Needed to give A0 to WIFI ~10ms within 40ms
* Theory for temperature measuring is done (see below). Maybe needs a bit more calibration.

## Todo

* PID parameters via webpage
* Define temperature profile via web page
* eans to store/retrieve profiles (could be spiffs, EEPROM, MQTT persistent topics, ...)
* Provide status via Neopixel colors, mqtt, webpage

## NTC Temperature Measurement

Formula for getting temperature in K from measuring Rntc

    T = 1 / (1/Tn + ln(Rt/Rn)/B)  (1)

* Tn = Temperature where Rntc = Rn. Usually 25°C -> 298.15K, but check datasheet to be sure.
* Rn = NTC Resistance at Tn. Usually 10k .. 100k. Better measure yourself, can differ quite a bit from datasheet.
* B = B-constant from datasheet. Often 3950 K.
* Rt = measured resistance

    Rt,min = Rn * e^(B/Tmin - B/Tn)  with Tmin = 0°C   -> Rt,min ~ 340k
    Rt,max = Rn * e^(B/Tmax - B/Tn)  with Tmax = 260°C -> Rt,max ~ 290

### Using an ADS1115

Using an ADS1115 you can compare Vcc to Vntc like the following sketch.
Since using Va3 as Varef === 0 it eliminates errors at Vax due to Vcc fluctuations.
Comparing Ax to A3 with A3 === 0 gives Ax ~ 0 for very high Rntc and Ax ~ -Amax for very low Rtc.

    Vcc ---+--- Rv ---+--- Rntc --- Gnd 
           |          |
           A3         Ax (Vcc=0...-32667=Gnd)

Voltage divider and some algebra with ohms law gives

Rntc = -Rv * (1 + Amax/Ax) (2)

Sanity check: 
If Rv chosen to be same as Rn, then Vntc at Ax should be Vcc/2 at 25°C.
Vcc/2 should result in Ax = -Amax/2. Put this Ax in (2) yields Rntc = Rv --> as expected.
Put this Rntc as Rt in (1) gives T = 1 / (1/298.15 + 1/3950*ln(1)) = 298.15K = 25°C --> as expected.

Choosing Rv: 100k is good for low to medium range up to 150°C but 10k gives ~10x better resolution at the high temperatures around 250°C that we need. An Rv of 1k gives bad resolution at room temperature, so my Rv will be 10k.

### Using ESP8266 ADC via NodeMCU A0

This method is less accurate, but maybe good enough for the usecase:

                              ADC
                               |
                  +--- 220k ---+--- 100k --- Gnd
                  |
                  A0 (Vcc=1023...0=Gnd)
                  |
    Vcc --- Rv ---+--- Rntc --- Gnd

Calculation

    Vcc/(Rv+Rntc) = Vntc/Rntc                | * (Rv+Rntc) * Rntc
    -> Vcc * Rntc = Vntc * Rv + Vntc * Rntc  | - (Vntc * Rntc)        
    -> Rntc * (Vcc-Vntc) = Rv * Vntc         | / (Vcc-Vntc)
    -> Rntc = Rv * Vntc/(Vcc-Vntc)           | * (1/Vcc)/(1/Vcc)
    -> Rntc = Rv * Vntc/Vcc/(1 - Vntc/Vcc)   | Vntc/Vcc = A0/Amax (Amax = 1023)
    -> Rntc = Rv * A0/1023 / (1-A0/1023)     | * (1023/1023)
    -> Rntc = Rv * A0 / (1023 - A0)

Solve for A0 to do some test calculations: 
    Rntc = Rv * A0 / (1023 - A0)       | * (1023 - A0)
    -> Rntc * 1023 - Rntc * A0 = Rv * A0  | + (Rntc * A0)
    -> Rntc * 1023 = (Rv + Rntc) * A0     | / (Rv + Rntc)
    -> Rntc * 1023 / (Rv + Rntc) = A0 

## PID Loop or Bang Bang?

I can only switch power on or off -> classic usecase for bang bang algorithm -> 
switch on if below (Tset - lower tolerance) and switch off if above (Tset + upper tolerance).
Choose tolerances such that T is close enough to Tset (-> low tolerances), switching is rare enough (-> high tolerances) and mean of T over an on/off cycle is as close as possible to Tset (-> since cooling is slower than heating, lower tolerance will be less than high tolerance). 

But since I can switch on/off easily and relatively fast with SSR I can also do a kind of slow PWM and use that for PID control. Should result in T following Tset with less oscillation. Maybe that means even less stress on material, but could also mean more EMV due to more high power switching.
