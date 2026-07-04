# ESP32-CAM Firmware

This firmware powers the Smart Fridge system by capturing images with the OV2640 camera, running inference locally with the Edge Impulse CNN model, and reporting the result as fresh or rotten.

## What the Firmware Does
- Captures live images from the ESP32-CAM camera module
- Runs on-device inference for fruit classification
- Publishes classification results to ThingsBoard over MQTT
- Streams the camera feed through a built-in web server
- Logs inference results for later review and debugging

## Key Features
- Real-time image capture and processing
- Fresh/rotten prediction with confidence-based output
- ThingsBoard telemetry integration
- Local HTTP endpoints for live view and log access
- Serial monitor support for setup and debugging

## Hardware Setup
The ESP32-CAM is the central processing unit for the system. It is connected to the camera module and programmed using a USB-to-TTL adapter.

<img src="../images/Hardware_Connection.png" alt="ESP32-CAM hardware connections" width="100%">

<img src="../images/Setup.png" alt="ESP32-CAM setup" width="100%">

## Firmware Workflow
1. The camera captures an image of the fruit stored inside the fridge.
2. The ESP32-CAM runs the trained inference model locally.
3. The device prints the prediction to the serial monitor.
4. The result is sent to ThingsBoard and stored in local logs.

## Monitoring and Debugging
- Use the serial monitor to observe live inference output.
- Access the camera stream from the ESP32 IP address.
- Download or clear logs using the built-in HTTP routes.

<img src="../images/Inside_setup.png" alt="Fridge installation view" width="100%">

<img src="../images/Serial Monitor.png" alt="Serial monitor output" width="100%">

## Notes for Uploading
- Install the ESP32 board support in Arduino IDE.
- Open the firmware sketch from [esp32_cam_rotten_fruit.ino](esp32_cam_rotten_fruit.ino).
- Update the Wi-Fi credentials and ThingsBoard token before uploading.
- Ensure the camera module is correctly wired before powering the board.

## Related Visuals
- [Setup image](../images/Setup.png)
- [Hardware connection image](../images/Hardware_Connection.png)
- [ThingsBoard dashboard](../images/Thingsboard_Dashboard.png)
- [Website dashboard](../images/Website.png)
