#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Adafruit_NeoPixel.h>

#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

/*
 * LED on Carbond D4
 */
#define LED_PIN        12
#define LED_ENABLE_PIN 13
#define LED_COUNT       1

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

// make characteristic global so callbacks can notify
BLECharacteristic *pCharacteristicGlobal = nullptr;

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

class LedCallback : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    String value = pCharacteristic->getValue();

    value.trim();   // remove CR/LF if sent from phone app

    float notifyVal = 0.0f;

    if (value == "ON") {
      strip.setPixelColor(0, strip.Color(0, 255, 0));
      strip.show();
      notifyVal = 1.0f;
    } 
    else if (value == "OFF") {
      strip.clear();
      strip.show();
      notifyVal = 0.0f;
    }

    // If global characteristic is set, send a 4-byte float notification (little-endian)
    if (pCharacteristicGlobal) {
      uint8_t buf[4];
      memcpy(buf, &notifyVal, sizeof(float));
      pCharacteristicGlobal->setValue(buf, sizeof(buf));
      pCharacteristicGlobal->notify();
      // debug log to serial
      Serial.print("Received write: ");
      Serial.print(value);
      Serial.print(" -> notified value: ");
      Serial.println(notifyVal);
    }
  }
};

void setup() {
  Serial.begin(115200);

  pinMode(LED_ENABLE_PIN, OUTPUT);
  digitalWrite(LED_ENABLE_PIN, LOW);

  strip.begin();
  strip.setBrightness(60);
  strip.clear();
  strip.show();

  BLEDevice::init("ESP32_BLE_LED");

  BLEServer *pServer = BLEDevice::createServer();
  BLEService *pService = pServer->createService(SERVICE_UUID);

  BLECharacteristic *pCharacteristic =
    pService->createCharacteristic(
      CHARACTERISTIC_UUID,
      BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY
    );

  // keep a global ref so callbacks can notify
  pCharacteristicGlobal = pCharacteristic;

  pCharacteristic->setCallbacks(new LedCallback());
  pCharacteristic->addDescriptor(new BLE2902());

  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->start();

  Serial.println("BLE server started, waiting for client...");
}


void loop() {
  // Nothing needed here for basic BLE
}