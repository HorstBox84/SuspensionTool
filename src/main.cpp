#include <Arduino.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include <FS.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <string.h>

#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

#include <SPI.h>

// WiFi
const char* ssid = "SuspensionTool";
const char* password = "SuspensionTool";

// WebServer
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
 
AsyncWebSocketClient * globalClient = NULL;

// Display
U8G2_SH1106_128X64_NONAME_1_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

// Measurement Values
const int analogInPin = A0;  // ESP8266 Analog Pin ADC0 = A0

float adcAvgValue = 0.0;  // value read from the pot
float Length = 0.0;
float RelLength = 0.0;
float MaxLength = 0.0;
float StaticSag = 0.0;
float DynamicSag = 0.0;

// Averaging
#define ADC_MIN 815
#define ADC_HUNDRED 1256

#define ADC_AVG 128
long avgBuffer[ADC_AVG] = {0};

// JSON
const int capacity = JSON_OBJECT_SIZE(5);
StaticJsonDocument<capacity> JsonDoc;

char JsonOutput[128];
 
// Timing
unsigned long now = 0;
unsigned long lastADC = 0;
unsigned long lastWeb = 0;
unsigned long lastDisp = 0;

#define ADC_TIMING 1
#define WEB_TIMING 100
#define DISP_TIMING 100

// WebSocket Handler
void onWsEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len){
 
  if(type == WS_EVT_CONNECT){
 
    Serial.println("Websocket client connection received");
    globalClient = client;
 
  } else if(type == WS_EVT_DISCONNECT){
 
    Serial.println("Websocket client connection finished");
    globalClient = NULL;
 
  } else if(type == WS_EVT_DATA){

    switch (data[0]) {
      case 'm':
        MaxLength = Length;
        Serial.print("MaxLength: ");
        Serial.println(MaxLength);
        break;
      case 's':
        StaticSag = MaxLength - Length;
        Serial.print("StaticSag: ");
        Serial.println(StaticSag);
        break;
      case 'd':
        DynamicSag = MaxLength - Length;
        Serial.print("DynamicSag: ");
        Serial.println(DynamicSag);
        break;
      default:
        Serial.println("Didn't match a string");
        break;
    }
  }
}

// Read ADC and calculate floating average
float measureADC () {
  static int i = 0;
  static int j = 0;
  
  if (i < (ADC_AVG - 1)) {
    i ++;
  }
  else {
    i = 0;
  }
  avgBuffer[i] = analogRead(analogInPin);

  j = 0;
  float value = 0;
  while (j < ADC_AVG) {
    value += avgBuffer[j];
    j++;
  }
  value /= (float)ADC_AVG;

  Serial.print("ADC Value: ");
  Serial.println(value);

  return (value);
}

float calculateLength (float value) {
  return ((value - ADC_MIN) * ((100.0)/(ADC_HUNDRED-ADC_MIN)));
}

void setup() {
  // initialize serial communication at 115200
  Serial.begin(115200);

  if(!SPIFFS.begin(true)){
     Serial.println("An Error has occurred while mounting SPIFFS");
     return;
  }

  u8g2.begin();

  u8g2.begin();
  u8g2.enableUTF8Print();		// enable UTF8 support for the Arduino print() function

  u8g2.setFont(u8g2_font_profont12_mf);

  //ESP32 As access point
  WiFi.disconnect();   //added to start with the wifi off, avoid crashing
  WiFi.mode(WIFI_OFF); //added to start with the wifi off, avoid crashing
  WiFi.setHostname("SuspensionTool");
  WiFi.mode(WIFI_AP);
  WiFi.softAP(ssid, password);

  IPAddress IP = WiFi.softAPIP();
  Serial.print("AP IP address: ");
  Serial.println(IP);

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
 
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/ws.html", "text/html");
  });

  server.begin();
}

void loop() {
  now = millis();

  if ((now - lastADC) > ADC_TIMING) {
    adcAvgValue = measureADC();
    Length = calculateLength(adcAvgValue);
    RelLength = MaxLength - Length;
    lastADC = now;
  }
  
  if ((now - lastDisp) > DISP_TIMING) {
    u8g2.firstPage();
    do {
      u8g2.setCursor(0, 15);
      u8g2.print("Length: ");
      u8g2.print(Length);
      u8g2.print(" mm");

      u8g2.setCursor(0, 30);
      u8g2.print("Relative: ");
      u8g2.print(RelLength);
      u8g2.print(" mm");

      u8g2.setCursor(0, 45);
      u8g2.print("Static: ");
      u8g2.print(StaticSag);
      u8g2.print(" mm");

      u8g2.setCursor(0, 60);
      u8g2.print("Dynamic: ");
      u8g2.print(DynamicSag);
      u8g2.print(" mm");
      
    } while ( u8g2.nextPage() );

    lastDisp = now;
  }

  if ((now - lastWeb) > WEB_TIMING) {
      if(globalClient != NULL && globalClient->status() == WS_CONNECTED){

        JsonDoc["length"] = Length;
        JsonDoc["max"] = MaxLength;
        JsonDoc["static"] = StaticSag;
        JsonDoc["dynamic"] = DynamicSag;
        JsonDoc["relative"] = RelLength;

        serializeJson(JsonDoc, JsonOutput);
        
        /*
        Serial.print("Length ");
        Serial.print(Length, 20);
        Serial.print(" MaxLength: ");
        Serial.print(MaxLength, 20);
        Serial.print(" StaticSag: ");
        Serial.print(StaticSag, 20);
        Serial.print(" Dynamic: ");
        Serial.println(DynamicSag, 20);

        
        Serial.println(JsonOutput);
        */

        globalClient->text(String(JsonOutput));
    }
    lastWeb = now;
  }
}
