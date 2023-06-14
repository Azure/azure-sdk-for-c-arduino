// Copyright (c) Microsoft Corporation. All rights reserved.
// SPDX-License-Identifier: MIT

/*
 * AzureIoT.cpp contains a state machine that implements the necessary calls to azure-sdk-for-c
 * for aiding to connect and work with Azure IoT services, plus abstractions to
 * simplify overall use of azure-sdk-for-c.
 * Besides the basic configuration needed to access the Azure IoT services,
 * all that is needed is to provide the functions required by that layer to:
 * - Interact with your MQTT client,
 * - Perform data manipulations (HMAC SHA256 encryption, Base64 decoding and encoding),
 * - Receive the callbacks for Plug and Play properties and commands.
 */

#ifndef AZURE_IOT_H
#define AZURE_IOT_H

#include <stdint.h>
#include <stdlib.h>
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
} log_level_t;

typedef void (*log_function_t)(log_level_t log_level, char const* const format, ...);

extern log_function_t default_logging_function;

#define set_logging_function(custom_logging_function) \
  default_logging_function = custom_logging_function;

#define Log(level, message, ...) default_logging_function(level, message, ##__VA_ARGS__)
#define LogInfo(message, ...) Log(log_level_info, message, ##__VA_ARGS__)
#define LogError(message, ...) Log(log_level_error, message, ##__VA_ARGS__)
#else
#define set_logging_function(custom_logging_function)
#define Log(level, message, ...)
#define LogInfo(message, ...)
#define LogError(message, ...)
#endif // DISABLE_LOGGING

/* --- Azure Definitions --- */
#define DPS_GLOBAL_ENDPOINT_FQDN "global.azure-devices-provisioning.net"
#define DPS_GLOBAL_ENDPOINT_PORT AZ_IOT_DEFAULT_MQTT_CONNECT_PORT
#define IOT_HUB_ENDPOINT_PORT AZ_IOT_DEFAULT_MQTT_CONNECT_PORT

#define DEFAULT_SAS_TOKEN_LIFETIME_IN_MINUTES 60
#define SAS_TOKEN_REFRESH_THRESHOLD_IN_SECS 30

/*
 * The structures below define a generic interface to abstract the interaction of this module,
 * with any MQTT client used in the user application.
 */

#define MQTT_QOS_AT_MOST_ONCE 0
#define MQTT_QOS_AT_LEAST_ONCE 1
#define MQTT_QOS_EXACTLY_ONCE 2

typedef enum mqtt_qos_t_enum
{
  mqtt_qos_at_most_once = MQTT_QOS_AT_MOST_ONCE,
  mqtt_qos_at_least_once = MQTT_QOS_AT_LEAST_ONCE,
  mqtt_qos_exactly_once = MQTT_QOS_EXACTLY_ONCE
} mqtt_qos_t;

/*
 * @brief     Defines a generic MQTT message to be exchanged between the AzureIoT layer
 *            and the user application.
 * @remark    It uses azure-sdk-for-c's az_span construct, which is merely a structure that
 *            has a pointer to a buffer and its size. Messages without a payload will have
 *            the `payload` member set to AZ_SPAN_EMPTY.
 *            Please see the az_span.h in azure-sdk-for-c for more details at
 *            https://azuresdkdocs.blob.core.windows.net/$web/c/az_core/1.2.0/az__span_8h.html
 */
typedef struct mqtt_message_t_struct
{
  az_span topic;
  az_span payload;
  mqtt_qos_t qos;
} mqtt_message_t;

/*
 * @brief    Configuration structure passed by `mqtt_client_init_function_t` to the user
 *           application for initializing the actual MQTT client.
 */
typedef struct mqtt_client_config_t_struct
{
  /*
   * @brief    FQDN address of the broker that the MQTT client shall connect to.
   */
  az_span address;

  /*
   * @brief    Port of the broker that the MQTT client shall connect to.
   */
  int port;

  /*
   * @brief    Client ID to be provided in the CONNECT sent by the MQTT client.
   */
  az_span client_id;

  /*
   * @brief    Username to be provided in the CONNECT sent by the MQTT client.
   */
  az_span username;

  /*
   * @brief    Password to be provided in the CONNECT sent by the MQTT client.
   */
  az_span password;
} mqtt_client_config_t;

