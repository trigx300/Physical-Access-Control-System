
const byte ROWS = 4;
const byte COLS = 3;
char keys[ROWS][COLS] = {
  { '1', '2', '3' },
  { '4', '5', '6' },
  { '7', '8', '9' },
  { '*', '0', '#' }
};

byte rowPins[ROWS] = { 26, 33, 14, 13 };  //keypad row pins 26.33.14.13
byte colPins[COLS] = { 27, 32, 12 };      //keypad column pins 27.32.12

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);



#define LED_PIN 16
#define NUM_LEDS 1
Adafruit_NeoPixel pixels = Adafruit_NeoPixel(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

#define DOOR_STRIKE_PIN 22

String enteredCode = "";

WiFiUDP udp;
EasyNTPClient ntpClient(udp, "pool.ntp.org", ((-6 * 60 * 60) + (00)));  // UTC-7:00 for Utah time


#define FORMAT_SPIFFS_IF_FAILED true
const int localTimeZoneOffset = -21600;  // adjust for your own time zone
const char* CONFIG_FILE = "/config.json";

struct Code {
  String value;
  String expires;

  Code() {}

  Code(String value) {
    this->value = value;
  }

  Code(String value, String expires) {
    this->value = value;
    this->expires = expires;
  }
};

const int MAX_CODES = 10;

struct Config {
  Code codes[MAX_CODES];
  int numCodes = 0;
  String user_id;
  String user_password;

  void changeUser(String uID, String pass) {
    user_id=uID;
    user_password=pass;
  }

  void addCode(String value, String expires = "") {
    if (numCodes < MAX_CODES) {
      codes[numCodes] = Code(value, expires);
      numCodes++;
    }
  }

  void removeCode(int index) {
    if (index >= 0 && index < numCodes) {
      for (int i = index; i < numCodes - 1; i++) {
        codes[i] = codes[i + 1];
      }
      numCodes--;
    }
  }
};

Config config;

WebServer server(80);

/*
 * CSS Syles
 */
const char* cssStyles ="<style>"
  "body {font-family: Arial, sans-serif; background-color: #f2f2f2; padding: 20px; font-size: x-large;} "
  "h1 {color: #005580; margin-bottom: 30px;}"
  "h2 {color: #0099cc; margin-bottom: 20px;}"
  "ul {list-style-type: none; margin: 0; padding: 0;}" 
  "li {padding: 10px; margin-bottom: 5px; background-color: #fff; border-radius: 5px; box-shadow: 2px 2px 5px #ccc; display: flex; justify-content: space-between; align-items: center;}"
  "li:hover {background-color: #f2f2f2;}"
  "label {display: block; margin-bottom: 5px;}"
  "input[type=text], input[type=datetime-local] {padding: 5px; border-radius: 5px; border: 1px solid #ccc; font-size: 16px; width: 200px;}"
  "input[type=submit], button {padding: 5px; border-radius: 5px; border: none; background-color: #0099cc; color: #fff; font-size: 16px; cursor: pointer; margin-top: 10px;}"
  ".dropdown {position: relative; display: inline-block; margin-left: auto;}"
  ".dropdown-content {display: none; position: absolute; top: 30px; right: 0; z-index: 1; background-color: #fff; border-radius: 5px; box-shadow: 2px 2px 5px #ccc;}"
  ".dropdown:hover .dropdown-content {display: block;}"
  ".dropdown-content a {color: #005580; padding: 12px 16px; text-decoration: none; display: block;}"
  ".dropdown-content a:hover {background-color: #f2f2f2;}"
"</style>";

const char* menuHtml = "<div class='dropdown' style='float: right;'>"
  "<button class='dropbtn'>Menu</button>"
  "<div class='dropdown-content'>"
  "<a href='/'>Home</a>"
  "<a href='/open'>Open Door</a>"
  "<a href='/settings'>Settings</a>"
  "<a href='/firmware'>Update firmware</a>"
  "<a href='/reset'>Restart chip</a>"
  "<a href='/clear'>Format Memory</a>"
  "</div>"
  "</div>";
/*
 * Server Index Page
 */

const char* serverIndex =
"<script src='https://ajax.googleapis.com/ajax/libs/jquery/3.2.1/jquery.min.js'></script>"
"<form method='POST' action='/firmware' enctype='multipart/form-data' id='upload_form'>"
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
  "console.log('success!');                                                                                                                                               "
  "document.getElementById('back-button').hidden = false;"
 "},"
 "error: function (a, b, c) {"
 "}"
 "});"
 "});"
 "</script>";

// This function sets the color of an LED and shows the color on a pixel display
void setLED(int r, int g, int b) {
  pixels.setPixelColor(0, pixels.Color(r, g, b)); // set the color of the pixel to (r,g,b) at index 0
  pixels.show(); // update the pixel display with the new color
}

// This function unlocks the door, turns on a green LED for 3 seconds and then turns on a red LED and locks the door.
void unlockDoor() {
  digitalWrite(DOOR_STRIKE_PIN, HIGH); // activate the door strike to unlock the door
  setLED(0, 255, 0);  // turn on green LED to indicate that the door is unlocked
  delay(3000);        // wait 3 seconds before locking the door
  setLED(255, 0, 0);  // turn on red LED to indicate that the door is locked
  digitalWrite(DOOR_STRIKE_PIN, LOW); // deactivate the door strike to lock the door
}

// This function checks if a given code is valid or not by comparing it to a list of valid codes in the configuration.
bool isCodeValid(String enteredCode) {
  for (int i = 0; i < config.numCodes; i++) { // iterate through each code in the configuration
    Serial.print("Checking code ");
    Serial.print(i);
    Serial.print(": ");
    Serial.println(config.codes[i].value); // print the code being checked

    if (config.codes[i].value == enteredCode) { // if the entered code matches a valid code in the configuration, return true
      Serial.println("Valid code found!");
      return true;
    }
  }
  Serial.println("No valid code found."); // if no valid code is found, return false
  return false;
}


// This function reads input from a keypad and performs actions based on the input.
void readKeypad() {
  char key = keypad.getKey(); // read input from the keypad

  if (key) { // if a key is pressed
    if (key == '#') { // if the key pressed is the pound symbol
      Serial.print("Entered code: ");
      Serial.println(enteredCode); // print the entered code to the serial monitor

      if (isCodeValid(enteredCode)) { // check if the entered code is valid
        unlockDoor(); // if it is, unlock the door
      }else{        
          setLED(255, 155, 0);  // turn on yellow LED to indicate an invalid code
          delay(100); // delay for 100 milliseconds
          setLED(255, 0, 0);  // turn on red LED to indicate an invalid code
          delay(100); // delay for 100 milliseconds
          setLED(255, 155, 0);  // turn on yellow LED to indicate an invalid code
          delay(100); // delay for 100 milliseconds
          setLED(255, 0, 0);  // turn on red LED to indicate an invalid code
      }      
      enteredCode = ""; // clear the entered code
    } else if (key == '*') { // if the key pressed is the asterisk symbol
      enteredCode = ""; // clear the entered code
    } else { // if any other key is pressed
      enteredCode += key; // add the key to the entered code
    }
  }
}

// This function loads a configuration file from SPIFFS (a file system for embedded devices).
void loadConfig() {
// Check if the configuration file exists.
if (SPIFFS.exists(CONFIG_FILE)) {
// Open the configuration file for reading.
File configFile = SPIFFS.open(CONFIG_FILE, "r");
if (configFile) {
// Read the entire content of the configuration file into a buffer.
size_t size = configFile.size();
std::unique_ptr<char[]> buf(new char[size]);
configFile.readBytes(buf.get(), size);
configFile.close();

  // Parse the buffer into a JSON document.
  DynamicJsonDocument doc(1024);
  DeserializationError error = deserializeJson(doc, buf.get());
  if (!error) {
    // Extract the codes array from the JSON document.
    JsonArray codes = doc["codes"];
    // Iterate over each element of the codes array.
    for (JsonVariant code : codes) {
      if (code.is<String>()) {
        // This is a code without expiration.
        // Extract the code value as a string and add it to the configuration object.
        String codeValue = code.as<String>();
        config.codes[config.numCodes++] = Code(codeValue);
      } else if (code.is<JsonObject>()) {
        // This is a code with expiration.
        // Extract the code value and expiration date from the JSON object and add them to the configuration object.
        JsonObject codeObj = code.as<JsonObject>();
        String codeValue = codeObj["value"].as<String>();
        String expiresStr = codeObj["expires"].as<String>();
        time_t expires = 0;
        if (expiresStr != "") {
          // Convert the expiration date string to a time_t value.
          struct tm tm;
          strptime(expiresStr.c_str(), "%Y-%m-%dT%H:%M", &tm);
          expires = mktime(&tm);
        }
        config.codes[config.numCodes++] = Code(codeValue, expiresStr);
      }
    }
    // Extract the user_id and user_password fields from the JSON document and add them to the configuration object if they exist.
    if (doc.containsKey("user_id")) {
      config.user_id = doc["user_id"].as<String>();
    }

    if (doc.containsKey("user_password")) {
      config.user_password = doc["user_password"].as<String>();
    }

  }
}
}
}

// This function takes a date and time string in the format "YYYY-MM-DDThh:mm" and validates it.
bool validateDateTime(String dateTimeStr) {
// Extract the year, month, day, hour, and minute from the date and time string using the substring method and convert them to integers using the toInt method.
int year = dateTimeStr.substring(0, 4).toInt();
int month = dateTimeStr.substring(5, 7).toInt();
int day = dateTimeStr.substring(8, 10).toInt();
int hour = dateTimeStr.substring(11, 13).toInt();
int minute = dateTimeStr.substring(14, 16).toInt();
// Check if the year, month, day, hour, and minute values are valid. If any value is invalid, return false.
if (year < 2021 || month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 || hour > 23 || minute < 0 || minute > 59) {
return false;
}
// If all values are valid, return true.
return true;
}

// This function saves the configuration object to a file in SPIFFS in JSON format.
void saveConfig() {
// Create a new JSON document with a capacity of 16384 bytes.
DynamicJsonDocument doc(16384);
// Create a new JSON array to hold the codes in the configuration object.
JsonArray codes = doc.createNestedArray("codes");
// Iterate over each code in the configuration object and add it to the JSON array.
for (int i = 0; i < config.numCodes; i++) {
JsonObject code = codes.createNestedObject();
code["value"] = config.codes[i].value;
code["expires"] = config.codes[i].expires;
}
// Add the user ID and password to the JSON document.
doc["user_id"] = config.user_id;
doc["user_password"] = config.user_password;
// Open the configuration file for writing.
File configFile = SPIFFS.open(CONFIG_FILE, "w");
if (configFile) {
// Serialize the JSON document to a string and write it to the configuration file.
serializeJson(doc, configFile);
configFile.close();
}
}

// This function checks if a string contains only numeric characters.
bool isNumeric(String str) {
// Iterate over each character in the string and check if it is a digit. If any character is not a digit, return false.
for (int i = 0; i < str.length(); i++) {
if (!isdigit(str.charAt(i))) {
return false;
}
}
// If all characters are digits, return true.
return true;
}

//-------------

void handleAddCode() {
  if (!server.authenticate(config.user_id.c_str(), config.user_password.c_str()))  {
    server.requestAuthentication();
    return;
  }
  String codeValue = server.arg("addCode");
  String expiresValue = server.arg("expires");
  if (codeValue.length() != 4 || !isNumeric(codeValue)) {
    server.send(400, "text/plain", "Invalid code format. Code must be a 4-digit number.");
    return;
  }
  for (int i = 0; i < config.numCodes; i++) {
    if (config.codes[i].value == codeValue) {
      String html = "<html><head><title>This code already exists.</title>";
      html += cssStyles;
      html +="</head><body><h1>This code already exists.</h1>";
      html += "<form action='/' method='POST'>";
      html += "<input type='submit' value='OK'></form>";
      html += "</body></html>";

      server.send(200, "text/html", html);
      return;
    }
  }
  if (expiresValue != "" && !validateDateTime(expiresValue)) {
      // fancy success messagge
      String html = "<html><head><title>Invalid expiration</title>";
      html += cssStyles;  
      html +="</head><body><p>Invalid expiration date and time format. Please use YYYY-MM-DDTHH:MM format.</p>";
      html += "<form action='/' method='POST'>";
      html += "<input type='submit' value='OK'></form>";
      html += "</body></html>";

      server.send(200, "text/html", html);
    return;
  }
  Code code;
  code.value = codeValue;
  code.expires = expiresValue;
  config.codes[config.numCodes++] = code;
  saveConfig();
  // fancy success messagge
  String html = "<html><head><title>Code added to list!</title>";
  html += cssStyles;
  html +="</head><body><h1>Code added to list!</h1>";
  html += "<form action='/' method='POST'>";
  html += "<input type='submit' value='OK'></form>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

void removeExpiredCodes() {
  // Get the current timestamp from the NTP client
  time_t now = ntpClient.getUnixTime();
  // Convert to local time
  struct tm* localTime = localtime(&now);
  char timestamp[80];
  strftime(timestamp, 80, "%Y-%m-%dT%H:%M", localTime);

  for (int i = 0; i < config.numCodes; i++) {
    Code& code = config.codes[i];
    if (code.expires != "") {
      struct tm tm;
      strptime(code.expires.c_str(), "%Y-%m-%dT%H:%M", &tm);
      time_t expires = mktime(&tm);
      time_t current = mktime(localTime);
      if (expires <= current) {
        config.removeCode(i);
        saveConfig();
        Serial.print("now: ");
        Serial.print(current);
        Serial.print(" - expires: ");
        Serial.println(expires);
        i--;
      }
    }
  }
}

void handleRemoveCode() {
  if (!server.authenticate(config.user_id.c_str(), config.user_password.c_str()))  {
    server.requestAuthentication();
    return;
  }
  String codeValue = server.arg("removeCode");
  bool removed = false;
  for (int i = 0; i < config.numCodes; i++) {
    if (config.codes[i].value == codeValue) {
      for (int j = i; j < config.numCodes - 1; j++) {
        config.codes[j] = config.codes[j + 1];
      }
      config.numCodes--;
      removed = true;
      break;
    }
  }
  if (!removed) {
    // fancy messagge
    String html = "<html><head><title>Code not found!</title>";
    html += cssStyles;
    html +="</head><body><h1>Code not found!</h1>";
    html += "<form action='/' method='POST'>";
    html += "<input type='submit' value='OK'></form>";
    html += "</body></html>";

    server.send(200, "text/html", html);
    return;
  }
  saveConfig();
  // fancy success messagge
  String html = "<html><head><title>Code removed!</title>";
  html += cssStyles;
  html +="</head><body><h1>Code removed!</h1>";
  html += "<form action='/' method='POST'>";
  html += "<input type='submit' value='OK'></form>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

void handleIndex() {
    if (!server.authenticate(config.user_id.c_str(), config.user_password.c_str()))  {
    server.requestAuthentication();
    return;
  }
  // Get the current timestamp from the NTP client
  time_t now = ntpClient.getUnixTime();

  // Convert to local time
  struct tm* localTime = localtime(&now);
  char timestamp[80];
  strftime(timestamp, 80, "%Y-%m-%d %H:%M:%S", localTime);
  String html = "<html><head><title>Access Controls</title>";
  html += cssStyles;
  html += "</head><body>";
  html += menuHtml;
  html += "<h1>Access Controls</h1><h3>"+ String(timestamp) + "</h3>";
  html += "<div style='vertical-align: top; display: inline-block; margin:0 50px;'><h2>Current Codes:</h2><ul>";
  for (int i = 0; i < config.numCodes; i++) {
    Code code = config.codes[i];
    String expires = "";
    if (!code.expires.isEmpty()) {
      expires = " (expires " + code.expires.substring(0, 10) + " " + code.expires.substring(11, 16) + ")";
    }
    html += "<li><span>" + code.value + expires + "</span><a href='/?removeCode=" + code.value + "'>Remove</a></li>";
  }

  // Add the code management forms to the HTML string
  html += "</ul></div><div style='display: inline-block'><h2>Add Code:</h2><form method='get'><label for='addCode'>Code:</label><input type='text' id='addCode' name='addCode' pattern='\\d{4}' required><br><label for='expires'>Expires (optional):</label><input type='datetime-local' id='expires' name='expires'><br><input type='submit' value='Add'></form><h2>Remove Code:</h2><form method='get'><label for='removeCode'>Code:</label><input type='text' id='removeCode' name='removeCode' pattern='\\d{4}' required><br><input type='submit' value='Remove'></form></div></br></body></html>";

  server.send(200, "text/html", html);
}

void handleFirmware() {
  
    String html = "<html><head><title>Firmware update</title>";
    html += cssStyles;
    html += menuHtml;
    html += "<h1>Firmware update</h1>";
    html += serverIndex;
    html += "</br><button id='back-button' hidden='true' onclick=\"window.location.href='/';\">Back</button>";
    server.send(200, "text/html", html);
  
}

void handleStore() {
  if (server.method() == HTTP_POST) {
    String user_id = server.arg("user");
    String user_password = server.arg("password");
    config.changeUser(user_id, user_password);    
    saveConfig();
    Serial.println(config.user_id);
    Serial.println(config.user_password);
        // fancy success messagge
    String html = "<html><head><title>Door unlocked!</title>";
    html += cssStyles;
    html +="</head><body><h1>Stored!</h1>";
    html += "<form action='/' method='POST'>";
    html += "<input type='submit' value='OK'></form>";
    html += "</body></html>";

    server.send(200, "text/html", html);
  }else{
    String html = "<html><head><title>Change Settings</title>";
    html += cssStyles;
    html += menuHtml;
    html += "<h1>Change Settings</h1>";
    html += "<form method='POST' action='#'>";
    html += "<label for='user'>User name:</label>";
    html += "<input type='text' name='user' value='" + config.user_id + "'><br><br>";
    html += "<label for='password'>Password:</label>";
    html += "<input type='password' name='password' value='" + config.user_password + "'><br><br>";
    html += "<input type='submit' value='Save'></form>";

    server.send(200, "text/html", html);

  }
}

void handleOpen() {
    if (!server.authenticate(config.user_id.c_str(), config.user_password.c_str()))  {
    server.requestAuthentication();
    return;
  }
    // fancy success messagge
    String html = "<html><head><title>Door unlocked!</title>";
    html += cssStyles;
    html +="</head><body><h1>Door unlocked!</h1>";
    html += "<form action='/' method='POST'>";
    html += "<input type='submit' value='OK'></form>";
    html += "</body></html>";

  server.send(200, "text/html", html);
  unlockDoor();
}

void handleReset() {
    if (!server.authenticate(config.user_id.c_str(), config.user_password.c_str()))  {
    server.requestAuthentication();
    return;
  }
    // fancy success messagge
    String html = "<html><head><title>Reseting complete!</title>";
    html += cssStyles;
    html +="</head><body><h1>Reset complete!</h1>";
    html += "<form action='/' method='POST'>";
    html += "<input type='submit' value='OK'></form>";
    html += "</body></html>";

    server.send(200, "text/html", html);
    delay(1000);
    ESP.restart();
}

void handleMemClear() {
    if (!server.authenticate(config.user_id.c_str(), config.user_password.c_str()))  {
    server.requestAuthentication();
    return;
  }
    // fancy success messagge
    String html = "<html><head><title>Memory cleared!</title>";
    html += cssStyles;



  // This function initializes SPIFFS and erases the entire flash memory.
  if (SPIFFS.format()) {
    html +="</head><body><h1>Memory cleared!</h1>";
    html += "<form action='/' method='POST'>";
    html += "<input type='submit' value='OK'></form>";
    html += "</body></html>";
    server.send(200, "text/html", html);
    delay(1000);
    ESP.restart();
  } else {
    html +="</head><body><h1>ERROR! Memory clear was not complete.</h1>";
    html += "<form action='/' method='POST'>";
    html += "<input type='submit' value='OK'></form>";
    html += "</body></html>";
    server.send(200, "text/html", html);
  }
}

void handleRoot() {
  if (server.hasArg("addCode")) {
    handleAddCode();
  } else if (server.hasArg("removeCode")) {
    handleRemoveCode(); 
  } else {
    handleIndex();
  }
}



