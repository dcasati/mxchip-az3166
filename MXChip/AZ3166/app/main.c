/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

#include <stdio.h>

#include "tx_api.h"

#include "board_init.h"
#include "cmsis_utils.h"
#include "screen.h"
#include "ssd1306.h"
#include "ssd1306_fonts.h"
#include "sensor.h"
#include "sntp_client.h"
#include "wwd_networking.h"

#include "legacy/mqtt.h"
#include "nx_client.h"

#include "azure_config.h"
#include "config_manager.h"

// Global configuration instance
device_config_t g_device_config;

// Display cycling state
static int display_mode = 0;  // 0=default, 1=MQTT_CLIENT_ID, 2=MQTT_BROKER, 3=MQTT_PORT, 4=WIFI_SSID
static int telemetry_mode = 0;  // 0=pressure, 1=humidity, 2=accel, 3=gyro, 4=magnetometer

// Forward declaration
static void init_device_configuration(void);
static void display_device_info(void);
static void cycle_display_info(void);
static void cycle_telemetry_info(void);

static void init_device_configuration(void)
{
    printf("Initializing device configuration...\r\n");
    
    // Check for factory reset button hold (5 seconds)
    if (config_manager_check_reset_button()) {
        // Button was held, perform factory reset
        config_manager_factory_reset();
        // Function will reset the device, so we won't reach here
        return;
    }
    
    // Try to load configuration from flash
    if (config_manager_load(&g_device_config) == CONFIG_OK)
    {
        printf("Configuration loaded from flash:\r\n");
        printf("  WiFi SSID: %s\r\n", g_device_config.wifi_ssid);
        printf("  MQTT Broker: %s:%u\r\n", g_device_config.mqtt_hostname, (unsigned int)g_device_config.mqtt_port);
        printf("  MQTT Client ID: %s\r\n", g_device_config.mqtt_client_id);
        
        // Ask if user wants to reconfigure
        printf("\r\nPress any key within 10 seconds to enter setup mode...\r\n");
        if (config_manager_wait_for_user_input(10000)) {
            printf("Entering setup mode...\r\n");
            if (config_manager_prompt_and_store(&g_device_config) == CONFIG_OK) {
                printf("Configuration updated and will be used for this session\r\n");
            }
        } else {
            printf("Timeout - using existing configuration\r\n");
        }
    }
    else
    {
        printf("No valid configuration found in flash\r\n");
        
        // Ask if user wants to configure or use defaults
        printf("Press any key within 10 seconds to enter setup mode, or defaults will be used...\r\n");
        if (config_manager_wait_for_user_input(10000)) {
            printf("Entering setup mode...\r\n");
            if (config_manager_prompt_and_store(&g_device_config) == CONFIG_OK) {
                printf("Configuration received and will be used for this session\r\n");
            } else {
                printf("Setup failed, using defaults\r\n");
                config_manager_get_defaults(&g_device_config);
            }
        } else {
            printf("Timeout - using default configuration\r\n");
            config_manager_get_defaults(&g_device_config);
        }
    }
    
    printf("Active configuration:\r\n");
    printf("  WiFi SSID: ");
    printf(g_device_config.wifi_ssid);
    printf("\r\n");
    
    printf("  WiFi Mode: ");
    if (g_device_config.wifi_mode == 0) printf("None");
    else if (g_device_config.wifi_mode == 1) printf("WEP");
    else if (g_device_config.wifi_mode == 2) printf("WPA_PSK_TKIP");
    else if (g_device_config.wifi_mode == 3) printf("WPA2_PSK_AES");
    else printf("Unknown");
    printf("\r\n");
    
    printf("  MQTT Broker: ");
    printf(g_device_config.mqtt_hostname);
    printf(":");
    if (g_device_config.mqtt_port == 1883) printf("1883");
    else if (g_device_config.mqtt_port == 8883) printf("8883");
    else printf("custom-port");
    printf("\r\n");
    
    printf("  MQTT Client ID: ");
    printf(g_device_config.mqtt_client_id);
    printf("\r\n");
    
    printf("  Telemetry Interval: ");
    if (g_device_config.telemetry_interval == 30) printf("30");
    else if (g_device_config.telemetry_interval == 60) printf("60");
    else if (g_device_config.telemetry_interval == 10) printf("10");
    else printf("custom");
    printf(" seconds\r\n");
    printf("\r\n");

    // Display device info on OLED screen
    display_device_info();
}

