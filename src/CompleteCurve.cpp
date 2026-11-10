#include <M5StickC.h>
#include <Wire.h>
#include <Adafruit_INA219.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

// --- WiFi Credentials ---
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

AsyncWebServer server(80);
Adafruit_INA219 ina219;
#define ledcPin 12
const int pwmPin = 26; 
const int pwmChannel = 0;
const int pwmFreq = 5000;
const int pwmResolution = 8; // 0-255 steps

String dataJson = "[]";

void captureCurve() {
  // Allocate memory for the 0-255 sweep data points (approx 52 points if stepping by 5)
  StaticJsonDocument<4096> doc;
  JsonArray array = doc.to<JsonArray>();

  for (int duty = 0; duty <= 255; duty += 5) {
    ledcWrite(pwmChannel, duty);
    delay(20); // Allow the RC filter voltage to stabilize

    // Bus voltage is the voltage at VIN- (voltage across the DUT)
    float voltage_V = ina219.getBusVoltage_V();
    float current_mA = ina219.getCurrent_mA();

    // Guard against negative noise floor readings at near 0V
    if (current_mA < 0) current_mA = 0;
    if (voltage_V < 0) voltage_V = 0;

    JsonObject point = array.createNestedObject();
    point["v"] = voltage_V;
    point["i"] = current_mA;
  }
  
  ledcWrite(pwmChannel, 0); // Turn off voltage sweep after test completes
  serializeJson(doc, dataJson);
}

// --- Frontend Web Page ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>M5Stick-C + INA219 Curve Tracer</title>
  <script src="https://jsdelivr.net"></script>
  <style>
    body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; text-align: center; margin: 40px; background: #f0f2f5; color: #333; }
    .container { width: 75%; max-width: 900px; margin: auto; background: white; padding: 30px; border-radius: 12px; box-shadow: 0 4px 15px rgba(0,0,0,0.08); }
    button { padding: 12px 28px; font-size: 16px; background: #00bcd4; color: white; border: none; font-weight: bold; border-radius: 6px; cursor: pointer; transition: background 0.2s; }
    button:hover { background: #0097a7; }
    .chart-box { margin-top: 25px; background: #fafafa; padding: 15px; border-radius: 8px; border: 1px solid #e0e0e0; }
  </style>
</head>
<body>
  <div class="container">
    <h2>INA219 Precision I-V Curve Tracer</h2>
    <p>Press the button below to execute a DC voltage sweep (0V - 3.3V) across the component.</p>
    <button onclick="sweepComponent()">Scan Component</button>
    <div class="chart-box"><canvas id="ivChart"></canvas></div>
  </div>
  <script>
    let ctx = document.getElementById('ivChart').getContext('2d');
    let chart = new Chart(ctx, {
      type: 'scatter',
      data: { 
        datasets: [{ 
          label: 'Component Signature (I-V)', 
          data: [], 
          borderColor: '#00bcd4', 
          backgroundColor: 'rgba(0, 188, 212, 0.2)',
          showLine: true, 
          borderWidth: 2,
          pointRadius: 4 
        }] 
      },
      options: { 
        responsive: true,
        scales: { 
          x: { title: { display: true, text: 'Voltage Across Component (V)' }, grid: { color: '#eaeaea' } }, 
          y: { title: { display: true, text: 'Current (mA)' }, grid: { color: '#eaeaea' } } 
        } 
      }
    });

    function sweepComponent() {
      fetch('/sweep')
        .then(res => res.json())
        .then(data => {
          // Remap JSON keys to Chart.js scatter coordinates (x, y)
          chart.data.datasets[0].data = data.map(p => ({ x: p.v, y: p.i }));
          chart.update();
        })
        .catch(err => console.error("Error fetching sweep data:", err));
    }
  </script>
</body>
</html>
)rawliteral";

void setup() {
  M5.begin();
  
  // Initialize I2C specifically on Pins 32 & 33 for the M5Stick-C External Header
  Wire.begin(32, 33); 
  
  if (!ina219.begin()) {
    M5.Lcd.println("INA219 Connection Failed");
    while (1) { delay(10); }
  }
  
  // Optional: Maximize INA219 resolution for lower ranges (e.g., 32V, 400mA max)
//  ina219.setCalibration_32V_400mA();
 ina219.setCalibration_16V_400mA();
  // Configure PWM Peripheral for DC Voltage Generation
 // ledcSetup(pwmChannel, pwmFreq, pwmResolution);
 // ledcAttachPin(pwmPin, pwmChannel);
 ledcAttach(ledcPin, pwmFreq,pwmResolution);
 
  ledcWrite(pwmChannel, 0); // Start at 0V

  // Connect to Network
  WiFi.begin(ssid, password);
  M5.Lcd.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) { 
    delay(500); 
    M5.Lcd.print(".");
  }
  
  M5.Lcd.fillScreen(BLACK);
  M5.Lcd.setCursor(0, 10);
  M5.Lcd.println("WiFi OK!");
  M5.Lcd.print("IP: ");
  M5.Lcd.println(WiFi.localIP());

  // --- Web Server Routing ---
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
 //   request->send_progmem(200, "text/html", index_html);
      request->send(200, "test/html", index_html);
  
  });

  server.on("/sweep", HTTP_GET, [](AsyncWebServerRequest *request){
    captureCurve();
    request->send(200, "application/json", dataJson);
  });

  server.begin();
}

void loop() {
  M5.update();
}

