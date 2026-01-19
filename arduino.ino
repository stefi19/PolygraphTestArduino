#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>

// ---------------- RFID PINS ----------------
#define SS_PIN   10
#define RST_PIN   9
MFRC522 rfid(SS_PIN, RST_PIN);

// ---------------- LED PINS -----------------
#define LED_AUTH     6
#define LED_DENY     5

// ---------------- I2C ----------------------
#define I2C_ADDRESS  0x08   // Arduino slave address

// ---------------- AUTHORIZED UID -----------
const char* AUTH_UID = "23FD844";

// ---------------- STATE --------------------
volatile byte authState = 0; // 0 = denied, 1 = authorized

// ---------------- GSR SENSOR ----------------
const int GSR_PIN = A0;
int gsr_average = 0;
const int GSR_SAMPLES = 10;
int gsr_values[GSR_SAMPLES];
int gsr_index = 0;
// ---------------- LED TIMING ----------------
const int SOUND_PIN = A1;
int sound_average = 0;
const int SOUND_SAMPLES = 10;
int sound_values[SOUND_SAMPLES];
int sound_index = 0;
// ---------------- LED TIMING ----------------
unsigned long ledOnTime = 0;
const unsigned long LED_DURATION = 300; // ms
bool ledAuthState = false;
bool ledDenyState = false;

// ----------------EKG ----------------
volatile byte ekgValue = 0;  

// ---------------- JOYSTICK ----------------
#define JOY_PIN A3
#define JOY_STRESS_THRESHOLD 900   // near upper limit
// ---------------- USE JS TO SIMULATE STRESS ----------------
bool stressActive = false;
unsigned long stressStart = 0;
const unsigned long STRESS_DURATION = 5000; // 5 seconds
// ---------------- I2C -----------------------
#include <Wire.h>

#define I2C_ADDRESS 0x08

#include <stdint.h>

struct PolyData {
  uint8_t ekg;      // 1 byte
  uint16_t gsr;     // 2 bytes
  uint16_t sound;   // 2 bytes
  uint8_t auth;     // 1 byte
  uint8_t stress;   // 1 byte
}__attribute__((packed));


volatile PolyData txData;
void onI2CRequest() {
  digitalWrite(13, HIGH);
  Wire.write((byte*)&txData, sizeof(txData));
  digitalWrite(13, LOW);
}


void setup() {
  Serial.begin(9600);
  while (!Serial);

  // RFID
  SPI.begin();
  rfid.PCD_Init();

  // LEDs
  pinMode(LED_AUTH, OUTPUT);
  pinMode(LED_DENY, OUTPUT);

  // I2C Slave
  Wire.begin(I2C_ADDRESS);
  Wire.onRequest(onI2CRequest);

  // Init GSR buffer
  for (int i = 0; i < GSR_SAMPLES; i++) gsr_values[i] = 0;

  //Serial.println("System ready. Scan RFID card...");
}

// ---------------- LOOP -----------------------
void loop() {
  int joyValue = analogRead(JOY_PIN);

  // trigger stress ONLY when not already active
  if (!stressActive && joyValue > JOY_STRESS_THRESHOLD) {
    stressActive = true;
    stressStart = millis();
    //Serial.println("FUUUUUUUUUUUUUUUUU");
  }
  if (stressActive && millis() - stressStart >= STRESS_DURATION) {
    stressActive = false;
  }


  unsigned long currentMillis = millis();

  // ---------------- Read GSR ----------------
  gsr_values[gsr_index] = analogRead(GSR_PIN);
  gsr_index = (gsr_index + 1) % GSR_SAMPLES;

  long gsrSum = 0;
  for (int i = 0; i < GSR_SAMPLES; i++) gsrSum += gsr_values[i];
  gsr_average = gsrSum / GSR_SAMPLES;

  // ---------------- Read Mic ----------------

int soundLevel = readSoundLevel();
//Serial.println(soundLevel);


  // ---------------- RFID -------------------
  if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
    String uidStr = "";
    for (byte i = 0; i < rfid.uid.size; i++) {
      uidStr += String(rfid.uid.uidByte[i], HEX);
    }
    uidStr.toUpperCase();
    //Serial.print("Scanned UID: "); Serial.println(uidStr);

    if (uidStr == AUTH_UID) {
      authState = 1;
      ledAuthState = true;
      ledOnTime = currentMillis;
      //Serial.println("CARD AUTHORISED");
    } else {
      authState = 0;
      ledDenyState = true;
      ledOnTime = currentMillis;
      //Serial.println("CARD NOT AUTHORISED");
    }

    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
  }
  //the EKG output selection
  if (authState == 1) {
  ekgValue = readRealEKG();
} else {
  ekgValue = readSimulatedEKG();
}
//Serial.println(ekgValue);
  // ---------------- LED CONTROL ----------------
  if (ledAuthState) {
    digitalWrite(LED_AUTH, HIGH);
    if (currentMillis - ledOnTime >= LED_DURATION) {
      digitalWrite(LED_AUTH, LOW);
      ledAuthState = false;
    }
  }
  if (ledDenyState) {
    digitalWrite(LED_DENY, HIGH);
    if (currentMillis - ledOnTime >= LED_DURATION) {
      digitalWrite(LED_DENY, LOW);
      ledDenyState = false;
    }
  }

  // ---------------- DEBUG ----------------
