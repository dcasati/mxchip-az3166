/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

#ifndef _AZURE_CONFIG_H
#define _AZURE_CONFIG_H

#include "config_manager.h"

typedef enum
{
    None         = 0,
    WEP          = 1,
    WPA_PSK_TKIP = 2,
    WPA2_PSK_AES = 3
} WiFi_Mode;

// Global configuration instance - will be loaded from flash or defaults
extern device_config_t g_device_config;

// ----------------------------------------------------------------------------
// WiFi connection config - fallback defaults (used if flash config fails)
// ----------------------------------------------------------------------------
#define WIFI_SSID_DEFAULT     "MYWIFI"
#define WIFI_PASSWORD_DEFAULT "MYPASS"
#define WIFI_MODE_DEFAULT     WPA2_PSK_AES

// ----------------------------------------------------------------------------
// Custom MQTT Server Configuration - fallback defaults
// ----------------------------------------------------------------------------
#define MQTT_BROKER_HOSTNAME_DEFAULT  "mqtt.dcasati.net"
#define MQTT_BROKER_PORT_DEFAULT      1883
#define MQTT_CLIENT_ID_DEFAULT        "mxchip-client-123456"
#define MQTT_USERNAME_DEFAULT         ""
#define MQTT_PASSWORD_DEFAULT         ""

// ----------------------------------------------------------------------------
// Dynamic macros that use the loaded configuration
// ----------------------------------------------------------------------------
#define WIFI_SSID     (g_device_config.wifi_ssid)
#define WIFI_PASSWORD (g_device_config.wifi_password)
#define WIFI_MODE     (g_device_config.wifi_mode)

// MQTT configuration macros using correct field names
#define MQTT_BROKER_HOSTNAME  (g_device_config.mqtt_hostname)
#define MQTT_BROKER_PORT      (g_device_config.mqtt_port)
#define MQTT_CLIENT_ID        (g_device_config.mqtt_client_id)
#define MQTT_USERNAME         (g_device_config.mqtt_username)
#define MQTT_PASSWORD         (g_device_config.mqtt_password)

// If you're connecting to a hidden SSID, uncomment the line below
// #define WIFI_IS_HIDDEN  1

// ----------------------------------------------------------------------------
// Enable legacy MQTT to use custom MQTT broker 
// ----------------------------------------------------------------------------
#define ENABLE_LEGACY_MQTT

// ----------------------------------------------------------------------------
// MQTT Topics Configuration
// ----------------------------------------------------------------------------
#define MQTT_TELEMETRY_TOPIC  "mxchip/telemetry"      // Simple test topic for telemetry
#define MQTT_COMMAND_TOPIC    "mxchip/command"        // Simple test topic for commands
#define MQTT_LED_TOPIC        "mxchip/led"            // Simple test topic for LED control

// Default telemetry interval in seconds
#define DEFAULT_TELEMETRY_INTERVAL 10

#endif // _AZURE_CONFIG_H
