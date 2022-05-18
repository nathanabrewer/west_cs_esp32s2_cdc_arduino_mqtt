/**
 * Simple CDC device connect with putty to use it
 * author: chegewara
 * Serial - used only for logging
 * Serial1 - can be used to control GPS or any other device, may be replaced with Serial
 *  
  Product ID: 0x1a06
  Vendor ID:  0x1eab
  Version:  0.01
  Serial Number:  FM3080-20-BL00212
  Speed:  Up to 12 Mb/s
  Manufacturer: Newland Auto-ID
  Location ID:  0x02100000 / 1
  Current Available (mA): 500
  Current Required (mA):  500
  Extra Operating Current (mA): 0

 */
#include "cdcusb.h"
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include "cert.h";


CDCusb USBSerial;

WiFiClientSecure espClient;
PubSubClient client(espClient);


String outputTopic = "";
String inputTopic= ""; 
String infoTopic="";
String macAddress ="";


class MyUSBCallbacks : public CDCCallbacks {
    void onCodingChange(cdc_line_coding_t const* p_line_coding)
    {
        int bitrate = USBSerial.getBitrate();
        Serial.printf("new bitrate: %d\n", bitrate);
    }

    bool onConnect(bool dtr, bool rts)
    {
        Serial.printf("connection state changed, dtr: %d, rts: %d\n", dtr, rts);
        return true;  // allow to persist reset, when Arduino IDE is trying to enter bootloader mode
    }

    void onData()
    {
        int len = USBSerial.available();
        //Serial.printf("\nnew data, len %d\n", len);
        uint8_t buf[len] = {};
        USBSerial.read(buf, len);
        Serial.write(buf, len);
    }

    void onWantedChar(char c)
    {
        Serial.printf("wanted char: %c\n", c);
    }
};


void setup()
{
  Serial.begin(115200);
  Serial1.begin(115200);
  macAddress = String(WiFi.macAddress());
  outputTopic = "west-cs/"+macAddress+"/out";
  inputTopic = "west-cs/"+macAddress+"/in";
  infoTopic = "west-cs/"+macAddress+"/info";
 char* SerialNumber = "FM3080-20-BL00212";
 char* Manufacturer = "Newland Auto-ID";
 char* Product = "NLS-FM3080-20 USB CDC";

  startUsbSerialHost(SerialNumber, Manufacturer, Product, 1, 0x1eab, 0x1a06);
  // connecting to a WiFi network
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.println("Connecting to WiFi..");
  }

  espClient.setCACert(cert_ca);
  espClient.setCertificate(cert_crt); // for client verification
  espClient.setPrivateKey(cert_key);  // for client verification
  
  client.setServer(mqtt_broker, mqtt_port);
  client.setCallback(callback);
  while (!client.connected()) {
      String client_id = "esp32-client-";
      client_id += String(WiFi.macAddress());
      Serial.printf("The client %s connects to the public mqtt broker\n", client_id.c_str());
      if (client.connect(client_id.c_str())) {
          Serial.println("CONNECTED!");
      } else {
          Serial.print("failed with state ");
          Serial.print(client.state());
          delay(2000);
      }
  } 

  infoAlert("ONLINE");
  client.subscribe(inputTopic.c_str());  
  client.subscribe(outputTopic.c_str());   
}

void infoAlert(String d){
  client.publish(infoTopic.c_str(), d.c_str());
}

void startUsbSerialHost(char* SerialNumber, char* Manufacturer, char* Product, uint8_t revision, uint16_t vid, uint16_t pid){
   USBSerial.setCallbacks(new MyUSBCallbacks());
   //USBSerial.setWantedChar('x');
   USBSerial.manufacturer(Manufacturer);
   USBSerial.product(Product); // product name
   USBSerial.serial(SerialNumber);  // serial number SN
   USBSerial.revision(revision); // product revison
   USBSerial.deviceID(vid, pid);
    if (!USBSerial.begin())
        Serial.println("Failed to start CDC USB stack");
}

void callback(char *topic, byte *payload, unsigned int length) {
  
  if (strcmp(topic, inputTopic.c_str()) == 0){
    Serial.print("INPUT TOPIC CONTENT:");
    for (int i = 0; i < length; i++) {
        Serial.print((char) payload[i]);
    }
    Serial.println();
  }
  
  if (strcmp(topic, outputTopic.c_str()) == 0){
    Serial.print("OUTPUT TOPIC CONTENT:");
    for (int i = 0; i < length; i++) {
        Serial.print((char) payload[i]);
    }
    Serial.println();
    int a = USBSerial.write(payload, length);
    const uint8_t feed[2] = {0x0D, 0x0A};
    USBSerial.write(feed, 2);    
  }

}

void loop()
{
  relaySerial1ToUsb();
  client.loop();
}

void relaySerialToUsb(){
    while (Serial.available())
    {
        int len = Serial.available();
        char buf1[len];
        Serial.read(buf1, len);
        int a = USBSerial.write((uint8_t*)buf1, len);
        Serial1.write((uint8_t*)buf1, len);
    }
}
void relaySerial1ToUsb(){
    while(Serial1.available())
    {
        int len = Serial1.available();
        char buf1[len];
        Serial1.read(buf1, len);
        int a = USBSerial.write((uint8_t*)buf1, len);
        //Serial.write((uint8_t*)buf1, len);
    }
}
