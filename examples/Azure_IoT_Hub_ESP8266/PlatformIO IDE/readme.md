---
page_type: sample
description: Connecting an ESP8266 device to Azure IoT using the Azure SDK for Embedded C
languages:
- c
products:
- azure-iot
- azure-iot-pnp
- azure-iot-dps
- azure-iot-hub
---

# How to Setup and Run Azure SDK for Embedded C IoT Hub Client on Espressif ESP8266 NodeMCU

- [How to Setup and Run Azure SDK for Embedded C IoT Hub Client on Espressif ESP8266 NodeMCU](#how-to-setup-and-run-azure-sdk-for-embedded-c-iot-hub-client-on-espressif-esp8266-nodemcu)
  - [Introduction](#introduction)
    - [What is Covered](#what-is-covered)
  - [Prerequisites](#prerequisites)
  - [Setup and Run Instructions](#setup-and-run-instructions)
  - [Certificates - Important to know](#certificates---important-to-know)
    - [Additional Information](#additional-information)
  - [Troubleshooting](#troubleshooting)
  - [Contributing](#contributing)
    - [License](#license)

## Introduction

This is a "to-the-point" guide outlining how to run an Azure SDK for Embedded C IoT Hub telemetry sample on an Esp8266 NodeMCU microcontroller. The command line examples are tailored to Debian/Ubuntu environments.

### What is Covered

- Configuration instructions for the PlatformIO IDE to compile a sample using the Azure SDK for Embedded C.
- Configuration, build, and run instructions for the IoT Hub telemetry sample.

_The following was run on Windows 10, Ubuntu Desktop 20.04, and macOS Monterey 12 environments, with Platform IO Core/Home 5.2.4/3.4.0 and platform Espressif 8266 3.2.0._

## Prerequisites

- Have an [Azure account](https://azure.microsoft.com/) created.
- Have an [Azure IoT Hub](https://docs.microsoft.com/azure/iot-hub/iot-hub-create-through-portal) created.
- Have a [logical device](https://docs.microsoft.com/azure/iot-hub/iot-hub-create-through-portal#register-a-new-device-in-the-iot-hub) created in your Azure IoT Hub using the authentication type "Symmetric Key".

    NOTE: Device keys are used to automatically generate a SAS token for authentication, which is only valid for one hour.

- Have the latest [Visual Studio Code](https://code.visualstudio.com/download) installed.

- Have the latest [PlatformIO IDE](https://docs.platformio.org/en/latest/integration/ide/vscode.html) installed in Visual Studio Code.

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
    
    On Mac:

      ```bash
      $ brew update && brew install azure-cli
      ```

      A list of all the Azure IoT CLI extension commands can be found [here](https://docs.microsoft.com/cli/azure/iot?view=azure-cli-latest).

  - The most recent version of [Azure IoT Explorer](https://github.com/Azure/azure-iot-explorer/releases) installed. More instruction on its usage can be found [here](https://docs.microsoft.com/azure/iot-pnp/howto-use-iot-explorer).

  NOTE: This guide demonstrates use of the Azure CLI and does NOT demonstrate use of Azure IoT Explorer.

## Setup and Run Instructions

1. Open Visial Studio Code and select the PlatformIO icon on the left side of the screen.

2. Click `PIO Home` -> `Open`-> `Open Project` and select this project (the PlatformIO IDE folder).

3. Once the project is opened, select the PlatformIO icon on the left side of the screen and under `PROJECT TASKS`, click `Build`.

4. Ensure the build completes with a status of `[SUCCESS]`.

5. Under the `include` folder, create a copy of `iot_configs.h` and name it `iot_configs_current.h`.

6. In `iot_configs_current.h`, enter your Azure IoT Hub and device information.
    - NOTE: `IOT_EDGE_GATEWAY` is the only macro that is optional and should only be used if you are connecting to an Edge Gateway, such as Azure IoT Edge, and not directly to Azure IoT Hub. If you want this sample to send telemetry to an Azure IoT Edge Gateway, uncomment and set this macro to the hostname of your gateway, copy your IoT Edge's root CA certificate to this project, name the certificate `edgeRootCA.pem`, and run the script `create_trusted_cert_header.sh`. For more details on this script and certificate usage, please review [Certificates - Important to know](#certificates---important-to-know)

7. Connect the Esp8266 NodeMCU microcontroller to your USB port.
    -   If you need to change the default COM port, board, serial monitor speed, or any other project configurations please review this [link](https://docs.platformio.org/en/latest/projectconf/index.html) on configuring the platform.ini file.

8. Select the PlatformIO icon on the left side of the screen and under `PROJECT TASKS`, click `Upload and Monitor`. This project will now be uploaded to your Esp8266 and a terminal window will open at the bottom of Visual Studio Code, where you can monitor the serial output. 

9. Monitor the telemetry messages sent to Azure IoT Hub using the connection string for the policy name `iothubowner` found under "Shared access policies" on your IoT Hub in the Azure portal.

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

For the Node MCU ESP8266 sample, our script `generate_arduino_zip_library.sh` automatically downloads the root certificate used in the United States regions (Baltimore CA certificate) and adds it to the Arduino sketch project.

If you would like to change what certificate is used, our script `create_trusted_cert_header.sh` demonstrates how to create a cerfiticate header file. This new cerfiticate header can be used instead of the default trusted root CA certificate header. At the moment, the script only looks for and creates a certificate header file for an Azure IoT Edge root CA certificate named `edgeRootCA.pem`, but it can be modified to use any root CA certificate of choice.

For other regions (and private cloud environments), please use the appropriate root CA certificate.

### Additional Information

For important information and additional guidance about certificates, please refer to [this blog post](https://techcommunity.microsoft.com/t5/internet-of-things/azure-iot-tls-changes-are-coming-and-why-you-should-care/ba-p/1658456) from the security team.

## Troubleshooting

- The error policy for the Embedded C SDK client library is documented [here](https://github.com/Azure/azure-sdk-for-c/blob/main/sdk/docs/iot/mqtt_state_machine.md#error-policy).
- File an issue via [GitHub Issues](https://github.com/Azure/azure-sdk-for-c/issues/new/choose).
- Check [previous questions](https://stackoverflow.com/questions/tagged/azure+c) or ask new ones on StackOverflow using the `azure` and `c` tags.

## Contributing

This project welcomes contributions and suggestions. Find more contributing details [here](https://github.com/Azure/azure-sdk-for-c/blob/main/CONTRIBUTING.md).

### License

This Azure SDK for C Arduino library is licensed under [MIT](https://github.com/Azure/azure-sdk-for-c-arduino/blob/main/LICENSE) license.

Azure SDK for Embedded C is licensed under the [MIT](https://github.com/Azure/azure-sdk-for-c/blob/main/LICENSE) license.
