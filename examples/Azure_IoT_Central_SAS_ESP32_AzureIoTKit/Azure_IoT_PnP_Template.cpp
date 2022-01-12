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

#define TELEMETRY_PROP_NAME_TEMPERATURE        "temperature"
#define TELEMETRY_PROP_NAME_HUMIDITY           "humidity"
#define TELEMETRY_PROP_NAME_LIGHT              "light"
#define TELEMETRY_PROP_NAME_PRESSURE           "pressure"
#define TELEMETRY_PROP_NAME_ALTITUDE           "altitude"
#define TELEMETRY_PROP_NAME_MAGNETOMETERX      "magnetometerX"
#define TELEMETRY_PROP_NAME_MAGNETOMETERY      "magnetometerY"
#define TELEMETRY_PROP_NAME_MAGNETOMETERZ      "magnetometerZ"
#define TELEMETRY_PROP_NAME_PITCH              "pitch"
#define TELEMETRY_PROP_NAME_ROLL               "roll"
#define TELEMETRY_PROP_NAME_ACCELEROMETERX     "accelerometerX"
#define TELEMETRY_PROP_NAME_ACCELEROMETERY     "accelerometerY"
#define TELEMETRY_PROP_NAME_ACCELEROMETERZ     "accelerometerZ"

#define DOUBLE_DECIMAL_PLACE_DIGITS 2

#define EXIT_IF_AZ_FAILED(azfn, message, ...)                         \
  do                                                                  \
  {                                                                   \
    az_result const result = (azfn);                                  \
                                                                      \
    if (az_result_failed(result))                                     \
    {                                                                 \
      Log(log_level_error, "Function returned az_result=%d", result); \
      Log(log_level_error, message, ##__VA_ARGS__ );                  \
      return __LINE__;                                                \
    }                                                                 \
  } while (0)

/* --- Data --- */
#define TOPIC_NAME_BUFFER_SIZE 128
static uint8_t topic_name_buffer[TOPIC_NAME_BUFFER_SIZE];

#define PAYLOAD_BUFFER_SIZE 1024
static uint8_t _payload_buffer[PAYLOAD_BUFFER_SIZE];
static uint32_t telemetry_send_count = 0;

static size_t telemetry_frequency_in_seconds = 10; // With default frequency of once in 10 seconds.
static time_t last_telemetry_send_time = INDEFINITE_TIME;

#define OLED_SPLASH_MESSAGE              "Azure IoT Central ESP32 Sample"

/* --- Function Prototypes --- */
/* Please find the function implementations at the bottom of this file */
static int generate_telemetry_payload(uint8_t* payload_buffer, size_t payload_buffer_size, size_t* payload_buffer_length);

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
  Log(log_level_info, "Telemetry frequency set to once every %d seconds.", telemetry_frequency_in_seconds);
}

/* Application-specific data section */
int azure_pnp_send_telemetry(azure_iot_t* azure_iot)
{
  time_t now = time(NULL);

  if (now == INDEFINITE_TIME)
  {
    Log(log_level_error, "Failed getting current time for controlling telemetry.");
    return RESULT_ERROR;
  }
  else if (last_telemetry_send_time == INDEFINITE_TIME ||
           difftime(now, last_telemetry_send_time) >= telemetry_frequency_in_seconds)
  {
    az_result rc;
    size_t payload_size;

    last_telemetry_send_time = now;

    if (generate_telemetry_payload(_payload_buffer, PAYLOAD_BUFFER_SIZE, &payload_size) != RESULT_OK)
    {
      Log(log_level_error, "Failed generating telemetry payload.");
      return RESULT_ERROR;
    }

    if (azure_iot_send_telemetry(azure_iot, _payload_buffer, payload_size) != 0)
    {
      Log(log_level_error, "Failed sending telemetry.");
      return RESULT_ERROR;
    }
  }

  return RESULT_OK;
}

/* --- Internal Functions --- */

static int generate_telemetry_payload(uint8_t* payload_buffer, size_t payload_buffer_size, size_t* payload_buffer_length)
{
  az_json_writer jw;
  az_result rc;
  az_span payoad_buffer_span = az_span_create(payload_buffer, payload_buffer_size);
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

  rc = az_json_writer_init(&jw, payoad_buffer_span, NULL);
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

  payoad_buffer_span = az_json_writer_get_bytes_used_in_destination(&jw);

  if ((payload_buffer_size - az_span_size(payoad_buffer_span)) < 1)
  {
    Log(log_level_error, "Insuficient space for telemetry payload null terminator.");
    return RESULT_ERROR;
  }

  payload_buffer[az_span_size(payoad_buffer_span)] = null_terminator;
  *payload_buffer_length = az_span_size(payoad_buffer_span) + sizeof(null_terminator);
 
  return RESULT_OK;
}
