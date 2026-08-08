#include "board_button.h"
#include "gpio.h"

#define BOARD_BUTTON_PORT GPIOC
#define BOARD_BUTTON_PIN GPIO_Pin_13

void Board_Button_Init(void) {
	RCC_GPIOCClock_Enable();

	GPIO_Config button = {
		.port = BOARD_BUTTON_PORT,
		.pin = BOARD_BUTTON_PIN,
		.mode = GPIO_Mode_Input,
		.output_type = GPIO_Output_PushPull,
		.speed = GPIO_Speed_Low,
		.pull = GPIO_Pull_Up,
	};

	GPIO_Init(&button);
}

uint8_t Board_Button_IsPressed(void) {
	return GPIO_ReadPin(BOARD_BUTTON_PORT, BOARD_BUTTON_PIN) == 0;
}
