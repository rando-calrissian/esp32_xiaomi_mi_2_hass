# esp32_xiaomi_mi_2_hass
Xiaomi Mi Body Composition Scale integration into Home Assistant using an ESP32 as a BLE to Wifi bridge with Appdaemon processing the data.
The Appdaemon app will create all sensors automatically as well as sent notifications to each user of their stats as well as the delta from their previous reading (any weight/muscle/etc. gained/lost).

## Acknowledgements: 
This builds on top of the work of lolouk44 https://github.com/lolouk44/xiaomi_mi_scale and directly uses the python functions of that tool to perform its measurements.

## Requirements:
1. Xiaomi Mi Scale V2 (V1 may also work, it has not been tested).
2. ESP32
3. MQTT Broker
4. Home Assistant using AppDaemon.

## Setup:
You'll need to know the mac adress of your scale.  If you have a linux machine with bluetooth, the following command will scan for you:
```
$ sudo hcitool lescan
```
Look for an entry with MIBCS.  Be sure to use the scale while scanning so it will be awake.

You can use the ESP32 program to determine the mac address as well, just look in the source for the scan and uncomment the lines which indicate this.

Add the mac address, wifi credentials, mqtt credentials, and desired IP to the arduino program.  Build it and upload.

Next open the xiaomi_scale.py appdaemon app.
For each user of the scale, fill out this block:
```
    {
        "name": "User1",
        "height": 185,
        "age": "1901-01-01",
        "sex": "male",
        "weight_max" : 300,
        "weight_min" : 135,
        "notification_target" : "pushbullet_target_for_user1"
    }
```
The weight range will be used to determine between multiple users, so if they overlap, you'll need a different solution.
Height is in CM and age is birth date in Year-Month-Day format.
The notification target is set up for pushbullet, but you can recofigure it relatively easily.
As long as you have discovery enabled for MQTT topics, nothing needs to be added to Home Assistant except for adding sensors to any UI you want, as Appdaemon will create all the sensors automatically.
