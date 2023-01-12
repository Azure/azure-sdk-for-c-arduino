---
page_type: sample
description: Connecting Arduino Nano RP2040 Connect to Azure IoT Hub using the Azure SDK for C Arduino library
languages:
- c
products:
- azure-iot
- azure-iot-pnp
- azure-iot-dps
- azure-iot-hub
---

# Getting started with the Arduino Nano RP2040 Connect and Embedded C IoT Hub Client with Azure SDK for C Arduino library

**Total completion time**:  30 minutes

- [Getting started with the Arduino Nano RP2040 Connect and Embedded C IoT Hub Client with Azure SDK for C Arduino library](#getting-started-with-the-arduino-nano-rp2040-connect-and-embedded-c-iot-hub-client-with-azure-sdk-for-c-arduino-library)
  - [Introduction](#introduction)
    - [What is Covered](#what-is-covered)
  - [Prerequisites](#prerequisites)
  - [IoT Hub Device Setup](#iot-hub-device-setup)
  - [Arduino IDE Setup](#arduino-ide-setup)
  - [Run the Sample](#run-the-sample)
  - [Troubleshooting](#troubleshooting)
    - [Additional Help](#additional-help)
  - [Certificates - Important to know](#certificates---important-to-know)
    - [Additional Information](#additional-information)
  - [Contributing](#contributing)
    - [License](#license)

## Introduction

In this tutorial you will use the Azure SDK for C to connect the Arduino Nano RP2040 Connect to Azure IoT Hub. The article is part of the series [IoT Device Development](https://go.microsoft.com/fwlink/p/?linkid=2129824). The series introduces device developers to the Azure SDK for C, and shows how to connect several device evaluation kits to Azure IoT.

### What is Covered
You will complete the following tasks:

* Install the Azure SDK for C library on Arduino
* Build the image and flash it onto the Nano RP2040 Connect
* Use Azure IoT Hub to create and view device telemetry

_The following was run on Windows 11 and WSL Ubuntu Desktop 20.04 environments, with Arduino IDE 1.8.15 and Arduino Arduino Nano RP2040 Connect with headers._

## Prerequisites

- Have an [Azure account](https://azure.microsoft.com/) created.
- Have an [Azure IoT Hub](https://docs.microsoft.com/azure/iot-hub/iot-hub-create-through-portal) created.
- Have the latest [Arduino IDE](https://www.arduino.cc/en/Main/Software) installed.
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

  - **Optional**: The most recent version of [Azure IoT Explorer](https://github.com/Azure/azure-iot-explorer/releases) installed. More instruction on its usage can be found [here](https://docs.microsoft.com/azure/iot-pnp/howto-use-iot-explorer).

  *NOTE: This guide demonstrates use of the Azure CLI and does NOT demonstrate use of Azure IoT Explorer.*

## IoT Hub Device Setup
1. In the Azure portal, navigate to your IoT Hub resource.

1. On the left side menu, click on **'Overview'**. We will need the following information from this page:

    - Hostname

1. On the left side menu under **Device management**, click on **'Devices'**.

1. Select **'+ Add Device'**.

1. Give your device a unique name.

1. Select **'Symmetric key'**, **'Auto-generate keys'**, and **'Enable'** for Connecting the device to an IoT Hub, then click **'Save'**.

1. When the device has been created, select its name under 'Device ID'.

1. We will need the following information from the device page:

    - Device ID
    - Primary Key

_NOTE: Device keys are used to automatically generate a SAS token for authentication, which is only valid for one hour._

## Arduino IDE Setup

1. Open the Arduino IDE.

1. Install the Azure SDK for Embedded C library.

    - Navigate to **Tools > Manage Libraries**.
    - Search for the **'azure-sdk-for-c'** library. 
    - Install the latest version.

1. Install Arduino Mbed OS Nano Boards support in the Arduino IDE. [Full instructions can be found here.](https://docs.arduino.cc/hardware/nano-rp2040-connect)

    - Navigate to **Tools > Board > Board Manager**.
    - Search for **'RP2040'** and install the **Arduino Mbed OS Nano Boards** core.
    - Install the latest version.    
    
      *Note: This process may take several minutes.*  

1. Nagivate to **Tools > Board > Arduino Mbed OS Nano Boards** and select **'Arduino Nano RP2040 Connect'**.

1. Install WiFiNINA library for the Nano RP2040 Embedded C SDK sample. 

    - Navigate to **Tools > Manage Libraries**.
    - Search for the **'WiFiNINA'** library. 
    - Install the latest version.

      *Note: This process may take several minutes.*  
    
1. If this is your first time using the Nano RP2040 Connect, [follow these instructions to update the WiFi firmware on the Nano RP2040 Connect](https://docs.arduino.cc/tutorials/nano-rp2040-connect/rp2040-upgrading-nina-firmware).

1. Install the ArduinoBearSSL, ArduinoMqttClient, and ArduinoECCX08 libraries.

    - Navigate to **Tools > Manage Libraries**.
    - Search for the **'ArduinoBearSSL'** library. Install the latest version.
    - Search for the **'Arduino MQTT Client'** library. Install the latest version.
    - Search for the **'ArduinoECCX08'** library. Install the latest version.

1. You may need to restart the Arduino IDE for changes to show up.


## Run the Sample

1. Open the Arduino Nano RP2040 Connect sample.

    - In the Arduino IDE, navigate to **File > Examples > Azure SDK For C**
    - Select **'Azure_IoT_Hub_Arduino_Nano_RP2040_Connect'** to open the sample.

1. Navigate to the '*iot_configs.h*' file

1. In the '*iot_configs.h*' file, fill in your credentials. 

    - Add in your WiFi SSID and password.
    - Paste your IoT Hub device Hostname for the `IOT_CONFIG_IOTHUB_FQDN` variable. It should look something like:

        ```#define IOT_CONFIG_IOTHUB_FQDN "my-resource-group.azure-devices.net"```

    - Paste your IoT Hub device ID for the `IOT_CONFIG_DEVICE_ID` variable.

    - Finally, paste your IoT Hub Primary Key for the `IOT_CONFIG_DEVICE_KEY` variable.

1. Connect the Arduino Nano RP 2040 to your USB port.

1. On the Arduino IDE, select the port.

    - Navigate to **Tools > Port**.
    - Select the port to which the Nano RP2040 Connect is connected.

1. Upload the sketch.

    - Navigate to **Sketch > Upload**.
    
      *Note: This process may take several minutes.* 

      <details><summary><i>Expected output of the upload:</i></summary>
      <p>

      ```text
      Sketch uses 185534 bytes (1%) of program storage space. Maximum is 16777216 bytes.
      Global variables use 63708 bytes (23%) of dynamic memory, leaving 206628 bytes for local variables. Maximum is 270336 bytes.
      .
      
      ```
      
      </p>
      </details>

1. While the sketch is uploading, open the Serial Monitor to monitor the MCU (microcontroller) locally via the Serial Port.

    - Navigate to **Tools > Serial Monitor**.

        If you perform this step right away after uploading the sketch, the serial monitor will show an output similar to the following upon success:

        ```text
        2106-02-06 23:28:16 [INFO] Attempting to connect to WIFI SSID: <ssid>
        .
        2106-02-06 23:28:16 [INFO] WiFi connected, IP address: 67769516, Strength (dBm): -67
        2106-02-06 23:28:16 [INFO] Syncing time.
        ....
        2022-06-07 14:28:16 [INFO] Time synced!
        2022-06-07 14:28:16 [INFO] Initializing Azure IoT Hub client.
        2022-06-07 14:28:16 [INFO] Azure IoT Hub hostname: <hostname>
        2022-06-07 14:28:16 [INFO] Azure IoT Hub client initialized.
        2022-06-07 14:28:16 [INFO] Initializing MQTT client.
        2022-06-07 14:28:16 [INFO] UTC Current time: 2022-06-07 21:28:16 (epoch: 1654637296 secs)
        2022-06-07 14:28:16 [INFO] UTC Expiry time: 2022-06-07 22:28:16 (epoch: 1654640896 secs)
        2022-06-07 14:28:16 [INFO] Local Current time: 2022-06-07 14:28:16
        2022-06-07 14:28:16 [INFO] Local Expiry time: 2022-06-07 15:28:16
        2022-06-07 14:28:16 [INFO] MQTT Client ID: <device id>
        2022-06-07 14:28:16 [INFO] MQTT Username: <hostname>/<device id>/?api-version=2020-09-30&DeviceClientType=c/1.3.1(ard;nanorp2040connect)
        2022-06-07 14:28:16 [INFO] MQTT Password (SAS Token): ***
        2022-06-07 14:28:16 [INFO] MQTT client initialized.
        2022-06-07 14:28:16 [INFO] Connecting to Azure IoT Hub.
        2022-06-07 14:28:18 [INFO] Connected to your Azure IoT Hub!
        2022-06-07 14:28:18 [INFO] Subscribed to MQTT topic: devices/+/messages/devicebound/#
        2022-06-07 14:28:18 [INFO] Arduino Nano RP2040 Connect sending telemetry . . . 
        2022-06-07 14:28:18 [INFO] Telemetry sent.
        2022-06-07 14:28:20 [INFO] Arduino Nano RP2040 Connect sending telemetry . . . 
        2022-06-07 14:28:20 [INFO] Telemetry sent.
        2022-06-07 14:28:23 [INFO] Arduino Nano RP2040 Connect sending telemetry . . . 
        2022-06-07 14:28:23 [INFO] Telemetry sent.
        ```

1. Monitor the telemetry messages sent to the Azure IoT Hub.

    - On the left side menu under **Security settings**, click on **'Shared access policies'**.
    - Under **Manage shared acess policies** and **Policy Name**, Select **'iothubowner'**
    - Copy the **'Primary connection string'**.  
    - Using the Azure CLI, type and run the following command, inserting your Primary connection string and Device ID.

      ```
      az iot hub monitor-events --login "<Primary connection string in quotes>" --device-id <device id>
      ```

      <details><summary><i>Expected telemetry output:</i></summary>
      <p>

      ```text
      Starting event monitor, filtering on device: <device id>, use ctrl-c to stop...
      {
          "event": {
              "origin": "<device id>",
              "module": "",
              "interface": "",
              "component": "",
              "payload": "<payload>"
          }
      }
      {
          "event": {
              "origin": "<device id>",
              "module": "",
              "interface": "",
              "component": "",
              "payload": "<payload>"
          }
      }
      {
          "event": {
              "origin": "<device id>",
              "module": "",
              "interface": "",
              "component": "",
              "payload": "<payload>"
          }
      }
      ^CStopping event monitor...
      ```

      </p>
      </details>

## Troubleshooting
1. Both the WiFi SSID and password are case sensitive.
1. In the Serial Monitor, select 'Show Timestamp'. Check that the device clock and the Serial Monitor clock are accurate within a few seconds.
1. In the Serial Monitor, check the Broker, Client ID, and Username are accurate.
1. Check the SAS Token output 'se' variable - this is the expiration time in seconds from 1/1/1970. It should be set to 1 hour from your local time. If this time is ealier than the local time, your device certificate will be invalid and IoT Hub will timeout.

### Additional Help

- The error policy for the Embedded C SDK client library is documented [here](https://github.com/Azure/azure-sdk-for-c/blob/main/sdk/docs/iot/mqtt_state_machine.md#error-policy).
- File an issue via [GitHub Issues](https://github.com/Azure/azure-sdk-for-c/issues/new/choose).
- Check the error logs in the Arduino IDE Serial Monitor and search [previous questions](https://stackoverflow.com/questions/tagged/azure+c) or ask new ones on StackOverflow using the `azure` and `c` tags. 


## Certificates - Important to know

The Azure IoT service certificates presented during TLS negotiation shall be always validated, on the device, using the appropriate trusted root CA certificate(s).

The Azure SDK for C Arduino library automatically installs the root certificate used in the United States regions, and adds it to the Arduino sketch project when the library is included.

For other regions (and private cloud environments), please use the appropriate root CA certificate.

### Additional Information

For important information and additional guidance about certificates, please refer to [this blog post](https://techcommunity.microsoft.com/t5/internet-of-things/azure-iot-tls-changes-are-coming-and-why-you-should-care/ba-p/1658456) from the security team.


## Contributing

This project welcomes contributions and suggestions. Find more contributing details [here](https://github.com/Azure/azure-sdk-for-c/blob/main/CONTRIBUTING.md).

### License

This Azure SDK for C Arduino library is licensed under [MIT](https://github.com/Azure/azure-sdk-for-c-arduino/blob/main/LICENSE) license.

Azure SDK for Embedded C is licensed under the [MIT](https://github.com/Azure/azure-sdk-for-c/blob/main/LICENSE) license.
