/*
 * File: wizfi360_mqtt_client.ino
 * 
 * Purpose: 
 * Entry for WIZnet WizFi360 Design Contest 2022.
 * Time-of-Flight distance sensor over MQTT for home automation system.
 *
 * Author: Clemens Valens
 * Date: 10/14/2022
 * 
 * Board: WizFi360-EVB-Pico
 * Arduino IDE: 1.8.19
 *
 * Boards Package:
 * - Raspberry Pi Pico/RP2040
 *   Earle F. Philhower, III version 2.6.0
 *
 * Libraries:
 * - DFRobot_VL53L0X, version 1.0.0
 *
 * License: BSD-3-Clause
 */
 
#include "Wire.h"
#include "DFRobot_VL53L0X.h"
#include "secret.h" // Put credentials in this file and don't share it.

DFRobot_VL53L0X sensor;

// Raspberry Pi Pico I2C0 pins.
const int scl0_pin = 9;
const int sda0_pin = 8;
const int vin_pin = 10;

const int led_pin = LED_BUILTIN;  // Green LED on GPIO25

#define wizfi360  Serial2

char ssid[] = MY_SECRET_SSID;  // your network SSID (name)
char pass[] = MY_SECRET_PASSPHRASE;  // your network password
char mqtt_user[] = MY_SECRET_MQTT_USER;
char mqtt_pwd[] = MY_SECRET_MQTT_PASSWORD; 
char mqtt_client_id[] = "vl53l0"; // Set to whatever you like as long as it is unique.
char mqtt_broker_ip[] = "192.168.2.36"; // Unfortunately, WifFi360 does not support mDNS.
int mqtt_port = 1883; // 1883 is MQTT default port.
char mqtt_topic[] = "vl53l0";
char mqtt_subtopic[] = "sub";

// Replies from WizFi360 module can be quite long so make sure there is enough space.
#define WIZFI_BUFFER_SIZE  (1000)
char wizfi_buffer[WIZFI_BUFFER_SIZE];

void led_flash(int count, int rate)
{
  for (int i=0; i<count; i++)
  {
    // Flash LED count times.
    delay(rate/2);
    digitalWrite(led_pin,HIGH);
    delay(rate);
    digitalWrite(led_pin,LOW);
    delay(rate/2);
  }
}

void fatal_error(int error)
{
  while (1)
  {
    // Flash LED error times.
    led_flash(error,50);
    delay(1000);
  }
}

bool get_reply(int timeout)
{
  int i = 0;  
  uint32_t t_start = millis();
  while (millis()<t_start+timeout)
  {
    if (wizfi360.available())
    {
      char ch = (char) wizfi360.read();
      wizfi_buffer[i] = ch;
	  i += 1;
	  if (i>=WIZFI_BUFFER_SIZE)
	  {
        wizfi_buffer[i-1] = 0;
        Serial.println("- [get_reply] buffer overflow");
        return false;
	  }
      Serial.print(ch); // echo
    }
  }
  wizfi_buffer[i] = 0;

  if (wizfi_buffer[i-4]=='O' && wizfi_buffer[i-3]=='K') return true;
  return false;
}

bool wifi_connect(void)
{
  // Set station mode.
  wizfi360.println("AT+CWMODE_CUR=1");
  if (get_reply(100)==false)  // timeout in ms.
  {
    Serial.println("- [wifi_connect] failed to set station mode");
    return false;
  }
  
  // Set station mode with DHCP.
  wizfi360.println("AT+CWDHCP_CUR=1,1");
  if (get_reply(100)==false)  // timeout in ms.
  {
    Serial.println("- [wifi_connect] failed to enable DHCP");
    return false;
  }
  
  // Set Wi-Fi credentials.
  sprintf(wizfi_buffer,"AT+CWJAP_CUR=\"%s\",\"%s\"",ssid,pass);
  wizfi360.println(wizfi_buffer);
  // Allow enough time for a slow connect.
  if (get_reply(10000)==false)  // timeout in ms.
  {
    Serial.println("- [wifi_connect] failed to connect to network");
    return false;
  }
  
  return true;
}

