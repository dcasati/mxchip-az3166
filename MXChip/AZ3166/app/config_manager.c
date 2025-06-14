/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

// Configuration storage approach for AZ3166:
// - Delayed flash write approach - use RAM immediately, flash later (DISABLED for safety)
// - EEPROM emulation in flash for safer storage when enabled
// - STM32 HAL provides EEPROM emulation functions
// - RAM cache for immediate access during runtime

#define FLASH_OPERATIONS_DISABLED 1
#define FLASH_ERASE_DISABLED 1
#define USE_DELAYED_FLASH_WRITE 0  // Disabled - flash operations causing system instability
#define USE_EEPROM_EMULATION 1  // Enable EEPROM emulation storage

// Configuration constants
#define CONFIG_MAGIC    0xDEADBEEF
#define CONFIG_VERSION  1

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "config_manager.h"
#include "azure_config.h"
#include "stm32f4xx_hal.h"
#include "stm32f4xx_hal_rcc_ex.h"  // For backup SRAM clock enable
#include "stm32f4xx_hal_pwr.h"    // For backup SRAM power control
#include "stm32f4xx_hal_flash.h"  // For EEPROM emulation
#include "stm32f4xx_hal_flash_ex.h" // For flash extended operations

// Configuration file support
#define CONFIG_FILE_BUFFER_SIZE 2048
#define CONFIG_LINE_MAX_LEN 256

// External UART handle from console.c
extern UART_HandleTypeDef UartHandle;

// Console function declarations
extern int __io_getchar(void);

// Forward declarations for internal functions
static config_result_t flash_read_config(device_config_t* config);
static uint32_t calculate_crc32(const device_config_t* config);
static bool validate_config(const device_config_t* config);
bool config_manager_char_available(void);
static void parse_config_file(device_config_t* config, const char* content);
static void load_config_file_defaults(device_config_t* config);

// CRC32 calculation
static const uint32_t crc32_table[256] = {
    0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f,
    0xe963a535, 0x9e6495a3, 0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
    0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2,
    0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
    0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9,
    0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
    0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b, 0x35b5a8fa, 0x42b2986c,
    0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
    0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423,
    0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
    0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d, 0x76dc4190, 0x01db7106,
    0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
    0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d,
    0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
    0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
    0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
    0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7,
    0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
    0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa,
    0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
    0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81,
    0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
    0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84,
    0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
    0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
    0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
    0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e,
    0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
    0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55,
    0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
    0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28,
    0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
    0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f,
    0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
    0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
    0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
    0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69,
    0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
    0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc,
    0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
    0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693,
    0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
    0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
};

// RAM-based configuration storage (immediate use)
static device_config_t g_ram_config;
static bool g_ram_config_valid = false;
#if USE_DELAYED_FLASH_WRITE
static bool g_delayed_flash_pending = false;  // Flag for delayed flash write
#endif

