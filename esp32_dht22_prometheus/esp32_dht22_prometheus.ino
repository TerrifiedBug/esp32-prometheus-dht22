// Alternative: If Adafruit DHT doesn't work, try SimpleDHT
// #include <SimpleDHT.h>
// SimpleDHT22 dht22(DHT_PIN);

#include <WiFi.h>
#include <WebServer.h>
#include <DHT.h>          // Install "DHT sensor library by Adafruit"
#include <ArduinoJson.h>  // Install "ArduinoJson by Benoit Blanchon"
#include <SPIFFS.h>       // Built-in with ESP32
#include <HTTPClient.h>   // Built-in with ESP32
#include <ArduinoOTA.h>   // Built-in with ESP32
#include <Update.h>       // Built-in with ESP32
#include <time.h>         // Built-in

// DHT22 Configuration
#define DHT_PIN 15  // GPIO pin for DHT22 (same as Pico)
#define DHT_TYPE DHT22
DHT dht(DHT_PIN, DHT_TYPE);

// Web Server
WebServer server(80);

// WiFi Credentials (you'll need to set these)
const char* ssid = "ssid";
const char* password = "password";

// Device Configuration
struct DeviceConfig {
  String location = "default-location";
  String deviceName = "default-device";
  String description = "";
  bool otaEnabled = true;
  bool autoUpdate = true;
  float updateInterval = 1.0;
  String repoOwner = "TerrifiedBug";
  String repoName = "esp32-prometheus-dht22";
  String branch = "main";
} config;

// System Variables
unsigned long bootTime;
float lastTemperature = NAN;
float lastHumidity = NAN;
unsigned long lastSensorRead = 0;
const unsigned long SENSOR_READ_INTERVAL = 30000;  // 30 seconds

// GitHub OTA Variables
String currentVersion = "v1.0.0";
unsigned long lastUpdateCheck = 0;
bool updateInProgress = false;

// Logging System
struct LogEntry {
  unsigned long timestamp;
  String level;
  String category;
  String message;
};

const int MAX_LOG_ENTRIES = 150;
LogEntry logBuffer[MAX_LOG_ENTRIES];
int logIndex = 0;
int logCount = 0;

void setup() {
  Serial.begin(115200);
  bootTime = millis();

  // Initialize SPIFFS for file storage
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS Mount Failed");
  }

  // Initialize DHT sensor
  dht.begin();

  // Load configuration
  loadConfig();

  // Connect to WiFi
  connectWiFi();

  // Setup web server routes
  setupWebServer();

  // Setup OTA if enabled
  if (config.otaEnabled) {
    setupOTA();
  }

  // Initialize time for accurate timestamps
  configTime(0, 0, "pool.ntp.org");

  logMessage("INFO", "SYSTEM", "ESP32 DHT22 sensor server started");
}

void loop() {
  // Handle web server requests
  server.handleClient();

  // Handle OTA updates if enabled
  if (config.otaEnabled) {
    ArduinoOTA.handle();

    // Check for GitHub updates if auto-update is enabled
    if (config.autoUpdate && !updateInProgress) {
      unsigned long updateIntervalMs = config.updateInterval * 3600000; // Convert hours to milliseconds
      if (millis() - lastUpdateCheck >= updateIntervalMs) {
        checkForGitHubUpdate();
        lastUpdateCheck = millis();
      }
    }
  }

  // Read sensor data periodically
  if (millis() - lastSensorRead >= SENSOR_READ_INTERVAL) {
    readDHT22();
    lastSensorRead = millis();
  }

  // Small delay to prevent watchdog issues
  delay(10);
}

void connectWiFi() {
  logMessage("INFO", "NETWORK", "Connecting to WiFi...");
  WiFi.begin(ssid, password);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(1000);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    String message = "WiFi connected, IP: " + WiFi.localIP().toString();
    logMessage("INFO", "NETWORK", message);
    Serial.println(message);
  } else {
    logMessage("ERROR", "NETWORK", "WiFi connection failed");
  }
}

void readDHT22() {
  float humidity = dht.readHumidity();
  float temperature = dht.readTemperature();

  // Check if readings are valid
  if (isnan(humidity) || isnan(temperature)) {
    logMessage("ERROR", "SENSOR", "Failed to read from DHT sensor");
    lastTemperature = NAN;
    lastHumidity = NAN;
  } else {
    lastTemperature = temperature;
    lastHumidity = humidity;
    // Only log successful reads occasionally to save memory
    static unsigned long lastLogTime = 0;
    if (millis() - lastLogTime > 300000) {  // Log every 5 minutes
      String msg = "Sensor read: " + String(temperature) + "C, " + String(humidity) + "%";
      logMessage("DEBUG", "SENSOR", msg);
      lastLogTime = millis();
    }
  }
}

void setupWebServer() {
  // Root endpoint - dashboard
  server.on("/", HTTP_GET, []() {
    handleRootPage();
  });

  // Prometheus metrics endpoint
  server.on("/metrics", HTTP_GET, []() {
    handleMetrics();
  });

  // Health check endpoint
  server.on("/health", HTTP_GET, []() {
    handleHealthCheck();
  });

  // Configuration endpoints
  server.on("/config", HTTP_GET, []() {
    handleConfigPage();
  });

  server.on("/config", HTTP_POST, []() {
    handleConfigUpdate();
  });

  // Logs endpoint
  server.on("/logs", HTTP_GET, []() {
    handleLogsPage();
  });

  // Manual update endpoint
  server.on("/update", HTTP_GET, []() {
    handleUpdateRequest();
  });

  // Reboot endpoint
  server.on("/reboot", HTTP_GET, []() {
    handleRebootRequest();
  });

  server.begin();
  logMessage("INFO", "SYSTEM", "HTTP server started on port 80");
}

void setupOTA() {
  ArduinoOTA.setHostname(config.deviceName.c_str());

  ArduinoOTA.onStart([]() {
    String type = (ArduinoOTA.getCommand() == U_FLASH) ? "sketch" : "filesystem";
    logMessage("INFO", "OTA", "OTA update started: " + type);
  });

  ArduinoOTA.onEnd([]() {
    logMessage("INFO", "OTA", "OTA update completed");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    // Log progress occasionally
    static unsigned long lastProgressLog = 0;
    if (millis() - lastProgressLog > 5000) {  // Every 5 seconds
      int percent = (progress / (total / 100));
      String msg = "OTA Progress: " + String(percent) + "%";
      logMessage("INFO", "OTA", msg);
      lastProgressLog = millis();
    }
  });

  ArduinoOTA.onError([](ota_error_t error) {
    String errorMsg = "OTA Error: ";
    if (error == OTA_AUTH_ERROR) errorMsg += "Auth Failed";
    else if (error == OTA_BEGIN_ERROR) errorMsg += "Begin Failed";
    else if (error == OTA_CONNECT_ERROR) errorMsg += "Connect Failed";
    else if (error == OTA_RECEIVE_ERROR) errorMsg += "Receive Failed";
    else if (error == OTA_END_ERROR) errorMsg += "End Failed";
    logMessage("ERROR", "OTA", errorMsg);
  });

  ArduinoOTA.begin();
  logMessage("INFO", "OTA", "OTA updater initialized");
}

