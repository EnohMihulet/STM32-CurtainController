#pragma once

#include <stdint.h>

#include "exti.h"

typedef enum {
	LIMIT_SWITCH_Contact_NO,
	LIMIT_SWITCH_Contact_NC,
} LIMIT_SWITCH_Contact;

#ifndef LIMIT_SWITCH_CONTACT
#define LIMIT_SWITCH_CONTACT LIMIT_SWITCH_Contact_NC
#endif

void LIMIT_SWITCH_Init(GPIO_Config* config);
uint8_t LIMIT_SWITCH_IsActive(GPIO_Config* config);

EXTI_Line LIMIT_SWITCH_EXTILine_Get(void);
EXTI_Trigger LIMIT_SWITCH_EXTITrigger_Get(void);