// Calculate CRC32 for configuration validation
static uint32_t calculate_crc32(const device_config_t* config) {
    uint32_t crc = 0xFFFFFFFF;
    const uint8_t* data = (const uint8_t*)config;
    size_t len = sizeof(device_config_t) - sizeof(config->crc32);  // Exclude CRC field itself
    
    for (size_t i = 0; i < len; i++) {
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    
    return crc ^ 0xFFFFFFFF;
}

// Wait for user input with timeout (non-blocking)
bool config_manager_wait_for_user_input(uint32_t timeout_ms) {
    uint32_t start_time = HAL_GetTick();
    uint32_t current_time;
    
    do {
        if (config_manager_char_available()) {
            return true;
        }
        current_time = HAL_GetTick();
        HAL_Delay(10);  // Small delay to prevent busy waiting
    } while ((current_time - start_time) < timeout_ms);
    
    return false;
}

// Check if character is available (non-blocking)
bool config_manager_char_available(void) {
    uint8_t temp_char;
    HAL_StatusTypeDef status = HAL_UART_Receive(&UartHandle, &temp_char, 1, 0);
    
    if (status == HAL_OK) {
        // Character is available, we need to put it back somehow
        // For simplicity, we'll just return true and let the caller read it
        return true;
    }
    
    return false;
}


// Flash configuration definitions
#define CONFIG_FLASH_ADDRESS    0x0807F800  // Last 2KB of sector 11 (128KB sector)
#define CONFIG_FLASH_SECTOR     11          // Flash sector 11 (last sector)
#define CONFIG_FLASH_SIZE       2048        // 2KB should be enough for config

// Flash operations (currently disabled for safety)
static config_result_t flash_read_config(device_config_t* config) {
#if FLASH_OPERATIONS_DISABLED
    // Flash operations disabled - return not found so RAM cache or defaults will be used
    return CONFIG_ERROR_NOT_FOUND;
#else
    // Read directly from flash
    memcpy(config, (void*)CONFIG_FLASH_ADDRESS, sizeof(device_config_t));
    
    // Validate CRC
    uint32_t calculated_crc = calculate_crc32(config);
    if (config->crc != calculated_crc) {
        printf("Flash config CRC mismatch: stored=0x%08lX, calculated=0x%08lX\r\n", 
               config->crc, calculated_crc);
        return CONFIG_ERROR_INVALID;
    }
    
    // Validate configuration
    if (!validate_config(config)) {
        printf("Flash config validation failed\r\n");
        return CONFIG_ERROR_INVALID;
    }
    
    printf("Configuration loaded from flash\r\n");
    return CONFIG_OK;
#endif
}

// Load configuration from persistent storage (if available)
config_result_t config_manager_load_from_persistent_storage(device_config_t* config) {
    return flash_read_config(config);
}

// Factory reset - clear all stored configuration
config_result_t config_manager_factory_reset(void) {
    printf("Performing factory reset...\r\n");
    
    // Clear RAM cache
    memset(&g_ram_config, 0, sizeof(device_config_t));
    g_ram_config_valid = false;
#if USE_DELAYED_FLASH_WRITE
    g_delayed_flash_pending = false;
#endif
    
    // Load defaults
    config_manager_get_defaults(&g_ram_config);
    g_ram_config_valid = true;
    
    printf("Factory reset completed\r\n");
    
    return CONFIG_OK;
}

// Validate configuration
static bool validate_config(const device_config_t* config) {
    // Check magic number
    if (config->magic != CONFIG_MAGIC) {
        return false;
    }
    
    // Check version
    if (config->version > CONFIG_VERSION) {
        return false;
    }
    
    // Validate strings (ensure null termination)
    if (strnlen(config->wifi_ssid, sizeof(config->wifi_ssid)) >= sizeof(config->wifi_ssid)) {
        return false;
    }
    
    if (strnlen(config->wifi_password, sizeof(config->wifi_password)) >= sizeof(config->wifi_password)) {
        return false;
    }
    
    if (strnlen(config->mqtt_client_id, sizeof(config->mqtt_client_id)) >= sizeof(config->mqtt_client_id)) {
        return false;
    }
    
    if (strnlen(config->mqtt_hostname, sizeof(config->mqtt_hostname)) >= sizeof(config->mqtt_hostname)) {
        return false;
    }
    
    if (strnlen(config->mqtt_username, sizeof(config->mqtt_username)) >= sizeof(config->mqtt_username)) {
        return false;
    }
    
    if (strnlen(config->mqtt_password, sizeof(config->mqtt_password)) >= sizeof(config->mqtt_password)) {
        return false;
    }
    
    return true;
}

// Flash write/erase functions (disabled for safety but kept for future use)
__attribute__((unused)) static config_result_t flash_write_config(const device_config_t* config) {
    return CONFIG_ERROR_FLASH;
}

__attribute__((unused)) static config_result_t flash_erase_config(void) {
    return CONFIG_ERROR_FLASH;
}

// Configuration management functions
config_result_t config_manager_load(device_config_t* config) {
    if (!config) {
        return CONFIG_ERROR_INVALID;
    }
    
    // First try to use RAM cache
    if (g_ram_config_valid) {
        memcpy(config, &g_ram_config, sizeof(device_config_t));
        printf("Configuration loaded from RAM cache\r\n");
        return CONFIG_OK;
    }
    
    // Try to load from persistent storage
    config_result_t result = config_manager_load_from_persistent_storage(config);
    if (result == CONFIG_OK) {
        // Cache in RAM for next time
        memcpy(&g_ram_config, config, sizeof(device_config_t));
        g_ram_config_valid = true;
        return CONFIG_OK;
    }
    
    // Fall back to defaults
    printf("Loading default configuration...\r\n");
    config_manager_get_defaults(config);
    
    // Cache defaults in RAM
    memcpy(&g_ram_config, config, sizeof(device_config_t));
    g_ram_config_valid = true;
    
    return CONFIG_OK;
}

config_result_t config_manager_save(const device_config_t* config) {
    if (!config) {
        return CONFIG_ERROR_INVALID;
    }
    
    // Validate configuration before saving
    if (!validate_config(config)) {
        printf("Invalid configuration - cannot save\r\n");
        return CONFIG_ERROR_INVALID;
    }
    
    // Always save to RAM cache immediately
    memcpy(&g_ram_config, config, sizeof(device_config_t));
    g_ram_config_valid = true;
    
#if USE_DELAYED_FLASH_WRITE
    // Mark for delayed flash write
    g_delayed_flash_pending = true;
    printf("Configuration saved to RAM (flash write delayed)\r\n");
#else
    printf("Configuration saved to RAM (flash operations disabled)\r\n");
#endif
    
    return CONFIG_OK;
}

void config_manager_get_defaults(device_config_t* config) {
    if (!config) {
        return;
    }
    
    memset(config, 0, sizeof(device_config_t));
    
    // Set magic number and version
    config->magic = CONFIG_MAGIC;
    config->version = CONFIG_VERSION;
    
    // Load from embedded configuration file
    load_config_file_defaults(config);
    
    // Calculate and set CRC
    config->crc32 = calculate_crc32(config);
    
    printf("Default configuration loaded\r\n");
}

bool config_manager_validate(const device_config_t* config) {
    return validate_config(config);
}

config_result_t config_manager_prompt_and_store(device_config_t* config) {
    if (!config) {
        return CONFIG_ERROR_INVALID;
    }
    
    printf("\r\n=== Device Configuration ===\r\n");
    printf("Please enter the device configuration:\r\n\r\n");
    
    // Get WiFi SSID
    printf("WiFi SSID: ");
    fgets(config->wifi_ssid, sizeof(config->wifi_ssid), stdin);
    // Remove newline if present
    config->wifi_ssid[strcspn(config->wifi_ssid, "\r\n")] = 0;
    
    // Get WiFi Password
    printf("WiFi Password: ");
    fgets(config->wifi_password, sizeof(config->wifi_password), stdin);
    config->wifi_password[strcspn(config->wifi_password, "\r\n")] = 0;
    
    // Get MQTT Client ID
    printf("MQTT Client ID: ");
    fgets(config->mqtt_client_id, sizeof(config->mqtt_client_id), stdin);
    config->mqtt_client_id[strcspn(config->mqtt_client_id, "\r\n")] = 0;
    
    // Get MQTT Hostname
    printf("MQTT Hostname: ");
    fgets(config->mqtt_hostname, sizeof(config->mqtt_hostname), stdin);
    config->mqtt_hostname[strcspn(config->mqtt_hostname, "\r\n")] = 0;
    
    // Get MQTT Username
    printf("MQTT Username: ");
    fgets(config->mqtt_username, sizeof(config->mqtt_username), stdin);
    config->mqtt_username[strcspn(config->mqtt_username, "\r\n")] = 0;
    
    // Set magic and version
    config->magic = CONFIG_MAGIC;
    config->version = CONFIG_VERSION;
    
    // Calculate CRC
    config->crc32 = calculate_crc32(config);
    
    // Save configuration
    config_result_t result = config_manager_save(config);
    if (result == CONFIG_OK) {
        printf("\r\nConfiguration saved successfully!\r\n");
    } else {
        printf("\r\nFailed to save configuration!\r\n");
    }
    
    return result;
}

// Embedded device configuration
static const char embedded_device_conf[] =
    "# AZ3166 Device Configuration\n"
    "# This is the default configuration embedded in the firmware\n"
    "\n"
    "# WiFi Configuration\n"
    "WIFI_SSID=\n"
    "WIFI_PASSWORD=\n"
    "\n"
    "# MQTT Configuration\n"
    "MQTT_CLIENT_ID=mxchip-az3166\n"
    "MQTT_HOSTNAME=\n"
    "MQTT_USERNAME=\n"
    "MQTT_PASSWORD=\n";

// Trim whitespace from string
static void trim_string(char* str) {
    char* start = str;
    char* end;

    // Trim leading whitespace
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') {
        start++;
    }

    // Move trimmed string to beginning
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }

    // Trim trailing whitespace
    end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')) {
        *end = '\0';
        end--;
    }
}

