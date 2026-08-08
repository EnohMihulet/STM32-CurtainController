#include "limit_switch.h"
#include "gpio.h"

#define LIMIT_SWITCH_EXTI_LINE EXTI_Line_6

void LIMIT_SWITCH_Init(GPIO_Config* config) {
	if (config == 0) return;

	RCC_GPIOClock_Enable(config->port);

	GPIO_Init(config);
}

uint8_t LIMIT_SWITCH_IsActive(GPIO_Config* config) {
	if (config == 0) return 0;

	uint8_t pin_is_high = GPIO_ReadPin(config->port, config->pin);

	if (LIMIT_SWITCH_CONTACT == LIMIT_SWITCH_Contact_NC) {
		return pin_is_high;
	}

	return !pin_is_high;
}

EXTI_Line LIMIT_SWITCH_EXTILine_Get(void) {
	return LIMIT_SWITCH_EXTI_LINE;
}

EXTI_Trigger LIMIT_SWITCH_EXTITrigger_Get(void) {
	return LIMIT_SWITCH_CONTACT == LIMIT_SWITCH_Contact_NC ? EXTI_Trigger_Rising : EXTI_Trigger_Falling;
}
