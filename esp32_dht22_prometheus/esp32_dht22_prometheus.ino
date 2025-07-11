#include <WiFi.h>
#include <WebServer.h>
#include <DHT.h>          // Install "DHT sensor library by Adafruit"
#include <ArduinoJson.h>  // Install "ArduinoJson by Benoit Blanchon"
#include <SPIFFS.h>       // Built-in with ESP32
#include <HTTPClient.h>   // Built-in with ESP32
#include <ArduinoOTA.h>   // Built-in with ESP32
#include <Update.h>       // Built-in with ESP32
#include <time.h>         // Built-in
#include "web_pages.h"    // Minified HTML templates

// DHT22 Configuration
#define DHT_PIN 15  // GPIO pin for DHT22
#define DHT_TYPE DHT22
DHT dht(DHT_PIN, DHT_TYPE);

// Web Server
WebServer server(80);

// WiFi Credentials (you'll need to set these)
const char* defaultSsid = "ssid";
const char* defaultPassword = "password";

// Device Configuration
struct DeviceConfig {
  String location = "default-location";
  String deviceName = "default-device";
  String description = "";
  String wifiSsid = "";
  String wifiPassword = "";
  bool autoUpdate = false;  // Default to manual updates only
  float updateInterval = 24.0;  // Default to daily checks
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

const int MAX_LOG_ENTRIES = 50;
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

  // Setup OTA (always enabled)
  setupOTA();

  // Initialize time for accurate timestamps
  configTime(0, 0, "pool.ntp.org");

  logMessage("INFO", "SYSTEM", "ESP32 DHT22 sensor server started");
}

