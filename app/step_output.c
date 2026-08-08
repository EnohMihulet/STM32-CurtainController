#include "step_output.h"

#include "gpio.h"
#include "rcc.h"

#define STEP_OUTPUT_TIMER_CLOCK_HZ 16000000UL
#define STEP_OUTPUT_DEFAULT_FREQUENCY_HZ 2UL
#define STEP_OUTPUT_PORT GPIOB
#define STEP_OUTPUT_PIN GPIO_Pin_3
#define STEP_OUTPUT_AF AF1

static STEP_ChannelConfig step_output = {
	.tim = TIM2,
	.channel = TIM_Channel_2,
	.timer_clock_hz = STEP_OUTPUT_TIMER_CLOCK_HZ,
	.frequency_hz = STEP_OUTPUT_DEFAULT_FREQUENCY_HZ,
	.polarity = TIM_OutputPolarity_ActiveHigh,
};

static void STEP_OutputGPIO_Init(void) {
	RCC_GPIOBClock_Enable();

	GPIO_Config step_output_pin = {
		.port = STEP_OUTPUT_PORT,
		.pin = STEP_OUTPUT_PIN,
		.mode = GPIO_Mode_Alt,
		.output_type = GPIO_Output_PushPull,
		.speed = GPIO_Speed_Low,
		.pull = GPIO_Pull_None,
		.alternate_function = STEP_OUTPUT_AF,
	};

	GPIO_Init(&step_output_pin);
}

STEP_Result STEP_Output_Init(void) {
	STEP_OutputGPIO_Init();
	RCC_TIM2Clock_Enable();
	return STEP_Channel_Init(&step_output);
}

STEP_Result STEP_Output_Start(void) {
	return STEP_Channel_Start(&step_output);
}

STEP_Result STEP_Output_Stop(void) {
	return STEP_Channel_Stop(&step_output);
}

uint8_t STEP_Output_IsStarted(void) {
	return STEP_Channel_IsStarted(&step_output);
}

STEP_Result STEP_Output_Frequency_Set(uint32_t frequency_hz) {
	return STEP_Frequency_Set(&step_output, frequency_hz);
}

uint32_t STEP_Output_Frequency_Get(void) {
	return STEP_Frequency_Get(&step_output);
}
