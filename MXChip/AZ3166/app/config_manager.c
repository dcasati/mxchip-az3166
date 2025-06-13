/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

// Delayed flash write approach - use RAM immediately, flash later (DISABLED for safety)
#define FLASH_OPERATIONS_DISABLED 1
#define FLASH_ERASE_DISABLED 1
#define USE_DELAYED_FLASH_WRITE 0  // Disabled - flash operations causing system instability

#include "config_manager.h"
#include "azure_config.h"
#include "stm32f4xx_hal.h"
#include "stm32f4xx_hal_rcc_ex.h"  // For backup SRAM clock enable
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// External UART handle from console.c
extern UART_HandleTypeDef UartHandle;

// Console function declarations
extern int __io_getchar(void);

// RAM-based configuration storage (immediate use)
static device_config_t g_ram_config;
static bool g_ram_config_valid = false;
#if USE_DELAYED_FLASH_WRITE
static bool g_config_needs_flash_save = false;  // Flag for delayed flash write
#endif

// Wait for user input with timeout (non-blocking)
bool config_manager_wait_for_user_input(uint32_t timeout_ms) {
    uint32_t start_time = HAL_GetTick();
    uint32_t current_time;
    
    printf("Countdown: ");
    fflush(stdout);
    
    while (1) {
        current_time = HAL_GetTick();
        uint32_t elapsed = current_time - start_time;
        
        if (elapsed >= timeout_ms) {
            printf("\r\nTimeout reached\r\n");
            return false;  // Timeout
        }
          // Show countdown every second
        uint32_t seconds_left = (timeout_ms - elapsed) / 1000 + 1;
        static uint32_t last_second = 0;
        if (seconds_left != last_second) {
            printf("\rCountdown: ");
            if (seconds_left <= 10) {
                if (seconds_left == 10) printf("10");
                else if (seconds_left == 9) printf("9");
                else if (seconds_left == 8) printf("8");
                else if (seconds_left == 7) printf("7");
                else if (seconds_left == 6) printf("6");
                else if (seconds_left == 5) printf("5");
                else if (seconds_left == 4) printf("4");
                else if (seconds_left == 3) printf("3");
                else if (seconds_left == 2) printf("2");
                else if (seconds_left == 1) printf("1");
                else printf("0");
            } else {
                printf("many");
            }
            printf(" seconds ");
            fflush(stdout);
            last_second = seconds_left;
        }
        
        // Check if character is available (non-blocking)
        if (config_manager_char_available()) {
            printf("\r\nKey pressed!\r\n");
            // Consume the character
            __io_getchar();
            return true;  // User pressed a key
        }
        
        HAL_Delay(100);  // Small delay to prevent busy waiting
    }
}

// Check if a character is available (non-blocking)
bool config_manager_char_available(void) {
    // Use HAL_UART_Receive with timeout of 0 to check if data is available
    uint8_t temp_char;
    HAL_StatusTypeDef status = HAL_UART_Receive(&UartHandle, &temp_char, 1, 0);
    
    if (status == HAL_OK) {
        // Character was available, we need to "put it back" somehow
        // Since we can't put it back, we'll set a flag or handle it differently
        // For now, return true and the character will be consumed by the next __io_getchar call
        return true;
    }
    
    return false;
}

// Configuration constants
#define CONFIG_MAGIC    0xDEADBEEF
#define CONFIG_VERSION  1

// STM32F412Rx flash storage configuration (MXChip AZ3166)
// Use the last sector of flash to avoid conflicts with firmware
#define CONFIG_FLASH_ADDRESS    0x0800BC00  // Sector 4 start address
#define CONFIG_FLASH_SECTOR     4           // Flash sector 4
#define CONFIG_FLASH_SIZE       16384       // 16KB sector

// STM32F412Rx backup SRAM storage configuration (MXChip AZ3166)  
// Use backup SRAM instead of flash to avoid interference with WiFi
// Backup SRAM: 0x40024000 - 0x40024FFF (4KB) - much safer than flash
#define CONFIG_BACKUP_SRAM_ADDRESS    0x40024000  // Backup SRAM base
#define CONFIG_BACKUP_SRAM_SIZE       4096

