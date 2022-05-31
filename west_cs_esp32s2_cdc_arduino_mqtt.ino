
#include "cdcusb.h"
#include "hidcomposite.h"
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

#include "cert.h"
#include <ArduinoJson.h>
#include "ArduinoNvs.h"
#include <esp_task_wdt.h>
#include "esp32fota.h"

CDCusb USBSerial;
HIDcomposite keyboardDevice;

#define CURRENT_VERSION 5
#define WDT_TIMEOUT 10
#define USB_MODE_NIL 0
#define USB_MODE_ACM 1
#define USB_MODE_HID 2

enum sendCodeOrigin {SERL0, SERL1, MQTT};
    
esp32FOTA esp32FOTA("west_cs_esp32s2_cdc_arduino_mqtt", CURRENT_VERSION);

WiFiClientSecure espClient;
WiFiClientSecure otaClient;

PubSubClient client(espClient);

//Serial, Product, Manifacturer String Chars
char buffera[30];
char bufferb[30];
char bufferc[30];
   
String outputTopic = "";
String inputTopic= ""; 
String infoTopic="";
String macAddress = "";

int usbMode;
class MyHIDCallbacks: public HIDCallbacks{
  void onData(uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize) {
    Serial.printf("ID: %d, type: %d, size: %d\n", report_id, (int)report_type, bufsize);
     for (size_t i = 0; i < bufsize; i++)
    {
        Serial.printf("%d\n", buffer[i]);
    }
  }
};


TaskHandle_t myTask1Handle = NULL;
TaskHandle_t myTask2Handle = NULL;
enum ledColor {GREEN, RED}; 
volatile int greenspeed = 200;
volatile int redspeed = 200;

void setLed(ledColor color, int speed){
  if(color == RED){
    redspeed = speed; 
  }
  if(color == GREEN){
    greenspeed = speed;
  }
}
#define RED_RED GPIO_NUM_36
#define GREEN_LED GPIO_NUM_35
void task1(void *arg)
{
  gpio_pad_select_gpio(RED_RED);
  gpio_set_direction(RED_RED, GPIO_MODE_OUTPUT);
  while(1) {
        if(redspeed>0){
              gpio_set_level(RED_RED, 1);
              vTaskDelay((redspeed+50) / portTICK_PERIOD_MS);
              gpio_set_level(RED_RED, 0);
        }
        vTaskDelay((redspeed+50) / portTICK_PERIOD_MS);
    }
}
void task2(void *arg)
{
  gpio_pad_select_gpio(GREEN_LED);
  gpio_set_direction(GREEN_LED, GPIO_MODE_OUTPUT);
  while(1) {
        if(greenspeed>0){
          gpio_set_level(GREEN_LED, 1);
          vTaskDelay((greenspeed+50) / portTICK_PERIOD_MS);
          gpio_set_level(GREEN_LED, 0);
        }
      vTaskDelay((greenspeed+50) / portTICK_PERIOD_MS);
}
}


TaskHandle_t Handle_aTask;
TaskHandle_t Handle_bTask;

 

static void ReadStreamsThread(void* pvParameters) {
    static String sString;
    static String sString1;
    while(1){
      relayStream(Serial, sString, SERL0);    
      relayStream(Serial1, sString1, SERL1);
      delay(100);    
    };
}

 

static void ThreadB(void* pvParameters) {
    Serial.println("Thread B: Started");
    for (int i = 0; i < 10; i++) {
        Serial.println("---This is Thread B---");
        delay(2000);
    }
    Serial.println("Thread B: Deleting");
    vTaskDelete(NULL);
}
//

void setup()
{

  xTaskCreate(task1, "task1", 4096, NULL,10, &myTask1Handle);
  xTaskCreate(task2, "task2", 4096, NULL, 9, &myTask2Handle);
  
  Serial.begin(115200);
  Serial1.begin(115200);

  //xTaskCreate(ThreadB, "Task B", 10000, NULL, tskIDLE_PRIORITY + 1, &Handle_bTask);

  Serial.println("Read NVS");
  NVS.begin("BrewerSystems");  //https://github.com/rpolitex/ArduinoNvs
  usbMode = NVS.getInt("USB_MODE"); 

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
    xTaskCreate(ReadStreamsThread, "ReadStreamsThread", 10000, NULL, tskIDLE_PRIORITY + 2, &Handle_aTask);
  }
  
  if(usbMode == USB_MODE_HID){
    Serial.println("USB in HID Mode");
    
    keyboardDevice.setBaseEP(3);
    keyboardDevice.begin();
    keyboardDevice.setCallbacks(new MyHIDCallbacks());
  }
  
  macAddress = String(WiFi.macAddress());
  outputTopic = "west-cs/"+macAddress+"/out";
  inputTopic = "west-cs/"+macAddress+"/in";
  infoTopic = "west-cs/"+macAddress+"/info";
  greenspeed = 100;
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

  checkForOTA();
  
  esp_task_wdt_init(WDT_TIMEOUT, true); //enable panic so ESP32 restarts
  esp_task_wdt_add(NULL); //a
  
  infoAlert("ONLINE");
  publishStatus();
  
  client.subscribe(inputTopic.c_str());  
  client.subscribe(outputTopic.c_str());   
  setLed(RED, 0);
  setLed(GREEN, 800);
  
}

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

