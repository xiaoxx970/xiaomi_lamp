/***************************************************
  触控台灯V0.9
  features：
    1、单个按键控制台灯开关和PWM亮度调整
    2、MQTT连接云端通过网页或手机控制亮度和开关
    3、OTA以及网页端可以升级固件
    4、渐亮渐暗
    5、OTA开关通过MQTT控制（0.8添加）
  plan：
    1、增加双色调节通过短按+长按
    3、增加debug信息输出到mqtt
    4、亮度保存

  Written by Xiaoxx for self use.
 ****************************************************/
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <WiFiClient.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
//#include <FS.h>
#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"

/************************* WiFi Access Point *********************************/

#define WLAN_SSID "ljp"
#define WLAN_PASS "18852513496"

/************************* Web Server Define *********************************/
ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;

/************************* Adafruit.io Setup *********************************/

#define AIO_SERVER "io.adafruit.com"
#define AIO_SERVERPORT 1883 // use 8883 for SSL
#define AIO_USERNAME "xiaoxx"
#define AIO_KEY "eb0927276b254479b700c2d9e94663d0"

/************ Global State (you don't need to change this!) ******************/

WiFiClient client;
// or... use WiFiFlientSecure for SSL
//WiFiClientSecure client;

// Setup the MQTT client class by passing in the WiFi client and MQTT server and login details.
Adafruit_MQTT_Client mqtt(&client, AIO_SERVER, AIO_SERVERPORT, AIO_USERNAME, AIO_KEY);

/****************************** Feeds ***************************************/

// Setup a feed called 'potValue' for publishing.
// Notice MQTT paths for AIO follow the form: <username>/feeds/<feedname>
Adafruit_MQTT_Publish potlight = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/ledBrightness");
Adafruit_MQTT_Publish potStatus = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/ledState");
Adafruit_MQTT_Publish potupdate = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/ota");
// Setup a feed called 'ledBrightness' for subscribing to changes.
Adafruit_MQTT_Subscribe ledBrightness = Adafruit_MQTT_Subscribe(&mqtt, AIO_USERNAME "/feeds/ledBrightness");
Adafruit_MQTT_Subscribe ledStatus = Adafruit_MQTT_Subscribe(&mqtt, AIO_USERNAME "/feeds/ledState");
Adafruit_MQTT_Subscribe isupdate = Adafruit_MQTT_Subscribe(&mqtt, AIO_USERNAME "/feeds/ota");
/*************************** Sketch Code ************************************/

// Bug workaround for Arduino 1.6.6, it seems to need a function declaration
// for some reason (only affects ESP8266, likely an arduino-builder bug).
void MQTT_connect();

const int buttonPin = 2;
const int ledPin = 3;

unsigned long duration;
int ledState = 0;
int light = 655;
int cplight;
bool plus = 1;
uint16_t potlightVaule = 0;
uint16_t potledState = 0;
const char *host = "esp-update";
int otaupdate = 0;
uint8_t retries = 20;

void pwm()
{
  //如果按键放开了还是进入了pwm那就把灯关了返回
  if (digitalRead(buttonPin))
  {
    ledState = 0;
    return;
  }
  plus = !plus;
  while (digitalRead(buttonPin) == LOW & light >= 10 & light <= 1024)
  {
    //Serial.println("at pwm.while");
    //Serial.println(light);
    plus == 1 ? light += 1 : light -= 1;
    if (light > 1024)
      light = 1024;
    if (light < 10)
      light = 10;
    delay(2);
    analogWrite(ledPin, light);
  }
  //potlight.publish(light);   //上传亮度
}

void event()
{
  delay(10);
  if (digitalRead(buttonPin) == LOW)
  {
    if (ledState == 1)
    {
      duration = pulseIn(buttonPin, LOW, 600000);
      if (duration == 0)  //如果长按就进入pwm程序
        pwm();
      else                //否则把灯关掉
        ledState = 0;
    }
    else                  //如果灯是关着的
      ledState = 1;
  }
  //potStatus.publish(ledState); //上传开关
  while (digitalRead(buttonPin) == LOW)
    ; //等待按键抬起
}