#if !FLASH_OPERATIONS_DISABLED || USE_DELAYED_FLASH_WRITE
// CRC32 calculation for data integrity
static uint32_t calculate_crc32(const uint8_t* data, size_t length) {
    uint32_t crc = 0xFFFFFFFF;
    
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc = crc >> 1;
            }
        }
    }
    return ~crc;
}
#endif

#if !FLASH_OPERATIONS_DISABLED || USE_DELAYED_FLASH_WRITE
// Validate configuration before saving
static bool validate_config(const device_config_t* config) {
    if (!config) {
        printf("Config validation: NULL pointer\r\n");
        return false;
    }
    
    // Check WiFi SSID (required)
    if (strlen(config->wifi_ssid) == 0) {
        printf("Config validation: WiFi SSID is empty\r\n");
        return false;
    }
    
    // Check MQTT hostname (required)
    if (strlen(config->mqtt_hostname) == 0) {
        printf("Config validation: MQTT hostname is empty\r\n");
        return false;
    }
    
    // Check MQTT client ID (required)
    if (strlen(config->mqtt_client_id) == 0) {
        printf("Config validation: MQTT client ID is empty\r\n");
        return false;
    }
    
    // Check telemetry interval (should be reasonable)
    if (config->telemetry_interval < 1 || config->telemetry_interval > 3600) {
        printf("Config validation: Invalid telemetry interval\r\n");
        return false;
    }
    
    printf("Config validation: All required fields are valid\r\n");
    return true;
}
#endif

config_result_t config_manager_init(void) {
    return CONFIG_OK;
}

config_result_t config_manager_load(device_config_t* config) {
    if (!config) {
        return CONFIG_ERROR_INVALID;
    }
    
#if USE_DELAYED_FLASH_WRITE
    // First check if we have valid RAM config (from current session)
    if (g_ram_config_valid) {
        printf("Loading configuration from RAM storage (current session)\r\n");
        memcpy(config, &g_ram_config, sizeof(device_config_t));
        return CONFIG_OK;
    }
    
    // Try to load from flash if no RAM config available
    printf("Attempting to load configuration from flash...\r\n");
    
    // Read configuration from flash with safety checks
    volatile device_config_t* flash_config = (volatile device_config_t*)CONFIG_FLASH_ADDRESS;
    
    // Check magic number first (quick safety check)
    __disable_irq();  // Disable interrupts during flash read
    uint32_t magic = flash_config->magic;
    uint32_t version = flash_config->version;
    __enable_irq();   // Re-enable interrupts
    
    if (magic != CONFIG_MAGIC) {
        printf("No valid configuration found in flash (magic: 0x%08lX)\r\n", magic);
        return CONFIG_ERROR_NOT_FOUND;
    }
    
    if (version != CONFIG_VERSION) {
        printf("Configuration version mismatch in flash (found: %lu, expected: %d)\r\n", version, CONFIG_VERSION);
        return CONFIG_ERROR_NOT_FOUND;
    }
    
    // Copy configuration from flash to temporary buffer
    device_config_t temp_config;
    __disable_irq();
    memcpy(&temp_config, (void*)flash_config, sizeof(device_config_t));
    __enable_irq();
    
    // Verify CRC32
    uint32_t calculated_crc = calculate_crc32((uint8_t*)&temp_config, sizeof(device_config_t) - sizeof(uint32_t));
    if (temp_config.crc32 != calculated_crc) {
        printf("CRC mismatch in flash config (stored: 0x%08lX, calculated: 0x%08lX)\r\n", 
               temp_config.crc32, calculated_crc);
        return CONFIG_ERROR_NOT_FOUND;
    }
    
    // Configuration is valid - copy to output and update RAM cache
    memcpy(config, &temp_config, sizeof(device_config_t));
    memcpy(&g_ram_config, &temp_config, sizeof(device_config_t));
    g_ram_config_valid = true;
    
    printf("Configuration loaded successfully from flash\r\n");
    return CONFIG_OK;
    
#elif FLASH_OPERATIONS_DISABLED
    printf("Flash operations disabled - using RAM-only storage\r\n");
    if (g_ram_config_valid) {
        printf("Loading configuration from RAM storage\r\n");
        memcpy(config, &g_ram_config, sizeof(device_config_t));
        return CONFIG_OK;
    } else {
        printf("No valid configuration found in RAM - using defaults\r\n");
        return CONFIG_ERROR_NOT_FOUND;
    }
#else
    printf("Loading configuration from flash...\r\n");
    
    // Read configuration from flash
    device_config_t* flash_config = (device_config_t*)CONFIG_FLASH_ADDRESS;
    
    // Check magic number
    if (flash_config->magic != CONFIG_MAGIC) {
        printf("Invalid magic number found in flash\r\n");
        return CONFIG_ERROR_NOT_FOUND;
    }
    
    // Check version
    if (flash_config->version != CONFIG_VERSION) {
        printf("Invalid version: %d (expected %d)\r\n", 
               (int)flash_config->version, (int)CONFIG_VERSION);
        return CONFIG_ERROR_NOT_FOUND;
    }
    
    // Verify CRC32
    uint32_t calculated_crc = calculate_crc32((uint8_t*)flash_config, sizeof(device_config_t) - sizeof(uint32_t));
    if (flash_config->crc32 != calculated_crc) {
        printf("CRC mismatch - config corrupted\r\n");
        return CONFIG_ERROR_NOT_FOUND;
    }
    
    // Copy valid configuration
    memcpy(config, flash_config, sizeof(device_config_t));
    printf("Configuration loaded successfully from flash\r\n");
    printf("Config validation passed\r\n");
    
    return CONFIG_OK;
#endif
}

