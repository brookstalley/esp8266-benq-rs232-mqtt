#include <ESP8266WiFi.h>
#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"
#include <ArduinoOTA.h>
#include <Regexp.h>
#include <WiFiUdp.h>
#include <OSCMessage.h>
#include <OSCBundle.h>
#include <OSCData.h>

// BEN Q SPECIFICS
#define serial_command_prepend "\r*"
#define serial_command_append "#\r"
#define serial_baud_rate 115200

// WIFI
//#define WLAN_SSID       "ssid"
//#define WLAN_PASS       "pass"
#define WLAN_SSID       "illuminati"
#define WLAN_PASS       "!!!menow"
WiFiClient client;
WiFiUDP Udp;

//COMMANDS
#define COMMAND_ROOT            "cmnd/projector/"
#define BENQ_COMMAND_POWER      "POWER"
#define BENQ_COMMAND_VOLUME     "VOLUME"
#define BENQ_COMMAND_SOURCE     "SOURCE"
#define BENQ_COMMAND_LAMP_MODE  "LAMP_MODE"
#define BENQ_COMMAND_ANY        "COMMAND"
#define BENQ_COMMAND_MUTE       "MUTE"

//MQTT SERVER
#define AIO_SERVER      "mqtt-server"
#define AIO_SERVERPORT  1883
#define AIO_USERNAME    "mqtt-user"
#define AIO_KEY         "mqtt-pass"
#define MQTT_RETRY_FREQ 60000
#define MQTT_RETRY_MAX  ULONG_MAX

unsigned long mqtt_last_retry = 0;
unsigned long mqtt_retry_count = 0;
Adafruit_MQTT_Client mqtt(&client, AIO_SERVER, AIO_SERVERPORT, AIO_USERNAME, AIO_KEY);
Adafruit_MQTT_Publish benq_status_pub = Adafruit_MQTT_Publish(&mqtt, "stat/projector/STATUS");
Adafruit_MQTT_Publish benq_any_command_pub = Adafruit_MQTT_Publish(&mqtt, "stat/projector/COMMAND");
Adafruit_MQTT_Subscribe benq_power_sub = Adafruit_MQTT_Subscribe(&mqtt, COMMAND_ROOT BENQ_COMMAND_POWER);
Adafruit_MQTT_Subscribe benq_volume_sub = Adafruit_MQTT_Subscribe(&mqtt, COMMAND_ROOT BENQ_COMMAND_VOLUME);
Adafruit_MQTT_Subscribe benq_source_sub = Adafruit_MQTT_Subscribe(&mqtt, COMMAND_ROOT BENQ_COMMAND_SOURCE);
Adafruit_MQTT_Subscribe benq_lamp_mode_sub = Adafruit_MQTT_Subscribe(&mqtt, COMMAND_ROOT BENQ_COMMAND_LAMP_MODE);
Adafruit_MQTT_Subscribe benq_any_command_sub = Adafruit_MQTT_Subscribe(&mqtt, COMMAND_ROOT BENQ_COMMAND_ANY);
Adafruit_MQTT_Subscribe benq_mute_sub = Adafruit_MQTT_Subscribe(&mqtt, COMMAND_ROOT BENQ_COMMAND_MUTE);

//OSC RECIVER
#define OSC_SERVERPORT  8001
OSCErrorCode error;

void MQTT_connect() {
  int8_t ret;

  // Stop if already connected.
  if (mqtt.connected()) {
    return;
  }
  if ((millis() - mqtt_last_retry) < MQTT_RETRY_FREQ) {
    return;
  }

  Serial.print("Connecting to MQTT... ");
  mqtt_last_retry = millis();
  if ((ret = mqtt.connect()) != 0) { // connect will return 0 for connected
       Serial.print(mqtt.connectErrorString(ret));
       Serial.println(" Will retry in ...");
       mqtt.disconnect();
       mqtt_retry_count++;
       if (mqtt_retry_count >= MQTT_RETRY_MAX) {
         // basically die and wait for WDT to reset me
         while (1);
       }
  } else {
    Serial.println("MQTT connected!");
    mqtt_retry_count = 0;
  }

}