/*
 * @brief    Generic pointer to the actual instance of the application MQTT client.
 * @remark   set by the user application when the `mqtt_client_init_function_t` callback is invoked.
 */
typedef void* mqtt_client_handle_t;

/*
 * @brief         Function to initialize and connect an MQTT client.
 * @remark        When this function is called, it provides the information necessary to initialize
 * a specific MQTT client (whichever is used by the user application). In this callback it is
 * expected that the MQTT client will be created/initialized, started and that it start sending a
 * CONNECT to the provided server.
 *
 * @param[in]     mqtt_client_config    An instance of mqtt_client_config_t containing all the
 * information needed by the MQTT client to connect to the target server. Please see
 * `mqtt_client_config_t` documentation for details.
 * @param[out]    mqtt_client_handle    A pointer to the resulting "instance" of the MQTT client
 * shall be saved in `mqtt_client_handle` for later use by the calling layer.
 *
 * @return        int                   0 on success, non-zero if any failure occurs.
 */
typedef int (*mqtt_client_init_function_t)(
    mqtt_client_config_t* mqtt_client_config,
    mqtt_client_handle_t* mqtt_client_handle);

/*
 * @brief        Function to disconnect and deinitialize an MQTT client.
 * @remark       When this function is called the MQTT client instance referenced by
 * `mqtt_client_handle` shall disconnect from the server and any functions of the MQTT client API
 * that destroy the instance shall be called (so any allocated memory resources can be released).
 *
 * @param[in]    mqtt_client_handle    A pointer to the instance of the MQTT client previously
 * created with `mqtt_client_init_function_t` function.
 *
 * @return       int                   0 on success, non-zero if any failure occurs.
 *                                     Returning non-zero results in the Azure IoT Client going into
 * error state.
 */
typedef int (*mqtt_client_deinit_function_t)(mqtt_client_handle_t mqtt_client_handle);

/*
 * @brief        Function to send an MQTT PUBLISH.
 * @remark       When this function is invoked, the caller expects the actual MQTT client
 * (referenced by `mqtt_client_handle`) to invoke the appropriate function in the MQTT client API to
 * publish an MQTT message.
 *
 * @param[in]    mqtt_client_handle    A pointer to the instance of the MQTT client previously
 * created with `mqtt_client_init_function_t` function.
 * @param[in]    mqtt_message          A structure containing the topic name, payload and QoS to be
 * used to publish an actual MQTT message.
 *
 * @return       int                   The packet ID on success, or NEGATIVE if any failure occurs.
 *                                     If the QoS in `mqtt_message` is:
 *                                     - AT LEAST ONCE, the Azure IoT client expects
 * `azure_iot_mqtt_client_connected` to be called once the MQTT client receives a PUBACK.
 *                                     - AT MOST ONCE, there should be no PUBACK, so no further
 * action is needed for this PUBLISH.
 */
typedef int (*mqtt_client_publish_function_t)(
    mqtt_client_handle_t mqtt_client_handle,
    mqtt_message_t* mqtt_message);

/*
 * @brief        Function to send an MQTT SUBSCRIBE.
 * @remark       When this function is invoked, Azure IoT client expects the actual MQTT client
 * (referenced by `mqtt_client_handle`) to invoke the appropriate function in the MQTT client API to
 * subscribe to an MQTT topic.
 *
 * @param[in]    topic           The az_span with the string containing the complete MQTT topic name
 * to subscribed to. This string is always NULL-terminated.
 * @param[in]    qos             MQTT QoS to be used for the topic subscription.
 *
 *
 * @return       int             The packet ID of the subscription on success, or NEGATIVE if any
 * failure occurs. Azure IoT client expects `azure_iot_mqtt_client_subscribe_completed` to be called
 * once the MQTT client receives a SUBACK.
 */
typedef int (*mqtt_client_subscribe_function_t)(
    mqtt_client_handle_t mqtt_client_handle,
    az_span topic,
    mqtt_qos_t qos);

/*
 * @brief    Structure that consolidates all the abstracted MQTT functions.
 */
