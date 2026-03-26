#include <Arduino.h>

#define BLYNK_PRINT Serial
#define BLYNK_TEMPLATE_ID   "TMPL6_hK8XnHx"
#define BLYNK_TEMPLATE_NAME "LED Control"
#define BLYNK_AUTH_TOKEN    "BrewmiLPnRMAFwD67Qv40f9jsdoPBzsb"

#include <WiFi.h>
#include <WiFiClient.h>
#include <BlynkSimpleEsp32.h>

// Configurations
const int buttonPin = 14;
const int buzzerPin = 15;
const int ledPins[] = {18, 4, 16, 17};
const int numLeds = 4;

char ssid[] = "dana";
char pass[] = "danaD1234";

volatile int stage = 0;  // 0=OFF 1=ON
volatile int selectedLed = 0;  // 0..3
volatile int ledPercent[4] = {0, 0, 0, 0};  // 0..100 

// Buzzer Control
volatile bool buzzerActive = false;
SemaphoreHandle_t buzzerSemaphore = NULL;

// PWM
const int pwmFreq = 5000; // to make the lighting consistant.
const int pwmResolution = 8;               

// Joystick 
volatile int joyX = 512;
volatile int joyY = 512;

const int HI_TH = 700;      
const int LO_TH = 300;     
const int CENTER = 512;    
const int DEAD_ZONE = 150; 

// to ensure that the joystick returned to the middle then another move happened
bool upLatched = false;
bool downLatched = false;
bool rightLatched = false;
bool leftLatched = false;

// Debounce timing
unsigned long lastJoyAction = 0;
const unsigned long JOY_DEBOUNCE = 200; 

// LCD Update Control
bool lcdNeedsUpdate = false;
unsigned long lastLcdUpdate = 0;
const unsigned long LCD_UPDATE_INTERVAL = 500; 

// FreeRTOS Button Notify 
TaskHandle_t buttonTaskHandle = NULL;
volatile uint32_t lastIsrMs = 0;

// Button Interrupt
void IRAM_ATTR handleButtonInterrupt() 
{
  uint32_t now = millis();
  if (now - lastIsrMs < 300) return;
  lastIsrMs = now;

  if (digitalRead(buttonPin) != LOW) return;

  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  vTaskNotifyGiveFromISR(buttonTaskHandle, &xHigherPriorityTaskWoken);
  if (xHigherPriorityTaskWoken) portYIELD_FROM_ISR();
}

// BUZZER TASK 
void buzzerTask(void *pvParameters) 
{
  (void)pvParameters;

  for (;;) 
  {
    // Wait for semaphore signal
    if (xSemaphoreTake(buzzerSemaphore, portMAX_DELAY) == pdTRUE) {
      Serial.println("BUZZER: ON for 2 seconds (Limit reached!)");
      
      // Turn buzzer ON with PWM tone (the highest)
      ledcWrite(buzzerPin, 255);
      
      vTaskDelay(pdMS_TO_TICKS(2000)); // 2 seconds
      
      // Turn buzzer OFF
      ledcWrite(buzzerPin, 0);
      
      Serial.println("BUZZER: OFF");
      buzzerActive = false;
    }
  }
}

// LED TASK 
void ledTask(void *pvParameters) 
{
  (void)pvParameters;

  for (;;) 
  {
    for (int i = 0; i < numLeds; i++) 
    {
      // Always use PWM based on ledPercent (no stage check)
      int pwmValue = map(ledPercent[i], 0, 100, 0, 255);
      ledcWrite(ledPins[i], pwmValue);
    }
    vTaskDelay(pdMS_TO_TICKS(20)); // Faster update for smooth PWM
  }
}

// LCD TASK 
void lcdTask(void *pvParameters) 
{
  (void)pvParameters;
  
  String prevLine1 = "";
  String prevLine2 = "";

  for (;;) {
    if (Blynk.connected() && (lcdNeedsUpdate || (millis() - lastLcdUpdate > LCD_UPDATE_INTERVAL))) 
    {
      String line1 = "L1:" + String(ledPercent[0]) + "% L2:" + String(ledPercent[1]) + "%";
      String line2 = "L3:" + String(ledPercent[2]) + "% L4:" + String(ledPercent[3]) + "%";
      
      // Only send if changed
      if (line1 != prevLine1) 
      {
        Blynk.virtualWrite(V0, line1);
        prevLine1 = line1;
      }
      if (line2 != prevLine2) 
      {
        Blynk.virtualWrite(V1, line2);
        prevLine2 = line2;
      }
      
      lcdNeedsUpdate = false;
      lastLcdUpdate = millis();
    }
    vTaskDelay(pdMS_TO_TICKS(200)); // Check every 200ms
  }
}

