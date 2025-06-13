# MXChip AZ3166 DHCP and WiFi Troubleshooting Guide

## Overview
This guide helps troubleshoot WiFi connection and DHCP issues specifically for the MXChip AZ3166 device.

## Changes Made
The code has been updated to:
1. **Fix the main issue**: Added `wwd_network_connect()` call in `main.c` to properly establish WiFi connection before attempting MQTT
2. **Enhanced logging**: Added detailed DHCP status reporting with IP address display
3. **Better error reporting**: More descriptive error messages for DHCP failures

## Expected Output After Fix
When working correctly, you should see:
```
Initializing WiFi
	MAC address: XX:XX:XX:XX:XX:XX
SUCCESS: WiFi initialized

Connecting WiFi
	Connecting to SSID 'YourSSID' with mode 4
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

## Root Cause Analysis
The original issue was that:
1. `wwd_network_init()` only initialized the network stack
2. `wwd_network_connect()` was never called in the main flow
3. MQTT client tried to connect without WiFi being established
4. Device never requested or received a DHCP IP address

## Troubleshooting Steps

### 1. Check WiFi Configuration
Verify in `azure_config.h`:
```c
#define WIFI_SSID       "YourNetworkName"
#define WIFI_PASSWORD   "YourPassword"  
#define WIFI_MODE       WPA2_PSK_AES
```

### 2. Monitor Serial Output
Look for these key messages:
- "SUCCESS: WiFi initialized" - Hardware is working
- "SUCCESS: WiFi connected" - Associated with access point
- "DHCP IP Configuration" section - Got IP address
- "SUCCESS: DHCP initialized" - Network fully ready

### 3. Common DHCP Issues

#### Issue: "ERROR: Can't resolve DHCP address"
**Possible Causes:**
- Router DHCP disabled
- Router DHCP pool exhausted
- Network congestion
- WiFi authentication issues

**Solutions:**
- Check router DHCP settings
- Restart router
- Try connecting other devices to same network
- Verify WiFi credentials

#### Issue: WiFi connects but no DHCP
**Possible Causes:**
- Router isolating wireless clients
- Network security policies
- DHCP server on different subnet

**Solutions:**
- Check router wireless isolation settings
- Try connecting to a different network
- Check if router has guest network restrictions

### 4. Network Verification
If you get an IP address, verify connectivity:
- Device should display IP, mask, and gateway
- MQTT broker at 172.16.5.151:1883 should be reachable from device's network
- Check firewall rules on both device network and broker network

### 5. Timing Considerations
The code now:
- Waits up to 30 seconds for DHCP (DHCP_WAIT_TIME_TICKS)
- Retries WiFi connection attempts with 5-second delays
- Properly sequences WiFi → DHCP → DNS → MQTT

### 6. Advanced Debugging
If issues persist:
1. Use a WiFi analyzer to check signal strength
2. Monitor router logs for connection attempts
3. Verify the MXChip device MAC address in router logs
4. Test with a mobile hotspot to isolate router issues

## Key Files Modified
1. `app/main.c` - Added `wwd_network_connect()` call
2. `app/wwd_networking.c` - Enhanced DHCP logging and error reporting

## Next Steps
After applying this fix:
1. Build and flash the updated firmware
2. Monitor serial output for the new detailed network connection flow
3. Verify you see the "DHCP IP Configuration" section with a valid IP address
4. Confirm MQTT connection succeeds after network is established

If DHCP still fails after this fix, the issue is likely with the network infrastructure (router, DHCP server) rather than the device code.
