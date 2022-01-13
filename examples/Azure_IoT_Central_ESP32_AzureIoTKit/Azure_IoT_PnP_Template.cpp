// Copyright (c) Microsoft Corporation. All rights reserved.
// SPDX-License-Identifier: MIT

#include <stdlib.h>
#include <stdarg.h>

#include <az_core.h>
#include <az_iot.h>

#include "AzureIoT.h"
#include "Azure_IoT_PnP_Template.h"
#include "esp32_azureiotkit_sensors.h"

/* --- Defines --- */
#define AZURE_PNP_MODEL_ID "dtmi:azureiot:devkit:freertos:Esp32AzureIotKit;1"

#define SAMPLE_DEVICE_INFORMATION_NAME                 "deviceInformation"
#define SAMPLE_MANUFACTURER_PROPERTY_NAME              "manufacturer"
#define SAMPLE_MODEL_PROPERTY_NAME                     "model"
#define SAMPLE_SOFTWARE_VERSION_PROPERTY_NAME          "swVersion"
#define SAMPLE_OS_NAME_PROPERTY_NAME                   "osName"
#define SAMPLE_PROCESSOR_ARCHITECTURE_PROPERTY_NAME    "processorArchitecture"
#define SAMPLE_PROCESSOR_MANUFACTURER_PROPERTY_NAME    "processorManufacturer"
#define SAMPLE_TOTAL_STORAGE_PROPERTY_NAME             "totalStorage"
#define SAMPLE_TOTAL_MEMORY_PROPERTY_NAME              "totalMemory"

#define SAMPLE_MANUFACTURER_PROPERTY_VALUE             "ESPRESSIF"
#define SAMPLE_MODEL_PROPERTY_VALUE                    "ESP32 Azure IoT Kit"
#define SAMPLE_VERSION_PROPERTY_VALUE                  "1.0.0"
#define SAMPLE_OS_NAME_PROPERTY_VALUE                  "FreeRTOS"
#define SAMPLE_ARCHITECTURE_PROPERTY_VALUE             "ESP32 WROVER-B"
#define SAMPLE_PROCESSOR_MANUFACTURER_PROPERTY_VALUE   "ESPRESSIF"
// The next couple properties are in KiloBytes.
#define SAMPLE_TOTAL_STORAGE_PROPERTY_VALUE            4096
#define SAMPLE_TOTAL_MEMORY_PROPERTY_VALUE             8192

#define TELEMETRY_PROP_NAME_TEMPERATURE                "temperature"
#define TELEMETRY_PROP_NAME_HUMIDITY                   "humidity"
#define TELEMETRY_PROP_NAME_LIGHT                      "light"
#define TELEMETRY_PROP_NAME_PRESSURE                   "pressure"
#define TELEMETRY_PROP_NAME_ALTITUDE                   "altitude"
#define TELEMETRY_PROP_NAME_MAGNETOMETERX              "magnetometerX"
#define TELEMETRY_PROP_NAME_MAGNETOMETERY              "magnetometerY"
#define TELEMETRY_PROP_NAME_MAGNETOMETERZ              "magnetometerZ"
#define TELEMETRY_PROP_NAME_PITCH                      "pitch"
#define TELEMETRY_PROP_NAME_ROLL                       "roll"
#define TELEMETRY_PROP_NAME_ACCELEROMETERX             "accelerometerX"
#define TELEMETRY_PROP_NAME_ACCELEROMETERY             "accelerometerY"
#define TELEMETRY_PROP_NAME_ACCELEROMETERZ             "accelerometerZ"

#define DOUBLE_DECIMAL_PLACE_DIGITS 2

/* --- Function Checks and Returns --- */
#define RESULT_OK       0
#define RESULT_ERROR    __LINE__

