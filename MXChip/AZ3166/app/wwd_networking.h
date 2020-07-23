/* Copyright (c) Microsoft Corporation.
   Licensed under the MIT License. */

#ifndef _NETWORKING_H
#define _NETWORKING_H

#include "nx_api.h"
#include "nxd_dns.h"

#include "azure_config.h"

// Country codes as defined in wwd_constants.h, doesn't need to change it.
#define WIFI_COUNTRY        WICED_COUNTRY_CHINA

/* Define the prototypes for ThreadX.  */
extern NX_PACKET_POOL                   nx_pool[2]; /* 0=TX, 1=RX. */
extern NX_IP                            nx_ip;
extern NX_DNS                           nx_dns_client;

int platform_init(CHAR *ssid, CHAR *password, wiced_security_t security, wiced_country_code_t country);

#endif // _NETWORKING_H