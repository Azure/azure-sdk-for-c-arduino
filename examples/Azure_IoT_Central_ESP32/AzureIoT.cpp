// Copyright (c) Microsoft Corporation. All rights reserved.
// SPDX-License-Identifier: MIT

#include "AzureIoT.h"
#include <stdarg.h>

#include <az_precondition_internal.h>

/* --- Function Returns --- */
#define RESULT_OK 0
#define RESULT_ERROR __LINE__

/* --- Logging --- */
#ifndef DISABLE_LOGGING
log_function_t default_logging_function = NULL;
#endif // DISABLE_LOGGING

/* --- Azure Definitions --- */
#define IOT_HUB_MQTT_PORT AZ_IOT_DEFAULT_MQTT_CONNECT_PORT
#define MQTT_PROTOCOL_PREFIX "mqtts://"
#define DPS_GLOBAL_ENDPOINT_MQTT_URI MQTT_PROTOCOL_PREFIX DPS_GLOBAL_ENDPOINT_FQDN
#define DPS_GLOBAL_ENDPOINT_MQTT_URI_WITH_PORT \
  DPS_GLOBAL_ENDPOINT_MQTT_URI ":" STR(DPS_GLOBAL_ENDPOINT_PORT)

#define MQTT_CLIENT_ID_BUFFER_SIZE 256
#define MQTT_USERNAME_BUFFER_SIZE 350
#define DECODED_SAS_KEY_BUFFER_SIZE 64
#define PLAIN_SAS_SIGNATURE_BUFFER_SIZE 256
#define SAS_HMAC256_ENCRYPTED_SIGNATURE_BUFFER_SIZE 32
#define SAS_SIGNATURE_BUFFER_SIZE 64
#define MQTT_PASSWORD_BUFFER_SIZE 512

#define DPS_REGISTER_CUSTOM_PAYLOAD_BEGIN "{\"modelId\":\""
#define DPS_REGISTER_CUSTOM_PAYLOAD_END "\"}"

#define NUMBER_OF_SECONDS_IN_A_MINUTE 60