#define EXIT_IF_TRUE(condition, retcode, message, ...)                              \
  do                                                                                \
  {                                                                                 \
    if (condition)                                                                  \
    {                                                                               \
      LogError(message, ##__VA_ARGS__ );                                            \
      return retcode;                                                               \
    }                                                                               \
  } while (0)

#define EXIT_IF_AZ_FAILED(azresult, message, ...)                                   \
  EXIT_IF_TRUE(az_result_failed(azresult), RESULT_ERROR, message, ##__VA_ARGS__ )

/* --- Data --- */
#define DATA_BUFFER_SIZE 1024
static uint8_t data_buffer[DATA_BUFFER_SIZE];
static uint32_t telemetry_send_count = 0;

static size_t telemetry_frequency_in_seconds = 10; // With default frequency of once in 10 seconds.
static time_t last_telemetry_send_time = INDEFINITE_TIME;

#define OLED_SPLASH_MESSAGE              "Azure IoT Central ESP32 Sample"

/* --- Function Prototypes --- */
/* Please find the function implementations at the bottom of this file */
static int generate_telemetry_payload(uint8_t* payload_buffer, size_t payload_buffer_size, size_t* payload_buffer_length);
static int generate_device_info_payload(az_iot_hub_client const* hub_client, uint8_t* payload_buffer, size_t payload_buffer_size, size_t* payload_buffer_length);

/* --- Public Functions --- */
void azure_pnp_init()
{
  esp32_azureiotkit_initialize_sensors();

  esp32_azureiotkit_oled_clean_screen();
  esp32_azureiotkit_oled_show_message((uint8_t*)OLED_SPLASH_MESSAGE, lengthof(OLED_SPLASH_MESSAGE));
}

const az_span azure_pnp_get_model_id()
{
  return AZ_SPAN_FROM_STR(AZURE_PNP_MODEL_ID);
}

void azure_pnp_set_telemetry_frequency(size_t frequency_in_seconds)
{
  telemetry_frequency_in_seconds = frequency_in_seconds;
  LogInfo("Telemetry frequency set to once every %d seconds.", telemetry_frequency_in_seconds);
}

/* Application-specific data section */

int azure_pnp_send_telemetry(azure_iot_t* azure_iot)
{
  time_t now = time(NULL);

  if (now == INDEFINITE_TIME)
  {
    LogError("Failed getting current time for controlling telemetry.");
    return RESULT_ERROR;
  }
  else if (last_telemetry_send_time == INDEFINITE_TIME ||
           difftime(now, last_telemetry_send_time) >= telemetry_frequency_in_seconds)
  {
    size_t payload_size;

    last_telemetry_send_time = now;

    if (generate_telemetry_payload(data_buffer, DATA_BUFFER_SIZE, &payload_size) != RESULT_OK)
    {
      LogError("Failed generating telemetry payload.");
      return RESULT_ERROR;
    }

    if (azure_iot_send_telemetry(azure_iot, data_buffer, payload_size) != 0)
    {
      LogError("Failed sending telemetry.");
      return RESULT_ERROR;
    }
  }

  return RESULT_OK;
}

int azure_pnp_send_device_info(azure_iot_t* azure_iot, uint32_t request_id)
{
  int result;
  size_t length;  
    
  result = generate_device_info_payload(&azure_iot->iot_hub_client, data_buffer, DATA_BUFFER_SIZE, &length);
  EXIT_IF_TRUE(result != RESULT_OK, RESULT_ERROR, "Failed generating telemetry payload.");

  result = azure_iot_send_properties_update(azure_iot, request_id, data_buffer, length);
  EXIT_IF_TRUE(result != RESULT_OK, RESULT_ERROR, "Failed sending reported properties update.");

  return RESULT_OK;
}

int azure_pnp_update_properties(azure_iot_t* azure_iot)
{
  int result = RESULT_OK;

  return result;
}


/* --- Internal Functions --- */

static int generate_telemetry_payload(uint8_t* payload_buffer, size_t payload_buffer_size, size_t* payload_buffer_length)
{
  az_json_writer jw;
  az_result rc;
  az_span payload_buffer_span = az_span_create(payload_buffer, payload_buffer_size);
  az_span json_span;
  float temperature, humidity, light, pressure, altitude;
  int32_t magneticFieldX, magneticFieldY, magneticFieldZ;
  int32_t pitch, roll, accelerationX, accelerationY, accelerationZ;

  // Acquiring data from Espressif's ESP32 Azure IoT Kit sensors.
  temperature = esp32_azureiotkit_get_temperature();
  humidity = esp32_azureiotkit_get_humidity();
  light = esp32_azureiotkit_get_ambientLight();
  esp32_azureiotkit_get_pressure_altitude(&pressure, &altitude);
  esp32_azureiotkit_get_magnetometer(&magneticFieldX, &magneticFieldY, &magneticFieldZ);
  esp32_azureiotkit_get_pitch_roll_accel(&pitch, &roll, &accelerationX, &accelerationY, &accelerationZ);

  rc = az_json_writer_init(&jw, payload_buffer_span, NULL);
  EXIT_IF_AZ_FAILED(rc, "Failed initializing json writer for telemetry.");

  rc = az_json_writer_append_begin_object(&jw);
  EXIT_IF_AZ_FAILED(rc, "Failed setting telemetry json root.");

  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR(TELEMETRY_PROP_NAME_TEMPERATURE));
  EXIT_IF_AZ_FAILED(rc, "Failed adding temperature property name to telemetry payload.");
  rc = az_json_writer_append_double(&jw, temperature, DOUBLE_DECIMAL_PLACE_DIGITS);
  EXIT_IF_AZ_FAILED(rc, "Failed adding temperature property value to telemetry payload. ");

  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR(TELEMETRY_PROP_NAME_HUMIDITY));
  EXIT_IF_AZ_FAILED(rc, "Failed adding humidity property name to telemetry payload.");
  rc = az_json_writer_append_double(&jw, humidity, DOUBLE_DECIMAL_PLACE_DIGITS);
  EXIT_IF_AZ_FAILED(rc, "Failed adding humidity property value to telemetry payload. ");

  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR(TELEMETRY_PROP_NAME_LIGHT));
  EXIT_IF_AZ_FAILED(rc, "Failed adding light property name to telemetry payload.");
  rc = az_json_writer_append_double(&jw, light, DOUBLE_DECIMAL_PLACE_DIGITS);
  EXIT_IF_AZ_FAILED(rc, "Failed adding light property value to telemetry payload.");

  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR(TELEMETRY_PROP_NAME_PRESSURE));
  EXIT_IF_AZ_FAILED(rc, "Failed adding pressure property name to telemetry payload.");
  rc = az_json_writer_append_double(&jw, pressure, DOUBLE_DECIMAL_PLACE_DIGITS);
  EXIT_IF_AZ_FAILED(rc, "Failed adding pressure property value to telemetry payload.");

  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR(TELEMETRY_PROP_NAME_ALTITUDE));
  EXIT_IF_AZ_FAILED(rc, "Failed adding altitude property name to telemetry payload.");
  rc = az_json_writer_append_double(&jw, altitude, DOUBLE_DECIMAL_PLACE_DIGITS);
  EXIT_IF_AZ_FAILED(rc, "Failed adding altitude property value to telemetry payload.");

  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR(TELEMETRY_PROP_NAME_MAGNETOMETERX));
  EXIT_IF_AZ_FAILED(rc, "Failed adding magnetometer(X) property name to telemetry payload.");
  rc = az_json_writer_append_int32(&jw, magneticFieldX);
  EXIT_IF_AZ_FAILED(rc, "Failed adding magnetometer(X) property value to telemetry payload.");

  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR(TELEMETRY_PROP_NAME_MAGNETOMETERY));
  EXIT_IF_AZ_FAILED(rc, "Failed adding magnetometer(Y) property name to telemetry payload.");
  rc = az_json_writer_append_int32(&jw, magneticFieldY);
  EXIT_IF_AZ_FAILED(rc, "Failed adding magnetometer(Y) property value to telemetry payload.");

  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR(TELEMETRY_PROP_NAME_MAGNETOMETERZ));
  EXIT_IF_AZ_FAILED(rc, "Failed adding magnetometer(Z) property name to telemetry payload.");
  rc = az_json_writer_append_int32(&jw, magneticFieldZ);
  EXIT_IF_AZ_FAILED(rc, "Failed adding magnetometer(Z) property value to telemetry payload.");

  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR(TELEMETRY_PROP_NAME_PITCH));
  EXIT_IF_AZ_FAILED(rc, "Failed adding pitch property name to telemetry payload.");
  rc = az_json_writer_append_int32(&jw, pitch);
  EXIT_IF_AZ_FAILED(rc, "Failed adding pitch property value to telemetry payload.");

  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR(TELEMETRY_PROP_NAME_ROLL));
  EXIT_IF_AZ_FAILED(rc, "Failed adding roll property name to telemetry payload.");
  rc = az_json_writer_append_int32(&jw, roll);
  EXIT_IF_AZ_FAILED(rc, "Failed adding roll property value to telemetry payload.");

  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR(TELEMETRY_PROP_NAME_ACCELEROMETERX));
  EXIT_IF_AZ_FAILED(rc, "Failed adding acceleration(X) property name to telemetry payload.");
  rc = az_json_writer_append_int32(&jw, accelerationX);
  EXIT_IF_AZ_FAILED(rc, "Failed adding acceleration(X) property value to telemetry payload.");

  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR(TELEMETRY_PROP_NAME_ACCELEROMETERY));
  EXIT_IF_AZ_FAILED(rc, "Failed adding acceleration(Y) property name to telemetry payload.");
  rc = az_json_writer_append_int32(&jw, accelerationY);
  EXIT_IF_AZ_FAILED(rc, "Failed adding acceleration(Y) property value to telemetry payload.");

  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR(TELEMETRY_PROP_NAME_ACCELEROMETERZ));
  EXIT_IF_AZ_FAILED(rc, "Failed adding acceleration(Z) property name to telemetry payload.");
  rc = az_json_writer_append_int32(&jw, accelerationZ);
  EXIT_IF_AZ_FAILED(rc, "Failed adding acceleration(Z) property value to telemetry payload.");

  rc = az_json_writer_append_end_object(&jw);
  EXIT_IF_AZ_FAILED(rc, "Failed closing telemetry json payload.");

  payload_buffer_span = az_json_writer_get_bytes_used_in_destination(&jw);

  if ((payload_buffer_size - az_span_size(payload_buffer_span)) < 1)
  {
    LogError("Insuficient space for telemetry payload null terminator.");
    return RESULT_ERROR;
  }

  payload_buffer[az_span_size(payload_buffer_span)] = null_terminator;
  *payload_buffer_length = az_span_size(payload_buffer_span);
 
  return RESULT_OK;
}

