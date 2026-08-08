# CurtainController

Bare-metal STM32F446RE curtain controller firmware using the reusable drivers in `../STM32-BareMetal-Lib`.

## Build

```sh
make
```

If the driver library lives somewhere else, override `LIB_DIR`:

```sh
make LIB_DIR=/path/to/STM32-BareMetal-Lib
```
