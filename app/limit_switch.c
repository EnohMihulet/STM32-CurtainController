#include "limit_switch.h"

#define LIMIT_SWITCH_PORT GPIOB
#define LIMIT_SWITCH_PIN GPIO_Pin_6
#define LIMIT_SWITCH_EXTI_LINE EXTI_Line_6

void LIMIT_SWITCH_Init(void) {
	RCC_GPIOBClock_Enable();

	GPIO_Config limit_switch = {
		.port = LIMIT_SWITCH_PORT,
		.pin = LIMIT_SWITCH_PIN,
		.mode = GPIO_Mode_Input,
		.output_type = GPIO_Output_PushPull,
		.speed = GPIO_Speed_Low,
		.pull = GPIO_Pull_Up,
	};

	GPIO_Init(&limit_switch);
}

uint8_t LIMIT_SWITCH_IsActive(void) {
	uint8_t pin_is_high = GPIO_ReadPin(LIMIT_SWITCH_PORT, LIMIT_SWITCH_PIN);

	if (LIMIT_SWITCH_CONTACT == LIMIT_SWITCH_Contact_NC) {
		return pin_is_high;
	}

	return !pin_is_high;
}

GPIO_TypeDef* LIMIT_SWITCH_Port_Get(void) {
	return LIMIT_SWITCH_PORT;
}

GPIO_Pin LIMIT_SWITCH_Pin_Get(void) {
	return LIMIT_SWITCH_PIN;
}

EXTI_Line LIMIT_SWITCH_EXTILine_Get(void) {
	return LIMIT_SWITCH_EXTI_LINE;
}

EXTI_Trigger LIMIT_SWITCH_EXTITrigger_Get(void) {
	return LIMIT_SWITCH_CONTACT == LIMIT_SWITCH_Contact_NC ? EXTI_Trigger_Rising : EXTI_Trigger_Falling;
}