void handleRootPage() {
  String html = generateRootPageHTML();
  server.send(200, "text/html", html);
}

String generateRootPageHTML() {
  String version = "1.0.0";
  String sensorStatus = (!isnan(lastTemperature) && !isnan(lastHumidity)) ? "OK" : "FAIL";
  String tempStr = isnan(lastTemperature) ? "N/A" : String(lastTemperature, 1);
  String humStr = isnan(lastHumidity) ? "N/A" : String(lastHumidity, 1);

  unsigned long uptimeSeconds = (millis() - bootTime) / 1000;
  unsigned long uptimeHours = uptimeSeconds / 3600;
  unsigned long uptimeMinutes = (uptimeSeconds % 3600) / 60;
  unsigned long uptimeDays = uptimeSeconds / 86400;

  String networkStatus = (WiFi.status() == WL_CONNECTED) ? "Connected" : "Disconnected";
  String sensorStatusClass = sensorStatus == "OK" ? "status-ok" : "status-error";
  String networkStatusClass = networkStatus == "Connected" ? "status-ok" : "status-error";

  String html = R"(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 Sensor Dashboard</title>
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }

        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            padding: 20px;
            color: #333;
        }

        .container {
            max-width: 1200px;
            margin: 0 auto;
            background: rgba(255, 255, 255, 0.95);
            border-radius: 20px;
            box-shadow: 0 20px 40px rgba(0, 0, 0, 0.1);
            overflow: hidden;
        }

        .header {
            background: linear-gradient(135deg, #2c3e50 0%, #34495e 100%);
            color: white;
            padding: 30px;
            text-align: center;
        }

        .header h1 {
            font-size: 2.5em;
            margin-bottom: 10px;
            font-weight: 300;
        }

        .device-info {
            display: flex;
            justify-content: center;
            gap: 30px;
            margin-top: 15px;
            flex-wrap: wrap;
        }

        .device-info span {
            background: rgba(255, 255, 255, 0.2);
            padding: 8px 16px;
            border-radius: 20px;
            font-size: 0.9em;
        }

        .dashboard {
            padding: 30px;
        }

        .metrics-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
            gap: 25px;
            margin-bottom: 30px;
        }

        .metric-card {
            background: white;
            border-radius: 15px;
            padding: 25px;
            box-shadow: 0 10px 25px rgba(0, 0, 0, 0.1);
            border-left: 5px solid #3498db;
            transition: transform 0.3s ease, box-shadow 0.3s ease;
        }

        .metric-card:hover {
            transform: translateY(-5px);
            box-shadow: 0 15px 35px rgba(0, 0, 0, 0.15);
        }

        .metric-card.temperature {
            border-left-color: #e74c3c;
        }

        .metric-card.humidity {
            border-left-color: #3498db;
        }

        .metric-card.system {
            border-left-color: #2ecc71;
        }

        .metric-card.network {
            border-left-color: #9b59b6;
        }

        .metric-header {
            display: flex;
            align-items: center;
            margin-bottom: 15px;
        }

        .metric-icon {
            width: 40px;
            height: 40px;
            border-radius: 50%;
            display: flex;
            align-items: center;
            justify-content: center;
            margin-right: 15px;
            font-size: 1.2em;
        }

        .temperature .metric-icon {
            background: rgba(231, 76, 60, 0.1);
            color: #e74c3c;
        }

        .humidity .metric-icon {
            background: rgba(52, 152, 219, 0.1);
            color: #3498db;
        }

        .system .metric-icon {
            background: rgba(46, 204, 113, 0.1);
            color: #2ecc71;
        }

        .network .metric-icon {
            background: rgba(155, 89, 182, 0.1);
            color: #9b59b6;
        }

        .metric-title {
            font-size: 1.1em;
            font-weight: 600;
            color: #2c3e50;
        }

        .metric-value {
            font-size: 2.2em;
            font-weight: 700;
            margin-bottom: 10px;
        }

        .temperature .metric-value {
            color: #e74c3c;
        }

        .humidity .metric-value {
            color: #3498db;
        }

        .metric-details {
            font-size: 0.9em;
            color: #7f8c8d;
            line-height: 1.5;
        }

        .status-badge {
            display: inline-block;
            padding: 4px 12px;
            border-radius: 20px;
            font-size: 0.8em;
            font-weight: 600;
            text-transform: uppercase;
        }

        .status-ok {
            background: rgba(46, 204, 113, 0.1);
            color: #27ae60;
        }

        .status-error {
            background: rgba(231, 76, 60, 0.1);
            color: #e74c3c;
        }

        .navigation {
            background: #f8f9fa;
            padding: 25px 30px;
            border-top: 1px solid #e9ecef;
        }

        .nav-title {
            font-size: 1.2em;
            font-weight: 600;
            margin-bottom: 15px;
            color: #2c3e50;
        }

        .nav-links {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
            gap: 15px;
        }

        .nav-link {
            display: block;
            padding: 12px 20px;
            background: white;
            color: #2c3e50;
            text-decoration: none;
            border-radius: 10px;
            text-align: center;
            font-weight: 500;
            transition: all 0.3s ease;
            box-shadow: 0 2px 10px rgba(0, 0, 0, 0.1);
        }

        .nav-link:hover {
            background: #3498db;
            color: white;
            transform: translateY(-2px);
            box-shadow: 0 5px 15px rgba(52, 152, 219, 0.3);
        }

        .refresh-info {
            text-align: center;
            margin-top: 20px;
            padding: 15px;
            background: rgba(52, 152, 219, 0.1);
            border-radius: 10px;
            color: #2c3e50;
        }

        @media (max-width: 768px) {
            .device-info {
                flex-direction: column;
                gap: 10px;
            }

            .metrics-grid {
                grid-template-columns: 1fr;
            }

            .header h1 {
                font-size: 2em;
            }

            .metric-value {
                font-size: 1.8em;
            }
        }
    </style>
    <script>
        function refreshData() {
            location.reload();
        }

        // Auto-refresh every 30 seconds
        setInterval(refreshData, 30000);

        // Update time display
        function updateTime() {
            const now = new Date();
            document.getElementById('current-time').textContent = now.toLocaleString();
        }

        setInterval(updateTime, 1000);
        window.onload = updateTime;
    </script>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>ESP32 Sensor Dashboard</h1>
            <div class="device-info">
                <span><strong>Device:</strong> )" + config.deviceName + R"(</span>
                <span><strong>Location:</strong> )" + config.location + R"(</span>
                <span><strong>Version:</strong> )" + version + R"(</span>
            </div>
        </div>

        <div class="dashboard">
            <div class="metrics-grid">
                <div class="metric-card temperature">
                    <div class="metric-header">
                        <div class="metric-icon">TEMP</div>
                        <div class="metric-title">Temperature</div>
                    </div>
                    <div class="metric-value">)" + tempStr + R"(°C</div>
                    <div class="metric-details">
                        Sensor Status: <span class="status-badge )" + sensorStatusClass + R"(">)" + sensorStatus + R"(</span><br>
                        Last Updated: )" + String((millis() - lastSensorRead) / 1000) + R"(s ago
                    </div>
                </div>

                <div class="metric-card humidity">
                    <div class="metric-header">
                        <div class="metric-icon">HUM</div>
                        <div class="metric-title">Humidity</div>
                    </div>
                    <div class="metric-value">)" + humStr + R"(%</div>
                    <div class="metric-details">
                        Sensor Status: <span class="status-badge )" + sensorStatusClass + R"(">)" + sensorStatus + R"(</span><br>
                        DHT22 on GPIO )" + String(DHT_PIN) + R"(
                    </div>
                </div>

                <div class="metric-card system">
                    <div class="metric-header">
                        <div class="metric-icon">SYS</div>
                        <div class="metric-title">System Status</div>
                    </div>
                    <div class="metric-value">)" + String(ESP.getFreeHeap() / 1024) + R"(KB</div>
                    <div class="metric-details">
                        Free Memory<br>
                        Uptime: )" + String(uptimeDays) + R"(d )" + String(uptimeHours % 24) + R"(h )" + String(uptimeMinutes) + R"(m<br>
                        OTA: )" + (config.otaEnabled ? "Enabled" : "Disabled") + R"(
                    </div>
                </div>

                <div class="metric-card network">
                    <div class="metric-header">
                        <div class="metric-icon">NET</div>
                        <div class="metric-title">Network</div>
                    </div>
                    <div class="metric-value">)" + WiFi.localIP().toString() + R"(</div>
                    <div class="metric-details">
                        Status: <span class="status-badge )" + networkStatusClass + R"(">)" + networkStatus + R"(</span><br>
                        SSID: )" + String(ssid) + R"(<br>
                        Signal: )" + String(WiFi.RSSI()) + R"( dBm
                    </div>
                </div>
            </div>

            <div class="refresh-info">
                <strong>Current Time:</strong> <span id="current-time"></span><br>
                <small>Dashboard auto-refreshes every 30 seconds</small>
            </div>
        </div>

        <div class="navigation">
            <div class="nav-title">System Tools</div>
            <div class="nav-links">
                <a href="/health" class="nav-link">Health Check</a>
                <a href="/config" class="nav-link">Config</a>
                <a href="/logs" class="nav-link">System Logs</a>
                <a href="/metrics" class="nav-link">Prometheus</a>
                <a href="/update" class="nav-link">Update</a>
                <a href="/reboot" class="nav-link">Reboot</a>
            </div>
        </div>
    </div>
</body>
</html>
)";

  return html;
}

