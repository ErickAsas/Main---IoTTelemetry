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

1️⃣ What you should already have

Confirm these are done (no need to answer yet, just for alignment):
- **✅ A GitHub repository created**
- **✅ Your ESP32 firmware builds a .bin file**
- **✅ You can push commits to the repo from VS Code / PlatformIO**  

2️⃣ Why GitHub Releases (important for OTA)
For OTA, Releases are better than normal repo files because:
Stable, versioned URLs
Public binary hosting (no auth needed)
Easy version comparison (v1.0.3, v1.0.4, etc.)
Works perfectly with HTTPClient / Update.h

Your ESP32 will typically download from a URL like:
https://github.com/<user>/<repo>/releases/download/v1.0.3/firmware.bin

2️⃣ Tag Versioning Strategy

OTA updates rely on semantic versioning:

MAJOR.MINOR.PATCH

MAJOR: Breaking changes
MINOR: New features or sensors
PATCH: Bug fixes, small tweaks

Example progression:

v2.0.8 → v2.0.9 → v2.1.0 → v3.0.0


Important rules for OTA:

1. Do not reuse tags: Each release must have a unique tag.
2. Do not overwrite binaries: Once a .bin is uploaded, it must remain immutable.
3. version.json must match the release: This file is the device’s reference to know the latest version.

3 ️⃣ version.json Example
{
  "version": "2.0.9",
  "tag": "v2.0.9",
  "bin": "firmware.ino.bin",
  "notes": "Bug fixes and OTA stability improvements"
}


Devices fetch this file to check if an update is required.
The tag is used to dynamically construct the download URL.
