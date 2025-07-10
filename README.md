# ESP32 DHT22 Prometheus Sensor with GitHub Actions OTA Updates

A comprehensive ESP32-based temperature and humidity monitoring system with automated over-the-air (OTA) firmware updates via GitHub Actions and releases.

## Features

### Core Functionality

- **DHT22 Sensor Monitoring**: Real-time temperature and humidity readings
- **Prometheus Metrics**: Export sensor data in Prometheus format
- **Web Dashboard**: Beautiful, responsive web interface with real-time data
- **Health Monitoring**: System health checks and diagnostics
- **Configuration Management**: Web-based configuration with persistent storage

### Advanced OTA Update System

- **GitHub Actions Integration**: Automated firmware compilation on release
- **Automatic Updates**: Configurable automatic update checking and installation
- **Manual Updates**: Web-based manual update triggering
- **Version Management**: Semantic versioning with release tracking
- **Checksum Verification**: Firmware integrity verification
- **Update Logging**: Comprehensive logging of update processes
- **Rollback Safety**: Safe update process with error handling

## Hardware Requirements

- ESP32 development board
- DHT22 temperature/humidity sensor
- Jumper wires
- Breadboard (optional)

## Wiring

```
DHT22 Sensor -> ESP32
VCC          -> 3.3V
GND          -> GND
DATA         -> GPIO 15
```

## Software Dependencies

The following libraries are automatically installed by the GitHub Actions workflow:

- **DHT sensor library** by Adafruit
- **ArduinoJson** by Benoit Blanchon
- Built-in ESP32 libraries (WiFi, WebServer, SPIFFS, HTTPClient, ArduinoOTA, Update)

## Quick Start

### 1. Hardware Setup

1. Connect the DHT22 sensor to your ESP32 as shown in the wiring diagram
2. Power on your ESP32

### 2. Software Configuration

1. Clone this repository
2. Open `src/esp32_dht22_prometheus.ino` in Arduino IDE
3. Update WiFi credentials:
   ```cpp
   const char* ssid = "YOUR_WIFI_SSID";
   const char* password = "YOUR_WIFI_PASSWORD";
   ```
4. Upload the firmware to your ESP32

### 3. Access the Web Interface

1. Open Serial Monitor to find the ESP32's IP address
2. Navigate to `http://[ESP32_IP]` in your web browser
3. Configure device settings via the web interface

## GitHub Actions OTA Update System

### How It Works

The OTA update system uses GitHub Actions to automatically compile firmware when you create a release, then allows the ESP32 to download and install updates directly from GitHub releases.

#### Workflow Process:

1. **Release Creation**: Create a new release on GitHub with a version tag (e.g., `v1.1.0`)
2. **Automatic Compilation**: GitHub Actions automatically compiles the firmware
3. **Binary Generation**: Creates `.bin` file and checksums
4. **Release Attachment**: Attaches compiled firmware to the GitHub release
5. **Device Update**: ESP32 checks for updates and downloads/installs automatically

### Setting Up OTA Updates

#### 1. Repository Configuration

The GitHub Actions workflow is already configured in `.github/workflows/build-release.yml`. No additional setup required.

#### 2. Device Configuration

Configure OTA settings via the web interface at `http://[ESP32_IP]/config`:

- **Repository Owner**: Your GitHub username
- **Repository Name**: This repository name
- **Auto Updates**: Enable/disable automatic updates
- **Update Interval**: How often to check for updates (hours)
- **Branch**: Which branch to track (main/dev)

#### 3. Creating Releases

1. Go to your GitHub repository
2. Click "Releases" → "Create a new release"
3. Create a new tag (e.g., `v1.1.0`)
4. Add release title and description
5. Click "Publish release"
6. GitHub Actions will automatically build and attach the firmware

### Manual Updates

- Access the update interface at `http://[ESP32_IP]/update`
- Click "Check for Updates" to manually trigger an update check
- Monitor update progress in the system logs

### Update Safety Features

- **Checksum Verification**: Ensures firmware integrity
- **Progress Logging**: Detailed logging of update process
- **Error Handling**: Graceful failure handling with rollback
- **Network Validation**: Checks WiFi connectivity before updates
- **Memory Validation**: Ensures sufficient space for updates

## Web Interface

### Dashboard (`/`)

