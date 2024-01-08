# esp32HUB75GifPlayer
ESP32 Controlled GIF Player on HUB75 RGB screen

Platform IO Project

Can be administrated with the [pythonESP32HUB75Admin](https://github.com/schlarmann/pythonESP32HUB75Admin)

## How To / Configuration
The Configuration is found on top of the main.cpp file, first the config for the `ESP32-VirtualMatrixPanel-I2S-DMA.h` library, Screen size, orientation, pins etc...
The WIFI Config is found below, SSID and PSK. The IP Address, Mask etc. must be changed in the beginning of `void setup()`

The Default / Boot gif can be found in the data directory. The SPIFFS image must be uploaded for the program to work, it does not create one on its own.
