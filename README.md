# Look4Sat-AntRunner-Controller
 * [Introduce](#introduce)
 * [How to Use](#how-to-use)
 * [Rerfernce](#reference)

## Introduce
This project works as a component of my [AntRunner](https://github.com/wuxx/AntRunner) project to control my rotator wirelessly through the Look4Sat. Of course, if you have your own rotator that needs wireless control, this project should also provide some reference.

![AntRunner-1](https://github.com/wuxx/AntRunner/blob/master/doc/1.jpg)

## How to Use

### Config the WiFi

Plug in the module on the AntRunner control board. After powering, a wifi hotspot called esp32 will appear. Click it to connect to this wifi hotspot (no password required).  
![connect-to-esp32c3-softap](https://github.com/wuxx/Look4Sat-AntRunner-Controller/blob/master/doc/connect-to-esp32c3-softap.png)

Then enter 10.10.10.1 in the browser to enter the operation background, select your wifi AP to connect. then the module will connect it in STA mode. After the connection is completed, you need to get the IP address, get it in the background of your WiFi AP or check the module's serial port output log.  
![connect-to-wifi](https://github.com/wuxx/Look4Sat-AntRunner-Controller/blob/master/doc/connect-to-wifi.jpg)

### Config the Look4Sat 

Open the configuration interface of Look4Sat, just fill in the IP address of module, and then the entire system can start working. 
![look4sat-config](https://github.com/wuxx/Look4Sat-AntRunner-Controller/blob/master/doc/look4sat-config.png)

You can get the status of the current module by checking the LED lights. The details are as follows  

#### LED Status
LED | Description
---|---
Red light blink slowly | Connecting to wifi
Blue light always on| WiFi is connected
Green light always on | A connection has been established with Look4Sat
Green light blink quickly | Look4Sat is sending control commands

### Look4Sat Usage

The use of look4sat is very simple, just select a satellite entry and enter, look4sat will start sending control commands to the controller.
![satelite-select](https://github.com/wuxx/Look4Sat-AntRunner-Controller/blob/master/doc/satelite-select.jpg)
![satelite-track](https://github.com/wuxx/Look4Sat-AntRunner-Controller/blob/master/doc/satelite-track.jpg)

## Reference
- AntRunner (https://github.com/wuxx/AntRunner) 
- Look4Sat (https://github.com/rt-bishop/Look4Sat)