typedef struct mqtt_client_interface_t_struct
{
  mqtt_client_init_function_t mqtt_client_init;
  mqtt_client_deinit_function_t mqtt_client_deinit;
  mqtt_client_publish_function_t mqtt_client_publish;
  mqtt_client_subscribe_function_t mqtt_client_subscribe;
} mqtt_client_interface_t;

/*
 * @brief    This function must be provided by the user for the AzureIoT layer
 *           to perform the generation of the SAS tokens used as MQTT passwords.
 *
 * @param[in]     data              Buffer containing the Base64-encoded content.
 * @param[in]     data_length       Length of `data`.
 * @param[in]     decoded           Buffer where to write the Base64-decoded content of `data`.
 * @param[in]     decoded_size      Size of `decoded`.
 * @param[out]    decoded_length    The final length of the Base64-decoded content written in
 *                                  the `decoded` buffer.
 *
 * @return        int               0 on success, or non-zero if any failure occurs.
 */
typedef int (*base64_decode_function_t)(
    uint8_t* data,
    size_t data_length,
    uint8_t* decoded,
    size_t decoded_size,
    size_t* decoded_length);

/*
 * @brief         This function must be provided by the user for the AzureIoT layer
 *                to perform the generation of the SAS tokens used as MQTT passwords.
 *
 * @param[in]     data              Buffer containing the Base64-decoded content.
 * @param[in]     data_length       Length of `data`.
 * @param[in]     encoded           Buffer where to write the Base64-encoded content of `data`.
 * @param[in]     encoded_size      Size of `encoded`.
 * @param[out]    encoded_length    The final length of the Base64-encoded content written in
 *                                  the `encoded` buffer.
 *
 * @return        int               0 on success, or non-zero if any failure occurs.
 */
typedef int (*base64_encode_function_t)(
    uint8_t* data,
    size_t data_length,
    uint8_t* encoded,
    size_t encoded_size,
    size_t* encoded_length);

/*
 * @brief        This function must be provided by the user for the AzureIoT layer
 *               to perform the generation of the SAS tokens used as MQTT passwords.
 *
 * @param[in]    key                       Encryption key to be used in the HMAC-SHA256 algorithm.
 * @param[in]    key_length                Length of `key`.
 * @param[in]    payload                   Buffer containing the data to be encrypted.
 * @param[in]    payload_size              Size of `payload`.
 * @param[in]    encrypted_payload         Buffer where to write the HMAC-SHA256 encrypted content
 * of `payload`.
 * @param[in]    encrypted_payload_size    The size of the `encrypted_payload` buffer.
 *
 * @return       int                       0 on success, or non-zero if any failure occurs.
 */
typedef int (*hmac_sha256_encryption_function_t)(
    const uint8_t* key,
    size_t key_length,
    const uint8_t* payload,
    size_t payload_length,
    uint8_t* encrypted_payload,
    size_t encrypted_payload_size);

/*
 * @brief    Structure that consolidates all the data manipulation functions.
 */
typedef struct data_manipulation_functions_t_struct
{
  base64_decode_function_t base64_decode;
  base64_encode_function_t base64_encode;
  hmac_sha256_encryption_function_t hmac_sha256_encrypt;
} data_manipulation_functions_t;

/*
 * @brief        Defines the callback for notifying the completion of a reported properties update.
 *
 * @param[in]    request_id     Request ID provided by the caller when sending the reported
 * properties update.
 * @param[in]    status_code    Result of the reported properties update (uses HTTP status code
 * semantics).
 *
 * @return                      Nothing.
 */
typedef void (*properties_update_completed_t)(uint32_t request_id, az_iot_status status_code);

/*
 * @brief        Defines the callback for receiving a writable-properties update.
 *
 * @param[in]    request_id     Request ID provided by the caller when sending the reported
 * properties update.
 * @param[in]    status_code    Result of the reported properties update (uses HTTP status code
 * semantics).
 *
 * @return                      Nothing.
 */
typedef void (*properties_received_t)(az_span properties);

/*
 * @brief    Structure containing all the details of a IoT Plug and Play Command.
 */
typedef struct command_request_t_struct
{
  /*
   * @brief    ID of the command request, as received from Azure.
   */
  az_span request_id;

  /*
   * @brief    Name of the component this command is targeted to.
   *           This can be empty (set as AZ_SPAN_EMPTY).
   */
  az_span component_name;

  /*
   * @brief    Name of the command.
   */
  az_span command_name;

  /*
   * @brief    Optional payload sent by the caller for this command.
   */
  az_span payload;
} command_request_t;