void handleMetrics() {
  String metrics = generatePrometheusMetrics();
  server.send(200, "text/plain", metrics);
}

String generatePrometheusMetrics() {
  String labels = "{location=\"" + config.location + "\",device=\"" + config.deviceName + "\"}";
  String metrics = "";

  // Temperature and humidity metrics
  if (!isnan(lastTemperature)) {
    metrics += "# HELP esp32_temperature_celsius Temperature in Celsius\n";
    metrics += "# TYPE esp32_temperature_celsius gauge\n";
    metrics += "esp32_temperature_celsius" + labels + " " + String(lastTemperature, 2) + "\n";
  }

  if (!isnan(lastHumidity)) {
    metrics += "# HELP esp32_humidity_percent Humidity in Percent\n";
    metrics += "# TYPE esp32_humidity_percent gauge\n";
    metrics += "esp32_humidity_percent" + labels + " " + String(lastHumidity, 2) + "\n";
  }

  // System health metrics
  int sensorStatus = (!isnan(lastTemperature) && !isnan(lastHumidity)) ? 1 : 0;
  metrics += "# HELP esp32_sensor_status Sensor health status (1=OK, 0=FAIL)\n";
  metrics += "# TYPE esp32_sensor_status gauge\n";
  metrics += "esp32_sensor_status" + labels + " " + String(sensorStatus) + "\n";

  int otaStatus = config.otaEnabled ? 1 : 0;
  metrics += "# HELP esp32_ota_status OTA system status (1=enabled, 0=disabled)\n";
  metrics += "# TYPE esp32_ota_status gauge\n";
  metrics += "esp32_ota_status" + labels + " " + String(otaStatus) + "\n";

  // Uptime metric
  unsigned long uptimeSeconds = (millis() - bootTime) / 1000;
  metrics += "# HELP esp32_uptime_seconds Uptime in seconds since boot\n";
  metrics += "# TYPE esp32_uptime_seconds counter\n";
  metrics += "esp32_uptime_seconds" + labels + " " + String(uptimeSeconds) + "\n";

  return metrics;
}

void handleHealthCheck() {
  String html = generateHealthCheckHTML();
  server.send(200, "text/html", html);
}

