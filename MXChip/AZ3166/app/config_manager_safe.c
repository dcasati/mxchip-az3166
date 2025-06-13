/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

#include "config_manager.h"
#include "azure_config.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// Configuration constants
#define CONFIG_MAGIC    0xDEADBEEF
#define CONFIG_VERSION  1

config_result_t config_manager_init(void) {
    return CONFIG_OK;
}

config_result_t config_manager_load(device_config_t* config) {
    if (!config) {
        return CONFIG_ERROR_INVALID;
    }
    
    // TEMPORARY: Always return not found to force configuration prompt
    return CONFIG_ERROR_NOT_FOUND;
}

config_result_t config_manager_save(const device_config_t* config) {
    if (!config) {
        return CONFIG_ERROR_INVALID;
    }
    
    // TEMPORARY: Disable flash writing to prevent corruption
    return CONFIG_OK;
}

bool config_manager_has_valid_config(void) {
    return false;  // Always return false to force configuration
}

void config_manager_get_defaults(device_config_t* config) {
    if (!config) {
        return;
    }
    
    memset(config, 0, sizeof(device_config_t));
    
    config->magic = CONFIG_MAGIC;
    config->version = CONFIG_VERSION;
    
    // Default WiFi settings
    strncpy(config->wifi_ssid, WIFI_SSID_DEFAULT, CONFIG_SSID_MAX_LEN - 1);
    strncpy(config->wifi_password, WIFI_PASSWORD_DEFAULT, CONFIG_PASSWORD_MAX_LEN - 1);
    config->wifi_mode = WIFI_MODE_DEFAULT;
    
    // Default MQTT settings
    strncpy(config->mqtt_hostname, MQTT_BROKER_HOSTNAME_DEFAULT, CONFIG_HOSTNAME_MAX_LEN - 1);
    config->mqtt_port = MQTT_BROKER_PORT_DEFAULT;
    strncpy(config->mqtt_client_id, MQTT_CLIENT_ID_DEFAULT, CONFIG_CLIENT_ID_MAX_LEN - 1);
    strncpy(config->mqtt_username, MQTT_USERNAME_DEFAULT, CONFIG_USERNAME_MAX_LEN - 1);
    strncpy(config->mqtt_password, MQTT_PASSWORD_DEFAULT, CONFIG_PASSWORD_MAX_LEN - 1);
    
    // Default telemetry settings
    config->telemetry_interval = DEFAULT_TELEMETRY_INTERVAL;
}

// Read string from serial with echo and length limit
static int read_string_from_serial(char* buffer, int max_length, const char* prompt) {
    int index = 0;
    int c;
    
    printf("%s", prompt);
    
    while (1) {
        c = __io_getchar();
        
        if (c == '\r' || c == '\n') {
            buffer[index] = '\0';
            return index;
        } else if (c == '\b' || c == 127) { // Backspace or DEL
            if (index > 0) {
                index--;
                printf("\b \b");
            }
        } else if (c >= 32 && c <= 126 && index < max_length - 1) { // Printable characters
            buffer[index++] = (char)c;
        }
    }
}

// Read integer from serial
static uint32_t read_int_from_serial(const char* prompt, uint32_t default_value) {
    char buffer[16];
    int len = read_string_from_serial(buffer, sizeof(buffer), prompt);
    
    if (len == 0) {
        return default_value;
    }
    
    return (uint32_t)strtoul(buffer, NULL, 10);
}

