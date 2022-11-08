#define TELEGRAM
#define VERSION "0.0.20"

#include <DHT.h>
#define DHTPIN 2

DHT dht11(DHTPIN, DHT11);
DHT dht21(DHTPIN, DHT21);
float humidity, temp;  // Values read from sensor
float t_add = 0;
float h_add = 0;
bool is_dht11 = true;

#ifdef TELEGRAM
#define SKETCH_VERSION VERSION "tg"
#else
#define SKETCH_VERSION VERSION
#endif

#define WEB_PASSWORD "your password"
#define FIRMWARE_FOLDER "http://192.168.0.43:80/espfw/dht/"

int L0 = 0;
int L1 = 1;

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>

#include <ESP8266WiFiMulti.h>

#include <Arduino.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <Regexp.h>

bool do_restart = false;
bool do_update = false;
String fwfn = "";

#ifdef TELEGRAM
#include <AsyncTelegram2.h>

// Timezone definition
#include <time.h>
#define MYTZ "CET-1CEST,M3.5.0,M10.5.0/3"

BearSSL::WiFiClientSecure clientsec;
BearSSL::Session session;
BearSSL::X509List certificate(telegram_cert);

AsyncTelegram2 myBot(clientsec);

char token[50];
int64_t userid = 123456789;
bool tgavail;

// Name of public channel (your bot must be in admin group)
char* channel = "@YorChannel";
#endif

ESP8266WiFiMulti wifiMulti;

const int numssisds = 5;
const char* ssids[numssisds] = { "WiFi-AP1", "WiFi-AP2", "WiFi-AP3" };
const char* pass = "your wifi password";
const int numpcs = 4;
const char* pcs[numpcs][6] = {
  { "192.168.0.202", "DHT11", "d", "telegram Token 1", "0", "0" },
  { "192.168.0.204", "DHT21", "d", "telegram Token 2", "0", "0" },
};

String ssid;
String device, deviceip;

ESP8266WebServer server(80);

const int led = LED_BUILTIN;
const int pin = 0;  // using GP0 esp8266

String top;

const String bot = "\
    </div>\
  </body>\
</html>";

String postFormRoot;
String postFormUpdate;

void handleRoot() {
  Serial.print("Root request\n");
  digitalWrite(led, 1);
  initpostFormRoot();
  server.send(200, "text/html", top + postFormRoot + bot);
  digitalWrite(led, 0);
}

void handleUpdForm() {
  Serial.print("UpdForm request\n");
  digitalWrite(led, 1);
  initpostFormUpdate();
  server.send(200, "text/html", top + postFormUpdate + bot);
  digitalWrite(led, 0);
}

void update_started() {
  Serial.println("CALLBACK:  HTTP update process started");
}

void update_finished() {
  Serial.println("CALLBACK:  HTTP update process finished");
}

void update_progress(int cur, int total) {
  Serial.printf("CALLBACK:  HTTP update process at %d of %d bytes...\n", cur, total);
}

void update_error(int err) {
  Serial.printf("CALLBACK:  HTTP update fatal error code %d\n", err);
}

void (*restartFunc)(void) = 0;  //declare restart function @ address 0

#ifdef TELEGRAM
void tgChannelSend(String s) {
  if (WiFi.status() == WL_CONNECTED && tgavail) {
    String message;
    message += "@";
    message += myBot.getBotName();
    message += " (";
    message += device;
    message += "):\n";
    message += s;
    Serial.println(message);
    myBot.sendToChannel(channel, message, true);
  }
}
#endif


void doRestart() {
  do_restart = false;
#ifdef TELEGRAM
  if (WiFi.status() == WL_CONNECTED) {
    tgChannelSend("Restarting...");
    // Wait until bot synced with telegram to prevent cyclic reboot
    while (WiFi.status() == WL_CONNECTED && !myBot.noNewMessage()) {
      Serial.print(".");
      delay(100);
    }
    Serial.println("Restart in 5 seconds...");
    delay(5000);
  }
#endif
  ESP.restart();
}

