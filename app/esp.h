#pragma once

#include "usart.h"

#define ESP_COMMAND_BUFFER_SIZE 64U

typedef enum {
	ESP_Result_Ok = 0,
	ESP_Result_Error,
	ESP_Result_Timeout,
	ESP_Result_InvalidConfig,
	ESP_Result_InvalidCommand,
	ESP_Result_Busy,
	ESP_Result_Capacity,
} ESP_Result;

typedef void (*ESP_CompletionCallback)(ESP_Result result, const char* response, const char* status, void* context);

ESP_Result ESP_Init(USART_TypeDef* usart);
ESP_Result ESP_Command_Send(const char* command);
void ESP_Update(void);
void ESP_CompletionCallback_Register(ESP_CompletionCallback callback, void* context);