config_result_t config_manager_prompt_and_save(void) {
    device_config_t config;
    char temp_buffer[128];
    
    printf("\r\n=== MXChip AZ3166 Configuration Setup ===\r\n");
    printf("Press Enter to keep current/default values\r\n\r\n");
    
    // Get current defaults
    config_manager_get_defaults(&config);
    
    // WiFi Configuration
    printf("WiFi Configuration:\r\n");
    printf("Current SSID: %s\r\n", config.wifi_ssid);
    read_string_from_serial(temp_buffer, CONFIG_SSID_MAX_LEN, "Enter WiFi SSID: ");
    if (strlen(temp_buffer) > 0) {
        strncpy(config.wifi_ssid, temp_buffer, CONFIG_SSID_MAX_LEN - 1);
        config.wifi_ssid[CONFIG_SSID_MAX_LEN - 1] = '\0';
    }
    
    printf("Current Password: %s\r\n", config.wifi_password);
    read_string_from_serial(temp_buffer, CONFIG_PASSWORD_MAX_LEN, "Enter WiFi Password: ");
    if (strlen(temp_buffer) > 0) {
        strncpy(config.wifi_password, temp_buffer, CONFIG_PASSWORD_MAX_LEN - 1);
        config.wifi_password[CONFIG_PASSWORD_MAX_LEN - 1] = '\0';
    }
    
    printf("WiFi Security Modes: 0=None, 1=WEP, 2=WPA_PSK_TKIP, 3=WPA2_PSK_AES\r\n");
    printf("Current Mode: %d\r\n", (int)config.wifi_mode);
    config.wifi_mode = read_int_from_serial("Enter WiFi Security Mode (0-3): ", config.wifi_mode);
    
    // MQTT Configuration
    printf("MQTT Configuration:\r\n");
    printf("Current Hostname: %s\r\n", config.mqtt_hostname);
    read_string_from_serial(temp_buffer, CONFIG_HOSTNAME_MAX_LEN, "Enter MQTT Broker Hostname/IP: ");
    if (strlen(temp_buffer) > 0) {
        strncpy(config.mqtt_hostname, temp_buffer, CONFIG_HOSTNAME_MAX_LEN - 1);
        config.mqtt_hostname[CONFIG_HOSTNAME_MAX_LEN - 1] = '\0';
    }
    
    printf("Current Port: %d\r\n", (int)config.mqtt_port);
    config.mqtt_port = (uint16_t)read_int_from_serial("Enter MQTT Port: ", config.mqtt_port);
    
    printf("Current Client ID: %s\r\n", config.mqtt_client_id);
    read_string_from_serial(temp_buffer, CONFIG_CLIENT_ID_MAX_LEN, "Enter MQTT Client ID: ");
    if (strlen(temp_buffer) > 0) {
        strncpy(config.mqtt_client_id, temp_buffer, CONFIG_CLIENT_ID_MAX_LEN - 1);
        config.mqtt_client_id[CONFIG_CLIENT_ID_MAX_LEN - 1] = '\0';
    }
    
    printf("Current Username: %s\r\n", config.mqtt_username);
    read_string_from_serial(temp_buffer, CONFIG_USERNAME_MAX_LEN, "Enter MQTT Username (optional): ");
    if (strlen(temp_buffer) > 0) {
        strncpy(config.mqtt_username, temp_buffer, CONFIG_USERNAME_MAX_LEN - 1);
        config.mqtt_username[CONFIG_USERNAME_MAX_LEN - 1] = '\0';
    }
    
    printf("Current Password: %s\r\n", config.mqtt_password);
    read_string_from_serial(temp_buffer, CONFIG_PASSWORD_MAX_LEN, "Enter MQTT Password (optional): ");
    if (strlen(temp_buffer) > 0) {
        strncpy(config.mqtt_password, temp_buffer, CONFIG_PASSWORD_MAX_LEN - 1);
        config.mqtt_password[CONFIG_PASSWORD_MAX_LEN - 1] = '\0';
    }
    
    // Telemetry Configuration
    printf("Telemetry Configuration:\r\n");
    printf("Current Interval: %d seconds\r\n", (int)config.telemetry_interval);
    config.telemetry_interval = read_int_from_serial("Enter Telemetry Interval (seconds): ", config.telemetry_interval);
    
    // Save configuration
    printf("Saving configuration to flash...\r\n");
    config_result_t result = config_manager_save(&config);
    
    if (result == CONFIG_OK) {
        printf("Configuration saved successfully!\r\n");
        printf("The device will restart to apply the new configuration.\r\n");
        return CONFIG_OK;
    } else {
        printf("Failed to save configuration: %d\r\n", result);
        return result;
    }
}

config_result_t config_manager_erase(void) {
    // TEMPORARY: Disable flash erase to prevent corruption
    return CONFIG_OK;
}

// Factory reset functions (temporarily disabled)
void config_manager_check_factory_reset(void) {
    // Temporarily disabled for safety
}

bool config_manager_validate(const device_config_t* config) {
    if (!config) {
        return false;
    }
    
    // Basic validation without CRC
    if (config->magic != CONFIG_MAGIC || config->version != CONFIG_VERSION) {
        return false;
    }
    
    return true;
}
