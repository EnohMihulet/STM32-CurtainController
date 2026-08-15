#include "stepper_motor.h"
#include "nvic.h"
#include "step.h"

static void STEPPER_Timer_Callback(void* context);

static IRQ_Number STEPPER_TimerIRQ_Get(TIM_GP_TypeDef* tim) {
	if (tim == TIM2) return TIM2_IRQ_NUMBER;
	if (tim == TIM3) return TIM3_IRQ_NUMBER;
	if (tim == TIM4) return TIM4_IRQ_NUMBER;
	if (tim == TIM5) return TIM5_IRQ_NUMBER;

	return IRQ_NUMBER_NONE;
}

STEPPER_Result STEPPER_Init(STEPPER_Handle* stepper, const STEPPER_Config* config) {
	if (stepper == 0 || config == 0) return STEPPER_Result_InvalidConfig;
	if (config->step_timer == 0 || config->step_port == 0) return STEPPER_Result_InvalidConfig;
	if (config->direction_port == 0 || config->enable_port == 0) return STEPPER_Result_InvalidConfig;
	if (config->timer_clock_hz == 0 || config->default_frequency_hz == 0) return STEPPER_Result_InvalidConfig;
	if (STEPPER_TimerIRQ_Get(config->step_timer) == IRQ_NUMBER_NONE) return STEPPER_Result_InvalidConfig;

	stepper->step_timer = config->step_timer;
	stepper->step_channel = config->step_channel;
	stepper->step_port = config->step_port;
	stepper->step_pin = config->step_pin;
	stepper->step_alternate_function = config->step_alternate_function;
	stepper->timer_clock_hz = config->timer_clock_hz;
	stepper->step_polarity = config->step_polarity;

	GPIO_Config gpio_step_config = {
		.port = config->step_port,
		.pin = config->step_pin,
		.mode = GPIO_Mode_Alt,
		.output_type = GPIO_Output_PushPull,
		.speed = GPIO_Speed_Low,
		.pull = GPIO_Pull_None,
		.alternate_function = config->step_alternate_function,
	};

	RCC_GPIOClock_Enable(config->step_port);
	GPIO_Init(&gpio_step_config);

	RCC_TIMClock_Enable(config->step_timer);
	STEP_ChannelConfig step_channel_config = {
		.tim = stepper->step_timer,
		.channel = stepper->step_channel,
		.timer_clock_hz = stepper->timer_clock_hz,
		.frequency_hz = config->default_frequency_hz,
		.polarity = stepper->step_polarity,
	};
	if (STEP_Channel_Init(&step_channel_config) != STEP_Result_OK) return STEPPER_Result_StepError;

	stepper->direction_port = config->direction_port;
	stepper->direction_pin = config->direction_pin;
	stepper->direction_forward_level = config->direction_forward_level ? 1U : 0U;

	GPIO_Config gpio_direction_config = {
		.port = config->direction_port,
		.pin = config->direction_pin,
		.mode = GPIO_Mode_Output,
		.output_type = GPIO_Output_PushPull,
		.speed = GPIO_Speed_Low,
		.pull = GPIO_Pull_None,
	};

	RCC_GPIOClock_Enable(config->direction_port);
	GPIO_Init(&gpio_direction_config);

	stepper->enable_port = config->enable_port;
	stepper->enable_pin = config->enable_pin;
	stepper->enable_polarity = config->enable_polarity;

	GPIO_Config gpio_enable_config = {
		.port = config->enable_port,
		.pin = config->enable_pin,
		.mode = GPIO_Mode_Output,
		.output_type = GPIO_Output_PushPull,
		.speed = GPIO_Speed_Low,
		.pull = GPIO_Pull_None,
	};

	RCC_GPIOClock_Enable(stepper->enable_port);
	GPIO_Init(&gpio_enable_config);

	stepper->target_steps = 0;
	stepper->completed_steps = 0;
	stepper->step_frequency_hz = config->default_frequency_hz;
	stepper->enabled = 0;
	stepper->busy = 0;
	stepper->direction = STEPPER_Direction_Forward;

	TIM_UpdateCallback_Register(stepper->step_timer, STEPPER_Timer_Callback, stepper);
	TIM_UpdateInterrupt_Disable(stepper->step_timer);
	NVIC_IRQ_Enable(STEPPER_TimerIRQ_Get(stepper->step_timer));

	STEPPER_Direction_Set(stepper, STEPPER_Direction_Forward);
	STEPPER_Disable(stepper);

	return STEPPER_Result_Ok;
}

