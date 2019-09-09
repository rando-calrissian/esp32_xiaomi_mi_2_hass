/*********
 * ESP32 Xiaomi Body Scale to MQTT/AppDaemon bridge
 * The ESP32 scans BLE for the scale and posts it 
 * to an MQTT topic when found.
 * AppDaemon processes the data and creates sensors
 * in Home Assistant
 * Uses the formulas and processing from
 * https://github.com/lolouk44/xiaomi_mi_scale
 * to calculate various metrics from the scale data
*********/

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ArduinoJson.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>


// Scale Mac Address
// If you don't know it, you can scan with serial debugging
// enabled and uncomment the lines to print out everything you find
// You should use the scale while this is running
String scale_mac_addr "xx:xx:xx:xx:xx:xx"


// network details
const char* ssid = "YourNetwork";
const char* password = "YourPass";

// This ESP32's IP
// Use a static IP so we shave off a bunch of time
// connecting to wifi
IPAddress ip(192, 168, 0, 100);
IPAddress gateway(192,168,0,1);
IPAddress subnet(255,255,255,0);

// MQTT Details
const char* mqtt_server = "192.168.0.101";
const int mqtt_port = 1883;
const char* mqtt_userName = "mqttuser";

const char* clientId = "esp32_";



// If you change these, you should also update the corresponding topics in the appdaemon app
String base_topic = "scale";
const char* mqtt_command = "cmnd/";
const char* mqtt_stat = "stat/";
const char* mqtt_attributes = "/attributes";
const char* mqtt_telemetry = "tele/";
const char* mqtt_tele_status = "/status";

String mqtt_clientId = String( clientId + base_topic );  //esp32_scale
String mqtt_topic_subscribe = String( mqtt_command + base_topic); //cmnd/scale
String mqtt_topic_telemetry = String( mqtt_telemetry + base_topic + mqtt_tele_status ); //tele/scale/status
String mqtt_topic_attributes = String( mqtt_stat + base_topic + mqtt_attributes ); //stat/scale/attributes

uint32_t unMillis = 1000;
uint64_t unNextTime = 0;

String publish_data;

bool bHandlingOTA = false;

WiFiClient espClient;
PubSubClient mqtt_client(espClient);

const int wdtTimeout = 1000000;  //time in ms to trigger the watchdog
hw_timer_t *timer = NULL;

uint8_t unNoImpedanceCount = 0;

void IRAM_ATTR resetModule() {
  ets_printf("reboot due to watchdog timeout\n");
  esp_restart();
}

int16_t stoi( String input, uint16_t index1 )
{
    return (int16_t)( strtol( input.substring( index1, index1+2 ).c_str(), NULL, 16) );
}

int16_t stoi2( String input, uint16_t index1 )
{
    Serial.print( "Substring : " );
    Serial.println( (input.substring( index1+2, index1+4 ) + input.substring( index1, index1+2 )).c_str() );
    return (int16_t)( strtol( (input.substring( index1+2, index1+4 ) + input.substring( index1, index1+2 )).c_str(), NULL, 16) );
}

// NOTE : To go into OTA you must sent a command over mqtt cmnd/scale {"ota":1 }
// But the scale must also connect to wifi - if you use it to send a reading,
// it will connect to wifi and proceed with OTA mode.
void ota()
{
    bHandlingOTA = true;
    timerWrite(timer, 0); //reset timer (feed watchdog)
     //Initialise OTA in case there is a software upgrade
    ArduinoOTA.setHostname("Container");
    ArduinoOTA.onStart([]() {
      Serial.println("Start");
    });
    ArduinoOTA.onEnd([]() {
      Serial.println("\nEnd");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
      timerWrite(timer, 0); //reset timer (feed watchdog)
    });
    ArduinoOTA.onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) {Serial.println("Auth Failed");}
      else if (error == OTA_BEGIN_ERROR) {Serial.println("Begin Failed");}
      else if (error == OTA_CONNECT_ERROR) {Serial.println("Connect Failed");}
      else if (error == OTA_RECEIVE_ERROR) {Serial.println("Receive Failed");}
      else if (error == OTA_END_ERROR) {Serial.println("End Failed");}
    });
      
    ArduinoOTA.begin();

    Serial.println("Waiting for any OTA updates");

    while (1) {
      Serial.print (".");
      timerWrite(timer, 0); //reset timer (feed watchdog)
      ArduinoOTA.handle();
        delay(500);
      }
}