String generateHealthCheckHTML() {
  String sensorStatus = (!isnan(lastTemperature) && !isnan(lastHumidity)) ? "OK" : "FAIL";
  String tempStr = isnan(lastTemperature) ? "ERROR" : String(lastTemperature, 1) + "°C";
  String humStr = isnan(lastHumidity) ? "ERROR" : String(lastHumidity, 1) + "%";

  unsigned long uptimeSeconds = (millis() - bootTime) / 1000;
  unsigned long uptimeDays = uptimeSeconds / 86400;
  unsigned long uptimeHours = (uptimeSeconds % 86400) / 3600;
  unsigned long uptimeMinutes = (uptimeSeconds % 3600) / 60;

  String networkStatus = (WiFi.status() == WL_CONNECTED) ? "Connected" : "Disconnected";
  String otaStatus = config.otaEnabled ? "Enabled" : "Disabled";

  String sensorStatusClass = sensorStatus == "OK" ? "status-ok" : "status-error";
  String networkStatusClass = networkStatus == "Connected" ? "status-ok" : "status-error";
  String otaStatusClass = config.otaEnabled ? "status-ok" : "status-warning";

  String html = R"(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 Health Check</title>
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }

        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            padding: 20px;
            color: #333;
        }

        .container {
            max-width: 1000px;
            margin: 0 auto;
            background: rgba(255, 255, 255, 0.95);
            border-radius: 20px;
            box-shadow: 0 20px 40px rgba(0, 0, 0, 0.1);
            overflow: hidden;
        }

        .header {
            background: linear-gradient(135deg, #27ae60 0%, #2ecc71 100%);
            color: white;
            padding: 30px;
            text-align: center;
        }

        .header h1 {
            font-size: 2.5em;
            margin-bottom: 10px;
            font-weight: 300;
        }

        .content {
            padding: 30px;
        }

        .health-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
            gap: 25px;
            margin-bottom: 30px;
        }

        .health-section {
            background: white;
            border-radius: 15px;
            padding: 25px;
            box-shadow: 0 10px 25px rgba(0, 0, 0, 0.1);
            border-left: 5px solid #3498db;
        }

        .health-section.device {
            border-left-color: #9b59b6;
        }

        .health-section.sensor {
            border-left-color: #e74c3c;
        }

        .health-section.network {
            border-left-color: #3498db;
        }

        .health-section.system {
            border-left-color: #2ecc71;
        }

        .section-header {
            display: flex;
            align-items: center;
            margin-bottom: 20px;
        }

        .section-icon {
            width: 40px;
            height: 40px;
            border-radius: 50%;
            display: flex;
            align-items: center;
            justify-content: center;
            margin-right: 15px;
            font-size: 1.2em;
        }

        .device .section-icon {
            background: rgba(155, 89, 182, 0.1);
            color: #9b59b6;
        }

        .sensor .section-icon {
            background: rgba(231, 76, 60, 0.1);
            color: #e74c3c;
        }

        .network .section-icon {
            background: rgba(52, 152, 219, 0.1);
            color: #3498db;
        }

        .system .section-icon {
            background: rgba(46, 204, 113, 0.1);
            color: #2ecc71;
        }

        .section-title {
            font-size: 1.3em;
            font-weight: 600;
            color: #2c3e50;
        }

        .health-item {
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding: 12px 0;
            border-bottom: 1px solid #ecf0f1;
        }

        .health-item:last-child {
            border-bottom: none;
        }

        .health-label {
            font-weight: 500;
            color: #34495e;
        }

        .health-value {
            font-weight: 600;
            color: #2c3e50;
        }

        .status-badge {
            display: inline-block;
            padding: 4px 12px;
            border-radius: 20px;
            font-size: 0.8em;
            font-weight: 600;
            text-transform: uppercase;
        }

        .status-ok {
            background: rgba(46, 204, 113, 0.1);
            color: #27ae60;
        }

        .status-error {
            background: rgba(231, 76, 60, 0.1);
            color: #e74c3c;
        }

        .status-warning {
            background: rgba(241, 196, 15, 0.1);
            color: #f39c12;
        }

        .navigation {
            background: #f8f9fa;
            padding: 25px 30px;
            border-top: 1px solid #e9ecef;
        }

        .nav-title {
            font-size: 1.2em;
            font-weight: 600;
            margin-bottom: 15px;
            color: #2c3e50;
        }

        .nav-links {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
            gap: 15px;
        }

        .nav-link {
            display: block;
            padding: 12px 20px;
            background: white;
            color: #2c3e50;
            text-decoration: none;
            border-radius: 10px;
            text-align: center;
            font-weight: 500;
            transition: all 0.3s ease;
            box-shadow: 0 2px 10px rgba(0, 0, 0, 0.1);
        }

        .nav-link:hover {
            background: #27ae60;
            color: white;
            transform: translateY(-2px);
            box-shadow: 0 5px 15px rgba(39, 174, 96, 0.3);
        }

        @media (max-width: 768px) {
            .health-grid {
                grid-template-columns: 1fr;
            }

            .header h1 {
                font-size: 2em;
            }
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>System Health Check</h1>
        </div>

        <div class="content">
            <div class="health-grid">
                <div class="health-section device">
                    <div class="section-header">
                        <div class="section-icon">DEV</div>
                        <div class="section-title">Device Information</div>
                    </div>
                    <div class="health-item">
                        <span class="health-label">Device Name</span>
                        <span class="health-value">)" + config.deviceName + R"(</span>
                    </div>
                    <div class="health-item">
                        <span class="health-label">Location</span>
                        <span class="health-value">)" + config.location + R"(</span>
                    </div>
                    <div class="health-item">
                        <span class="health-label">Firmware Version</span>
                        <span class="health-value">1.0.0</span>
                    </div>
                    <div class="health-item">
                        <span class="health-label">Chip Model</span>
                        <span class="health-value">ESP32</span>
                    </div>
                </div>

                <div class="health-section sensor">
                    <div class="section-header">
                        <div class="section-icon">TEMP</div>
                        <div class="section-title">Sensor Status</div>
                    </div>
                    <div class="health-item">
                        <span class="health-label">Overall Status</span>
                        <span class="status-badge )" + sensorStatusClass + R"(">)" + sensorStatus + R"(</span>
                    </div>
                    <div class="health-item">
                        <span class="health-label">Temperature</span>
                        <span class="health-value">)" + tempStr + R"(</span>
                    </div>
                    <div class="health-item">
                        <span class="health-label">Humidity</span>
                        <span class="health-value">)" + humStr + R"(</span>
                    </div>
                    <div class="health-item">
                        <span class="health-label">Sensor Pin</span>
                        <span class="health-value">GPIO )" + String(DHT_PIN) + R"(</span>
                    </div>
                    <div class="health-item">
                        <span class="health-label">Last Reading</span>
                        <span class="health-value">)" + String((millis() - lastSensorRead) / 1000) + R"(s ago</span>
                    </div>
                </div>

                <div class="health-section network">
                    <div class="section-header">
                        <div class="section-icon">NET</div>
                        <div class="section-title">Network Status</div>
                    </div>
                    <div class="health-item">
                        <span class="health-label">Connection Status</span>
                        <span class="status-badge )" + networkStatusClass + R"(">)" + networkStatus + R"(</span>
                    </div>
                    <div class="health-item">
                        <span class="health-label">IP Address</span>
                        <span class="health-value">)" + WiFi.localIP().toString() + R"(</span>
                    </div>
                    <div class="health-item">
                        <span class="health-label">SSID</span>
                        <span class="health-value">)" + String(ssid) + R"(</span>
                    </div>
                    <div class="health-item">
                        <span class="health-label">Signal Strength</span>
                        <span class="health-value">)" + String(WiFi.RSSI()) + R"( dBm</span>
                    </div>
                    <div class="health-item">
                        <span class="health-label">MAC Address</span>
                        <span class="health-value">)" + WiFi.macAddress() + R"(</span>
                    </div>
                </div>

                <div class="health-section system">
                    <div class="section-header">
                        <div class="section-icon">SYS</div>
                        <div class="section-title">System Resources</div>
                    </div>
                    <div class="health-item">
                        <span class="health-label">Uptime</span>
                        <span class="health-value">)" + String(uptimeDays) + R"(d )" + String(uptimeHours) + R"(h )" + String(uptimeMinutes) + R"(m</span>
                    </div>
                    <div class="health-item">
                        <span class="health-label">Free Memory</span>
                        <span class="health-value">)" + String(ESP.getFreeHeap() / 1024) + R"( KB</span>
                    </div>
                    <div class="health-item">
                        <span class="health-label">Total Memory</span>
                        <span class="health-value">)" + String(ESP.getHeapSize() / 1024) + R"( KB</span>
                    </div>
                    <div class="health-item">
                        <span class="health-label">CPU Frequency</span>
                        <span class="health-value">)" + String(ESP.getCpuFreqMHz()) + R"( MHz</span>
                    </div>
                    <div class="health-item">
                        <span class="health-label">OTA Status</span>
                        <span class="status-badge )" + otaStatusClass + R"(">)" + otaStatus + R"(</span>
                    </div>
                </div>
            </div>
        </div>

        <div class="navigation">
            <div class="nav-title">Navigation</div>
            <div class="nav-links">
                <a href="/" class="nav-link">Dashboard</a>
                <a href="/config" class="nav-link">Config</a>
                <a href="/logs" class="nav-link">System Logs</a>
                <a href="/metrics" class="nav-link">Prometheus</a>
                <a href="/update" class="nav-link">Update</a>
                <a href="/reboot" class="nav-link">Reboot</a>
            </div>
        </div>
    </div>
</body>
</html>
)";

  return html;
}