- Real-time sensor readings
- System status and uptime
- Network information
- Quick navigation to all features

### Health Check (`/health`)

- Comprehensive system diagnostics
- Device information
- Sensor status
- Network connectivity
- System resources

### Configuration (`/config`)

- Device settings
- OTA update configuration
- Network settings
- Persistent configuration storage

### Update Management (`/update`)

- Current firmware version
- Update status and history
- Manual update triggering
- Update configuration

### System Logs (`/logs`)

- Real-time system logging
- Update process logs
- Error tracking
- Debug information

### Prometheus Metrics (`/metrics`)

- Temperature and humidity metrics
- System health metrics
- Uptime tracking
- Sensor status indicators

## API Endpoints

| Endpoint               | Method   | Description                 |
| ---------------------- | -------- | --------------------------- |
| `/`                    | GET      | Main dashboard              |
| `/health`              | GET      | System health check         |
| `/config`              | GET/POST | Device configuration        |
| `/update`              | GET      | Update management interface |
| `/update?action=check` | GET      | Trigger manual update check |
| `/logs`                | GET      | System logs (plain text)    |
| `/metrics`             | GET      | Prometheus metrics          |
| `/reboot`              | GET      | Restart device              |

## Configuration Options

### Device Settings

- **Device Name**: Friendly name for identification
- **Location**: Physical location description
- **Description**: Optional device description

### OTA Update Settings

- **OTA Enabled**: Enable/disable OTA functionality
- **Auto Updates**: Automatic update checking
- **Update Interval**: Check frequency (0.5-168 hours)
- **Repository Owner**: GitHub username
- **Repository Name**: Repository name
- **Branch**: Git branch to track

### Network Settings

- **WiFi SSID**: Network name
- **WiFi Password**: Network password

## Prometheus Integration

The device exports metrics in Prometheus format at `/metrics`:

```
# Temperature in Celsius
esp32_temperature_celsius{location="office",device="sensor-01"} 23.5

# Humidity in Percent
esp32_humidity_percent{location="office",device="sensor-01"} 45.2

# Sensor health status (1=OK, 0=FAIL)
esp32_sensor_status{location="office",device="sensor-01"} 1

# OTA system status (1=enabled, 0=disabled)
esp32_ota_status{location="office",device="sensor-01"} 1

# Uptime in seconds
esp32_uptime_seconds{location="office",device="sensor-01"} 86400
```

## Troubleshooting

### Common Issues

#### WiFi Connection Problems

- Verify SSID and password in the code
- Check WiFi signal strength
- Ensure 2.4GHz network (ESP32 doesn't support 5GHz)

#### Sensor Reading Errors

- Check wiring connections
- Verify DHT22 sensor functionality
- Ensure proper power supply (3.3V)

#### OTA Update Failures

- Check internet connectivity
- Verify GitHub repository settings
- Review system logs for error details
- Ensure sufficient free memory

#### Web Interface Issues

- Clear browser cache
- Check ESP32 IP address
- Verify network connectivity

### Debug Information

- Access system logs at `/logs`
- Monitor Serial output during boot
- Check network connectivity status
- Verify sensor readings in dashboard

## Development

### Building Locally

1. Install Arduino IDE
2. Install ESP32 board package
3. Install required libraries
4. Open `src/esp32_dht22_prometheus.ino`
5. Configure settings and upload

### GitHub Actions Workflow

The workflow automatically:

- Sets up Arduino CLI
- Installs ESP32 platform and libraries
- Compiles firmware for ESP32
- Generates checksums
- Attaches binaries to releases

### Customization

- Modify sensor pin assignments in the code
- Adjust update intervals and timeouts
- Customize web interface styling
- Add additional sensors or features

## Security Considerations

- **Network Security**: Use WPA2/WPA3 protected WiFi
- **Firmware Integrity**: Checksum verification prevents corrupted updates
- **Access Control**: Consider implementing authentication for web interface
- **HTTPS**: GitHub releases use HTTPS for secure downloads

## License

This project is open source. Feel free to modify and distribute according to your needs.

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test thoroughly
5. Submit a pull request

## Support

For issues and questions:

1. Check the troubleshooting section
2. Review system logs
3. Create a GitHub issue with detailed information
4. Include relevant log outputs and configuration details
