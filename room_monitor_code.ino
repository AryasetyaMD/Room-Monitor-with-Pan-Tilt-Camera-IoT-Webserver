#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include "WiFi.h"
#include <HTTPClient.h>
#include "time.h"

#define DHTPIN 2
#define DHTTYPE DHT22
#define RELAY_PIN 18
#define BUZZER_PIN 13
#define LED_PIN 27
#define MQ135_PIN 35
#define THRESHOLD_HUMIDITY 80
#define THRESHOLD_ANALOG_MQ135 20

int done = 0; // Variable for blinking once
int drop = 0; // Variable for blinking once
int count = 1; // Variable for counting how much data is registered since the device is started
float temperature;
float humidity;
int air;
String relayStatus = "OFF";
String airStatus = "OK";

const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 25200; // Timezone Indonesia
const int daylightOffset_sec = 0;
const char* ssid = "Galaxy A72 39CD"; // Wi-Fi Name
const char* password = "okhf5496"; // Wi-Fi Password
String GOOGLE_SCRIPT_ID = "AKfycbz6_-GkmX5AoiZluVJVEQXK3oYQYcli434GWaeuxbbCal7k2ttQn2ZaRrLvm8fE3vQ"; // ID Google Script

DHT dht(DHTPIN, DHTTYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2);

void send_data_to_server(void *parameter);
void local_data_read_write(void *parameter);

// Turn on buzzer once
void beepOnce() {
  if (done == 0 && drop == 1) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(200);
    digitalWrite(BUZZER_PIN, LOW);
    done = 1;
    drop = 0;
  }
}

// Blink without delay
void blink(unsigned long currentMillis, unsigned long interval, const int pin) {
  static unsigned long previousMillis = 0;
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    if (digitalRead(pin) == LOW) {
      digitalWrite(pin, HIGH);
    }
    else {
      digitalWrite(pin, LOW);
    }
  }
}

void setup() {
  Serial.begin(115200);
  dht.begin();
  delay(2000);
  Serial.print("Wi-Fi Name: ");
  Serial.println(ssid);
  Serial.println("Connecting to Wi-Fi...");
  Serial.flush();
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("Connected to WiFi");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer); // Start configtime base on ntpServer
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("RELAY: ");
  lcd.setCursor(10, 0);
  lcd.print("AIR:");
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(MQ135_PIN, INPUT);

  // Use multitask to reduce delay and doing task at different times
  // Create task on core 0 for sending data to server 
  xTaskCreatePinnedToCore(
    send_data_to_server,   /* Task function. */
    "send_data_to_server",     /* name of task. */
    10000,       /* Stack size of task */
    NULL,        /* parameter of the task */
    1,           /* priority of the task */
    NULL,      /* Task handle to keep track of created task */
    0            /* pin task to core 0 */
  );                  

  // Create task on core 1 for reading sensor and writting the LED, buzzer, and LCD
  xTaskCreatePinnedToCore(
    local_data_read_write,   /* Task function. */
    "local_data_read_write",     /* name of task. */
    10000,       /* Stack size of task */
    NULL,        /* parameter of the task */
    1,           /* priority of the task */
    NULL,      /* Task handle to keep track of created task */
    1            /* pin task to core 1 */
  );          
}