#define EXIT_IF_TRUE(condition, retcode, message, ...) \
  do                                                   \
  {                                                    \
    if (condition)                                     \
    {                                                  \
      LogError(message, ##__VA_ARGS__);                \
      return retcode;                                  \
    }                                                  \
  } while (0)

#define EXIT_IF_AZ_FAILED(azresult, retcode, message, ...) \
  EXIT_IF_TRUE(az_result_failed(azresult), retcode, message, ##__VA_ARGS__)

/* --- Internal function prototypes --- */
static uint32_t get_current_unix_time();

static int generate_sas_token_for_dps(
    az_iot_provisioning_client* provisioning_client,
    az_span device_key,
    unsigned int duration_in_minutes,
    az_span data_buffer_span,
    data_manipulation_functions_t data_manipulation_functions,
    az_span sas_token,
    uint32_t* expiration_time);

static int generate_sas_token_for_iot_hub(
    az_iot_hub_client* iot_hub_client,
    az_span device_key,
    unsigned int duration_in_minutes,
    az_span data_buffer_span,
    data_manipulation_functions_t data_manipulation_functions,
    az_span sas_token,
    uint32_t* expiration_time);

static int get_mqtt_client_config_for_dps(
    azure_iot_t* azure_iot,
    mqtt_client_config_t* mqtt_client_config);

static int get_mqtt_client_config_for_iot_hub(
    azure_iot_t* azure_iot,
    mqtt_client_config_t* mqtt_client_config);

static az_span generate_dps_register_custom_property(
    az_span model_id,
    az_span data_buffer,
    az_span* remainder);

#define is_device_provisioned(azure_iot)                                     \
  (!az_span_is_content_equal(azure_iot->config->iot_hub_fqdn, AZ_SPAN_EMPTY) \
   && !az_span_is_content_equal(azure_iot->config->device_id, AZ_SPAN_EMPTY))

/* --- Public API --- */
void azure_iot_init(azure_iot_t* azure_iot, azure_iot_config_t* azure_iot_config)
{
  _az_PRECONDITION_NOT_NULL(azure_iot);
  _az_PRECONDITION_NOT_NULL(azure_iot_config);
  if (azure_iot_config->use_device_provisioning)
  {
    _az_PRECONDITION(az_span_is_content_equal(azure_iot_config->iot_hub_fqdn, AZ_SPAN_EMPTY));
    _az_PRECONDITION(az_span_is_content_equal(azure_iot_config->device_id, AZ_SPAN_EMPTY));
    _az_PRECONDITION_VALID_SPAN(azure_iot_config->dps_id_scope, 1, false);
    _az_PRECONDITION_VALID_SPAN(azure_iot_config->dps_registration_id, 1, false);
  }
  else
  {
    _az_PRECONDITION_VALID_SPAN(azure_iot_config->iot_hub_fqdn, 1, false);
    _az_PRECONDITION_VALID_SPAN(azure_iot_config->device_id, 1, false);
    _az_PRECONDITION(az_span_is_content_equal(azure_iot_config->dps_id_scope, AZ_SPAN_EMPTY));
    _az_PRECONDITION(
        az_span_is_content_equal(azure_iot_config->dps_registration_id, AZ_SPAN_EMPTY));
  }

  // Either device key or device certificate and certificate key should be defined.
  if (az_span_is_content_equal(azure_iot_config->device_key, AZ_SPAN_EMPTY)
      && (az_span_is_content_equal(azure_iot_config->device_certificate, AZ_SPAN_EMPTY)
          || az_span_is_content_equal(
              azure_iot_config->device_certificate_private_key, AZ_SPAN_EMPTY)))
  {
    LogError("Please define either a device key or a device certificate and certificate private "
             "key. See iot_configs.h");
    return;
  }

  _az_PRECONDITION_VALID_SPAN(azure_iot_config->data_buffer, 1, false);
  _az_PRECONDITION_NOT_NULL(azure_iot_config->data_manipulation_functions.base64_decode);
  _az_PRECONDITION_NOT_NULL(azure_iot_config->data_manipulation_functions.base64_encode);
  _az_PRECONDITION_NOT_NULL(azure_iot_config->data_manipulation_functions.hmac_sha256_encrypt);
  _az_PRECONDITION_NOT_NULL(azure_iot_config->mqtt_client_interface.mqtt_client_init);
  _az_PRECONDITION_NOT_NULL(azure_iot_config->mqtt_client_interface.mqtt_client_deinit);
  _az_PRECONDITION_NOT_NULL(azure_iot_config->mqtt_client_interface.mqtt_client_subscribe);
  _az_PRECONDITION_NOT_NULL(azure_iot_config->mqtt_client_interface.mqtt_client_publish);
  _az_PRECONDITION_NOT_NULL(azure_iot_config->on_properties_update_completed);
  _az_PRECONDITION_NOT_NULL(azure_iot_config->on_properties_received);
  _az_PRECONDITION_NOT_NULL(azure_iot_config->on_command_request_received);

  (void)memset(azure_iot, 0, sizeof(azure_iot_t));
  azure_iot->config = azure_iot_config;
  azure_iot->data_buffer = azure_iot->config->data_buffer;
  azure_iot->state = azure_iot_state_initialized;
  azure_iot->dps_operation_id = AZ_SPAN_EMPTY;

  if (azure_iot->config->sas_token_lifetime_in_minutes == 0)
  {
    azure_iot->config->sas_token_lifetime_in_minutes = DEFAULT_SAS_TOKEN_LIFETIME_IN_MINUTES;
  }
}

int azure_iot_start(azure_iot_t* azure_iot)
{
  _az_PRECONDITION_NOT_NULL(azure_iot);

  int result;

  if (azure_iot->state == azure_iot_state_not_initialized)
  {
    LogError("Azure IoT client must be initialized before starting.");
    result = RESULT_ERROR;
  }
  else if (azure_iot->state != azure_iot_state_initialized)
  {
    LogError("Azure IoT client already started or in error state.");
    result = RESULT_ERROR;
  }
  else
  {
    // TODO: should only go to started if stopped or in error?
    azure_iot->state = azure_iot_state_started;
    result = RESULT_OK;
  }

  return result;
}

int azure_iot_stop(azure_iot_t* azure_iot)
{
  _az_PRECONDITION_NOT_NULL(azure_iot);

  int result;

  if (azure_iot->state == azure_iot_state_not_initialized)
  {
    LogError("Azure IoT client must be initialized before stopping.");
    result = RESULT_ERROR;
  }
  else
  {
    if (azure_iot->mqtt_client_handle != NULL)
    {
      if (azure_iot->config->mqtt_client_interface.mqtt_client_deinit(azure_iot->mqtt_client_handle)
          != 0)
      {
        azure_iot->state = azure_iot_state_error;
        LogError("Failed deinitializing MQTT client.");
        result = RESULT_ERROR;
      }
      else
      {
        azure_iot->state = azure_iot_state_initialized;
        result = RESULT_OK;
      }

      azure_iot->mqtt_client_handle = NULL;
    }
    else
    {
      azure_iot->state = azure_iot_state_initialized;
      result = RESULT_OK;
    }
  }

  return result;
}

azure_iot_status_t azure_iot_get_status(azure_iot_t* azure_iot)
{
  _az_PRECONDITION_NOT_NULL(azure_iot);

  azure_iot_status_t status;

  switch (azure_iot->state)
  {
    case azure_iot_state_not_initialized:
    case azure_iot_state_initialized:
      status = azure_iot_disconnected;
      break;
    case azure_iot_state_started:
    case azure_iot_state_connecting_to_dps:
    case azure_iot_state_connected_to_dps:
    case azure_iot_state_subscribing_to_dps:
    case azure_iot_state_subscribed_to_dps:
    case azure_iot_state_provisioning_querying:
    case azure_iot_state_provisioning_waiting:
    case azure_iot_state_provisioned:
    case azure_iot_state_connecting_to_hub:
    case azure_iot_state_connected_to_hub:
    case azure_iot_state_subscribing_to_pnp_cmds:
    case azure_iot_state_subscribed_to_pnp_cmds:
    case azure_iot_state_subscribing_to_pnp_props:
    case azure_iot_state_subscribed_to_pnp_props:
    case azure_iot_state_subscribing_to_pnp_writable_props:
    case azure_iot_state_refreshing_sas:
      status = azure_iot_connecting;
      break;
    case azure_iot_state_ready:
      status = azure_iot_connected;
      break;
    case azure_iot_state_error:
    default:
      status = azure_iot_error;
      break;
  }

  return status;
}

void azure_iot_do_work(azure_iot_t* azure_iot)
{
  _az_PRECONDITION_NOT_NULL(azure_iot);

  int result;
  int64_t now;
  int packet_id;
  az_result azrc;
  size_t length;
  mqtt_client_config_t mqtt_client_config;
  mqtt_message_t mqtt_message;
  az_span data_buffer;
  az_span dps_register_custom_property;

  switch (azure_iot->state)
  {
    case azure_iot_state_not_initialized:
    case azure_iot_state_initialized:
      break;
    case azure_iot_state_started:
      if (azure_iot->config->use_device_provisioning && !is_device_provisioned(azure_iot))
      {
        // This seems harmless, but...
        // azure_iot->config->data_buffer always points to the original buffer provided by the user.
        // azure_iot->data_buffer is an intermediate pointer. It starts by pointing to
        // azure_iot->config->data_buffer. In the steps below the code might need to retain part of
        // azure_iot->data_buffer for saving some critical information, namely the DPS operation id,
        // the provisioned IoT Hub FQDN and provisioned Device ID (if provisioning is being used).
        // In these cases, azure_iot->data_buffer will then point to `the remaining available space`
        // of azure_iot->config->data_buffer after deducting the spaces for the data mentioned above
        // (DPS operation id, IoT Hub FQDN and Device ID). Not all these data exist at the same time
        // though. Memory (from azure_iot->data_buffer) is taken/reserved for the `Operation ID`
        // while provisioning is in progress, but as soon as it is succesfully completed the
        // `Operation ID` is no longer needed, so its memory is released back into
        // azure_iot->data_buffer, but then space is reserved again for the provisioned IoT Hub FQDN
        // and Device ID. Finally, when the client is stopped and started again, it does not do
        // provisioning again if done already. In such case, the logic needs to preserve the spaces
        // reserved for IoT Hub FQDN and Device ID previously provisioned.
        azure_iot->data_buffer = azure_iot->config->data_buffer;

        result = get_mqtt_client_config_for_dps(azure_iot, &mqtt_client_config);
        azure_iot->state = azure_iot_state_connecting_to_dps;
      }
      else
      {
        result = get_mqtt_client_config_for_iot_hub(azure_iot, &mqtt_client_config);
        azure_iot->state = azure_iot_state_connecting_to_hub;
      }

      if (result != 0
          || azure_iot->config->mqtt_client_interface.mqtt_client_init(
                 &mqtt_client_config, &azure_iot->mqtt_client_handle)
              != 0)
      {
        azure_iot->state = azure_iot_state_error;
        LogError("Failed initializing MQTT client.");
        return;
      }

      break;
    case azure_iot_state_connecting_to_dps:
      break;
    case azure_iot_state_connected_to_dps:
      // Subscribe to DPS topic.
      azure_iot->state = azure_iot_state_subscribing_to_dps;

      packet_id = azure_iot->config->mqtt_client_interface.mqtt_client_subscribe(
          azure_iot->mqtt_client_handle,
          AZ_SPAN_FROM_STR(AZ_IOT_PROVISIONING_CLIENT_REGISTER_SUBSCRIBE_TOPIC),
          mqtt_qos_at_most_once);

      if (packet_id < 0)
      {
        azure_iot->state = azure_iot_state_error;
        LogError("Failed subscribing to Azure Device Provisioning respose topic.");
        return;
      }

      break;
    case azure_iot_state_subscribing_to_dps:
      break;
    case azure_iot_state_subscribed_to_dps:
      data_buffer = azure_iot->data_buffer;

      azrc = az_iot_provisioning_client_register_get_publish_topic(
          &azure_iot->dps_client,
          (char*)az_span_ptr(data_buffer),
          az_span_size(data_buffer),
          &length);

      if (az_result_failed(azrc))
      {
        azure_iot->state = azure_iot_state_error;
        LogError("Failed getting the DPS register topic: az_result return code 0x%08x.", azrc);
        return;
      }

      mqtt_message.topic = split_az_span(data_buffer, length + 1, &data_buffer);

      if (az_span_is_content_equal(mqtt_message.topic, AZ_SPAN_EMPTY)
          || az_span_is_content_equal(data_buffer, AZ_SPAN_EMPTY))
      {
        azure_iot->state = azure_iot_state_error;
        LogError("Failed reserving memory for DPS register payload.");
        return;
      }

      dps_register_custom_property = generate_dps_register_custom_property(
          azure_iot->config->model_id, data_buffer, &mqtt_message.payload);

      if (az_span_is_content_equal(dps_register_custom_property, AZ_SPAN_EMPTY))
      {
        azure_iot->state = azure_iot_state_error;
        LogError("Failed generating DPS register custom property payload.");
        return;
      }

      azrc = az_iot_provisioning_client_get_request_payload(
          &azure_iot->dps_client,
          dps_register_custom_property,
          NULL,
          az_span_ptr(mqtt_message.payload),
          az_span_size(mqtt_message.payload),
          &length);

      if (az_result_failed(azrc))
      {
        azure_iot->state = azure_iot_state_error;
        LogError("az_iot_provisioning_client_get_request_payload failed (0x%08x).", azrc);
        return;
      }

      mqtt_message.payload = az_span_slice(mqtt_message.payload, 0, length);
      mqtt_message.qos = mqtt_qos_at_most_once;

      azure_iot->state = azure_iot_state_provisioning_waiting;

      packet_id = azure_iot->config->mqtt_client_interface.mqtt_client_publish(
          azure_iot->mqtt_client_handle, &mqtt_message);

      if (packet_id < 0)
      {
        azure_iot->state = azure_iot_state_error;
        LogError("Failed publishing to DPS registration topic");
        return;
      }

      break;
    case azure_iot_state_provisioning_querying:
      now = get_current_unix_time();

      if (now == 0)
      {
        azure_iot->state = azure_iot_state_error;
        LogError("Failed getting current time for DPS query throttling");
        return;
      }

      if ((now - azure_iot->dps_last_query_time) < azure_iot->dps_retry_after_seconds)
      {
        // Throttling query...
        return;
      }

      azrc = az_iot_provisioning_client_query_status_get_publish_topic(
          &azure_iot->dps_client,
          azure_iot->dps_operation_id, // register_response->operation_id,
          (char*)az_span_ptr(azure_iot->data_buffer),
          (size_t)az_span_size(azure_iot->data_buffer),
          &length);

      if (az_result_failed(azrc))
      {
        azure_iot->state = azure_iot_state_error;
        LogError(
            "Unable to get provisioning query status publish topic: az_result return code 0x%08x.",
            azrc);
        return;
      }

      mqtt_message.topic = az_span_slice(azure_iot->data_buffer, 0, length + 1);
      mqtt_message.payload = AZ_SPAN_EMPTY;
      mqtt_message.qos = mqtt_qos_at_most_once;

      azure_iot->state = azure_iot_state_provisioning_waiting;
      azure_iot->dps_last_query_time = now;

      packet_id = azure_iot->config->mqtt_client_interface.mqtt_client_publish(
          azure_iot->mqtt_client_handle, &mqtt_message);

      if (packet_id < 0)
      {
        azure_iot->state = azure_iot_state_error;
        LogError("Failed publishing to DPS status query topic");
        return;
      }

      break;
    case azure_iot_state_provisioning_waiting:
      break;
    case azure_iot_state_provisioned:
      // Disconnect from Provisioning Service first.
      if (azure_iot->config->use_device_provisioning && azure_iot->mqtt_client_handle != NULL
          && azure_iot->config->mqtt_client_interface.mqtt_client_deinit(
                 azure_iot->mqtt_client_handle)
              != 0)
      {
        azure_iot->state = azure_iot_state_error;
        LogError("Failed de-initializing MQTT client.");
        return;
      }

      azure_iot->mqtt_client_handle = NULL;

      // Connect to Hub
      result = get_mqtt_client_config_for_iot_hub(azure_iot, &mqtt_client_config);

      if (result != 0)
      {
        azure_iot->state = azure_iot_state_error;
        LogError("Failed getting MQTT client configuration for connecting to IoT Hub.");
        return;
      }

      azure_iot->state = azure_iot_state_connecting_to_hub;

      if (azure_iot->config->mqtt_client_interface.mqtt_client_init(
              &mqtt_client_config, &azure_iot->mqtt_client_handle)
          != 0)
      {
        azure_iot->state = azure_iot_state_error;
        LogError("Failed initializing MQTT client for IoT Hub connection.");
        return;
      }

      break;
    case azure_iot_state_connecting_to_hub:
      break;
    case azure_iot_state_connected_to_hub:
      azure_iot->state = azure_iot_state_subscribing_to_pnp_cmds;

      packet_id = azure_iot->config->mqtt_client_interface.mqtt_client_subscribe(
          azure_iot->mqtt_client_handle,
          AZ_SPAN_FROM_STR(AZ_IOT_HUB_CLIENT_COMMANDS_SUBSCRIBE_TOPIC),
          mqtt_qos_at_least_once);

      if (packet_id < 0)
      {
        azure_iot->state = azure_iot_state_error;
        LogError("Failed subscribing to IoT Plug and Play commands topic.");
        return;
      }

      break;
    case azure_iot_state_subscribing_to_pnp_cmds:
      break;
    case azure_iot_state_subscribed_to_pnp_cmds:
      azure_iot->state = azure_iot_state_subscribing_to_pnp_props;

      packet_id = azure_iot->config->mqtt_client_interface.mqtt_client_subscribe(
          azure_iot->mqtt_client_handle,
          AZ_SPAN_FROM_STR(AZ_IOT_HUB_CLIENT_PROPERTIES_MESSAGE_SUBSCRIBE_TOPIC),
          mqtt_qos_at_least_once);

      if (packet_id < 0)
      {
        azure_iot->state = azure_iot_state_error;
        LogError("Failed subscribing to IoT Plug and Play properties topic.");
        return;
      }

      break;
    case azure_iot_state_subscribing_to_pnp_props:
      break;
    case azure_iot_state_subscribed_to_pnp_props:
      azure_iot->state = azure_iot_state_subscribing_to_pnp_writable_props;

      packet_id = azure_iot->config->mqtt_client_interface.mqtt_client_subscribe(
          azure_iot->mqtt_client_handle,
          AZ_SPAN_FROM_STR(AZ_IOT_HUB_CLIENT_PROPERTIES_WRITABLE_UPDATES_SUBSCRIBE_TOPIC),
          mqtt_qos_at_least_once);

      if (packet_id < 0)
      {
        azure_iot->state = azure_iot_state_error;
        LogError("Failed subscribing to IoT Plug and Play writable properties topic.");
        return;
      }

      break;
    case azure_iot_state_subscribing_to_pnp_writable_props:
      break;
    case azure_iot_state_ready:
      // Checking for SAS token expiration.
      now = get_current_unix_time();

      if (now == 0)
      {
        azure_iot->state = azure_iot_state_error;
        LogError("Failed getting current time for checking SAS token expiration.");
        return;
      }
      else if ((azure_iot->sas_token_expiration_time - now) < SAS_TOKEN_REFRESH_THRESHOLD_IN_SECS)
      {
        azure_iot->state = azure_iot_state_refreshing_sas;
        if (azure_iot->config->mqtt_client_interface.mqtt_client_deinit(
                azure_iot->mqtt_client_handle)
            != 0)
        {
          azure_iot->state = azure_iot_state_error;
          LogError("Failed de-initializing MQTT client.");
          return;
        }

        azure_iot->mqtt_client_handle = NULL;
      }
      break;
    case azure_iot_state_refreshing_sas:
      break;
    case azure_iot_state_error:
    default:
      break;
  }
}

int azure_iot_send_telemetry(azure_iot_t* azure_iot, az_span message)
{
  _az_PRECONDITION_NOT_NULL(azure_iot);
  _az_PRECONDITION_VALID_SPAN(message, 1, false);

  az_result azr;
  size_t topic_length;
  mqtt_message_t mqtt_message;

  azr = az_iot_hub_client_telemetry_get_publish_topic(
      &azure_iot->iot_hub_client,
      NULL,
      (char*)az_span_ptr(azure_iot->data_buffer),
      az_span_size(azure_iot->data_buffer),
      &topic_length);
  EXIT_IF_AZ_FAILED(azr, RESULT_ERROR, "Failed to get the telemetry topic");

  mqtt_message.topic = az_span_slice(azure_iot->data_buffer, 0, topic_length + 1);
  mqtt_message.payload = message;
  mqtt_message.qos = mqtt_qos_at_most_once;

  int packet_id = azure_iot->config->mqtt_client_interface.mqtt_client_publish(
      azure_iot->mqtt_client_handle, &mqtt_message);
  EXIT_IF_TRUE(packet_id < 0, RESULT_ERROR, "Failed publishing to telemetry topic");

  return RESULT_OK;
}

int azure_iot_send_properties_update(azure_iot_t* azure_iot, uint32_t request_id, az_span message)
{
  _az_PRECONDITION_NOT_NULL(azure_iot);
  _az_PRECONDITION_VALID_SPAN(message, 1, false);

  az_result azr;
  size_t topic_length;
  mqtt_message_t mqtt_message;
  az_span data_buffer = azure_iot->data_buffer;
  az_span request_id_span = data_buffer;

  azr = az_span_u32toa(request_id_span, request_id, &data_buffer);
  EXIT_IF_TRUE(az_result_failed(azr), RESULT_ERROR, "Failed generating Twin request id.");
  request_id_span = az_span_slice(
      request_id_span, 0, az_span_size(request_id_span) - az_span_size(data_buffer));

  azr = az_iot_hub_client_properties_get_reported_publish_topic(
      &azure_iot->iot_hub_client,
      request_id_span,
      (char*)az_span_ptr(data_buffer),
      az_span_size(data_buffer),
      &topic_length);
  EXIT_IF_AZ_FAILED(azr, RESULT_ERROR, "Failed to get the reported properties publish topic");

  mqtt_message.topic = az_span_slice(data_buffer, 0, topic_length);
  mqtt_message.payload = message;
  mqtt_message.qos = mqtt_qos_at_most_once;

  int packet_id = azure_iot->config->mqtt_client_interface.mqtt_client_publish(
      azure_iot->mqtt_client_handle, &mqtt_message);
  EXIT_IF_TRUE(packet_id < 0, RESULT_ERROR, "Failed publishing to reported properties topic.");

  return RESULT_OK;
}

int azure_iot_mqtt_client_connected(azure_iot_t* azure_iot)
{
  _az_PRECONDITION_NOT_NULL(azure_iot);

  int result;

  if (azure_iot->state == azure_iot_state_connecting_to_dps)
  {
    if (!azure_iot->config->use_device_provisioning)
    {
      azure_iot->state = azure_iot_state_error;
      LogError("Invalid state, provisioning disabled in config.");
      result = RESULT_ERROR;
    }
    else
    {
      azure_iot->state = azure_iot_state_connected_to_dps;
      result = RESULT_OK;
    }
  }
  else if (azure_iot->state == azure_iot_state_connecting_to_hub)
  {
    azure_iot->state = azure_iot_state_connected_to_hub;
    result = RESULT_OK;
  }
  else
  {
    LogError("Unexpected mqtt client connection (%d).", azure_iot->state);
    azure_iot->state = azure_iot_state_error;
    result = RESULT_ERROR;
  }

  return result;
}

int azure_iot_mqtt_client_disconnected(azure_iot_t* azure_iot)
{
  _az_PRECONDITION_NOT_NULL(azure_iot);

  int result;

  if (azure_iot->state == azure_iot_state_refreshing_sas)
  {
    // Moving the state to azure_iot_state_provisioned will cause this client to move
    // on to trying to connect to the Azure IoT Hub again.
    azure_iot->state = azure_iot_state_provisioned;
    result = RESULT_OK;
  }
  else
  {
    // MQTT client could disconnect at any time for any reason, it is an expected situation.
    azure_iot->state = azure_iot_state_initialized;
    result = RESULT_OK;
  }

  return result;
}

int azure_iot_mqtt_client_subscribe_completed(azure_iot_t* azure_iot, int packet_id)
{
  _az_PRECONDITION_NOT_NULL(azure_iot);

  (void)packet_id;
  int result;

  if (azure_iot->state == azure_iot_state_subscribing_to_dps)
  {
    azure_iot->state = azure_iot_state_subscribed_to_dps;
    result = RESULT_OK;
  }
  else if (azure_iot->state == azure_iot_state_subscribing_to_pnp_cmds)
  {
    azure_iot->state = azure_iot_state_subscribed_to_pnp_cmds;
    result = RESULT_OK;
  }
  else if (azure_iot->state == azure_iot_state_subscribing_to_pnp_props)
  {
    azure_iot->state = azure_iot_state_subscribed_to_pnp_props;
    result = RESULT_OK;
  }
  else if (azure_iot->state == azure_iot_state_subscribing_to_pnp_writable_props)
  {
    azure_iot->state = azure_iot_state_ready;
    result = RESULT_OK;
  }
  else
  {
    LogError("No SUBACK notification expected (packet id=%d)", packet_id);
    result = RESULT_ERROR;
  }

  return result;
}

/*
 * Currently unused.
 */
int azure_iot_mqtt_client_publish_completed(azure_iot_t* azure_iot, int packet_id)
{
  _az_PRECONDITION_NOT_NULL(azure_iot);

  (void)packet_id;

  return RESULT_OK;
}

int azure_iot_mqtt_client_message_received(azure_iot_t* azure_iot, mqtt_message_t* mqtt_message)
{
  _az_PRECONDITION_NOT_NULL(azure_iot);
  _az_PRECONDITION_NOT_NULL(mqtt_message);
  _az_PRECONDITION_VALID_SPAN(mqtt_message->topic, 1, false);

  int result;
  az_result azrc;

  if (azure_iot->state == azure_iot_state_ready)
  {
    // This message should either be:
    // - a response to a properties update request, or
    // - a response to a "get" properties request, or
    // - a command request.

    az_iot_hub_client_properties_message property_message;
    azrc = az_iot_hub_client_properties_parse_received_topic(
        &azure_iot->iot_hub_client, mqtt_message->topic, &property_message);

    if (az_result_succeeded(azrc))
    {
      switch (property_message.message_type)
      {
        // A response from a property GET publish message with the property document as a payload.
        case AZ_IOT_HUB_CLIENT_PROPERTIES_MESSAGE_TYPE_GET_RESPONSE:
          result = RESULT_ERROR; // TODO: change to RESULT_OK once implemented.
          break;

        // An update to the desired properties with the properties as a payload.
        case AZ_IOT_HUB_CLIENT_PROPERTIES_MESSAGE_TYPE_WRITABLE_UPDATED:
          if (azure_iot->config->on_properties_received != NULL)
          {
            azure_iot->config->on_properties_received(mqtt_message->payload);
          }
          result = RESULT_OK;
          break;

        // When the device publishes a property update, this message type arrives when
        // server acknowledges this.
        case AZ_IOT_HUB_CLIENT_PROPERTIES_MESSAGE_TYPE_ACKNOWLEDGEMENT:
          result = RESULT_OK;

          if (azure_iot->config->on_properties_update_completed != NULL)
          {
            uint32_t request_id = 0;

            if (az_result_failed(az_span_atou32(property_message.request_id, &request_id)))
            {
              LogError(
                  "Failed parsing properties update request id (%.*s).",
                  az_span_size(property_message.request_id),
                  az_span_ptr(property_message.request_id));
              result = RESULT_ERROR;
            }
            else
            {
              azure_iot->config->on_properties_update_completed(
                  request_id, property_message.status);
            }
          }
          break;

        // An error has occurred
        case AZ_IOT_HUB_CLIENT_PROPERTIES_MESSAGE_TYPE_ERROR:
          LogError("Message Type: Request Error");
          result = RESULT_ERROR;
          break;
      }
    }
    else
    {
      az_iot_hub_client_command_request az_sdk_command_request;
      azrc = az_iot_hub_client_commands_parse_received_topic(
          &azure_iot->iot_hub_client, mqtt_message->topic, &az_sdk_command_request);

      if (az_result_succeeded(azrc))
      {
        if (azure_iot->config->on_command_request_received != NULL)
        {
          command_request_t command_request;
          command_request.request_id = az_sdk_command_request.request_id;
          command_request.component_name = az_sdk_command_request.component_name;
          command_request.command_name = az_sdk_command_request.command_name;
          command_request.payload = mqtt_message->payload;

          azure_iot->config->on_command_request_received(command_request);
        }

        result = RESULT_OK;
      }
      else
      {
        LogError(
            "Could not recognize MQTT message (%.*s).",
            az_span_size(mqtt_message->topic),
            az_span_ptr(mqtt_message->topic));
        result = RESULT_ERROR;
      }
    }
  }
  else if (azure_iot->state == azure_iot_state_provisioning_waiting)
  {
    az_iot_provisioning_client_register_response register_response;

    azrc = az_iot_provisioning_client_parse_received_topic_and_payload(
        &azure_iot->dps_client, mqtt_message->topic, mqtt_message->payload, &register_response);

    if (az_result_failed(azrc))
    {
      LogError("Could not parse device provisioning message: az_result return code 0x%08x.", azrc);
      result = RESULT_ERROR;
    }
    else
    {
      if (!az_iot_provisioning_client_operation_complete(register_response.operation_status))
      {
        result = RESULT_OK;

        if (az_span_is_content_equal(azure_iot->dps_operation_id, AZ_SPAN_EMPTY))
        {
          azure_iot->dps_operation_id = slice_and_copy_az_span(
              azure_iot->data_buffer, register_response.operation_id, &azure_iot->data_buffer);

          if (az_span_is_content_equal(azure_iot->dps_operation_id, AZ_SPAN_EMPTY))
          {
            azure_iot->state = azure_iot_state_error;
            LogError("Failed reserving memory for DPS operation id.");
            result = RESULT_ERROR;
          }
        }

        if (result == RESULT_OK)
        {
          azure_iot->dps_retry_after_seconds = register_response.retry_after_seconds;
          azure_iot->state = azure_iot_state_provisioning_querying;
        }
      }
      else if (register_response.operation_status == AZ_IOT_PROVISIONING_STATUS_ASSIGNED)
      {
        az_span data_buffer = azure_iot->config->data_buffer; // Operation ID is no longer needed.
        azure_iot->data_buffer = data_buffer; // In case any step below fails.

        azure_iot->config->iot_hub_fqdn = slice_and_copy_az_span(
            data_buffer, register_response.registration_state.assigned_hub_hostname, &data_buffer);

        if (az_span_is_content_equal(azure_iot->config->iot_hub_fqdn, AZ_SPAN_EMPTY))
        {
          azure_iot->state = azure_iot_state_error;
          LogError("Failed saving IoT Hub fqdn from provisioning.");
          result = RESULT_ERROR;
        }
        else
        {
          azure_iot->config->device_id = slice_and_copy_az_span(
              data_buffer, register_response.registration_state.device_id, &data_buffer);

          if (az_span_is_content_equal(azure_iot->config->device_id, AZ_SPAN_EMPTY))
          {
            azure_iot->state = azure_iot_state_error;
            LogError("Failed saving device id from provisioning.");
            result = RESULT_ERROR;
          }
          else
          {
            azure_iot->data_buffer = data_buffer;
            azure_iot->state = azure_iot_state_provisioned;
            result = RESULT_OK;
          }
        }
      }
      else
      {
        azure_iot->state = azure_iot_state_error;
        LogError("Device provisisioning failed.");
        result = RESULT_OK;
      }
    }
  }
  else
  {
    LogError("No PUBLISH notification expected.");
    result = RESULT_ERROR;
  }

  return result;
}

int azure_iot_send_command_response(
    azure_iot_t* azure_iot,
    az_span request_id,
    uint16_t response_status,
    az_span payload)
{
  _az_PRECONDITION_NOT_NULL(azure_iot);
  _az_PRECONDITION_VALID_SPAN(request_id, 1, false);

  az_result azrc;
  mqtt_message_t mqtt_message;
  size_t topic_length;
  int packet_id;

  mqtt_message.topic = azure_iot->data_buffer;

  azrc = az_iot_hub_client_commands_response_get_publish_topic(
      &azure_iot->iot_hub_client,
      request_id,
      response_status,
      (char*)az_span_ptr(mqtt_message.topic),
      az_span_size(mqtt_message.topic),
      &topic_length);
  EXIT_IF_AZ_FAILED(azrc, RESULT_ERROR, "Failed to get the commands response topic.");

  mqtt_message.topic = az_span_slice(mqtt_message.topic, 0, topic_length + 1);
  mqtt_message.payload = payload;
  mqtt_message.qos = mqtt_qos_at_most_once;

  packet_id = azure_iot->config->mqtt_client_interface.mqtt_client_publish(
      azure_iot->mqtt_client_handle, &mqtt_message);

  if (packet_id < 0)
  {
    azure_iot->state = azure_iot_state_error;
    LogError(
        "Failed publishing command response (%.*s).",
        az_span_size(request_id),
        az_span_ptr(request_id));
    return RESULT_ERROR;
  }
  else
  {
    return RESULT_OK;
  }
}

/* --- Implementation of internal functions --- */

/*
 * @brief           Gets the number of seconds since UNIX epoch until now.
 * @return uint32_t Number of seconds.
 */
static uint32_t get_current_unix_time()
{
  time_t now = time(NULL);
  return (now == INDEFINITE_TIME ? 0 : (uint32_t)(now));
}

/*
 * @brief           Initializes the Device Provisioning client and generates the config for an MQTT
 * client.
 * @param[in]       azure_iot          A pointer to an initialized instance of azure_iot_t.
 * @param[in]       mqtt_client_config A pointer to a generic structure to contain the configuration
 * for creating and connecting an MQTT client to Azure Device Provisioning service.
 *
 * @return int      0 on success, non-zero if any failure occurs.
 */
static int get_mqtt_client_config_for_dps(
    azure_iot_t* azure_iot,
    mqtt_client_config_t* mqtt_client_config)
{
  az_span data_buffer_span;
  az_span client_id_span;
  az_span username_span;
  az_span password_span;
  size_t client_id_length;
  size_t username_length;
  size_t password_length;
  az_result azrc;

  azrc = az_iot_provisioning_client_init(
      &azure_iot->dps_client,
      AZ_SPAN_FROM_STR(DPS_GLOBAL_ENDPOINT_MQTT_URI_WITH_PORT),
      azure_iot->config->dps_id_scope,
      azure_iot->config->dps_registration_id,
      NULL);
  EXIT_IF_AZ_FAILED(azrc, RESULT_ERROR, "Failed to initialize provisioning client.");

  data_buffer_span = azure_iot->data_buffer;

  password_span = split_az_span(data_buffer_span, MQTT_PASSWORD_BUFFER_SIZE, &data_buffer_span);
  EXIT_IF_TRUE(
      az_span_is_content_equal(password_span, AZ_SPAN_EMPTY),
      RESULT_ERROR,
      "Failed reserving buffer for password_span.");

  if (!az_span_is_content_equal(azure_iot->config->device_key, AZ_SPAN_EMPTY))
  {
    password_length = generate_sas_token_for_dps(
        &azure_iot->dps_client,
        azure_iot->config->device_key,
        azure_iot->config->sas_token_lifetime_in_minutes,
        data_buffer_span,
        azure_iot->config->data_manipulation_functions,
        password_span,
        &azure_iot->sas_token_expiration_time);
    EXIT_IF_TRUE(
        password_length == 0, RESULT_ERROR, "Failed creating mqtt password for DPS connection.");

    mqtt_client_config->password = password_span;
  }
  else
  {
    mqtt_client_config->password = AZ_SPAN_EMPTY;
  }

  client_id_span = split_az_span(data_buffer_span, MQTT_CLIENT_ID_BUFFER_SIZE, &data_buffer_span);
  EXIT_IF_TRUE(
      az_span_is_content_equal(client_id_span, AZ_SPAN_EMPTY),
      RESULT_ERROR,
      "Failed reserving buffer for client_id_span.");

  azrc = az_iot_provisioning_client_get_client_id(
      &azure_iot->dps_client,
      (char*)az_span_ptr(client_id_span),
      az_span_size(client_id_span),
      &client_id_length);
  EXIT_IF_AZ_FAILED(azrc, RESULT_ERROR, "Failed getting client id for DPS connection.");

  username_span = split_az_span(data_buffer_span, MQTT_USERNAME_BUFFER_SIZE, &data_buffer_span);

  azrc = az_iot_provisioning_client_get_user_name(
      &azure_iot->dps_client,
      (char*)az_span_ptr(username_span),
      az_span_size(username_span),
      &username_length);
  EXIT_IF_AZ_FAILED(azrc, RESULT_ERROR, "Failed to get MQTT client username.");

  mqtt_client_config->address = AZ_SPAN_FROM_STR(DPS_GLOBAL_ENDPOINT_FQDN);
  mqtt_client_config->port = DPS_GLOBAL_ENDPOINT_PORT;

  mqtt_client_config->client_id = client_id_span;
  mqtt_client_config->username = username_span;

  return RESULT_OK;
}

/*
 * @brief           Initializes the Azure IoT Hub client and generates the config for an MQTT
 * client.
 * @param[in]       azure_iot          A pointer to an initialized instance of azure_iot_t.
 * @param[in]       mqtt_client_config A pointer to a generic structure to contain the configuration
 * for creating and connecting an MQTT client to Azure IoT Hub.
 *
 * @return int      0 on success, non-zero if any failure occurs.
 */
static int get_mqtt_client_config_for_iot_hub(
    azure_iot_t* azure_iot,
    mqtt_client_config_t* mqtt_client_config)
{
  az_span data_buffer_span;
  az_span client_id_span;
  az_span username_span;
  az_span password_span;
  size_t client_id_length;
  size_t username_length;
  size_t password_length;
  az_result azrc;

  azure_iot->iot_hub_client_options = az_iot_hub_client_options_default();
  azure_iot->iot_hub_client_options.user_agent = azure_iot->config->user_agent;
  azure_iot->iot_hub_client_options.model_id = azure_iot->config->model_id;

  azrc = az_iot_hub_client_init(
      &azure_iot->iot_hub_client,
      azure_iot->config->iot_hub_fqdn,
      azure_iot->config->device_id,
      &azure_iot->iot_hub_client_options);
  EXIT_IF_AZ_FAILED(azrc, RESULT_ERROR, "Failed to initialize Azure IoT Hub client.");

  data_buffer_span = azure_iot->data_buffer;

  password_span = split_az_span(data_buffer_span, MQTT_PASSWORD_BUFFER_SIZE, &data_buffer_span);
  EXIT_IF_TRUE(
      az_span_is_content_equal(password_span, AZ_SPAN_EMPTY),
      RESULT_ERROR,
      "Failed reserving buffer for password_span.");

  password_length = generate_sas_token_for_iot_hub(
      &azure_iot->iot_hub_client,
      azure_iot->config->device_key,
      azure_iot->config->sas_token_lifetime_in_minutes,
      data_buffer_span,
      azure_iot->config->data_manipulation_functions,
      password_span,
      &azure_iot->sas_token_expiration_time);
  EXIT_IF_TRUE(
      password_length == 0, RESULT_ERROR, "Failed creating mqtt password for IoT Hub connection.");

  client_id_span = split_az_span(data_buffer_span, MQTT_CLIENT_ID_BUFFER_SIZE, &data_buffer_span);
  EXIT_IF_TRUE(
      az_span_is_content_equal(client_id_span, AZ_SPAN_EMPTY),
      RESULT_ERROR,
      "Failed reserving buffer for client_id_span.");

  azrc = az_iot_hub_client_get_client_id(
      &azure_iot->iot_hub_client,
      (char*)az_span_ptr(client_id_span),
      az_span_size(client_id_span),
      &client_id_length);
  EXIT_IF_AZ_FAILED(azrc, RESULT_ERROR, "Failed getting client id for IoT Hub connection.");

  username_span = split_az_span(data_buffer_span, MQTT_USERNAME_BUFFER_SIZE, &data_buffer_span);

  azrc = az_iot_hub_client_get_user_name(
      &azure_iot->iot_hub_client,
      (char*)az_span_ptr(username_span),
      az_span_size(username_span),
      &username_length);
  EXIT_IF_AZ_FAILED(azrc, RESULT_ERROR, "Failed to get MQTT client username.");

  mqtt_client_config->address = azure_iot->config->iot_hub_fqdn;
  mqtt_client_config->port = IOT_HUB_ENDPOINT_PORT;

  mqtt_client_config->client_id = client_id_span;
  mqtt_client_config->username = username_span;
  mqtt_client_config->password = password_span;

  return RESULT_OK;
}

/*
 * @brief           Generates a SAS token used as password for connecting with Azure Device
 * Provisioning.
 * @remarks         The SAS token generation depends on the following steps:
 *                  1. Calculate the expiration time, as current unix time since epoch plus the
 * token duration, in minutes.
 *                  2. Generate the SAS signature;
 *                    a. Generate the DPS-specific secret string (a.k.a., "signature");
 *                    b. base64-decode the encryption key (device key);
 *                    c. Encrypt (HMAC-SHA256) the signature using the base64-decoded encryption
 * key; d. base64-encode the encrypted signature, which gives the final SAS signature (sig);
 *                  3. Compose the final SAS token with the DPS audience (sr), SAS signature (sig)
 * and expiration time (se).
 * @param[in]       provisioning_client         A pointer to an initialized instance of
 * az_iot_provisioning_client.
 * @param[in]       device_key                  az_span containing the device key.
 * @param[in]       duration_in_minutes         Duration of the SAS token, in minutes.
 * @param[in]       data_buffer_span            az_span with a buffer containing enough space for
 * all the intermediate data generated by this function.
 * @param[in]       data_manipulation_functions Set of user-defined functions needed for the
 * generation of the SAS token.
 * @param[out]      sas_token                   az_span with buffer where to write the resulting SAS
 * token.
 * @param[out]      expiration_time             The expiration time of the resulting SAS token, as
 * unix time.
 *
 * @return int      Length of the resulting SAS token, or zero if any failure occurs.
 */
static int generate_sas_token_for_dps(
    az_iot_provisioning_client* provisioning_client,
    az_span device_key,
    unsigned int duration_in_minutes,
    az_span data_buffer_span,
    data_manipulation_functions_t data_manipulation_functions,
    az_span sas_token,
    uint32_t* expiration_time)
{
  int result;
  az_result rc;
  uint32_t current_unix_time;
  size_t mqtt_password_length, decoded_sas_key_length, length;
  az_span plain_sas_signature, sas_signature, decoded_sas_key, sas_hmac256_signed_signature;

  // Step 1.
  current_unix_time = get_current_unix_time();
  EXIT_IF_TRUE(current_unix_time == 0, 0, "Failed getting current unix time.");

  *expiration_time = current_unix_time + duration_in_minutes * NUMBER_OF_SECONDS_IN_A_MINUTE;

  // Step 2.a.
  plain_sas_signature
      = split_az_span(data_buffer_span, PLAIN_SAS_SIGNATURE_BUFFER_SIZE, &data_buffer_span);
  EXIT_IF_TRUE(
      az_span_is_content_equal(plain_sas_signature, AZ_SPAN_EMPTY),
      0,
      "Failed reserving buffer for plain sas token.");

  rc = az_iot_provisioning_client_sas_get_signature(
      provisioning_client, *expiration_time, plain_sas_signature, &plain_sas_signature);
  EXIT_IF_AZ_FAILED(rc, 0, "Could not get the signature for SAS key.");

  // Step 2.b.
  sas_signature = split_az_span(data_buffer_span, SAS_SIGNATURE_BUFFER_SIZE, &data_buffer_span);
  EXIT_IF_TRUE(
      az_span_is_content_equal(sas_signature, AZ_SPAN_EMPTY),
      0,
      "Failed reserving buffer for sas_signature.");

  decoded_sas_key = split_az_span(data_buffer_span, DECODED_SAS_KEY_BUFFER_SIZE, &data_buffer_span);
  EXIT_IF_TRUE(
      az_span_is_content_equal(decoded_sas_key, AZ_SPAN_EMPTY),
      0,
      "Failed reserving buffer for decoded_sas_key.");

  result = data_manipulation_functions.base64_decode(
      az_span_ptr(device_key),
      az_span_size(device_key),
      az_span_ptr(decoded_sas_key),
      az_span_size(decoded_sas_key),
      &decoded_sas_key_length);
  EXIT_IF_TRUE(result != 0, 0, "Failed decoding SAS key.");

  // Step 2.c.
  sas_hmac256_signed_signature = split_az_span(
      data_buffer_span, SAS_HMAC256_ENCRYPTED_SIGNATURE_BUFFER_SIZE, &data_buffer_span);
  EXIT_IF_TRUE(
      az_span_is_content_equal(sas_hmac256_signed_signature, AZ_SPAN_EMPTY),
      0,
      "Failed reserving buffer for sas_hmac256_signed_signature.");

  result = data_manipulation_functions.hmac_sha256_encrypt(
      az_span_ptr(decoded_sas_key),
      decoded_sas_key_length,
      az_span_ptr(plain_sas_signature),
      az_span_size(plain_sas_signature),
      az_span_ptr(sas_hmac256_signed_signature),
      az_span_size(sas_hmac256_signed_signature));
  EXIT_IF_TRUE(result != 0, 0, "Failed encrypting SAS signature.");

  // Step 2.d.
  result = data_manipulation_functions.base64_encode(
      az_span_ptr(sas_hmac256_signed_signature),
      az_span_size(sas_hmac256_signed_signature),
      az_span_ptr(sas_signature),
      az_span_size(sas_signature),
      &length);
  EXIT_IF_TRUE(result != 0, 0, "Failed encoding SAS signature.");

  sas_signature = az_span_slice(sas_signature, 0, length);

  // Step 3.
  rc = az_iot_provisioning_client_sas_get_password(
      provisioning_client,
      sas_signature,
      *expiration_time,
      AZ_SPAN_EMPTY,
      (char*)az_span_ptr(sas_token),
      az_span_size(sas_token),
      &mqtt_password_length);
  EXIT_IF_AZ_FAILED(rc, 0, "Could not get the password.");

  return mqtt_password_length;
}

/*
 * @brief           Generates a SAS token used as password for connecting with Azure IoT Hub.
 * @remarks         The SAS token generation depends on the following steps:
 *                  1. Calculate the expiration time, as current unix time since epoch plus the
 * token duration, in minutes.
 *                  2. Generate the SAS signature;
 *                    a. Generate the DPS-specific secret string (a.k.a., "signature");
 *                    b. base64-decode the encryption key (device key);
 *                    c. Encrypt (HMAC-SHA256) the signature using the base64-decoded encryption
 * key; d. base64-encode the encrypted signature, which gives the final SAS signature (sig);
 *                  3. Compose the final SAS token with the DPS audience (sr), SAS signature (sig)
 * and expiration time (se).
 * @param[in]       iot_hub_client              A pointer to an initialized instance of
 * az_iot_hub_client.
 * @param[in]       device_key                  az_span containing the device key.
 * @param[in]       duration_in_minutes         Duration of the SAS token, in minutes.
 * @param[in]       data_buffer_span            az_span with a buffer containing enough space for
 * all the intermediate data generated by this function.
 * @param[in]       data_manipulation_functions Set of user-defined functions needed for the
 * generation of the SAS token.
 * @param[out]      sas_token                   az_span with buffer where to write the resulting SAS
 * token.
 * @param[out]      expiration_time             The expiration time of the resulting SAS token, as
 * unix time.
 *
 * @return int      Length of the resulting SAS token, or zero if any failure occurs.
 */
static int generate_sas_token_for_iot_hub(
    az_iot_hub_client* iot_hub_client,
    az_span device_key,
    unsigned int duration_in_minutes,
    az_span data_buffer_span,
    data_manipulation_functions_t data_manipulation_functions,
    az_span sas_token,
    uint32_t* expiration_time)
{
  int result;
  az_result rc;
  uint32_t current_unix_time;
  size_t mqtt_password_length, decoded_sas_key_length, length;
  az_span plain_sas_signature, sas_signature, decoded_sas_key, sas_hmac256_signed_signature;

  // Step 1.
  current_unix_time = get_current_unix_time();
  EXIT_IF_TRUE(current_unix_time == 0, 0, "Failed getting current unix time.");

  *expiration_time = current_unix_time + duration_in_minutes * NUMBER_OF_SECONDS_IN_A_MINUTE;

  // Step 2.a.
  plain_sas_signature
      = split_az_span(data_buffer_span, PLAIN_SAS_SIGNATURE_BUFFER_SIZE, &data_buffer_span);
  EXIT_IF_TRUE(
      az_span_is_content_equal(plain_sas_signature, AZ_SPAN_EMPTY),
      0,
      "Failed reserving buffer for plain sas token.");

  rc = az_iot_hub_client_sas_get_signature(
      iot_hub_client, *expiration_time, plain_sas_signature, &plain_sas_signature);
  EXIT_IF_AZ_FAILED(rc, 0, "Could not get the signature for SAS key.");

  // Step 2.b.
  sas_signature = split_az_span(data_buffer_span, SAS_SIGNATURE_BUFFER_SIZE, &data_buffer_span);
  EXIT_IF_TRUE(
      az_span_is_content_equal(sas_signature, AZ_SPAN_EMPTY),
      0,
      "Failed reserving buffer for sas_signature.");

  decoded_sas_key = split_az_span(data_buffer_span, DECODED_SAS_KEY_BUFFER_SIZE, &data_buffer_span);
  EXIT_IF_TRUE(
      az_span_is_content_equal(decoded_sas_key, AZ_SPAN_EMPTY),
      0,
      "Failed reserving buffer for decoded_sas_key.");

  result = data_manipulation_functions.base64_decode(
      az_span_ptr(device_key),
      az_span_size(device_key),
      az_span_ptr(decoded_sas_key),
      az_span_size(decoded_sas_key),
      &decoded_sas_key_length);
  EXIT_IF_TRUE(result != 0, 0, "Failed decoding SAS key.");

  // Step 2.c.
  sas_hmac256_signed_signature = split_az_span(
      data_buffer_span, SAS_HMAC256_ENCRYPTED_SIGNATURE_BUFFER_SIZE, &data_buffer_span);
  EXIT_IF_TRUE(
      az_span_is_content_equal(sas_hmac256_signed_signature, AZ_SPAN_EMPTY),
      0,
      "Failed reserving buffer for sas_hmac256_signed_signature.");

  result = data_manipulation_functions.hmac_sha256_encrypt(
      az_span_ptr(decoded_sas_key),
      decoded_sas_key_length,
      az_span_ptr(plain_sas_signature),
      az_span_size(plain_sas_signature),
      az_span_ptr(sas_hmac256_signed_signature),
      az_span_size(sas_hmac256_signed_signature));
  EXIT_IF_TRUE(result != 0, 0, "Failed encrypting SAS signature.");

  // Step 2.d.
  result = data_manipulation_functions.base64_encode(
      az_span_ptr(sas_hmac256_signed_signature),
      az_span_size(sas_hmac256_signed_signature),
      az_span_ptr(sas_signature),
      az_span_size(sas_signature),
      &length);
  EXIT_IF_TRUE(result != 0, 0, "Failed encoding SAS signature.");

  sas_signature = az_span_slice(sas_signature, 0, length);

  // Step 3.
  rc = az_iot_hub_client_sas_get_password(
      iot_hub_client,
      *expiration_time,
      sas_signature,
      AZ_SPAN_EMPTY,
      (char*)az_span_ptr(sas_token),
      az_span_size(sas_token),
      &mqtt_password_length);
  EXIT_IF_AZ_FAILED(rc, 0, "Could not get the password.");

  return mqtt_password_length;
}

/**
 * @brief        Generates a custom payload for DPS registration request containing the Azure PnP
 * model ID.
 * @remark       This payload with the model ID is required for Azure IoT Central to properly
 *               assign the IoT Plug and Play template.
 *
 * @param[in]    mode_id        The string with the IoT Plug and Play model ID.
 * @param[in]    data_buffer    Buffer where to write the resulting payload.
 * @param[in]    remainder      The remainder space of `data_buffer` after enough is reserved for
 *                              the resulting payload.
 *
 * @return       az_span        An az_span (reserved from `data_buffer`) containing the payload
 *                              for the DPS registration request.
 */
static az_span generate_dps_register_custom_property(
    az_span model_id,
    az_span data_buffer,
    az_span* remainder)
{
  az_span custom_property;
  size_t length = lengthof(DPS_REGISTER_CUSTOM_PAYLOAD_BEGIN) + az_span_size(model_id)
      + lengthof(DPS_REGISTER_CUSTOM_PAYLOAD_END);

  custom_property = split_az_span(data_buffer, length, remainder);
  EXIT_IF_TRUE(
      az_span_is_content_equal(custom_property, AZ_SPAN_EMPTY),
      AZ_SPAN_EMPTY,
      "Failed generating DPS register custom property (not enough space).");

  data_buffer = az_span_copy(data_buffer, AZ_SPAN_FROM_STR(DPS_REGISTER_CUSTOM_PAYLOAD_BEGIN));
  EXIT_IF_TRUE(
      az_span_is_content_equal(data_buffer, AZ_SPAN_EMPTY),
      AZ_SPAN_EMPTY,
      "Failed generating DPS register custom property (prefix).");

  data_buffer = az_span_copy(data_buffer, model_id);
  EXIT_IF_TRUE(
      az_span_is_content_equal(data_buffer, AZ_SPAN_EMPTY),
      AZ_SPAN_EMPTY,
      "Failed generating DPS register custom property (model id).");

  data_buffer = az_span_copy(data_buffer, AZ_SPAN_FROM_STR(DPS_REGISTER_CUSTOM_PAYLOAD_END));
  EXIT_IF_TRUE(
      az_span_is_content_equal(data_buffer, AZ_SPAN_EMPTY),
      AZ_SPAN_EMPTY,
      "Failed generating DPS register custom property (suffix).");

  return custom_property;
}

/* --- az_core extensions --- */
az_span split_az_span(az_span span, int32_t size, az_span* remainder)
{
  az_span result = az_span_slice(span, 0, size);

  if (remainder != NULL && !az_span_is_content_equal(result, AZ_SPAN_EMPTY))
  {
    *remainder = az_span_slice(span, size, az_span_size(span));
  }

  return result;
}

az_span slice_and_copy_az_span(az_span destination, az_span source, az_span* remainder)
{
  az_span result = split_az_span(destination, az_span_size(source), remainder);

  if (az_span_is_content_equal(*remainder, AZ_SPAN_EMPTY))
  {
    result = AZ_SPAN_EMPTY;
  }

  if (!az_span_is_content_equal(result, AZ_SPAN_EMPTY))
  {
    (void)az_span_copy(result, source);
  }

  return result;
}