config_result_t config_manager_save(const device_config_t* config) {
    if (!config) {
        return CONFIG_ERROR_INVALID;
    }
    
#if USE_DELAYED_FLASH_WRITE
    printf("Saving configuration to RAM for immediate use...\r\n");
    memcpy(&g_ram_config, config, sizeof(device_config_t));
    g_ram_config_valid = true;
    g_config_needs_flash_save = true;  // Mark for delayed flash write
    printf("Configuration saved to RAM - will write to flash after WiFi connects\r\n");
    return CONFIG_OK;
#elif FLASH_OPERATIONS_DISABLED
    printf("Flash operations disabled - saving configuration to RAM storage\r\n");
    memcpy(&g_ram_config, config, sizeof(device_config_t));
    g_ram_config_valid = true;
    printf("Configuration saved successfully to RAM!\r\n");
    return CONFIG_OK;  // Success
#else
    printf("Saving configuration to flash at 0x0800BC00...\r\n");
    
    // Create a copy with proper magic, version, and CRC
    device_config_t config_copy = *config;
    config_copy.magic = CONFIG_MAGIC;
    config_copy.version = CONFIG_VERSION;
    
    // Calculate CRC on everything except the CRC field itself
    config_copy.crc32 = calculate_crc32((uint8_t*)&config_copy, sizeof(device_config_t) - sizeof(uint32_t));
    
    printf("Config prepared for flash storage\r\n");
    
    HAL_StatusTypeDef status;
    
    // Unlock flash for writing
    status = HAL_FLASH_Unlock();
    if (status != HAL_OK) {
        printf("Flash unlock failed\r\n");
        return CONFIG_ERROR_FLASH;
    }
    
#if !FLASH_ERASE_DISABLED
    // Erase the configuration sector (STM32F4 sector erase)
    FLASH_EraseInitTypeDef erase_init;
    uint32_t sector_error = 0;
    
    erase_init.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase_init.VoltageRange = FLASH_VOLTAGE_RANGE_3;  // 2.7V to 3.6V
    erase_init.Sector = CONFIG_FLASH_SECTOR;
    erase_init.NbSectors = 1;
    printf("Erasing flash sector 4...\r\n");
    status = HAL_FLASHEx_Erase(&erase_init, &sector_error);
    if (status != HAL_OK) {
        printf("Flash erase failed with status: %d, sector_error: %lu\r\n", status, sector_error);
        HAL_FLASH_Lock();
        return CONFIG_ERROR_FLASH;
    }
    
    printf("Flash sector erased successfully\r\n");
    HAL_Delay(10);  // Small delay after erase
#else
    printf("Skipping flash erase - writing to existing flash\r\n");
#endif
    
    // Program the configuration data (STM32F4 uses word programming)
    uint8_t* src_data = (uint8_t*)&config_copy;
    uint32_t flash_address = CONFIG_FLASH_ADDRESS;
    uint32_t bytes_remaining = sizeof(device_config_t);
    
    // Use simple string output to avoid printf issues
    printf("Writing config data to flash...\r\n");
    
    while (bytes_remaining > 0) {
        uint32_t data_word = 0xFFFFFFFF; // Default to erased state
        
        // Copy up to 4 bytes to word
        for (int i = 0; i < 4 && bytes_remaining > 0; i++) {
            if (i == 0) data_word = 0x00000000; // Clear first
            data_word |= ((uint32_t)(*src_data)) << (i * 8);
            src_data++;
            bytes_remaining--;
        }
        
        // Program the word
        status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, flash_address, data_word);
        if (status != HAL_OK) {
            printf("Flash program failed at address: 0x%08lX\r\n", flash_address);
            HAL_FLASH_Lock();
            return CONFIG_ERROR_FLASH;
        }
        
        flash_address += 4;
    }
    
    // Lock flash
    HAL_FLASH_Lock();
    
    printf("Configuration saved successfully to flash!\r\n");
    
    return CONFIG_OK;
