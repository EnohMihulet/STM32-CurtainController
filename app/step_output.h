#pragma once

#include <stdint.h>
#include "step.h"

STEP_Result STEP_Output_Init(void);
STEP_Result STEP_Output_Start(void);
STEP_Result STEP_Output_Stop(void);
uint8_t STEP_Output_IsStarted(void);
STEP_Result STEP_Output_Frequency_Set(uint32_t frequency_hz);
uint32_t STEP_Output_Frequency_Get(void);