void checkForOTA(){
  /* firmware.json
   *  
{
    "type": "west_cs_esp32s2_cdc_arduino_mqtt",
    "version": 2,
    "host": "www.brewersystems.com",
    "port": 443,
    "bin": "/firmware/west_cs_esp32s2_cdc_arduino_mqtt/firmware.bin"
}
   */

  esp32FOTA.checkURL="https://raw.githubusercontent.com/nathanabrewer/west_cs_esp32s2_cdc_arduino_mqtt/master/firmware.json"; 
  bool shouldExecuteFirmwareUpdate = esp32FOTA.execHTTPcheck();
  if(shouldExecuteFirmwareUpdate)
  {
    Serial.println("Firmware update available!");
    esp32FOTA.execOTA();
  }
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

void startUsbSerialHost(){
  Serial.println("Starting USB_MODE_ACM");
  String usbSerl = NVS.getString("USB_SERL");
  String usbManf = NVS.getString("USB_MANF");
  String usbProd = NVS.getString("USB_PROD");
  uint16_t vid = NVS.getInt("USB_VID");
  uint16_t pid = NVS.getInt("USB_PID");
  uint8_t revision = NVS.getInt("USB_REV");
 
   USBSerial.setCallbacks(new MyUSBCallbacks());
   
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

  if(doc.containsKey("red_led")){
    setLed(RED, (int)doc["red_led"]);
  }  
  if(doc.containsKey("green_led")){
    setLed(GREEN, (int)doc["green_led"]);
  } 
  
  if (doc.containsKey("code")) {
    String code = String( doc["code"].as<char*>() );
    sendOutput(code, MQTT);
  }

  //Mouse.move(40, 0);
  if (doc.containsKey("mouse")) {
    if(usbMode == USB_MODE_HID){
      //keyboardDevice.sendString(doc["keyboard"]);
    }else{
      Serial.println("Error, not in USB HID Mode for Mouse Command");
    }
  }
  if (doc.containsKey("keyboard")) {
      if(usbMode == USB_MODE_HID){
        String keydata = doc["keyboard"];
        keyboardDevice.sendString(keydata);
        Serial.print("Keyboard Send: ");
        Serial.println(keydata);
      }else{
        Serial.println("Error, not in USB HID Mode for Keyboard Command");
      }
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
    setLed(RED, 100);
    setLed(GREEN, 0);
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
  //relaySerial1ToUsb();
  //relaySerialToUsb();
  client.loop();
  esp_task_wdt_reset();
}

void sendOutput(String &code, sendCodeOrigin origin){
  Serial.println(code);
  USBSerial.write((byte*) code.c_str(), code.length());
  Serial.printf("USBSerial wrote %d bytes. (json['code'])");
  Serial.println("");
  const uint8_t feed[2] = {0x0D, 0x0A};
  USBSerial.write(feed, 2);  
  Serial.println("USBSerial wrote 2 bytes. (appending LF CR)");
  //infoAlert how many bytes i wrote?   

  StaticJsonDocument<256> doc;
  JsonObject obj = doc.createNestedObject("sent");
  obj["size"] = code.length();
  obj["string"] = code;
  if(origin == SERL0){
    obj["origin"] = "SERIAL0";
  }else if (origin == SERL1){
    obj["origin"] = "SERIAL1";
  }else if (origin == MQTT){
    obj["origin"] = "MQTT";
  }  
  char buffer[256];
  serializeJson(doc,buffer);  
  client.publish(infoTopic.c_str(), buffer);  
}

void relayStream(Stream &port, String &content, sendCodeOrigin origin){
  static char c;
  static bool endofline;
  while(port.available()) {
     c = port.read();
     if(c == 0x0D || c == 0x0A){
       endofline = true;
     }else{
       content.concat(c);
     }
  }

  if(endofline){
      sendOutput(content, origin);
      content = "";
      endofline = false;
  }
     
}
void relaySerialToUsb(){
    static String sString;
    relayStream(Serial, sString, SERL0);    
}

void relaySerial1ToUsb(){
    static String sString1;
    relayStream(Serial1, sString1, SERL1);    
}