// Parse configuration file content
static void parse_config_file(device_config_t* config, const char* content) {
    char line[CONFIG_LINE_MAX_LEN];
    const char* ptr = content;
    const char* line_end;
    
    while (*ptr != '\0') {
        // Find end of line
        line_end = strchr(ptr, '\n');
        if (!line_end) {
            line_end = ptr + strlen(ptr);
        }
        
        // Copy line
        size_t line_len = line_end - ptr;
        if (line_len >= sizeof(line)) {
            line_len = sizeof(line) - 1;
        }
        memcpy(line, ptr, line_len);
        line[line_len] = '\0';
        
        // Trim whitespace
        trim_string(line);
        
        // Skip empty lines and comments
        if (line[0] == '\0' || line[0] == '#') {
            ptr = (*line_end == '\n') ? line_end + 1 : line_end;
            continue;
        }
        
        // Parse key=value pairs
        char* equals = strchr(line, '=');
        if (equals) {
            *equals = '\0';
            char* key = line;
            char* value = equals + 1;
            
            trim_string(key);
            trim_string(value);
            
            // Set configuration values
            if (strcmp(key, "WIFI_SSID") == 0) {
                strncpy(config->wifi_ssid, value, sizeof(config->wifi_ssid) - 1);
            } else if (strcmp(key, "WIFI_PASSWORD") == 0) {
                strncpy(config->wifi_password, value, sizeof(config->wifi_password) - 1);
            } else if (strcmp(key, "MQTT_CLIENT_ID") == 0) {
                strncpy(config->mqtt_client_id, value, sizeof(config->mqtt_client_id) - 1);
            } else if (strcmp(key, "MQTT_HOSTNAME") == 0) {
                strncpy(config->mqtt_hostname, value, sizeof(config->mqtt_hostname) - 1);
            } else if (strcmp(key, "MQTT_USERNAME") == 0) {
                strncpy(config->mqtt_username, value, sizeof(config->mqtt_username) - 1);
            } else if (strcmp(key, "MQTT_PASSWORD") == 0) {
                strncpy(config->mqtt_password, value, sizeof(config->mqtt_password) - 1);
            }
        }
        
        // Move to next line
        ptr = (*line_end == '\n') ? line_end + 1 : line_end;
    }
}

// Check if reset button is held for factory reset
bool config_manager_check_reset_button(void) {
    // For now, always return false as we don't have hardware button setup
    return false;
}

// Perform delayed flash write if needed (call after WiFi connects)
config_result_t config_manager_delayed_flash_write(void) {
#if USE_DELAYED_FLASH_WRITE
    if (g_delayed_flash_pending) {
        // Try to write to flash now that WiFi is connected and system is stable
        config_result_t result = flash_write_config(&g_ram_config);
        if (result == CONFIG_OK) {
            g_delayed_flash_pending = false;
            printf("Delayed flash write completed successfully\r\n");
        } else {
            printf("Delayed flash write failed: %d\r\n", result);
        }
        return result;
    }
#endif
    return CONFIG_OK;
}

// Load defaults from embedded configuration file
static void load_config_file_defaults(device_config_t* config) {
    parse_config_file(config, embedded_device_conf);
}