//Serial.println(gsr_average);
//Serial.println(sound_average);
  // Very short loop delay to prevent flooding serial
txData.ekg    = ekgValue;
txData.gsr    = gsr_average;
txData.sound = soundLevel;
txData.auth  = authState;
txData.stress = stressActive ? 1 : 0;

 Serial.print("EKG: ");    Serial.print(ekgValue);
     Serial.print(" | GSR: "); Serial.print(gsr_average);
     Serial.print(" | SOUND: "); Serial.print(soundLevel);
     Serial.print(" | AUTH: "); Serial.print(authState);
     Serial.print(" | STRESS: "); Serial.println(stressActive);


  delay(10);
}

int readSoundLevel() {
  int prev = analogRead(SOUND_PIN);
  long energy = 0;

  unsigned long start = micros();
  while (micros() - start < 5000) { // 5 ms window
    int curr = analogRead(SOUND_PIN);
    energy += abs(curr - prev);
    prev = curr;
  }
  if(energy>512)energy=512;
  return energy;
}

byte readRealEKG() {
  static boolean flipflop = false;
  static int loops = 0;

  loops++;
  if (loops % 100 == 0) {
    flipflop = !flipflop;
    digitalWrite(LED_BUILTIN, flipflop);
  }

  return analogRead(A2) / 4;  // A0 → 0–255
}


const byte EKG_SIM[51] = {
  40,38,42,43,44,45,44,43,42,41,
  40,40,40,40,40,39,42,43,60,80,
  100,120,80,50,5,10,20,30,40,50,
  35,40,41,42,43,44,45,46,47,48,
  49,50,51,51,50,49,48,45,42,40,40
};
byte readSimulatedEKG() {
  // ---- Static state ----
  static byte t = 0;           // index in waveform
  static byte y = 40;          // current output
  static unsigned long lastStep = 0;  // timing for advancing waveform

  unsigned long now = millis();
  const unsigned long stepInterval = 20; // ms per sample (~50 Hz)

  // ---- STRESS PARAMETERS ----
  byte noiseAmp   = stressActive ? 12 : 2;   // baseline noise
  byte peakBoost  = stressActive ? 50 : 0;   // stress R-peak boost
  byte baseLength = stressActive ? 25 : 50;  // samples per beat (~1 sec normal, faster stress)
  byte dropChance = stressActive ? 4 : 0;    // occasional artifact

  // ---- Advance waveform based on time ----
  if (now - lastStep >= stepInterval) {
    lastStep = now;
    t++; // move to next sample

    if ((!stressActive && t >= 50) || (stressActive && t >= (50 + baseLength))) {
      t = 0; // loop waveform
    }
  }

  // ---- PQRST simulation ----
  if (t < 50) {
    byte a = EKG_SIM[t];
    byte b = EKG_SIM[t + 1];
    float interp = ((float)(now - lastStep) / stepInterval); // smooth interpolation
    y = a + (b - a) * interp; 
    y += random(-noiseAmp, noiseAmp);

    // R-peak exaggeration for stress
    if (stressActive && t >= 18 && t <= 22) {
      y += peakBoost;
    }

    y = constrain(y, 0, 255);
  }
  else {
    // baseline between beats
    y = 40 + random(-noiseAmp, noiseAmp);
    if (stressActive) {
      y += random(-8, 8); // tremor
    }
  }

  // ---- Random artifacts for realism ----
  if (stressActive && random(1000) < dropChance) {
    y = random(0, 20);
  }

  return y;
}