void loop() {
  // Handle web server requests
  server.handleClient();

  // Handle OTA updates (always enabled)
  ArduinoOTA.handle();

  // Check for GitHub updates if auto-update is enabled
  if (config.autoUpdate && !updateInProgress) {
    unsigned long updateIntervalMs = config.updateInterval * 3600000; // Convert hours to milliseconds
    if (millis() - lastUpdateCheck >= updateIntervalMs) {
      checkForGitHubUpdate();
      lastUpdateCheck = millis();
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

  // Set WiFi power management for better stability
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);

  WiFi.begin(config.wifiSsid.c_str(), config.wifiPassword.c_str());

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

  // Config download endpoint
  server.on("/config/download", HTTP_GET, []() {
    handleConfigDownload();
  });

  // Config import endpoint
  server.on("/config/import", HTTP_POST, []() {
    handleConfigImport();
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
  String sensorStatus = (!isnan(lastTemperature) && !isnan(lastHumidity)) ? "OK" : "FAIL";
  String tempStr = isnan(lastTemperature) ? "N/A" : String(lastTemperature, 1);
  String humStr = isnan(lastHumidity) ? "N/A" : String(lastHumidity, 1);

  unsigned long uptimeSeconds = (millis() - bootTime) / 1000;
  unsigned long uptimeHours = uptimeSeconds / 3600;
  unsigned long uptimeMinutes = (uptimeSeconds % 3600) / 60;
  unsigned long uptimeDays = uptimeSeconds / 86400;
  String uptimeStr = String(uptimeDays) + "d " + String(uptimeHours % 24) + "h " + String(uptimeMinutes) + "m";

  String networkStatus = (WiFi.status() == WL_CONNECTED) ? "Connected" : "Disconnected";
  String sensorStatusClass = sensorStatus == "OK" ? "status-ok" : "status-error";
  String networkStatusClass = networkStatus == "Connected" ? "status-ok" : "status-error";

  // Use minified template from PROGMEM
  String html = String(index_html);

  // Replace placeholders with actual values
  html.replace("{{DEVICE_NAME}}", config.deviceName);
  html.replace("{{LOCATION}}", config.location);
  html.replace("{{VERSION}}", currentVersion);
  html.replace("{{TEMPERATURE}}", tempStr);
  html.replace("{{HUMIDITY}}", humStr);
  html.replace("{{SENSOR_STATUS}}", sensorStatus);
  html.replace("{{SENSOR_STATUS_CLASS}}", sensorStatusClass);
  html.replace("{{LAST_UPDATE}}", String((millis() - lastSensorRead) / 1000));
  html.replace("{{DHT_PIN}}", String(DHT_PIN));
  html.replace("{{FREE_MEMORY}}", String(ESP.getFreeHeap() / 1024));
  html.replace("{{UPTIME}}", uptimeStr);
  html.replace("{{OTA_STATUS}}", "Enabled");
  html.replace("{{IP_ADDRESS}}", WiFi.localIP().toString());
  html.replace("{{NETWORK_STATUS}}", networkStatus);
  html.replace("{{NETWORK_STATUS_CLASS}}", networkStatusClass);
  html.replace("{{WIFI_SSID}}", config.wifiSsid);
  html.replace("{{WIFI_RSSI}}", String(WiFi.RSSI()));

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

  // OTA is always enabled now
  metrics += "# HELP esp32_ota_status OTA system status (1=enabled, 0=disabled)\n";
  metrics += "# TYPE esp32_ota_status gauge\n";
  metrics += "esp32_ota_status" + labels + " 1\n";

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
  String uptimeStr = String(uptimeDays) + "d " + String(uptimeHours) + "h " + String(uptimeMinutes) + "m";

  String networkStatus = (WiFi.status() == WL_CONNECTED) ? "Connected" : "Disconnected";
  String otaStatus = "Enabled";  // OTA is always enabled now

  String sensorStatusClass = sensorStatus == "OK" ? "status-ok" : "status-error";
  String networkStatusClass = networkStatus == "Connected" ? "status-ok" : "status-error";
  String otaStatusClass = "status-ok";  // OTA is always enabled

  // Use minified template from PROGMEM
  String html = String(health_html);

  // Replace placeholders with actual values
  html.replace("{{DEVICE_NAME}}", config.deviceName);
  html.replace("{{LOCATION}}", config.location);
  html.replace("{{VERSION}}", currentVersion);
  html.replace("{{SENSOR_STATUS}}", sensorStatus);
  html.replace("{{SENSOR_STATUS_CLASS}}", sensorStatusClass);
  html.replace("{{TEMPERATURE}}", tempStr);
  html.replace("{{HUMIDITY}}", humStr);
  html.replace("{{DHT_PIN}}", String(DHT_PIN));
  html.replace("{{LAST_UPDATE}}", String((millis() - lastSensorRead) / 1000));
  html.replace("{{NETWORK_STATUS}}", networkStatus);
  html.replace("{{NETWORK_STATUS_CLASS}}", networkStatusClass);
  html.replace("{{IP_ADDRESS}}", WiFi.localIP().toString());
  html.replace("{{WIFI_SSID}}", config.wifiSsid);
  html.replace("{{WIFI_RSSI}}", String(WiFi.RSSI()));
  html.replace("{{MAC_ADDRESS}}", WiFi.macAddress());
  html.replace("{{UPTIME}}", uptimeStr);
  html.replace("{{FREE_MEMORY}}", String(ESP.getFreeHeap() / 1024));
  html.replace("{{TOTAL_MEMORY}}", String(ESP.getHeapSize() / 1024));
  html.replace("{{SPIFFS_USED}}", String(SPIFFS.usedBytes() / 1024));
  html.replace("{{CPU_FREQ}}", String(ESP.getCpuFreqMHz()));
  html.replace("{{OTA_STATUS}}", otaStatus);
  html.replace("{{OTA_STATUS_CLASS}}", otaStatusClass);

  return html;
}

void handleConfigPage() {
  String html = generateConfigPageHTML();
  server.send(200, "text/html", html);
}

String generateConfigPageHTML() {
  String autoUpdateStr = config.autoUpdate ? "Yes" : "No";
  String autoCheckedStr = config.autoUpdate ? "checked" : "";
  String mainSelectedStr = (config.branch == "main") ? " selected" : "";
  String devSelectedStr = (config.branch == "dev") ? " selected" : "";

  // Use minified template from PROGMEM
  String html = String(config_html);

  // Replace placeholders with actual values
  html.replace("{{DEVICE_NAME}}", config.deviceName);
  html.replace("{{LOCATION}}", config.location);
  html.replace("{{AUTO_UPDATE}}", autoUpdateStr);
  html.replace("{{UPDATE_INTERVAL}}", String(config.updateInterval));
  html.replace("{{REPO_OWNER}}", config.repoOwner);
  html.replace("{{REPO_NAME}}", config.repoName);
  html.replace("{{DESCRIPTION}}", config.description);
  html.replace("{{WIFI_SSID}}", config.wifiSsid);
  html.replace("{{WIFI_PASSWORD}}", config.wifiPassword);
  html.replace("{{AUTO_CHECKED}}", autoCheckedStr);
  html.replace("{{MAIN_SELECTED}}", mainSelectedStr);
  html.replace("{{DEV_SELECTED}}", devSelectedStr);

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
  if (server.hasArg("wifi_ssid")) {
    config.wifiSsid = server.arg("wifi_ssid");
  }
  if (server.hasArg("wifi_password")) {
    config.wifiPassword = server.arg("wifi_password");
  }

  // OTA is always enabled now, only handle auto-update setting
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
  String response = "📋 ESP32 System Logs\n";
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

  // Use minified template from PROGMEM
  String html = String(update_html);

  // Replace placeholders with actual values
  html.replace("{{CURRENT_VERSION}}", currentVersion);
  html.replace("{{UPDATE_STATUS}}", updateStatus);
  html.replace("{{UPDATE_STATUS_CLASS}}", updateStatusClass);
  html.replace("{{AUTO_UPDATE}}", config.autoUpdate ? "Enabled" : "Disabled");
  html.replace("{{LAST_CHECK}}", lastCheckStr);
  html.replace("{{REPO_OWNER}}", config.repoOwner);
  html.replace("{{REPO_NAME}}", config.repoName);
  html.replace("{{UPDATE_INTERVAL}}", String(config.updateInterval));
  html.replace("{{UPDATE_BUTTON}}", buttonHtml);
  html.replace("{{UPDATE_IN_PROGRESS}}", updateInProgress ? "true" : "false");

  return html;
}

void handleRebootRequest() {
  String html = "<!DOCTYPE html><html><head><title>🔄 Rebooting</title></head><body>";
  html += "<h1>🔄 DEVICE REBOOT INITIATED</h1>";
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
      config.wifiSsid = doc["wifiSsid"] | "";
      config.wifiPassword = doc["wifiPassword"] | "";
      config.autoUpdate = doc["autoUpdate"] | false;  // Default to manual updates
      config.updateInterval = doc["updateInterval"] | 24.0;  // Default to daily checks
      config.repoOwner = doc["repoOwner"] | "TerrifiedBug";
      config.repoName = doc["repoName"] | "esp32-prometheus-dht22";
      config.branch = doc["branch"] | "main";

      configFile.close();
      logMessage("INFO", "CONFIG", "Configuration loaded from file");
    }
  } else {
    logMessage("INFO", "CONFIG", "Using default configuration");
  }

  // Fallback to default WiFi if not configured
  if (config.wifiSsid.length() == 0) {
    config.wifiSsid = defaultSsid;
    config.wifiPassword = defaultPassword;
    logMessage("INFO", "CONFIG", "Using default WiFi credentials");
  }
}

void saveConfig() {
  DynamicJsonDocument doc(1024);

  doc["location"] = config.location;
  doc["deviceName"] = config.deviceName;
  doc["description"] = config.description;
  doc["wifiSsid"] = config.wifiSsid;
  doc["wifiPassword"] = config.wifiPassword;
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
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(300000); // 5 minute timeout for large firmware files
  http.begin(firmwareUrl);
  http.addHeader("User-Agent", "ESP32-OTA-Updater");
  http.addHeader("Accept", "*/*");
  http.addHeader("Accept-Encoding", "identity");

  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    int contentLength = http.getSize();

    if (contentLength > 0) {
      logMessage("INFO", "OTA", "Firmware size: " + String(contentLength) + " bytes");

      // Check available space
      size_t freeSpace = ESP.getFreeSketchSpace();
      logMessage("INFO", "OTA", "Available space: " + String(freeSpace) + " bytes");

      if (contentLength > freeSpace) {
        logMessage("ERROR", "OTA", "Not enough space: need " + String(contentLength) + " bytes, have " + String(freeSpace) + " bytes");
        http.end();
        updateInProgress = false;
        return false;
      }

      bool canBegin = Update.begin(contentLength);

      if (canBegin) {
        logMessage("INFO", "OTA", "Starting firmware update, size: " + String(contentLength) + " bytes");

        WiFiClient* client = http.getStreamPtr();
        size_t written = 0;
        size_t lastProgress = 0;
        unsigned long startTime = millis();

        // Download in chunks with progress reporting
        while (written < contentLength) {
          if (millis() - startTime > 300000) { // 5 minute timeout
            logMessage("ERROR", "OTA", "Download timeout after 5 minutes");
            break;
          }

          if (WiFi.status() != WL_CONNECTED) {
            logMessage("ERROR", "OTA", "WiFi disconnected during download");
            break;
          }

          size_t available = client->available();
          if (available > 0) {
            uint8_t buffer[1024];
            size_t toRead = min(available, sizeof(buffer));
            size_t bytesRead = client->read(buffer, toRead);

            if (bytesRead > 0) {
              size_t bytesWritten = Update.write(buffer, bytesRead);
              written += bytesWritten;

              // Report progress every 10%
              if (written - lastProgress > contentLength / 10) {
                int progress = (written * 100) / contentLength;
                logMessage("INFO", "OTA", "Download progress: " + String(progress) + "%");
                lastProgress = written;
              }
            }
          } else {
            delay(10); // Small delay to prevent watchdog issues
          }
        }

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

// Add config download/import endpoints
void handleConfigDownload() {
  if (SPIFFS.exists("/config.json")) {
    File configFile = SPIFFS.open("/config.json", "r");
    if (configFile) {
      server.sendHeader("Content-Disposition", "attachment; filename=config.json");
      server.sendHeader("Content-Type", "application/json");
      server.streamFile(configFile, "application/json");
      configFile.close();
      return;
    }
  }
  server.send(404, "text/plain", "Config file not found");
}

void handleConfigImport() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      server.send(500, "text/plain", "Failed to open config file for writing");
      return;
    }
    configFile.close();
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    File configFile = SPIFFS.open("/config.json", "a");
    if (configFile) {
      configFile.write(upload.buf, upload.currentSize);
      configFile.close();
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    loadConfig();
    server.sendHeader("Location", "/config");
    server.send(303);
  }
}
