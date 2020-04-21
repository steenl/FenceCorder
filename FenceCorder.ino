#include <FS.h>                   //this needs to be first, or it all crashes and burns...
#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
//needed for library
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>         //https://github.com/tzapu/WiFiManager
#include <Adafruit_Sensor.h>
#include "ThingSpeak.h"
#include <DHT.h>
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <HX711.h>

// Plot DTH11 data onto thingspeak.com using an ESP8266
//  added ADC, deep sleep, port to ESP-12

// replace with your channelâ€™s thingspeak API key,
//String apiKey = "IIZWTRI8E9IYTF6D";
String apiKey =   "0123456789012345";
char* sleeps = "1440"; // daily interval minute count
//const char* ssid = "abcdef";
//const char* password = "*!@";
char* host = "esp8266-webupdate";
const char* server = "api.thingspeak.com";
#define DHTPIN 2 // GPIO02 is pin weâ€™re connected to
//set ADC to measure power voltage in mV comment this out to utilize external ADC
//ADC_MODE(ADC_VCC);  //for vcc voltage sensor
DHT dht(DHTPIN, DHT11,15);
// Scale Settings
const int LOADCELL_DOUT_PIN = 4;  //D2 GPIO4
//const int LOADCELL_SCK_PIN = 0;  //D3  GPIO0 
const int LOADCELL_SCK_PIN = 12;  //D3  GPIO12
//half-bridge connected as sensor1 = red to A+, white to E-, black to E+
//   sensor2 =red to A-, white E+, black E-
HX711 scale;

 WiFiClient client;
 char thingspeakch[10];
 char thingspeakapi[40];
 char sleeptime[40];
  char OTA_enable[3];
ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;

//flag for saving data
bool shouldSaveConfig = false;

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

