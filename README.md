# Xiaomi Mi Scale V2 ESP32 to Home Assistant Bridge
Xiaomi Mi Body Composition Scale integration into Home Assistant using an ESP32 as a BLE to Wifi bridge with Appdaemon processing the data.
The Appdaemon app will create all sensors automatically and send notifications to each user of their stats as well as the delta from their previous reading (any weight/muscle/etc. gained/lost).

## What this fork does:
* Integrate 2 issues reported by other users: [this](https://github.com/rando-calrissian/esp32_xiaomi_mi_2_hass/issues/3) and [that](https://github.com/rando-calrissian/esp32_xiaomi_mi_2_hass/pull/2/commits/02b5ce7a416f39f3d03ec222934be112e28b3e7d).
* Slight change in the way detected devices are computed. The newer BLE lib breaks compatibility with the way the original sketch worked, see [this](https://github.com/espressif/arduino-esp32/issues/4627#issuecomment-751400018). Slight change in the `MyAdvertisedDeviceCallbacks` class and in `ScanBLE`.
* Turn on the ESP32 onboard LED for a bit after MQTT update.
* Uses new lib (whatever it changes): https://github.com/rando-calrissian/esp32_xiaomi_mi_2_hass/issues/1

## Still to do:
* I've made a quick&dirty python MQTT listener that in turn sends data to Garmin Connect.
* Adjust the `appdaemon` code with my changes.
* Add support for Garmin Connect via `appdaemon`.

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

You can use the ESP32 program to determine the mac address as well, as by default it will list the devices and mac addresses it finds over serial.

Add the mac address, wifi credentials, mqtt credentials, and desired IP to the arduino program.  It may require more space the the default setup, if you have build errors, add this to your platformio.ini:
```
board_build.partitions = min_spiffs.csv
```
Once built and uploaded to your ESP32, it will scan all the time waiting for new data over BLE, and when it finds it, it will switch onto wifi, upload the data, then switch back to scanning again after a few minutes.

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
As long as you have discovery enabled for MQTT topics, nothing needs to be added to Home Assistant except any UI you want, as Appdaemon will create all the sensors automatically.

You will find a sensor.bodymetrics_[username] with the overall stats along with individual sensors for each metric for easier graphing.

Note:. Due to the nature of the scan, it can take thirty or so seconds before it notifies the user.  It will also sleep for 5 minutes after a reading to avoid keeping the scale awake.  I'm not sure this is actually a thing, but it's mentioned elsewhere and I didn't test it.
