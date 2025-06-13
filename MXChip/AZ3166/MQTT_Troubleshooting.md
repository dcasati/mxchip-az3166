# MQTT Connection Troubleshooting Guide

## Issue Analysis
Based on the error logs, the following issues were detected:

1. **Network Connectivity Issues**
   - The device interface shows IP: 0.0.0.0
   - No gateway configured
   - All ping attempts failed (MQTT broker, default gateway, Google DNS)

2. **MQTT Connection Failures**
   - TCP connection attempts to broker failed (both port 1883 and 8883)
   - MQTT connection attempts timed out with error code 0x00010005 (NXD_MQTT_CONNECT_FAILURE)

## Troubleshooting Steps

### 1. WiFi Connection Issues
- Verify the WiFi credentials (SSID and password) are correct
- Check if your WiFi network is 2.4 GHz (the MXChip does not support 5 GHz networks)
- If connecting to a hidden SSID, uncomment the `WIFI_IS_HIDDEN` definition
- Check if your WiFi network has MAC address filtering enabled and ensure the MXChip's MAC is allowed

### 2. IP Configuration Issues
- If your network requires static IP, uncomment and configure the static IP settings:
  - WIFI_USE_STATIC_IP
  - WIFI_IP_ADDRESS
  - WIFI_SUBNET_MASK
  - WIFI_GATEWAY
  - WIFI_DNS_SERVER

### 3. MQTT Broker Connection Issues
- Verify the MQTT broker is running and accessible from your network
- Check if the broker requires authentication (if so, set MQTT_USERNAME and MQTT_PASSWORD)
- Try connecting to the broker from another device on the same network to verify it's working
- Check if the broker has any client ID restrictions
- Verify there are no firewall rules blocking connections to the broker

### 4. Hardware Reset
- Press the reset button on the MXChip device
- Try a power cycle (disconnect and reconnect USB cable)

### 5. Debug Output
- Monitor the serial output from the device (115200 baud rate) to see more detailed error messages
- Add debug print statements to your code to trace the connection process

## Common Solutions

1. **If your WiFi network is working but device still can't connect:**
   - Try a different WiFi network if possible
   - Create a mobile hotspot as a test network
   - Check for any WiFi extenders or mesh networks that might be causing issues

2. **If your device connects to WiFi but can't reach the MQTT broker:**
   - Try using the broker's hostname instead of IP address if DNS is available
   - Try a different port (1883, 8883, 8884) if your broker supports multiple ports
   - Check if TLS/SSL is required (port 8883) and configure accordingly
   - Verify the broker is accepting connections and not at capacity

3. **If MQTT authentication fails:**
   - Double check username and password
   - Verify if the broker requires client certificates
   - Check if the broker has access control lists (ACLs) restricting your topics

## Next Steps
After making changes to the configuration:
1. Rebuild the application using `getting-started\MXChip\AZ3166\tools\rebuild.bat`
2. Flash the new image to your device
3. Monitor the serial output for more detailed information
