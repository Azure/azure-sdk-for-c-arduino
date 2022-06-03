---
page_type: sample
description: Connecting Arduino Nano RP2040 Connect to Azure IoT Central using the Azure SDK for C Arduino library
languages:
- c
products:
- azure-iot
- azure-iot-pnp
- azure-iot-central
---

# Getting started with the Arduino Nano RP2040 Connect and Azure IoT Central with Azure SDK for C Arduino library

**Total completion time**:  30 minutes

In this tutorial you use the Azure SDK for C to connect the [Arduino Nano RP2040 Connect](https://docs.arduino.cc/hardware/nano-rp2040-connect) to Azure IoT Central. The article is part of the series [IoT Device Development](https://go.microsoft.com/fwlink/p/?linkid=2129824). The series introduces device developers to the Azure SDK for C, and shows how to connect several device evaluation kits to Azure IoT.

You will complete the following tasks:

* Install the Azure SDK for C library on Arduino
* Build the image and flash it onto the Arduino Nano RP2040 Connect
* Use Azure IoT Central to create cloud components, view properties, view device telemetry, and call direct commands

## Prerequisites

* Have an [Azure account](https://azure.microsoft.com/) created.

* Have the latest [Arduino IDE](https://www.arduino.cc/en/Main/Software) installed.


## IoT Central and Device Setup

### Create the IoT Central Application

There are several ways to connect devices to Azure IoT. In this section, you learn how to connect a device by using Azure IoT Central. IoT Central is an IoT application platform that reduces the cost and complexity of creating and managing IoT solutions.

To create a new application:

1. Go to [Azure IoT Central portal](https://apps.azureiotcentral.com/).
1. On the left side menu select **'My apps'**.
1. Select **'+ New application'**.
1. In the **Custom app** box, select **'Create app'**.
1. Create a custom Application name and a URL.
1. Under **Pricing plan**, select **'Free'** to activate a 7-day trial.

    ![IoT Central create an application](media/iotcentralcreate-custom.png)

1. Select **'Create'**.
1. After IoT Central provisions the application, it redirects you automatically to the new application dashboard.

    > Note: If you have an existing IoT Central application, you can use it to complete the steps in this article rather than create a new application.

### Create a new device

In this section, you will use the IoT Central application dashboard to create a new logical device.

To create a device:

1. On the left side menu, under **Connect**, select **'Devices'**.
1. Select **'+ New'**. A **Create a new device** window will appear.
1. Create a Device name and Device ID.
1. Leave Device template as **Unassigned**.

    ![IoT Central create a device](media/iotcentralcreate-device.png)

1. Select **'Create'**. The newly created device will appear in the **All devices** list.  
1. Under **Device name**, select your newly created device.
1. In the top menu bar, select **'Connect'**. A **Device connection groups** window will appear.

    ![IoT Central create a device](media/iotcentraldevice-connection-info.png)

1. You will need the following information from this window:

> * ID scope
> * Device ID
> * Primary key


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

1. Install the ArduinoBearSSL and ArduinoMqttClient libraries.

    - Navigate to **Tools > Manage Libraries**.
    - Search for the **'ArduinoBearSSL'** library. Install the latest version.
    - Search for the **'Arduino MQTT Client'** library. Install the latest version.

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
      Sketch uses 187636 bytes (1%) of program storage space. Maximum is 16777216 bytes.
      Global variables use 63492 bytes (23%) of dynamic memory, leaving 206844 bytes for local variables. Maximum is 270336 bytes.
      .
      
      ```
      
      </p>
      </details>

1. While the sketch is uploading, open the Serial Monitor to monitor the MCU (microcontroller) locally via the Serial Port.

    - Navigate to **Tools > Serial Monitor**.

        If you perform this step right away after uploading the sketch, the serial monitor will show an output similar to the following upon success:

        ```text
        ...
        1970/1/1 00:00:03 [INFO] WiFi connected, IP address: 192.168.1.228
        1970/1/1 00:00:03 [INFO] Setting time using SNTP
        ...
        2022/1/18 23:53:17 [INFO] Time initialized!
        2022/1/18 23:53:18 [INFO] Azure IoT client initialized (state=1)
        2022/1/18 23:53:18 [INFO] MQTT client target uri set to 'mqtts://global.azure-devices-provisioning.net'
        2022/1/18 23:53:18 [INFO] MQTT client connecting.
        2022/1/18 23:53:19 [INFO] MQTT client connected (session_present=0).
        2022/1/18 23:53:19 [INFO] MQTT client subscribing to '$dps/registrations/res/#'
        2022/1/18 23:53:19 [INFO] MQTT topic subscribed (message id=48879).
        2022/1/18 23:53:19 [INFO] MQTT client publishing to '$dps/registrations/PUT/iotdps-register/?$rid=1{"modelId":"dtmi:azureiot:devkit:freertos:Esp32AzureIotKit;1"}{"registrationId":"myDeviceId","payload":{"modelId":"dtmi:azureiot:devkit:freertos:Esp32AzureIotKit;1"}}'
        2022/1/18 23:53:19 [INFO] MQTT message received.
        2022/1/18 23:53:19 [INFO] MQTT client publishing to '$dps/registrations/GET/iotdps-get-operationstatus/?$rid=1&operationId=4.8a7f95e72373290a.f936a9b2-12ff-4b7d-8189-4c250236c141'
        2022/1/18 23:53:19 [INFO] MQTT message received.
        2022/1/18 23:53:22 [INFO] MQTT client publishing to '$dps/registrations/GET/iotdps-get-operationstatus/?$rid=1&operationId=4.8a7f95e72373290a.f936a9b2-12ff-4b7d-8189-4c250236c141'
        2022/1/18 23:53:22 [INFO] MQTT message received.
        2022/1/18 23:53:22 [INFO] MQTT client being disconnected.
        2022/1/18 23:53:22 [INFO] MQTT client target uri set to 'mqtts://myProvisionedIoTHubFqdn.azure-devices.net'
        2022/1/18 23:53:22 [INFO] MQTT client connecting.
        2022/1/18 23:53:23 [INFO] MQTT client connected (session_present=0).
        2022/1/18 23:53:23 [INFO] MQTT client subscribing to '$iothub/methods/POST/#'
        2022/1/18 23:53:23 [INFO] MQTT topic subscribed (message id=556).
        2022/1/18 23:53:23 [INFO] MQTT client subscribing to '$iothub/twin/res/#'
        2022/1/18 23:53:23 [INFO] MQTT topic subscribed (message id=10757).
        2022/1/18 23:53:23 [INFO] MQTT client subscribing to '$iothub/twin/PATCH/properties/desired/#'
        2022/1/18 23:53:23 [INFO] MQTT topic subscribed (message id=15912).
        2022/1/18 23:53:23 [INFO] MQTT client publishing to '$iothub/twin/PATCH/properties/reported/?$rid=0'
        2022/1/18 23:53:23 [INFO] MQTT client publishing to 'devices/myDeviceId/messages/events/'
        2022/1/18 23:53:23 [INFO] MQTT message received.
        2022/1/18 23:53:23 [INFO] Properties update request completed (id=0, status=204)
        2022/1/18 23:53:33 [INFO] MQTT client publishing to 'devices/myDeviceId/messages/events/'
        ```



















## Verify the device status

To view the device status in IoT Central portal:

1. From the application dashboard, select **Devices** on the side navigation menu.
1. Check the **Device status** of the device is updated to **Provisioned**.
1. Check the **Device template** of the device has updated to **Espressif ESP32 Azure IoT Kit**.

    ![IoT Central device status](media/azure-iot-central-device-view-status.png)

## View telemetry

With IoT Central, you can view the flow of telemetry from your device to the cloud.

To view telemetry in IoT Central portal:

1. From the application dashboard, select **Devices** on the side navigation menu.
1. Select the device from the device list.
1. View the telemetry as the device sends messages to the cloud in the **Overview** tab.

    ![IoT Central device telemetry](media/azure-iot-central-device-telemetry.png)

## Send a command on the device

You can also use IoT Central to send a command to your device. In this section, you can call a command to toggle LEDs or write to the screen.

To write to the screen:

1. Select the **Command** tab from the device page.
1. Locate the **Display Text** command.
1. In the **Content** textbox, enter the text to be displayed on the screen.
1. Select **Run**. 
1. The screen on the device will update with the desired text.

To toggle an LED:

1. Select the **Command** tab from the device page.
1. Locate the **Toggle LED 1** or **Toggle LED 2** command
1. Select **Run**.
1. An LED light on the device will toggle state.

![IoT Central invoke method](media/azure-iot-central-invoke-method.png)

## View device information

You can view the device information from IoT Central.

Select **About** tab from the device page.
![IoT Central device info](media/azure-iot-central-device-about.png)

## Clean up resources

If you no longer need the Azure resources created in this tutorial, you can delete them from the IoT Central portal. Optionally, if you continue to another tutorial in this Getting Started guide, you can keep the resources you've already created and reuse them.

To keep the Azure IoT Central sample application but remove only specific devices:

1. Select the **Devices** tab for your application.
1. Select the device from the device list.
1. Select **Delete**.

To remove the entire Azure IoT Central sample application and all its devices and resources:

1. Select **Administration** > **Your application**.
1. Select **Delete**.


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