STEPPER_Result STEPPER_Enable(STEPPER_Handle* stepper) {
	if (stepper == 0) return STEPPER_Result_InvalidConfig;

	uint8_t enable_level = stepper->enable_polarity == STEPPER_Polarity_ActiveHigh ? 1U : 0U;
	GPIO_WritePin(stepper->enable_port, stepper->enable_pin, enable_level);
	stepper->enabled = 1;
	return STEPPER_Result_Ok;
}

STEPPER_Result STEPPER_Disable(STEPPER_Handle* stepper) {
	if (stepper == 0) return STEPPER_Result_InvalidConfig;

	uint8_t enable_level = stepper->enable_polarity == STEPPER_Polarity_ActiveHigh ? 1U : 0U;
	GPIO_WritePin(stepper->enable_port, stepper->enable_pin, !enable_level);
	stepper->enabled = 0;
	return STEPPER_Result_Ok;
}

void STEPPER_Direction_Set(STEPPER_Handle* stepper, STEPPER_Direction direction) {
	if (stepper == 0) return;

	uint8_t level = stepper->direction_forward_level;
	if (direction == STEPPER_Direction_Reverse) {
		level = !level;
	}

	GPIO_WritePin(stepper->direction_port, stepper->direction_pin, level);
	stepper->direction = direction;
}

STEPPER_Result STEPPER_Start(STEPPER_Handle* stepper, uint32_t frequency_hz) {
	if (stepper == 0 || frequency_hz == 0) return STEPPER_Result_InvalidConfig;
	if (stepper->target_steps == 0) TIM_UpdateInterrupt_Disable(stepper->step_timer);

	STEP_ChannelConfig step_channel_config = {
		.tim = stepper->step_timer,
		.channel = stepper->step_channel,
		.timer_clock_hz = stepper->timer_clock_hz,
		.frequency_hz = stepper->step_frequency_hz,
		.polarity = stepper->step_polarity,
	};
	if (frequency_hz != stepper->step_frequency_hz) {
		if (STEP_Frequency_Set(&step_channel_config, frequency_hz) != STEP_Result_OK) return STEPPER_Result_StepError;
		stepper->step_frequency_hz = frequency_hz;
		step_channel_config.frequency_hz = frequency_hz;
	}

	if (STEP_Channel_Start(&step_channel_config) != STEP_Result_OK) return STEPPER_Result_StepError;
	stepper->busy = 1;
	return STEPPER_Result_Ok;
}

STEPPER_Result STEPPER_Step(STEPPER_Handle* stepper, STEPPER_Direction direction, uint32_t steps, uint32_t frequency_hz) {
	if (stepper == 0 || steps == 0) return STEPPER_Result_InvalidConfig;

	STEPPER_Direction_Set(stepper, direction);
	stepper->target_steps = steps;
	stepper->completed_steps = 0;
	TIM_UpdateFlag_Clear(stepper->step_timer);
	TIM_UpdateInterrupt_Enable(stepper->step_timer);
	return STEPPER_Start(stepper, frequency_hz);
}

void STEPPER_Stop(STEPPER_Handle* stepper) {
	if (stepper == 0) return;

	STEP_ChannelConfig step_channel_config = {
		.tim = stepper->step_timer,
		.channel = stepper->step_channel,
		.timer_clock_hz = stepper->timer_clock_hz,
		.frequency_hz = stepper->step_frequency_hz,
		.polarity = stepper->step_polarity,
	};
	(void)STEP_Channel_Stop(&step_channel_config);
	TIM_UpdateInterrupt_Disable(stepper->step_timer);
	stepper->target_steps = 0;
	stepper->busy = 0;
}

uint8_t STEPPER_IsBusy(STEPPER_Handle* stepper) {
	if (stepper == 0) return 0;

	return stepper->busy;
}

uint32_t STEPPER_CompletedSteps_Get(STEPPER_Handle* stepper) {
	if (stepper == 0) return 0;

	return stepper->completed_steps;
}

static void STEPPER_Timer_Callback(void* context) {
	STEPPER_Handle* stepper = context;
	if (stepper == 0 || stepper->target_steps == 0) return;

	stepper->completed_steps++;
	if (stepper->completed_steps >= stepper->target_steps) {
		STEPPER_Stop(stepper);
		if (stepper->completion_callback) {
			stepper->completion_callback(stepper->completion_context);
		}
	}
}

void STEPPER_CompletionCallback_Register(STEPPER_Handle* handle, Completion_Callback completion_callback, void* completion_context) {
	handle->completion_callback = completion_callback;
	handle->completion_context = completion_context;
}
