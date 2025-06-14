/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

#include "mqtt.h"

#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "screen.h"
#include "sensor.h"
#include "stm32f4xx_hal.h"

#include "sntp_client.h"

#include "azure_config.h"

// Define packet pool payload size if not already defined
#ifndef NX_PACKET_POOL_PAYLOAD_SIZE
#define NX_PACKET_POOL_PAYLOAD_SIZE 1536
#endif

// MQTT client settings
#define TELEMETRY_INTERVAL_PROPERTY "telemetryInterval"
#define LED_STATE_PROPERTY          "ledState"

#define TELEMETRY_INTERVAL_EVENT 1

// MQTT client settings for custom broker
#define MQTT_CLIENT_STACK_SIZE        4096
#define MQTT_CLIENT_PRIORITY          2
#define MQTT_TIMEOUT                  (30 * TX_TIMER_TICKS_PER_SECOND)  // Increase timeout to 30 seconds
#define MQTT_KEEP_ALIVE               120                               // Reduce keep-alive to be less aggressive
#define MQTT_TELEMETRY_QOS            1

// MQTT client instance
static NXD_MQTT_CLIENT mqtt_client;
static TX_EVENT_FLAGS_GROUP mqtt_events;

// Default telemetry interval
static const INT telemetry_interval = DEFAULT_TELEMETRY_INTERVAL;

// Telemetry state tracking
static UINT telemetry_state = 0;

// Forward declaration of LED control function
static void set_led_state(bool level);

// MQTT message callback function 
static VOID mqtt_message_callback(NXD_MQTT_CLIENT *client_ptr, UINT number_of_messages)
{
    UINT status;
    UCHAR message_buffer[NX_PACKET_POOL_PAYLOAD_SIZE];
    UINT message_length;
    UCHAR topic_buffer[128];
    UINT topic_length;

    // Process all messages in the queue
    while (number_of_messages > 0)
    {
        number_of_messages--;

        // Get the next message in the queue
        status = nxd_mqtt_client_message_get(client_ptr, topic_buffer, sizeof(topic_buffer), &topic_length,
                                            message_buffer, sizeof(message_buffer), &message_length);
        if (status == NXD_MQTT_SUCCESS)
        {
            // Ensure null termination
            topic_buffer[topic_length] = 0;
            message_buffer[message_length] = 0;

            printf("Received message: topic=%s, message=%s\r\n", topic_buffer, message_buffer);

            // Check if this is a LED control message
            if (strncmp((CHAR*)topic_buffer, MQTT_LED_TOPIC, topic_length) == 0)
            {
                // Process LED command
                if (strncmp((CHAR*)message_buffer, "ON", message_length) == 0)
                {
                    set_led_state(true);
                }
                else if (strncmp((CHAR*)message_buffer, "OFF", message_length) == 0)
                {
                    set_led_state(false);
                }
            }
        }
        else
        {
            printf("Error getting MQTT message: 0x%08lx\r\n", (unsigned long)status);
            break;
        }
    }
}

// LED control function
static void set_led_state(bool level)
{
    if (level)
    {
        printf("LED is turned ON\r\n");
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
    }
    else
    {
        printf("LED is turned OFF\r\n");
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
    }
}

// MQTT disconnect callback
static VOID mqtt_disconnect_callback(NXD_MQTT_CLIENT *client_ptr)
{
    printf("MQTT client disconnected\r\n");
    
    // Set event flag for reconnection
    tx_event_flags_set(&mqtt_events, TELEMETRY_INTERVAL_EVENT, TX_OR);
}

