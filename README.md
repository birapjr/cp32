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
The flash will send the code to `/dev/cu.usbmodem2101` serial port on MacOs, adjust to your port