bool mqtt_connect(char *mqtt_user, char *mqtt_pwd, char *mqtt_client_id)
{
  // Set credentials, 60 is keep-alive time in seconds.
  sprintf(wizfi_buffer,"AT+MQTTSET=\"%s\",\"%s\",\"%s\",60",mqtt_user,mqtt_pwd,mqtt_client_id);
  wizfi360.println(wizfi_buffer);
  if (get_reply(100)==false)  // timeout in ms.
  {
    Serial.println("- [mqtt_connect] failed to set credentials");
    return false;
  }

  // Set topic.
  sprintf(wizfi_buffer,"AT+MQTTTOPIC=\"%s\",\"%s\"",mqtt_topic,mqtt_subtopic);
  wizfi360.println(wizfi_buffer);
  if (get_reply(100)==false)  // timeout in ms.
  {
    Serial.println("- [mqtt_connect] failed to set topic");
    return false;
  }

  // Connect to broker with authentication.
  sprintf(wizfi_buffer,"AT+MQTTCON=0,\"%s\",%d",mqtt_broker_ip,mqtt_port);
  Serial.println(wizfi_buffer);
  wizfi360.println(wizfi_buffer);
  if (get_reply(1000)==false)  // timeout in ms.
  {
    Serial.println("- [mqtt_connect] failed to connect");
    return false;
  }

  return true;
}

bool mqtt_publish_int(int value)
{
  sprintf(wizfi_buffer,"AT+MQTTPUB=\"%d\"",value);
  wizfi360.println(wizfi_buffer);
  if (get_reply(100)==false)  // timeout in ms.
  {
    Serial.println("- [mqtt_publish_int] failed to publish");
    return false;
  }
  led_flash(1,100); // Show successfull publication.
  return true;
}

void vl53l0x_init(void)
{
  // Power on VL53L0X board.
  pinMode(vin_pin,OUTPUT);
  digitalWrite(vin_pin,HIGH);
  
  // Configure I2C0 for use with Pico.
  Wire.setSDA(sda0_pin);
  Wire.setSCL(scl0_pin);
  
  // Join i2c bus.
  Wire.begin();
  sensor.begin();
  // Set to back-to-back and high precision modes.
  sensor.setMode(sensor.eContinuous,sensor.eHigh);
  sensor.start();
}

void setup(void)
{
  pinMode(led_pin,OUTPUT);
  digitalWrite(led_pin,HIGH);
  
  // Initialize serial port for debugging.
  Serial.begin(115200);
  //while (!Serial); // for debug only.
  digitalWrite(led_pin,LOW);

  // Initialize distance/TOF sensor.
  vl53l0x_init();
  
  // Initialize serial port for WizFi360 module.
  wizfi360.begin(115200);

  // Wake up WizFi360 module.
  wizfi360.println("AT");
  if (get_reply(500)==false)  // timeout in ms.
  {
    Serial.println("WizFi360 module does not respond.");
    fatal_error(3);
  }

  // Connect to network.
  if (wifi_connect()==false)
  {
    Serial.println("Could not connect to network.");
    fatal_error(4);
  }
  
  // Connect to MQTT broker.
  if (mqtt_connect(mqtt_user,mqtt_pwd,mqtt_client_id)==false)
  {
    Serial.println("Could not connect to MQTT broker.");
    fatal_error(5);
  }

  led_flash(2,100); // Setup done.
}

void loop(void)
{
  float distance = sensor.getDistance();
  
  // Publish value.
  if (mqtt_publish_int((int)distance)==false)
  {
    Serial.println("failed to publish");
  }
  
  if (distance>70)
  {
    led_flash(3,50); // Distance crossed threshold.
  }

  delay(5000); // Limit publish rate.
}