void setup() {
    // put your setup code here, to run once:
    Serial.begin(115200);
    delay(10);  //is this needed?
       dht.begin();

  //clean FS, for testing
  //does not clear wifimanager, but needed when json format changes
   // SPIFFS.format();

  //read configuration from FS json
  Serial.println("mounting FS...");

  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nparsed json");

          strcpy(thingspeakch, json["thingspeakch"]);
          strcpy(thingspeakapi, json["thingspeakapi"]);
          strcpy(sleeptime, json["sleeptime"]);
          strcpy(OTA_enable, json["OTA_enable"]);
        } else {
          Serial.println("failed to load json config");
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }
  //end read
  
  Serial.print("OTA state is ");
  Serial.println(OTA_enable[0]);
  if ( OTA_enable[0] == 'Y' ) {
    Serial.println("Enabling OTA server");
     pinMode(LED_BUILTIN, OUTPUT);     // Initialize the LED_BUILTIN pin as an output
   digitalWrite(LED_BUILTIN, LOW);  //drive blue LED on module on.
    WiFi.mode(WIFI_AP_STA);
    Serial.println(WiFi.SSID());
    Serial.println(WiFi.psk());
    Serial.println(WiFi.localIP());
 //   WiFi.begin(ssid, password);  This duplicate causes issues!
    while(WiFi.waitForConnectResult() != WL_CONNECTED){
      delay(1000);
      Serial.println("WiFi failed, retrying.");
    }
  MDNS.begin(host);
  httpUpdater.setup(&httpServer);
  httpServer.begin();
  MDNS.addService("http", "tcp", 80);
  Serial.printf("HTTPUpdateServer ready! Open http://%s.local/update in your browser\n", host);
  Serial.printf("remember to have bonjour services installed on the downloader system\n");
  //Now both ESP12f and laptop are connected to a router as clients.  
  //if no bonjour, find the IP address based on router DHCP.  Also wifimanager can show ESP12F MAC address to match
  // then navigate to w.x.y.z/update to get the download prompt.  After download completes, need to reset ESP12F.
  Serial.printf("sequence: Set OTA:reset:With blueLED, ESP12F ready for upload: client connects and pushes .bin:reconfig wifimanager\n");
  Serial.printf("After download completes, reset occurs, but wifimanager settings remain, so need to reconfigure wifimanager as needed\n");
  SPIFFS.format();  //clear out OTA enabled setting
   
  } else {  //OTA not enabled
  // id/name, placeholder/prompt, default, length
       WiFiManagerParameter custom_thingspeakch("Channel", "thingspeak channel", thingspeakch, 10);
       WiFiManagerParameter custom_thingspeakapi("APIcode", "thingspeakAPI", thingspeakapi, 40);
       WiFiManagerParameter custom_sleeptime("Sleeptime", "sleeptime", sleeptime, 40);     
       WiFiManagerParameter custom_OTA_enable("OTA_enable", "Y_for_OTA", "OTA_enable", 3);            
    //WiFiManager
    //Local intialization. Once its business is done, there is no need to keep it around
    WiFiManager wifiManager;
    //uncomment below to reset saved settings
   // wifiManager.resetSettings();
  //sets timeout until configuration portal gets turned off
  //useful to make it all retry or go to sleep
  //in seconds
  Serial.println("setting timeout to 180sec");
  wifiManager.setTimeout(180);
    //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);
//add all your parameters here
    wifiManager.addParameter(&custom_thingspeakch);  
    wifiManager.addParameter(&custom_thingspeakapi);  
    wifiManager.addParameter(&custom_sleeptime);  
    wifiManager.addParameter(&custom_OTA_enable);  
    //set custom ip for portal
    //wifiManager.setAPConfig(IPAddress(10,0,1,1), IPAddress(10,0,1,1), IPAddress(255,255,255,0));

    //fetches ssid and pass from eeprom and tries to connect
    //if it does not connect it starts an access point with the specified name
    //here  "AutoConnectAP"
    //and goes into a blocking loop awaiting configuration
    //wifiManager.autoConnect("AutoConnectAP");
     // wifiManager.autoConnect("DaCorder_1217");
  if(!wifiManager.autoConnect("DaCorder_03a19")) {
    Serial.println("failed to connect and hit timeout");
    delay(300);
    //reset and try again, or maybe put it to deep sleep
    Serial.println("starting ESP 1day deepsleep");
    //ESP.reset();
    //86400 seconds = 1day
    ESP.deepSleep(8640000000,WAKE_RF_DEFAULT);
     //if you get here you have connected to the WiFi
    Serial.println("connected...yeey :)");
    }
    Serial.println("Starting DaCorder version March19");
   //read updated parameters
  strcpy(thingspeakch, custom_thingspeakch.getValue());
  strcpy(thingspeakapi, custom_thingspeakapi.getValue());
  strcpy(sleeptime, custom_sleeptime.getValue());
  strcpy(OTA_enable, custom_OTA_enable.getValue());
 //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["thingspeakch"] = thingspeakch;
    json["thingspeakapi"] = thingspeakapi;
    json["sleeptime"] = sleeptime;
    json["OTA_enable"] = OTA_enable;
    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
    //end save
    }
  Serial.println("local ip");
  Serial.println(WiFi.localIP());
  } //OTA else

   ThingSpeak.begin(client);  // Initialize ThingSpeak

//  Serial.println("starting scale setup \t\t");
//scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
// Serial.println(scale.read_average(20));   // print the average of 20 readings from the ADC

  // scale.set_scale();// <- set here calibration factor!!!
//   scale.set_scale(46000 / 13.6); // calib from November for KG reading
 //  scale.set_scale(2280.f);  // from example for pounds reading
 //  scale.tare();
//
//  Serial.print("\t| average:\t");
//  Serial.println(scale.get_units(10), 1);
// if (scale.is_ready()) {
 //   long reading = scale.read();
  //  Serial.print("HX711 reading: ");
  //  Serial.println(reading);
 // } else {
 //   Serial.println("HX711 not found.");
 // }
 // Serial.print("read average: \t\t");
 // Serial.println(scale.read_average(20));       // print the average of 20 readings from the ADC
 //Serial.print("get value: \t\t");
 // Serial.println(scale.get_value(5));    // print the average of 5 readings from the ADC minus the tare weight, set with tare()

  //Serial.print("get units: \t\t");
 // Serial.println(scale.get_units(5), 1);        // print the average of 5 readings from the ADC minus tare weight, divided
            // by the SCALE parameter set with set_scale

}