// MQTT Callback if we need to receive stuff
void callback(char* topic, byte* payload, unsigned int length) {
  
  Serial.print("MQTT Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  // Copy payload into message buffer
  char message[length + 1];
  for (int i = 0; i < length; i++) {
    message[i] = (char)payload[i];
  }
  
  message[length] = '\0';

  if (strcmp(topic, mqtt_topic_subscribe.c_str() ) == 0) { //if the incoming message is on the command topic...
    // Parse message into JSON
    const size_t bufferSize = JSON_OBJECT_SIZE(6);
    DynamicJsonDocument jsonBuffer(bufferSize);

    auto error = deserializeJson(jsonBuffer, message);

    if (error) {
      mqtt_client.publish(mqtt_topic_telemetry.c_str(), "!root.success(): invalid JSON on ...");
      return;
    }

    JsonObject root = jsonBuffer.as<JsonObject>();

    if (root.containsKey("ota")) {
      Serial.println( "Found MQTT OTA Request - Switching into OTA Mode.");
       mqtt_client.publish(mqtt_topic_telemetry.c_str(), "Entering OTA Mode" );   
       ota();
    }

    if (root.containsKey("reset")) {
      int reset = root["reset"];
      if ( reset > 0 )
      {
        Serial.print("Resetting...");
        mqtt_client.publish(mqtt_topic_telemetry.c_str(), "Resetting by Request" );  
        delay(500);    
        esp_restart();
      }
    }
  }  
} //end callback


// wifi/MQTT needs to connect each time we get new BLE scan data 
// as wifi and ble appear to work best when mutually exclusive
void reconnect() {
  int nFailCount = 0;
  if ( WiFi.status() != WL_CONNECTED )
  {
    Serial.println();
    Serial.print("Connecting to ");
    Serial.println(ssid);

    WiFi.config(ip, gateway, subnet); 
    WiFi.begin(ssid, password);
    WiFi.config(ip, gateway, subnet); 
    
    while (WiFi.status() != WL_CONNECTED) {
      delay(10);
      Serial.print(".");
      nFailCount++;
      if ( nFailCount > 1500 )
        // Why can't we connect?  Let's not get stuck forever trying...
        // Just reboot and start fresh.
        esp_restart();
    }
    Serial.println("");
    Serial.println("WiFi connected");    
  }
  // Loop until we're reconnected
  while (!mqtt_client.connected()) 
  {
    Serial.print("Attempting MQTT connection...");

    mqtt_client.setServer(mqtt_server, mqtt_port);

    // Attempt to connect
    if (mqtt_client.connect(mqtt_clientId.c_str(),mqtt_userName,mqtt_userName))
    {
      Serial.println("connected");

     //once connected to MQTT broker, subscribe to our command topic
      bool bSubscribed = false;

      while( !bSubscribed )
      {
        bSubscribed = mqtt_client.subscribe( mqtt_topic_subscribe.c_str() );
      }

      mqtt_client.setCallback(callback);
      mqtt_client.loop();      
    } 
    else 
    {
      Serial.print("failed, rc=");
      Serial.print(mqtt_client.state());
      Serial.println(" try again in 200 milliseconds");
      delay(200);
      nFailCount++;
      if ( nFailCount > 500 )
        esp_restart();      
    }  
  }
} //end reconnect()


void publish() {
  if (!mqtt_client.connected()) 
  {
    Serial.println( "Attemping Reconnect." );
    reconnect();
  }
  
  mqtt_client.publish(mqtt_topic_attributes.c_str(), publish_data.c_str(), true );
  
  Serial.print( "Publishing : " );
  Serial.println( publish_data.c_str() );
  Serial.print( "to : " );
  Serial.println( mqtt_topic_attributes.c_str() );  

  delay( 2000 );
}



class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks
{
    void onResult(BLEAdvertisedDevice advertisedDevice)
    {

      Serial.printf("Advertised Device: %s \n", advertisedDevice.toString().c_str());
 
      if ( advertisedDevice.getAddress().toString() == scale_mac_addr )
      {        
        BLEScan *pBLEScan = BLEDevice::getScan(); // found what we want, stop now
        pBLEScan->stop();
      }      
      // We can print out all the stuff we find...
      //Serial.printf("Service Data : %s\n", advertisedDevice.getServiceData().c_str() );
      //Serial.printf("Service Data UUID : %s\n", advertisedDevice.getServiceDataUUID() );
    }
};

void ScanBLE()
{
  // Scan often unless we find a reading
  unNextTime = millis() + ( 30 * unMillis );

  Serial.println( "Starting BLE Scan" );
  if ( WiFi.status() == WL_CONNECTED )
  {
    Serial.println( "Disconnecting From MQTT." );  
    mqtt_client.disconnect();
    delay( 1000 );            
    Serial.println( "Disconnecting WiFi" );
    WiFi.disconnect();
    delay( 1000 );
  }

  BLEDevice::init("");

  BLEScan *pBLEScan = BLEDevice::getScan(); //create new scan
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true); //active scan uses more power, but get results faster
  pBLEScan->setInterval(0x50);
  pBLEScan->setWindow(0x30);

  // Scan for 30 seconds.
  BLEScanResults foundDevices = pBLEScan->start(30);

  int count = foundDevices.getCount();

  for (int i = 0; i < count; i++)
  {
    BLEAdvertisedDevice d = foundDevices.getDevice(i);

    if ( d.getAddress().toString() != scale_mac_addr )
      continue;

    String hex;
    
    if (d.haveServiceData())
    {
      std::string md = d.getServiceData();
      uint8_t* mdp = (uint8_t*)d.getServiceData().data();
      char *pHex = BLEUtils::buildHexData(nullptr, mdp, md.length());
      hex = pHex;
      Serial.println(hex);
      Serial.println( pHex );
      Serial.println( stoi2( hex, 22 ) );    
      free(pHex);
    }
    
    float  weight = stoi2( hex, 22 ) * 0.01f;
    float  impedance = stoi2( hex, 18 ) * 0.01f;

    if ( unNoImpedanceCount < 3 && impedance == 0 )
    {
      // Got a reading, but it's missing the impedance value
      // We may have gotten the scan in between the weight measurement
      // and the impedance reading or there may just not be a reading...
      // We'll try a few more times to get a good read before we publish 
      // what we've got.
      unNextTime = millis() + ( 10 * unMillis );      
      unNoImpedanceCount++;
      Serial.println( "Reading incomplete, reattempting" );
      return;
    }

    unNoImpedanceCount = 0;

    int user = stoi( hex, 6 );
    int units = stoi( hex, 0 );
    String strUnits;
    if ( units == 1 )
      strUnits = "jin";
    else if ( units == 2 )
      strUnits = "kg";
    else if ( units == 3 )
      strUnits = "lbs";      
    
    String time = String( String( stoi2( hex, 4 ) ) + " " + String( stoi( hex, 8 ) ) + " " + String( stoi( hex, 10 ) ) + " " + String( stoi( hex, 12 ) ) + " " + String( stoi( hex, 14 ) ) + " " + String( stoi( hex, 16 ) ) );

    // Currently we just send the raw values over and let appdaemon figure out the rest...
    if ( weight > 0 )
    {
      publish_data = String("{\"Weight\":\"");
      publish_data += String( weight );
      publish_data += String("\", \"Impedance\":\"");  
      publish_data += String( impedance );       
      publish_data += String("\", \"Units\":\"");  
      publish_data += String( strUnits );
      publish_data += String("\", \"User\":\"");  
      publish_data += String( user );      
      publish_data += String("\", \"Timestamp\":\"");    
      publish_data += time;                    
      publish_data += String("\"}");
      
      publish();

      // Got a reading, we can time out for a bit (5 minutes)
      unNextTime = millis() + ( 5 * 60 * unMillis );
    }
  }
  Serial.println( "Finished BLE Scan" );
}


