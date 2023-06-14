---
page_type: sample
description: Connecting an ESP32 device to Azure IoT using the Azure SDK for Embedded C
languages:
- c
products:
- azure-iot
- azure-iot-pnp
- azure-iot-dps
- azure-iot-hub
---

> ## Before you proceed
> 
>_This sample is based on the Azure IoT SDK for C, uses a bare metal (no RTOS) approach and has support for Arduino IDE._
> 
>_There is a different sample for the ESP32 with the Azure IoT middleware for FreeRTOS (requires FreeRTOS to work)._
> 
> _If this is what you’re looking for please visit  [this GitHub repo](https://github.com/Azure-Samples/iot-middleware-freertos-samples/blob/main/README.md)_.

# How to Setup and Run Azure SDK for Embedded C IoT Hub Client on Espressif ESP32

  - [How to Setup and Run Azure SDK for Embedded C IoT Hub Client on Espressif ESP32](#how-to-setup-and-run-azure-sdk-for-embedded-c-iot-hub-client-on-espressif-esp32)
  - [Introduction](#introduction)
  - [What is Covered](#what-is-covered)
  - [Prerequisites](#prerequisites)
  - [Setup and Run Instructions](#setup-and-run-instructions)
  - [Troubleshooting](#troubleshooting)
  - [Contributing](#contributing)
  - [License](#license)

## Introduction

This is a "to-the-point" guide outlining how to run an Azure SDK for Embedded C IoT Hub telemetry sample on an ESP32 microcontroller. The command line examples are tailored to Debian/Ubuntu environments.

### What is Covered

- Configuration instructions for the Arduino IDE to compile a sample using the [Azure SDK for Embedded C](https://github.com/Azure/azure-sdk-for-c).
- Configuration, build, and run instructions for the IoT Hub telemetry sample.

_The following was run on Windows 11, with Arduino IDE 2.1.0 and ESP32 board library version 2.0.9._

## Prerequisites

- Have an [Azure account](https://azure.microsoft.com/) created.
- Have an [Azure IoT Hub](https://docs.microsoft.com/azure/iot-hub/iot-hub-create-through-portal) created.
- Have a logical device created in your Azure IoT Hub: using authentication type "Symmetric Key" or "X.509 self-signed".   
    - **Symmetric Key**: follow [this guidance](https://docs.microsoft.com/azure/iot-hub/iot-hub-create-through-portal#register-a-new-device-in-the-iot-hub) to create a device.In this case, the device keys are used to automatically generate a SAS token for authentication.
    - **X.509 self-signed cert**: Instructions on how to create an X.509 cert for tests can be found [here](https://github.com/Azure/azure-sdk-for-c/blob/main/sdk/samples/iot/docs/how_to_iot_hub_samples_linux.md#configure-and-run-the-samples) (Step 1). Please note that you might need to install some of the [prerequisites](https://github.com/Azure/azure-sdk-for-c/blob/main/sdk/samples/iot/docs/how_to_iot_hub_samples_linux.md#prerequisites) like OpenSSL.
- Have the latest [Arduino IDE](https://www.arduino.cc/en/Main/Software) installed.

- Have the [ESP32 board support](https://github.com/espressif/arduino-esp32) installed on Arduino IDE.

    - ESP32 boards are not natively supported by Arduino IDE, so you need to add them manually.
    - Follow the [instructions](https://github.com/espressif/arduino-esp32) in the official ESP32 repository.
    - If your ESP32 board is not recognized and a COM port is not mapped by your computer, try installing the [serial port drivers](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/establish-serial-connection.html#connect-esp32-to-pc) recommended by ESPRESSIF.

- Have one of the following interfaces to your Azure IoT Hub set up:
  - [Azure Command Line Interface](https://docs.microsoft.com/cli/azure/install-azure-cli?view=azure-cli-latest) (Azure CLI) utility installed, along with the [Azure IoT CLI extension](https://github.com/Azure/azure-iot-cli-extension).

    On Windows:

      Download and install: https://aka.ms/installazurecliwindows

      ```powershell
      PS C:\>az extension add --name azure-iot
      ```

    On Linux:

      ```bash
      $ curl -sL https://aka.ms/InstallAzureCLIDeb | sudo bash
      $ az extension add --name azure-iot
      ```

      A list of all the Azure IoT CLI extension commands can be found [here](https://docs.microsoft.com/cli/azure/iot?view=azure-cli-latest).

  - The most recent version of [Azure IoT Explorer](https://github.com/Azure/azure-iot-explorer/releases) installed. More instruction on its usage can be found [here](https://docs.microsoft.com/azure/iot-pnp/howto-use-iot-explorer).

  NOTE: This guide demonstrates use of the Azure CLI and does NOT demonstrate use of Azure IoT Explorer.

## Setup and Run Instructions

1. Run the Arduino IDE.

2. Install the Azure SDK for Embedded C library.

    - On the Arduino IDE, go to menu `Sketch`, `Include Library`, `Manage Libraries...`.
    - Search for and install `azure-sdk-for-c`.

3. Open the ESPRESSIF ESP32 sample.

    - On the Arduino IDE, go to menu `File`, `Examples`, `azure-sdk-for-c`.
    - Click on `az_esp32` to open the sample.

4. Configure the ESPRESSIF ESP32 sample.

    Enter your Azure IoT Hub and device information into the sample's `iot_configs.h`:
    - Add your Wi-Fi SSID to `IOT_CONFIG_WIFI_SSID`
    - Add your Wi-Fi password to `IOT_CONFIG_WIFI_PASSWORD`
    - Add you IoTHub Name to `IOT_CONFIG_IOTHUB_FQDN`
    - Add your Device ID to `IOT_CONFIG_DEVICE_ID`
    - If using **X.509 Cert**:
        - Uncomment the `#define IOT_CONFIG_USE_X509_CERT`
        - Add your cert to `IOT_CONFIG_USE_X509_CERT`
        - Add your cert PK to `IOT_CONFIG_DEVICE_CERT_PRIVATE_KEY`
    - If using **Symmetric Key**:
        - Add your device key to `IOT_CONFIG_DEVICE_KEY`

5. Connect the ESP32 microcontroller to your USB port.

6. On the Arduino IDE, select the board and port.

    - Go to menu `Tools`, `Board` and select `ESP32`.
    - Go to menu `Tools`, `Port` and select the port to which the microcontroller is connected.

7. Upload the sketch.

    - Go to menu `Sketch` and click on `Upload`.

        <details><summary><i>Expected output of the upload:</i></summary>
        <p>

        ```text
        Executable segment sizes:
        IROM   : 361788          - code in flash         (default or ICACHE_FLASH_ATTR)
        IRAM   : 26972   / 32768 - code in IRAM          (ICACHE_RAM_ATTR, ISRs...)
        DATA   : 1360  )         - initialized variables (global, static) in RAM/HEAP
        RODATA : 2152  ) / 81920 - constants             (global, static) in RAM/HEAP
        BSS    : 26528 )         - zeroed variables      (global, static) in RAM/HEAP
        Sketch uses 392272 bytes (37%) of program storage space. Maximum is 1044464 bytes.
        Global variables use 30040 bytes (36%) of dynamic memory, leaving 51880 bytes for local variables. Maximum is 81920 bytes.
        /home/user/.arduino15/packages/esp8266/tools/python3/3.7.2-post1/python3 /home/user/.arduino15/packages/esp8266/hardware/esp8266/2.7.1/tools/upload.py --chip esp8266 --port /dev/ttyUSB0 --baud 230400 --before default_reset --after hard_reset write_flash 0x0 /tmp/arduino_build_826987/azure_iot_hub_telemetry.ino.bin
        esptool.py v2.8
        Serial port /dev/ttyUSB0
        Connecting....
        Chip is ESP8266EX
        Features: WiFi
        Crystal is 26MHz
        MAC: dc:4f:22:5e:a7:09
        Uploading stub...
        Running stub...
        Stub running...
        Changing baud rate to 230400
        Changed.
        Configuring flash size...
        Auto-detected Flash size: 4MB
        Compressed 396432 bytes to 292339...

        Writing at 0x00000000... (5 %)
        Writing at 0x00004000... (11 %)
        Writing at 0x00008000... (16 %)
        Writing at 0x0000c000... (22 %)
        Writing at 0x00010000... (27 %)
        Writing at 0x00014000... (33 %)
        Writing at 0x00018000... (38 %)
        Writing at 0x0001c000... (44 %)
        Writing at 0x00020000... (50 %)
        Writing at 0x00024000... (55 %)
        Writing at 0x00028000... (61 %)
        Writing at 0x0002c000... (66 %)
        Writing at 0x00030000... (72 %)
        Writing at 0x00034000... (77 %)
        Writing at 0x00038000... (83 %)
        Writing at 0x0003c000... (88 %)
        Writing at 0x00040000... (94 %)
        Writing at 0x00044000... (100 %)
        Wrote 396432 bytes (292339 compressed) at 0x00000000 in 13.0 seconds (effective 243.4 kbit/s)...
        Hash of data verified.

        Leaving...
        Hard resetting via RTS pin...
        ```

        </p>
        </details>

8. Monitor the MCU (microcontroller) locally via the Serial Port.

    - Go to menu `Tools`, `Serial Monitor`.

        If you perform this step right away after uploading the sketch, the serial monitor will show an output similar to the following upon success:

        ```text
        Connecting to WIFI SSID buckaroo
        .......................WiFi connected, IP address:
        192.168.1.123
        Setting time using SNTP..............................done!
        Current time: Thu May 28 02:55:05 2020
        Client ID: mydeviceid
        Username: myiothub.azure-devices.net/mydeviceid/?api-version=2018-06-30&DeviceClientType=c%2F1.0.0
        SharedAccessSignature sr=myiothub.azure-devices.net%2Fdevices%2Fmydeviceid&sig=placeholder-password&se=1590620105
        MQTT connecting ... connected.
        ```

9. Monitor the telemetry messages sent to the Azure IoT Hub using the connection string for the policy name `iothubowner` found under "Shared access policies" on your IoT Hub in the Azure portal.

    ```bash
    $ az iot hub monitor-events --login <your Azure IoT Hub owner connection string in quotes> --device-id <your device id>
    ```

    <details><summary><i>Expected telemetry output:</i></summary>
    <p>

    ```bash
    Starting event monitor, filtering on device: mydeviceid, use ctrl-c to stop...
    {
        "event": {
            "origin": "mydeviceid",
            "payload": "payload"
        }
    }
    {
        "event": {
            "origin": "mydeviceid",
            "payload": "payload"
        }
    }
    {
        "event": {
            "origin": "mydeviceid",
            "payload": "payload"
        }
    }
    {
        "event": {
            "origin": "mydeviceid",
            "payload": "payload"
        }
    }
    {
        "event": {
            "origin": "mydeviceid",
            "payload": "payload"
        }
    }
    {
        "event": {
            "origin": "mydeviceid",
            "payload": "payload"
        }
    }
    ^CStopping event monitor...
    ```

    </p>
    </details>

## Certificates - Important to know

The Azure IoT service certificates presented during TLS negotiation shall be always validated, on the device, using the appropriate trusted root CA certificate(s).

The Azure SDK for C Arduino library automatically installs the root certificate used in the United States regions, and adds it to the Arduino sketch project when the library is included.

For other regions (and private cloud environments), please use the appropriate root CA certificate.

### Additional Information

For important information and additional guidance about certificates, please refer to [this blog post](https://techcommunity.microsoft.com/t5/internet-of-things/azure-iot-tls-changes-are-coming-and-why-you-should-care/ba-p/1658456) from the security team.

## Troubleshooting

- The error policy for the Embedded C SDK client library is documented [here](https://github.com/Azure/azure-sdk-for-c/blob/main/sdk/docs/iot/mqtt_state_machine.md#error-policy).
- File an issue via [Github Issues](https://github.com/Azure/azure-sdk-for-c/issues/new/choose).
- Check [previous questions](https://stackoverflow.com/questions/tagged/azure+c) or ask new ones on StackOverflow using the `azure` and `c` tags.

## Contributing

This project welcomes contributions and suggestions. Find more contributing details [here](https://github.com/Azure/azure-sdk-for-c/blob/main/CONTRIBUTING.md).

### License

Azure SDK for Embedded C is licensed under the [MIT](https://github.com/Azure/azure-sdk-for-c/blob/main/LICENSE) license.