#define MQTT_THREAD_STACK_SIZE 4096
#define MQTT_THREAD_PRIORITY   4

TX_THREAD mqtt_thread;
ULONG mqtt_thread_stack[MQTT_THREAD_STACK_SIZE / sizeof(ULONG)];

static void mqtt_thread_entry(ULONG parameter)
{
    UINT status;

    printf("Starting MQTT client thread\r\n\r\n");
    
    // Initialize configuration manager and load/prompt for configuration
    init_device_configuration();
    
    // Wait for sensors to stabilize after board initialization
    printf("Waiting for sensors to initialize...\r\n");
    tx_thread_sleep(3 * TX_TIMER_TICKS_PER_SECOND); // Wait 3 seconds
    printf("Sensors should be ready now\r\n");

    // Initialize the network with configuration from persistent storage
    printf("Connecting to WiFi: %s\r\n", WIFI_SSID);
    if ((status = wwd_network_init(WIFI_SSID, WIFI_PASSWORD, WIFI_MODE)))
    {
        printf("ERROR: Failed to initialize the network (0x%08lx)\r\n", (unsigned long)status);
    }
    
    // Connect to WiFi and get IP address via DHCP
    else if ((status = wwd_network_connect()))
    {
        printf("ERROR: Failed to connect to network (0x%08lx)\r\n", (unsigned long)status);
    }

#ifdef ENABLE_LEGACY_MQTT
    else
    {
        printf("Connecting to MQTT broker: %s:%d\r\n", MQTT_BROKER_HOSTNAME, MQTT_BROKER_PORT);
        if ((status = azure_iot_mqtt_entry(&nx_ip, &nx_pool[0], &nx_dns_client, sntp_time_get)))
        {
            printf("ERROR: Failed to run MQTT client (0x%08lx)\r\n", (unsigned long)status);
        }
    }
#else
    else
    {
        printf("ERROR: MQTT client is not enabled. Define ENABLE_LEGACY_MQTT in azure_config.h\r\n");
    }
#endif
}

// Try to load configuration from persistent storage after WiFi is stable
void try_load_persistent_config_after_wifi(void) {
    device_config_t persistent_config;
    
    printf("Attempting to load saved configuration from persistent storage...\r\n");
    
    if (config_manager_load_from_persistent_storage(&persistent_config) == CONFIG_OK) {
        printf("Found saved configuration in persistent storage!\r\n");
        printf("Loaded config:\r\n");
        printf("  WiFi SSID: %s\r\n", persistent_config.wifi_ssid);
        printf("  MQTT Broker: %s:%u\r\n", persistent_config.mqtt_hostname, (unsigned int)persistent_config.mqtt_port);
        printf("  MQTT Client ID: %s\r\n", persistent_config.mqtt_client_id);
        
        // Use this config for the next reboot
        printf("This configuration will be available on the next reboot\r\n");
    } else {
        printf("No saved configuration found in persistent storage\r\n");
    }
}

void tx_application_define(void* first_unused_memory)
{
    systick_interval_set(TX_TIMER_TICKS_PER_SECOND);

    // Create MQTT thread
    UINT status = tx_thread_create(&mqtt_thread,
        "MQTT Thread",
        mqtt_thread_entry,
        0,
        mqtt_thread_stack,
        MQTT_THREAD_STACK_SIZE,
        MQTT_THREAD_PRIORITY,
        MQTT_THREAD_PRIORITY,
        TX_NO_TIME_SLICE,
        TX_AUTO_START);

    if (status != TX_SUCCESS)
    {
        printf("ERROR: MQTT thread creation failed\r\n");
    }
}

int main(void)
{
    // Initialize the board
    board_init();

    // Enter the ThreadX kernel
    tx_kernel_enter();

    return 0;
}



