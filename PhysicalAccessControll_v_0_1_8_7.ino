#include <WiFiManager.h>
#include <Update.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <Time.h>
#include <ESPDateTime.h>
#include <TimeLib.h>
#include <EasyNTPClient.h>
#include <WiFiUdp.h>
#include <Keypad.h>
#include <Adafruit_NeoPixel.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include "functions.h"

void setup() {
  Serial.begin(115200);
  if (!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)) {
    Serial.println("SPIFFS Mount Failed");
    return;
  }
  loadConfig();
  WiFiManager wifiManager;
  wifiManager.setConfigPortalTimeout(300);
  wifiManager.autoConnect("AccessControl", "password");
  server.on("/", handleRoot);
  server.on("/firmware", handleFirmware);
  server.on("/settings", handleStore);
  server.on("/open", handleOpen);
  server.on("/reset", handleReset);
  server.on("/clear", handleMemClear);

/*handling uploading firmware file */
  server.on("/serverIndex", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", serverIndex);
  });
  server.on("/update", HTTP_POST, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    delay(1000);
    ESP.restart();
  }, []() {
    HTTPUpload& upload = server.upload();
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
  Serial.println("HTTP server started");
  Serial.println(ntpClient.getUnixTime());
  pinMode(DOOR_STRIKE_PIN, OUTPUT);
  digitalWrite(DOOR_STRIKE_PIN, LOW);
  pixels.begin();
  setLED(255, 0, 0);  // red
    // Start OTA update service
  ArduinoOTA.setHostname("access-control");
  ArduinoOTA.setPassword("password"); // Change this to a secure password
  ArduinoOTA.begin();
  Serial.println("Loaded user ID: " + config.user_id);
  Serial.println("Loaded user password: " + config.user_password);

}

void loop() {
  ArduinoOTA.handle();
  server.handleClient();
  removeExpiredCodes();
  readKeypad();
}