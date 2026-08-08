#pragma once

#include "tim.h"
#include "gpio.h"

typedef enum {
	STEPPER_Direction_Forward = 0,
	STEPPER_Direction_Reverse  = 1,
} STEPPER_Direction;

typedef enum {
	STEPPER_Polarity_ActiveLow  = 0,
	STEPPER_Polarity_ActiveHigh = 1,
} STEPPER_Polarity;

typedef enum {
	STEPPER_Result_Ok = 0,
	STEPPER_Result_InvalidConfig,
	STEPPER_Result_StepError,
} STEPPER_Result;

typedef struct {
	TIM_GP_TypeDef* step_timer;
	TIM_Channel step_channel;
	GPIO_TypeDef* step_port;
	GPIO_Pin step_pin;
	GPIO_AlternateFunction step_alternate_function;

	GPIO_TypeDef* direction_port;
	GPIO_Pin direction_pin;
	uint8_t direction_forward_level;

	GPIO_TypeDef* enable_port;
	GPIO_Pin enable_pin;
	STEPPER_Polarity enable_polarity;

	uint32_t timer_clock_hz;
	uint32_t default_frequency_hz;
	TIM_OutputPolarity step_polarity;
} STEPPER_Config;

typedef struct {
	TIM_GP_TypeDef* step_timer;
	TIM_Channel step_channel;
	GPIO_TypeDef* step_port;
	GPIO_Pin step_pin;
	GPIO_AlternateFunction step_alternate_function;

	GPIO_TypeDef* direction_port;
	GPIO_Pin direction_pin;
	uint8_t direction_forward_level;

	GPIO_TypeDef* enable_port;
	GPIO_Pin enable_pin;
	STEPPER_Polarity enable_polarity;

	uint32_t timer_clock_hz;
	TIM_OutputPolarity step_polarity;

	uint32_t target_steps;
	volatile uint32_t completed_steps;

	uint32_t step_frequency_hz;

	uint8_t enabled;
	uint8_t busy;

	STEPPER_Direction direction;

	void (*completion_callback)(void* context);
	void* completion_context;
} STEPPER_Handle;

typedef void (*STEPPER_CompletionCallback)(void* context);

STEPPER_Result STEPPER_Init(STEPPER_Handle* stepper, const STEPPER_Config* config);

STEPPER_Result STEPPER_Enable(STEPPER_Handle* stepper);
STEPPER_Result STEPPER_Disable(STEPPER_Handle* stepper);

void STEPPER_Direction_Set(STEPPER_Handle* stepper, STEPPER_Direction direction);

STEPPER_Result STEPPER_Start(STEPPER_Handle* stepper, uint32_t frequency_hz);
STEPPER_Result STEPPER_Step(STEPPER_Handle* stepper, STEPPER_Direction direction, uint32_t steps, uint32_t frequency_hz);
STEPPER_Result STEPPER_MoveSteps(STEPPER_Handle* stepper, STEPPER_Direction direction, uint32_t steps, uint32_t frequency_hz);
void STEPPER_Stop(STEPPER_Handle* stepper);

uint8_t STEPPER_IsBusy(STEPPER_Handle* stepper);
uint32_t STEPPER_CompletedSteps_Get(STEPPER_Handle* stepper);

void STEPPER_CompletionCallback_Register(STEPPER_Handle* stepper, STEPPER_CompletionCallback callback, void* context);
