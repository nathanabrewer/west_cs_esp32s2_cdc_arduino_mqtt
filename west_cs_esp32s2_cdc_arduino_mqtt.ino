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

#include "cert.h"
#include <ArduinoJson.h>
#include "ArduinoNvs.h"
#include <esp_task_wdt.h>
#define WDT_TIMEOUT 10

CDCusb USBSerial;

#define USB_MODE_NIL 0
#define USB_MODE_ACM 1
#define USB_MODE_HID 2

WiFiClientSecure espClient;
PubSubClient client(espClient);

   // USBSerial.serial(SerialNumber);
 char buffera[30];
 char bufferb[30];
 char bufferc[30];
   
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

void setUsbDefaults(){
    Serial.println("Setting USB Defaults");
    NVS.setInt("USB_MODE", USB_MODE_ACM);
    NVS.setInt("USB_REV", 1);
    NVS.setString("USB_SERL", "FM3080-20-BL00212");
    NVS.setString("USB_MANF", "Newland Auto-ID");
    NVS.setString("USB_PROD", "NLS-FM3080-20 USB CDC");
    NVS.setInt("USB_VID", 0x1eab);
    NVS.setInt("USB_PID", 0x1a06);
}

void setUsbConfig(){
    int saveCounter = NVS.getInt("CONFIG_SAVE_COUNTER"); 
    saveCounter++;
    NVS.setInt("CONFIG_SAVE_COUNTER", saveCounter);
    ESP.restart();
}



