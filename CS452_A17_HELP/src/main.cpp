#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Stepper.h>
#include "ClosedCube_HDC1080.h"
#include "PixelFunctions.h"
#ifdef __AVR__
  #include <avr/power.h>
#endif

#define PIN 21
#define NUM_LEDS 4
#define BRIGHTNESS 50

#define NUM_STEPS_PER_REV 2048
#define STEPS 32

/*Put your SSID & Password*/
//const char *ssid = "AirVandalRobot";    // Enter SSID here
//const char *password = "R0b0tsb3Fr33!"; //Enter Password here
const char *ssid = "MySpectrumWiFid8-2G";
const char *password = "pinkbubble433";

WebServer server(80);
const char* detectServer = "http://13.83.132.121:5000//IOTAPI/DetectServer";
const char* registerServer = "http://13.83.132.121:5000//IOTAPI/RegisterWithServer";
const char* commandServer = "http://13.83.132.121:5000//IOTAPI/QueryServerForCommands";
const char* dataServer = "http://13.83.132.121:5000//IOTAPI/IOTData";
const char* shutdownServer = "http://13.83.132.121:5000//IOTAPI/IOTShutdown";

// class objects
ClosedCube_HDC1080 hdc1080;
Stepper myStepper(NUM_STEPS_PER_REV, 33, 32, 4, 14);
enum IOTAPICommands
{
  ping,
  login,
  query,
  data,
  shutdown
};

enum RGBCommands
{
  still,
  blink,
  rainbw
};

// task prototypes
void TaskMakeWebPage(void *pvParameters);
void TaskMoveStepper(void *pvParameters);
void TaskGetHumidTemp(void *pvParameters);
void IOTServerSend(void *pvParameters);

// function prototypes
void handle_OnConnect();
void handle_CWon();
void handle_CWoff();
void handle_CCWon();
void handle_CCWoff();
void handle_NotFound();
String SendHTML(uint8_t, uint8_t);
void managePixels(int, int, int, int, RGBCommands);

// HDC semaphores and queues
QueueHandle_t hdcQueue;
SemaphoreHandle_t hdcSemaphore;
// Stepper Queue
QueueHandle_t stepperQueue;

uint8_t CWpin = 4;
bool CWstatus = LOW;

uint8_t CCWpin = 5;
bool CCWstatus = LOW;

void setup()
{
  myStepper.setSpeed(12);
  Serial.begin(115200);
  hdc1080.begin(0x40);
  //delay(100);
  pinMode(CWpin, OUTPUT);
  pinMode(CCWpin, OUTPUT);

  Serial.println("Connecting to ");
  Serial.println(ssid);
  WiFi.mode(WIFI_MODE_STA);
  //connect to your local wi-fi network
  WiFi.begin(ssid, password);
  int timeToReboot = 0;
  //check wi-fi is connected to wi-fi network
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
    if(timeToReboot == 50)
    {
      ESP.restart();
    }
    timeToReboot++;
  }
  Serial.println("");
  Serial.println("WiFi connected.");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.println("===================================================");
  Serial.println("HTTP server started");
  if (digitalLeds_initStrands(STRANDS, STRANDCNT))
  {
    Serial.println("Init FAILURE: halting");
    while (true)
    {
    };
  }
  server.on("/", handle_OnConnect);
  server.on("/CWon", handle_CWon);
  server.on("/CWoff", handle_CWoff);
  server.on("/CCWon", handle_CCWon);
  server.on("/CCWoff", handle_CCWoff);
  server.onNotFound(handle_NotFound);
  server.begin();
  //server.handleClient();

  stepperQueue = xQueueCreate(1, sizeof(int)); // stepper queue take one arg for stepper dir
  hdcQueue = xQueueCreate(2, sizeof(int)); // size 2 for temp/humid
  hdcSemaphore = xSemaphoreCreateBinary();
  xTaskCreatePinnedToCore(TaskMakeWebPage, "WebPage", 2048, NULL, configMAX_PRIORITIES - 1, NULL, 1);
  xTaskCreatePinnedToCore(TaskMoveStepper, "Stepper", 2048, NULL, configMAX_PRIORITIES - 2, NULL, 1);
  xTaskCreatePinnedToCore(TaskGetHumidTemp, "Temp/Humid Sensor", 1026, NULL, configMAX_PRIORITIES -2, NULL, 1);
}
void loop()
{ 
}

void TaskMakeWebPage(void *pvParameters)
{
  (void) pvParameters;
  double temp;
  double humid;
  int direction = 0;
  RGBCommands command = still;
  //SendHTML(0, 0);
  for(;;)
  {
    server.handleClient();
    if(CWstatus != 1 && CCWstatus != 1) // Get temp and humid
    {
      managePixels(255, 0, 0, 0, command);
      xSemaphoreGive(hdcSemaphore);
      vTaskDelay(100 / portTICK_PERIOD_MS);
      xSemaphoreTake(hdcSemaphore, portMAX_DELAY);
      xQueueReceive(hdcQueue, &temp, 0);
      xQueueReceive(hdcQueue, &humid, 0);
      managePixels(0, 0, 0, 0, command);
    }
    if(CWstatus == 1 && CCWstatus != 1) // CCW
    {
      managePixels(0, 255, 0, 0, command);
      direction = -1;
      xQueueSend(stepperQueue, &direction, portMAX_DELAY);
      managePixels(0, 0, 0, 0, command);
    }
    if(CWstatus != 1 && CCWstatus == 1) // CW
    {
      managePixels(0, 0, 255, 0, command);
      direction = 1;
      xQueueSend(stepperQueue, &direction, portMAX_DELAY);
      managePixels(0, 0, 0, 0, command);
    }
    if(CWstatus == 1 && CCWstatus == 1)
    {
      //direction = 0;
      //xQueueSend(stepperQueue, &direction, portMAX_DELAY);
    }
    
  }
}


