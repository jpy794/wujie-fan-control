# Wujie Fan Control

A kernel module that could read and set fan speed for my Mechrevo Wujie 16 Pro laptop.

## Description

EC register map is extracted from a factory fan control tool. Feel free to leave a message if you need further information or have any suggestion.

The kernel module is just a demo for now. Use at your own risk.

## Usage

```shell
# build module
cd src && make

# load module
sudo insmod wujie_fan.ko

# enable fan control
echo 1 | sudo tee /sys/kernel/wujie_fan/wujie_fan/fanctl_en

# set fan1 speed 0-127
echo 100 | sudo tee /sys/kernel/wujie_fan/wujie_fan/fan1

# read fan1 speed
cat /sys/kernel/wujie_fan/wujie_fan/fan1
```

## Credits

- [Mechrevo-Fan-Control](https://github.com/bavelee/Mechrevo-Fan-Control)
- [LenovoLegionLinux](https://github.com/johnfanv2/LenovoLegionLinux)
