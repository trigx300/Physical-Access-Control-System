#include <WiFiManager.h>          // Include the WiFiManager library for easier WiFi configuration
#include <Update.h>               // Include the Update library for OTA firmware updates
#include <SPIFFS.h>               // Include the SPIFFS library for accessing the ESP's internal file system
#include <ArduinoJson.h>          // Include the ArduinoJson library for parsing and creating JSON data
#include <WebServer.h>            // Include the WebServer library for handling incoming HTTP requests
#include <Time.h>                 // Include the Time library for basic timekeeping functions
#include <ESPDateTime.h>          // Include the ESPDateTime library for more advanced timekeeping functions
#include <TimeLib.h>              // Include the TimeLib library for managing time zones
#include <EasyNTPClient.h>        // Include the EasyNTPClient library for connecting to NTP servers
#include <WiFiUdp.h>              // Include the WiFiUdp library for UDP communication with NTP servers
#include <Keypad.h>               // Include the Keypad library for reading input from a keypad
#include <Adafruit_NeoPixel.h>    // Include the Adafruit_NeoPixel library for controlling NeoPixel LED strips
#include <ESPmDNS.h>              // Include the ESPmDNS library for resolving mDNS hostnames on the local network
#include <ArduinoOTA.h>           // Include the ArduinoOTA library for OTA updates

#include "functions.h"            // Include a custom functions header file

void setup() {
  Serial.begin(115200);          // Start the serial communication at 115200 baud
  if (!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)) {  // Mount the SPIFFS file system, and if it fails, print an error message and return
    Serial.println("SPIFFS Mount Failed");
    return;
  }
  loadConfig();                  // Load the configuration from a JSON file on the file system
  WiFiManager wifiManager;       // Create a WiFiManager object
  wifiManager.setConfigPortalTimeout(300);  // Set the timeout for the WiFiManager access point to 300 seconds
  wifiManager.autoConnect("AccessControl", "password");  // Attempt to connect to a WiFi network, and create an access point if it fails
  server.on("/", handleRoot);    // Handle requests to the root path with the handleRoot function
  server.on("/firmware", handleFirmware);  // Handle requests to the firmware path with the handleFirmware function
  server.on("/settings", handleStore);  // Handle requests to the settings path with the handleStore function
  server.on("/open", handleOpen);  // Handle requests to the open path with the handleOpen function
  server.on("/reset", handleReset);  // Handle requests to the reset path with the handleReset function
  server.on("/clear", handleMemClear);  // Handle requests to the clear path with the handleMemClear function

  // Handle requests to the /serverIndex path with a lambda function
  server.on("/serverIndex", HTTP_GET, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/html", serverIndex);
  });
  
  // Handle firmware update requests with a lambda function
  server.on("/update", HTTP_POST, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    delay(1000);
    ESP.restart();  // Restart the ESP after updating the firmware
  }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {  // If a new firmware file is being uploaded
  Serial.printf("Update: %s\n", upload.filename.c_str());  // Print the name of the firmware file being uploaded
  if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {  // Begin the firmware update with the max available size
    Update.printError(Serial);  // Print any errors that occur during the firmware update
  }
} else if (upload.status == UPLOAD_FILE_WRITE) {
  if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {  // Write the firmware to the ESP
    Update.printError(Serial);  // Print any errors that occur during the firmware update
  }
} else if (upload.status == UPLOAD_FILE_END) {
  if (Update.end(true)) {  // End the firmware update and set the size to the current progress
    Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
  } else {
    Update.printError(Serial);  // Print any errors that occur during the firmware update
  }
}
});

server.begin(); // Start the web server
Serial.println("HTTP server started");
Serial.println(ntpClient.getUnixTime()); // Print the current Unix time
pinMode(DOOR_STRIKE_PIN, OUTPUT); // Set the door strike pin to output mode
digitalWrite(DOOR_STRIKE_PIN, LOW); // Turn off the door strike by default
pixels.begin(); // Initialize the NeoPixel LED strip
setLED(255, 0, 0); // Set the LED color to red

// Set up the OTA update service
ArduinoOTA.setHostname("access-control"); // Set the hostname for the OTA update service
ArduinoOTA.setPassword("password"); // Set a password for the OTA update service
ArduinoOTA.begin(); // Begin the OTA update service
Serial.println("Loaded user ID: " + config.user_id); // Print the user ID loaded from the configuration file
Serial.println("Loaded user password: " + config.user_password); // Print the user password loaded from the configuration file
}

void loop() {
ArduinoOTA.handle(); // Handle OTA update requests
server.handleClient(); // Handle incoming client requests
removeExpiredCodes(); // Remove expired access codes
readKeypad(); // Read input from the keypad
}
