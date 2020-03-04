#include <Arduino.h>
#include <WiFi.h>
// #include <WiFiMulti.h>
#include <HTTPClient.h>
#include "secrets.h"

// this pin is attached to the signal coming from the smoke detector
int SIGNAL_PIN = 4;
int WAKE_PIN = 33;
int BATTERY_MONITOR_PIN = 35;

#define uS_TO_S_FACTOR 1000000ULL
#define wakeup_seconds 60ULL * 60ULL * 24ULL * 7ULL


const char* serverEndpoint = "http://garage-monitor.herokuapp.com/smokedetector";

// wakeup this often (in micro seconds) to send a friendly handshake message letting us know that
// the device is healthy
const long timerWakeSeconds = 7 * 24 * 60 * 60;

/*
  Method to print the reason by which ESP32
  has been awaken from sleep
  mostly for debugging
*/
void print_wakeup_reason() {
  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch(wakeup_reason)
  {
    case ESP_SLEEP_WAKEUP_EXT0 : Serial.println("Wakeup caused by external signal using RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1 : Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER : Serial.println("Wakeup caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD : Serial.println("Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP : Serial.println("Wakeup caused by ULP program"); break;
    default : Serial.printf("Wakeup was not caused by deep sleep: %d\n",wakeup_reason); break;
  }
}

boolean establishWIFIConnection() {

  for(uint8_t t = 4; t > 0; t--) {
      Serial.printf("[SETUP] WAIT %d...\n", t);
      Serial.flush();
      delay(1000);
  }

  // if we're already connected, just return true
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  Serial.printf("Connecting to %s...\n", essid);

  WiFi.begin(essid, pw);  

  int connectTimeoutSeconds = 20;

  while (WiFi.status() != WL_CONNECTED && connectTimeoutSeconds > 0) {
    connectTimeoutSeconds--;
    Serial.print(".");
    delay(1000);
  }

  return WiFi.status() == WL_CONNECTED;
}


boolean checkAlarm() {
// pinState = digitalRead(pin);
// if (pinState == LOW) {
//    lastLowMillis = millis();
// }

// if (millis() - lastLowMillis > intervalMillis) {
//   // pin has been high for the interval

  // poll the alarm.  If it's high at least once for 3 seconds then assume high
  const int checkDurationMS = 3000;
  const int checkIntervalMS = 100; // so we'll check ten times a second
  
  // start by assuming low
  int alarmState = 0;

  for (int i = 0; i < checkDurationMS; i += 100) {
    if (digitalRead(SIGNAL_PIN) == 1) {
      alarmState = 1;
      break;
    }
    delay(checkIntervalMS);
  }

  return alarmState == 1;
}


void sendMessage(const char* message) {
  Serial.println("POST MESSAGE TO SERVER");
  Serial.println(message);
  HTTPClient http;
  http.begin(serverEndpoint);
  http.addHeader("content-type", "application/json");
  http.addHeader("x-api-token", header);
  String JSONToSend = "{ \"message\" : ";
  JSONToSend += "\"";
  JSONToSend += message;
  JSONToSend += "\" }";
  int responseCode = http.POST(JSONToSend);
  if (responseCode > 0) {
    Serial.printf("[HTTP] code: %d\n", responseCode);

    if(responseCode == HTTP_CODE_OK) {
        String payload = http.getString();
        Serial.println(payload);
    }

  } else {
    Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(responseCode).c_str());
  }
}

void sendAlarmMessage() {
  sendMessage("FIRE!!!");
}

void sendHeartbeatMessage() {

  String batteryMessage = "heartbeat, battery level ";
  batteryMessage += getBatteryVoltage();
  batteryMessage += "V";
  sendMessage(batteryMessage.c_str());
}

void sleep() {
  // set a trigger to wakeup if the alarm pin ever goes high
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_33,1); // 1 = High, 0 = Low

  // also set a timer to wakeup peridically to send heartbeat
  const int setWakupResult = esp_sleep_enable_timer_wakeup(wakeup_seconds * uS_TO_S_FACTOR);
  Serial.print("result of setting wakeup "); Serial.println(setWakupResult);

  Serial.println("Sleeping now");
  delay(1000);
  Serial.flush();
  esp_deep_sleep_start();
}

float getBatteryVoltage() {
  int rawBatteryLevel1 = analogRead(BATTERY_MONITOR_PIN);
  delay(100);
  int rawBatteryLevel2 = analogRead(BATTERY_MONITOR_PIN);
  delay(100);
  int rawBatteryLevel3 = analogRead(BATTERY_MONITOR_PIN);
  delay(100);  
  int rawBatteryLevel4 = analogRead(BATTERY_MONITOR_PIN);
  delay(100);  
  int rawBatteryLevel5 = analogRead(BATTERY_MONITOR_PIN);

  float avgRawBatteryLevel = (rawBatteryLevel1 + rawBatteryLevel2 + rawBatteryLevel3 + rawBatteryLevel4 + rawBatteryLevel5) / 5.0;
  Serial.print("avg raw battery level ");  Serial.println(avgRawBatteryLevel);
  /*
    When you read the ADC you’ll get a value like 2339. The ADC value is a 12-bit number, 
    so the maximum value is 4095 (counting from 0). To convert the ADC integer value to a real voltage you’ll need to divide it by 
    the maximum value of 4095, then double it (note above that Adafruit halves the voltage), 
    then multiply that by the reference voltage of the ESP32 which is 3.3V and then finally, 
    multiply that again by the ADC Reference Voltage of 1100mV
  */
  float calculatedVoltage = avgRawBatteryLevel / 4095.0 * 2.0 * 3.3 * 1.1;

  return calculatedVoltage;  
}

void sendBootMessage() {
  String bootMessage = "restarted, battery voltage ";
  bootMessage += getBatteryVoltage();
  bootMessage += "V";
  sendMessage(bootMessage.c_str());
}


void setup() {
  Serial.begin(115200);
  delay(1000); // allow some time to open the serial monitor

  pinMode(SIGNAL_PIN, INPUT);  
  pinMode(A13, INPUT);
  pinMode(35, INPUT);

  // Print the wakeup reason for ESP32
  print_wakeup_reason();

  // we were awaken for some reason, check the alarm pin

  // check 6 times every 5 seconds (30 seconds)
  for (int checkCount = 0; checkCount < 6; checkCount++) {
    boolean alarm = checkAlarm(); // this takes at most 3 seconds
  
    if (alarm) {
      // FIRE!  Establish wifi
      Serial.println("FIRE!");

      // establish wifi
      boolean connected = establishWIFIConnection();

      if (connected) {
        Serial.print("Established connection.  Local IP is: "); Serial.println(WiFi.localIP());

        // send message
        sendAlarmMessage();
      } else {
        Serial.println("WiFi connection failed");
      }
    } else if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {
      Serial.println("awoke on a timer");

      // establish wifi
      boolean connected = establishWIFIConnection();

      if (connected) {
        sendHeartbeatMessage();
      } else {
        Serial.println("WiFi connection failed");
      }

      break; // break out of the loop in this case
    } else if (esp_sleep_get_wakeup_cause() == 0){
      Serial.println("wakup caused probably from reboot");
      // establish wifi
      boolean connected = establishWIFIConnection();

      if (connected) {
        sendBootMessage();
      } else {
        Serial.println("WiFi connection failed");
      }
      break;
    } else {
      Serial.println("awoke for an unexpected reason (perhaps loading new code...)");
    }

    delay(5000);
  }

  // nappy time
  sleep();
}

// main loop never runs because we're waking up from deep sleep
void loop() {
  Serial.println("main loop");
  delay(1000);
}