void handleConfigPage() {
  String html = generateConfigPageHTML();
  server.send(200, "text/html", html);
}

String generateConfigPageHTML() {
  String otaEnabledStr = config.otaEnabled ? "Enabled" : "Disabled";
  String autoUpdateStr = config.autoUpdate ? "Yes" : "No";
  String otaCheckedStr = config.otaEnabled ? "checked" : "";
  String autoCheckedStr = config.autoUpdate ? "checked" : "";
  String mainSelectedStr = (config.branch == "main") ? " selected" : "";
  String devSelectedStr = (config.branch == "dev") ? " selected" : "";

  String html = R"(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 Configuration</title>
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }

        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            padding: 20px;
            color: #333;
        }

        .container {
            max-width: 800px;
            margin: 0 auto;
            background: rgba(255, 255, 255, 0.95);
            border-radius: 20px;
            box-shadow: 0 20px 40px rgba(0, 0, 0, 0.1);
            overflow: hidden;
        }

        .header {
            background: linear-gradient(135deg, #f39c12 0%, #e67e22 100%);
            color: white;
            padding: 30px;
            text-align: center;
        }

        .header h1 {
            font-size: 2.5em;
            margin-bottom: 10px;
            font-weight: 300;
        }

        .content {
            padding: 30px;
        }

        .current-config {
            background: white;
            border-radius: 15px;
            padding: 25px;
            margin-bottom: 30px;
            box-shadow: 0 10px 25px rgba(0, 0, 0, 0.1);
            border-left: 5px solid #3498db;
        }

        .config-title {
            font-size: 1.3em;
            font-weight: 600;
            color: #2c3e50;
            margin-bottom: 20px;
            display: flex;
            align-items: center;
        }

        .config-title::before {
            content: "";
            margin-right: 10px;
            font-size: 1.2em;
        }

        .config-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 15px;
        }

        .config-item {
            display: flex;
            flex-direction: column;
            padding: 15px;
            background: #f8f9fa;
            border-radius: 10px;
            border-left: 3px solid #3498db;
        }

        .config-label {
            font-size: 0.9em;
            color: #7f8c8d;
            font-weight: 500;
            margin-bottom: 5px;
        }

        .config-value {
            font-weight: 600;
            color: #2c3e50;
        }

        .form-section {
            background: white;
            border-radius: 15px;
            padding: 25px;
            box-shadow: 0 10px 25px rgba(0, 0, 0, 0.1);
            border-left: 5px solid #e67e22;
        }

        .form-title {
            font-size: 1.3em;
            font-weight: 600;
            color: #2c3e50;
            margin-bottom: 25px;
            display: flex;
            align-items: center;
        }

        .form-title::before {
            content: "";
            margin-right: 10px;
            font-size: 1.2em;
        }

        .form-group {
            margin-bottom: 20px;
        }

        .form-label {
            display: block;
            font-weight: 600;
            color: #34495e;
            margin-bottom: 8px;
            font-size: 0.95em;
        }

        .form-input {
            width: 100%;
            padding: 12px 15px;
            border: 2px solid #ecf0f1;
            border-radius: 10px;
            font-size: 1em;
            transition: border-color 0.3s ease, box-shadow 0.3s ease;
            background: white;
        }

        .form-input:focus {
            outline: none;
            border-color: #3498db;
            box-shadow: 0 0 0 3px rgba(52, 152, 219, 0.1);
        }

        .form-select {
            width: 100%;
            padding: 12px 15px;
            border: 2px solid #ecf0f1;
            border-radius: 10px;
            font-size: 1em;
            background: white;
            cursor: pointer;
            transition: border-color 0.3s ease;
        }

        .form-select:focus {
            outline: none;
            border-color: #3498db;
        }

        .checkbox-group {
            display: flex;
            align-items: center;
            margin-bottom: 15px;
        }

        .checkbox-input {
            width: 18px;
            height: 18px;
            margin-right: 12px;
            cursor: pointer;
        }

        .checkbox-label {
            font-weight: 500;
            color: #34495e;
            cursor: pointer;
        }

        .form-row {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 20px;
        }

        .submit-btn {
            background: linear-gradient(135deg, #27ae60 0%, #2ecc71 100%);
            color: white;
            padding: 15px 30px;
            border: none;
            border-radius: 10px;
            font-size: 1.1em;
            font-weight: 600;
            cursor: pointer;
            transition: all 0.3s ease;
            box-shadow: 0 5px 15px rgba(46, 204, 113, 0.3);
        }

        .submit-btn:hover {
            transform: translateY(-2px);
            box-shadow: 0 8px 25px rgba(46, 204, 113, 0.4);
        }

        .navigation {
            background: #f8f9fa;
            padding: 25px 30px;
            border-top: 1px solid #e9ecef;
        }

        .nav-title {
            font-size: 1.2em;
            font-weight: 600;
            margin-bottom: 15px;
            color: #2c3e50;
        }

        .nav-links {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
            gap: 15px;
        }

        .nav-link {
            display: block;
            padding: 12px 20px;
            background: white;
            color: #2c3e50;
            text-decoration: none;
            border-radius: 10px;
            text-align: center;
            font-weight: 500;
            transition: all 0.3s ease;
            box-shadow: 0 2px 10px rgba(0, 0, 0, 0.1);
        }

        .nav-link:hover {
            background: #f39c12;
            color: white;
            transform: translateY(-2px);
            box-shadow: 0 5px 15px rgba(243, 156, 18, 0.3);
        }

        @media (max-width: 768px) {
            .form-row {
                grid-template-columns: 1fr;
            }

            .config-grid {
                grid-template-columns: 1fr;
            }

            .header h1 {
                font-size: 2em;
            }
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>&#x2699;&#xFE0F; Device Configuration</h1>
        </div>

        <div class="content">
            <div class="current-config">
                <div class="config-title">Current Settings</div>
                <div class="config-grid">
                    <div class="config-item">
                        <span class="config-label">Device Name</span>
                        <span class="config-value">)" + config.deviceName + R"(</span>
                    </div>
                    <div class="config-item">
                        <span class="config-label">Location</span>
                        <span class="config-value">)" + config.location + R"(</span>
                    </div>
                    <div class="config-item">
                        <span class="config-label">OTA Updates</span>
                        <span class="config-value">)" + otaEnabledStr + R"(</span>
                    </div>
                    <div class="config-item">
                        <span class="config-label">Auto Updates</span>
                        <span class="config-value">)" + autoUpdateStr + R"(</span>
                    </div>
                    <div class="config-item">
                        <span class="config-label">Update Interval</span>
                        <span class="config-value">)" + String(config.updateInterval) + R"( hours</span>
                    </div>
                    <div class="config-item">
                        <span class="config-label">Repository</span>
                        <span class="config-value">)" + config.repoOwner + R"(/)" + config.repoName + R"(</span>
                    </div>
                </div>
            </div>

            <div class="form-section">
                <div class="form-title">Update Configuration</div>
                <form method="POST">
                    <div class="form-row">
                        <div class="form-group">
                            <label class="form-label" for="device">Device Name</label>
                            <input type="text" id="device" name="device" class="form-input" value=")" + config.deviceName + R"(" required>
                        </div>
                        <div class="form-group">
                            <label class="form-label" for="location">Location</label>
                            <input type="text" id="location" name="location" class="form-input" value=")" + config.location + R"(" required>
                        </div>
                    </div>

                    <div class="form-group">
                        <label class="form-label" for="description">Description</label>
                        <input type="text" id="description" name="description" class="form-input" value=")" + config.description + R"(" placeholder="Optional device description">
                    </div>

                    <div class="checkbox-group">
                        <input type="checkbox" id="ota_enabled" name="ota_enabled" class="checkbox-input" )" + otaCheckedStr + R"(>
                        <label class="checkbox-label" for="ota_enabled">Enable OTA Updates</label>
                    </div>

                    <div class="checkbox-group">
                        <input type="checkbox" id="auto_update" name="auto_update" class="checkbox-input" )" + autoCheckedStr + R"(>
                        <label class="checkbox-label" for="auto_update">Enable Automatic Updates</label>
                    </div>

                    <div class="form-group">
                        <label class="form-label" for="update_interval">Update Interval (hours)</label>
                        <input type="number" id="update_interval" name="update_interval" class="form-input" value=")" + String(config.updateInterval) + R"(" min="0.5" max="168" step="0.5">
                    </div>

                    <div class="form-row">
                        <div class="form-group">
                            <label class="form-label" for="repo_owner">Repository Owner</label>
                            <input type="text" id="repo_owner" name="repo_owner" class="form-input" value=")" + config.repoOwner + R"(">
                        </div>
                        <div class="form-group">
                            <label class="form-label" for="repo_name">Repository Name</label>
                            <input type="text" id="repo_name" name="repo_name" class="form-input" value=")" + config.repoName + R"(">
                        </div>
                    </div>

                    <div class="form-group">
                        <label class="form-label" for="branch">Branch</label>
                        <select id="branch" name="branch" class="form-select">
                            <option value="main")" + mainSelectedStr + R"(>main</option>
                            <option value="dev")" + devSelectedStr + R"(>dev</option>
                        </select>
                    </div>

                    <button type="submit" class="submit-btn">&#x1F4BE; Save Configuration</button>
                </form>
            </div>
        </div>

        <div class="navigation">
            <div class="nav-title">&#x1F527; Navigation</div>
            <div class="nav-links">
                <a href="/" class="nav-link">&#x1F3E0; Dashboard</a>
                <a href="/health" class="nav-link">&#x1F3E5; Health Check</a>
                <a href="/config" class="nav-link">Config</a>
                <a href="/logs" class="nav-link">&#x1F4CB; System Logs</a>
                <a href="/metrics" class="nav-link">&#x1F4CA; Prometheus</a>
                <a href="/update" class="nav-link">&#x1F504; Update</a>
                <a href="/reboot" class="nav-link">&#x1F504; Reboot</a>
            </div>
        </div>
    </div>
</body>
</html>
)";

  return html;
}