/*
 * @brief        Defines the callback for receiving an IoT Plug and Play Command.
 * @remark       A response for this command MUST be provided to Azure by calling
 *               `azure_iot_send_command_response`.
 *
 * @param[in]    command     An instance of `command_request_t` containing all the details of
 *                           of the command received.
 *
 * @return                   Nothing.
 */
typedef void (*command_request_received_t)(command_request_t command);

/*
 * @brief    All the possible statuses returned by `azure_iot_get_status`.
 */
typedef enum azure_iot_status_t_struct
{
  /*
   * @brief    The Azure IoT client is completely disconnected.
   */
  azure_iot_disconnected,
  /*
   * @brief     The client is in an intermediate state between disconnected and connected.
   * @remark    When using SAS-based authentication (default for Azure IoT Central), the client
   *            will automatically perform a reconnection with a new SAS token after the previous
   *            one expires, going back into `azure_iot_connecting` state briefly.
   */
  azure_iot_connecting,
  /*
   * @brief    In this state the Azure IoT client is ready to be used for messaging.
   */
  azure_iot_connected,
  /*
   * @brief     The Azure IoT client encountered some internal error and is no longer active.
   * @remark    This can be caused by:
   *            - Memory issues (not enough memory provided in `azure_iot_config_t`'s
   * `data_buffer`);
   *            - Any failures reported by the application's MQTT client
   *              (through the abstracted interface) return values;
   *            - Any protocol errors returned by Azure IoT services.
   *            If not enough memory has been provided in `azure_iot_config_t`'s `data_buffer,
   *            please expand the size of that memory space.`
   *            Once the possible mitigations are applied stop the Azure IoT client
   *            by calling `azure_iot_stop` (which resets the client state) and restart it
   *            using `azure_iot_start`.
   */
  azure_iot_error
} azure_iot_status_t;

/*
 * @brief     Internal states of the Azure IoT client.
 * @remark    These states are not exposed to the user application.
 */
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
} azure_iot_client_state_t;

/*
 * @brief    Structure that holds the configuration for the Azure IoT client.
 * @remark   Once `azure_iot_start` is called, this structure SHALL NOT be modified by the
 *           user application unless `azure_iot_stop` is called.
 *           Also make sure that the instance of `azure_iot_config_t` (and its members) do not
 *           lose scope throughout the lifetime of the Azure IoT client.
 *           Most of the members of this structure use az_span for encapsulating a buffer and
 *           its size. For more details on az_span, please explore the code at
 *           https://github.com/azure/azure-sdk-for-c.
 */
