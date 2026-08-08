#include "board_led.h"
#include "gpio.h"

#define BOARD_LED_PORT GPIOA
#define BOARD_LED_PIN GPIO_Pin_5

void Board_LED_Init(void) {
	RCC_GPIOAClock_Enable();

	GPIO_Config led = {
		.port = BOARD_LED_PORT,
		.pin = BOARD_LED_PIN,
		.mode = GPIO_Mode_Output,
		.output_type = GPIO_Output_PushPull,
		.speed = GPIO_Speed_Low,
		.pull = GPIO_Pull_None,
	};

	GPIO_Init(&led);
}

void Board_LED_PWM_Init(void) {
	RCC_GPIOAClock_Enable();

	GPIO_Config led = {
		.port = BOARD_LED_PORT,
		.pin = BOARD_LED_PIN,
		.mode = GPIO_Mode_Alt,
		.output_type = GPIO_Output_PushPull,
		.speed = GPIO_Speed_Low,
		.pull = GPIO_Pull_None,
		.alternate_function = AF1,
	};

	GPIO_Init(&led);
}

void LED_On(void) {
	GPIO_SetPin(BOARD_LED_PORT, BOARD_LED_PIN);
}

void LED_Off(void) {
	GPIO_ClearPin(BOARD_LED_PORT, BOARD_LED_PIN);
}

void LED_Toggle(void) {
	GPIO_TogglePin(BOARD_LED_PORT, BOARD_LED_PIN);
}