void doUpdate() {
  do_update = false;
#ifdef TELEGRAM
  if (WiFi.status() == WL_CONNECTED) {
    tgChannelSend("Updating...");
    // Wait until bot synced with telegram to prevent cyclic reboot
    while (WiFi.status() == WL_CONNECTED && !myBot.noNewMessage()) {
      Serial.print(".");
      delay(100);
    }
    Serial.println("Update in 5 seconds...");
    delay(5000);
  }
#endif
  update(fwfn);
}


void updateOtherDevice(String devip, String firmware) {
  if (WiFi.status() == WL_CONNECTED) {

    WiFiClient client;
    HTTPClient http;

    String url = "http://" + devip + "/remote/" + "?pswupd=" + WEB_PASSWORD + "&firmware=" + firmware;
    http.begin(client, url);  //HTTP
    http.addHeader("Content-Type", "text/html");

    //Serial.println(url);

    int httpCode = http.GET();

    //Serial.println(httpCode);

    // httpCode will be negative on error
    if (httpCode > 0) {
      // HTTP header has been send and Server response header has been handled
      //Serial.printf("UPDATE %s: %d\n", devip, httpCode);

      // file found at server
      if (httpCode == HTTP_CODE_OK) {
        const String& payload = http.getString();
        //Serial.println("received payload:\n<<");
        Serial.println(payload);
        //Serial.println(">>");
      }
    } else {
      Serial.printf("UPDATE %s failed, error: %s\n",
                    devip,
                    http.errorToString(httpCode).c_str());
    }

    http.end();
  }
}

int iListIndex = -1;

void updateOthers(String firmware) {
  for (int i = 0; i < numpcs; i++) {
    if (i != iListIndex) {
      Serial.println(deviceip + " is sending update request to " + pcs[i][0]);
      updateOtherDevice(pcs[i][0], firmware);
    }
  }
};

