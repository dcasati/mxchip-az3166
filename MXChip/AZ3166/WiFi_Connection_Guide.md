# MXChip WiFi Troubleshooting Guide

## Common WiFi Connection Issues

### Problem: Device can't connect to WiFi (0.0.0.0 IP address)

If your MXChip AZ3166 device isn't connecting to your WiFi network despite having the correct SSID and password, try the following solutions:

## Configuration Changes Made

1. **WiFi Debug Mode**: 
   - Added `WIFI_DEBUG_ENABLE` for more detailed connection logs
   - Increased connection timeout with `WIFI_CONNECTION_TIMEOUT_MS`
   - Added retry attempts with `WIFI_MAX_RETRY_COUNT`

2. **Static IP Configuration**:
   - Enabled static IP addressing with `WIFI_USE_STATIC_IP`
   - Set a static IP (172.16.5.100) to avoid DHCP issues
   - Configured subnet, gateway, and DNS settings

## Additional Troubleshooting Steps

### 1. Check WiFi Router Settings

- **Channel Settings**: Ensure your router is using a channel below 13 (ideally 1, 6, or 11)
- **Band Selection**: Confirm your router's 2.4GHz band is enabled (MXChip doesn't support 5GHz)
- **MAC Filtering**: If enabled, add your MXChip's MAC address to allowed devices
- **Signal Strength**: Position the device closer to your router or access point

### 2. Network Security

- **Security Mode**: Try changing `WIFI_MODE` (WPA2_PSK_AES is most common, but some routers may need WPA_PSK_TKIP)
- **Special Characters**: If your password contains special characters, verify they're properly escaped

### 3. Hardware Reset

1. Press and hold the **A button** while pressing and releasing the **Reset button**
2. Release the **A button** after the device restarts
3. This performs a factory reset on the device

### 4. Serial Output Monitoring

Monitor the device via USB serial at 115200 baud to see detailed connection logs. Look for:
- WiFi initialization messages
- Authentication attempts
- IP addressing information
- Connection status codes

### 5. Alternative Networks

If possible, test connecting to:
- A mobile hotspot (often has simpler security)
- A guest network (may have fewer restrictions)
- An open network temporarily (to eliminate password issues)

## Next Steps After Making Changes

1. Rebuild the application:
   ```
   getting-started\MXChip\AZ3166\tools\rebuild.bat
   ```

2. Flash the new firmware:
   - Copy the `mxchip_azure_iot.bin` file to the MXChip drive

3. Monitor the serial output (115200 baud) for detailed connection information

If the device connects to WiFi but still can't reach the MQTT broker, check the network connectivity between your WiFi network and the MQTT broker at 172.16.5.151.
