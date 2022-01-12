// Copyright (c) Microsoft Corporation. All rights reserved.
// SPDX-License-Identifier: MIT

#include "AzureIoT.h"
#include <stdarg.h>

/* --- Logging --- */
#ifndef DISABLE_LOGGING
log_function_t default_logging_function = NULL;
#endif // DISABLE_LOGGING

/* --- az_core extensions --- */
#define az_span_is_empty(span) az_span_is_content_equal(span, AZ_SPAN_EMPTY)

static az_span az_span_split(az_span span, int32_t size, az_span* remainder)
{
  az_span result = az_span_slice(span, 0, size);

  if (remainder != NULL && !az_span_is_empty(result))
  {
    *remainder = az_span_slice(span, size, az_span_size(span));
  }

  return result;
}

/**
 * @brief Splits `destination` into two parts, copy `source` into the first one and returns the second through `remainder`.
 * 
 * @param[in]    destination    A span large enough to contain the copy of the contents of `source`.
 * @param[in]    source         The span to copy the contents from.
 * @param[in]    remainder      The pointer where to store the remainder of `destination` after `source` is copied.
 * 
 * @return az_span A slice of `destination` (from its start) with the exact content and size of `source`.
 */
static az_span az_span_split_copy(az_span destination, az_span source, az_span* remainder)
{
  az_span result = az_span_split(destination, az_span_size(source), remainder);

  if (az_span_is_empty(*remainder))
  {
    result = AZ_SPAN_EMPTY;
  }

  if (!az_span_is_empty(result))
  {
    (void)az_span_copy(result, source);
  }

  return result;
}

/**
 * @brief Returns an #az_span expression over a variable containing a null-terminated string.
 *
 * For example:
 * `char* string_var = "this is a null-terminated string";`
 * `az_span string_span = AZ_SPAN_FROM_STRING_VAR(string_var);`
 *
 * Will result in:
 * `az_span string_span = az_span_create((uint8_t*)string_var, strlen(string_var));`
 */
#define AZ_SPAN_FROM_STRING_VAR(string_var) az_span_create((uint8_t*)string_var, strlen(string_var))

/* --- Azure Abstractions --- */
#define IOT_HUB_MQTT_PORT                           8883
#define MQTT_PROTOCOL_PREFIX                        "mqtts://"
#define DPS_GLOBAL_ENDPOINT_MQTT_URI                MQTT_PROTOCOL_PREFIX DPS_GLOBAL_ENDPOINT_FQDN
#define DPS_GLOBAL_ENDPOINT_MQTT_URI_WITH_PORT      DPS_GLOBAL_ENDPOINT_MQTT_URI ":" STR(DPS_GLOBAL_ENDPOINT_PORT)

#define MQTT_CLIENT_ID_BUFFER_SIZE                  256
#define MQTT_USERNAME_BUFFER_SIZE                   256
#define DECODED_SAS_KEY_BUFFER_SIZE                 32
#define PLAIN_SAS_SIGNATURE_BUFFER_SIZE             256
#define SAS_HMAC256_ENCRIPTED_SIGNATURE_BUFFER_SIZE 32
#define SAS_SIGNATURE_BUFFER_SIZE                   64
#define MQTT_PASSWORD_BUFFER_SIZE                   256

