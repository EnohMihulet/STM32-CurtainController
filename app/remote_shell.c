#include "remote_shell.h"

#include "esp_at.h"
#include "shell.h"

#define REMOTE_SHELL_COMMAND_CAPACITY 64U
#define REMOTE_SHELL_RESPONSE_CAPACITY 2048U
#define REMOTE_SHELL_CONNECTION_ID_INVALID 0xFFU

#define REMOTE_SHELL_INPUT_INVALID_RESPONSE "Invalid command payload.\r\n"
#define REMOTE_SHELL_INPUT_TOO_LONG_RESPONSE "Command is too long.\r\n"
#define REMOTE_SHELL_RESPONSE_TOO_LARGE_RESPONSE "Command response is too large.\r\n"

typedef enum {
	REMOTE_SHELL_State_Receiving,
	REMOTE_SHELL_State_CommandReady,
	REMOTE_SHELL_State_WaitingToSend,
} REMOTE_SHELL_State;

typedef enum {
	REMOTE_SHELL_InputError_None,
	REMOTE_SHELL_InputError_Invalid,
	REMOTE_SHELL_InputError_TooLong,
} REMOTE_SHELL_InputError;

typedef struct {
	REMOTE_SHELL_State state;
	uint8_t connection_id;
	REMOTE_SHELL_InputError input_error;
	char command[REMOTE_SHELL_COMMAND_CAPACITY];

	char response[REMOTE_SHELL_RESPONSE_CAPACITY];
	SHELL_Buffer response_buffer;
} REMOTE_SHELL_Handle;

static REMOTE_SHELL_Handle remote;

static void REMOTE_SHELL_Session_Reset(REMOTE_SHELL_Handle* shell) {
	if (shell == 0) return;

	shell->state = REMOTE_SHELL_State_Receiving;
	shell->connection_id = REMOTE_SHELL_CONNECTION_ID_INVALID;
	shell->input_error = REMOTE_SHELL_InputError_None;
	shell->command[0] = '\0';
	SHELL_Buffer_Reset(&shell->response_buffer);
}

static void REMOTE_SHELL_Command_Reject(REMOTE_SHELL_Handle* shell, uint8_t connection_id, REMOTE_SHELL_InputError error) {
	shell->connection_id = connection_id;
	shell->input_error = error;
	shell->command[0] = '\0';
	shell->state = REMOTE_SHELL_State_CommandReady;
}

static void ESP_REMOTE_ReceiveCallback(uint8_t connection_id, const char* data, uint32_t length, void* context) {
	REMOTE_SHELL_Handle* shell = context;
	if (shell == 0 || shell->state != REMOTE_SHELL_State_Receiving) return;

	if (data == 0) {
		REMOTE_SHELL_Command_Reject(shell, connection_id, REMOTE_SHELL_InputError_Invalid);
		return;
	}

	uint32_t command_length = length;
	if (command_length > 0U && data[command_length - 1U] == '\n') {
		command_length--;
		if (command_length > 0U && data[command_length - 1U] == '\r') command_length--;
	}
	else if (command_length > 0U && data[command_length - 1U] == '\r') {
		command_length--;
	}

	if (command_length >= REMOTE_SHELL_COMMAND_CAPACITY) {
		REMOTE_SHELL_Command_Reject(shell, connection_id, REMOTE_SHELL_InputError_TooLong);
		return;
	}

	for (uint32_t i = 0; i < command_length; i++) {
		if (data[i] == '\0' || data[i] == '\r' || data[i] == '\n') {
			REMOTE_SHELL_Command_Reject(shell, connection_id, REMOTE_SHELL_InputError_Invalid);
			return;
		}
		shell->command[i] = data[i];
	}

	shell->command[command_length] = '\0';
	shell->connection_id = connection_id;
	shell->input_error = REMOTE_SHELL_InputError_None;
	shell->state = REMOTE_SHELL_State_CommandReady;
}

static void REMOTE_SHELL_Response_Prepare(REMOTE_SHELL_Handle* shell) {
	SHELL_Buffer_Reset(&shell->response_buffer);
	const SHELL_Output* output = SHELL_Buffer_Output(&shell->response_buffer);
	if (output == 0) {
		REMOTE_SHELL_Session_Reset(shell);
		return;
	}

	if (shell->input_error == REMOTE_SHELL_InputError_Invalid) {
		SHELL_Write(output, REMOTE_SHELL_INPUT_INVALID_RESPONSE);
	}
	else if (shell->input_error == REMOTE_SHELL_InputError_TooLong) {
		SHELL_Write(output, REMOTE_SHELL_INPUT_TOO_LONG_RESPONSE);
	}
	else {
		SHELL_Result result = SHELL_ExecuteCommand(shell->command, output);
		SHELL_PrintResult(output, result);
	}

	if (shell->response_buffer.overflow) {
		SHELL_Buffer_Reset(&shell->response_buffer);
		SHELL_Write(output, REMOTE_SHELL_RESPONSE_TOO_LARGE_RESPONSE);
	}

	shell->state = REMOTE_SHELL_State_WaitingToSend;
}

void REMOTE_SHELL_Init(void) {
	SHELL_Buffer_Init(&remote.response_buffer, remote.response, REMOTE_SHELL_RESPONSE_CAPACITY);
	REMOTE_SHELL_Session_Reset(&remote);
	ESP_TCPReceiveCallback_Set(&ESP_REMOTE_ReceiveCallback, &remote);
}

void REMOTE_SHELL_Update(void) {
	if (remote.state == REMOTE_SHELL_State_Receiving) return;

	if (remote.state == REMOTE_SHELL_State_CommandReady) {
		if (ESP_TCPState_Get() != ESP_TCPState_Connected || remote.connection_id != ESP_ConnectionID_Get()) {
			REMOTE_SHELL_Session_Reset(&remote);
			return;
		}
		REMOTE_SHELL_Response_Prepare(&remote);
		return;
	}

	ESP_Result result = ESP_TCPSend(remote.connection_id, remote.response_buffer.data, remote.response_buffer.length);
	switch (result) {
	case ESP_Result_Busy:
		return;
	case ESP_Result_Ok:
	case ESP_Result_Error:
	case ESP_Result_Timeout:
	case ESP_Result_Capacity:
	case ESP_Result_Disconnected:
	case ESP_Result_InvalidConfig:
	case ESP_Result_InvalidCommand:
		REMOTE_SHELL_Session_Reset(&remote);
		return;
	}
}
