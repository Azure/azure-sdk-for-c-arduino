---
page_type: sample
description: Connecting Arduino Portenta H7 to Azure IoT Hub using the Azure SDK for C Arduino library
languages:
- c
products:
- azure-iot
- azure-iot-pnp
- azure-iot-dps
- azure-iot-hub
---

# Getting started with the Arduino Portenta H7 and Embedded C IoT Hub Client with Azure SDK for C Arduino library

**Total completion time**:  30 minutes

- [Getting started with the Arduino Portenta H7 and Embedded C IoT Hub Client with Azure SDK for C Arduino library](#getting-started-with-the-arduino-portenta-h7-and-embedded-c-iot-hub-client-with-azure-sdk-for-c-arduino-library)
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

In this tutorial you use the Azure SDK for C to connect the Arduino Portenta H7 to Azure IoT Hub. The article is part of the series [IoT Device Development](https://go.microsoft.com/fwlink/p/?linkid=2129824). The series introduces device developers to the Azure SDK for C, and shows how to connect several device evaluation kits to Azure IoT.

### What is Covered
You will complete the following tasks:

* Install the Azure SDK for C library on Arduino
* Build the image and flash it onto the Portenta H7
* Use Azure IoT Hub to create view device telemetry

_The following was run on Windows 11 and WSL Ubuntu Desktop 20.04 environments, with Arduino IDE 1.8.15 and Arduino Portenta H7 Rev2._

## Prerequisites

- Have an [Azure account](https://azure.microsoft.com/) created.
- Have an [Azure IoT Hub](https://docs.microsoft.com/azure/iot-hub/iot-hub-create-through-portal) created.

- Have the latest [Arduino IDE](https://www.arduino.cc/en/Main/Software) installed.

- Have the [Arduino CLI installed](https://arduino.github.io/arduino-cli/0.21/installation/). 

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

1. We'll need the following information from the device page:

    - Device ID
    - Primary Key

_NOTE: Device keys are used to automatically generate a SAS token for authentication, which is only valid for one hour._

## Arduino IDE Setup

1. Open the Arduino IDE.

1. Install the Azure SDK for Embedded C library.

    - Navigate to **Tools > Manage Libraries**.
    - Search for the **'azure-sdk-for-c'** library.
    - Install the latest version.

1. Install Arduino Mbed OS Portenta board support in the Arduino IDE. [Full instructions can be found here.](https://docs.arduino.cc/software/ide-v1/tutorials/getting-started/cores/arduino-mbed_portenta)

    - Navigate to **Tools > Board > Boards Manager**
    - Search for **'Portenta'** and install the **Arduino Mbed OS Portenta Boards** core.
    - Install the latest version.    
    
      *Note: This process may take several minutes.*  

1. Nagivate to **Tools > Board > Arduino Mbed OS Portenta Boards** and select **'Arduino Portenta H7 (M7 core)'**.

1. In **Tools > Flash split**, set the flash split to be **'2MB M7 + M4 in SDRAM'**.

1. If this is your first time using the Portenta, [follow these instructions to update the WiFi firmware on the Portenta](https://support.arduino.cc/hc/en-us/articles/4403365234322-How-to-update-Wi-Fi-firmware-on-Portenta-H7).

1. Install additional libraries for the Portenta Embedded C SDK sample. 

    - This process is more involved than typical because we are using the NTP Client Generic library which has circular dependencies so we must do a special install to only grab the what we need.

    - There are two ways to do this:
        1. Download the  NTP Client Generic library manually from its repository, or
        2. Use the Arduino CLI. 

    - This tutorial will use the CLI approach because it is faster and easier to describe. 
    - Using the Arduino CLI, type and run the following command to install the NTP Client :

      ```
      arduino-cli lib install --no-deps NTPClient_Generic
      ```

    - Since we're already in the Arduino CLI, let's install remaining libraries (can also install these from Library Manager):

      ``` 
      arduino-cli lib install "Azure SDK for C" ArduinoBearSSL Time ArduinoMqttClient ArduinoECCX08
      ```

1. You may need to restart the Arduino IDE for changes to show up.


## Run the Sample

1. Open the Arduino Portenta sample.

    - In the Arduino IDE, navigate to **File > Examples > Azure SDK For C**
    - Select **Azure_IoT_Hub_PortentaH7** to open the sample. 

1. Navigate to the '*iot_configs.h*' file

1. In the '*iot_configs.h*' file, fill in your credentials. 

    - Add in your WiFi SSID and password.
    - Paste your IoT Hub device HostName for the `IOT_CONFIG_IOTHUB_FQDN` variable. It should look something like:

        ```#define IOT_CONFIG_IOTHUB_FQDN "my-resource-group.azure-devices.net"```

    - Paste your IoT Hub device ID for the `IOT_CONFIG_DEVICE_ID` variable.

    - Finally, paste your IoT Hub Primary Key for the `IOT_CONFIG_DEVICE_KEY` variable.

1. This sample was configured for a PST timezone (GMT -8hrs) with a Daylight Savings offset. If you live in a different timezone, update the values in '*Time Zone Offset*' at the bottom of the *iot_configs.h* file.

    - Change the `IOT_CONFIG_TIME_ZONE` value to reflect the number of hours to add or subtract from the GMT timezone for your timezone.
    - Why is this necessary? 
        - Our sample generates a temporary SAS token that is valid for 1 hour. If your device clock is off from your local timezone, the SAS token may appear to be expired and IoT Hub will refuse the device connection (it will timeout).

1. Connect the Arduino Portenta to your USB port.

1. On the Arduino IDE, select the port.

    - Navigate to **Tools > Port**.
    - Select the port to which the Portenta is connected.

1. Upload the sketch.

    - Navigate to **Sketch > Upload**.

      *Note: This process may take several minutes.* 

      <details><summary><i>Expected output of the upload:</i></summary>
      <p>

      ```text
      Sketch uses 398116 bytes (20%) of program storage space. Maximum is 1966080 bytes.
      Global variables use 92104 bytes (17%) of dynamic memory, leaving 431520 bytes for local variables. Maximum is 523624 bytes.
      dfu-util 0.10-dev

      Copyright 2005-2009 Weston Schmidt, Harald Welte and OpenMoko Inc.
      Copyright 2010-2021 Tormod Volden and Stefan Schmidt
      This program is Free Software and has ABSOLUTELY NO WARRANTY
      Please report bugs to http://sourceforge.net/p/dfu-util/tickets/

      Opening DFU capable USB device...
      Device ID 2341:035b
      Device DFU version 011a
      Claiming USB DFU Interface...
      Setting Alternate Interface #0 ...
      Determining device status...
      DFU state(2) = dfuIDLE, status(0) = No error condition is present
      DFU mode device DFU version 011a
      Device returned transfer size 4096
      DfuSe interface name: "Internal Flash   "
      Downloading element to address = 0x08040000, size = 403188
      Erase   	[=========================] 100%       403188 bytes
      Erase    done.
      Download	[=========================] 100%       403188 bytes
      Download done.
      File downloaded successfully
      Transitioning to dfuMANIFEST state
      ```
      
      </p>
      </details>

1. While the sketch is uploading, open the Serial Monitor to monitor the MCU (microcontroller) locally via the Serial Port.

    - Navigate to **Tools > Serial Monitor**.

        If you perform this step right away after uploading the sketch, the serial monitor will show an output similar to the following upon success:

        ```text
        2106-02-06 23:29:28 [INFO] Attempting to connect to WIFI SSID: <ssid>
        .

        2106-02-06 23:29:48 [INFO] WiFi connected, IP address: 84546732, Strength (dBm): -42
        2106-02-06 23:29:48 [INFO] Syncing time.

        2022-06-07 16:38:17 [INFO] Time synced!
        2022-06-07 16:38:17 [INFO] Initializing Azure IoT Hub client.
        2022-06-07 16:38:17 [INFO] Azure IoT Hub hostname: <hostname>
        2022-06-07 16:38:17 [INFO] Azure IoT Hub client initialized.
        2022-06-07 16:38:17 [INFO] Initializing MQTT client.
        2022-06-07 16:38:17 [INFO] UTC Current time: 2022-06-07 23:38:17 (epoch: 1654645097 secs)
        2022-06-07 16:38:17 [INFO] UTC Expiry time: 2022-06-08 00:38:17 (epoch: 1654648697 secs)
        2022-06-07 16:38:17 [INFO] Local Current time: 2022-06-07 16:38:17
        2022-06-07 16:38:17 [INFO] Local Expiry time: 2022-06-07 17:38:17
        2022-06-07 16:38:17 [INFO] MQTT Client ID: <device id>
        2022-06-07 16:38:17 [INFO] MQTT Username: <hostname>/<device id>/?api-version=2020-09-30&DeviceClientType=c/1.3.1(ard;portentaH7)
        2022-06-07 16:38:17 [INFO] MQTT Password (SAS Token): ***
        2022-06-07 16:38:17 [INFO] MQTT client initialized.
        2022-06-07 16:38:17 [INFO] Connecting to Azure IoT Hub.
        2022-06-07 16:38:19 [INFO] Connected to your Azure IoT Hub!
        2022-06-07 16:38:20 [INFO] Subscribed to MQTT topic: devices/+/messages/devicebound/#
        2022-06-07 16:38:20 [INFO] Arduino Portenta H7 sending telemetry . . .
        2022-06-07 16:38:20 [INFO] Telemetry sent.
        2022-06-07 16:38:22 [INFO] Arduino Portenta H7 sending telemetry . . .
        2022-06-07 16:38:22 [INFO] Telemetry sent.
        2022-06-07 16:38:25 [INFO] Arduino Portenta H7 sending telemetry . . .
        2022-06-07 16:38:25 [INFO] Telemetry sent.
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
