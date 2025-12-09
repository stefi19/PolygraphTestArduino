#include <WiFi.h>
#include <WiFiAP.h>
#include <WiFiClient.h>
#include <Adafruit_NeoPixel.h>

/*
 * LED on Carbond D4 - ESP32
 */
#define LED_PIN        12
#define LED_ENABLE_PIN 13
#define LED_COUNT       1

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

// MESSAGE STRINGS
const String SETUP_INIT = "SETUP: Initializing ESP32 dev board";
const String SETUP_ERROR = "!!ERROR!! SETUP: Unable to start SoftAP mode";
const String SETUP_SERVER_START = "SETUP: HTTP server started --> IP addr: ";
const String SETUP_SERVER_PORT = " on port: ";
const String INFO_NEW_CLIENT = "New client connected";
const String INFO_DISCONNECT_CLIENT = "Client disconnected";

// HTTP headers
const String HTTP_HEADER = "HTTP/1.1 200 OK\r\nContent-type:text/html\r\n\r\n";
const String HTML_WELCOME = "<h1>Welcome to your ESP32 Web Server!</h1>";

// BASIC WIFI CONFIGURATION
const char *SSID = "CarbondD4";
const char *PASS = "12345678";
const int HTTP_PORT_NO = 80;

// Server
WiFiServer HttpServer(HTTP_PORT_NO);

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Init NeoPixel LED
  pinMode(LED_ENABLE_PIN, OUTPUT);
  digitalWrite(LED_ENABLE_PIN, LOW);
  strip.begin();
  strip.setBrightness(60);
  strip.clear();
  strip.show();

  Serial.println(SETUP_INIT);

  // Start AP mode
  if (!WiFi.softAP(SSID, PASS)) {
    Serial.println(SETUP_ERROR);

    // RED = error
    strip.setPixelColor(0, strip.Color(255, 0, 0));
    strip.show();

    while (1); // stop execution
  }

  // GREEN = AP active
  strip.setPixelColor(0, strip.Color(0, 255, 0));
  strip.show();

  // Print AP info
  IPAddress accessPointIP = WiFi.softAPIP();
  String info = SETUP_SERVER_START + accessPointIP.toString() +
                SETUP_SERVER_PORT + HTTP_PORT_NO;

  Serial.println(info);

  // Start HTTP server
  HttpServer.begin();
}

void loop() {
  WiFiClient client = HttpServer.available();  // listen for incoming clients

  if (client) {
    Serial.println(INFO_NEW_CLIENT);

    // BLUE = client connected
    strip.setPixelColor(0, strip.Color(0, 0, 255));
    strip.show();

    String currentLine = "";

    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        Serial.write(c);

        if (c == '\n') {
          if (currentLine.length() == 0) {
            printWelcomePage(client);
            break;
          } else {
            currentLine = "";
          }
        } else if (c != '\r') {
          currentLine += c;
        }
      }
    }

    client.stop();
    Serial.println(INFO_DISCONNECT_CLIENT);
    Serial.println();

    // back to GREEN = AP idle
    strip.setPixelColor(0, strip.Color(0, 255, 0));
    strip.show();
  }
}

void printWelcomePage(WiFiClient client) {
  client.println(HTTP_HEADER);
  client.print(HTML_WELCOME);
  client.println();
}