#endif
}

// Delayed flash write - called after WiFi connects
config_result_t config_manager_delayed_flash_write(void) {
#if USE_DELAYED_FLASH_WRITE
    if (!g_config_needs_flash_save) {
        printf("No config changes to save to flash\r\n");
        return CONFIG_OK;
    }
    
    if (!g_ram_config_valid) {
        printf("No valid config in RAM to save\r\n");
        return CONFIG_ERROR_INVALID;
    }
    
    printf("Writing config to flash storage after WiFi connection...\r\n");
    
    // Validate before saving
    if (!validate_config(&g_ram_config)) {
        printf("Config validation failed - cannot save to flash\r\n");
        return CONFIG_ERROR_INVALID;
    }
    
    // Create a copy with proper magic, version, and CRC for flash storage
    device_config_t config_copy = g_ram_config;
    config_copy.magic = CONFIG_MAGIC;
    config_copy.version = CONFIG_VERSION;
    config_copy.crc32 = calculate_crc32((uint8_t*)&config_copy, sizeof(device_config_t) - sizeof(uint32_t));
    
    printf("Config prepared for flash storage\r\n");
    
    HAL_StatusTypeDef status;
    
    // Unlock flash for writing
    status = HAL_FLASH_Unlock();
    if (status != HAL_OK) {
        printf("Flash unlock failed\r\n");
        return CONFIG_ERROR_FLASH;
    }
    
    // Program the configuration data (STM32F4 uses word programming)
    // Skip erase since FLASH_ERASE_DISABLED is 1 for safety
    uint8_t* src_data = (uint8_t*)&config_copy;
    uint32_t flash_address = CONFIG_FLASH_ADDRESS;
    uint32_t bytes_remaining = sizeof(device_config_t);
    
    printf("Writing config data to flash at 0x%08lX...\r\n", flash_address);
    
    while (bytes_remaining > 0) {
        uint32_t data_word = 0xFFFFFFFF; // Default to erased state
        
        // Copy up to 4 bytes to word
        for (int i = 0; i < 4 && bytes_remaining > 0; i++) {
            if (i == 0) data_word = 0x00000000; // Clear first
            data_word |= ((uint32_t)(*src_data)) << (i * 8);
            src_data++;
            bytes_remaining--;
        }
        
        // Program the word
        status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, flash_address, data_word);
        if (status != HAL_OK) {
            printf("Flash program failed at address: 0x%08lX\r\n", flash_address);
            HAL_FLASH_Lock();
            return CONFIG_ERROR_FLASH;
        }
        
        flash_address += 4;
    }
    
    // Lock flash and add delay for stability
    HAL_FLASH_Lock();
    HAL_Delay(10);  // Small delay after flash programming
    
    printf("Config saved successfully to flash!\r\n");
    g_config_needs_flash_save = false;
    return CONFIG_OK;
