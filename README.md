# absolute-touch-x11
Absolute Touch for Linux

I originally made this because I couldn't get apsun's [Absolute Touch](https://github.com/apsun/AbsoluteTouch) to work on my machine, because of Synaptic's rubbish drivers. However, I don't really have any use for this on Linux so this project may not get any updates at all.

the code is largely based on [evtest](https://github.com/freedesktop-unofficial-mirror/evtest) to capture touchpad events, and uses the xdo library to simulate mouse movement and clicks

# Dependencies
- requires `libxdo`, install `xdotool` from your distribution's repositories

# Compiling
```
gcc -lxdo -o absolute-touch absolute-touch-x11.c
```
