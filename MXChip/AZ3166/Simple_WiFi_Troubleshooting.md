# Simple WiFi Troubleshooting for MXChip AZ3166

## Step-by-Step Troubleshooting

### Step 1: Try Different Security Mode
In `azure_config.h`, change the WiFi mode:
```c
#define WIFI_MODE     WPA_PSK_TKIP  // Instead of WPA2_PSK_AES
```

### Step 2: Router Settings to Check
1. **2.4GHz Band**: Make sure it's enabled (not just 5GHz)
2. **Channel**: Use channels 1, 6, or 11 (avoid auto-channel)
3. **Channel Width**: Set to 20MHz (not 40MHz or 80MHz)
4. **WiFi Standard**: Ensure 802.11n/g/b is enabled

### Step 3: Test with Mobile Hotspot
Create a simple mobile hotspot with:
- 2.4GHz only
- WPA2 security
- Simple password (no special characters)

### Step 4: Monitor Serial Output
Connect via USB and monitor at 115200 baud to see:
- WiFi initialization messages
- Connection attempt details
- Error codes

### Step 5: Hardware Reset
1. Hold the **A button** while pressing **Reset**
2. Release **A** after device restarts
3. Try connection again

If none of these work, the issue might be hardware-related or require firmware updates.
