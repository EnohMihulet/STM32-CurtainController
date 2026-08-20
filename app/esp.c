#include "esp.h"

#include "string_helper.h"

#define ESP_LINE_BUFFER_SIZE 96U
#define ESP_RESPONSE_BUFFER_SIZE 256U
#define ESP_TIMEOUT_MS 3000UL

#define SYSTICK_CTRL (*(volatile uint32_t*)0xE000E010UL)
#define SYSTICK_LOAD (*(volatile uint32_t*)0xE000E014UL)
#define SYSTICK_VALUE (*(volatile uint32_t*)0xE000E018UL)
#define SYSTICK_CTRL_ENABLE (1UL << 0)
#define SYSTICK_CTRL_TICKINT (1UL << 1)
#define SYSTICK_CTRL_CLKSOURCE (1UL << 2)

typedef enum {
	ESP_State_Idle = 0,
	ESP_State_WaitingResponse,
} ESP_State;

typedef struct {
	USART_TypeDef* usart;
	ESP_State state;

	char command_buffer[ESP_COMMAND_BUFFER_SIZE];
	char line_buffer[ESP_LINE_BUFFER_SIZE];
	uint32_t line_length;
	char response_buffer[ESP_RESPONSE_BUFFER_SIZE];
	uint32_t response_length;

	uint32_t started_at;
	uint8_t received_data;
	uint8_t received_echo;

	ESP_CompletionCallback completion_callback;
	void* completion_context;
} ESP_Handle;

static ESP_Handle esp_handle;
static volatile uint32_t esp_millis;

void SysTick_Handler(void) {
	esp_millis++;
}

static void ESP_TimeoutTimer_Init(void) {
	esp_millis = 0;
	SYSTICK_LOAD = (CLOCK_SPEED_HZ / 1000UL) - 1UL;
	SYSTICK_VALUE = 0;
	SYSTICK_CTRL = SYSTICK_CTRL_ENABLE | SYSTICK_CTRL_TICKINT | SYSTICK_CTRL_CLKSOURCE;
}

ESP_Result ESP_Init(USART_TypeDef* usart) {
	if (usart == 0) return ESP_Result_InvalidConfig;

	esp_handle.usart = usart;
	esp_handle.state = ESP_State_Idle;
	esp_handle.command_buffer[0] = '\0';
	esp_handle.line_buffer[0] = '\0';
	esp_handle.line_length = 0;
	esp_handle.response_buffer[0] = '\0';
	esp_handle.response_length = 0;
	esp_handle.started_at = 0;
	esp_handle.received_data = 0;
	esp_handle.received_echo = 0;
	esp_handle.completion_callback = 0;
	esp_handle.completion_context = 0;
	ESP_TimeoutTimer_Init();
	return ESP_Result_Ok;
}

void ESP_CompletionCallback_Register(ESP_CompletionCallback callback, void* context) {
	esp_handle.completion_callback = callback;
	esp_handle.completion_context = context;
}

static uint8_t ESP_Line_IsCommandTerminator(const char* line) {
	return STRING_Equals(line, "OK") || STRING_Equals(line, "ERROR") || STRING_Equals(line, "FAIL");
}

static void ESP_ResponseAppend(const char* line) {
	if (!STRING_Append(esp_handle.response_buffer, ESP_RESPONSE_BUFFER_SIZE, &esp_handle.response_length, line)) return;
	(void)STRING_Append(esp_handle.response_buffer, ESP_RESPONSE_BUFFER_SIZE, &esp_handle.response_length, "\r\n");
}

static void ESP_Finish(ESP_Result result, const char* status) {
	esp_handle.state = ESP_State_Idle;

	if (esp_handle.completion_callback != 0) {
		esp_handle.completion_callback(result, esp_handle.response_buffer, status, esp_handle.completion_context);
	}
}

static uint8_t ESP_Line_Process(void) {
	esp_handle.line_buffer[esp_handle.line_length] = '\0';

	if (ESP_Line_IsCommandTerminator(esp_handle.line_buffer)) {
		ESP_Result result = STRING_Equals(esp_handle.line_buffer, "OK") ? ESP_Result_Ok : ESP_Result_Error;
		ESP_Finish(result, esp_handle.line_buffer);
		return 1;
	}

	if (STRING_Equals(esp_handle.line_buffer, esp_handle.command_buffer)) {
		esp_handle.received_echo = 1;
	}
	else {
		ESP_ResponseAppend(esp_handle.line_buffer);
	}

	esp_handle.line_length = 0;
	esp_handle.line_buffer[0] = '\0';
	return 0;
}

ESP_Result ESP_Command_Send(const char* command) {
	if (esp_handle.usart == 0) return ESP_Result_InvalidConfig;
	if (command == 0 || command[0] == '\0') return ESP_Result_InvalidCommand;
	if (esp_handle.state != ESP_State_Idle) return ESP_Result_Busy;

	uint32_t command_length = 0;
	while (command[command_length] != '\0' && command[command_length] != '\r' && command[command_length] != '\n') {
		if (command_length >= ESP_COMMAND_BUFFER_SIZE - 1U) {
			return ESP_Result_Capacity;
		}

		esp_handle.command_buffer[command_length] = command[command_length];
		command_length++;
	}

	if (command_length == 0) {
		return ESP_Result_InvalidCommand;
	}

	esp_handle.command_buffer[command_length] = '\0';

	char discarded;
	while (USART_Receive_Char(esp_handle.usart, &discarded) == 0) {
	}

	esp_handle.line_length = 0;
	esp_handle.response_length = 0;
	esp_handle.received_data = 0;
	esp_handle.received_echo = 0;
	esp_handle.line_buffer[0] = '\0';
	esp_handle.response_buffer[0] = '\0';
	esp_handle.started_at = esp_millis;
	esp_handle.state = ESP_State_WaitingResponse;

	USART_Transmit_String(esp_handle.usart, esp_handle.command_buffer);
	USART_Transmit_String(esp_handle.usart, "\r\n");
	return ESP_Result_Ok;
}

void ESP_Update(void) {
	if (esp_handle.usart == 0 || esp_handle.state != ESP_State_WaitingResponse) return;

	char c;
	while (USART_Receive_Char(esp_handle.usart, &c) == 0) {
		esp_handle.received_data = 1;

		if (c == '\r' || c == '\n') {
			if (esp_handle.line_length == 0) continue;
			if (ESP_Line_Process()) return;
			continue;
		}

		if (esp_handle.line_length < ESP_LINE_BUFFER_SIZE - 1U) {
			esp_handle.line_buffer[esp_handle.line_length++] = c;
			esp_handle.line_buffer[esp_handle.line_length] = '\0';
		}
	}

	if ((uint32_t)(esp_millis - esp_handle.started_at) < ESP_TIMEOUT_MS) return;

	if (esp_handle.received_echo) {
		ESP_Finish(ESP_Result_Timeout, "timeout after echo; no OK/ERROR received");
	}
	else if (esp_handle.received_data) {
		ESP_Finish(ESP_Result_Timeout, "timeout after partial response");
	}
	else {
		ESP_Finish(ESP_Result_Timeout, "timeout; no response");
	}
}
