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

void LIMIT_SWITCH_Init(void);
uint8_t LIMIT_SWITCH_IsActive(void);
GPIO_TypeDef* LIMIT_SWITCH_Port_Get(void);
GPIO_Pin LIMIT_SWITCH_Pin_Get(void);
EXTI_Line LIMIT_SWITCH_EXTILine_Get(void);
EXTI_Trigger LIMIT_SWITCH_EXTITrigger_Get(void);