// Display device information on OLED screen
static void display_device_info(void) {
    // Reset to default view and display it
    display_mode = 0;
    cycle_display_info();
}
// Override the weak button_a_callback from board_init.c
void button_a_callback(void) {
    printf("Button A pressed - cycling display\r\n");
    // Cycle through display modes
    cycle_display_info();
}

// Override the weak button_b_callback from board_init.c  
void button_b_callback(void) {
    printf("Button B pressed - cycling telemetry\r\n");
    // Cycle through telemetry sensor readings
    cycle_telemetry_info();
}

// Cycle through different configuration displays when Button A is pressed
static void cycle_display_info(void) {
    char line_buffer[32];
    
    // Increment display mode and wrap around
    display_mode = (display_mode + 1) % 5;
    
    // Clear screen and show device name (always on top line)
    ssd1306_Fill(Black);
    ssd1306_SetCursor(2, L0);
    ssd1306_WriteString("MXChip AZ3166", Font_11x18, White);
    
    switch (display_mode) {
        case 0:  // Default view - overview
            ssd1306_SetCursor(2, L1);
            snprintf(line_buffer, sizeof(line_buffer), "WiFi: %.15s", g_device_config.wifi_ssid);
            ssd1306_WriteString(line_buffer, Font_11x18, White);
            
            ssd1306_SetCursor(2, L2);
            snprintf(line_buffer, sizeof(line_buffer), "MQTT: %.15s", g_device_config.mqtt_hostname);
            ssd1306_WriteString(line_buffer, Font_11x18, White);
            
            ssd1306_SetCursor(2, L3);
            snprintf(line_buffer, sizeof(line_buffer), "ID: %.18s", g_device_config.mqtt_client_id);
            ssd1306_WriteString(line_buffer, Font_11x18, White);
            break;
            
        case 1:  // MQTT Client ID
            ssd1306_SetCursor(2, L1);
            ssd1306_WriteString("MQTT CLIENT ID:", Font_11x18, White);
            ssd1306_SetCursor(2, L2);
            snprintf(line_buffer, sizeof(line_buffer), "%.20s", g_device_config.mqtt_client_id);
            ssd1306_WriteString(line_buffer, Font_11x18, White);
            break;
            
        case 2:  // MQTT Broker Hostname
            ssd1306_SetCursor(2, L1);
            ssd1306_WriteString("MQTT BROKER:", Font_11x18, White);
            ssd1306_SetCursor(2, L2);
            snprintf(line_buffer, sizeof(line_buffer), "%.20s", g_device_config.mqtt_hostname);
            ssd1306_WriteString(line_buffer, Font_11x18, White);
            break;
            
        case 3:  // MQTT Port
            ssd1306_SetCursor(2, L1);
            ssd1306_WriteString("MQTT PORT:", Font_11x18, White);
            ssd1306_SetCursor(2, L2);
            snprintf(line_buffer, sizeof(line_buffer), "%u", (unsigned int)g_device_config.mqtt_port);
            ssd1306_WriteString(line_buffer, Font_11x18, White);
            break;
            
        case 4:  // WiFi SSID
            ssd1306_SetCursor(2, L1);
            ssd1306_WriteString("WIFI SSID:", Font_11x18, White);
            ssd1306_SetCursor(2, L2);
            snprintf(line_buffer, sizeof(line_buffer), "%.20s", g_device_config.wifi_ssid);
            ssd1306_WriteString(line_buffer, Font_11x18, White);
            break;
    }
    
    ssd1306_UpdateScreen();
}

