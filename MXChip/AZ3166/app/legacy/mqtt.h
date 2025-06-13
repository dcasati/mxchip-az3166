/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

#ifndef _MQTT_H
#define _MQTT_H

#include "tx_api.h"
#include "nx_api.h"
#include "nxd_dns.h"
#include "nxd_mqtt_client.h"

/**
 * @brief Custom MQTT client entry point
 * 
 * @param ip_ptr Pointer to the IP instance
 * @param pool_ptr Pointer to the packet pool
 * @param dns_ptr Pointer to the DNS instance
 * @param sntp_time_get Function pointer to get the current time
 * @return UINT Status code
 */
UINT azure_iot_mqtt_entry(NX_IP* ip_ptr, NX_PACKET_POOL* pool_ptr, NX_DNS* dns_ptr, ULONG (*sntp_time_get)(VOID));

#endif // _MQTT_H