static int generate_device_info_payload(az_iot_hub_client const* hub_client, uint8_t* payload_buffer, size_t payload_buffer_size, size_t* payload_buffer_length)
{
  az_json_writer jw;
  az_result rc;
  az_span payload_buffer_span = az_span_create(payload_buffer, payload_buffer_size);
  az_span json_span;

  rc = az_json_writer_init(&jw, payload_buffer_span, NULL);
  EXIT_IF_AZ_FAILED(rc, "Failed initializing json writer for telemetry.");

  rc = az_json_writer_append_begin_object(&jw);
  EXIT_IF_AZ_FAILED(rc, "Failed setting telemetry json root.");
  
  rc = az_iot_hub_client_properties_writer_begin_component(
    hub_client, &jw, AZ_SPAN_FROM_STR(SAMPLE_DEVICE_INFORMATION_NAME));
  EXIT_IF_AZ_FAILED(rc, "Failed writting component name.");

  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR(SAMPLE_MANUFACTURER_PROPERTY_NAME));
  EXIT_IF_AZ_FAILED(rc, "Failed adding SAMPLE_MANUFACTURER_PROPERTY_NAME to payload.");
  rc = az_json_writer_append_string(&jw, AZ_SPAN_FROM_STR(SAMPLE_MANUFACTURER_PROPERTY_VALUE));
  EXIT_IF_AZ_FAILED(rc, "Failed adding SAMPLE_MANUFACTURER_PROPERTY_VALUE to payload. ");

  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR(SAMPLE_MODEL_PROPERTY_NAME));
  EXIT_IF_AZ_FAILED(rc, "Failed adding SAMPLE_MODEL_PROPERTY_NAME to payload.");
  rc = az_json_writer_append_string(&jw, AZ_SPAN_FROM_STR(SAMPLE_MODEL_PROPERTY_VALUE));
  EXIT_IF_AZ_FAILED(rc, "Failed adding SAMPLE_MODEL_PROPERTY_VALUE to payload. ");

  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR(SAMPLE_SOFTWARE_VERSION_PROPERTY_NAME));
  EXIT_IF_AZ_FAILED(rc, "Failed adding SAMPLE_SOFTWARE_VERSION_PROPERTY_NAME to payload.");
  rc = az_json_writer_append_string(&jw, AZ_SPAN_FROM_STR(SAMPLE_VERSION_PROPERTY_VALUE));
  EXIT_IF_AZ_FAILED(rc, "Failed adding SAMPLE_VERSION_PROPERTY_VALUE to payload. ");

  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR(SAMPLE_OS_NAME_PROPERTY_NAME));
  EXIT_IF_AZ_FAILED(rc, "Failed adding SAMPLE_OS_NAME_PROPERTY_NAME to payload.");
  rc = az_json_writer_append_string(&jw, AZ_SPAN_FROM_STR(SAMPLE_OS_NAME_PROPERTY_VALUE));
  EXIT_IF_AZ_FAILED(rc, "Failed adding SAMPLE_OS_NAME_PROPERTY_VALUE to payload. ");

  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR(SAMPLE_PROCESSOR_ARCHITECTURE_PROPERTY_NAME));
  EXIT_IF_AZ_FAILED(rc, "Failed adding SAMPLE_PROCESSOR_ARCHITECTURE_PROPERTY_NAME to payload.");
  rc = az_json_writer_append_string(&jw, AZ_SPAN_FROM_STR(SAMPLE_ARCHITECTURE_PROPERTY_VALUE));
  EXIT_IF_AZ_FAILED(rc, "Failed adding SAMPLE_ARCHITECTURE_PROPERTY_VALUE to payload. ");

  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR(SAMPLE_PROCESSOR_MANUFACTURER_PROPERTY_NAME));
  EXIT_IF_AZ_FAILED(rc, "Failed adding SAMPLE_PROCESSOR_MANUFACTURER_PROPERTY_NAME to payload.");
  rc = az_json_writer_append_string(&jw, AZ_SPAN_FROM_STR(SAMPLE_PROCESSOR_MANUFACTURER_PROPERTY_VALUE));
  EXIT_IF_AZ_FAILED(rc, "Failed adding SAMPLE_PROCESSOR_MANUFACTURER_PROPERTY_VALUE to payload. ");

  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR(SAMPLE_TOTAL_STORAGE_PROPERTY_NAME));
  EXIT_IF_AZ_FAILED(rc, "Failed adding SAMPLE_TOTAL_STORAGE_PROPERTY_NAME to payload.");
  rc = az_json_writer_append_double(&jw, SAMPLE_TOTAL_STORAGE_PROPERTY_VALUE, DOUBLE_DECIMAL_PLACE_DIGITS);
  EXIT_IF_AZ_FAILED(rc, "Failed adding SAMPLE_TOTAL_STORAGE_PROPERTY_VALUE to payload. ");

  rc = az_json_writer_append_property_name(&jw, AZ_SPAN_FROM_STR(SAMPLE_TOTAL_MEMORY_PROPERTY_NAME));
  EXIT_IF_AZ_FAILED(rc, "Failed adding SAMPLE_TOTAL_MEMORY_PROPERTY_NAME to payload.");
  rc = az_json_writer_append_double(&jw, SAMPLE_TOTAL_MEMORY_PROPERTY_VALUE, DOUBLE_DECIMAL_PLACE_DIGITS);
  EXIT_IF_AZ_FAILED(rc, "Failed adding SAMPLE_TOTAL_MEMORY_PROPERTY_VALUE to payload. ");

  rc = az_iot_hub_client_properties_writer_end_component(hub_client, &jw);
  EXIT_IF_AZ_FAILED(rc, "Failed closing component object.");

  rc = az_json_writer_append_end_object(&jw);
  EXIT_IF_AZ_FAILED(rc, "Failed closing telemetry json payload.");

  payload_buffer_span = az_json_writer_get_bytes_used_in_destination(&jw);

  if ((payload_buffer_size - az_span_size(payload_buffer_span)) < 1)
  {
    LogError("Insuficient space for telemetry payload null terminator.");
    return RESULT_ERROR;
  }

  payload_buffer[az_span_size(payload_buffer_span)] = null_terminator;
  *payload_buffer_length = az_span_size(payload_buffer_span);
 
  return RESULT_OK;
}
