#include "board_button.h"
#include "curtain_shell_commands.h"
#include "board_led.h"
#include "limit_switch.h"
#include "stepper_motor.h"
#include "exti.h"
#include "shell.h"
#include "usart.h"

#define TEST_STEPPER_DEFAULT_FREQUENCY_HZ 2UL

static STEPPER_Handle test_stepper;

static const STEPPER_Config test_stepper_config = {
	.step_timer = TIM2,
	.step_channel = TIM_Channel_2,
	.step_port = GPIOB,
	.step_pin = GPIO_Pin_3,
	.step_alternate_function = AF1,
	.direction_port = GPIOB,
	.direction_pin = GPIO_Pin_4,
	.direction_forward_level = 1U,
	.enable_port = GPIOB,
	.enable_pin = GPIO_Pin_5,
	.enable_polarity = STEPPER_Polarity_ActiveLow,
	.timer_clock_hz = 16000000UL,
	.default_frequency_hz = TEST_STEPPER_DEFAULT_FREQUENCY_HZ,
	.step_polarity = TIM_OutputPolarity_ActiveHigh,
};

static void LimitSwitch_Callback(void) {
	STEPPER_Stop(&test_stepper);
}

int main(void) {
	Board_Button_Init();
	Board_LED_Init();
	LIMIT_SWITCH_Init();
	(void)STEPPER_Init(&test_stepper, &test_stepper_config);
	Curtain_ShellCommands_Init(&test_stepper);

	EXTI_Config exti_limit_switch = {
		.port = LIMIT_SWITCH_Port_Get(),
		.pin = LIMIT_SWITCH_Pin_Get(),
		.trigger = LIMIT_SWITCH_EXTITrigger_Get(),
		.interrupt_enable = 1,
		.event_enable = 0,
	};

	(void)EXTI_Callback_Register(LIMIT_SWITCH_EXTILine_Get(), LimitSwitch_Callback);
	(void)EXTI_Line_Configure(&exti_limit_switch);
	USART2_Init();
	SHELL_Start();
	
	while (1) {
	}

	return 0;
}
