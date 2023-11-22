# Look4Sat-AntRunner-Controller
 * [Introduce](#introduce)
 * [How to Use](#how-to-use)
 * [Rerfernce](#reference)

## Introduce
This project works as a component of my [AntRunner](https://github.com/wuxx/AntRunner) project to control my rotator wirelessly through the Look4Sat. Of course, if you have your own rotator that needs wireless control, this project should also provide some reference.

![AntRunner-1](https://github.com/wuxx/AntRunner/blob/master/doc/1.jpg)
![AntRunner-2](https://github.com/wuxx/AntRunner/blob/master/doc/3.jpg)
![AntRunner-3](https://github.com/wuxx/AntRunner/blob/master/doc/2.jpg)

## How to Use

### Config the WiFi


![connect-to-esp32c3-softap](https://github.com/wuxx/Look4Sat-AntRunner-Controller/blob/master/doc/connect-to-esp32c3-softap.png)

![connect-to-wifi](https://github.com/wuxx/Look4Sat-AntRunner-Controller/blob/master/doc/connect-to-wifi.jpg)

### Config the Look4Sat 

![look4sat-config](https://github.com/wuxx/Look4Sat-AntRunner-Controller/blob/master/doc/look4sat-config.png)


### LED Status
LED | Description
---|---
Red light blink slowly | Connecting to wifi
Blue light always on| WiFi is connected
Green light always on | A connection has been established with Look4Sat
Green light blink quickly | Look4Sat is sending control commands

## Reference
- AntRunner (https://github.com/wuxx/AntRunner) 
- Look4Sat (https://github.com/rt-bishop/Look4Sat)
