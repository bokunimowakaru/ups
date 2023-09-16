# ups
DIY UPS VRLA Battery Controller for ESP32C3, ESP32, M5Stack Core  

Network devices and servers connected to USB ports will continue to operate even in power outage.

<font color="red">CONTENTS ON THIS REPOSITORY ARE UNDER DEVELOPMENT</font>  

![DIY UPS System Example](/pictures/ups_m5stack.jpg)  
Fig. DIY UPS System Using M5Stack Core and 12V 7.2Ah VRLA Battery

![DIY UPS System in Power Outage](/pictures/ups_m5stack_outage.jpg)  
Fig. Server Continuity Plan in Power Outage

### WARNING
I do not take any responsibility for safety.
For example, mishandling batteries may result in danger to your life or loss of your property.

## Supported MCUs
- ESP32C3
- ESP32
- M5Stack Core

## DIY UPS Schematic for M5Stack Core

![Schematic](/pictures/schematic_m5stack.png)  
Fig. Schematic of DIY UPS Battery Controller for M5Stack Core

## Hardware Adjusting CV Charging Voltage
This charging system use CV; constant voltage charging, with current limit resistor 1.8 Ohm.  
The CV Charging Voltage is set using a 50k Ohm variable resistor by manually.
Please adjust the output voltage is NOT over 13.8 V or under 10.8V. 
For the example setting from 12.0 V to 13.8 V, if it is set 12.0 V, the backup time in outage might be shorter.
But if sets 13.8V, the battery life might be shorter than 12.0 V setting.

## Software Definition ADC_CHG_DIV and ADC_BAT_DIV
The values of definition ADC_CHG_DIV and ADC_BAT_DIV need to be adjusted.  
These are the voltage divided factors for the voltage measurements.  
ADC_CHG_DIV is for the output voltage of NJM317F regulator for battery charging, and ADC_BAT_DIV is for the battery voltage.  
If you put the resistors 100k, 10k, and 12k which have enough tolerance to measure, these values are caliculated as below.  

	#define ADC_CHG_DIV 12./(110.+12.)  
	#define ADC_BAT_DIV 12./(110.+12.)  

But Generally, resistors have an error tolerance of +/-5% or +/-1%. So you need to adjust the definition values.

1. Measure the output voltage of NJM317F, the value is for the denominator of ADC_CHG_DIV.  
2. Measure the divided voltage of NJM317F by resistors at ADC_CHG. The value is for the numerator of ADC_CHG_DIV.  
3. In a similar way, measure for ADC_BAT_DIV at BATT and ADC_BAT.  
4. Edit the values like a below example.  

	#define ADC_CHG_DIV 1.353 / 13.78  
	#define ADC_BAT_DIV 1.363 / 13.72  

## Planning

### Features
- for 12V VRLA Batteries
- Level Meters show Charging/Discharging Power and Battery Voltage
- USB Type A Female Output Ports, 5V 500mA
- the Ambient; the web monitor service shows visible charts such as the sensor values
- LINE Notify; a message notification service on the LINE platform

![Monitor on Web Service Ambient](/pictures/ups_ambient02.png)  
Fig. Web Service the Ambient for Monitoring Sensor Values

![LINE Notify](/pictures/ups_line01.png)  
Fig. LINE App Gets Message from the UPS

### Inputs of 2 ADCs used for Voltage And Current Measurement  
One ADC is for battery voltage measurement.  
The other is for measuring the charging or discharging current of the battery via a small resistor.  

### Circuits for ADC Inputs
Series R=110k Ohm and R=12k Ohm resistores devides the battery voltage to 1/10.  

### Input on a GPIO used for Detecting Outage  
One GPIO input is used to detect the DC voltage output by the AC adapter.  
This is also to detect when there is a power outage in your home.  

### Circuits for ADC Inputs
Series R=47k Ohm and R=12k Ohm resistores devides the 16V output of AC adapter to 3.3V.  