void setup()
{
  Serial.begin(115200);
  //  SPIFFS.begin();
  //delay(10);
  Serial.println("");
  Serial.println("Welcome to use Xiaoxx cloud lamp");
  Serial.println("version 0.7");
  Serial.println();
  Serial.print("Connecting to ");
  Serial.print(WLAN_SSID);
  /**************连接WiFi和MQTT服务器*****************/
  WiFi.begin(WLAN_SSID, WLAN_PASS);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  mqtt.subscribe(&ledBrightness);
  mqtt.subscribe(&ledStatus);
  mqtt.subscribe(&isupdate);
  /************************************************/
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else // U_SPIFFS
      type = "filesystem";
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
    if (error == OTA_AUTH_ERROR)
      Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR)
      Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR)
      Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR)
      Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR)
      Serial.println("End Failed");
  });
  ArduinoOTA.begin();
  Serial.println("OTA Ready");
  MDNS.begin(host);
  httpUpdater.setup(&httpServer);
  //  httpServer.on ("/", handleMain); // 绑定‘/’地址到handleMain方法处理
  //  httpServer.on ("/pin", HTTP_GET, handlePin); // 绑定‘/pin’地址到handlePin方法处理
  //  httpServer.onNotFound ( handleNotFound ); // NotFound处理
  httpServer.begin();
  Serial.println("HTTP server started");
  MDNS.addService("http", "tcp", 80);
  Serial.printf("HTTPUpdateServer ready! Open http://%s.local/update in your browser\n", host);
  potupdate.publish(otaupdate);
}

void loop()
{
  MQTT_connect();
  Adafruit_MQTT_Subscribe *subscription;
  while ((subscription = mqtt.readSubscription(200)))
  { //订阅MQTT
    if (subscription == &ledBrightness)
    {
      Serial.print(F("Got LED Brightness : "));
      cplight = light;
      light = atoi((char *)ledBrightness.lastread);
      Serial.println(light);
      if (ledState == 1 && light >= 10)
      { //灯开着而且收到大于10的亮度，执行亮度
        for (; cplight < light; cplight++)
        {
          analogWrite(ledPin, cplight);
          delayMicroseconds(500);
        }
        for (; cplight > light; cplight--)
        {
          analogWrite(ledPin, cplight);
          delayMicroseconds(500);
        }
      }
      else if (ledState == 1 && light < 10)
      { //灯开着但是亮度调到了10以下，执行关灯并把亮度设回10
        for (int i = cplight; i > 0; i--)
        {
          analogWrite(ledPin, i);
          delayMicroseconds(500);
        }
        ledState = 0;
        light = 10;
        potlight.publish(light);
        potStatus.publish(ledState);
        break;
      }
    }
    if (subscription == &ledStatus)
    {
      Serial.print(F("Got LED Status : "));
      ledState = atoi((char *)ledStatus.lastread);
      Serial.println(ledState);
    }
    if (subscription == &isupdate)
    {
      Serial.print(F("Got OTA Status : "));
      otaupdate = atoi((char *)isupdate.lastread);
      Serial.println(otaupdate);
      if (otaupdate)
      {
        Serial.println("OTA response on.");
      }
      else
        Serial.println("OTA response off.");
    }
  }
  if (otaupdate == 1)
  {
    httpServer.handleClient();
    ArduinoOTA.handle();
  }
  if (!digitalRead(buttonPin))
  {
    event();
    if ((light > (potlightVaule + 5)) || (light < (potlightVaule - 5)))
    {
      potlightVaule = light;
      Serial.print(F("Sending light val "));
      Serial.print(potlightVaule);
      Serial.print("...");
      if (!potlight.publish(potlightVaule))
      {
        Serial.println(F("Failed"));
      }
      else
      {
        Serial.println(F("OK!"));
      }
    }
  }
  if (ledState != potledState)
  {
    if (ledState == 1)
      for (int i = 0; i < light; i++)
      {
        analogWrite(ledPin, i);
        delayMicroseconds(500);
      }
    else
      for (int i = light; i >= 0; i--)
      {
        analogWrite(ledPin, i);
        delayMicroseconds(500);
      }
    potledState = ledState;
    Serial.print(F("Sending switch val "));
    Serial.print(potledState);
    Serial.print("...");
    if (!potStatus.publish(potledState))
    {
      Serial.println(F("Failed"));
    }
    else
    {
      Serial.println(F("OK!"));
    }
  }
}

// Function to connect and reconnect as necessary to the MQTT server.
// Should be called in the loop function and it will take care if connecting.
void MQTT_connect()
{
  int8_t ret;

  // Stop if already connected.
  if (mqtt.connected())
  {
    return;
  }

  //Serial.print("Connecting to MQTT... ");

  retries--;
  if (retries == 0)
  {
    if ((ret = mqtt.connect()) != 0)
    { // connect will return 0 for connected
      mqtt.disconnect();
      return;
      //Serial.println(mqtt.connectErrorString(ret));
      //Serial.println("Retrying MQTT connection in 5 seconds...");
      //delay(5000);  // wait 5 seconds
      // basically die and wait for WDT to reset me
      //while (1);
    }
    retries = 20;
  }
  //Serial.println("MQTT Connected!");
}
