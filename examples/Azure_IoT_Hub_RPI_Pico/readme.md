page_type: sample description: Connecting an RPI Pico W (with Arduino installed)
_(based upon ESP8266 device version of code)__ to Azure IoT using the Azure SDK for
Embedded C languages:

-   c products:

-   azure-iot

-   azure-iot-pnp

-   azure-iot-dps

-   azure-iot-hub


# How to Setup and Run Azure SDK for Embedded C IoT Hub Client on Raspberry Pi Pico

-   [How to install Arduino BSP onto a RPI Pico](#How-to-install-Arduino-BSP-onto-a-RPI-Pico)

    -   [How To](#How-to-install-Arduino-BSP-onto-a-RPI-Pico)
    
-   [How to Setup and Run Azure SDK for Embedded C IoT Hub Client on a RPI Pico](#how-to-setup-and-run-azure-sdk-for-embedded-c-iot-hub-client-on-rpi-pico)

    -   [Introduction](#introduction)

        -   [What is Covered](#what-is-covered)

    -   [Prerequisites](#prerequisites)

    -   [Setup and Run Instructions](#setup-and-run-instructions)

    -   [Certificates - Important to know](#certificates---important-to-know)

        -   [Additional Information](#additional-information)

    -   [Troubleshooting](#troubleshooting)

    -   [Contributing](#contributing)

        -   [License](#license)

## How to install Arduino BSP onto a RPI Pico
- I was given a [Freenove Basic Pico Starter Kit](https://github.com/Freenove/Freenove_Basic_Starter_Kit_for_Raspberry_Pi_Pico) and a [RPI Pico W](https://www.raspberrypi.com/documentation/microcontrollers/raspberry-pi-pico.html#raspberry-pi-pico-w) for Xmas!!
- Ref: [https://freenove.com/fnk0058/](https://freenove.com/fnk0058/)
  - The repository [earlephilhower-arduino-pico on Github](https://github.com/earlephilhower/arduino-pico)  
  - Thx Earle.
- Open Arduino IDE, and click File>Preference. In new pop-up window, find “Additional Boards Manager 
URLs”, and replace with a new line：
```
https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
```
- In the IDE click Tools>Board>Boards Manager...on the menu bar
- Enter Pico in the searching box, and select ”Raspberry Pi Pico/RP2040” and click on [Install]
- Click Yes in the pop-up “dpinst-amd64.exe”installation window.
- Starting with the Pico disconnected from computer: Press and keep pressing the white button (BOOTSEL) on Pico, and connect Pico to 
computer using the USB port before releasing the button. (Note: Be sure to keep pressing the button before powering the Pico, 
otherwise the firmware will not download successfully)/
- In the Arduino IDE. Click File>Examples>01.Basics>Blink.
- Click Tools>Board>Raspberry Pi RP2040 Boards>Raspberry Pi Pico.
- Click Tools>Port>UF2 Board.
- Upload sketch to Pico.
- LED onboard should flash.


*From here on you upload to a specific COM port.*


## Introduction

This is a "to-the-point" guide outlining how to run an Azure SDK for Embedded C
IoT Hub telemetry sample on a Rapsberry Pi Pico microcontroller with Arduino OS installed. The command line
examples are tailored to Debian/Ubuntu environments.

### What is Covered

-   Configuration instructions for the Arduino IDE to compile a sample using the
    Azure SDK for Embedded C.

-   Configuration, build, and run instructions for the IoT Hub telemetry sample.

*The following was run on Windows 11 environment, with
Arduino IDE 2.0.3.*

## Prerequisites

-   Have an [Azure account](https://azure.microsoft.com/) created.

-   Have an [Azure IoT
    Hub](https://docs.microsoft.com/azure/iot-hub/iot-hub-create-through-portal)
    created.

-   Have a [logical
    device](https://docs.microsoft.com/azure/iot-hub/iot-hub-create-through-portal#register-a-new-device-in-the-iot-hub)
    created in your Azure IoT Hub using the authentication type "Symmetric Key".

    NOTE: Device keys are used to automatically generate a SAS token for
    authentication, which is only valid for one hour.

-   Have the latest [Arduino IDE](https://www.arduino.cc/en/Main/Software)
    installed.

-   Have the [RPI Pico (Arduino) board
    support](https://github.com/esp8266/Arduino#installing-with-boards-manager)
    installed on Arduino IDE. RPI Pico W boards are not natively supported by
    Arduino IDE, so you need to add them manually.

    -   Follow instructiuons above at
            [install Arduino on a Pico instructions](#How-to-install-Arduino-onto-a-RPI-Pico);

-   Have one of the following interfaces to your Azure IoT Hub set up:

    -   [Azure Command Line
        Interface](https://docs.microsoft.com/cli/azure/install-azure-cli?view=azure-cli-latest)
        (Azure CLI) utility installed, along with the [Azure IoT CLI
        extension](https://github.com/Azure/azure-iot-cli-extension).

        On Windows:

        Download and install: https://aka.ms/installazurecliwindows

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  PS C:\>az extension add --name azure-iot
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

>   On Linux:

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  $ curl -sL https://aka.ms/InstallAzureCLIDeb | sudo bash
  $ az extension add --name azure-iot
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

>   A list of all the Azure IoT CLI extension commands can be found
>   [here](https://docs.microsoft.com/cli/azure/iot?view=azure-cli-latest).

-   The most recent version of [Azure IoT
    Explorer](https://github.com/Azure/azure-iot-explorer/releases) installed.
    More instruction on its usage can be found
    [here](https://docs.microsoft.com/azure/iot-pnp/howto-use-iot-explorer).

    NOTE: This guide demonstrates use of the Azure CLI and does NOT demonstrate
    use of Azure IoT Explorer.

## Setup and Run Instructions

1.  Run the Arduino IDE.

2.  Install the Azure SDK for Embedded C library.

    -   On the Arduino IDE, go to menu `Sketch`, `Include Library`, `Manage
        Libraries...`.

    -   Search for and install `azure-sdk-for-c`.

3.  Install the Arduino PubSubClient library. (PubSubClient is a popular MQTT
    client for Arduino.)

    -   On the Arduino IDE, go to menu `Sketch`, `Include Library`, `Manage
        Libraries...`.

    -   Search for `PubSubClient` (by Nick O'Leary).

    -   Hover over the library item on the result list, then click on "Install".

3.  You may also need to install the Install the Arduino TimeLib library.

    -   Search for `TimeLib` aka Time (by Michael Margiolis).
 
    -   Hover over the library item on the result list, then click on "Install".

4.  Open the sample.

    -   On the Arduino IDE, go to menu `File`, `Examples`, `azure-sdk-for-c`.

    -   Click on `azure_esp8266` to open the sample. <-- This needs to change.

5.  Configure the sample.

    Enter your Azure IoT Hub and device information into the sample's
    `iot_configs.h`.

6.  Connect the RPI Pico microcontroller to your USB port.

7.  On the Arduino IDE, select the board and port.

    -   Go to menu `Tools`, `Board` and select `Raspberry Pi Pico W`.

    -   Go to menu `Tools`, `Port` and select the port to which the
        microcontroller is connected.

8.  Upload the sketch.

    -   Go to menu `Sketch` and click on `Upload`.

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
   Executable segment sizes:
   IROM   : 361788          - code in flash         (default or ICACHE_FLASH_ATTR)
   IRAM   : 26972   / 32768 - code in IRAM          (ICACHE_RAM_ATTR, ISRs...)
   DATA   : 1360  )         - initialized variables (global, static) in RAM/HEAP
   RODATA : 2152  ) / 81920 - constants             (global, static) in RAM/HEAP
   BSS    : 26528 )         - zeroed variables      (global, static) in RAM/HEAP

 Sketch uses 436972 bytes (20%) of program storage space. Maximum is 2093056 bytes.
 Global variables use 72244 bytes (27%) of dynamic memory, leaving 189900 bytes for local variables. Maximum is 262144 bytes.
 Resetting COM3
 Converting to uf2, output size: 910848, start address: 0x2000
 Flashing E: (RPI-RP2)
 Wrote 910848 bytes to E:/NEW.UF
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

1.  Monitor the MCU (microcontroller) locally via the Serial Port.

    -   Go to menu `Tools`, `Serial Monitor`.

        If you perform this step right away after uploading the sketch, the
        serial monitor will show an output similar to the following upon
        success:

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
.........WiFi connected, IP address: 192.168.0.14
Setting time using SNTP..........done!
Current time: Fri Dec 30 05:05:48 2022
Client ID: PicoDev137
Username: PicoHub137.azure-devices.net/PicoDev137/?api-version=2020-09-30&DeviceClientType=c%2F1.5.0-beta.1(ard;rpipico)
MQTT connecting ... connected.
13652 RPI Pico (Arduino) Sending telemetry . . . OK
16352 RPI Pico (Arduino) Sending telemetry . . . OK
19021 RPI Pico (Arduino) Sending telemetry . . . OK
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

1.  Monitor the telemetry messages sent to the Azure IoT Hub using the
    connection string for the policy name `iothubowner` found under "Shared
    access policies" on your IoT Hub in the Azure portal.

~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
$ az iot hub monitor-events --login <your Azure IoT Hub owner connection string in quotes> --device-id <your device id>

Starting event monitor, filtering on device: PicoDev137, use ctrl-c to stop...
{
    "event": {
        "origin": "PicoDev137",
        "module": "",
        "interface": "",
        "component": "",
        "payload": "{ \"msgCount\": 1 }"
    }
}
{
    "event": {
        "origin": "PicoDev137",
        "module": "",
        "interface": "",
        "component": "",
        "payload": "{ \"msgCount\": 2 }"
    }
}
{
    "event": {
        "origin": "PicoDev137",
        "module": "",
        "interface": "",
        "component": "",
        "payload": "{ \"msgCount\": 3 }"
    }
}
^CStopping event monitor...
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

## Certificates - Important to know

The Azure IoT service certificates presented during TLS negotiation shall be
always validated, on the device, using the appropriate trusted root CA
certificate(s).

The Azure SDK for C Arduino library automatically installs the root certificate
used in the United States regions, and adds it to the Arduino sketch project
when the library is included.

For other regions (and private cloud environments), please use the appropriate
root CA certificate.

### Additional Information

For important information and additional guidance about certificates, please
refer to [this blog
post](https://techcommunity.microsoft.com/t5/internet-of-things/azure-iot-tls-changes-are-coming-and-why-you-should-care/ba-p/1658456)
from the security team.

## Troubleshooting

-   The error policy for the Embedded C SDK client library is documented
    [here](https://github.com/Azure/azure-sdk-for-c/blob/main/sdk/docs/iot/mqtt_state_machine.md#error-policy).

-   File an issue via [GitHub
    Issues](https://github.com/Azure/azure-sdk-for-c/issues/new/choose).

-   Check [previous
    questions](https://stackoverflow.com/questions/tagged/azure+c) or ask new
    ones on StackOverflow using the `azure` and `c` tags.

## Contributing

This project welcomes contributions and suggestions. Find more contributing
details
[here](https://github.com/Azure/azure-sdk-for-c/blob/main/CONTRIBUTING.md).

### License

This Azure SDK for C Arduino library is licensed under
[MIT](https://github.com/Azure/azure-sdk-for-c-arduino/blob/main/LICENSE)
license.

Azure SDK for Embedded C is licensed under the
[MIT](https://github.com/Azure/azure-sdk-for-c/blob/main/LICENSE) license.