typedef struct azure_iot_config_t_struct
{
  /*
   * @brief     User agent string to be provided to Azure IoT services.
   * @remark    This string is not exposed back to the user device or service applications,
   *            currently only being used internally by Azure IoT. However, this
   *            data can sometimes be useful for customer support, so we recommend
   *            providing this information in the standard format expected
   *            (e.g., #define UA "c%2F" AZ_SDK_VERSION_STRING "(<platform>%3B<architecture>)")
   */
  az_span user_agent;

  /*
   * @brief     Controls whether Azure IoT client must perform device-provisioning or not.
   * @remark    If the configuration provided below is for connecting to Azure Device Provisioning
   *            Service (dps_id_scope, ...), this flag must be set to true. This is the case when
   *            connecting to Azure IoT Central as well.
   */
  bool use_device_provisioning;

  /*
   * @brief     Fully qualified domain name of the Azure IoT Hub to connect to.
   * @remark    If performing device-provisioning this MUST be set to AZ_SPAN_EMPTY;
   *            in such case, this member will be set with the target Azure IoT Hub FQDN
   *            once device-provisioning is completed.
   */
  az_span iot_hub_fqdn;

  /*
   * @brief     Device ID to authenticate as when connecting to Azure IoT Hub.
   * @remark    If performing device-provisioning this MUST be set to AZ_SPAN_EMPTY;
   *            in such case, this member will be set with the target device ID
   *            once device-provisioning is completed.
   */
  az_span device_id;

  /*
   * @brief     Symmetric key of the device to authenticate as when connecting to Azure IoT Hub.
   * @remark    This key will be used to generate the MQTT client password, if using SAS-token
   *            authentication (which is used, for example, when connecting to Azure IoT Central).
   */
  az_span device_key;

  /*
   * @brief     X509 certificate to be used for authentication.
   *
   */
  az_span device_certificate;

  /*
   * @brief     X509 certificate private key to be used for authentication.
   *
   */
  az_span device_certificate_private_key;

  /*
   * @brief     The "Registration ID" to authenticate with when connecting to
   *            Azure Device Provisioning service.
   * @remark    This information is only needed when performing device-provisioning (which is used,
                for example, when connecting to Azure IoT Central). If device-provisioning is
                not being used (i.e., Azure IoT client is connecting directly to Azure IoT Hub)
                this member MUST be set with AZ_SPAN_EMPTY.
                Additionaly (!), if connecting to Azure IoT Central, this member MUST be set
                with the `device ID` created in the Azure IoT Central application.
   */
  az_span dps_registration_id;

  /*
   * @brief     The "ID Scope" to authenticate with when connecting to
   *            Azure Device Provisioning service.
   * @remark    This information is only needed when performing device-provisioning (which is used,
                for example, when connecting to Azure IoT Central). If device-provisioning is
                not being used (i.e., Azure IoT client is connecting directly to Azure IoT Hub)
                this member MUST be set with AZ_SPAN_EMPTY.
   */
  az_span dps_id_scope;

  /*
   * @brief     Model ID of the IoT Plug and Play template implemented by the user application.
   * @remark    This is used only when the application uses/implements IoT Plug and Play.
   */
  az_span model_id;

  /*
   * @brief     Buffer with memory space to be used internally by Azure IoT client for its
   *            internal functions.
   * @remark    This buffer MUST be large enough to perform all the string manipulations done
   *            internally by the Azure IoT client, like MQTT client client id, username and
   * password generation, as well as storing Azure IoT Hub fqdn and device id after
   * device-provisioning is completed (if device-provisioning is used). The maximum size required
   * depends on which services and authentication mode are used. If using device-provisioning with
   * SAS-token authentication (as used with Azure IoT Central), this size must be at least:
   *              sizeof(data_buffer) >= ( lengthof(<iot-hub-fqdn>) + lengthof(<device-id>) +
   *                                       lengthof(<MQTT-clientid>) + lengthof(<MQTT-username>) +
   *                                       2 * lengthof(<MQTT-password>) )
   *
   *              Where:
   *              <MQTT-clientid>  = <device-id> + '\0'
   *              <MQTT-username>  = <iot-hub-fqdn> + '/' + lengthof(<device-id>) + '/' + '?' +
   * <api-version> +
   *                                 "&DeviceClientType=" + urlenc(<user-agent>) + "&model-id=" +
   * urlenc(<pnp-model-id>) + '\0' <api-version>    = "api-version=<YYYY-MM-DD>" <MQTT-password>) =
   * "SharedAccessSignature sr=" + <iot-hub-fqdn> + "%2Fdevices%2F" + <device-id> +
   *                                 "&sig=" + urlenc(<sha256-string>) + "&se=" + <expiration-time>
   * + '\0' lengthof(<sha256-string>) <= lengthof(<64-char-string>) <expiration-time> =
   * <10-digit-unix-time>
   *
   *              Note: We use two times the length of <MQTT-password> given the internal operations
   * needed to generate it.
   *
   *              Example:
   *              <iot-hub-fqdn>    = "iotc-1a430cf3-6f05-4b84-965d-cb1385077966.azure-devices.net"
   *              <device-id>       = "d1"
   *              <sha256-string>   = "vj7jSTPe4CZqqs5c+DkEpdoHMB+m1rzsFF04JyP9Pr4="
   *              <pnp-model-id>    = "dtmi:azureiot:devkit:freertos:Esp32AzureIotKit;1"
   *              <api-version>     = "api-version=2020-09-30"
   *              <expiration-time> = "1641251566"
   *              <user-agent>      = "c%2F1.1.0-beta.1(FreeRTOS)"
   *
   *              sizeof(data_buffer) >= 592 bytes (59 bytes + 2 bytes + 3 bytes + 190 bytes + 338
   * bytes, respectively)
   */
  az_span data_buffer;

  /*
   * @brief    Set of functions to serve as interface between Azure IoT client and the
   * user-application MQTT client.
   */
  mqtt_client_interface_t mqtt_client_interface;

  /*
   * @brief    Set of functions for Azure IoT client to perform its necessary data manipulations.
   */
  data_manipulation_functions_t data_manipulation_functions;

  /*
   * @brief    Amount of minutes for which the MQTT password should be valid.
   * @remark   If set to zero, Azure IoT client sets it to the default value of 60 minutes.
   *           Once this amount of minutes has passed and the MQTT password is expired,
   *           Azure IoT client triggers its logic to generate a new password and reconnect with
   *           Azure IoT Hub.
   */
  uint32_t sas_token_lifetime_in_minutes;

  /*
   * @brief     Callback handler used by Azure IoT client to inform the user application of
   *            a completion of properties update.
   * @remark    A properties update is triggered by the user application when
   *            `azure_iot_send_properties_update` is called.
   */
  properties_update_completed_t on_properties_update_completed;

  /*
   * @brief     Callback handler used by Azure IoT client to inform the user application of
   *            a writable-properties update received from Azure IoT Hub.
   * @remark    If IoT Plug and Play is used, a response must be sent back to
   *            Azure IoT Hub.
   */
  properties_received_t on_properties_received;

  /*
   * @brief     Callback handler used by Azure IoT client to inform the user application of
   *            a device command received from Azure IoT Hub.
   * @remark    A response must be sent back to Azure IoT Hub by calling
   *            `azure_iot_send_command_response`.
   */
  command_request_received_t on_command_request_received;
} azure_iot_config_t;