// JOYSTICK LOGIC TASK 
void joystickTask(void *pvParameters) 
{
  (void)pvParameters;

  for (;;) 
  {
    unsigned long now = millis();
    int x = joyX;
    int y = joyY;

    // Check if joystick is in dead zone (near center)
    bool inDeadZoneX = (x > (CENTER - DEAD_ZONE) && x < (CENTER + DEAD_ZONE));
    bool inDeadZoneY = (y > (CENTER - DEAD_ZONE) && y < (CENTER + DEAD_ZONE));

    // UP: +20%
    if (y > HI_TH && !inDeadZoneY) 
    {
      if (!upLatched && (now - lastJoyAction > JOY_DEBOUNCE)) 
      {
        int oldValue = ledPercent[selectedLed];
        ledPercent[selectedLed] += 20;
        
        // Check if exceeded maximum (100%)
        if (ledPercent[selectedLed] > 100) 
        {
          ledPercent[selectedLed] = 100;
          // Trigger buzzer if trying to go beyond 100%
          if (oldValue == 100 && !buzzerActive) 
          {
            buzzerActive = true;
            xSemaphoreGive(buzzerSemaphore);
          }
        }
        
        lcdNeedsUpdate = true;
        upLatched = true;
        lastJoyAction = now;
        Serial.print("Joystick UP - LED");
        Serial.print(selectedLed + 1);
        Serial.print(" intensity: ");
        Serial.print(ledPercent[selectedLed]);
        Serial.println("%");
      }
    } else 
    {
      upLatched = false;
    }

    // DOWN: -20%
    if (y < LO_TH && !inDeadZoneY) 
    {
      if (!downLatched && (now - lastJoyAction > JOY_DEBOUNCE)) 
      {
        int oldValue = ledPercent[selectedLed];
        ledPercent[selectedLed] -= 20;
        
        // Check if went below minimum (0%)
        if (ledPercent[selectedLed] < 0) 
        {
          ledPercent[selectedLed] = 0;
          // Trigger buzzer if trying to go below 0%
          if (oldValue == 0 && !buzzerActive) 
          {
            buzzerActive = true;
            xSemaphoreGive(buzzerSemaphore);
          }
        }
        
        lcdNeedsUpdate = true;
        downLatched = true;
        lastJoyAction = now;
        Serial.print("Joystick DOWN - LED");
        Serial.print(selectedLed + 1);
        Serial.print(" intensity: ");
        Serial.print(ledPercent[selectedLed]);
        Serial.println("%");
      }
    } else 
    {
      downLatched = false;
    }

    // RIGHT: select next LED
    if (x > HI_TH && !inDeadZoneX) 
    {
      if (!rightLatched && (now - lastJoyAction > JOY_DEBOUNCE)) 
      {
        selectedLed = (selectedLed + 1) % 4;
        lcdNeedsUpdate = true;
        rightLatched = true;
        lastJoyAction = now;
        Serial.print("Joystick RIGHT - Selected LED");
        Serial.println(selectedLed + 1);
      }
    } else 
    {
      rightLatched = false;
    }

    // LEFT: select previous LED
    if (x < LO_TH && !inDeadZoneX) 
    {
      if (!leftLatched && (now - lastJoyAction > JOY_DEBOUNCE)) 
      {
        selectedLed = (selectedLed + 3) % 4;
        lcdNeedsUpdate = true;
        leftLatched = true;
        lastJoyAction = now;
        Serial.print("Joystick LEFT - Selected LED");
        Serial.println(selectedLed + 1);
      }
    } else 
    {
      leftLatched = false;
    }

    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

// BUTTON TASK 
void buttonTask(void *pvParameters) 
{
  (void)pvParameters;

  for (;;) 
  {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    // Toggle stage
    stage = 1 - stage;
    
    // Update ALL LED percentages based on stage
    int newValue = (stage == 1) ? 100 : 0;
    for (int i = 0; i < 4; i++) 
    {
      ledPercent[i] = newValue;
    }
    
    lcdNeedsUpdate = true;
    
    Serial.print("Button pressed! All LEDs set to: ");
    Serial.print(newValue);
    Serial.println("%");
  }
}

// CONNECTION with blynk 
void connectionTask(void *pvParameters) 
{
  (void)pvParameters;

  for (;;) 
  {
    if (WiFi.status() != WL_CONNECTED) 
    {
      Serial.println("WiFi disconnected, reconnecting...");
      WiFi.disconnect();
      WiFi.begin(ssid, pass);
      vTaskDelay(pdMS_TO_TICKS(5000));
    }

    if (WiFi.status() == WL_CONNECTED && !Blynk.connected()) {
      Serial.println("Blynk disconnected, reconnecting...");
      Blynk.connect(3000);
    }

    vTaskDelay(pdMS_TO_TICKS(2000));
  }
}

// SERIAL TASK for serial print
void serialPrintingTask(void *pvParameters) 
{
  (void)pvParameters;

  for (;;) 
  {
    Serial.print("WiFi=");
    Serial.print(WiFi.status() == WL_CONNECTED ? "OK" : "DOWN");
    Serial.print(" | Blynk=");
    Serial.print(Blynk.connected() ? "OK" : "DOWN");
    Serial.print(" | Stage=");
    Serial.print(stage ? "ON" : "OFF");
    Serial.print(" | Selected=L");
    Serial.print(selectedLed + 1);
    Serial.print(" | LEDs: ");
    for (int i = 0; i < 4; i++) 
    {
      Serial.print(ledPercent[i]);
      Serial.print("% ");
    }
    Serial.println();
    vTaskDelay(pdMS_TO_TICKS(3000));
  }
}

// BLYNK HANDLERS
BLYNK_WRITE(V2) 
{
  joyX = param.asInt();
}

BLYNK_WRITE(V3) 
{
  joyY = param.asInt();
}

BLYNK_CONNECTED() 
{
  Serial.println("Blynk Connected!");
  lcdNeedsUpdate = true;
}

// SETUP 
void setup() 
{
  Serial.begin(115200);
  Serial.println("\n\nStarting ESP32 LED Control...");

  pinMode(buttonPin, INPUT_PULLUP);
  pinMode(buzzerPin, OUTPUT);

  // Create semaphore for buzzer
  buzzerSemaphore = xSemaphoreCreateBinary();

  // WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  Serial.print("Connecting to WiFi");
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) 
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println(WiFi.status() == WL_CONNECTED ? "\nWiFi Connected" : "\nWiFi NOT Connected");

  // Blynk
  Blynk.config(BLYNK_AUTH_TOKEN, "blynk.cloud", 80);
  Blynk.connect(3000); 

  // PWM setup for LEDs 
  for (int i = 0; i < numLeds; i++) 
  {
    ledcAttach(ledPins[i], pwmFreq, pwmResolution);
    ledcWrite(ledPins[i], 0);
  }
  
  // Setup buzzer PWM  (2000 Hz frequency)
  ledcAttach(buzzerPin, 2000, 8); 
  ledcWrite(buzzerPin, 0); // Start with buzzer OFF

  // Tasks
  xTaskCreate(buzzerTask, "Buzzer", 2048, NULL, 4, NULL);
  xTaskCreate(ledTask, "LED", 2048, NULL, 3, NULL);
  xTaskCreate(lcdTask, "LCD", 4096, NULL, 1, NULL);
  xTaskCreate(joystickTask, "Joy", 3072, NULL, 2, NULL);
  xTaskCreate(buttonTask, "Btn", 2048, NULL, 3, &buttonTaskHandle);
  xTaskCreate(connectionTask, "Conn", 3072, NULL, 2, NULL);
  xTaskCreate(serialPrintingTask, "SerialPrint", 2048, NULL, 1, NULL);

  // Interrupt
  attachInterrupt(digitalPinToInterrupt(buttonPin), handleButtonInterrupt, FALLING);
  
  Serial.println("Setup complete!");
}

void loop()
{
  if (Blynk.connected()) 
  {
    Blynk.run();
  }
  delay(10);
}