void update(String firmware) {
  do_update = false;
  if (WiFi.status() == WL_CONNECTED) {

    Serial.print("Update with ");
    Serial.print(firmware);
    Serial.println("...");

    WiFiClient client;

    ESPhttpUpdate.setLedPin(LED_BUILTIN, LOW);

    ESPhttpUpdate.onStart(update_started);
    ESPhttpUpdate.onEnd(update_finished);
    ESPhttpUpdate.onProgress(update_progress);
    ESPhttpUpdate.onError(update_error);

#ifdef TELEGRAM
    tgChannelSend(firmware);
#endif

    t_httpUpdate_return ret = ESPhttpUpdate.update(client, FIRMWARE_FOLDER + firmware);

    switch (ret) {
      case HTTP_UPDATE_FAILED:
#ifdef TELEGRAM
        tgChannelSend(ESPhttpUpdate.getLastErrorString().c_str());
#else
        Serial.printf("HTTP_UPDATE_FAILD Error (%d): %s\n", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
#endif
        do_restart = true;
        break;

      case HTTP_UPDATE_NO_UPDATES:
        Serial.println("HTTP_UPDATE_NO_UPDATES");
        do_restart = true;
        break;

      case HTTP_UPDATE_OK:
#ifdef TELEGRAM
        tgChannelSend("HTTP_UPDATE_OK");
#else
        Serial.println("HTTP_UPDATE_OK");
#endif
        break;
    }
  } else {
    do_restart = true;
  }
}

void handleRemote() {
  digitalWrite(led, 1);
  server.send(200, "text/plain", "- OK -");
  if (server.arg("pswupd").equals(WEB_PASSWORD)) {
    update(server.arg("firmware"));
  };
  digitalWrite(led, 0);
}

void handleUpdate() {
  if (server.method() != HTTP_POST) {
    digitalWrite(led, 1);
    server.send(405, "text/plain", "Method Not Allowed");
    digitalWrite(led, 0);
  } else {
    digitalWrite(led, 1);
    char buffer[40];
    String message = top;

    //message += server.arg("plain");

    message += "<table><td>";

    bool pswcorrect = server.arg("pswupd").equals(WEB_PASSWORD);
    if (pswcorrect) {
      //message += "<p style=\"background-color:MediumSeaGreen;\">SUCCESS</p>";

      if (server.arg("firmware").equals("restart")) {
        message += "<p>Restarting...</p>";
      } else {

        if (server.arg("alldev").equals("yes")) {
          message += "<p>UPDATE ALL DEVICES</p>";
          updateOthers(server.arg("firmware"));
        } else {
          message += "<p>UPDATE STARTED</p>";
        }

        message += server.arg("firmware");
        message += "</td></table>";
        message += bot;
        server.send(200, "text/html", message);
        digitalWrite(led, 0);
        update(server.arg("firmware"));
      }

    } else {
      message += "<p style=\"background-color:Tomato;\">FAIL</p>";
    }

    message += "</td></table>";
    message += bot;
    server.send(200, "text/html", message);
    digitalWrite(led, 0);

    if (pswcorrect && server.arg(1).equals("restart")) {
      do_restart = true;
    }
  }
}

void handleNotFound() {
  digitalWrite(led, 1);
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
  digitalWrite(led, 0);
}

// WiFi connect timeout per AP. Increase when connecting takes longer.
const uint32_t connectTimeoutMs = 5000;

// called for each match
void match_callback(const char* match,          // matching string (not null-terminated)
                    const unsigned int length,  // length of matching string
                    const MatchState& ms)       // MatchState in use (to get captures)
{
  char cap[100];  // must be large enough to hold captures

  for (byte i = 0; i < ms.level; i++) {
    ms.GetCapture(cap, i);
    if (strcmp(cap, "../")) {
      //Serial.println(cap);
      String s(cap);
      if (s.endsWith(".bin")) {
        postFormUpdate += "<option value=\"" + s + "\">" + s + "</option>";
      }
    }

  }  // end of for each capture

}  // end of match_callback

// Generally, you should use "unsigned long" for variables that hold time
unsigned long previousMillis = 0;  // will store last temp was read
const long interval = 2000;        // interval at which to read sensor

void gettemperature() {
  // Wait at least 2 seconds seconds between measurements.
  // if the difference between the current time and last time you read
  // the sensor is bigger than the interval you set, read the sensor
  // Works better than delay for things happening elsewhere also
  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis >= interval) {
    // save the last time you read the sensor
    previousMillis = currentMillis;

    // Reading temperature for humidity takes about 250 milliseconds!
    // Sensor readings may also be up to 2 seconds 'old' (it's a very slow sensor)
    // Check if any reads failed and exit early (to try again).
    for (int i = 0; i < 10; i++) {
      if (is_dht11) {
        humidity = dht11.readHumidity();      // Read humidity (percent)
        temp = dht11.readTemperature(false);  // Read temperature as Fahrenheit
      } else {
        humidity = dht21.readHumidity();      // Read humidity (percent)
        temp = dht21.readTemperature(false);  // Read temperature as Fahrenheit
      }

      if (isnan(humidity) || isnan(temp) || temp > 1000 || humidity > 1000) {
        delay(500);
      } else {
        humidity += h_add;
        temp += t_add;
        return;
      }
    }
    humidity = 0;
    temp = 0;
  }
}

void initpostFormRoot(void) {
  String butLbl = "Submit", color = "green";

  gettemperature();

  char buff[60];

  sprintf(buff, "Humidity = %.0f%s, Temperature = %.1f&#176;C<br>", humidity, "%", temp);
  postFormRoot = String(buff) + "<form method=\"post\" enctype=\"application/x-www-form-urlencoded\" action=\"/switch/\">\
      <table>\
      <tr><td><label for=\"password\">Password: </label></td>\
      <td><input type=\"password\" id=\"password\" name=\"password\" value=\"\" required></td></tr>\
      <tr><td><span class=\"dot"
                 + color + "\"></span></td>\
      <td align=\"right\"><input type=\"submit\" value=\""
                 + butLbl + "\"></td></tr>\
      </table>\
      </form>\
      <a href=\"/updform/\">Update Firmware</a>";
}

