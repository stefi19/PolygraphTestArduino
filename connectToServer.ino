#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <Wire.h>

// WiFi credentials - replace with your network
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// I2C configuration
#define I2C_SDA 14
#define I2C_SCL 15
#define ARDUINO_ADDR 0x08

// Web server and WebSocket
WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

// Card / authorization state (can be driven from Arduino via I2C or
// simulated via HTTP endpoints below)
String lastCardId = "";
bool manualAuthorized = false;
// Calibration state driven by ESP when /startCalibration called or via WS command
bool calibrating = false;
unsigned long calibrationEndMillis = 0;
unsigned long calibrationCount = 0;
unsigned long heartAcc = 0;
unsigned long gsrAcc = 0;
unsigned long voiceAcc = 0;
unsigned long calibrationDurationMs = 0;

// If you develop the frontend separately (Vite/dev server), set this to its URL
// e.g. "http://192.168.1.42:5173" or an ngrok/localtunnel https URL.
// If empty, the ESP will serve the built-in HTML page.
const char* FRONTEND_URL = "";

// Forward declarations
void handleRoot();
void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length);

// Data structure matching Arduino (packed to avoid padding)
struct __attribute__((packed)) SensorData {
  uint16_t heartRate;
  uint16_t gsr;
  uint16_t voiceLevel;
  uint8_t authenticated;
  uint8_t leadsOff;
};
SensorData sensorData;

// Lie detection variables
float heartBaseline = 0;
float gsrBaseline = 0;
float voiceBaseline = 0;
int baselineCount = 0;
bool baselineEstablished = false;
const int BASELINE_SAMPLES = 200; // ~10 seconds at 20Hz

void setup() {
  Serial.begin(115200);
  delay(100);

  // Initialize I2C as master
  Wire.begin(I2C_SDA, I2C_SCL);

  // Connect to WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // Setup web server routes
  server.on("/", handleRoot);
  // Simple endpoints to simulate card detection and authorization from a browser
  // Example: http://<esp_ip>/card?card=ABC123
  server.on("/card", []() {
    if (server.hasArg("card")) {
      lastCardId = server.arg("card");
      Serial.print("Card detected: ");
      Serial.println(lastCardId);
      // do not auto-authorize here; wait for explicit authorize
      server.send(200, "text/plain", String("Card received: ") + lastCardId);
      return;
    }
    server.send(400, "text/plain", "Missing ?card=ID");
  });

  // Authorize a detected card: http://<esp_ip>/authorize?card=ABC123
  server.on("/authorize", []() {
    if (server.hasArg("card")) {
      String c = server.arg("card");
      if (c == lastCardId) {
        manualAuthorized = true;
        Serial.print("Authorized card: ");
        Serial.println(c);
        server.send(200, "text/plain", String("Authorized: ") + c);
        return;
      } else {
        server.send(400, "text/plain", "Card mismatch");
        return;
      }
    }
    server.send(400, "text/plain", "Missing ?card=ID");
  });

  // Start calibration via HTTP: /startCalibration?duration=10
  server.on("/startCalibration", []() {
    int dur = 10;
    if (server.hasArg("duration")) {
      dur = server.arg("duration").toInt();
      if (dur <= 0) dur = 10;
    }
    // begin calibration for dur seconds
    calibrating = true;
    calibrationDurationMs = (unsigned long)dur * 1000UL;
    calibrationEndMillis = millis() + calibrationDurationMs;
    calibrationCount = 0;
    heartAcc = gsrAcc = voiceAcc = 0;
    baselineEstablished = false;
    manualAuthorized = true;
    Serial.printf("Calibration started for %d seconds\n", dur);
    server.send(200, "text/plain", String("Calibration started for ") + dur + " seconds");
  });

  // Deauthorize
  server.on("/deauthorize", []() {
    manualAuthorized = false;
    lastCardId = "";
    Serial.println("Deauthorized");
    server.send(200, "text/plain", "Deauthorized");
  });
  server.begin();

  // Start WebSocket
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  Serial.println("ESP32 Polygraph Server Started");
}

