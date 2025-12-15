# IoTTelemetry OTA ![GitHub release](https://img.shields.io/github/v/release/ErickAsas/Main---IoTTelemetry) ![License](https://img.shields.io/badge/license-MIT-blue)

Welcome to **IoTTelemetry OTA**, an ESP32-based telemetry system that brings **automatic over-the-air firmware updates** and robust sensor monitoring to your IoT devices.  

This project ensures your ESP32 devices always run the latest firmware directly from GitHub, with zero manual intervention.  

---

## 🌟 Features

- **Automatic OTA Updates**  
  Devices periodically check a remote GitHub release and update themselves if a new firmware version is available.  

- **Manual OTA Trigger**  
  Send a simple `"ota"` command via Serial or MQTT to initiate an immediate update.  

- **Version Control**  
  Each device tracks its firmware version and compares it against a remote `version.json`.  

- **Secure & Efficient**  
  Optional flash encryption and secure boot protect your firmware with minimal runtime impact.  

- **Easy Integration**  
  Works seamlessly with multiple sensors, logging systems, and your existing IoTTelemetry network.  

---

## ⚙ How It Works

1. Device boots and connects to WiFi.  
2. Fetches `version.json` from GitHub.  
3. Compares local firmware version with the remote version.  
4. If newer firmware exists:  
   - Downloads the `.bin` firmware (follows redirects automatically).  
   - Installs firmware securely.  
   - Restarts automatically with the updated version.  

---

## 🚀 Getting Started

1. Clone this repository to your Arduino or PlatformIO workspace.  
2. Configure WiFi credentials and OTA URLs in the source code.  
3. Upload firmware to your ESP32 device.  
4. Monitor Serial logs for version and OTA status.  

---

## 🔐 Security Notes

- Consider enabling **ESP32 flash encryption** and **secure boot** for production devices.  
- Avoid hardcoding sensitive keys or tokens; fetch them securely at runtime.  

---

## 🖥 OTA Flow Diagram

        ┌───────────────┐
        │ ESP32 Boots   │
        └───────┬───────┘
                │
                ▼
       ┌─────────────────┐
       │ Connect to WiFi │
       └───────┬─────────┘
               │
               ▼
      ┌───────────────────┐
      │ Fetch version.json │
      └───────┬───────────┘
              │
     ┌────────┴─────────┐
     │ Compare versions │
     └────────┬─────────┘
              │
    ┌─────────┴───────────┐
    │ Is remote newer?    │
    └───────┬─────────────┘
Yes ──────▶│
▼
┌───────────────────┐
│ Download firmware │
└───────┬───────────┘
▼
┌───────────────────┐
│ Install & Reboot │
└───────────────────┘
│
▼
┌───────────────┐
│ Running Latest│
└───────────────┘

**IoTTelemetry OTA** makes firmware management **automatic, secure, and reliable**, giving you the freedom to focus on building smarter IoT solutions.