#else
    printf("Delayed flash write not enabled\r\n");
    return CONFIG_OK;
#endif
}

// Placeholder function - functionality moved to main config_manager_load
config_result_t config_manager_load_from_persistent_storage(device_config_t* config) {
    printf("This function is deprecated - use config_manager_load instead\r\n");
    return config_manager_load(config);
}

config_result_t config_manager_erase(void) {
    // Safe implementation: do nothing for now
    printf("Config erase disabled for safety - no flash operations performed\r\n");
    return CONFIG_OK;
}

// Factory reset functions (temporarily disabled)
bool config_manager_check_reset_button(void) {
    // Safe implementation: always return false (no reset triggered)
    // This prevents any flash operations during recovery
    printf("Factory reset check disabled for safety\r\n");
    return false;
}

config_result_t config_manager_factory_reset(void) {
    // Safe implementation: do nothing
    printf("Factory reset disabled for safety - no flash operations performed\r\n");
    return CONFIG_OK;
}

bool config_manager_validate(const device_config_t* config) {
    if (!config) {
        return false;
    }
      // Basic validation without CRC
    if (config->magic != CONFIG_MAGIC || config->version != CONFIG_VERSION) {        return false;
    }
    
    return true;
}

#if FLASH_OPERATIONS_DISABLED
#define HAL_FLASH_Unlock()      (HAL_OK)
#define HAL_FLASH_Lock()        (HAL_OK)
#define HAL_FLASH_Program(...)  (HAL_OK)
#define HAL_FLASHEx_Erase(...) (HAL_OK)
#endif

void config_manager_get_defaults(device_config_t* config) {
    if (!config) {
        return;
    }
    
    // Clear the structure
    memset(config, 0, sizeof(device_config_t));
    
    // Set magic and version
    config->magic = CONFIG_MAGIC;
    config->version = CONFIG_VERSION;
      // WiFi Configuration defaults
    strncpy(config->wifi_ssid, WIFI_SSID_DEFAULT, CONFIG_SSID_MAX_LEN - 1);
    strncpy(config->wifi_password, WIFI_PASSWORD_DEFAULT, CONFIG_PASSWORD_MAX_LEN - 1);
    config->wifi_mode = WIFI_MODE_DEFAULT;
    
    // MQTT Configuration defaults
    strncpy(config->mqtt_hostname, MQTT_BROKER_HOSTNAME_DEFAULT, CONFIG_HOSTNAME_MAX_LEN - 1);
    config->mqtt_port = MQTT_BROKER_PORT_DEFAULT;
    strncpy(config->mqtt_client_id, MQTT_CLIENT_ID_DEFAULT, CONFIG_CLIENT_ID_MAX_LEN - 1);
    strncpy(config->mqtt_username, MQTT_USERNAME_DEFAULT, CONFIG_USERNAME_MAX_LEN - 1);
    strncpy(config->mqtt_password, MQTT_PASSWORD_DEFAULT, CONFIG_PASSWORD_MAX_LEN - 1);
    
    // Telemetry Configuration defaults
    config->telemetry_interval = DEFAULT_TELEMETRY_INTERVAL;
    
    printf("Default configuration applied\r\n");
}

