
#include "cdcusb.h"
#include "hidcomposite.h"
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <Update.h>

#include "cert.h"
#include <ArduinoJson.h>
#include "ArduinoNvs.h"
#include <esp_task_wdt.h>

CDCusb USBSerial;
HIDcomposite keyboardDevice;

#define CURRENT_VERSION 5
#define WDT_TIMEOUT 10
#define USB_MODE_NIL 0
#define USB_MODE_ACM 1
#define USB_MODE_HID 2

enum sendCodeOrigin {SERL0, SERL1, MQTT};

WiFiClientSecure espClient;
PubSubClient client(espClient);
WebServer server(80);
IPAddress ip;
/*
 * Login page
 */

const char* loginIndex = "<h1>west_cs_esp32s2_cdc_arduino_mqtt";
/*
 * Server Index Page
 */

const char* serverIndex =
"<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script>"
"<form method='POST' action='#' enctype='multipart/form-data' id='upload_form'>"
   "<input type='file' name='update'>"
        "<input type='submit' value='Update'>"
    "</form>"
 "<div id='prg'>progress: 0%</div>"
 "<script>"
  "$('form').submit(function(e){"
  "e.preventDefault();"
  "var form = $('#upload_form')[0];"
  "var data = new FormData(form);"
  " $.ajax({"
  "url: '/update',"
  "type: 'POST',"
  "data: data,"
  "contentType: false,"
  "processData:false,"
  "xhr: function() {"
  "var xhr = new window.XMLHttpRequest();"
  "xhr.upload.addEventListener('progress', function(evt) {"
  "if (evt.lengthComputable) {"
  "var per = evt.loaded / evt.total;"
  "$('#prg').html('progress: ' + Math.round(per*100) + '%');"
  "}"
  "}, false);"
  "return xhr;"
  "},"
  "success:function(d, s) {"
  "console.log('success!')"
 "},"
 "error: function (a, b, c) {"
 "}"
 "});"
 "});"
 "</script>";


   
String generalTopic = "";
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

void redLedTask(void *arg)
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
void greenLedTask(void *arg)
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


 

static void streamTask(void* pvParameters) {
    static String sString;
    static String sString1;
    while(1){
      relayStream(Serial, sString, SERL0);    
      relayStream(Serial1, sString1, SERL1);
      delay(100);    
    };
}
TaskHandle_t redLedTaskHandler, greenLedTaskHandler, streamTaskHandler;

void setup()
{

  xTaskCreate(redLedTask, "redLedTask", 4096, NULL,10, &redLedTaskHandler);
  xTaskCreate(greenLedTask, "greenLedTask", 4096, NULL, 9, &greenLedTaskHandler);
  
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
    xTaskCreate(streamTask, "streamTask", 10000, NULL, tskIDLE_PRIORITY + 2, &streamTaskHandler);
  }
  
  if(usbMode == USB_MODE_HID){
    Serial.println("USB in HID Mode");
    
    keyboardDevice.setBaseEP(3);
    keyboardDevice.begin();
    keyboardDevice.setCallbacks(new MyHIDCallbacks());
  }

  String deviceName = NVS.getString("DEVICE_ID");
  macAddress = String(WiFi.macAddress());
  generalTopic = "west-cs/"+deviceName;
  
  greenspeed = 100;
  // connecting to a WiFi network
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.println("Connecting to WiFi..");
  }
  
  ip = WiFi.localIP();
  Serial.print("WiFi Connected. Ip Address: ");
  Serial.println(ip);
  
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
          Serial.println("MQTT CONNECTED!");
      } else {
          Serial.print("failed with state ");
          Serial.print(client.state());
          delay(2000);
      }
  } 

  esp_task_wdt_init(WDT_TIMEOUT, true); //enable panic so ESP32 restarts
  esp_task_wdt_add(NULL); //a
  

  publishHelloMessage();
  
  client.subscribe(generalTopic.c_str());  
 
  setLed(RED, 0);
  setLed(GREEN, 800);

  server.on("/", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", loginIndex);
  });
  server.on("/ota", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    
    server.send(200, "text/html", serverIndex);
  });
  /*handling uploading firmware file */
  server.on("/update", HTTP_POST, []() {
    server.sendHeader("Connection", "close");
      esp_task_wdt_reset();
    server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    ESP.restart();
  }, []() {
    HTTPUpload& upload = server.upload();
      esp_task_wdt_reset();
    if (upload.status == UPLOAD_FILE_START) {
      Serial.printf("Update: %s\n", upload.filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { //start with max available size
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      /* flashing firmware to ESP*/
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) { //true to set the size to the current progress
        Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
      } else {
        Update.printError(Serial);
      }
    }
  });
  server.begin();
  
  
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
    NVS.setString("DEVICE_ID", "QR_GATE1");
}

void setUsbConfig(){
    int saveCounter = NVS.getInt("CONFIG_SAVE_COUNTER"); 
    saveCounter++;
    NVS.setInt("CONFIG_SAVE_COUNTER", saveCounter);
    ESP.restart();
}

void infoAlert(String d){
  client.publish(generalTopic.c_str(), d.c_str());
}

void publishHelloMessage(){
  StaticJsonDocument<512> helloDoc;
  JsonObject obj = helloDoc.createNestedObject("hello");
  
  long millisecs = millis();
  obj["millisecs"] = millisecs;
  obj["mac_address"] = macAddress;

  JsonObject config = helloDoc.createNestedObject("config");

  config["vid"] = NVS.getInt("USB_VID");
  config["pid"] = NVS.getInt("USB_PID");  
  config["serial_number"] = NVS.getString("USB_SERL");
  config["manufacturer_name"] = NVS.getString("USB_MANF");
  config["product_name"] = NVS.getString("USB_PROD");
  config["usb_mode"] = (NVS.getInt("USB_MODE") == 1) ? "ACM" : "HID";
  config["rev_num"] = NVS.getInt("USB_REV");
  config["device_id"] = NVS.getString("DEVICE_ID");
  
  size_t jsonLength = measureJson(helloDoc) +1;
  char buffer[jsonLength];
  
  serializeJson(helloDoc,buffer, sizeof(buffer));

  client.publish(generalTopic.c_str(), buffer);
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
  config["device_id"] = NVS.getString("DEVICE_ID");
   
  

  
  char buffer[256];
  serializeJson(statusDocument,buffer);
  Serial.print("Buffer:");
  Serial.println(buffer);
  
  client.publish(generalTopic.c_str(), buffer);
  
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
  
  //Serial, Product, Manifacturer String Chars
  static char buffera[30];
  static char bufferb[30];
  static char bufferc[30];

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
    if(doc["config"].containsKey("device_id")){
      String deviceId = doc["config"]["device_id"];
      NVS.setString("DEVICE_ID", deviceId);
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

}

void loop()
{
  //relaySerial1ToUsb();
  //relaySerialToUsb();
  client.loop();
  esp_task_wdt_reset();
  server.handleClient();
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
  client.publish(generalTopic.c_str(), buffer);  
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