void setup() {
  Serial.begin(115200);
  Serial1.begin(serial_baud_rate);

  // Connect to WiFi access point.
  Serial.print("Connecting to ");
  Serial.print(WLAN_SSID);
  WiFi.hostname("benq-controller");
  wifi_station_set_hostname("benq-controller");
  WiFi.begin(WLAN_SSID, WLAN_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  Udp.begin(OSC_SERVERPORT);

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_SPIFFS
      type = "filesystem";
    }

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();  

  // Setup MQTT subscriptions
  mqtt.subscribe(&benq_power_sub);
  mqtt.subscribe(&benq_volume_sub);
  mqtt.subscribe(&benq_source_sub);
  mqtt.subscribe(&benq_lamp_mode_sub);
  mqtt.subscribe(&benq_any_command_sub);
  mqtt.subscribe(&benq_mute_sub);
}

void loop() {
  ArduinoOTA.handle();
  MQTT_connect();
  Adafruit_MQTT_Subscribe *subscription;

  while ((subscription = mqtt.readSubscription(5000))) {
    if (subscription == &benq_power_sub) {
      benq_send_any_command("pow="+String((char *)benq_power_sub.lastread));
      benq_publish_status();
    }
    if (subscription == &benq_volume_sub) {
      benq_set_volume(atoi((char *)benq_volume_sub.lastread));
      benq_publish_status();
    }
    if (subscription == &benq_source_sub) {
      benq_send_any_command("sour="+String((char *)benq_source_sub.lastread));
      benq_publish_status();
    }    
    if (subscription == &benq_lamp_mode_sub) {
      benq_send_any_command("lampm="+String((char *)benq_lamp_mode_sub.lastread));
      benq_publish_status();
    }    
    if (subscription == &benq_any_command_sub) {
      benq_send_any_command((char *)benq_any_command_sub.lastread);
      benq_publish_status();
    }
    if (subscription == &benq_mute_sub) {
      benq_send_any_command("mute="+String((char *)benq_mute_sub.lastread));
      benq_publish_status();
    }        
  }
  // Process OSC commands
  OSCMessage msg;
  int size = Udp.parsePacket();

  if (size > 0) {
    while (size--) {
      msg.fill(Udp.read());
    }
    if (!msg.hasError()) {
      Serial.print("Routing OSC packet for address: ");
      char address[50];
      msg.getAddress((char *)&address, 0, 49);
      Serial.println(address);
      msg.route("/cmnd/projector", osc_command);
    } else {
      error = msg.getError();
      Serial.print("error: ");
      Serial.println(error);
    }
  }
  benq_publish_status();
}

String serial_send_command(String serial_command) {
  Serial1.print(serial_command_prepend);
  Serial1.print(serial_command);
  Serial1.print(serial_command_append);
  
  String serial_response = Serial.readString();
  return serial_response;
}

void osc_command(OSCMessage &msg, int addressOffset) {
  // We know we matched COMMAND_ROOT which includes a trailing slash
  Serial.print("Got OSC command:");
  Serial.println(msg.getAddress());
  char command[24];
  char firstParam[8] = "";
  msg.getAddress((char *)&command, addressOffset + 1, sizeof(command));
  Serial.print("Command: ");
  Serial.println(command);
  // We're almost always going to use the first param as a string, just get it once
  if (msg.size() >= 1) {
    if (msg.isString(0)) {
      msg.getString(0, (char *)&firstParam, sizeof(firstParam));
    } else if (msg.isInt(0)) {
      int32_t param = msg.getInt(0);
      itoa(param,firstParam,7);
    }
    Serial.print("First param: ");
    Serial.println(firstParam);
  }

  if (strcmp(command,BENQ_COMMAND_POWER) == 0) {
      int32_t value = msg.getInt(0);
      switch (value) {
        case 0:
          benq_send_any_command("pow=off");
          break;
        case 1:
          benq_send_any_command("pow=on");
          break;       
        default:
          Serial.print("Unknown value: ");
          Serial.println(value); 
      }
      
  }
  if (strcmp(command,BENQ_COMMAND_VOLUME) == 0)
      benq_set_volume(atoi((char *)firstParam));
  if (strcmp(command,BENQ_COMMAND_SOURCE) == 0)
      benq_send_any_command("sour="+String((char *)firstParam));
  if (strcmp(command,BENQ_COMMAND_LAMP_MODE) == 0)
      benq_send_any_command("lampm="+String((char *)firstParam));   
  if (strcmp(command,BENQ_COMMAND_ANY) == 0)
      benq_send_any_command(String((char *)firstParam));      
  if (strcmp(command,BENQ_COMMAND_MUTE) == 0)
      benq_send_any_command("mute="+String((char *)firstParam));     

  benq_publish_status();
}