// Read a string from serial console
static void read_string_from_serial(char* buffer, size_t max_len, const char* prompt) {
    printf("%s", prompt);
    fflush(stdout);
    
    size_t index = 0;
    buffer[0] = '\0';  // Initialize as empty string
    
    while (index < max_len - 1) {
        int c = __io_getchar();
        
        if (c == '\r' || c == '\n') {
            buffer[index] = '\0';
            printf("\r\n");
            break;
        }
        else if (c == 8 || c == 127) { // Backspace or DEL
            if (index > 0) {
                index--;
                printf("\b \b"); // Erase character on screen
            }
        }
        else if (c >= 32 && c <= 126) { // Printable characters
            buffer[index] = (char)c;
            index++;
            printf("%c", c); // Echo character
        }
    }
    
    buffer[max_len - 1] = '\0'; // Ensure null termination
}

// Read an integer from serial console
static uint32_t read_int_from_serial(const char* prompt, uint32_t default_value) {
    char buffer[16];
    printf("%s(default: %u): ", prompt, (unsigned int)default_value);
    
    read_string_from_serial(buffer, sizeof(buffer), "");
    
    if (strlen(buffer) == 0) {
        return default_value;
    }
    
    return (uint32_t)atoi(buffer);
}

config_result_t config_manager_prompt_and_store(device_config_t* config) {
    if (!config) {
        return CONFIG_ERROR_INVALID;
    }
    
    char temp_buffer[128];
    
    printf("\r\n=== MXChip AZ3166 Configuration Setup ===\r\n");
    printf("Press Enter to keep current/default values\r\n\r\n");
    
    // Get current defaults
    config_manager_get_defaults(config);
    
    // WiFi Configuration
    printf("WiFi Configuration:\r\n");
    printf("Current SSID: %s\r\n", config->wifi_ssid);
    read_string_from_serial(temp_buffer, CONFIG_SSID_MAX_LEN, "Enter WiFi SSID: ");
    if (strlen(temp_buffer) > 0) {
        strncpy(config->wifi_ssid, temp_buffer, CONFIG_SSID_MAX_LEN - 1);
        config->wifi_ssid[CONFIG_SSID_MAX_LEN - 1] = '\0';
    }
    
    printf("Current Password: %s\r\n", config->wifi_password);
    read_string_from_serial(temp_buffer, CONFIG_PASSWORD_MAX_LEN, "Enter WiFi Password: ");
    if (strlen(temp_buffer) > 0) {
        strncpy(config->wifi_password, temp_buffer, CONFIG_PASSWORD_MAX_LEN - 1);
        config->wifi_password[CONFIG_PASSWORD_MAX_LEN - 1] = '\0';
    }
    
    printf("Current Mode: ");
    if (config->wifi_mode == 0) printf("None");
    else if (config->wifi_mode == 1) printf("WEP");
    else if (config->wifi_mode == 2) printf("WPA_PSK_TKIP");
    else if (config->wifi_mode == 3) printf("WPA2_PSK_AES");
    else printf("Unknown");
    printf("\r\n");
    
    printf("WiFi Security Modes:\r\n");
    printf("  0 = None (Open)\r\n");
    printf("  1 = WEP\r\n");
    printf("  2 = WPA_PSK_TKIP\r\n");
    printf("  3 = WPA2_PSK_AES (recommended)\r\n");
    config->wifi_mode = read_int_from_serial("Enter WiFi Mode: ", config->wifi_mode);
    
    // MQTT Configuration
    printf("\r\nMQTT Configuration:\r\n");
    printf("Current Hostname: %s\r\n", config->mqtt_hostname);
    read_string_from_serial(temp_buffer, CONFIG_HOSTNAME_MAX_LEN, "Enter MQTT Hostname: ");
    if (strlen(temp_buffer) > 0) {
        strncpy(config->mqtt_hostname, temp_buffer, CONFIG_HOSTNAME_MAX_LEN - 1);
        config->mqtt_hostname[CONFIG_HOSTNAME_MAX_LEN - 1] = '\0';
    }
    
    printf("Current Port: ");
    if (config->mqtt_port == 1883) printf("1883");
    else if (config->mqtt_port == 8883) printf("8883");
    else printf("custom");
    printf("\r\n");
    config->mqtt_port = read_int_from_serial("Enter MQTT Port: ", config->mqtt_port);
    
    printf("Current Client ID: %s\r\n", config->mqtt_client_id);
    read_string_from_serial(temp_buffer, CONFIG_CLIENT_ID_MAX_LEN, "Enter MQTT Client ID: ");
    if (strlen(temp_buffer) > 0) {
        strncpy(config->mqtt_client_id, temp_buffer, CONFIG_CLIENT_ID_MAX_LEN - 1);
        config->mqtt_client_id[CONFIG_CLIENT_ID_MAX_LEN - 1] = '\0';
    }
    
    printf("Current Username: %s\r\n", config->mqtt_username);
    read_string_from_serial(temp_buffer, CONFIG_USERNAME_MAX_LEN, "Enter MQTT Username (optional): ");
    if (strlen(temp_buffer) > 0) {
        strncpy(config->mqtt_username, temp_buffer, CONFIG_USERNAME_MAX_LEN - 1);
        config->mqtt_username[CONFIG_USERNAME_MAX_LEN - 1] = '\0';
    }
    
    printf("Current Password: %s\r\n", config->mqtt_password);
    read_string_from_serial(temp_buffer, CONFIG_PASSWORD_MAX_LEN, "Enter MQTT Password (optional): ");
    if (strlen(temp_buffer) > 0) {
        strncpy(config->mqtt_password, temp_buffer, CONFIG_PASSWORD_MAX_LEN - 1);
        config->mqtt_password[CONFIG_PASSWORD_MAX_LEN - 1] = '\0';
    }
    
    // Telemetry Configuration
    printf("\r\nTelemetry Configuration:\r\n");
    printf("Current Interval: ");
    if (config->telemetry_interval == 30) printf("30");
    else if (config->telemetry_interval == 60) printf("60");
    else if (config->telemetry_interval == 10) printf("10");
    else printf("custom");
    printf(" seconds\r\n");
    config->telemetry_interval = read_int_from_serial("Enter Telemetry Interval (seconds): ", config->telemetry_interval);
    
    // Save configuration
    printf("\r\nSaving configuration...\r\n");
    printf("Final config values:\r\n");
    printf("  WiFi SSID: %s\r\n", config->wifi_ssid);
    printf("  WiFi Password: %s\r\n", config->wifi_password);
    printf("  WiFi Mode: ");
    if (config->wifi_mode == 0) printf("None");
    else if (config->wifi_mode == 1) printf("WEP");
    else if (config->wifi_mode == 2) printf("WPA_PSK_TKIP");
    else if (config->wifi_mode == 3) printf("WPA2_PSK_AES");
    else printf("Unknown");
    printf("\r\n");
    printf("  MQTT Hostname: %s\r\n", config->mqtt_hostname);
    printf("  MQTT Port: ");
    if (config->mqtt_port == 1883) printf("1883");
    else if (config->mqtt_port == 8883) printf("8883");
    else printf("custom");
    printf("\r\n");
    printf("  MQTT Client ID: %s\r\n", config->mqtt_client_id);
    printf("  MQTT Username: %s\r\n", config->mqtt_username);
    printf("  MQTT Password: %s\r\n", config->mqtt_password);
    printf("  Telemetry Interval: ");
    if (config->telemetry_interval == 30) printf("30");
    else if (config->telemetry_interval == 60) printf("60");
    else if (config->telemetry_interval == 10) printf("10");
    else printf("custom");
    printf(" seconds\r\n");
    
    config_result_t result = config_manager_save(config);
    
    if (result == CONFIG_OK) {
        printf("Configuration saved successfully!\r\n");
        return CONFIG_OK;
    } else {
        printf("Failed to save configuration\r\n");
        return result;
    }
}