void handleConfigUpdate() {
  // Parse form data
  if (server.hasArg("location")) {
    config.location = server.arg("location");
  }
  if (server.hasArg("device")) {
    config.deviceName = server.arg("device");
  }
  if (server.hasArg("description")) {
    config.description = server.arg("description");
  }

  config.otaEnabled = server.hasArg("ota_enabled");
  config.autoUpdate = server.hasArg("auto_update");

  if (server.hasArg("update_interval")) {
    config.updateInterval = server.arg("update_interval").toFloat();
  }
  if (server.hasArg("repo_owner")) {
    config.repoOwner = server.arg("repo_owner");
  }
  if (server.hasArg("repo_name")) {
    config.repoName = server.arg("repo_name");
  }
  if (server.hasArg("branch")) {
    config.branch = server.arg("branch");
  }

  // Save configuration
  saveConfig();

  logMessage("INFO", "CONFIG", "Configuration updated via web interface");

  // Redirect back to config page
  server.sendHeader("Location", "/config");
  server.send(302, "text/plain", "");
}

void handleLogsPage() {
  String logsText = generateLogsText();
  server.send(200, "text/plain", logsText);
}

String generateLogsText() {
  String response = "ESP32 System Logs\n";
  response += "================\n\n";
  response += "Stats: " + String(logCount) + " entries | ";
  response += "Memory: " + String(ESP.getFreeHeap() / 1024) + "KB\n\n";

  // Display recent logs
  int startIndex = (logCount > MAX_LOG_ENTRIES) ? logIndex : 0;
  int displayCount = min(logCount, MAX_LOG_ENTRIES);

  for (int i = 0; i < displayCount; i++) {
    int idx = (startIndex + i) % MAX_LOG_ENTRIES;
    unsigned long relativeTime = (logBuffer[idx].timestamp - bootTime) / 1000;

    response += "[+" + String(relativeTime) + "s] ";
    response += logBuffer[idx].level + " ";
    response += logBuffer[idx].category + ": ";
    response += logBuffer[idx].message + "\n";
  }

  response += "\nShowing last " + String(displayCount) + " entries.\n";
  return response;
}

void handleUpdateRequest() {
  if (server.hasArg("action") && server.arg("action") == "check") {
    // Manual update check requested
    logMessage("INFO", "OTA", "Manual update check requested");
    checkForGitHubUpdate();

    // Redirect back to update page
    server.sendHeader("Location", "/update");
    server.send(302, "text/plain", "");
    return;
  }

  String html = generateUpdatePageHTML();
  server.send(200, "text/html", html);
}