void send_data_to_server(void *parameter) {
  for(;;) {
    if (WiFi.status() == WL_CONNECTED) {
      static bool flag = false;
      struct tm timeInfo;
      if (!getLocalTime(&timeInfo)) {
        Serial.println("Failed to obtain time information...");
        return;
      }
      char getTime[50];
      strftime(getTime, sizeof(getTime), "%A, %B %d %Y %H:%M:%S", &timeInfo);
      String combinedTime(getTime);
      combinedTime.replace(" ", "-");
      Serial.print("Time: ");
      Serial.println(combinedTime);
      // Send data to Google Script
      String linkURL = "https://script.google.com/macros/s/"+GOOGLE_SCRIPT_ID+"/exec?"+"date=" + combinedTime + "&count=" + String(count) + "&sensor1=" + String(temperature) + "&sensor2=" + String(humidity) + "&airSensor=" + String(air) + "&buzzerAlert=" + String(airStatus)+ "&relayStatus=" + String(relayStatus);
      Serial.print("Sending data to spreadsheet: ");
      Serial.println(linkURL);
      HTTPClient http;
      http.begin(linkURL.c_str());
      http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
      int httpCode = http.GET(); 
      Serial.print("HTTP Status Code: ");
      Serial.println(httpCode);

      String payload;
      if (httpCode > 0) {
          payload = http.getString();
          Serial.println("Payload: "+payload);    
      }
      http.end();
      count++;      
      // Print output to serial monitor
      Serial.println("Data has been sent to the server");
      Serial.print("Temperature: ");
      Serial.println(temperature);
      Serial.print("Humidity: ");
      Serial.print(humidity);
      Serial.println("%");
      Serial.print("Air: ");
      Serial.print(air);
      Serial.println("%");
      Serial.print("Buzzer Alert Status: ");
      Serial.println(airStatus);
      Serial.print("Relay Status: ");
      Serial.println(relayStatus);
      Serial.println("");
      delay(8000);
    }
  }
}

void local_data_read_write(void *parameter) {
  for(;;) {
    if (WiFi.status() == WL_CONNECTED) {
      delay(3000);
      // Sensor
      temperature = dht.readTemperature();
      humidity = dht.readHumidity();
      air = map(analogRead(MQ135_PIN), 0, 4095, 0, 100);

      if (isnan(temperature) || isnan(humidity)) {
        Serial.println("Error reading DHT sensor");
        continue;
      }

      // Local Variable to prevent global variable bug (conflict with other task)
      float local_temp = temperature;
      float local_hum = humidity;
      int local_air = air;

      // Print temperature and humidity to LCD
      lcd.setCursor(1, 1);
      lcd.print(local_temp);
      lcd.print("C  ");
      lcd.setCursor(9, 1);
      lcd.print(local_hum);
      lcd.print("% ");
      unsigned long currentMillis = millis();
      // System Logic
      /* 
      When humidity > threshold, blink LED continously, blink buzzer once, write "ON", trigger relay
      When gas sensor > threshold, blink LED fast, blink buzzer fast, write "ON", trigger relay
      Else, don't do anything, write "OFF"
      */
      if (local_hum < THRESHOLD_HUMIDITY) {
        drop = 1;
      }
      if (local_hum >= THRESHOLD_HUMIDITY && drop == 1) {
        done = 0;
      }
      if (local_hum < THRESHOLD_HUMIDITY && local_air < THRESHOLD_ANALOG_MQ135) {
        digitalWrite(BUZZER_PIN, LOW);
        digitalWrite(RELAY_PIN, HIGH);
        digitalWrite(LED_PIN, HIGH);
      }
      else if (local_hum >= THRESHOLD_HUMIDITY && local_air < THRESHOLD_ANALOG_MQ135) {
        beepOnce();
        digitalWrite(RELAY_PIN, LOW);
        blink(currentMillis, 500, LED_PIN);
      }
      if (local_air >= THRESHOLD_ANALOG_MQ135) {
        digitalWrite(RELAY_PIN, LOW);
        airStatus = "WARNING!";
        lcd.setCursor(14, 0);
        lcd.print("!!");
        for (int i = 0; i < 10; i++) {
          tone(BUZZER_PIN, 1000);
          digitalWrite(LED_PIN, HIGH);
          delay(100);
          digitalWrite(LED_PIN, LOW);
          noTone(BUZZER_PIN);
          delay(100);
        }
      }
      else {
        airStatus = "OK";   
        lcd.setCursor(14, 0);
        lcd.print("OK");        
      }
      if (digitalRead(RELAY_PIN) == LOW) {
        relayStatus = "ON";
        lcd.setCursor(6, 0);
        lcd.print("ON ");
      }
      else {
        relayStatus = "OFF";
        lcd.setCursor(6, 0);
        lcd.print("OFF");
      }
    }
  }
}

void loop() {
}
