/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

#ifndef _NX_CLIENT_H
#define _NX_CLIENT_H

#include "nx_api.h"
#include "nxd_dns.h"
#include "tx_api.h"

// Dummy struct for Azure IoT context to maintain compatibility with existing code
typedef struct AZURE_IOT_NX_CONTEXT_STRUCT
{
    UINT dummy;  // Placeholder to avoid empty struct
} AZURE_IOT_NX_CONTEXT;

// Stub declarations for Azure IoT client functions
UINT azure_iot_nx_client_entry(
    NX_IP* ip_ptr, NX_PACKET_POOL* pool_ptr, NX_DNS* dns_ptr, UINT (*unix_time_callback)(ULONG* unix_time));

UINT azure_iot_nx_client_create(AZURE_IOT_NX_CONTEXT* context,
    NX_IP* ip_ptr,
    NX_PACKET_POOL* pool_ptr,
    NX_DNS* dns_ptr,
    UINT (*unix_time_callback)(ULONG* unix_time),
    CHAR* iot_hub_hostname,
    CHAR* iot_hub_device_id,
    CHAR* iot_hub_module_id,
    CHAR* iot_hub_device_key,
    ULONG iot_hub_device_key_length,
    CHAR* iot_model_id,
    UINT iot_model_id_length);

UINT azure_iot_nx_client_delete(AZURE_IOT_NX_CONTEXT* context);

UINT azure_iot_nx_client_connect(AZURE_IOT_NX_CONTEXT* context);

UINT azure_iot_nx_client_disconnect(AZURE_IOT_NX_CONTEXT* context);

UINT azure_iot_nx_client_publish_telemetry(AZURE_IOT_NX_CONTEXT* context, CHAR* component_name, UINT component_name_len,
    CHAR* data, UINT data_len, UINT wait_option);

UINT azure_iot_nx_client_publish_properties(
    AZURE_IOT_NX_CONTEXT* context, CHAR* data, UINT data_len, UINT wait_option);

UINT azure_iot_nx_client_register_command_callback(AZURE_IOT_NX_CONTEXT* context,
    const CHAR* command_name,
    UINT command_name_length,
    VOID (*callback)(AZURE_IOT_NX_CONTEXT*, const CHAR*, UINT, UCHAR*, UINT, VOID*, UINT, UCHAR*, UINT*, UINT*),
    VOID* callback_data);

UINT azure_iot_nx_client_register_properties_complete_callback(AZURE_IOT_NX_CONTEXT* context,
    VOID (*properties_complete_callback)(AZURE_IOT_NX_CONTEXT*,
        UINT,
        UINT,
        VOID*,
        UINT,
        UCHAR*,
        UINT),
    VOID* callback_data);

UINT azure_iot_nx_client_register_direct_method_callback(AZURE_IOT_NX_CONTEXT* context,
    VOID (*direct_method_callback)(AZURE_IOT_NX_CONTEXT*,
        const UCHAR*,
        UINT,
        UCHAR*,
        UINT,
        VOID*,
        UINT,
        UCHAR**,
        UINT*,
        UINT*),
    VOID* callback_data);

UINT azure_iot_nx_client_register_writable_properties_callback(AZURE_IOT_NX_CONTEXT* context,
    VOID (*writable_properties_callback)(AZURE_IOT_NX_CONTEXT*,
        const UCHAR*,
        UINT,
        UCHAR*,
        UINT,
        VOID*,
        UINT),
    VOID* callback_data);

UINT azure_iot_nx_client_add_component(AZURE_IOT_NX_CONTEXT* context, CHAR* component_name, UINT component_name_length);

#endif // _NX_CLIENT_H