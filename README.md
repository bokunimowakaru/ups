# ups
DIY UPS VRLA Batteries Controller for ESP32C3, ESP32, M5Stack Core  

<font color="red">CONTENTS ON THIS REPOSITORY ARE UNDER DEVELOPMENT</font>  

### WARNING
I do not take any responsibility for safety.
For example, mishandling batteries may result in danger to your life or loss of your property.

## Supported MCUs
- ESP32C3
- ESP32
- M5Stack Core

## Planning

### Features
- for 12V VRLA Batteries
- Charging / Discharging / Battery Level Meter

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
In the charging mode, the both of FETs are turned On.  
And in the discharging mode, turned Off the charging FET.  
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
|1	|3V3	|Power(3.3V)	|	|18	|IO 0	|NA	|
|2	|EN	|Pull Up, RESET SW	|	|17	|IO 1	|USER SW	|
|3	|IO 4	|	|	|16	|IO 2	|	|
|4	|IO 5	|	|	|15	|IO 3	|	|
|5	|IO 6	|	|	|14	|IO19	|	|
|6	|IO 7	|	|	|13	|IO18	|NA	|
|7	|IO 8	|Pull Up, RGB LED	|	|12	|TXD	|Serial RX	|
|8	|IO 9	|Pull Up, BOOT SW	|	|11	|RXD	|Serial TX	|
|9	|GND	|Power(GND)	|	|10	|IO10	|LED	|

## Functional Design for 12V VRLA Batteries

12V VRLA Batteries are structed series 6 cells, and each cells work if the cell volteges meet to 2.0V or between 1.8V to 2.3V. 

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