void initpostFormUpdate(void) {
  postFormUpdate = "<form method=\"post\" enctype=\"application/x-www-form-urlencoded\" action=\"/update/\">\
      <table>\
      <tr><td><label for=\"pswupd\">Password: </label></td>\
      <td><input type=\"password\" id=\"pswupd\" name=\"pswupd\" value=\"\" required></td></tr>\
      <tr><td><label for=\"firmware\">Firmware: </label></td> \
      <td align=\"right\"><select id=\"firmware\" name=\"firmware\">\
      <option value=\"restart\">Restart</option>";

  WiFiClient client;
  HTTPClient http;

  //Serial.print("[HTTP] begin...\n");
  if (http.begin(client, FIRMWARE_FOLDER)) {  // HTTP


    //Serial.print("[HTTP] GET...\n");
    // start connection and send HTTP header
    int httpCode = http.GET();

    // httpCode will be negative on error
    if (httpCode > 0) {
      // HTTP header has been send and Server response header has been handled
      //Serial.printf("[HTTP] GET... code: %d\n", httpCode);

      // file found at server
      if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
        String payload = http.getString();
        //Serial.println(payload);

        unsigned long count;

        int payload_len = payload.length() + 1;
        char c_payload[payload_len];
        payload.toCharArray(c_payload, payload_len);

        // match state object
        MatchState ms(c_payload);

        count = ms.GlobalMatch("href=['\"]?([^'\" >]+)", match_callback);
      }
    } else {
      Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
    }

    http.end();
  } else {
    Serial.printf("[HTTP} Unable to connect\n");
  }

  postFormUpdate += "</select></td></tr>\
    <tr><td><label for=\"alldev\">All devices: </label></td>\
    <td><input type=\"checkbox\" id=\"alldev\" name=\"alldev\" value=\"yes\"></td></tr> \
    <tr><td></td><td align=\"right\"><input type=\"submit\" value=\"Restart/Update\"></td></tr>\
    </table>\
    </form>\
    <a href=\"/\">Switcher</a>";
}