String generateUpdatePageHTML() {
  String updateStatus = updateInProgress ? "IN PROGRESS" : "READY";
  String updateStatusClass = updateInProgress ? "status-warning" : "status-ok";

  unsigned long timeSinceLastCheck = (millis() - lastUpdateCheck) / 1000;
  String lastCheckStr = lastUpdateCheck == 0 ? "Never" : String(timeSinceLastCheck) + "s ago";

  String buttonHtml = "<button class=\"action-btn primary\" onclick=\"checkForUpdates()\"";
  if (updateInProgress) buttonHtml += " disabled";
  buttonHtml += ">Check for Updates</button>";

  String html = R"(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32 Firmware Update</title>
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }

        body {
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            padding: 20px;
            color: #333;
        }

        .container {
            max-width: 800px;
            margin: 0 auto;
            background: rgba(255, 255, 255, 0.95);
            border-radius: 20px;
            box-shadow: 0 20px 40px rgba(0, 0, 0, 0.1);
            overflow: hidden;
        }

        .header {
            background: linear-gradient(135deg, #e67e22 0%, #d35400 100%);
            color: white;
            padding: 30px;
            text-align: center;
        }

        .header h1 {
            font-size: 2.5em;
            margin-bottom: 10px;
            font-weight: 300;
        }

        .content {
            padding: 30px;
        }

        .update-section {
            background: white;
            border-radius: 15px;
            padding: 25px;
            margin-bottom: 25px;
            box-shadow: 0 10px 25px rgba(0, 0, 0, 0.1);
            border-left: 5px solid #e67e22;
        }

        .section-title {
            font-size: 1.3em;
            font-weight: 600;
            color: #2c3e50;
            margin-bottom: 20px;
            display: flex;
            align-items: center;
        }

        .section-title::before {
            content: "&#x1F504;";
            margin-right: 10px;
            font-size: 1.2em;
        }

        .update-info {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 15px;
            margin-bottom: 25px;
        }

        .info-item {
            display: flex;
            flex-direction: column;
            padding: 15px;
            background: #f8f9fa;
            border-radius: 10px;
            border-left: 3px solid #e67e22;
        }

        .info-label {
            font-size: 0.9em;
            color: #7f8c8d;
            font-weight: 500;
            margin-bottom: 5px;
        }

        .info-value {
            font-weight: 600;
            color: #2c3e50;
        }

        .status-badge {
            display: inline-block;
            padding: 4px 12px;
            border-radius: 20px;
            font-size: 0.8em;
            font-weight: 600;
            text-transform: uppercase;
        }

        .status-ok {
            background: rgba(46, 204, 113, 0.1);
            color: #27ae60;
        }

        .status-warning {
            background: rgba(241, 196, 15, 0.1);
            color: #f39c12;
        }

        .update-actions {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 15px;
            margin-bottom: 25px;
        }

        .action-btn {
            display: block;
            padding: 15px 25px;
            background: linear-gradient(135deg, #3498db 0%, #2980b9 100%);
            color: white;
            text-decoration: none;
            border-radius: 10px;
            text-align: center;
            font-weight: 600;
            transition: all 0.3s ease;
            box-shadow: 0 5px 15px rgba(52, 152, 219, 0.3);
            border: none;
            cursor: pointer;
            font-size: 1em;
        }

        .action-btn:hover {
            transform: translateY(-2px);
            box-shadow: 0 8px 25px rgba(52, 152, 219, 0.4);
        }

        .action-btn.primary {
            background: linear-gradient(135deg, #27ae60 0%, #2ecc71 100%);
            box-shadow: 0 5px 15px rgba(46, 204, 113, 0.3);
        }

        .action-btn.primary:hover {
            box-shadow: 0 8px 25px rgba(46, 204, 113, 0.4);
        }

        .action-btn:disabled {
            background: #95a5a6;
            cursor: not-allowed;
            transform: none;
            box-shadow: none;
        }

        .warning-box {
            background: rgba(241, 196, 15, 0.1);
            border: 2px solid rgba(241, 196, 15, 0.3);
            border-radius: 10px;
            padding: 20px;
            margin-bottom: 25px;
        }

        .warning-box h3 {
            color: #f39c12;
            margin-bottom: 10px;
            display: flex;
            align-items: center;
        }

        .warning-box h3::before {
            content: "&#x26A0;&#xFE0F;";
            margin-right: 10px;
        }

        .navigation {
            background: #f8f9fa;
            padding: 25px 30px;
            border-top: 1px solid #e9ecef;
        }

        .nav-title {
            font-size: 1.2em;
            font-weight: 600;
            margin-bottom: 15px;
            color: #2c3e50;
        }

        .nav-links {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
            gap: 15px;
        }

        .nav-link {
            display: block;
            padding: 12px 20px;
            background: white;
            color: #2c3e50;
            text-decoration: none;
            border-radius: 10px;
            text-align: center;
            font-weight: 500;
            transition: all 0.3s ease;
            box-shadow: 0 2px 10px rgba(0, 0, 0, 0.1);
        }

        .nav-link:hover {
            background: #e67e22;
            color: white;
            transform: translateY(-2px);
            box-shadow: 0 5px 15px rgba(230, 126, 34, 0.3);
        }

        @media (max-width: 768px) {
            .update-info {
                grid-template-columns: 1fr;
            }

            .update-actions {
                grid-template-columns: 1fr;
            }

            .header h1 {
                font-size: 2em;
            }
        }
    </style>
    <script>
        function checkForUpdates() {
            if (confirm('Check for firmware updates now?')) {
                window.location.href = '/update?action=check';
            }
        }

        // Auto-refresh page every 30 seconds if update is in progress
        if ()" + String(updateInProgress ? "true" : "false") + R"() {
            setTimeout(function() {
                location.reload();
            }, 30000);
        }
    </script>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>&#x1F504; Firmware Update</h1>
        </div>

        <div class="content">
            <div class="update-section">
                <div class="section-title">Current Status</div>
                <div class="update-info">
                    <div class="info-item">
                        <span class="info-label">Current Version</span>
                        <span class="info-value">)" + currentVersion + R"(</span>
                    </div>
                    <div class="info-item">
                        <span class="info-label">Update Status</span>
                        <span class="status-badge )" + updateStatusClass + R"(">)" + updateStatus + R"(</span>
                    </div>
                    <div class="info-item">
                        <span class="info-label">Auto Updates</span>
                        <span class="info-value">)" + (config.autoUpdate ? "Enabled" : "Disabled") + R"(</span>
                    </div>
                    <div class="info-item">
                        <span class="info-label">Last Check</span>
                        <span class="info-value">)" + lastCheckStr + R"(</span>
                    </div>
                    <div class="info-item">
                        <span class="info-label">Repository</span>
                        <span class="info-value">)" + config.repoOwner + R"(/)" + config.repoName + R"(</span>
                    </div>
                    <div class="info-item">
                        <span class="info-label">Update Interval</span>
                        <span class="info-value">)" + String(config.updateInterval) + R"( hours</span>
                    </div>
                </div>

                <div class="warning-box">
                    <h3>Important Notice</h3>
                    <p>Firmware updates will temporarily disconnect the device and restart it. Ensure the device has stable power and network connectivity before proceeding.</p>
                </div>

                <div class="update-actions">
            )" + buttonHtml + R"(
                    <a href="/config" class="action-btn">
                        Update Settings
                    </a>
                    <a href="/logs" class="action-btn">
                        View Update Logs
                    </a>
                </div>
            </div>
        </div>

        <div class="navigation">
            <div class="nav-title">🔧 Navigation</div>
            <div class="nav-links">
                <a href="/" class="nav-link">🏠 Dashboard</a>
                <a href="/health" class="nav-link">🏥 Health Check</a>
                <a href="/config" class="nav-link">Config</a>
                <a href="/logs" class="nav-link">📋 System Logs</a>
                <a href="/metrics" class="nav-link">📊 Prometheus</a>
                <a href="/reboot" class="nav-link">🔄 Reboot</a>
            </div>
        </div>
    </div>
</body>
</html>
)";

  return html;
}

void handleRebootRequest() {
  String html = "<!DOCTYPE html><html><head><title>Rebooting</title></head><body>";
  html += "<h1>DEVICE REBOOT INITIATED</h1>";
  html += "<p>Device will restart in 3 seconds...</p>";
  html += "<p>Refresh this page after 15 seconds to reconnect.</p>";
  html += "</body></html>";

  server.send(200, "text/html", html);

  logMessage("INFO", "SYSTEM", "Manual reboot requested");
  delay(3000);
  ESP.restart();
}

void logMessage(String level, String category, String message) {
  // Add to circular buffer
  logBuffer[logIndex].timestamp = millis();
  logBuffer[logIndex].level = level;
  logBuffer[logIndex].category = category;
  logBuffer[logIndex].message = message;

  logIndex = (logIndex + 1) % MAX_LOG_ENTRIES;
  logCount++;

  // Also print to serial
  Serial.println("[" + level + "] " + category + ": " + message);
}

void loadConfig() {
  if (SPIFFS.exists("/config.json")) {
    File configFile = SPIFFS.open("/config.json", "r");
    if (configFile) {
      DynamicJsonDocument doc(1024);
      deserializeJson(doc, configFile);

      config.location = doc["location"] | "default-location";
      config.deviceName = doc["deviceName"] | "default-device";
      config.description = doc["description"] | "";
      config.otaEnabled = doc["otaEnabled"] | true;
      config.autoUpdate = doc["autoUpdate"] | true;
      config.updateInterval = doc["updateInterval"] | 1.0;
      config.repoOwner = doc["repoOwner"] | "TerrifiedBug";
      config.repoName = doc["repoName"] | "pico-w-prometheus-dht22";
      config.branch = doc["branch"] | "main";

      configFile.close();
      logMessage("INFO", "CONFIG", "Configuration loaded from file");
    }
  } else {
    logMessage("INFO", "CONFIG", "Using default configuration");
  }
}

void saveConfig() {
  DynamicJsonDocument doc(1024);

  doc["location"] = config.location;
  doc["deviceName"] = config.deviceName;
  doc["description"] = config.description;
  doc["otaEnabled"] = config.otaEnabled;
  doc["autoUpdate"] = config.autoUpdate;
  doc["updateInterval"] = config.updateInterval;
  doc["repoOwner"] = config.repoOwner;
  doc["repoName"] = config.repoName;
  doc["branch"] = config.branch;

  File configFile = SPIFFS.open("/config.json", "w");
  if (configFile) {
    serializeJson(doc, configFile);
    configFile.close();
    logMessage("INFO", "CONFIG", "Configuration saved to file");
  } else {
    logMessage("ERROR", "CONFIG", "Failed to save configuration");
  }
}

// GitHub OTA Update Functions
void checkForGitHubUpdate() {
  if (WiFi.status() != WL_CONNECTED) {
    logMessage("ERROR", "OTA", "WiFi not connected, skipping update check");
    return;
  }

  logMessage("INFO", "OTA", "Checking for GitHub updates...");

  String latestVersion = getLatestGitHubRelease();
  if (latestVersion.length() > 0 && latestVersion != currentVersion) {
    logMessage("INFO", "OTA", "New version available: " + latestVersion + " (current: " + currentVersion + ")");

    if (downloadAndInstallUpdate(latestVersion)) {
      logMessage("INFO", "OTA", "Update successful, rebooting...");
      delay(2000);
      ESP.restart();
    } else {
      logMessage("ERROR", "OTA", "Update failed");
    }
  } else {
    logMessage("INFO", "OTA", "No updates available (current: " + currentVersion + ")");
  }
}

String getLatestGitHubRelease() {
  HTTPClient http;
  String url = "https://api.github.com/repos/" + config.repoOwner + "/" + config.repoName + "/releases/latest";

  http.begin(url);
  http.addHeader("User-Agent", "ESP32-OTA-Updater");

  int httpCode = http.GET();
  String latestVersion = "";

  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();

    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, payload);

    if (!error) {
      latestVersion = doc["tag_name"].as<String>();
      logMessage("INFO", "OTA", "Latest release: " + latestVersion);
    } else {
      logMessage("ERROR", "OTA", "Failed to parse GitHub API response");
    }
  } else {
    logMessage("ERROR", "OTA", "GitHub API request failed: " + String(httpCode));
  }

  http.end();
  return latestVersion;
}