void setup() {
  // Initializing serial port for debugging purposes
  Serial.begin(115200);

  // setup watchdog timer - shouldn't be necessary but just incase
  timer = timerBegin(0, 80, true);                  //timer 0, div 80
  timerAttachInterrupt(timer, &resetModule, true);  //attach callback
  timerAlarmWrite(timer, wdtTimeout * 1000, false); //set time in us
  timerAlarmEnable(timer);                          //enable interrupt
  timerWrite(timer, 0);                             //reset timer (feed watchdog)

  Serial.println( "Setup Finished." );
  ScanBLE();
}

// runs over and over again
void loop() 
{
  timerWrite(timer, 0); //reset timer (feed watchdog)

  uint64_t time = millis();

  // We could deepsleep, but we're scanning fairly frequently...
  if ( time > unNextTime && !bHandlingOTA )
  {
    ScanBLE();
    Serial.println( "Waiting for next Scan" );
  }

  // Shouldn't bee necessary to restart, but we'll do so every 12 hours
  // just to keep things like our timers from losing precision
  if ( time > ( 12 * 60 * 60 * unMillis ) )
  {
    Serial.println( "Doing Periodic Restart" );
    delay( 500 );
    esp_restart();
  }

  // Don't need to spin too hard...
  delay( 1000 ); 
}   
