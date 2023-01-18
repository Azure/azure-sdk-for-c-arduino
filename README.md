# Project

The Arduino library is built from the Azure Embedded SDK for C. For more details about it, please refer to the [official library website](https://github.com/azure/azure-sdk-for-c).

There are several other alternatives to get MCU-based devices connected to Azure. See [Other Azure IoT SDKs](https://learn.microsoft.com/azure/iot-develop/concepts-using-c-sdk-and-embedded-c-sdk) to learn more.

This library package contains the following samples. Please refer to their documentation for setup and execution instructions:

- [Azure IoT Central ESPRESSIF ESP32 Azure IoT Kit](examples/Azure_IoT_Central_ESP32_AzureIoTKit/readme.md)

- [Azure IoT Central ESPRESSIF ESP32](examples/Azure_IoT_Central_ESP32/readme.md)

- [Azure IoT Central Arduino Nano RP2040](examples/Azure_IoT_Central_Arduino_Nano_RP2040_Connect/readme.md)

- [Azure IoT Central Arduino Portenta H7](examples/Azure_IoT_Central_Arduino_Portenta_H7/readme.md)

- [Azure IoT Hub ESPRESSIF ESP8266](examples/Azure_IoT_Hub_ESP8266/readme.md)

- [Azure IoT Hub ESPRESSIF ESP32](examples/Azure_IoT_Hub_ESP32/readme.md)

- [Azure IoT Hub Realtek AmebaD](examples/Azure_IoT_Hub_RealtekAmebaD/readme.md)

- [Azure IoT Hub Arduino Nano RP2040](examples/Azure_IoT_Hub_Arduino_Nano_RP2040_Connect/README.md)

- [Azure IoT Hub Arduino Portenta H7](examples/Azure_IoT_Hub_PortentaH7/README.md)

- [Azure IoT Device Update ESP32](examples/Azure_IoT_Adu_ESP32/readme.md)

What is the difference between **IoT Hub** and **IoT Central** samples?

1. IoT Hub samples will get devices connected directly to [Azure IoT Hub](https://docs.microsoft.com/azure/iot-hub/iot-concepts-and-iot-hub)
1. IoT Central samples will leverage DPS ([Device Provisioning Service](https://docs.microsoft.com/azure/iot-dps/about-iot-dps)) to provision the device and then connect it to [Azure IoT Central](https://docs.microsoft.com/azure/iot-central/core/overview-iot-central). 

Please note that provisioning through DPS is mandatory for IoT Central scenarios, but DPS can also be used for IoT Hub devices as well.

## Contributing

For reporting any issues or requesting support, please open an issue on [azure-sdk-for-c](https://github.com/Azure/azure-sdk-for-c/issues/new/choose).

This project has adopted the [Microsoft Open Source Code of Conduct](https://opensource.microsoft.com/codeofconduct/).
For more information see the [Code of Conduct FAQ](https://opensource.microsoft.com/codeofconduct/faq/) or
contact [opencode@microsoft.com](mailto:opencode@microsoft.com) with any additional questions or comments.

### Fix Code Formatting

Run the following command from the root of the sdk with `clang-format` version 9.0.0.

```bash
find ./examples \( -iname '*.h' -o -iname '*.c' -o -iname '*.cpp' -o -iname '*.ino' \) -exec clang-format -i {} \;
```

Commit the resulting code formatting changes if there are any.

### Reporting Security Issues and Security Bugs

Security issues and bugs should be reported privately, via email, to the Microsoft Security Response Center (MSRC) <secure@microsoft.com>. You should receive a response within 24 hours. If for some reason you do not, please follow up via email to ensure we received your original message. Further information, including the MSRC PGP key, can be found in the [Security TechCenter](https://www.microsoft.com/msrc/faqs-report-an-issue).

### License

This Azure SDK for C Arduino library is licensed under [MIT](https://github.com/Azure/azure-sdk-for-c-arduino/blob/main/LICENSE) license.

Azure SDK for Embedded C is licensed under the [MIT](https://github.com/Azure/azure-sdk-for-c/blob/main/LICENSE) license.