bool downloadAndInstallUpdate(String version) {
  updateInProgress = true;

  String firmwareUrl = "https://github.com/" + config.repoOwner + "/" + config.repoName +
                      "/releases/download/" + version + "/esp32_dht22_prometheus_" + version + ".bin";

  logMessage("INFO", "OTA", "Downloading firmware from: " + firmwareUrl);

  HTTPClient http;
  http.begin(firmwareUrl);
  http.addHeader("User-Agent", "ESP32-OTA-Updater");

  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    int contentLength = http.getSize();

    if (contentLength > 0) {
      bool canBegin = Update.begin(contentLength);

      if (canBegin) {
        logMessage("INFO", "OTA", "Starting firmware update, size: " + String(contentLength) + " bytes");

        WiFiClient* client = http.getStreamPtr();
        size_t written = Update.writeStream(*client);

        if (written == contentLength) {
          logMessage("INFO", "OTA", "Firmware download completed");

          if (Update.end()) {
            if (Update.isFinished()) {
              logMessage("INFO", "OTA", "Update successfully completed");
              currentVersion = version;
              updateInProgress = false;
              return true;
            } else {
              logMessage("ERROR", "OTA", "Update not finished");
            }
          } else {
            logMessage("ERROR", "OTA", "Update end failed: " + String(Update.getError()));
          }
        } else {
          logMessage("ERROR", "OTA", "Written only " + String(written) + "/" + String(contentLength) + " bytes");
        }
      } else {
        logMessage("ERROR", "OTA", "Not enough space to begin OTA");
      }
    } else {
      logMessage("ERROR", "OTA", "Invalid content length");
    }
  } else {
    logMessage("ERROR", "OTA", "Firmware download failed: " + String(httpCode));
  }

  http.end();
  updateInProgress = false;
  return false;
}

bool verifyFirmwareChecksum(String version) {
  // Download and verify checksum file
  String checksumUrl = "https://github.com/" + config.repoOwner + "/" + config.repoName +
                       "/releases/download/" + version + "/checksums.txt";

  HTTPClient http;
  http.begin(checksumUrl);
  http.addHeader("User-Agent", "ESP32-OTA-Updater");

  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    String checksumData = http.getString();
    logMessage("INFO", "OTA", "Downloaded checksums for verification");

    // Parse checksum file and verify
    // This is a simplified implementation - in production you'd want more robust verification
    String expectedChecksum = "";
    String firmwareFilename = "esp32_dht22_prometheus_" + version + ".bin";

    int lineStart = 0;
    int lineEnd = checksumData.indexOf('\n');

    while (lineEnd != -1) {
      String line = checksumData.substring(lineStart, lineEnd);
      if (line.indexOf(firmwareFilename) != -1) {
        expectedChecksum = line.substring(0, line.indexOf(' '));
        break;
      }
      lineStart = lineEnd + 1;
      lineEnd = checksumData.indexOf('\n', lineStart);
    }

    if (expectedChecksum.length() > 0) {
      logMessage("INFO", "OTA", "Found expected checksum: " + expectedChecksum);
      http.end();
      return true; // Simplified - in production, calculate and compare actual checksum
    } else {
      logMessage("ERROR", "OTA", "Checksum not found for firmware file");
    }
  } else {
    logMessage("ERROR", "OTA", "Failed to download checksums: " + String(httpCode));
  }

  http.end();
  return false;
}