/*
 * @brief     Structure that holds the state of the Azure IoT client.
 * @remark    None of the members within this structure may be accessed
 *            directly by the user application. Any of the members in azure_iot_t
 *            may be renamed, re-ordered, added and/or removed without
 *            notice.
 */
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
} azure_iot_t;

/*
 * @brief        Initializes the azure_iot_t structure that holds the Azure IoT client state.
 * @remark       This function must be called only once per `azure_iot_t` instance,
 *               before any other function can be called using it.
 *
 * @param[in]    azure_iot           A pointer to the instance of `azure_iot_t` defined by the
 * caller.
 * @param[in]    azure_iot_config    A pointer to a `azure_iot_config_t` containing all the
 *                                   configuration neeeded for the client to connect and work with
 *                                   the Azure IoT services.
 * @return                           Nothing.
 */
void azure_iot_init(azure_iot_t* azure_iot, azure_iot_config_t* azure_iot_config);

/*
 * @brief        Starts an Azure IoT client.
 * @remark       This function must be called once the user application is ready to start
 *               connecting and working with Azure IoT services. By calling this function
 *               the `azure_iot_t` instance is reset to the default state it needs to work properly.
 *               Only after a `azure_iot_t` is started `azure_iot_do_work` will be able to perform
 *               any tasks. If the Azure IoT client gets into an error state, `azure_iot_stop` must
 *               be called first, followed by a call to `azure_iot_start` so it can reconnect to
 *               the Azure IoT Hub again.
 *               Note: if device-provisioning is used, the device is provisioned only the first
 *               time a given `azure_iot_t` instance is started. Subsequent calls to
 *               `azure_iot_start` will re-use the Azure IoT Hub FQDN and device ID previously
 *               provisioned.
 *
 * @param[in]    azure_iot           A pointer to the instance of `azure_iot_t` defined by the
 * caller.
 * @param[in]    azure_iot_config    A pointer to a `azure_iot_config_t` containing all the
 *                                   configuration neeeded for the client to connect and work with
 *                                   the Azure IoT services.
 * @return       int                 0 on success, or non-zero if any failure occurs.
 */
int azure_iot_start(azure_iot_t* azure_iot);

/*
 * @brief        Stops an Azure IoT client.
 * @remark       This function must be called once the user application wants to stop working and
 *               disconnect from the Azure IoT services. The same instance of `azure_iot_t` can be
 *               used again by the user application by calling `azure_iot_start`.
 *
 * @param[in]    azure_iot           A pointer to the instance of `azure_iot_t` defined by the
 * caller.
 *
 * @return       int                 0 on success, or non-zero if any failure occurs.
 */