### Outputs of 2 GPIOs used for Switching Charging or Discharging  
Two of the series FETs switches the connetcion modes of the charging, the discharging, and disconected mode.  
In the charging mode and the discharging mode, the both of FETs are turned On.  
And if the voltage of batterry reached too high voltage for CV charging, turned Off the charging FET.  
Both FETs are turned off to prevent over-discharge when the voltage drops below the end voltage.  

## Pinout for M5Stack Core

|GPIO	|Direction	|to	|
|-------|-----------|---|
|2	|Output	|Charging FET	|
|5	|Output	|Discharging FET	|
|26	|Input	|Outage Detector	|
|35(ADC1_7)	|Input	|Charger Voltage	|
|36(ADC1_0)	|Input	|Battery Voltage	|

## Pinout for ESP32C3

|Pin	|割当	|接続	|	|Pin	|割当	|接続	|
|-------|-------|-------|---|-------|-------|-------|
|1	|3V3	|<b>Power(3.3V)	|	|18	|IO 0	|NA(or GND)	|
|2	|EN	|<s>Pull Up, RESET SW	|	|17	|IO 1	|<b>Outage Detector</b>(USER SW)	|
|3	|IO 4	|<b>Charging FET	|	|16	|IO 2	|<b>Charger Voltage</b>(ADC1_2)	|
|4	|IO 5	|<b>Discharging FET	|	|15	|IO 3	|<b>Battery Voltage</b>(ADC1_3)	|
|5	|IO 6	|	|	|14	|IO19	|(I2C SCL)	|
|6	|IO 7	|	|	|13	|IO18	|NA(or GND)	|
|7	|IO 8	|Pull Up, RGB LED	|	|12	|TXD	|<s>Serial RX	|
|8	|IO 9	|<s>Pull Up, BOOT SW	|	|11	|RXD	|<s>Serial TX	|
|9	|GND	|<b>Power(GND)	|	|10	|IO10	|(I2C SDA)	|

## Functional Design for 12V VRLA Batteries

12V VRLA Batteries are structed series 6 cells, and each cells work if the cell volteges meet to 2.0V or between 1.8V to 2.3V. 
This is CV charging system for UPS, so the charging voltage is set to 13.8V.
And the stopping voltage for charging is set to 14.7, it's protected from unexpected higher voltege, such as breakdown circuits.

|Mode	|Battery Voltege	|Function	|Condition	|
|-------|-------------------|-----------|-----------|
|Charging	|2.3 x 6 = 13.8V	|Stop Charging	|Over 14.7V	|
|Fatal	|1.8 x 6 = 10.8V|Disconnect	|Under 10.8V	|

----------------------------------------------------------------

by 国野 亘 Wataru KUNINO 
- ウェブサイト [https://bokunimo.net/](https://bokunimo.net/)
- ブログ [https://bokunimo.net/blog/](https://bokunimo.net/blog/)
- ESP32メニュー [https://bokunimo.net/blog/menu/esp/](https://bokunimo.net/blog/menu/esp/)
- M5Stackメニュー [https://bokunimo.net/blog/menu/m5stack/](https://bokunimo.net/blog/menu/m5stack/)

----------------------------------------------------------------

## GitHub Pages by bokunimo.net 

*  (This Document)  
  [https://git.bokunimo.com/ups/](https://git.bokunimo.com/ups/)  

* ESP  
  [https://git.bokunimo.com/esp/](https://git.bokunimo.com/esp/)

* ESP32C3  
  [https://git.bokunimo.com/esp32c3/](https://git.bokunimo.com/esp32c3/)

* M5Stack  
  [https://git.bokunimo.com/m5/](https://git.bokunimo.com/m5/)

----------------------------------------------------------------

# git.bokunimo.com GitHub Pages site
[http://git.bokunimo.com/](http://git.bokunimo.com/)  

----------------------------------------------------------------
