#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define SERVICE_UUID        "12345678-1234-1234-1234-1234567890ab"
#define CHARACTERISTIC_UUID "abcdefab-1234-1234-1234-abcdefabcdef"

BLECharacteristic *pCharacteristic;
bool deviceConnected = false;

class MyServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) { deviceConnected = true; Serial.println("BLE client connected"); }
  void onDisconnect(BLEServer* pServer) { deviceConnected = false; Serial.println("BLE client disconnected"); }
};

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("ESP32-CAM BLE Starting...");

  // Initialize BLE
  BLEDevice::init("ESP32CAM_Stress"); // Device name
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_READ |
                      BLECharacteristic::PROPERTY_NOTIFY
                    );
  pCharacteristic->addDescriptor(new BLE2902());
  pCharacteristic->setValue("0");
  pService->start();

  // Proper advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);

  // Include device name in advertising payload
  BLEAdvertisementData advData;
  advData.setName("ESP32CAM_Stress");       // <-- Important!
  advData.setCompleteServices(BLEUUID(SERVICE_UUID));
  pAdvertising->setAdvertisementData(advData);

  BLEDevice::startAdvertising();
  Serial.println("BLE advertising started!");
}

void loop() {
  if (Serial.available()) {
    String stressValue = Serial.readStringUntil('\n');
    stressValue.trim();
    Serial.print("Received from Python: "); Serial.println(stressValue);

    if (deviceConnected) {
      pCharacteristic->setValue(stressValue.c_str());
      pCharacteristic->notify();
      Serial.print("Sent via BLE: "); Serial.println(stressValue);
    }
  }
  delay(50);
}