void loop() {
  server.handleClient();
  webSocket.loop();

  // Read data from Arduino via I2C
  Wire.requestFrom(ARDUINO_ADDR, (uint8_t)sizeof(sensorData));
  if (Wire.available() >= sizeof(sensorData)) {
    uint8_t *p = (uint8_t *)&sensorData;
    for (size_t i = 0; i < sizeof(sensorData) && Wire.available(); ++i) {
      p[i] = Wire.read();
    }

    // Process and send data
    // Note: processData sends to all WebSocket clients
    // We'll call it directly here
    // Build baseline and compute lie probability
    // (moved inline to avoid an extra function call overhead)

    // Determine if currently authorized: either Arduino suggests it or manual HTTP authorize was used
    bool isAuth = sensorData.authenticated || manualAuthorized;

    // Build baseline if authenticated and not established
    if (isAuth && !baselineEstablished) {
      if (baselineCount < BASELINE_SAMPLES) {
        heartBaseline += sensorData.heartRate;
        gsrBaseline += sensorData.gsr;
        voiceBaseline += sensorData.voiceLevel;
        baselineCount++;
      } else {
        heartBaseline /= BASELINE_SAMPLES;
        gsrBaseline /= BASELINE_SAMPLES;
        voiceBaseline /= BASELINE_SAMPLES;
        baselineEstablished = true;
        Serial.println("Baseline established");
      }
    }

    // Reset baseline if authentication lost
    if (!isAuth && baselineEstablished) {
      baselineEstablished = false;
      baselineCount = 0;
      heartBaseline = 0;
      gsrBaseline = 0;
      voiceBaseline = 0;
    }

    // Calculate lie probability
    float lieProbability = 0;

    if (baselineEstablished && !sensorData.leadsOff) {
      float heartDev = fabs((float)sensorData.heartRate - heartBaseline) / (heartBaseline > 0 ? heartBaseline : 1);
      float gsrDev = ((float)sensorData.gsr - gsrBaseline) / (gsrBaseline > 0 ? gsrBaseline : 1); // Positive deviation
      float voiceDev = fabs((float)sensorData.voiceLevel - voiceBaseline) / (voiceBaseline > 0 ? voiceBaseline : 1);

      // Weight the indicators
      lieProbability = (heartDev * 30.0f + fmax(gsrDev, 0.0f) * 40.0f + voiceDev * 30.0f);
      lieProbability = constrain(lieProbability * 100.0f, 0.0f, 100.0f);
    }

    // Create JSON data
    String json = "{";
    json += "\"heart\":" + String(sensorData.heartRate) + ",";
    json += "\"gsr\":" + String(sensorData.gsr) + ",";
    json += "\"voice\":" + String(sensorData.voiceLevel) + ",";
    // send combined auth state (Arduino or manual)
    json += "\"auth\":" + String(isAuth ? 1 : 0) + ",";
    json += "\"leadsOff\":" + String(sensorData.leadsOff) + ",";
    json += "\"lie\":" + String(lieProbability, 1) + ",";
    json += "\"baseline\":" + String(baselineEstablished ? 1 : 0);
    // include calibration remaining seconds if calibrating
    unsigned long calRem = 0;
    if (calibrating) {
      if (calibrationEndMillis > millis()) calRem = (calibrationEndMillis - millis() + 999) / 1000;
    }
    json += ",\"calibration\":" + String((int)calRem);
    // include cardId if present
    if (lastCardId.length() > 0) {
      json += ",\"cardId\":\"" + lastCardId + "\"";
    }
    json += "}";

    // Broadcast to all connected clients
    webSocket.broadcastTXT(json);
  }

  delay(50); // 20Hz sampling
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.printf("[%u] Disconnected!\n", num);
      break;
    case WStype_CONNECTED: {
        // If ESP is in calibration mode, accumulate samples for baseline
        if (calibrating) {
          calibrationCount++;
          heartAcc += sensorData.heartRate;
          gsrAcc += sensorData.gsr;
          voiceAcc += sensorData.voiceLevel;
          // check if calibration period ended
          if ((long)(millis() - calibrationEndMillis) >= 0) {
            // finalize baseline
            if (calibrationCount > 0) {
              heartBaseline = (float)heartAcc / (float)calibrationCount;
              gsrBaseline = (float)gsrAcc / (float)calibrationCount;
              voiceBaseline = (float)voiceAcc / (float)calibrationCount;
              baselineEstablished = true;
              Serial.println("Calibration complete: baseline established");
            }
            calibrating = false;
          }
        }

        // Build baseline if authenticated and not established
        // Print remote IP for debugging
        IPAddress ip = webSocket.remoteIP(num);
        Serial.printf("[%u] Connected from %d.%d.%d.%d\n", num, ip[0], ip[1], ip[2], ip[3]);
      break;
    }
      case WStype_TEXT: {
        // Convert payload to String for simple parsing
        String msg = String((char*)payload);
        Serial.printf("[%u] Received: %s\n", num, msg.c_str());

        // Support a simple WebSocket command to start calibration.
        // Expected payload (JSON-ish): {"cmd":"startCalibration","duration":10}
        if (msg.indexOf("startCalibration") >= 0) {
          int dur = 10;
          int idx = msg.indexOf("duration");
          if (idx >= 0) {
            int colon = msg.indexOf(':', idx);
            if (colon >= 0) {
              String numStr = "";
              for (int i = colon + 1; i < msg.length(); ++i) {
                char c = msg.charAt(i);
                if (isDigit(c)) numStr += c;
                else if (numStr.length() > 0) break;
              }
              if (numStr.length() > 0) dur = numStr.toInt();
            }
          }
          // Begin calibration
          calibrating = true;
          calibrationDurationMs = (unsigned long)dur * 1000UL;
          calibrationEndMillis = millis() + calibrationDurationMs;
          calibrationCount = 0;
          heartAcc = gsrAcc = voiceAcc = 0;
          baselineEstablished = false;
          manualAuthorized = true; // keep auth during calibration
          Serial.printf("Calibration started via WS for %d seconds\n", dur);
          // Acknowledge to the requester
          String resp = String("{\"calibrationStarted\":") + dur + "}";
          webSocket.sendTXT(num, resp);
        }
        break;
      }
  }
}

