#pragma once

#include "stepper_motor.h"
#include "exti.h"

typedef enum {
	CURTAIN_Result_Ok = 0,
	CURTAIN_Result_InvalidStepperConfig,
	CURTAIN_Result_InvalidLimitSwitchConfig,
	CURTAIN_Result_InvalidLimitSwitchEXTIConfig,
	CURTAIN_Result_InvalidLimitSwitchCallback,
	CURTAIN_Result_Busy,
	CURTAIN_Result_QueueFull,
	CURTAIN_Result_InvalidState,
} CURTAIN_Result;

typedef enum {
	CURTAIN_Unknown = 0,
	CURTAIN_Up = 1,
	CURTAIN_DownOpen = 2,
	CURTAIN_DownClosed = 3,
} CURTAIN_State;

typedef struct {
	STEPPER_Handle* stepper;
	GPIO_Config* upper_limit_switch;
	GPIO_Config* lower_limit_switch;
	CURTAIN_State state;
	uint8_t busy;
} CURTAIN_Controller;

CURTAIN_Result CURTAIN_Controller_Init();
STEPPER_Handle* CURTAIN_Controller_Stepper_Get(void);
CURTAIN_State CURTAIN_Controller_State_Get(void);
uint8_t CURTAIN_Controller_IsBusy(void);
CURTAIN_Result CURTAIN_Controller_ChangeState(CURTAIN_State new_state);
CURTAIN_Result CURTAIN_Controller_Up();
CURTAIN_Result CURTAIN_Controller_DownOpen();
CURTAIN_Result CURTAIN_Controller_DownClosed();
CURTAIN_Result CURTAIN_Controller_Move(STEPPER_Direction direction, uint32_t steps);
void CURTAIN_Controller_Stop(CURTAIN_State state);
