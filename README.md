# cp32
CP32 OS - M5Stack Cardputer Adv ESP32-S3 Unix like OS

This is a personal project to write a unix like os for the Cardputer.

I will be using the Minix v1 as base for the code, ajusting it as it goes.

The project initial goal have the os kernel, a shell and some applications

## Compile Steps

Clean old build
```shell
make clean
```

Compile kernel
```shell
make
```

Flash to Cardputer
```shell
make flash
```
The flash will send the code to `/dev/cu.usbmodem2101` serial port on MacOs, adjust to your port.
If the flash process do not work, put the Cardputer in `download mode` by pressing and holding the `Go` button before
connection the data cacle to the Cardputer.

## Debugging

The initial kernel will output data to the serial port. This data can be seen with `screen` application.
Before start the `screen`, is recommended to press the `reset` button on the Cardputer.

```shell
screen /dev/cu.usbmodem2101 115200
```