void loop() {
    // put your main code here, to run repeatedly:
// Over the air updates support verified Feb'19.  If Bonjour approach does not work, then use router DHCP
  if (OTA_enable[0] == 'Y') { 
      httpServer.handleClient();
  } else {
float v;
float vmax=1.0;
float vmin=1023;
float h = 50.0; // dht.readHumidity();
float t = 10.0; // dht.readTemperature();
//float v = 87.65;
//float v = ESP.getVcc();
       Serial.print("time start in ms: ");
       Serial.println(millis());

 //the photo-resistor is in series with 10Kohm to ground at 4.2V.
 // The ADC signal is at the 10Kohm
// for (int i = 0; i < (20000); i++) { // about 1ms per sample
 for (int i = 0; i < (40000); i++) { // about 1ms per sample
     v = analogRead(A0) ; 
    // v = v *(3.3/1023.0);
    //Serial.println(v);
    if (vmax < v) {vmax = v;} 
    if (vmin > v) {vmin = v;} 
    //feed watchdog based on 
    //https://techtutorialsx.com/2017/01/21/esp8266-watchdog-functions/
    ESP.wdtFeed();
    //delayMicroseconds(50);
      }
Serial.print("Max recorded voltage is ");
Serial.println(String(vmax));
Serial.print("Min recorded voltage is ");
Serial.println(String(vmin));
   Serial.print("timeend in ms: ");
   Serial.println(millis());
  
//Propably better way, but char swap
apiKey[0] = thingspeakapi[0];
apiKey[1] = thingspeakapi[1];
apiKey[2] = thingspeakapi[2];
apiKey[3] = thingspeakapi[3];
apiKey[4] = thingspeakapi[4];
apiKey[5] = thingspeakapi[5];
apiKey[6] = thingspeakapi[6];
apiKey[7] = thingspeakapi[7];
apiKey[8] = thingspeakapi[8];
apiKey[9] = thingspeakapi[9];
apiKey[10] = thingspeakapi[10];
apiKey[11] = thingspeakapi[11];
apiKey[12] = thingspeakapi[12];
apiKey[13] = thingspeakapi[13];
apiKey[14] = thingspeakapi[14];
apiKey[15] = thingspeakapi[15];
sleeps[0] = sleeptime[0];
sleeps[1] = sleeptime[1];
sleeps[2] = sleeptime[2];
sleeps[3] = sleeptime[3];
sleeps[4] = sleeptime[4];
sleeps[5] = sleeptime[5];
sleeps[6] = sleeptime[6];
   ThingSpeak.setField(1, vmax);
   ThingSpeak.setField(2, vmin);
  // ThingSpeak.setField(3, t);
  // ThingSpeak.setField(4, h);
  // ThingSpeak.setField(5, v);
   
  // write to the ThingSpeak channel
 int x = ThingSpeak.writeFields(int(thingspeakch), thingspeakapi);
  if(x == 200){
    Serial.println("Channel update successful.");
   }
  else{
    Serial.println("Problem updating channel. HTTP error code " + String(x));
  }
//client.stop();
int sleepusec = atoi(sleeps);
Serial.print("minutes to sleep : ");
Serial.println (sleepusec);
sleepusec = sleepusec*60;
Serial.print ("sleeptime in sec is ");
Serial.println (sleepusec);
Serial.println("Entering sleepâ€¦");
// thingspeak needs minimum 15 sec delay between updates
//delay(20000);
//ESP.deepSleep(180000000,WAKE_RF_DEFAULT);
  ESP.deepSleep(sleepusec*1000*1000,WAKE_RF_DEFAULT);
     } // OTA else
}
