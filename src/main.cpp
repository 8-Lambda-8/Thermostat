#include <Arduino.h>
#include <ArduinoOTA.h>
//#include <DHTesp.h>  // Click here to get the library: http://librarymanager/All#DHTesp
#include <SimpleDHT.h>

#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <PubSubClient.h>
#include <WiFiManager.h>

#include "select.h"

// Display : HTE029A1_V33
// Chip: IL3820

#define PIN_DHT22 10
#define PIN_Relay 0

const String host = "Thermostat";
const char* version = __DATE__ " / " __TIME__;
const char* mqtt_server = "MQTT";

const String TopicStatus = "/" + host + "/Status";
const String TopicVersion = "/" + host + "/Version";
const String TopicTemp = "/" + host + "/Temp/";

String imageUrl = "";

//DHTesp dht;
SimpleDHT22 dht22(PIN_DHT22);


float targetTemp = 20, H = 0.25f;  // From MQTT
float sensorTemp, sensorHum;       // From Sensor
bool heaterState = false;

long mill, mqttConnectMillis, updateMillis;
int sleepSeconds = 60;
WiFiClient httpClient;
WiFiClient espClient;
PubSubClient client(espClient);

HTTPClient http;

char* str2ch(String str) {
  if (str.length() != 0) {
    char* p = const_cast<char*>(str.c_str());
    return p;
  }
  return const_cast<char*>("");
}

unsigned char* str2uch(String str) {
  if (str.length() != 0) {
    char* p = const_cast<char*>(str.c_str());
    return (unsigned char*)p;
  }
  return (unsigned char*)const_cast<char*>("");
}

void updateDisplay(){
  
    http.begin(httpClient, imageUrl);
    int httpCode = http.GET();
    Serial.printf("Http Code: %d \n", httpCode);
    Serial.println(http.getSize());

    Serial.println(http.getString().length());
    Serial.println(http.getString().charAt(0));
    Serial.print("FF:");
    Serial.println(http.getString().charAt(0) == 0xFF);
    //Display_clear();
    Display_picture(str2uch(http.getString()));
    http.end();
}

void callback(char* topic, byte* payload, unsigned int length) {
  String msg = "";
  #if debug
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (uint16_t i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
    msg += (char)payload[i];
  }
  Serial.println();
  #else
  for (uint16_t i = 0; i < length; i++) { 
    msg += (char)payload[i];
  }
  #endif

  String topicStr = String(topic);

  topicStr.replace("/" + host + "/", "");

  if (topicStr == "imageUrl") {
    imageUrl = msg;
  } else if (topicStr == "updateDisplay") {
    if(imageUrl!=""&&msg=="1"){
      updateDisplay();
      client.publish(str2ch("/"+host+"/updateDisplay"),0,false);
    }
  } else if (topicStr == "Temp/targetTemp") {
    targetTemp = msg.toFloat();
    Serial.print("Target temp is: ");
    Serial.println(targetTemp);
  } else if (topicStr == "Temp/H") {
    H = msg.toFloat();
    Serial.print("hysteresis is: ");
    Serial.println(H);
  }
  #if debug
  Serial.println("endCallback");
  #endif
}

void reconnect() {
  Serial.println();

  Serial.print("Attempting MQTT connection...");
  // Create a random client ID
  String clientId = host + "-";
  clientId += String(random(0xffff), HEX);
  // Attempt to connect
  if (client.connect(clientId.c_str(), "test1", "test1", str2ch(TopicStatus), 0,
                     true, "OFFLINE")) {
    Serial.println("connected");

    client.publish(str2ch(TopicVersion), version, true);
    client.publish(str2ch(TopicStatus), "ONLINE", true);
    Serial.print("Sub to= ");
    Serial.println("/" + host + "/#");
    client.subscribe(str2ch("/" + host + "/#"));
  } else {
    Serial.print("failed, rc=");
    Serial.print(client.state());
    Serial.println(" try again in 5 seconds");
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println();

  pinMode(PIN_Relay, OUTPUT);

  epd.Init(lut_full_update);
  Serial.println("after Epaper init");

  Serial.println(version);

  ESP.wdtDisable();
  ESP.wdtEnable(5000);

  WiFiManager wifiManager;
  WiFi.hostname(host);
  wifiManager.setAPStaticIPConfig(IPAddress(10, 0, 0, 1),
                                  IPAddress(10, 0, 0, 1),
                                  IPAddress(255, 255, 255, 0));
  wifiManager.autoConnect(str2ch(host));
  Serial.println("after wifi Auto Connect");

  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  ArduinoOTA.setHostname(str2ch(host));
  ArduinoOTA.begin();

  MDNS.begin(host);
  MDNS.addService("http", "tcp", 80);

  //dht.setup(PIN_DHT22, DHTesp::DHT22);

  Serial.println();
  Serial.println();
}

long loops = 0;

String getDHTError(int err){
  switch (SimpleDHTErrCode(err))
  {
  case 0:
    return "Success";
    break;
  case 16:
    return "StartLow";
    break;
  case 17:
    return "StartHigh";
    break;
  case 18:
    return "DataLow";
    break;
  case 19:
    return "DataRead";
    break;
  case 20:
    return "DataEOF";
    break;
  case 21:
    return "DataChecksum";
    break;
  case 22:
    return "ZeroSamples";
    break;
  case 23:
    return "NoPin";
    break;
  case 24:
    return "PinMode";
    break;
  default:
    return "unknown Error";
    break;
  }
}

void loop() {
  ArduinoOTA.handle();

  if ((millis() - mqttConnectMillis) > 10000 && !client.connected()) {
    reconnect();
    mqttConnectMillis = millis();
  }
  client.loop();

  //Sensor / Heating stuff
  if ((millis() - mill) > 5000) {
    // Serial.println("5s");
    loops = 0;

    mill = millis();
    Serial.println();
    
    char buf[16];

    int err = SimpleDHTErrSuccess;
    if ((err = dht22.read2(&sensorTemp, &sensorHum, NULL)) != SimpleDHTErrSuccess) {
      Serial.print("Read DHT22 failed, err=");
      Serial.println(err);
      client.publish(str2ch(TopicTemp + "err"),str2ch(getDHTError(err)),true);
      sensorTemp = NAN;
      sensorHum = NAN;
    }

    /* sensorHum = dht.getHumidity();
    sensorTemp = dht.getTemperature();

    Serial.print(dht.getStatusString());
    client.publish(str2ch(TopicTemp + "err"),dht.getStatusString(),true); */
    
    Serial.print(sensorTemp, 2);
    Serial.print("\t");
    Serial.println(sensorHum, 2);

    sprintf(buf, "%.2f", sensorTemp);
    client.publish(str2ch(TopicTemp + "sensorTemp"), buf, true);
    sprintf(buf, "%.2f", sensorHum);
    client.publish(str2ch(TopicTemp + "sensorHum"), buf, true);

    if (sensorTemp > (targetTemp + H) || sensorTemp < 1) {
      heaterState = false;  // Aus

    } else if (sensorTemp < (targetTemp - H)) {
      heaterState = true;  // Ein
    }

    if (heaterState)
      client.publish(str2ch(TopicTemp + "heaterState"), "1", true);
    else
      client.publish(str2ch(TopicTemp + "heaterState"), "0", true);

    digitalWrite(PIN_Relay,
                 !heaterState);  // invert because Relay is on when on GND
  }

  loops++;
}