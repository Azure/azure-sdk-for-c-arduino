// Copyright (c) Microsoft Corporation. All rights reserved.
// SPDX-License-Identifier: MIT

#ifndef AZURE_IOT_H
#define AZURE_IOT_H

#include <stdlib.h>
#include <stdint.h>
#include <time.h>

#include <az_core.h>
#include <az_iot.h>

/* --- Array and String Helpers --- */
#define lengthof(s) (sizeof(s) - 1)
#define sizeofarray(a) (sizeof(a) / sizeof(a[0]))
#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)
static const uint8_t null_terminator = '\0';

/* --- Time --- */
#define INDEFINITE_TIME ((time_t)-1)

/* --- Logging --- */
#ifndef DISABLE_LOGGING
typedef enum log_level_t_enum
{
  log_level_info,
  log_level_error
}
log_level_t;

typedef void (*log_function_t)(log_level_t log_level, char const* const format, ...);

extern log_function_t default_logging_function;

#define set_logging_function(custom_logging_function) \
  default_logging_function = custom_logging_function;

#define Log(level, message, ...) default_logging_function(level, message, ##__VA_ARGS__ )
#define LogInfo(message, ...) Log(log_level_info, message, ##__VA_ARGS__ )
#define LogError(message, ...) Log(log_level_error, message, ##__VA_ARGS__ )
#else
#define set_logging_function(custom_logging_function) 
#define Log(level, message, ...) 
#define LogInfo(message, ...)
#define LogError(message, ...)
#endif // DISABLE_LOGGING


/* --- Azure Abstraction --- */
#define DPS_GLOBAL_ENDPOINT_FQDN          "global.azure-devices-provisioning.net"
#define DPS_GLOBAL_ENDPOINT_PORT          8883
#define IOT_HUB_ENDPOINT_PORT             8883

#define DEFAULT_SAS_TOKEN_LIFETIME_IN_MINUTES 60
#define SAS_TOKEN_REFRESH_THRESHOLD_SECS 30 

#define MQTT_QOS_AT_MOST_ONCE  0
#define MQTT_QOS_AT_LEAST_ONCE 1
#define MQTT_QOS_EXACTLY_ONCE  2
#define MQTT_DO_NOT_RETAIN_MSG 0
#define MQTT_NULL_PAYLOAD      NULL
#define MQTT_NULL_PAYLOAD_SIZE 0

typedef enum mqtt_qos_t_enum
{
  mqtt_qos_at_most_once = MQTT_QOS_AT_MOST_ONCE,
  mqtt_qos_at_least_once = MQTT_QOS_AT_LEAST_ONCE,
  mqtt_qos_exactly_once = MQTT_QOS_EXACTLY_ONCE
}
mqtt_qos_t;

typedef struct mqtt_message_t_struct
{
  az_span topic;
  az_span payload;
  mqtt_qos_t qos;
}
mqtt_message_t;


typedef struct mqtt_client_config_t_struct
{
  az_span address;
  int port;
  az_span client_id;
  az_span username;
  az_span password;
    
  // certs...
}
mqtt_client_config_t;

typedef void* mqtt_client_handle_t;

/*
 * @brief           Function to initialize and connect a MQTT client.
 * @remark          When this function is called, it provides the information necessary to initialize a 
 *                  specific MQTT client (whichever is used by the user application). In this callback
 *                  it is expected that the MQTT client will be created/initialized, started and that it
 *                  start sending a CONNECT to the provided server.
 * @param[in]       mqtt_client_config    An instance of mqtt_client_config_t containing all the information
 *                                        needed by the MQTT client to connect to the target server.
 *                                        Please see `mqtt_client_config_t` documentation for details.
 * @param[out]      mqtt_client_handle    A pointer to the resulting "instance" of the MQTT client shall
 *                                        be saved in `mqtt_client_handle` for later use by the calling layer.
 * 
 * @return int      0 on success, non-zero if any failure occurs.
 */
typedef int (*mqtt_client_init_function_t)(mqtt_client_config_t* mqtt_client_config, mqtt_client_handle_t* mqtt_client_handle);

/*
 * @brief           Function to disconnect and deinitialize a MQTT client.
 * @remark          When this function is called the MQTT client instance referenced by `mqtt_client_handle` shall disconnect
 *                  from the server and any functions of the MQTT client API that destroy the instance shall be called
 *                  (so any allocated memory resources can be released).
 * @param[in]       mqtt_client_handle    A pointer to the instance of the MQTT client previously created with
 *                                        `mqtt_client_init_function_t` function.
 * 
 * @return int      0 on success, non-zero if any failure occurs.
 *                  Returning non-zero results in the Azure IoT Client going into error state.
 */
typedef int (*mqtt_client_deinit_function_t)(mqtt_client_handle_t mqtt_client_handle);