#define EXIT_IF_NULL_SPAN(span, message_to_log, ...)                  \
  do                                                                  \
  {                                                                   \
    if (az_span_is_content_equal(span, AZ_SPAN_EMPTY))                \
    {                                                                 \
      LogError(message_to_log, ##__VA_ARGS__ );                       \
      return __LINE__;                                                \
    }                                                                 \
  } while (0)

#define EXIT_IF_AZ_FAILED(azfn, retcode, message, ...)                \
  do                                                                  \
  {                                                                   \
    az_result const result = (azfn);                                  \
                                                                      \
    if (az_result_failed(result))                                     \
    {                                                                 \
      LogError("Function returned az_result=0x%08x", result);             \
      LogError(message, ##__VA_ARGS__ );                              \
      return retcode;                                                 \
    }                                                                 \
  } while (0)

#define EXIT_IF_TRUE(condition, retcode, message, ...)                \
  do                                                                  \
  {                                                                   \
    if (condition)                                                    \
    {                                                                 \
      LogError(message, ##__VA_ARGS__ );                              \
      return retcode;                                                 \
    }                                                                 \
  } while (0)

/* --- Internal function prototypes --- */
static uint32_t get_current_unix_time();

static int generate_sas_token_for_dps(
  az_iot_provisioning_client* provisioning_client,
  az_span device_key,
  unsigned int duration_in_minutes,
  az_span data_buffer_span,
  data_manipulation_functions_t data_manipulation_functions,
  az_span sas_token, uint32_t* expiration_time);

static int generate_sas_token_for_iot_hub(
  az_iot_hub_client* iot_hub_client,
  az_span device_key,
  unsigned int duration_in_minutes,
  az_span data_buffer_span,
  data_manipulation_functions_t data_manipulation_functions,
  az_span sas_token, uint32_t* expiration_time);

static int get_mqtt_client_config_for_dps(azure_iot_t* azure_iot, mqtt_client_config_t* mqtt_client_config);

static int get_mqtt_client_config_for_iot_hub(azure_iot_t* azure_iot, mqtt_client_config_t* mqtt_client_config);

#define is_device_provisioned(azure_iot) \
  (!az_span_is_empty(azure_iot->config->iot_hub_fqdn) && !az_span_is_empty(azure_iot->config->device_id))

/* --- Public API --- */
int azure_iot_init(azure_iot_t* azure_iot, azure_iot_config_t* iot_config)
{
  // TODO: error check
  (void)memset(azure_iot, 0, sizeof(azure_iot_t));
  azure_iot->config = iot_config;
  azure_iot->data_buffer = azure_iot->config->data_buffer;
  azure_iot->state = azure_iot_state_initialized;
  azure_iot->dps_operation_id = AZ_SPAN_EMPTY;

  return RESULT_OK;
}

int azure_iot_start(azure_iot_t* azure_iot)
{
  // TODO: error check
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
  // TODO: error check
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
      if (azure_iot->config->mqtt_client_interface.mqtt_client_deinit(&azure_iot->mqtt_client_handle) != 0)
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
  // TODO: error check.
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
  int result;
  // TODO: error check.
  int64_t now;
  int packet_id;
  az_result azrc;
  size_t topic_name_length;
  mqtt_client_config_t mqtt_client_config;
  mqtt_message_t mqtt_message;
      
  switch (azure_iot->state)
  {
    case azure_iot_state_not_initialized:
    case azure_iot_state_initialized:
      break;
    case azure_iot_state_started:
      mqtt_client_config_t mqtt_client_config;

      if (azure_iot->config->use_device_provisioning &&
          !is_device_provisioned(azure_iot))
      {
        // This seems harmless, but...
        // azure_iot->config->data_buffer always points to the original buffer provided by the user.
        // azure_iot->data_buffer is an intermediate pointer. It starts by pointint to azure_iot->config->data_buffer.
        // In the steps below the code might need to retain part of azure_iot->data_buffer for saving some critical information,
        // namely the DPS operation id, the provisioned IoT Hub FQDN and provisioned Device ID (if provisioning is being used).
        // In these cases, azure_iot->data_buffer will then point to `the remaining available space of azure_iot->config->data_buffer
        // after deducting the spaces for the data mentioned above (DPS operation id, IoT Hub FQDN and Device ID).
        // Not all these data exist at the same time though.
        // Memory (from azure_iot->data_buffer) is taken/reserved for the `Operation ID` while provisioning is in progress,
        // but as soon as it is succesfully completed the `Operation ID` is no longer needed, so its memory is released back into
        // azure_iot->data_buffer, but then space is reserved again for the provisioned IoT Hub FQDN and Device ID.
        // Finally, when the client is stopped and started again, it does not do provisioning again if done already.
        // In such case, the logic needs to preserve the spaces reserved for IoT Hub FQDN and Device ID previously provisioned.
        azure_iot->data_buffer = azure_iot->config->data_buffer;

        result = get_mqtt_client_config_for_dps(azure_iot, &mqtt_client_config);
        azure_iot->state = azure_iot_state_connecting_to_dps;
      }
      else
      {
        result = get_mqtt_client_config_for_iot_hub(azure_iot, &mqtt_client_config);
        azure_iot->state = azure_iot_state_connecting_to_hub;
      }

      if (result != 0 || 
          azure_iot->config->mqtt_client_interface.mqtt_client_init(&mqtt_client_config, &azure_iot->mqtt_client_handle) != 0)
      {
        azure_iot->state = azure_iot_state_error;
        LogError("Failed initializing MQTT client.");
      }

      break;
    case azure_iot_state_connecting_to_dps:
      break;
    case azure_iot_state_connected_to_dps:
      // Subscribe to DPS topic.
      azure_iot->state = azure_iot_state_subscribing_to_dps;

      packet_id = azure_iot->config->mqtt_client_interface.mqtt_client_subscribe(
          azure_iot->mqtt_client_handle, 
          (const uint8_t*)AZ_IOT_PROVISIONING_CLIENT_REGISTER_SUBSCRIBE_TOPIC,
          lengthof(AZ_IOT_PROVISIONING_CLIENT_REGISTER_SUBSCRIBE_TOPIC),
          mqtt_qos_at_most_once);
          
      if (packet_id < 0)
      {
        azure_iot->state = azure_iot_state_error;
        LogError("Failed subscribing to Azure Device Provisioning respose topic.");
      }

      break;
    case azure_iot_state_subscribing_to_dps:
      break;
    case azure_iot_state_subscribed_to_dps:
      azrc = az_iot_provisioning_client_register_get_publish_topic(
          &azure_iot->dps_client, (char*)az_span_ptr(azure_iot->data_buffer), az_span_size(azure_iot->data_buffer), &topic_name_length);
    
      if (az_result_failed(azrc))
      {
        azure_iot->state = azure_iot_state_error;
        LogError("Failed getting the DPS register topic: az_result return code 0x%08x.", azrc);
        return;
      }

      mqtt_message.topic = az_span_slice(azure_iot->data_buffer, 0, topic_name_length);
      mqtt_message.payload = AZ_SPAN_EMPTY;
      mqtt_message.qos = mqtt_qos_at_most_once;

      azure_iot->state = azure_iot_state_provisioning_waiting;
        
      packet_id = azure_iot->config->mqtt_client_interface.mqtt_client_publish(azure_iot->mqtt_client_handle, &mqtt_message);
      
      if (packet_id < 0)
      {
        azure_iot->state = azure_iot_state_error;
        LogError("Failed publishing to DPS registration topic");
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
        &topic_name_length);

      if (az_result_failed(azrc))
      {
        azure_iot->state = azure_iot_state_error;
        LogError("Unable to get provisioning query status publish topic: az_result return code 0x%08x.", azrc);
        return;
      }

      mqtt_message.topic = az_span_slice(azure_iot->data_buffer, 0, topic_name_length);
      mqtt_message.payload = AZ_SPAN_EMPTY;
      mqtt_message.qos = mqtt_qos_at_most_once;

      azure_iot->state = azure_iot_state_provisioning_waiting;
      azure_iot->dps_last_query_time = now;
         
      packet_id = azure_iot->config->mqtt_client_interface.mqtt_client_publish(azure_iot->mqtt_client_handle, &mqtt_message);
      
      if (packet_id < 0)
      {
        azure_iot->state = azure_iot_state_error;
        LogError("Failed publishing to DPS status query topic");
      }

      break;
    case azure_iot_state_provisioning_waiting:
      break;
    case azure_iot_state_provisioned:
      // Disconnect from Provisioning Service first.
      if (azure_iot->config->use_device_provisioning &&
          azure_iot->config->mqtt_client_interface.mqtt_client_deinit(azure_iot->mqtt_client_handle) != 0)
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

      if (azure_iot->config->mqtt_client_interface.mqtt_client_init(&mqtt_client_config, &azure_iot->mqtt_client_handle) != 0)
      {
        azure_iot->state = azure_iot_state_error;
        LogError("Failed initializing MQTT client.");
      }

      break;
    case azure_iot_state_connecting_to_hub:
      break;
    case azure_iot_state_connected_to_hub:
      azure_iot->state = azure_iot_state_subscribing_to_pnp_cmds;
      
      packet_id = azure_iot->config->mqtt_client_interface.mqtt_client_subscribe(
          azure_iot->mqtt_client_handle, (const uint8_t*)AZ_IOT_HUB_CLIENT_COMMANDS_SUBSCRIBE_TOPIC,
          lengthof(AZ_IOT_HUB_CLIENT_COMMANDS_SUBSCRIBE_TOPIC), mqtt_qos_at_least_once);

      if (packet_id < 0)
      {
        azure_iot->state = azure_iot_state_error;
        LogError("Failed subscribing to Azure Plug and Play commands topic.");
      }

      break;
    case azure_iot_state_subscribing_to_pnp_cmds:
      break;
    case azure_iot_state_subscribed_to_pnp_cmds:
      azure_iot->state = azure_iot_state_subscribing_to_pnp_props;

      packet_id = azure_iot->config->mqtt_client_interface.mqtt_client_subscribe(
          azure_iot->mqtt_client_handle, (const uint8_t*)AZ_IOT_HUB_CLIENT_PROPERTIES_MESSAGE_SUBSCRIBE_TOPIC, 
          lengthof(AZ_IOT_HUB_CLIENT_PROPERTIES_MESSAGE_SUBSCRIBE_TOPIC), mqtt_qos_at_least_once);

      if (packet_id < 0)
      {
        azure_iot->state = azure_iot_state_error;
        LogError("Failed subscribing to Azure Plug and Play properties topic.");
      }
    
      break;

    case azure_iot_state_subscribing_to_pnp_props:
      break;
    case azure_iot_state_subscribed_to_pnp_props:
      azure_iot->state = azure_iot_state_subscribing_to_pnp_writable_props;
    
      packet_id = azure_iot->config->mqtt_client_interface.mqtt_client_subscribe(
          azure_iot->mqtt_client_handle, (const uint8_t*)AZ_IOT_HUB_CLIENT_PROPERTIES_WRITABLE_UPDATES_SUBSCRIBE_TOPIC,
          lengthof(AZ_IOT_HUB_CLIENT_PROPERTIES_WRITABLE_UPDATES_SUBSCRIBE_TOPIC), mqtt_qos_at_least_once);

      if (packet_id < 0)
      {
        azure_iot->state = azure_iot_state_error;
        LogError("Failed subscribing to Azure Plug and Play writable properties topic.");
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
      }
      else if ((azure_iot->sas_token_expiration_time - now) < SAS_TOKEN_REFRESH_THRESHOLD_SECS)
      {
        azure_iot->state = azure_iot_state_refreshing_sas;
        if (azure_iot->config->mqtt_client_interface.mqtt_client_deinit(&azure_iot->mqtt_client_handle) != 0)
        {
          azure_iot->state = azure_iot_state_error;
          LogError("Failed de-initializing MQTT client.");
        }
      }
      break;
    case azure_iot_state_refreshing_sas:
      break;
    case azure_iot_state_error:
    default:
      break;
  }
}

int azure_iot_send_telemetry(azure_iot_t* azure_iot, const unsigned char* message, size_t length)
{
  // TODO: check errors (azure_iot status, etc).
  
  az_result azr;
  size_t topic_length;
  mqtt_message_t mqtt_message;

  azr = az_iot_hub_client_telemetry_get_publish_topic(
      &azure_iot->iot_hub_client, NULL, (char*)az_span_ptr(azure_iot->data_buffer), az_span_size(azure_iot->data_buffer), &topic_length);
  EXIT_IF_AZ_FAILED(azr, RESULT_ERROR, "Failed to get the telemetry topic");

  mqtt_message.topic = az_span_slice(azure_iot->data_buffer, 0, topic_length);
  mqtt_message.payload = az_span_create((uint8_t*)message, length);
  mqtt_message.qos = mqtt_qos_at_most_once;

  int packet_id = azure_iot->config->mqtt_client_interface.mqtt_client_publish(azure_iot->mqtt_client_handle, &mqtt_message);

  if (packet_id < 0)
  {
    LogError("Failed publishing to telemetry topic");
    return RESULT_ERROR;
  }
  else
  {
    return RESULT_OK;    
  }
}

int azure_iot_mqtt_client_connected(azure_iot_t* azure_iot)
{
  // TODO: error check.
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
  // TODO: error check.
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
  (void)packet_id;

  // TODO: error check.
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

int azure_iot_mqtt_client_publish_completed(azure_iot_t* azure_iot, int packet_id)
{
  // TODO: error check.
  int result = RESULT_OK;
  // TODO: reassess and remove if not needed.  
  return result;
}

int azure_iot_mqtt_client_message_received(azure_iot_t* azure_iot, mqtt_message_t* mqtt_message)
{
  // TODO: error check.
  int result;

  if (azure_iot->state == azure_iot_state_provisioning_waiting)
  {
    az_result azrc;
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

        if (az_span_is_empty(azure_iot->dps_operation_id))
        {
          azure_iot->dps_operation_id = az_span_split_copy(azure_iot->data_buffer, register_response.operation_id, &azure_iot->data_buffer);

          if (az_span_is_empty(azure_iot->dps_operation_id))
          {
            azure_iot->state = azure_iot_state_error;
            LogError("Failed allocating memory for DPS operation id.");
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

        azure_iot->config->iot_hub_fqdn = az_span_split_copy(data_buffer, register_response.registration_state.assigned_hub_hostname, &data_buffer);
        
        if (az_span_is_empty(azure_iot->config->iot_hub_fqdn))
        {
          azure_iot->state = azure_iot_state_error;
          LogError("Failed saving IoT Hub fqdn from provisioning.");
          result = RESULT_ERROR;
        }
        else
        {
          azure_iot->config->device_id = az_span_split_copy(data_buffer, register_response.registration_state.device_id, &data_buffer);
                    
          if (az_span_is_empty(azure_iot->config->device_id))
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
    LogError("No PUBLISH notification expected");
    result = RESULT_ERROR;    
  }
  
  return result;
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
 * @brief           Initializes the Device Provisioning client and generates the config for a MQTT client.
 * @param[in]       azure_iot          A pointer to an initialized instance of azure_iot_t.
 * @param[in]       mqtt_client_config A pointer to a generic structure to contain the configuration for
 *                                     creating and connecting an MQTT client to Azure Device Provisioning service.
 * 
 * @return int      0 on success, non-zero if any failure occurs.
 */
static int get_mqtt_client_config_for_dps(azure_iot_t* azure_iot, mqtt_client_config_t* mqtt_client_config)
{
  az_span data_buffer_span, client_id_span, username_span, password_span;
  size_t client_id_length, username_length, password_length;
  az_result azrc;

  azrc = az_iot_provisioning_client_init(
      &azure_iot->dps_client,
      AZ_SPAN_FROM_STR(DPS_GLOBAL_ENDPOINT_MQTT_URI_WITH_PORT),
      azure_iot->config->dps_id_scope,
      azure_iot->config->dps_registration_id,
      NULL);
  EXIT_IF_AZ_FAILED(azrc, RESULT_ERROR, "Failed to initialize provisioning client.");

  data_buffer_span = azure_iot->data_buffer;
  
  password_span = az_span_split(data_buffer_span, MQTT_PASSWORD_BUFFER_SIZE, &data_buffer_span);
  EXIT_IF_TRUE(az_span_is_empty(password_span), RESULT_ERROR, "Failed allocating buffer for password_span.");

  password_length = generate_sas_token_for_dps(
    &azure_iot->dps_client,
    azure_iot->config->device_key,
    azure_iot->config->sas_token_lifetime_in_minutes,
    data_buffer_span,
    azure_iot->config->data_manipulation_functions,
    password_span,
    &azure_iot->sas_token_expiration_time);
  EXIT_IF_TRUE(password_length == 0, RESULT_ERROR, "Failed creating mqtt password for DPS connection.");    

  client_id_span = az_span_split(data_buffer_span, MQTT_CLIENT_ID_BUFFER_SIZE, &data_buffer_span);
  EXIT_IF_TRUE(az_span_is_empty(client_id_span), RESULT_ERROR, "Failed allocating buffer for client_id_span.");

  azrc = az_iot_provisioning_client_get_client_id(
      &azure_iot->dps_client, (char*)az_span_ptr(client_id_span), az_span_size(client_id_span), &client_id_length);
  EXIT_IF_AZ_FAILED(azrc, RESULT_ERROR, "Failed getting client id for DPS connection.");

  username_span = az_span_split(data_buffer_span, MQTT_USERNAME_BUFFER_SIZE, &data_buffer_span);

  azrc = az_iot_provisioning_client_get_user_name(
      &azure_iot->dps_client, (char*)az_span_ptr(username_span), az_span_size(username_span), &username_length);
  EXIT_IF_AZ_FAILED(azrc, RESULT_ERROR, "Failed to get MQTT client username.");

  mqtt_client_config->address = AZ_SPAN_FROM_STR(DPS_GLOBAL_ENDPOINT_FQDN);
  mqtt_client_config->port = DPS_GLOBAL_ENDPOINT_PORT;

  mqtt_client_config->client_id = client_id_span;
  mqtt_client_config->username = username_span;
  mqtt_client_config->password = password_span;

  return RESULT_OK;
}

/*
 * @brief           Initializes the Azure IoT Hub client and generates the config for a MQTT client.
 * @param[in]       azure_iot          A pointer to an initialized instance of azure_iot_t.
 * @param[in]       mqtt_client_config A pointer to a generic structure to contain the configuration for
 *                                     creating and connecting an MQTT client to Azure IoT Hub.
 * 
 * @return int      0 on success, non-zero if any failure occurs.
 */
static int get_mqtt_client_config_for_iot_hub(azure_iot_t* azure_iot, mqtt_client_config_t* mqtt_client_config)
{
  az_span data_buffer_span, client_id_span, username_span, password_span;
  size_t client_id_length, username_length, password_length;
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
  
  password_span = az_span_split(data_buffer_span, MQTT_PASSWORD_BUFFER_SIZE, &data_buffer_span);
  EXIT_IF_TRUE(az_span_is_empty(password_span), RESULT_ERROR, "Failed allocating buffer for password_span.");

  password_length = generate_sas_token_for_iot_hub(
    &azure_iot->iot_hub_client,
    azure_iot->config->device_key,
    azure_iot->config->sas_token_lifetime_in_minutes,
    data_buffer_span,
    azure_iot->config->data_manipulation_functions,
    password_span,
    &azure_iot->sas_token_expiration_time);
  EXIT_IF_TRUE(password_length == 0, RESULT_ERROR, "Failed creating mqtt password for IoT Hub connection.");    

  client_id_span = az_span_split(data_buffer_span, MQTT_CLIENT_ID_BUFFER_SIZE, &data_buffer_span);
  EXIT_IF_TRUE(az_span_is_empty(client_id_span), RESULT_ERROR, "Failed allocating buffer for client_id_span.");

  azrc = az_iot_hub_client_get_client_id(
      &azure_iot->iot_hub_client, (char*)az_span_ptr(client_id_span), az_span_size(client_id_span), &client_id_length);
  EXIT_IF_AZ_FAILED(azrc, RESULT_ERROR, "Failed getting client id for IoT Hub connection.");

  username_span = az_span_split(data_buffer_span, MQTT_USERNAME_BUFFER_SIZE, &data_buffer_span);

  azrc = az_iot_hub_client_get_user_name(
      &azure_iot->iot_hub_client, (char*)az_span_ptr(username_span), az_span_size(username_span), &username_length);
  EXIT_IF_AZ_FAILED(azrc, RESULT_ERROR, "Failed to get MQTT client username.");

  mqtt_client_config->address = azure_iot->config->iot_hub_fqdn;
  mqtt_client_config->port = IOT_HUB_ENDPOINT_PORT;

  mqtt_client_config->client_id = client_id_span;
  mqtt_client_config->username = username_span;
  mqtt_client_config->password = password_span;

  return RESULT_OK;
}

/*
 * @brief           Generates a SAS token used as password for connecting with Azure Device Provisioning.
 * @remarks         The SAS token generation depends on the following steps:
 *                  1. Calculate the expiration time, as current unix time since epoch plus the token duration, in minutes.
 *                  2. Generate the SAS signature;
 *                    a. Generate the DPS-specific secret string (a.k.a., "signature");
 *                    b. base64-decode the encryption key (device key);
 *                    c. Encrypt (HMAC-SHA256) the signature using the base64-decoded encryption key;
 *                    d. base64-encode the encrypted signature, which gives the final SAS signature (sig);
 *                  3. Compose the final SAS token with the DPS audience (sr), SAS signature (sig) and expiration time (se). 
 * @param[in]       provisioning_client         A pointer to an initialized instance of az_iot_provisioning_client.
 * @param[in]       device_key                  az_span containing the device key.
 * @param[in]       duration_in_minutes         Duration of the SAS token, in minutes.
 * @param[in]       data_buffer_span            az_span with a buffer containing enough space for all the 
 *                                              intermediate data generated by this function.
 * @param[in]       data_manipulation_functions Set of user-defined functions needed for the generation of the SAS token.
 * @param[out]      sas_token                   az_span with buffer where to write the resulting SAS token.
 * @param[out]      expiration_time             The expiration time of the resulting SAS token, as unix time.
 * 
 * @return int      Length of the resulting SAS token, or zero if any failure occurs.
 */
static int generate_sas_token_for_dps(
  az_iot_provisioning_client* provisioning_client,
  az_span device_key,
  unsigned int duration_in_minutes,
  az_span data_buffer_span,
  data_manipulation_functions_t data_manipulation_functions,
  az_span sas_token, uint32_t* expiration_time)
{
  int result;
  az_result rc;
  uint32_t current_unix_time;
  size_t mqtt_password_length, decoded_sas_key_length, length;
  az_span plain_sas_signature, sas_signature, decoded_sas_key, sas_hmac256_signed_signature;

  // Step 1.
  current_unix_time = get_current_unix_time();
  EXIT_IF_TRUE(current_unix_time == 0, 0, "Failed getting current unix time.");

  *expiration_time = current_unix_time + duration_in_minutes * 60;

  // Step 2.a.
  plain_sas_signature = az_span_split(data_buffer_span, PLAIN_SAS_SIGNATURE_BUFFER_SIZE, &data_buffer_span);
  EXIT_IF_TRUE(az_span_is_empty(plain_sas_signature), 0, "Failed allocating buffer for plain sas token.");

  rc = az_iot_provisioning_client_sas_get_signature(
      provisioning_client, *expiration_time, plain_sas_signature, &plain_sas_signature);
  EXIT_IF_AZ_FAILED(rc, 0, "Could not get the signature for SAS key");

  // Step 2.b.
  sas_signature = az_span_split(data_buffer_span, SAS_SIGNATURE_BUFFER_SIZE, &data_buffer_span);
  EXIT_IF_TRUE(az_span_is_empty(sas_signature), 0, "Failed allocating buffer for sas_signature.");
  
  decoded_sas_key = az_span_split(data_buffer_span, DECODED_SAS_KEY_BUFFER_SIZE, &data_buffer_span);
  EXIT_IF_TRUE(az_span_is_empty(decoded_sas_key), 0, "Failed allocating buffer for decoded_sas_key.");

  result = data_manipulation_functions.base64_decode(
    az_span_ptr(device_key), az_span_size(device_key), az_span_ptr(decoded_sas_key), az_span_size(decoded_sas_key), &decoded_sas_key_length);
  EXIT_IF_TRUE(result != 0, 0, "Failed decoding SAS key.");

  // Step 2.c.
  sas_hmac256_signed_signature = az_span_split(data_buffer_span, SAS_HMAC256_ENCRIPTED_SIGNATURE_BUFFER_SIZE, &data_buffer_span);
  EXIT_IF_TRUE(az_span_is_empty(sas_hmac256_signed_signature), 0, "Failed allocating buffer for sas_hmac256_signed_signature.");

  result = data_manipulation_functions.hmac_sha512_encrypt(
    az_span_ptr(decoded_sas_key), decoded_sas_key_length, 
    az_span_ptr(plain_sas_signature), az_span_size(plain_sas_signature), 
    az_span_ptr(sas_hmac256_signed_signature), az_span_size(sas_hmac256_signed_signature));
  EXIT_IF_TRUE(result != 0, 0, "Failed encrypting SAS signature.");

  // Step 2.d.
  result = data_manipulation_functions.base64_encode(
    az_span_ptr(sas_hmac256_signed_signature), az_span_size(sas_hmac256_signed_signature), az_span_ptr(sas_signature), az_span_size(sas_signature), &length);
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
 *                  1. Calculate the expiration time, as current unix time since epoch plus the token duration, in minutes.
 *                  2. Generate the SAS signature;
 *                    a. Generate the DPS-specific secret string (a.k.a., "signature");
 *                    b. base64-decode the encryption key (device key);
 *                    c. Encrypt (HMAC-SHA256) the signature using the base64-decoded encryption key;
 *                    d. base64-encode the encrypted signature, which gives the final SAS signature (sig);
 *                  3. Compose the final SAS token with the DPS audience (sr), SAS signature (sig) and expiration time (se). 
 * @param[in]       iot_hub_client              A pointer to an initialized instance of az_iot_hub_client.
 * @param[in]       device_key                  az_span containing the device key.
 * @param[in]       duration_in_minutes         Duration of the SAS token, in minutes.
 * @param[in]       data_buffer_span            az_span with a buffer containing enough space for all the 
 *                                              intermediate data generated by this function.
 * @param[in]       data_manipulation_functions Set of user-defined functions needed for the generation of the SAS token.
 * @param[out]      sas_token                   az_span with buffer where to write the resulting SAS token.
 * @param[out]      expiration_time             The expiration time of the resulting SAS token, as unix time.
 * 
 * @return int      Length of the resulting SAS token, or zero if any failure occurs.
 */
static int generate_sas_token_for_iot_hub(
  az_iot_hub_client* iot_hub_client,
  az_span device_key,
  unsigned int duration_in_minutes,
  az_span data_buffer_span,
  data_manipulation_functions_t data_manipulation_functions,
  az_span sas_token, uint32_t* expiration_time)
{
  int result;
  az_result rc;
  uint32_t current_unix_time;
  size_t mqtt_password_length, decoded_sas_key_length, length;
  az_span plain_sas_signature, sas_signature, decoded_sas_key, sas_hmac256_signed_signature;

  // Step 1.
  current_unix_time = get_current_unix_time();
  EXIT_IF_TRUE(current_unix_time == 0, 0, "Failed getting current unix time.");

  *expiration_time = current_unix_time + duration_in_minutes * 60;

  // Step 2.a.
  plain_sas_signature = az_span_split(data_buffer_span, PLAIN_SAS_SIGNATURE_BUFFER_SIZE, &data_buffer_span);
  EXIT_IF_TRUE(az_span_is_empty(plain_sas_signature), 0, "Failed allocating buffer for plain sas token.");

  rc = az_iot_hub_client_sas_get_signature(
      iot_hub_client, *expiration_time, plain_sas_signature, &plain_sas_signature);
  EXIT_IF_AZ_FAILED(rc, 0, "Could not get the signature for SAS key");

  // Step 2.b.
  sas_signature = az_span_split(data_buffer_span, SAS_SIGNATURE_BUFFER_SIZE, &data_buffer_span);
  EXIT_IF_TRUE(az_span_is_empty(sas_signature), 0, "Failed allocating buffer for sas_signature.");
  
  decoded_sas_key = az_span_split(data_buffer_span, DECODED_SAS_KEY_BUFFER_SIZE, &data_buffer_span);
  EXIT_IF_TRUE(az_span_is_empty(decoded_sas_key), 0, "Failed allocating buffer for decoded_sas_key.");

  result = data_manipulation_functions.base64_decode(
    az_span_ptr(device_key), az_span_size(device_key), az_span_ptr(decoded_sas_key), az_span_size(decoded_sas_key), &decoded_sas_key_length);
  EXIT_IF_TRUE(result != 0, 0, "Failed decoding SAS key.");

  // Step 2.c.
  sas_hmac256_signed_signature = az_span_split(data_buffer_span, SAS_HMAC256_ENCRIPTED_SIGNATURE_BUFFER_SIZE, &data_buffer_span);
  EXIT_IF_TRUE(az_span_is_empty(sas_hmac256_signed_signature), 0, "Failed allocating buffer for sas_hmac256_signed_signature.");

  result = data_manipulation_functions.hmac_sha512_encrypt(
    az_span_ptr(decoded_sas_key), decoded_sas_key_length, 
    az_span_ptr(plain_sas_signature), az_span_size(plain_sas_signature), 
    az_span_ptr(sas_hmac256_signed_signature), az_span_size(sas_hmac256_signed_signature));
  EXIT_IF_TRUE(result != 0, 0, "Failed encrypting SAS signature.");

  // Step 2.d.
  result = data_manipulation_functions.base64_encode(
    az_span_ptr(sas_hmac256_signed_signature), az_span_size(sas_hmac256_signed_signature), az_span_ptr(sas_signature), az_span_size(sas_signature), &length);
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