void handleRoot() {
  // If a separate frontend URL is configured, redirect HTTP clients to it so
  // you can develop the frontend with HMR (Vite) and the ESP only provides
  // the WebSocket data source.
  if (FRONTEND_URL[0] != '\0') {
    server.sendHeader("Location", String(FRONTEND_URL));
    server.send(302, "text/plain", "");
    return;
  }

  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Polygraph Monitor</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <script src="https://cdnjs.cloudflare.com/ajax/libs/Chart.js/3.9.1/chart.min.js"></script>
  <style>
    * { margin: 0; padding: 0; box-sizing: border-box; }
    body {
      font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
      padding: 20px;
      min-height: 100vh;
    }
    .container {
      max-width: 1400px;
      margin: 0 auto;
      background: white;
      border-radius: 20px;
      padding: 30px;
      box-shadow: 0 20px 60px rgba(0,0,0,0.3);
    }
    h1 {
      text-align: center;
      color: #333;
      margin-bottom: 10px;
    }
    .status {
      text-align: center;
      padding: 15px;
      margin: 20px 0;
      border-radius: 10px;
      font-size: 1.2em;
      font-weight: bold;
    }
    .status.connected { background: #d4edda; color: #155724; }
    .status.disconnected { background: #f8d7da; color: #721c24; }
    .status.simulated { background: #fff3cd; color: #856404; }
    
    .metrics {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
      gap: 20px;
      margin: 30px 0;
    }
    .metric {
      background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
      padding: 20px;
      border-radius: 15px;
      color: white;
      text-align: center;
    }
    .metric-label {
      font-size: 0.9em;
      opacity: 0.9;
      margin-bottom: 10px;
    }
    .metric-value {
      font-size: 2.5em;
      font-weight: bold;
    }
    .metric-unit {
      font-size: 0.8em;
      opacity: 0.8;
    }
    
    .lie-detector {
      text-align: center;
      padding: 30px;
      margin: 30px 0;
      background: #f8f9fa;
      border-radius: 15px;
    }
    .lie-meter {
      width: 100%;
      height: 60px;
      background: #e9ecef;
      border-radius: 30px;
      position: relative;
      margin: 20px 0;
      overflow: hidden;
    }
    .lie-fill {
      height: 100%;
      background: linear-gradient(90deg, #28a745 0%, #ffc107 50%, #dc3545 100%);
      border-radius: 30px;
      transition: width 0.3s ease;
      display: flex;
      align-items: center;
      justify-content: flex-end;
      padding-right: 20px;
      color: white;
      font-weight: bold;
      font-size: 1.3em;
    }
    
    .charts {
      display: grid;
      grid-template-columns: 1fr;
      gap: 30px;
    }
    .chart-container {
      background: white;
      padding: 20px;
      border-radius: 15px;
      box-shadow: 0 4px 6px rgba(0,0,0,0.1);
    }
    .chart-title {
      font-size: 1.3em;
      font-weight: bold;
      margin-bottom: 15px;
      color: #333;
    }
  </style>
</head>
<body>
  <div class="container">
    <h1>Polygraph Monitor</h1>
    <div id="status" class="status disconnected">Connecting...</div>
    
    <div class="lie-detector">
      <h2>Deception Probability</h2>
      <div class="lie-meter">
        <div id="lieFill" class="lie-fill" style="width: 0%">0%</div>
      </div>
      <p id="lieText" style="font-size: 1.1em; color: #666;">Establishing baseline...</p>
    </div>
    
    <div class="metrics">
      <div class="metric">
        <div class="metric-label">Heart Signal</div>
        <div class="metric-value" id="heartValue">0</div>
        <div class="metric-unit">ADC Units</div>
      </div>
      <div class="metric">
        <div class="metric-label">Skin Conductivity</div>
        <div class="metric-value" id="gsrValue">0</div>
        <div class="metric-unit">ADC Units</div>
      </div>
      <div class="metric">
        <div class="metric-label">Voice Level</div>
        <div class="metric-value" id="voiceValue">0</div>
        <div class="metric-unit">ADC Units</div>
      </div>
    </div>
    
    <div class="charts">
      <div class="chart-container">
        <div class="chart-title">Heart Rate Signal</div>
        <canvas id="heartChart"></canvas>
      </div>
      <div class="chart-container">
        <div class="chart-title">Galvanic Skin Response</div>
        <canvas id="gsrChart"></canvas>
      </div>
      <div class="chart-container">
        <div class="chart-title">Voice Amplitude</div>
        <canvas id="voiceChart"></canvas>
      </div>
    </div>
  </div>

  <script>
    const protocol = (location.protocol === 'https:') ? 'wss://' : 'ws://';
    const ws = new WebSocket(protocol + location.hostname + ':81');
    const maxDataPoints = 100;
    
    // Initialize charts
    const chartConfig = (label, color) => ({
      type: 'line',
      data: {
        labels: Array(maxDataPoints).fill(''),
        datasets: [{
          label: label,
          data: Array(maxDataPoints).fill(null),
          borderColor: color,
          backgroundColor: color + '20',
          borderWidth: 2,
          tension: 0.4,
          fill: true
        }]
      },
      options: {
        responsive: true,
        maintainAspectRatio: true,
        aspectRatio: 3,
        plugins: { legend: { display: false } },
        scales: {
          y: { 
            beginAtZero: true, 
            max: 1023,
            grid: { color: '#e9ecef' }
          },
          x: { 
            display: false 
          }
        },
        animation: { duration: 0 }
      }
    });
    
    const heartChart = new Chart(document.getElementById('heartChart'), 
      chartConfig('Heart Rate', '#dc3545'));
    const gsrChart = new Chart(document.getElementById('gsrChart'), 
      chartConfig('GSR', '#28a745'));
    const voiceChart = new Chart(document.getElementById('voiceChart'), 
      chartConfig('Voice', '#007bff'));
    
    function updateChart(chart, value) {
      chart.data.datasets[0].data.push(value);
      if (chart.data.datasets[0].data.length > maxDataPoints) {
        chart.data.datasets[0].data.shift();
      }
      chart.update();
    }
    
    ws.onopen = () => {
      console.log('WebSocket connected');
    };
    
    ws.onmessage = (event) => {
      const data = JSON.parse(event.data);
      
      // Update status
      const statusDiv = document.getElementById('status');
      if (data.auth) {
        if (data.leadsOff) {
          statusDiv.className = 'status disconnected';
          statusDiv.textContent = 'Sensor Leads Disconnected';
        } else {
          statusDiv.className = 'status connected';
          statusDiv.textContent = 'Authenticated - Real Sensor Data';
        }
      } else {
        statusDiv.className = 'status simulated';
        statusDiv.textContent = 'Simulation Mode - No Authentication';
      }
      
      // Update metrics
      document.getElementById('heartValue').textContent = data.heart;
      document.getElementById('gsrValue').textContent = data.gsr;
      document.getElementById('voiceValue').textContent = data.voice;
      
      // Update lie probability
      const lieFill = document.getElementById('lieFill');
      const lieText = document.getElementById('lieText');
      const liePercent = Math.round(data.lie);
      
      if (!data.baseline) {
        lieText.textContent = 'Establishing baseline... Please remain calm.';
        lieFill.style.width = '0%';
      } else {
        lieFill.style.width = liePercent + '%';
        lieFill.textContent = liePercent + '%';
        
        if (liePercent < 30) {
          lieText.textContent = 'Truthful - No deception detected';
        } else if (liePercent < 60) {
          lieText.textContent = 'Possible Stress - Mild indicators detected';
        } else {
          lieText.textContent = 'High Probability - Strong deception indicators';
        }
      }
      
      // Update charts
      updateChart(heartChart, data.heart);
      updateChart(gsrChart, data.gsr);
      updateChart(voiceChart, data.voice);
    };
    
    ws.onerror = (error) => {
      console.error('WebSocket error:', error);
      document.getElementById('status').textContent = 'Connection Error';
    };
    
    ws.onclose = () => {
      document.getElementById('status').className = 'status disconnected';
      document.getElementById('status').textContent = 'Disconnected - Attempting to reconnect...';
      setTimeout(() => location.reload(), 3000);
    };
  </script>
</body>
</html>
)rawliteral";
  
  server.send(200, "text/html", html);
}