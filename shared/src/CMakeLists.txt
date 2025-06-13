# Copyright (c) Microsoft Corporation.
# Licensed under the MIT License.

set(TARGET app_common)

set(SOURCES
    sntp_client.c
)

# Only include Azure IoT related sources if Azure IoT is enabled
if(NXD_ENABLE_AZURE_IOT)
    list(APPEND SOURCES
        azure_iot_mqtt/azure_iot_mqtt.c
        azure_iot_mqtt/azure_iot_dps_mqtt.c
        azure_iot_mqtt/hmac_sha256.c
        azure_iot_mqtt/sas_token.c
        azure_iot_mqtt/sha256.c
        azure_iot_mqtt/json_utils.c

        azure_iot_nx_client.c
        azure_iot_connect.c
        azure_iot_cert.c
        azure_iot_ciphersuites.c
    )
endif()

# Allow to disable the common networking component
if(NOT DEFINED DISABLE_COMMON_NETWORK) 
    list(APPEND SOURCES
        networking.c
    )
endif()

# Allow to disable the newlib stubbing
if(NOT DEFINED DISABLE_NEWLIB_STUB) 
    list(APPEND SOURCES
        newlib_nano.c
    )
endif()

add_library(${TARGET} OBJECT
    ${SOURCES}
)

target_include_directories(${TARGET}
    PUBLIC
        .
)

# Include Azure IoT MQTT directories only if Azure IoT is enabled
if(NXD_ENABLE_AZURE_IOT)
    target_include_directories(${TARGET}
        PUBLIC
            azure_iot_mqtt
    )
endif()

target_link_libraries(${TARGET}
    azrtos::threadx
    azrtos::netxduo
    jsmn
)