void setup(void) {
  WiFi.persistent(false);

  pinMode(led, OUTPUT);
  digitalWrite(led, 0);
  Serial.begin(115200);

  Serial.print("Version: " SKETCH_VERSION "\n");

  // Set WiFi to station mode
  WiFi.mode(WIFI_STA);

  // Register multi WiFi networks
  for (int i = 0; i < numssisds; i++) {
    //Serial.println(ssids[i]);
    wifiMulti.addAP(ssids[i], pass);
  }

  Serial.println("Connecting...");
  if (wifiMulti.run(connectTimeoutMs) == WL_CONNECTED) {
    ssid = WiFi.SSID();
    deviceip = WiFi.localIP().toString();
    device = "Unknown";
    Serial.println(String("Connected: ") + ssid + " - " + deviceip);

    for (int i = 0; i < numpcs; i++) {
      if (deviceip.equals(pcs[i][0])) {
        iListIndex = i;
        device = pcs[i][1];
        is_dht11 = pcs[i][6] == "11";
        t_add = String(pcs[i][4]).toFloat();
        h_add = String(pcs[i][5]).toFloat();
        if (pcs[i][2] == "r") {
          L0 = 1;
          L1 = 0;
        }
        pinMode(pin, OUTPUT);
        digitalWrite(pin, L1);
        digitalWrite(pin, L0);

#ifdef TELEGRAM
        tgavail = strlen(pcs[i][3]) > 0;
        if (tgavail) {
          String(pcs[i][3]).toCharArray(token, strlen(pcs[i][3]) + 1);
        }
#endif

        if (is_dht11) {
          dht11.begin();
        } else {
          dht21.begin();
        }

        break;
      }
    }

    top = "<head>\
        <title>"
          + device + "</title>\
        <style>\
          body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }\
          table {\
            border: 1px solid #4CAF50;\
            border-radius: 5px;\
            padding: 20px;\
            margin-top: 10px;\
            margin-bottom: 10px;\
            margin-right: 10px;\
            margin-left: 30px;\
            background-color: lightblue;\
          }\
          .dotgray {\
            height: 20px;\
            width: 20px;\
            background-color: #bbb;\
            border-radius: 50%;\
            border: 1px solid #4CAF50;\
            display: inline-block;\
          }\
          .dotred {\
            height: 20px;\
            width: 20px;\
            background-color: red;\
            border-radius: 50%;\
            border: 1px solid #4CAF50;\
            display: inline-block;\
          }\
        </style>\
      </head>\
      <body>\
        <h1>"
          + device + "</h1>DHT" + pcs[iListIndex][6] + "<br>" + ssid + "<br><small>" SKETCH_VERSION "</small><br>\
        <div>";

    server.on("/", handleRoot);
    server.on("/updform/", handleUpdForm);
    server.on("/update/", handleUpdate);
    server.on("/remote/", handleRemote);
    server.onNotFound(handleNotFound);
    server.begin();

#ifdef TELEGRAM
    if (tgavail) {
      // Sync time with NTP, to check properly Telegram certificate
      configTime(MYTZ, "time.google.com", "time.windows.com", "pool.ntp.org");
      //Set certficate, session and some other base client properies
      clientsec.setSession(&session);
      clientsec.setTrustAnchors(&certificate);
      clientsec.setBufferSizes(1024, 1024);
      // Set the Telegram bot properies
      myBot.setUpdateTime(2000);
      myBot.setTelegramToken(token);

      // Check if all things are ok
      Serial.print("\nTest Telegram connection... ");
      myBot.begin() ? Serial.println("OK") : Serial.println("NOK");

      //char welcome_msg[128];
      //snprintf(welcome_msg, 128, "%s\nBOT @%s online", device, myBot.getBotName());

      // Send a message to specific user who has started your bot
      //myBot.sendTo(userid, welcome_msg);
      tgChannelSend(SKETCH_VERSION " Online " + deviceip + " via " + ssid);
    }
#endif

  } else {
    Serial.println("Can't connect to Network");
    delay(5000);
    Serial.println("Restart...");
    do_restart = true;
  }
}

void loop(void) {
  if (do_restart)
    doRestart();

  if (do_update)
    doUpdate();

  if (WiFi.status() == WL_CONNECTED) {
    server.handleClient();

#ifdef TELEGRAM
    if (tgavail) {
      // local variable to store telegram message data
      TBMessage msg;

      // if there is an incoming message...
      if (myBot.getNewMessage(msg)) {

        int l = msg.text.length() + 1;
        char buf[l];
        msg.text.toCharArray(buf, l);

        char* command = strtok(buf, " ");
        String s = "";
        if (strcmp(command, "dht") == 0) {
          gettemperature();
          s = String("humidity: ") + String((int)humidity) + "%,\n";
          s += String("temperature: ") + String(temp) + "&#176;C\n";
          tgChannelSend(s);
        } else if (strcmp(command, "restart") == 0) {
          s = "Restarting...";
          do_restart = true;
        } else if (strcmp(command, "update") == 0) {
          s = "Updating...";
          fwfn = strtok(NULL, " ");
          do_update = fwfn != "";
          if (!do_update) {
            s = "Second param 'File name' is needed";
          }
        } else if (strcmp(command, "updateall") == 0) {
          s = "Updating all...";
          fwfn = strtok(NULL, " ");
          updateOthers(fwfn);
          do_update = true;
        } else {
          s = String("Unknown: ") + command;
        }
        myBot.sendMessage(msg, s);
      }
    }
#endif
  } else {
    Serial.println("Connection lost. Restart...");
    //restartFunc();
    do_restart = true;
  }
}