int azure_iot_stop(azure_iot_t* azure_iot);

/*
 * @brief        Gets the current state of the Azure IoT client.
 * @remark       The states informed are simplified for ease-of-use of this client, not reflecting
 *               all the detailed internal states possible within the Azure IoT client.
 *
 * @param[in]    azure_iot             A pointer to the instance of `azure_iot_t` previously
 * initialized by the caller.
 *
 * @return       azure_iot_status_t    One of four possible states as defined by
 * `azure_iot_status_t`. Please see the `azure_iot_status_t` documentation above for details.
 */
azure_iot_status_t azure_iot_get_status(azure_iot_t* azure_iot);

/*
 * @brief        Causes the Azure IoT client to perform its tasks for connecting and working with
 * Azure IoT services.
 * @remark       This function must be called frequently enough for the Azure IoT client to work
 * properly. That frequency should be enough to respect the timeouts implemented by the Azure IoT
 * services for its TCP connection and MQTT-protocol traffic with the client application. Calling it
 * once within a main loop() function of most embedded implementations should be enough. The
 * recommended minimum frequency is no less than once every 100 milliseconds.
 *
 * @param[in]    azure_iot             A pointer to the instance of `azure_iot_t` previously
 * initialized by the caller.
 *
 * @return                             Nothing.
 */
void azure_iot_do_work(azure_iot_t* azure_iot);

/*
 * @brief        Sends a telemetry payload to the Azure IoT Hub.
 *
 * @param[in]    azure_iot    A pointer to the instance of `azure_iot_t` previously initialized by
 * the caller.
 * @param[in]    message      An az_span instance containing the buffer and size of the actual
 * message to be sent.
 *
 * @return       int          0 on success, or non-zero if any failure occurs.
 */
int azure_iot_send_telemetry(azure_iot_t* azure_iot, az_span message);

/**
 * @brief        Sends a property update message to Azure IoT Hub.
 *
 * @param[in]    azure_iot     The pointer to the azure_iot_t instance that holds the state of the
 * Azure IoT client.
 * @param[in]    request_id    An unique identification number to correlate the response with when
 * @param[in]    message       An `az_span` with the message with the reported properties update
 *                             (a JSON document formatted according to the DTDL specification).
 *                             `message` gets passed as-is to the MQTT client publish function as
 * the payload, so if your MQTT client expects a null-terminated string for payload, make sure
 * `message` is a null-terminated string. `on_properties_update_completed` (set in
 * azure_iot_config_t) is invoked.
 *
 * @return       int           0 if the function succeeds, or non-zero if any failure occurs.
 */
int azure_iot_send_properties_update(azure_iot_t* azure_iot, uint32_t request_id, az_span message);

/**
 * @brief        Sends a property update message to Azure IoT Hub.
 *
 * @param[in]    azure_iot          The pointer to the azure_iot_t instance that holds the state of
 * the Azure IoT client.
 * @param[in]    request_id         The same `request_id` of the device command received from Azure
 * IoT Hub.
 * @param[in]    response_status    An HTTP-like code informing the success or failure of the
 * command. A 202 will inform the command was accepted and succeeded. A 404 will inform the command
 * received does not match the commands supported by this device application.
 * @param[in]    payload            A custom payload to be sent in the device command response.
 *                                  This is expected to be a json content.
 *                                  If no payload is to be sent, please set it as AZ_SPAN_EMPTY.
 *
 * @return       int                0 if the function succeeds, or non-zero if any failure occurs.
 */
int azure_iot_send_command_response(
    azure_iot_t* azure_iot,
    az_span request_id,
    uint16_t response_status,
    az_span payload);

/*
 * @brief        Informs the Azure IoT client that the MQTT client is connected.
 * @remark       This must be called after Azure IoT client invokes the `mqtt_client_init` callback
 *               (provided in the azure_iot_config_t instance) so it knows the MQTT client has
 * received a CONNACK from the Azure IoT service.
 *
 * @param[in]    azure_iot    A pointer to the instance of `azure_iot_t` previously initialized by
 * the caller.
 *
 * @return       int          0 on success, or non-zero if any failure occurs.
 */
