CPU		= -mcpu=cortex-m4
FPU		= -mfpu=fpv4-sp-d16
FLOAT_ABI	= -mfloat-abi=hard
THUMB		= -mthumb

MCU = $(CPU) $(FPU) $(FLOAT_ABI) $(THUMB)

CC	= arm-none-eabi-gcc
OBJCOPY	= arm-none-eabi-objcopy

TARGET = curtain_controller
LIB_DIR ?= ../STM32-BareMetal-Lib
BUILD_DIR = build

APP_SOURCES	= $(wildcard app/*.c)
CORE_SOURCES	= $(wildcard $(LIB_DIR)/core/*.c)
DRIVER_SOURCES	= $(wildcard $(LIB_DIR)/drivers/src/*.c)
DEVICE_SOURCES	= $(wildcard $(LIB_DIR)/devices/esp_at/*.c)
SHELL_SOURCES	= $(LIB_DIR)/shell/shell.c
C_SOURCES	= $(APP_SOURCES) $(CORE_SOURCES) $(DRIVER_SOURCES) $(DEVICE_SOURCES) $(SHELL_SOURCES)
ASM_SOURCES	= $(LIB_DIR)/startup/mcl_startup.s
LD_SCRIPT	= $(LIB_DIR)/linker/mcl_stm32f446re.ld

OBJECTS	= $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o)))
OBJECTS	+= $(BUILD_DIR)/$(notdir $(ASM_SOURCES:.s=.o))
DEPFILES = $(OBJECTS:.o=.d)

C_FLAGS = $(MCU) -Iapp -I$(LIB_DIR)/core -I$(LIB_DIR)/drivers/inc -I$(LIB_DIR)/devices/esp_at -I$(LIB_DIR)/shell -Wall -Wextra -O0 -g -MMD -MP
LD_FLAGS = $(MCU) -T$(LD_SCRIPT) -Wl,-Map=$(TARGET).map -Wl,--gc-sections -nostdlib
LD_LIBS  = -lgcc

vpath %.c app $(LIB_DIR)/core $(LIB_DIR)/drivers/src $(LIB_DIR)/devices/esp_at $(LIB_DIR)/shell
vpath %.s $(LIB_DIR)/startup

.PHONY: all elf bin hex clean

all: $(TARGET).elf $(TARGET).bin $(TARGET).hex

$(BUILD_DIR)/%.o: %.s
	mkdir -p $(BUILD_DIR)
	$(CC) $(MCU) -c $< -o $@

$(BUILD_DIR)/%.o: %.c
	mkdir -p $(BUILD_DIR)
	$(CC) $(C_FLAGS) -c $< -o $@

elf: $(TARGET).elf

$(TARGET).elf: $(OBJECTS) $(LD_SCRIPT)
	$(CC) $(OBJECTS) $(LD_FLAGS) $(LD_LIBS) -o $@

bin: $(TARGET).elf $(TARGET).bin

$(TARGET).bin: $(TARGET).elf
	$(OBJCOPY) -O binary $< $@

hex: $(TARGET).elf $(TARGET).hex

$(TARGET).hex: $(TARGET).elf
	$(OBJCOPY) -O ihex $< $@

clean:
	rm -rf $(BUILD_DIR) *.elf *.bin *.hex *.map

-include $(DEPFILES)