/*
 * @brief           Function to send an MQTT PUBLISH.
 * @remark          When this function is invoked, the caller expects the actual MQTT client (referenced by `mqtt_client_handle`) 
 *                  to invoke the appropriate function in the MQTT client API to publish an MQTT message.
 * @param[in]       mqtt_client_handle    A pointer to the instance of the MQTT client previously created with
 *                                        `mqtt_client_init_function_t` function.
 * @param[in]       mqtt_message          A structure containing the topic name, payload and QoS to be used to publish
 *                                        an actual MQTT message.
 * 
 * @return int      The packet ID on success, or NEGATIVE if any failure occurs.
 *                  If the QoS in `mqtt_message` is:
 *                  - AT LEAST ONCE, the Azure IoT client expects `azure_iot_mqtt_client_connected` to be called once 
 *                    the MQTT client receives a PUBACK.
 *                  - AT MOST ONCE, there should be no PUBACK, so no further action is needed for this PUBLISH.
 */
typedef int (*mqtt_client_publish_function_t)(mqtt_client_handle_t mqtt_client_handle, mqtt_message_t* mqtt_message);

/*
 * @brief           Function to send an MQTT SUBSCRIBE.
 * @remark          When this function is invoked, Azure IoT client expects the actual MQTT client (referenced by
 *                  `mqtt_client_handle`) to invoke the appropriate function in the MQTT client API to subscribe to 
 *                  an MQTT topic.
 * @param[in]       topic                 The string containing the complete MQTT topic name to subscribed to.
 *                                        This string is always NULL-terminated.
 * @param[in]       topic_length          The length of `topic`. It can be used in case the MQTT client takes a 
 *                                        topic name as a non-NULL-terminated string.
 * @param[in]       qos                   MQTT QoS to be used for the topic subscription.
 * 
 * @return int      The packet ID of the subscription on success, or NEGATIVE if any failure occurs.
 *                  Azure IoT client expects `azure_iot_mqtt_client_subscribe_completed` to be called once the
 *                  MQTT client receives a SUBACK.
 */
typedef int (*mqtt_client_subscribe_function_t)(mqtt_client_handle_t mqtt_client_handle, const uint8_t* topic, size_t topic_lenght, mqtt_qos_t qos);

typedef struct mqtt_client_interface_t_struct
{
  mqtt_client_init_function_t mqtt_client_init;
  mqtt_client_deinit_function_t mqtt_client_deinit;
  mqtt_client_subscribe_function_t mqtt_client_subscribe;
  mqtt_client_publish_function_t mqtt_client_publish;
}
mqtt_client_interface_t;

typedef int (*base64_decode_function_t)(uint8_t* data, size_t data_length, uint8_t* decoded, size_t decoded_size, size_t* decoded_length);
typedef int (*base64_encode_function_t)(uint8_t* data, size_t data_length, uint8_t* encoded, size_t encoded_size, size_t* encoded_length);
typedef int (*hmac_sha512_encryption_function_t)(const uint8_t* key, size_t key_length, const uint8_t* payload, size_t payload_length, uint8_t* encrypted_payload, size_t encrypted_payload_size);

/*
 * @brief Defines the callback for notifying the completion of a reported properties update.
 * 
 * @param[in] request_id    Request ID provided by the caller when sending the reported properties update.
 * @param[in] status_code   Result of the reported properties update (uses HTTP status code semantics).
 *
 * @return                  Nothing
 */
typedef void (*properties_update_completed_t)(uint32_t request_id, az_iot_status status_code);

typedef struct data_manipulation_functions_t_struct
{
  base64_decode_function_t base64_decode;
  base64_encode_function_t base64_encode;
  hmac_sha512_encryption_function_t hmac_sha512_encrypt;
}
data_manipulation_functions_t;

typedef enum azure_iot_status_t_struct
{
  azure_iot_disconnected,
  azure_iot_connecting,
  azure_iot_connected,
  azure_iot_error
}
azure_iot_status_t;

typedef enum azure_iot_client_state_t_struct
{
  azure_iot_state_not_initialized = 0,
  azure_iot_state_initialized,
  azure_iot_state_started,
  azure_iot_state_connecting_to_dps,
  azure_iot_state_connected_to_dps,
  azure_iot_state_subscribing_to_dps,
  azure_iot_state_subscribed_to_dps,
  azure_iot_state_provisioning_querying,
  azure_iot_state_provisioning_waiting,
  azure_iot_state_provisioned,
  azure_iot_state_connecting_to_hub,
  azure_iot_state_connected_to_hub,
  azure_iot_state_subscribing_to_pnp_cmds,
  azure_iot_state_subscribed_to_pnp_cmds,
  azure_iot_state_subscribing_to_pnp_props,
  azure_iot_state_subscribed_to_pnp_props,
  azure_iot_state_subscribing_to_pnp_writable_props,
  azure_iot_state_ready,
  azure_iot_state_refreshing_sas,
  azure_iot_state_error
}
azure_iot_client_state_t;