int azure_iot_mqtt_client_connected(azure_iot_t* azure_iot);

/*
 * @brief        Informs the Azure IoT client that the MQTT client is disconnected.
 * @remark       This must be called after Azure IoT client invokes the `mqtt_client_deinit`
 * callback (provided in the azure_iot_config_t instance) so it knows the MQTT client has
 * disconnected from the Azure IoT service.
 *
 * @param[in]    azure_iot    A pointer to the instance of `azure_iot_t` previously initialized by
 * the caller.
 *
 * @return       int          0 on success, or non-zero if any failure occurs.
 */
int azure_iot_mqtt_client_disconnected(azure_iot_t* azure_iot);

/*
 * @brief        Informs the Azure IoT client that the MQTT client has subscribed to a topic.
 * @remark       This must be called after Azure IoT client invokes the `mqtt_client_subscribe`
 * callback (provided in the azure_iot_config_t instance) so it knows the MQTT client has received a
 * SUBACK for a topic subscription.
 *
 * @param[in]    azure_iot    A pointer to the instance of `azure_iot_t` previously initialized by
 * the caller.
 * @param[in]    packet_id    The ID of the subscription performed previous by the Azure IoT client.
 *
 * @return       int          0 on success, or non-zero if any failure occurs.
 */
int azure_iot_mqtt_client_subscribe_completed(azure_iot_t* azure_iot, int packet_id);

/*
 * @brief        Informs the Azure IoT client that the MQTT client has completed a PUBLISH.
 * @remark       This must be called after Azure IoT client invokes the `mqtt_client_publish`
 * callback (provided in the azure_iot_config_t instance) so it knows the MQTT client has completed
 * an MQTT PUBLISH. If the QoS is 0 (AT MOST ONCE), this shall be called by the user application
 * right after `mqtt_client_publish` is invoked. If the QoS of is 1 (AT LEAST ONCE), this shall be
 * called whenever a PUBACK is received.
 *
 * @param[in]    azure_iot    A pointer to the instance of `azure_iot_t` previously initialized by
 * the caller.
 * @param[in]    packet_id    The ID of the subscription performed previous by the Azure IoT client.
 *
 * @return       int          0 on success, or non-zero if any failure occurs.
 */
int azure_iot_mqtt_client_publish_completed(azure_iot_t* azure_iot, int packet_id);

/*
 * @brief        Informs the Azure IoT client that a new message has been received by the Azure IoT
 * service.
 * @remark       This must be called whenever an MQTT message is received from an Azure IoT service.
 *
 * @param[in]    azure_iot       A pointer to the instance of `azure_iot_t` previously initialized
 * by the caller.
 * @param[in]    mqtt_message    A `mqtt_message_t` instance containing all the details of the MQTT
 * message received (topic name, qos and payload).
 *
 * @return       int             0 on success, or non-zero if any failure occurs.
 */
int azure_iot_mqtt_client_message_received(azure_iot_t* azure_iot, mqtt_message_t* mqtt_message);

/* --- az_core extensions --- */
/*
 * These functions are used internally by the Azure IoT client code and its extensions.
 */

/**
 * @brief Slices `span` at position `size`, returns the first slice and assigns the second slice to
 * `remainder`.
 *
 * @param[in]    span           A span to be sliced.
 * @param[in]    source         The span to copy the contents from.
 * @param[out]   remainder      The pointer where to store the remainder of `span` after it is
 * sliced.
 *
 * @return       az_span        A slice of `span` from position zero to `size`.
 */
az_span split_az_span(az_span span, int32_t size, az_span* remainder);

/**
 * @brief Slices `destination` to fit `source`, copy `source` into the first slice and returns the
 * second through `remainder`.
 *
 * @param[in]    destination    A span large enough to contain the copy of the contents of `source`.
 * @param[in]    source         The span to copy the contents from.
 * @param[out]   remainder      The pointer where to store the remainder of `destination` after
 * `source` is copied.
 *
 * @return       az_span        A slice of `destination` with the same size as `source`, with
 * `source`'s content copied over.
 */
static az_span slice_and_copy_az_span(az_span destination, az_span source, az_span* remainder);

#endif // AZURE_IOT_H