String benq_send_any_command(String command) {
  Serial.print("Sending benq command: ");
  Serial.println(command);
  String response = serial_send_command(command);
  String status_response = "{\"COMMAND\":\"" + command + "\",\"RESPONSE\":\"" + response + "\"}";
  benq_any_command_pub.publish(status_response.c_str());
  return response;
}

String regex(char match_array[], char match_string[]) {
  MatchState parse_result;
  char match_result[100];
  
  parse_result.Target(match_array);
  if(parse_result.Match(match_string) == REGEXP_MATCHED) {
    parse_result.GetCapture(match_result, 0);
    return String(match_result); 
  } else {
    return "UNKNOWN";
  }
}

String benq_get_power_status() {
  char current_power_status[50];
  serial_send_command("pow=?").toCharArray(current_power_status,50);
  String result = regex(current_power_status, "POW=([^#]*)");
  if(result == "UNKNOWN") {
    return "OFF";
  } else {
    return result;
  }
}

String benq_get_source_status() {
  char current_source_status[50];
  serial_send_command("sour=?").toCharArray(current_source_status,50);
  return regex(current_source_status, "SOUR=([^#]*)");
}

int benq_get_volume_status() {
  char current_volume_status[50];
  serial_send_command("vol=?").toCharArray(current_volume_status,50);
  return (regex(current_volume_status, "VOL=([^#]*)")).toInt();
}

String benq_get_lamp_mode() {
  char current_lamp_status[50];
  serial_send_command("lampm=?").toCharArray(current_lamp_status,50);
  return regex(current_lamp_status, "LAMPM=([^#]*)");
}

int benq_get_lamp_hours() {
  char current_lamp_hours[50];
  serial_send_command("ltim=?").toCharArray(current_lamp_hours,50);
  return (regex(current_lamp_hours, "LTIM=([^#]*)")).toInt();
}

String benq_get_mute_status() {
  char current_mute_status[50];
  serial_send_command("mute=?").toCharArray(current_mute_status,50);
  return regex(current_mute_status, "MUTE=([^#]*)");
}

String benq_collect_status() {
  String current_status;
  current_status += "{";
  current_status += "\"POWER\":\"" + benq_get_power_status() + "\"";
  current_status += ",";
  current_status += "\"SOURCE\":\"" + benq_get_source_status() + "\"";
  current_status += ",";
  current_status += "\"VOLUME\":" + String(benq_get_volume_status());  
  current_status += ",";
  current_status += "\"LAMP_MODE\":\"" + String(benq_get_lamp_mode()) + "\""; 
  current_status += ",";
  current_status += "\"LAMP_HOURS\":" + String(benq_get_lamp_hours());  
  current_status += ",";
  current_status += "\"MUTE\":\"" + String(benq_get_mute_status()) + "\"";     
  current_status += "}";
  return current_status;
}

void benq_publish_status() {
  benq_status_pub.publish(benq_collect_status().c_str());
}

void benq_set_volume(int target_volume) {
  int delta = target_volume - benq_get_volume_status();
  for(int i = 0; i <= abs(delta); i++) {
    if(delta > 0) {
      benq_send_any_command("vol=+");
    } else {
      benq_send_any_command("vol=-");
    }
  }
}