typedef struct azure_iot_config_t_struct
{
  az_span user_agent;

  bool use_device_provisioning;
  // bool use_symetric_key_authentication;

  az_span iot_hub_fqdn;
  az_span device_id;
  az_span device_key;

  az_span dps_registration_id;
  az_span dps_id_scope;

  az_span model_id;

  // TODO: document that needs enough for sas doc, sas token
  az_span data_buffer;

  mqtt_client_interface_t mqtt_client_interface;
  data_manipulation_functions_t data_manipulation_functions;

  uint32_t sas_token_lifetime_in_minutes = DEFAULT_SAS_TOKEN_LIFETIME_IN_MINUTES;

  properties_update_completed_t on_properties_update_completed;
}
azure_iot_config_t;

typedef struct azure_iot_t_struct
{
  azure_iot_config_t* config;
  az_span data_buffer;
  mqtt_client_handle_t mqtt_client_handle;
  az_iot_hub_client iot_hub_client;
  az_iot_hub_client_options iot_hub_client_options;
  az_iot_provisioning_client dps_client;
  azure_iot_client_state_t state;
  uint32_t sas_token_expiration_time;
  uint32_t dps_retry_after_seconds;
  uint32_t dps_last_query_time;
  az_span dps_operation_id;
}
azure_iot_t;

int azure_iot_init(azure_iot_t* azure_iot, azure_iot_config_t* iot_config);
int azure_iot_start(azure_iot_t* azure_iot);
int azure_iot_stop(azure_iot_t* azure_iot);
azure_iot_status_t azure_iot_get_status(azure_iot_t* azure_iot);
void azure_iot_do_work(azure_iot_t* azure_iot);
int azure_iot_send_telemetry(azure_iot_t* azure_iot, const uint8_t* message, size_t length);

/**
 * @brief Checks whether `span` is equal AZ_SPAN_EMPTY.
 *
 * @param[in]    azure_iot                         The pointer to the azure_iot_t instance that holds the state of the Azure IoT client.
 * @param[in]    message                           The message with the reported properties update
 *                                                 (a JSON document formatted according to the DTDL specification).
 *                                                 `message` gets passed as-is to the MQTT client publish function as the payload, so if 
 *                                                 your MQTT client expects a null-terminated string for payload, make sure `message` is
 *                                                 a null-terminated string.
 * @param[in]    length                            The length of `message`.
 * @param[in]    request_id                        An unique identification number to correlate the response with when
 *                                                 `on_properties_update_completed` (set in azure_iot_config_t) is invoked.
 *
 * @return       int                               0 if the function succeeds, or non-zero if any failure occurs.
 */
int azure_iot_send_properties_update(azure_iot_t* azure_iot, uint32_t request_id, const uint8_t* message, size_t length);

int azure_iot_mqtt_client_connected(azure_iot_t* azure_iot);
int azure_iot_mqtt_client_disconnected(azure_iot_t* azure_iot);
int azure_iot_mqtt_client_subscribe_completed(azure_iot_t* azure_iot, int packet_id);
int azure_iot_mqtt_client_publish_completed(azure_iot_t* azure_iot, int packet_id);
int azure_iot_mqtt_client_message_received(azure_iot_t* azure_iot, mqtt_message_t* mqtt_message);

/* --- az_core extensions --- */

/**
 * @brief Checks whether `span` is equal AZ_SPAN_EMPTY.
 *
 * @param[in]    span           A span to be verified.
 *
 * @return       boolean        True if `span`'s pointer and size are equal to AZ_SPAN_EMPTY, or false otherwise.
 */
#define az_span_is_empty(span) az_span_is_content_equal(span, AZ_SPAN_EMPTY)

/**
 * @brief Slices `span` at position `size`, returns the first slice and assigns the second slice to `remainder`.
 *
 * @param[in]    span           A span to be sliced.
 * @param[in]    source         The span to copy the contents from.
 * @param[out]   remainder      The pointer where to store the remainder of `span` after it is sliced.
 *
 * @return       az_span        A slice of `span` from position zero to `size`.
 */
az_span az_span_split(az_span span, int32_t size, az_span* remainder);

/**
 * @brief Slices `destination` to fit `source`, copy `source` into the first slice and returns the second through `remainder`.
 *
 * @param[in]    destination    A span large enough to contain the copy of the contents of `source`.
 * @param[in]    source         The span to copy the contents from.
 * @param[out]   remainder      The pointer where to store the remainder of `destination` after `source` is copied.
 *
 * @return       az_span        A slice of `destination` with the same size as `source`, with `source`'s content copied over.
 */
static az_span az_span_slice_and_copy(az_span destination, az_span source, az_span* remainder);

#endif // AZURE_IOT_H