void setup()
{

  Serial.begin(115200);
  Serial1.begin(115200);

  esp_task_wdt_init(WDT_TIMEOUT, true); //enable panic so ESP32 restarts
  esp_task_wdt_add(NULL); //a
  
  Serial.println("Read NVS");
  NVS.begin("BrewerSystems");  //https://github.com/rpolitex/ArduinoNvs
  int usbMode = NVS.getInt("USB_MODE"); 

  Serial.print("USB_MODE: ");
  Serial.println(usbMode);

  if(usbMode == USB_MODE_NIL){   
    //apply Defaults
    setUsbDefaults();
    usbMode = USB_MODE_ACM;
  }

  Serial.println("NVS Ready");
  if(usbMode == USB_MODE_ACM){
    startUsbSerialHost();
  }
  if(usbMode == USB_MODE_HID){
    Serial.println("USB HID NOT IMPLEMENTED YET.");
  }
  

  macAddress = String(WiFi.macAddress());
  outputTopic = "west-cs/"+macAddress+"/out";
  inputTopic = "west-cs/"+macAddress+"/in";
  infoTopic = "west-cs/"+macAddress+"/info";

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
  client.setBufferSize(1024);
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

void publishStatus(){
  Serial.println("publishStatus");
  
  int counter = NVS.getInt("CONFIG_COUNTER"); 
  counter++;
  NVS.setInt("CONFIG_COUNTER", counter);
  
  StaticJsonDocument<512> statusDocument;
  
  JsonObject obj = statusDocument.createNestedObject("stats");
  obj["stat_req_counter"] = counter;
  obj["save_counter"] =  NVS.getInt("CONFIG_SAVE_COUNTER");

  JsonObject config = statusDocument.createNestedObject("config");

  config["vid"] = NVS.getInt("USB_VID");
  config["pid"] = NVS.getInt("USB_PID");  
  config["serial_number"] = NVS.getString("USB_SERL");
  config["manufacturer_name"] = NVS.getString("USB_MANF");
  config["product_name"] = NVS.getString("USB_PROD");
  config["usb_mode"] = (NVS.getInt("USB_MODE") == 1) ? "ACM" : "HID";
  config["rev_num"] = NVS.getInt("USB_REV");
   
  

  
  char buffer[256];
  serializeJson(statusDocument,buffer);
  Serial.print("Buffer:");
  Serial.println(buffer);
  
  client.publish(infoTopic.c_str(), buffer);
  
}

void startUsbSerialHost(){
  Serial.println("Starting USB_MODE_ACM");
  String usbSerl = NVS.getString("USB_SERL");
  String usbManf = NVS.getString("USB_MANF");
  String usbProd = NVS.getString("USB_PROD");
  uint16_t vid = NVS.getInt("USB_VID");
  uint16_t pid = NVS.getInt("USB_PID");
  uint8_t revision = NVS.getInt("USB_REV");

  char* SerialNumber = "FM3080-20-BL00212"; //strstr(usbSerl.c_str(), "]" );
  char* Manufacturer = "Newland Auto-ID"; //strstr(usbManf.c_str(), "]" );
  char* Product      = "NLS-FM3080-20 USB CDC"; //strstr(usbProd.c_str(), "]" );
  
  //char* SerialNumber = (char*)usbSerl.c_str();
  //char* Manufacturer = (char*) usbManf.c_str();
  //char* Product      = (char*) usbProd.c_str();
  
  
   USBSerial.setCallbacks(new MyUSBCallbacks());
   //USBSerial.setWantedChar('x');
   
   // USBSerial.manufacturer(Manufacturer);
   // USBSerial.product(Product);
;
   strlcpy(buffera, usbManf.c_str(), usbManf.length()+1);
   buffera[usbManf.length()+1] ='\0';
   USBSerial.manufacturer(buffera);
   Serial.print("Set Manufacturer: ");
   Serial.println(buffera);

   strlcpy(bufferb, usbProd.c_str(), usbProd.length()+1);
   bufferb[usbProd.length()+1] ='\0';
   USBSerial.product(bufferb); // product name
   Serial.print("Set Product: ");
   Serial.println(bufferb);
   
   strlcpy(bufferc, usbSerl.c_str(), usbSerl.length()+1);
   bufferc[usbSerl.length()+1] ='\0';
   USBSerial.serial(bufferc);  // serial number SN
   Serial.print("Set SerialNumber: ");
   Serial.println(bufferc);   
   
   USBSerial.revision(revision); // product revison
   USBSerial.deviceID(vid, pid);
   
    if (!USBSerial.begin())
        Serial.println("Failed to start CDC USB stack");
}

void callback(char *topic, byte *payload, unsigned int length) {
  
  StaticJsonDocument<1024> doc;
  DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    return;
  }
  if (doc.containsKey("code")) {
    String code = String( doc["code"].as<char*>() );

    Serial.println(code);
    USBSerial.write((byte*) code.c_str(), code.length());
    Serial.printf("USBSerial wrote %d bytes. (json['code'])");
    Serial.println("");
    
    
    const uint8_t feed[2] = {0x0D, 0x0A};
    USBSerial.write(feed, 2);  
    Serial.println("USBSerial wrote 2 bytes. (appending LF CR)");
    //infoAlert how many bytes i wrote? 
  }
  
  if (doc.containsKey("config")) {
    if(doc["config"].containsKey("reset")){
      setUsbDefaults();
    }
    if(doc["config"].containsKey("pid")){
      uint16_t pid = doc["config"]["pid"];
      NVS.setInt("USB_PID", pid);
      Serial.print("SETTING USB_PID: ");
      Serial.println(pid, HEX);
    }
    
    if(doc["config"].containsKey("vid")){
      uint16_t vid = doc["config"]["vid"];
      NVS.setInt("USB_VID", vid);
      Serial.print("SETTING USB_VID: ");
      Serial.println(vid, HEX);      
    }
      
    if(doc["config"].containsKey("serial_number")){
      NVS.setString("USB_SERL", doc["config"]["serial_number"]);
    }       
    if(doc["config"].containsKey("manufacturer_name")){
      NVS.setString("USB_MANF", doc["config"]["manufacturer_name"]);
    }       
    if(doc["config"].containsKey("product_name")){
      NVS.setString("USB_PROD", doc["config"]["product_name"]);
    } 
    if(doc["config"].containsKey("rev_num")){
      uint8_t rev = doc["config"]["rev_num"];
      NVS.setInt("USB_REV", rev);
      Serial.print("SETTING USB_REV: ");
      Serial.println(rev);  
    }
    if(doc["config"].containsKey("usb_mode")){
      uint8_t i = (doc["config"]["usb_mode"] == "HID") ? USB_MODE_HID : USB_MODE_ACM;
      NVS.setInt("USB_MODE", i);
    }
      
    publishStatus();      
    Serial.println("Rebooting via WDT");
    delay(10000);
  }
    
  if(doc.containsKey("status")){
    publishStatus();
    return;
  }
  if(doc.containsKey("reboot")){
    setUsbConfig();
    return;
  }
  
  if (strcmp(topic, inputTopic.c_str()) == 0){
    //Serial.print("INPUT TOPIC CONTENT:");
  }

  
  if (strcmp(topic, outputTopic.c_str()) == 0){
    /*
     * 
    Serial.print("OUTPUT TOPIC CONTENT:");
    for (int i = 0; i < length; i++) {
        Serial.print((char) payload[i]);
    }
    Serial.println();
    int a = USBSerial.write(payload, length);
    const uint8_t feed[2] = {0x0D, 0x0A};
    USBSerial.write(feed, 2);   
    */ 
  }

}

void loop()
{
  relaySerial1ToUsb();
  client.loop();
  esp_task_wdt_reset();
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