// Cycle through telemetry sensor values when Button B is pressed
static void cycle_telemetry_info(void) {
    char line_buffer[32];
    char line_buffer2[32];
    
    // Increment telemetry mode and wrap around
    telemetry_mode = (telemetry_mode + 1) % 5;
    
    // Clear screen and show device name (always on top line)
    ssd1306_Fill(Black);
    ssd1306_SetCursor(2, L0);
    ssd1306_WriteString("MXChip AZ3166", Font_11x18, White);
    
    switch (telemetry_mode) {
        case 0: {  // Pressure & Temperature (LPS22HB)
            lps22hb_t pressure_data = lps22hb_data_read();
            ssd1306_SetCursor(2, L1);
            ssd1306_WriteString("PRESSURE:", Font_11x18, White);
            ssd1306_SetCursor(2, L2);
            snprintf(line_buffer, sizeof(line_buffer), "%.1f hPa", (double)pressure_data.pressure_hPa);
            ssd1306_WriteString(line_buffer, Font_11x18, White);
            ssd1306_SetCursor(2, L3);
            snprintf(line_buffer2, sizeof(line_buffer2), "%.1f C", (double)pressure_data.temperature_degC);
            ssd1306_WriteString(line_buffer2, Font_11x18, White);
            break;
        }
        
        case 1: {  // Humidity & Temperature (HTS221)
            hts221_data_t humidity_data = hts221_data_read();
            ssd1306_SetCursor(2, L1);
            ssd1306_WriteString("HUMIDITY:", Font_11x18, White);
            ssd1306_SetCursor(2, L2);
            snprintf(line_buffer, sizeof(line_buffer), "%.1f %%", (double)humidity_data.humidity_perc);
            ssd1306_WriteString(line_buffer, Font_11x18, White);
            ssd1306_SetCursor(2, L3);
            snprintf(line_buffer2, sizeof(line_buffer2), "%.1f C", (double)humidity_data.temperature_degC);
            ssd1306_WriteString(line_buffer2, Font_11x18, White);
            break;
        }
        
        case 2: {  // Accelerometer (LSM6DSL)
            lsm6dsl_data_t accel_data = lsm6dsl_data_read();
            ssd1306_SetCursor(2, L1);
            ssd1306_WriteString("ACCELEROMETER:", Font_11x18, White);
            ssd1306_SetCursor(2, L2);
            snprintf(line_buffer, sizeof(line_buffer), "X:%.0f Y:%.0f", (double)accel_data.acceleration_mg[0], (double)accel_data.acceleration_mg[1]);
            ssd1306_WriteString(line_buffer, Font_11x18, White);
            ssd1306_SetCursor(2, L3);
            snprintf(line_buffer2, sizeof(line_buffer2), "Z:%.0f mg", (double)accel_data.acceleration_mg[2]);
            ssd1306_WriteString(line_buffer2, Font_11x18, White);
            break;
        }
        
        case 3: {  // Gyroscope (LSM6DSL)
            lsm6dsl_data_t gyro_data = lsm6dsl_data_read();
            ssd1306_SetCursor(2, L1);
            ssd1306_WriteString("GYROSCOPE:", Font_11x18, White);
            ssd1306_SetCursor(2, L2);
            snprintf(line_buffer, sizeof(line_buffer), "X:%.0f Y:%.0f", (double)gyro_data.angular_rate_mdps[0], (double)gyro_data.angular_rate_mdps[1]);
            ssd1306_WriteString(line_buffer, Font_11x18, White);
            ssd1306_SetCursor(2, L3);
            snprintf(line_buffer2, sizeof(line_buffer2), "Z:%.0f mdps", (double)gyro_data.angular_rate_mdps[2]);
            ssd1306_WriteString(line_buffer2, Font_11x18, White);
            break;
        }
        
        case 4: {  // Magnetometer (LIS2MDL)
            lis2mdl_data_t mag_data = lis2mdl_data_read();
            ssd1306_SetCursor(2, L1);
            ssd1306_WriteString("MAGNETOMETER:", Font_11x18, White);
            ssd1306_SetCursor(2, L2);
            snprintf(line_buffer, sizeof(line_buffer), "X:%.0f Y:%.0f", (double)mag_data.magnetic_mG[0], (double)mag_data.magnetic_mG[1]);
            ssd1306_WriteString(line_buffer, Font_11x18, White);
            ssd1306_SetCursor(2, L3);
            snprintf(line_buffer2, sizeof(line_buffer2), "Z:%.0f mG", (double)mag_data.magnetic_mG[2]);
            ssd1306_WriteString(line_buffer2, Font_11x18, White);
            break;
        }
    }
    
    ssd1306_UpdateScreen();
}