void handle_OnConnect()
{
  CWstatus = LOW;
  CCWstatus = LOW;
  Serial.println("GPIO4 Status: OFF | GPIO5 Status: OFF");
  server.send(200, "text/html", SendHTML(CWstatus, CCWstatus));
}

void handle_CWon()
{
  CWstatus = HIGH;
  Serial.println("CW Status: ON");
  server.send(200, "text/html", SendHTML(true, CCWstatus));
}

void handle_CWoff()
{
  CWstatus = LOW;
  Serial.println("CW Status: OFF");
  server.send(200, "text/html", SendHTML(false, CWstatus));
}

void handle_CCWon()
{
  CCWstatus = HIGH;
  Serial.println("CCW Status: ON");
  server.send(200, "text/html", SendHTML(CWstatus, true));
}

void handle_CCWoff()
{
  CCWstatus = LOW;
  Serial.println("CCW Status: OFF");
  server.send(200, "text/html", SendHTML(CWstatus, false));
}

void handle_NotFound()
{
  server.send(404, "text/plain", "Not found");
}

String SendHTML(uint8_t CWstat, uint8_t CCWstat)
{
  String ptr = "<!DOCTYPE html> <html>\n";
  ptr += "<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n";
  ptr += "<title>LED Control</title>\n";
  ptr += "<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}\n";
  ptr += "body{margin-top: 50px;} h1 {color: #444444;margin: 50px auto 30px;} h3 {color: #444444;margin-bottom: 50px;}\n";
  ptr += ".button {display: block;width: 80px;background-color: #3498db;border: none;color: white;padding: 13px 30px;text-decoration: none;font-size: 25px;margin: 0px auto 35px;cursor: pointer;border-radius: 4px;}\n";
  ptr += ".button-on {background-color: #3498db;}\n";
  ptr += ".button-on:active {background-color: #2980b9;}\n";
  ptr += ".button-off {background-color: #34495e;}\n";
  ptr += ".button-off:active {background-color: #2c3e50;}\n";
  ptr += "p {font-size: 14px;color: #888;margin-bottom: 10px;}\n";
  ptr += "</style>\n";
  ptr += "</head>\n";
  ptr += "<body>\n";
  ptr += "<h1>ESP32 Web Server</h1>\n";
  ptr += "<h3>Using Station(STA) Mode</h3>\n";

  if (CWstat)
  {
    ptr += "<p>CW Status: ON</p><a class=\"button button-off\" href=\"/CWoff\">OFF</a>\n";
  }
  else
  {
    ptr += "<p>CW Status: OFF</p><a class=\"button button-on\" href=\"/CWon\">ON</a>\n";
  }

  if (CCWstat)
  {
    ptr += "<p>CCW Status: ON</p><a class=\"button button-off\" href=\"/CCWoff\">OFF</a>\n";
  }
  else
  {
    ptr += "<p>CCW Status: OFF</p><a class=\"button button-on\" href=\"/CCWon\">ON</a>\n";
  }

  ptr += "</body>\n";
  ptr += "</html>\n";
  return ptr;
}
/********************************************************
 * TASK   void moveStepper(void *pvParameters)
 *  PRIORITY -> maxPriorities - 2
 *  This task moves the stepper motor
 * ******************************************************/
void TaskMoveStepper(void *pvParameters)
{
  (void) pvParameters;
  int direction = 0;
  for(;;)
  {

    if(!xQueueReceive(stepperQueue, &direction, portMAX_DELAY))
    {
      Serial.println("Stepper Task not receiving");
    }
    //Serial.println(direction);
    //Serial.println("In Stepper Task");
    myStepper.step(direction);
    direction = 0;
    taskYIELD();
  }
}
/**************************************************
 * void getHumidTemp(void *pvParameters)
 *  PRIORITY -> maxPriorities - 2
 *  This task gets temp / humidity
 * ************************************************/
void TaskGetHumidTemp(void *pvParameters)
{
  (void) pvParameters;
  double humid;
  double temp;
  for(;;)
  {
    xSemaphoreTake(hdcSemaphore, portMAX_DELAY);
    temp = hdc1080.readTemperature();
    Serial.println(temp);
    humid = hdc1080.readHumidity();
    Serial.println(humid);
    if(!xQueueSend(hdcQueue, &temp, portMAX_DELAY))
    {
      Serial.println("Error Sending Temp");
    }
    if(!xQueueSend(hdcQueue, &humid, portMAX_DELAY))
    {
      Serial.println("Error Sending Humidity");
    }
    xSemaphoreGive(hdcSemaphore);
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

void managePixels(int r, int g, int b, int w, RGBCommands command)
{
  strand_t *pStrand = &STRANDS[0];
  switch(command)
  {
    case still:
      for(uint16_t i = 0; i < pStrand->numPixels; i++)
      {
        pStrand->pixels[i] = pixelFromRGBW(r, g, b, w);
      }
      digitalLeds_updatePixels(pStrand);
      break;
    case blink:
      break;
    case rainbw:
      rainbow(pStrand, 0, 2000);
      break;
  }
}
