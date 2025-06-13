# MXChip AZ3166 WiFi and DHCP Troubleshooting Guide

## Issue
The MXChip AZ3166 device connects to WiFi but doesn't receive an IP address via DHCP, showing IP=0.0.0.0 in logs, which prevents MQTT connections.

## Code Changes Applied

### 1. Fixed WiFi Connection Sequence
**File:** `MXChip/AZ3166/app/main.c`

Added explicit call to `wwd_network_connect()` after `wwd_network_init()` to ensure WiFi is connected and DHCP completes before attempting MQTT connections.

**Before:**
```c
// Initialize the network
if ((status = wwd_network_init(WIFI_SSID, WIFI_PASSWORD, WIFI_MODE)))
{
    printf("ERROR: Failed to initialize the network (0x%08x)\r\n", status);
}

#ifdef ENABLE_LEGACY_MQTT
else if ((status = azure_iot_mqtt_entry(&nx_ip, &nx_pool[0], &nx_dns_client, sntp_time_get)))
```

**After:**
```c
// Initialize the network
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
else if ((status = azure_iot_mqtt_entry(&nx_ip, &nx_pool[0], &nx_dns_client, sntp_time_get)))
```

### 2. Enhanced DHCP Debugging
**File:** `MXChip/AZ3166/app/wwd_networking.c`

#### Enhanced DHCP Error Messages:
```c
printf("\r\nInitializing DHCP\r\n");
printf("Waiting for DHCP lease assignment...\r\n");

// Wait until address is solved.
if ((status = nx_ip_status_check(&nx_ip, NX_IP_ADDRESS_RESOLVED, &actual_status, DHCP_WAIT_TIME_TICKS)))
{
    // DHCP Failed...  no IP address!
    printf("ERROR: Can't resolve DHCP address (0x%08lx)\r\n", (unsigned long)status);
    printf("       Actual status: 0x%08lx\r\n", (unsigned long)actual_status);
    printf("       Check WiFi connection and router DHCP settings\r\n");
    return status;
}
```

#### Enhanced IP Address Display:
```c
// Output IP address and gateway address
printf("\r\n=============================\r\n");
printf("DHCP IP Configuration\r\n");
printf("=============================\r\n");
print_address("IP address", ip_address);
print_address("Mask", network_mask);
print_address("Gateway", gateway_address);
printf("=============================\r\n");
```

#### Enhanced WiFi Connection Messages:
```c
// Connect to the specified SSID
printf("\tConnecting to SSID '%s' with mode %d\r\n", netx_ssid, netx_mode);
printf("\tPlease wait while WiFi attempts to connect...\r\n");
```

## What This Fixes

1. **Connection Sequence**: Ensures WiFi is fully connected and has an IP address before attempting MQTT connections
2. **DHCP Timeout**: The device now properly waits for DHCP lease assignment (30 seconds timeout)
3. **Debug Visibility**: Clear indication when IP address is assigned and what the values are
4. **Error Reporting**: Better error messages to identify DHCP vs WiFi connection issues

## Expected Output

After successful connection, you should see:
```
Initializing WiFi
	MAC address: XX:XX:XX:XX:XX:XX
SUCCESS: WiFi initialized

Connecting WiFi
	Connecting to SSID 'YourSSID' with mode 16
	Please wait while WiFi attempts to connect...
	Attempt 1...
SUCCESS: WiFi connected

Initializing DHCP
Waiting for DHCP lease assignment...

=============================
DHCP IP Configuration
=============================
	IP address: 192.168.1.XXX
	Mask: 255.255.255.0
	Gateway: 192.168.1.1
=============================
SUCCESS: DHCP initialized
```

## If Issues Persist

1. **Check WiFi Credentials**: Verify SSID and password in `azure_config.h`
2. **Check WiFi Security**: Ensure WPA2_PSK_AES is correct for your router
3. **Router DHCP Pool**: Verify router has available DHCP addresses
4. **Network Range**: Check if device MAC address is blocked
5. **Increase Timeout**: If needed, increase `DHCP_WAIT_TIME_TICKS` in `wwd_networking.c`

## Configuration File
Your current configuration in `azure_config.h`:
- WiFi Mode: WPA2_PSK_AES (recommended)
- MQTT Broker: 172.16.5.151:1883 (local network)
