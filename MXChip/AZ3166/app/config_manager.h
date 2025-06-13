/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

#ifndef _CONFIG_MANAGER_H
#define _CONFIG_MANAGER_H

#include <stdint.h>
#include <stdbool.h>

// Maximum string lengths for configuration
#define CONFIG_SSID_MAX_LEN         64
#define CONFIG_PASSWORD_MAX_LEN     64
#define CONFIG_HOSTNAME_MAX_LEN     64
#define CONFIG_USERNAME_MAX_LEN     32
#define CONFIG_CLIENT_ID_MAX_LEN    64

// Configuration structure stored in flash (aligned for 32-bit flash programming)
typedef struct {
    uint32_t magic;                                     // Magic number to verify valid config
    uint32_t version;                                   // Config version for future compatibility
    
    // Wi-Fi Configuration
    char wifi_ssid[CONFIG_SSID_MAX_LEN];               // Wi-Fi SSID
    char wifi_password[CONFIG_PASSWORD_MAX_LEN];       // Wi-Fi password
    uint32_t wifi_mode;                                 // Wi-Fi security mode (WiFi_Mode enum)
    
    // MQTT Configuration
    char mqtt_hostname[CONFIG_HOSTNAME_MAX_LEN];       // MQTT broker hostname/IP
    uint16_t mqtt_port;                                 // MQTT broker port
    char mqtt_client_id[CONFIG_CLIENT_ID_MAX_LEN];     // MQTT client ID
    char mqtt_username[CONFIG_USERNAME_MAX_LEN];       // MQTT username (optional)
    char mqtt_password[CONFIG_PASSWORD_MAX_LEN];       // MQTT password (optional)
    
    // Telemetry Configuration
    uint32_t telemetry_interval;                       // Telemetry interval in seconds
    
    uint32_t crc32;                                     // CRC32 checksum for data integrity
} device_config_t;

// Configuration manager API
typedef enum {
    CONFIG_OK = 0,
    CONFIG_ERROR_FLASH,
    CONFIG_ERROR_INVALID,
    CONFIG_ERROR_CORRUPTED,
    CONFIG_ERROR_NOT_FOUND,
    CONFIG_ERROR_STORAGE
} config_result_t;

/**
 * @brief Initialize the configuration manager
 * @return CONFIG_OK on success, error code otherwise
 */
config_result_t config_manager_init(void);

/**
 * @brief Load configuration from flash memory
 * @param config Pointer to configuration structure to populate
 * @return CONFIG_OK on success, error code otherwise
 */
config_result_t config_manager_load(device_config_t* config);

/**
 * @brief Save configuration to flash memory
 * @param config Pointer to configuration structure to save
 * @return CONFIG_OK on success, error code otherwise
 */
config_result_t config_manager_save(const device_config_t* config);

/**
 * @brief Check if valid configuration exists in flash
 * @return true if valid config exists, false otherwise
 */
bool config_manager_has_valid_config(void);

/**
 * @brief Get default configuration values
 * @param config Pointer to configuration structure to populate with defaults
 */
void config_manager_get_defaults(device_config_t* config);

/**
 * @brief Prompt user for configuration via serial and save to flash
 * @return CONFIG_OK on success, error code otherwise
 */
config_result_t config_manager_prompt_and_save(void);

/**
 * @brief Prompt user for configuration via serial and store in provided buffer
 * @param config Pointer to configuration structure to store the result
 * @return CONFIG_OK on success, error code otherwise
 */
config_result_t config_manager_prompt_and_store(device_config_t* config);

/**
 * @brief Wait for user input with timeout
 * @param timeout_ms Timeout in milliseconds
 * @return true if user pressed a key, false if timeout
 */
bool config_manager_wait_for_user_input(uint32_t timeout_ms);

/**
 * @brief Check if a character is available for reading (non-blocking)
 * @return true if character available, false otherwise
 */
bool config_manager_char_available(void);

/**
 * @brief Erase configuration from flash (factory reset)
 * @return CONFIG_OK on success, error code otherwise
 */
config_result_t config_manager_erase(void);

/**
 * @brief Validate configuration structure
 * @param config Pointer to configuration structure to validate
 * @return true if configuration is valid, false otherwise
 */
bool config_manager_validate(const device_config_t* config);

/**
 * @brief Check if reset button is held for factory reset
 * @return true if button held for 5 seconds, false otherwise
 */
bool config_manager_check_reset_button(void);

/**
 * @brief Perform factory reset - erase config and prompt for new settings
 * @return CONFIG_OK on success, error code otherwise
 */
config_result_t config_manager_factory_reset(void);

/**
 * @brief Perform delayed flash write if needed (call after WiFi connects)
 * @return CONFIG_OK on success, error code otherwise
 */
config_result_t config_manager_delayed_flash_write(void);

/**
 * @brief Safely attempt to load configuration from persistent storage after system is stable
 * @param config Pointer to configuration structure to populate
 * @return CONFIG_OK on success, error code otherwise
 */
config_result_t config_manager_load_from_persistent_storage(device_config_t* config);

#endif // _CONFIG_MANAGER_H