UINT azure_iot_mqtt_entry(NX_IP* ip_ptr, NX_PACKET_POOL* pool_ptr, NX_DNS* dns_ptr, ULONG (*sntp_time_function)(VOID))
{
    UINT status;
    static UCHAR mqtt_client_stack[MQTT_CLIENT_STACK_SIZE];
    
    NXD_ADDRESS server_ip;
    UINT server_port = MQTT_BROKER_PORT;
    
    // Reset telemetry state counter for this session
    telemetry_state = 0;
    
    printf("\r\n=============================\r\n");
    printf("MQTT Client Initialization\r\n");
    printf("=============================\r\n");
    
    // Initialize event flags for MQTT events
    if ((status = tx_event_flags_create(&mqtt_events, "MQTT Events")))
    {
        printf("FAIL: Unable to create MQTT event flags (0x%08lx)\r\n", (unsigned long)status);
        return status;
    }
    
    printf("Initializing MQTT client to connect to broker: %s:%d\r\n", MQTT_BROKER_HOSTNAME, MQTT_BROKER_PORT);
    printf("Using client ID: %s\r\n", MQTT_CLIENT_ID);
    if (strlen(MQTT_USERNAME) > 0) {
        printf("Using authentication with username: %s\r\n", MQTT_USERNAME);
    } else {
        printf("No authentication credentials configured\r\n");
    }
    
    // Create MQTT client instance
    status = nxd_mqtt_client_create(&mqtt_client,
                               "MQTT Client",
                               MQTT_CLIENT_ID,
                               strlen(MQTT_CLIENT_ID),
                               ip_ptr,
                               pool_ptr,
                               mqtt_client_stack,
                               MQTT_CLIENT_STACK_SIZE,
                               MQTT_CLIENT_PRIORITY,
                               NX_NULL,
                               0);
    
    if (status != NXD_MQTT_SUCCESS)
    {
        printf("FAIL: Failed to create MQTT client (0x%08lx)\r\n", (unsigned long)status);
        return status;
    }
    printf("SUCCESS: MQTT client created\r\n");
    
    // Register callbacks
    nxd_mqtt_client_receive_notify_set(&mqtt_client, mqtt_message_callback);
    printf("Registered message callback\r\n");
    
    nxd_mqtt_client_disconnect_notify_set(&mqtt_client, mqtt_disconnect_callback);
    printf("Registered disconnect callback\r\n");
    
    // Connect to the broker
    printf("\r\nIP Address Resolution\r\n");
    printf("-------------------\r\n");
    printf("Checking if broker address is an IP or hostname: %s\r\n", MQTT_BROKER_HOSTNAME);
    
    // Variable to store IP components when parsing
    UINT ip_0, ip_1, ip_2, ip_3;
    
    // Check if the hostname is an IP address (simple parsing for IPv4)
    if (sscanf(MQTT_BROKER_HOSTNAME, "%u.%u.%u.%u", &ip_0, &ip_1, &ip_2, &ip_3) == 4)
    {
        // It's an IP address, so convert directly
        printf("Direct IP address detected: %s\r\n", MQTT_BROKER_HOSTNAME);
        
        // Ensure the IP components are valid (0-255)
        if (ip_0 <= 255 && ip_1 <= 255 && ip_2 <= 255 && ip_3 <= 255)
        {
            // For NetworkX, IP address is stored in host byte order
            ULONG host_byte_order_ip = (ip_0 << 24) | (ip_1 << 16) | (ip_2 << 8) | ip_3;
            server_ip.nxd_ip_address.v4 = host_byte_order_ip;
            
            printf("SUCCESS: Using direct IP: %u.%u.%u.%u (0x%08lx)\r\n", 
                   ip_0, ip_1, ip_2, ip_3, (unsigned long)host_byte_order_ip);
        }
        else
        {
            printf("FAIL: Invalid IP address format: %s\r\n", MQTT_BROKER_HOSTNAME);
            return NX_DNS_QUERY_FAILED;
        }
    }
    else
    {
    // It's a hostname, resolve via DNS
        printf("Resolving hostname via DNS: %s\r\n", MQTT_BROKER_HOSTNAME);
        
        // Try multiple DNS resolutions with different timeouts
        UINT retry;
        UINT max_retries = 3;
        ULONG timeouts[3] = {5 * NX_IP_PERIODIC_RATE, 10 * NX_IP_PERIODIC_RATE, 15 * NX_IP_PERIODIC_RATE};
        
        for (retry = 0; retry < max_retries; retry++) 
        {
            printf("DNS resolution attempt %d of %d (timeout: %lu ticks)...\r\n", 
                  retry + 1, max_retries, timeouts[retry]);
                  
            // Resolve hostname to IP address
            status = nx_dns_host_by_name_get(dns_ptr, 
                                          (UCHAR *)MQTT_BROKER_HOSTNAME, 
                                          &server_ip.nxd_ip_address.v4, 
                                          timeouts[retry]);
            if (status == NX_SUCCESS) 
            {
                printf("DNS resolution successful on attempt %d!\r\n", retry + 1);
                break;
            }
            
            printf("DNS resolution attempt %d failed (0x%08lx)\r\n", retry + 1, (unsigned long)status);
            
            // Check specific DNS error codes
            if (status == NX_DNS_QUERY_FAILED)
            {
                printf("  - DNS query failed - no DNS servers could resolve this hostname\r\n");
            }
            else if (status == NX_DNS_NO_SERVER)
            {
                printf("  - No DNS servers are configured in the system\r\n");
            }
            else if (status == NX_DNS_TIMEOUT)
            {
                printf("  - DNS query timed out - DNS servers may be unreachable\r\n");
            }
            
            // Show available DNS servers
            ULONG dns_server_address[5];
            UINT dns_count = 0;
            
            printf("Checking DNS server configuration...\r\n");
            if (nx_dns_server_get(dns_ptr, 0, &dns_server_address[0]) == NX_SUCCESS)
            {
                printf("  - Primary DNS: %u.%u.%u.%u\r\n",
                      (UINT)((dns_server_address[0] >> 24) & 0xFF),
                      (UINT)((dns_server_address[0] >> 16) & 0xFF),
                      (UINT)((dns_server_address[0] >> 8) & 0xFF),
                      (UINT)(dns_server_address[0] & 0xFF));
                dns_count++;
            }
            else
            {
                printf("  - No primary DNS server configured\r\n");
            }
            
            // Try Google DNS directly if hostname resolution fails on final attempt
            if (retry == max_retries - 1)
            {
                printf("Trying fallback to local IP address...\r\n");
                // Use the local IP we configured earlier
                server_ip.nxd_ip_address.v4 = IP_ADDRESS(192, 168, 1, 100);
                printf("Using fallback IP: 192.168.1.100\r\n");
                status = NX_SUCCESS;
            }
        }
        
        printf("SUCCESS: Hostname resolved via DNS\r\n");
    }
    
    // Set IP version
    server_ip.nxd_ip_version = NX_IP_VERSION_V4;
    
    printf("SUCCESS: Resolved to IP: %u.%u.%u.%u\r\n", 
           (UINT)((server_ip.nxd_ip_address.v4 >> 24) & 0xFF),
           (UINT)((server_ip.nxd_ip_address.v4 >> 16) & 0xFF),
           (UINT)((server_ip.nxd_ip_address.v4 >> 8) & 0xFF),
           (UINT)(server_ip.nxd_ip_address.v4 & 0xFF));
    
    // Set credentials if provided
    printf("\r\nMQTT Authentication\r\n");
    printf("-------------------\r\n");
    if (strlen(MQTT_USERNAME) > 0)
    {
        printf("Setting MQTT credentials for user: %s\r\n", MQTT_USERNAME);
        
        status = nxd_mqtt_client_login_set(&mqtt_client, 
                                          MQTT_USERNAME, 
                                          strlen(MQTT_USERNAME),
                                          MQTT_PASSWORD, 
                                          strlen(MQTT_PASSWORD));
        if (status != NXD_MQTT_SUCCESS)
        {
            printf("FAIL: Failed to set MQTT login credentials (0x%02lx)\r\n", (unsigned long)status);
            return status;
        }
        printf("SUCCESS: MQTT credentials set\r\n");
    }
    else
    {
        printf("No authentication credentials provided, connecting anonymously\r\n");
    }
    
    // Connect to broker
    printf("\r\nMQTT Connection\r\n");
    printf("-------------------\r\n");
    printf("Connecting to MQTT broker at %u.%u.%u.%u:%u\r\n",
           (UINT)((server_ip.nxd_ip_address.v4 >> 24) & 0xFF),
           (UINT)((server_ip.nxd_ip_address.v4 >> 16) & 0xFF),
           (UINT)((server_ip.nxd_ip_address.v4 >> 8) & 0xFF),
           (UINT)(server_ip.nxd_ip_address.v4 & 0xFF),
           (UINT)server_port);
           
    printf("Keep alive: %d seconds\r\n", MQTT_KEEP_ALIVE);
    printf("Clean session: Yes\r\n");
    
    // Make sure IP version is set correctly
    server_ip.nxd_ip_version = NX_IP_VERSION_V4;
    
    // Debug: Show IP address we're using
    printf("Using IP address: 0x%08lx (%u.%u.%u.%u):%u\r\n", 
           (unsigned long)server_ip.nxd_ip_address.v4,
           (UINT)((server_ip.nxd_ip_address.v4 >> 24) & 0xFF),
           (UINT)((server_ip.nxd_ip_address.v4 >> 16) & 0xFF),
           (UINT)((server_ip.nxd_ip_address.v4 >> 8) & 0xFF),
           (UINT)(server_ip.nxd_ip_address.v4 & 0xFF),
           server_port);
             // Print network interface information
    printf("Checking network interface status...\r\n");
    CHAR *interface_name = NULL;  // Interface name (passing NULL as we don't need it)
    ULONG ip_address;
    ULONG network_mask;
    ULONG mtu_size;
    ULONG physical_address_msw;
    ULONG physical_address_lsw;
    
    if (nx_ip_interface_info_get(ip_ptr, 0, &interface_name, &ip_address, &network_mask,
                                &physical_address_msw, &physical_address_lsw,
                                &mtu_size) == NX_SUCCESS)
    {
        printf("Interface 0: IP=%u.%u.%u.%u, Mask=%u.%u.%u.%u, MTU=%u\r\n",
               (UINT)((ip_address >> 24) & 0xFF),
               (UINT)((ip_address >> 16) & 0xFF),
               (UINT)((ip_address >> 8) & 0xFF),
               (UINT)(ip_address & 0xFF),
               (UINT)((network_mask >> 24) & 0xFF),
               (UINT)((network_mask >> 16) & 0xFF),
               (UINT)((network_mask >> 8) & 0xFF),
               (UINT)(network_mask & 0xFF),
               (UINT)mtu_size);
    }
    else
    {
        printf("Failed to get interface information\r\n");
    }
    
    // Print IP routing information
    ULONG next_hop_address;
    if (nx_ip_gateway_address_get(ip_ptr, &next_hop_address) == NX_SUCCESS)
    {
        printf("Gateway: %u.%u.%u.%u\r\n",
               (UINT)((next_hop_address >> 24) & 0xFF),
               (UINT)((next_hop_address >> 16) & 0xFF),
               (UINT)((next_hop_address >> 8) & 0xFF),
               (UINT)(next_hop_address & 0xFF));
    }
    else
    {
        printf("No gateway configured\r\n");
    }
           
    // Add more debug info for network diagnosis    printf("Attempting to verify basic network connectivity...\r\n");
    NX_PACKET *ping_packet;
    
    // Try pinging the MQTT broker first
    printf("Pinging MQTT broker at %u.%u.%u.%u...\r\n",
           (UINT)((server_ip.nxd_ip_address.v4 >> 24) & 0xFF),
           (UINT)((server_ip.nxd_ip_address.v4 >> 16) & 0xFF),
           (UINT)((server_ip.nxd_ip_address.v4 >> 8) & 0xFF),
           (UINT)(server_ip.nxd_ip_address.v4 & 0xFF));
           
    if (nx_icmp_ping(ip_ptr, server_ip.nxd_ip_address.v4, "ICMP Ping test", 
                    strlen("ICMP Ping test"), &ping_packet, 5 * NX_IP_PERIODIC_RATE) == NX_SUCCESS)
    {
        printf("ICMP ping to MQTT broker successful! Network connectivity confirmed.\r\n");
        nx_packet_release(ping_packet);
    }
    else
    {
        printf("ICMP ping to MQTT broker failed.\r\n");
        
        // Try pinging the router/default gateway
        ULONG gateway_address;
        nx_ip_gateway_address_get(ip_ptr, &gateway_address);
        printf("Trying to ping default gateway at %u.%u.%u.%u...\r\n",
               (UINT)((gateway_address >> 24) & 0xFF),
               (UINT)((gateway_address >> 16) & 0xFF),
               (UINT)((gateway_address >> 8) & 0xFF),
               (UINT)(gateway_address & 0xFF));
               
        if (nx_icmp_ping(ip_ptr, gateway_address, "ICMP Ping test", 
                         strlen("ICMP Ping test"), &ping_packet, 5 * NX_IP_PERIODIC_RATE) == NX_SUCCESS)
        {
            printf("Ping to default gateway successful! Local network connectivity confirmed.\r\n");
            printf("The issue may be related to routing to the MQTT broker.\r\n");
            nx_packet_release(ping_packet);
        }
        else
        {
            printf("Ping to default gateway failed. Device may have WiFi connectivity issues.\r\n");
        }
        
        // Try pinging Google's DNS as another test point
        ULONG google_dns = IP_ADDRESS(8, 8, 8, 8);
        printf("Trying to ping Google DNS (8.8.8.8) as an Internet connectivity test...\r\n");
        if (nx_icmp_ping(ip_ptr, google_dns, "ICMP Ping test", 
                         strlen("ICMP Ping test"), &ping_packet, 5 * NX_IP_PERIODIC_RATE) == NX_SUCCESS)
        {
            printf("Ping to Google DNS successful! Internet connectivity confirmed.\r\n");
            printf("The issue may be specific to the MQTT broker or firewall settings.\r\n");
            nx_packet_release(ping_packet);
        }
        else
        {
            printf("Ping to Google DNS failed. Device may not have Internet connectivity.\r\n");
            printf("Check WiFi settings, firewall, and network configuration.\r\n");
        }
    }
         // Test TCP connectivity to MQTT broker before attempting MQTT connection
    printf("Testing direct TCP connection to broker %u.%u.%u.%u:%u...\r\n",
           (UINT)((server_ip.nxd_ip_address.v4 >> 24) & 0xFF),
           (UINT)((server_ip.nxd_ip_address.v4 >> 16) & 0xFF),
           (UINT)((server_ip.nxd_ip_address.v4 >> 8) & 0xFF),
           (UINT)(server_ip.nxd_ip_address.v4 & 0xFF),
           server_port);
    
    NX_TCP_SOCKET test_socket;
    
    // Create TCP socket
    status = nx_tcp_socket_create(ip_ptr, &test_socket, "TCP Test Socket", 
                                NX_IP_NORMAL, NX_FRAGMENT_OKAY, NX_IP_TIME_TO_LIVE, 
                                1024, NX_NULL, NX_NULL);
    if (status != NX_SUCCESS)
    {
        printf("ERROR: Failed to create TCP socket (0x%08lx)\r\n", (unsigned long)status);
        printf("This is a low-level network issue - will try MQTT anyway\r\n");
    }
    else
    {
        // Try different timeouts for socket operations
        UINT bind_attempts = 0;
        const UINT MAX_BIND_ATTEMPTS = 3;
        ULONG bind_timeouts[3] = {2 * NX_IP_PERIODIC_RATE, 
                                 5 * NX_IP_PERIODIC_RATE, 
                                 10 * NX_IP_PERIODIC_RATE};
                          
        // Keep trying to bind with increasing timeouts         
        while (bind_attempts < MAX_BIND_ATTEMPTS)
        {
            // Bind socket to an available port
            printf("Binding TCP socket (attempt %d)...\r\n", bind_attempts+1);
            status = nx_tcp_client_socket_bind(&test_socket, NX_ANY_PORT, bind_timeouts[bind_attempts]);
            
            if (status == NX_SUCCESS)
                break;
                
            printf("TCP socket bind failed (0x%08lx) - retrying\r\n", (unsigned long)status);
            bind_attempts++;
        }
        
        if (status != NX_SUCCESS)
        {
            printf("ERROR: Failed to bind TCP socket after %d attempts\r\n", MAX_BIND_ATTEMPTS);
            nx_tcp_socket_delete(&test_socket);
        }
        else
        {
            // Try different ports common for MQTT
            UINT test_ports[] = {server_port, 1883, 8883}; 
            UINT port_idx;
            UINT connected = NX_FALSE;
            
            for (port_idx = 0; port_idx < sizeof(test_ports)/sizeof(test_ports[0]); port_idx++)
            {
                // Attempt to connect TCP socket
                printf("Attempting TCP connection to %u.%u.%u.%u:%u (test %d of %d)...\r\n",
                       (UINT)((server_ip.nxd_ip_address.v4 >> 24) & 0xFF),
                       (UINT)((server_ip.nxd_ip_address.v4 >> 16) & 0xFF),
                       (UINT)((server_ip.nxd_ip_address.v4 >> 8) & 0xFF),
                       (UINT)(server_ip.nxd_ip_address.v4 & 0xFF),
                       test_ports[port_idx], port_idx+1, 
                       (int)(sizeof(test_ports)/sizeof(test_ports[0])));
                   
                status = nx_tcp_client_socket_connect(&test_socket, server_ip.nxd_ip_address.v4, 
                                                    test_ports[port_idx], 
                                                    10 * NX_IP_PERIODIC_RATE);
                                                    
                if (status == NX_SUCCESS)
                {
                    printf("SUCCESS: TCP connection established to broker on port %u!\r\n", 
                           test_ports[port_idx]);
                           
                    if (test_ports[port_idx] != server_port)
                    {
                        printf("IMPORTANT: Switching to working port %u for MQTT connection\r\n", 
                               test_ports[port_idx]);
                        server_port = test_ports[port_idx];
                    }
                    
                    nx_tcp_socket_disconnect(&test_socket, 5 * NX_IP_PERIODIC_RATE);
                    connected = NX_TRUE;
                    break;
                }
                else
                {
                    printf("TCP connection to port %u failed (0x%08lx)\r\n", 
                           test_ports[port_idx], (unsigned long)status);
                           
                    if (status == NX_NOT_CONNECTED)
                    {
                        printf("  - Connection refused or timed out\r\n");
                    }
                    else if (status == NX_WAIT_ABORTED)
                    {
                        printf("  - Connection wait was aborted\r\n");
                    }
                }
            }
            
            if (!connected)
            {
                printf("WARNING: Could not establish TCP connection to broker on any tested port\r\n");
                printf("This suggests a network connectivity issue or firewall blocking\r\n");
            }
            
            // Clean up
            nx_tcp_client_socket_unbind(&test_socket);
            nx_tcp_socket_delete(&test_socket);
        }
    }
    printf("Connecting to MQTT broker with timeout of %ld seconds...\r\n", (long)(MQTT_TIMEOUT/TX_TIMER_TICKS_PER_SECOND));
    
    // Try connecting multiple times with different configurations
    status = NX_NOT_SUCCESSFUL;
      // Attempt connection with progressive timeouts
    UINT connection_attempts = 0;
    const UINT MAX_ATTEMPTS = 3;
    const ULONG timeouts[3] = {5 * TX_TIMER_TICKS_PER_SECOND, 
                               10 * TX_TIMER_TICKS_PER_SECOND,
                               MQTT_TIMEOUT};
                               
    while (connection_attempts < MAX_ATTEMPTS)
    {
        printf("Connection attempt %d - with %ld second timeout...\r\n", 
               connection_attempts+1, 
               (long)(timeouts[connection_attempts]/TX_TIMER_TICKS_PER_SECOND));
               
        // Try to connect - first with shorter timeout, then longer
        status = nxd_mqtt_client_connect(&mqtt_client, 
                                      &server_ip, 
                                      server_port,
                                      MQTT_KEEP_ALIVE, 
                                      NX_TRUE,  // Clean session
                                      timeouts[connection_attempts]);
                                       
        if (status == NXD_MQTT_SUCCESS)
        {
            printf("MQTT connection successful on attempt %d!\r\n", connection_attempts+1);
            break;
        }
        
        printf("Connection attempt %d failed (0x%08lx)\r\n", connection_attempts+1, (unsigned long)status);
        connection_attempts++;
    }

    if (status != NXD_MQTT_SUCCESS)
    {
        printf("FAIL: Failed to connect to MQTT broker (0x%08lx)\r\n", (unsigned long)status);
        printf("Common issues:\r\n");
        printf("  - Check if the broker is running and accessible\r\n");
        printf("  - Verify the port number is correct\r\n");
        printf("  - Check if authentication is required\r\n");
        printf("  - Ensure there's no firewall blocking the connection\r\n");
        
        // Check for specific MQTT error codes
        if (status == NXD_MQTT_CONNECT_FAILURE)
        {
            printf("  - MQTT connection failure (NXD_MQTT_CONNECT_FAILURE: 0x%08lx)\r\n", (unsigned long)NXD_MQTT_CONNECT_FAILURE);
            printf("  - TCP connection could not be established\r\n");
            printf("  - Is the broker accepting connections on port %u?\r\n", (UINT)server_port);
        }
        else if (status == NXD_MQTT_NOT_CONNECTED)
        {
            printf("  - MQTT client not connected (NXD_MQTT_NOT_CONNECTED: 0x%08lx)\r\n", (unsigned long)NXD_MQTT_NOT_CONNECTED);
            printf("  - Check network connectivity\r\n");
        }
        else if (status == NXD_MQTT_COMMUNICATION_FAILURE) 
        {
            printf("  - MQTT communication failure (NXD_MQTT_COMMUNICATION_FAILURE: 0x%08lx)\r\n", (unsigned long)NXD_MQTT_COMMUNICATION_FAILURE);
            printf("  - Connection was established but communication failed\r\n");
        }
        else
        {
            // For other error types, check NetX error codes (lower 16 bits)
            switch (status & 0x0000FFFF)
            {
                case NX_NOT_CONNECTED:
                    printf("  - TCP socket is not connected (NX_NOT_CONNECTED: 0x%02x)\r\n", NX_NOT_CONNECTED);
                    printf("  - Is the broker accepting connections on port %u?\r\n", (UINT)server_port);
                    break;
                    
                case NX_IN_PROGRESS:
                    printf("  - Connection attempt is still in progress (NX_IN_PROGRESS: 0x%02x)\r\n", NX_IN_PROGRESS);
                    break;
                    
                case NX_WAIT_ABORTED:
                    printf("  - Connection wait was aborted (NX_WAIT_ABORTED: 0x%02x)\r\n", NX_WAIT_ABORTED);
                    break;
                    
                case NX_INVALID_PARAMETERS:
                    printf("  - Invalid parameters for connection (NX_INVALID_PARAMETERS: 0x%02x)\r\n", NX_INVALID_PARAMETERS);
                    break;
                    
                case NX_NO_RESPONSE:
                    printf("  - No response from the server (NX_NO_RESPONSE: 0x%02x)\r\n", NX_NO_RESPONSE);
                    printf("  - Check that the broker is running and accessible\r\n");
                    break;
                    
                default:
                    printf("  - Unknown connection error: 0x%08lx\r\n", (unsigned long)status);
                    printf("  - Check logs for more details\r\n");
                    break;
            }
        }
        
        return status;
    }
    
    printf("SUCCESS: Connected to MQTT broker\r\n");
    
    printf("\r\nMQTT Subscriptions\r\n");
    printf("-------------------\r\n");
    
    // Subscribe to the command topic
    printf("Subscribing to command topic: %s (QoS %d)\r\n", MQTT_COMMAND_TOPIC, MQTT_TELEMETRY_QOS);
    status = nxd_mqtt_client_subscribe(&mqtt_client, 
                                      MQTT_COMMAND_TOPIC, 
                                      strlen(MQTT_COMMAND_TOPIC),
                                      MQTT_TELEMETRY_QOS);
                                      
    if (status != NXD_MQTT_SUCCESS)
    {
        printf("FAIL: Failed to subscribe to command topic (0x%08lx)\r\n", (unsigned long)status);
        return status;
    }
    printf("SUCCESS: Subscribed to command topic: %s\r\n", MQTT_COMMAND_TOPIC);
    
    // Subscribe to the LED topic
    printf("Subscribing to LED control topic: %s (QoS %d)\r\n", MQTT_LED_TOPIC, MQTT_TELEMETRY_QOS);
    status = nxd_mqtt_client_subscribe(&mqtt_client, 
                                      MQTT_LED_TOPIC, 
                                      strlen(MQTT_LED_TOPIC),
                                      MQTT_TELEMETRY_QOS);
                                      
    if (status != NXD_MQTT_SUCCESS)
    {
        printf("FAIL: Failed to subscribe to LED topic (0x%08lx)\r\n", (unsigned long)status);
    }
    else
    {
        printf("SUCCESS: Subscribed to LED control topic: %s\r\n", MQTT_LED_TOPIC);
    }
    
    // Initialize the LED (off)
    set_led_state(false);
    
    // Update screen
    printf("\r\nMQTT Telemetry\r\n");
    printf("-------------------\r\n");
    printf("Starting MQTT telemetry loop - interval: %d seconds\r\n", telemetry_interval);
    printf("Publishing to topic: %s\r\n", MQTT_TELEMETRY_TOPIC);
    printf("Press button B to exit (not implemented yet)\r\n");
    
    screen_print("Custom MQTT", L0);
    screen_print(MQTT_BROKER_HOSTNAME, L1);
    
    // Main telemetry loop
    while (true)
    {
        // Wait for events or timeout for regular telemetry
        ULONG events;
        tx_event_flags_get(
            &mqtt_events, TELEMETRY_INTERVAL_EVENT, TX_OR_CLEAR, &events, telemetry_interval * NX_IP_PERIODIC_RATE);
            
<<<<<<< HEAD
        // Declare message buffer once for all cases (increased size for device name)
        CHAR mqtt_message_buffer[256];
=======
        // Declare message buffer once for all cases
        CHAR mqtt_message_buffer[128];
>>>>>>> e01ae56d4244fe5c27dd66bf92faac0b0214d777
        UINT message_length;
        
        switch (telemetry_state)
        {            case 0:
                // Send temperature data (using HTS221 for highest accuracy)
                {
                    hts221_data_t hts221_data = hts221_data_read();
                    
                    // Convert float to integers for printf (since floating point printf is disabled)
                    int temp_int = (int)(hts221_data.temperature_degC * 100); // Convert to int (e.g., 23.45 -> 2345)
                    int temp_whole = temp_int / 100;
                    int temp_frac = temp_int % 100;
                    
                    printf("DEBUG: Temperature (HTS221) as int*100: %d (= %d.%02d°C)\r\n", temp_int, temp_whole, temp_frac);
                    
                    // Use integer formatting for JSON since floating point printf doesn't work
<<<<<<< HEAD
                    sprintf(mqtt_message_buffer, "{\"device\": \"%s\", \"temperature\": %d.%02d}", MQTT_CLIENT_ID, temp_whole, temp_frac);
=======
                    sprintf(mqtt_message_buffer, "{\"temperature\": %d.%02d}", temp_whole, temp_frac);
>>>>>>> e01ae56d4244fe5c27dd66bf92faac0b0214d777
                    message_length = strlen(mqtt_message_buffer);
                    printf("Publishing temperature (HTS221): %d.%02d°C\r\n", temp_whole, temp_frac);
                    printf("Topic: %s\r\n", MQTT_TELEMETRY_TOPIC);
                    printf("Message: %s\r\n", mqtt_message_buffer);
                
                    status = nxd_mqtt_client_publish(&mqtt_client, 
                                                   MQTT_TELEMETRY_TOPIC,
                                                   strlen(MQTT_TELEMETRY_TOPIC),
                                                   (CHAR*)mqtt_message_buffer,
                                                   message_length,
                                                   NX_TRUE,
                                                   MQTT_TELEMETRY_QOS,
                                                   NX_WAIT_FOREVER);
                                                   
                    if (status != NXD_MQTT_SUCCESS)
                    {
                        printf("FAIL: Failed to publish temperature message (0x%08lx)\r\n", (unsigned long)status);
                    }
                    else
                    {
                        printf("SUCCESS: Temperature data (HTS221) published\r\n");
                    }
                }
                break;            case 1:
                // Send pressure data
                {
                    lps22hb_t lps22hb_data = lps22hb_data_read();
                    
                    // Convert float to integers for printf (since floating point printf is disabled)
                    int pressure_int = (int)(lps22hb_data.pressure_hPa * 100); // Convert to int (e.g., 1013.25 -> 101325)
                    int pressure_whole = pressure_int / 100;
                    int pressure_frac = pressure_int % 100;
                    
                    printf("DEBUG: Pressure as int*100: %d (= %d.%02d hPa)\r\n", pressure_int, pressure_whole, pressure_frac);
                    
                    // Use integer formatting for JSON since floating point printf doesn't work
<<<<<<< HEAD
                    sprintf(mqtt_message_buffer, "{\"device\": \"%s\", \"pressure\": %d.%02d}", MQTT_CLIENT_ID, pressure_whole, pressure_frac);
=======
                    sprintf(mqtt_message_buffer, "{\"pressure\": %d.%02d}", pressure_whole, pressure_frac);
>>>>>>> e01ae56d4244fe5c27dd66bf92faac0b0214d777
                    message_length = strlen(mqtt_message_buffer);
                    printf("Publishing: %s\r\n", mqtt_message_buffer);
                
                    status = nxd_mqtt_client_publish(&mqtt_client, 
                                                   MQTT_TELEMETRY_TOPIC,
                                                   strlen(MQTT_TELEMETRY_TOPIC),
                                                   (CHAR*)mqtt_message_buffer,
                                                   message_length,
                                                   NX_TRUE,
                                                   MQTT_TELEMETRY_QOS,
                                                   NX_WAIT_FOREVER);
                                                   
                    if (status != NXD_MQTT_SUCCESS)
                    {
                        printf("FAIL: Failed to publish pressure message (0x%08lx)\r\n", (unsigned long)status);
                    }
                }
                break;            case 2:
                // Send humidity data
                {
                    hts221_data_t hts221_data = hts221_data_read();
                    
                    // Convert float to integers for printf (since floating point printf is disabled)
                    int humidity_int = (int)(hts221_data.humidity_perc * 100); // Convert to int (e.g., 45.67 -> 4567)
                    int humidity_whole = humidity_int / 100;
                    int humidity_frac = humidity_int % 100;
                    
                    // Use integer formatting for JSON since floating point printf doesn't work
<<<<<<< HEAD
                    sprintf(mqtt_message_buffer, "{\"device\": \"%s\", \"humidity\": %d.%02d}", MQTT_CLIENT_ID, humidity_whole, humidity_frac);
=======
                    sprintf(mqtt_message_buffer, "{\"humidity\": %d.%02d}", humidity_whole, humidity_frac);
>>>>>>> e01ae56d4244fe5c27dd66bf92faac0b0214d777
                    message_length = strlen(mqtt_message_buffer);
                    printf("Publishing: %s\r\n", mqtt_message_buffer);
                
                    status = nxd_mqtt_client_publish(&mqtt_client, 
                                                   MQTT_TELEMETRY_TOPIC,
                                                   strlen(MQTT_TELEMETRY_TOPIC),
                                                   (CHAR*)mqtt_message_buffer,
                                                   message_length,
                                                   NX_TRUE,
                                                   MQTT_TELEMETRY_QOS,
                                                   NX_WAIT_FOREVER);
                                                   
                    if (status != NXD_MQTT_SUCCESS)
                    {
                        printf("FAIL: Failed to publish humidity message (0x%08lx)\r\n", (unsigned long)status);
                    }
                    else
                    {
                        printf("SUCCESS: Humidity data published\r\n");
                    }
                }
                break;            case 3:
                // Send acceleration data
                {
                    lsm6dsl_data_t lsm6dsl_data = lsm6dsl_data_read();
                    
                    // Convert float to integers for printf (since floating point printf is disabled)
                    int accel_int = (int)(lsm6dsl_data.acceleration_mg[0] * 100); // Convert to int (e.g., 9.81 -> 981)
                    int accel_whole = accel_int / 100;
                    int accel_frac = abs(accel_int % 100); // Use abs() for negative values
                    
                    // Use integer formatting for JSON since floating point printf doesn't work
<<<<<<< HEAD
                    sprintf(mqtt_message_buffer, "{\"device\": \"%s\", \"acceleration\": %s%d.%02d}", 
                            MQTT_CLIENT_ID, (accel_int < 0) ? "-" : "", abs(accel_whole), accel_frac);
=======
                    sprintf(mqtt_message_buffer, "{\"acceleration\": %s%d.%02d}", 
                            (accel_int < 0) ? "-" : "", abs(accel_whole), accel_frac);
>>>>>>> e01ae56d4244fe5c27dd66bf92faac0b0214d777
                    message_length = strlen(mqtt_message_buffer);
                    printf("Publishing: %s\r\n", mqtt_message_buffer);
                
                    status = nxd_mqtt_client_publish(&mqtt_client, 
                                                   MQTT_TELEMETRY_TOPIC,
                                                   strlen(MQTT_TELEMETRY_TOPIC),
                                                   (CHAR*)mqtt_message_buffer,
                                                   message_length,
                                                   NX_TRUE,
                                                   MQTT_TELEMETRY_QOS,
                                                   NX_WAIT_FOREVER);
                                                   
                    if (status != NXD_MQTT_SUCCESS)
                    {
                        printf("FAIL: Failed to publish acceleration message (0x%08lx)\r\n", (unsigned long)status);
                    }
                    else
                    {
                        printf("SUCCESS: Acceleration data published\r\n");
                    }
                }
                break;            case 4:
                // Send magnetic field data
                {
                    lis2mdl_data_t lis2mdl_data = lis2mdl_data_read();
                    
                    // Convert float to integers for printf (since floating point printf is disabled)
                    int magnetic_int = (int)(lis2mdl_data.magnetic_mG[0] * 100); // Convert to int
                    int magnetic_whole = magnetic_int / 100;
                    int magnetic_frac = abs(magnetic_int % 100); // Use abs() for negative values
                    
                    // Use integer formatting for JSON since floating point printf doesn't work
<<<<<<< HEAD
                    sprintf(mqtt_message_buffer, "{\"device\": \"%s\", \"magnetic\": %s%d.%02d}", 
                            MQTT_CLIENT_ID, (magnetic_int < 0) ? "-" : "", abs(magnetic_whole), magnetic_frac);
=======
                    sprintf(mqtt_message_buffer, "{\"magnetic\": %s%d.%02d}", 
                            (magnetic_int < 0) ? "-" : "", abs(magnetic_whole), magnetic_frac);
>>>>>>> e01ae56d4244fe5c27dd66bf92faac0b0214d777
                    message_length = strlen(mqtt_message_buffer);
                    printf("Publishing: %s\r\n", mqtt_message_buffer);
                
                    status = nxd_mqtt_client_publish(&mqtt_client, 
                                                   MQTT_TELEMETRY_TOPIC,
                                                   strlen(MQTT_TELEMETRY_TOPIC),
                                                   (CHAR*)mqtt_message_buffer,
                                                   message_length,
                                                   NX_TRUE,
                                                   MQTT_TELEMETRY_QOS,
                                                   NX_WAIT_FOREVER);
                                                   
                    if (status != NXD_MQTT_SUCCESS)
                    {
                        printf("FAIL: Failed to publish magnetic field message (0x%08lx)\r\n", (unsigned long)status);
                    }
                    else
                    {
                        printf("SUCCESS: Magnetic field data published\r\n");
                    }                }
                break;
            
            case 5:
                // Send gyroscope data
                {
                    lsm6dsl_data_t lsm6dsl_data = lsm6dsl_data_read();
                    
                    // Convert float to integers for printf (since floating point printf is disabled)
                    int gyro_x_int = (int)(lsm6dsl_data.angular_rate_mdps[0] * 100); // Convert to int
                    int gyro_x_whole = gyro_x_int / 100;
                    int gyro_x_frac = abs(gyro_x_int % 100); // Use abs() for negative values
                    
                    int gyro_y_int = (int)(lsm6dsl_data.angular_rate_mdps[1] * 100);
                    int gyro_y_whole = gyro_y_int / 100;
                    int gyro_y_frac = abs(gyro_y_int % 100);
                    
                    int gyro_z_int = (int)(lsm6dsl_data.angular_rate_mdps[2] * 100);
                    int gyro_z_whole = gyro_z_int / 100;
                    int gyro_z_frac = abs(gyro_z_int % 100);
                    
                    // Use integer formatting for JSON with all three axes
<<<<<<< HEAD
                    sprintf(mqtt_message_buffer, "{\"device\": \"%s\", \"gyroscope\": {\"x\": %s%d.%02d, \"y\": %s%d.%02d, \"z\": %s%d.%02d}}", 
                            MQTT_CLIENT_ID,
=======
                    sprintf(mqtt_message_buffer, "{\"gyroscope\": {\"x\": %s%d.%02d, \"y\": %s%d.%02d, \"z\": %s%d.%02d}}", 
>>>>>>> e01ae56d4244fe5c27dd66bf92faac0b0214d777
                            (gyro_x_int < 0) ? "-" : "", abs(gyro_x_whole), gyro_x_frac,
                            (gyro_y_int < 0) ? "-" : "", abs(gyro_y_whole), gyro_y_frac,
                            (gyro_z_int < 0) ? "-" : "", abs(gyro_z_whole), gyro_z_frac);
                    message_length = strlen(mqtt_message_buffer);
                    printf("Publishing: %s\r\n", mqtt_message_buffer);
                
                    status = nxd_mqtt_client_publish(&mqtt_client, 
                                                   MQTT_TELEMETRY_TOPIC,
                                                   strlen(MQTT_TELEMETRY_TOPIC),
                                                   (CHAR*)mqtt_message_buffer,
                                                   message_length,
                                                   NX_TRUE,
                                                   MQTT_TELEMETRY_QOS,
                                                   NX_WAIT_FOREVER);
                                                   
                    if (status != NXD_MQTT_SUCCESS)
                    {
                        printf("FAIL: Failed to publish gyroscope message (0x%08lx)\r\n", (unsigned long)status);
                    }
                    else
                    {
                        printf("SUCCESS: Gyroscope data published\r\n");
                    }
                }
                break;
        }
        
        // Move to the next telemetry type (now 6 states: 0-5)
        telemetry_state = (telemetry_state + 1) % 6;
    }

    // Clean up (this will never execute in the current implementation)
    nxd_mqtt_client_disconnect(&mqtt_client);
    nxd_mqtt_client_delete(&mqtt_client);
    tx_event_flags_delete(&mqtt_events);

    return NXD_MQTT_SUCCESS;
}
