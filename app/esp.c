#include "esp.h"

#include "shell.h"
#include "string_helper.h"

#define ESP_WIFI_SSID ""
#define ESP_WIFI_PASSWORD ""
#define ESP_WIFI_JOIN_COMMAND "AT+CWJAP=\"" ESP_WIFI_SSID "\",\"" ESP_WIFI_PASSWORD "\""

#define ESP_LINE_BUFFER_SIZE 256U
#define ESP_PAYLOAD_BUFFER_SIZE 256U
#define ESP_SSID_BUFFER_SIZE 64U
#define ESP_IP_OCTET_COUNT 4U

#define ESP_TIMEOUT_MS 3000UL
#define ESP_JOIN_TIMEOUT_MS 20000UL
#define ESP_IPD_TIMEOUT_MS 3000UL

#define SYSTICK_CTRL (*(volatile uint32_t*)0xE000E010UL)
#define SYSTICK_LOAD (*(volatile uint32_t*)0xE000E014UL)
#define SYSTICK_VALUE (*(volatile uint32_t*)0xE000E018UL)
#define SYSTICK_CTRL_ENABLE (1UL << 0)
#define SYSTICK_CTRL_TICKINT (1UL << 1)
#define SYSTICK_CTRL_CLKSOURCE (1UL << 2)

typedef enum {
	ESP_Command_None = 0,
	ESP_Command_AT,
	ESP_Command_CWMODE,
	ESP_Command_CWJAP,
	ESP_Command_CIFSR,
	ESP_Command_CIPMUX,
	ESP_Command_CIPSERVER,
	ESP_Command_CIPSTATUS,
	ESP_Command_Test,
} ESP_Command;

typedef struct {
	USART_TypeDef* usart;

	ESP_RX_Mode rx_mode;
	ESP_Mode mode;
	ESP_WifiState wifi_state;
	char ssid[ESP_SSID_BUFFER_SIZE];
	uint8_t ip[ESP_IP_OCTET_COUNT];

	ESP_TCPState tcp_state;
	ESP_ConnectionMode connection_mode;
	ESP_ServerState server_state;

	uint8_t metadata_parsed;
	uint8_t payload_discard;
	uint8_t connection_id;
	uint32_t payload_position;
	uint32_t payload_length;
	uint32_t ipd_started_at;
	char payload[ESP_PAYLOAD_BUFFER_SIZE];

	char line[ESP_LINE_BUFFER_SIZE];
	uint32_t line_length;
	uint8_t line_overflow;

	ESP_Command command;
	uint8_t command_waiting;
	ESP_Result command_result;
	uint8_t response_parsed;
	uint8_t parsed_ip[ESP_IP_OCTET_COUNT];
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

static void ESP_IP_Clear(void) {
	for (uint32_t i = 0; i < ESP_IP_OCTET_COUNT; i++) {
		esp_handle.ip[i] = 0;
	}
}

static void ESP_NetworkDetails_Clear(void) {
	esp_handle.ssid[0] = '\0';
	ESP_IP_Clear();
}

static void ESP_TCPDetails_Clear(void) {
	esp_handle.connection_id = 0;
	esp_handle.tcp_state = ESP_TCPState_Disconnected;
	esp_handle.connection_mode = ESP_ConnectionMode_Single;
	esp_handle.server_state = ESP_ServerState_NotStarted;
}

static void ESP_Payload_Reset(void) {
	esp_handle.metadata_parsed = 0;
	esp_handle.payload_discard = 0;
	esp_handle.connection_id = 0;
	esp_handle.payload_position= 0;
	esp_handle.payload_length = 0;
	esp_handle.ipd_started_at = 0;
	esp_handle.payload[0] = '\0';
}

static void ESP_State_Clear(void) {
	esp_handle.rx_mode = ESP_RX_Mode_Line;
	esp_handle.mode = ESP_Mode_Unknown;
	esp_handle.wifi_state = ESP_WifiState_Unknown;
	ESP_NetworkDetails_Clear();
	ESP_TCPDetails_Clear();
}

static void ESP_Line_Reset(void) {
	esp_handle.line[0] = '\0';
	esp_handle.line_length = 0;
	esp_handle.line_overflow = 0;
}

static void ESP_IPD_Reset(void) {
	esp_handle.rx_mode = ESP_RX_Mode_Line;
	ESP_Line_Reset();
	ESP_Payload_Reset();
}

static void ESP_IPD_Abort(void) {
	if (esp_handle.command_waiting) {
		esp_handle.command_result = ESP_Result_Error;
		esp_handle.command_waiting = 0;
	}

	ESP_IPD_Reset();
}

static void ESP_IPD_Timeout_Recover(void) {
	if (esp_handle.rx_mode != ESP_RX_Mode_IPDPayload) return;
	if ((uint32_t)(esp_millis - esp_handle.ipd_started_at) < ESP_IPD_TIMEOUT_MS) return;

	ESP_IPD_Abort();
}

static uint8_t ESP_IP_IsZero(const uint8_t ip[ESP_IP_OCTET_COUNT]) {
	for (uint32_t i = 0; i < ESP_IP_OCTET_COUNT; i++) {
		if (ip[i] != 0) return 0;
	}
	return 1;
}

static uint8_t ESP_IP_Parse(const char* input, uint8_t ip[ESP_IP_OCTET_COUNT]) {
	if (input == 0 || *input++ != '\"') return 0;

	for (uint32_t i = 0; i < ESP_IP_OCTET_COUNT; i++) {
		uint32_t octet;
		if (!STRING_ParseUnsigned(&input, &octet) || octet > 255U) return 0;
		ip[i] = (uint8_t)octet;

		char delimiter = i == ESP_IP_OCTET_COUNT - 1U ? '\"' : '.';
		if (*input++ != delimiter) return 0;
	}

	return *input == '\0';
}

static void ESP_Line_Process(void) {
	esp_handle.line[esp_handle.line_length] = '\0';
	const char* line = esp_handle.line;

	SHELL_Write("ESP: ");
	SHELL_Write(line);
	SHELL_Write("\r\n");

	if (STRING_Equals(line, "WIFI CONNECTED")) {
		esp_handle.wifi_state = ESP_WifiState_Connected;
		ESP_NetworkDetails_Clear();
	}
	else if (STRING_Equals(line, "WIFI GOT IP")) {
		esp_handle.wifi_state = ESP_WifiState_GotIP;
		ESP_IP_Clear();
	}
	else if (STRING_Equals(line, "WIFI DISCONNECT") || STRING_Equals(line, "WIFI DISCONNECTED")) {
		esp_handle.wifi_state = ESP_WifiState_Disconnected;
		ESP_NetworkDetails_Clear();
	}
	else if (esp_handle.command == ESP_Command_CIFSR &&
		STRING_StartsWith(line, "+CIFSR:STAIP,") &&
		ESP_IP_Parse(line + 13, esp_handle.parsed_ip)) {
		esp_handle.response_parsed = 1;
	}
	else if (esp_handle.command == ESP_Command_CIPSTATUS && STRING_StartsWith(line, "STATUS:")) {
		const char* value = line + 7;
		uint32_t status;
		if (STRING_ParseUnsigned(&value, &status) && *value == '\0') {
			esp_handle.tcp_state = status == 3U ? ESP_TCPState_Connected : ESP_TCPState_Disconnected;
			if (esp_handle.tcp_state == ESP_TCPState_Disconnected) esp_handle.connection_id = 0;
		}
	}
	else if (esp_handle.command == ESP_Command_CIPSTATUS && STRING_StartsWith(line, "+CIPSTATUS:")) {
		const char* value = line + 11;
		uint32_t connection_id;
		if (STRING_ParseUnsigned(&value, &connection_id) && connection_id <= 4U && *value == ',') {
			esp_handle.connection_id = (uint8_t)connection_id;
			esp_handle.tcp_state = ESP_TCPState_Connected;
		}
	}
	else if (esp_handle.line_length > 2 && line[0] >= '0' && line[0] <= '4' && line[1] == ',' && STRING_Equals(&line[2], "CONNECT")) {
		esp_handle.connection_id = (uint8_t)(line[0] - '0');
		esp_handle.tcp_state = ESP_TCPState_Connected;
	}
	else if (esp_handle.line_length > 2 && line[0] >= '0' && line[0] <= '4' && line[1] == ',' && STRING_Equals(&line[2], "CLOSED")) {
		esp_handle.connection_id = (uint8_t)(line[0] - '0');
		esp_handle.tcp_state = ESP_TCPState_Disconnected;
	}

	if (esp_handle.command_waiting &&
		(STRING_Equals(line, "OK") || STRING_Equals(line, "ERROR") || STRING_Equals(line, "FAIL"))) {
		esp_handle.command_result = STRING_Equals(line, "OK") ? ESP_Result_Ok : ESP_Result_Error;
		esp_handle.command_waiting = 0;
	}

	ESP_Line_Reset();
}

static uint8_t ESP_TCPMetadata_Process(void) {
	esp_handle.line[esp_handle.line_length] = '\0';
	const char* cursor = esp_handle.line;
	uint32_t connection_id;
	uint32_t payload_length;

	if (!STRING_ParseUnsigned(&cursor, &connection_id) || *cursor++ != ',') return 0;
	if (!STRING_ParseUnsigned(&cursor, &payload_length) || *cursor != '\0' || payload_length == 0U) return 0;

	esp_handle.payload_length = payload_length;
	esp_handle.payload_discard = connection_id > 4U ||
		connection_id != esp_handle.connection_id ||
		payload_length > ESP_PAYLOAD_BUFFER_SIZE;
	esp_handle.metadata_parsed = 1;
	ESP_Line_Reset();
	return 1;
}

static void ESP_TCPPayload_Process(void) {
	if (esp_handle.payload_length == 0U || esp_handle.payload_length > ESP_PAYLOAD_BUFFER_SIZE) {
		ESP_IPD_Reset();
		return;
	}

	esp_handle.rx_mode = ESP_RX_Mode_Line;
	esp_handle.payload[esp_handle.payload_length - 1] = '\0';
	SHELL_Write("TCP command: ");
	SHELL_Write(esp_handle.payload);
	SHELL_Write("\r\n");
	SHELL_PrintResult(SHELL_ExecuteCommand(esp_handle.payload));
	ESP_Payload_Reset();
}

void ESP_Update(void) {
	if (esp_handle.usart == 0) return;
	ESP_IPD_Timeout_Recover();

	char c;
	while (USART_Receive_Char(esp_handle.usart, &c) == 0) {
		if (esp_handle.rx_mode == ESP_RX_Mode_Line) {

			if (c == '\r') continue;
			if (c == '\n') {
				if (esp_handle.line_overflow) {
					ESP_Line_Reset();
				}
				else if (esp_handle.line_length != 0) {
					ESP_Line_Process();
				}
				continue;
			}

			if (esp_handle.line_overflow) continue;
			if (esp_handle.line_length >= ESP_LINE_BUFFER_SIZE - 1U) {
				esp_handle.line_overflow = 1;
				continue;
			}

			esp_handle.line[esp_handle.line_length++] = c;
			esp_handle.line[esp_handle.line_length] = '\0';

			if (STRING_StartsWith(esp_handle.line, "+IPD,")) {
				esp_handle.rx_mode = ESP_RX_Mode_IPDPayload;
				esp_handle.ipd_started_at = esp_millis;
				ESP_Line_Reset();
			}
		}
		else {
			if (!esp_handle.metadata_parsed) {

				if (c == ':') {
					if (esp_handle.line_overflow || !ESP_TCPMetadata_Process()) ESP_IPD_Abort();
					continue;
				}

				if (esp_handle.line_overflow) continue;
				if (esp_handle.line_length >= ESP_LINE_BUFFER_SIZE - 1U) {
					esp_handle.line_overflow = 1;
					continue;
				}

				esp_handle.line[esp_handle.line_length++] = c;
				esp_handle.line[esp_handle.line_length] = '\0';
			}
			else {
				if (!esp_handle.payload_discard && esp_handle.payload_position < ESP_PAYLOAD_BUFFER_SIZE) {
					esp_handle.payload[esp_handle.payload_position] = c;
				}
				esp_handle.payload_position++;

				if (esp_handle.payload_position >= esp_handle.payload_length) {
					if (esp_handle.payload_discard) ESP_IPD_Reset();
					else ESP_TCPPayload_Process();
				}
			}
		}
	}

	ESP_IPD_Timeout_Recover();
}

static ESP_Result ESP_Command_Execute(ESP_Command command, const char* command_text, uint32_t timeout_ms) {
	if (esp_handle.usart == 0) return ESP_Result_InvalidConfig;
	if (command_text == 0 || command_text[0] == '\0') return ESP_Result_InvalidCommand;

	for (const char* c = command_text; *c != '\0'; c++) {
		if (*c == '\r' || *c == '\n') return ESP_Result_InvalidCommand;
	}

	ESP_Update();
	if (esp_handle.rx_mode != ESP_RX_Mode_Line || esp_handle.line_length != 0U || esp_handle.line_overflow) {
		return ESP_Result_Error;
	}
	ESP_Line_Reset();
	esp_handle.command = command;
	esp_handle.response_parsed = 0;
	esp_handle.command_result = ESP_Result_Timeout;
	esp_handle.command_waiting = 1;

	USART_Transmit_String(esp_handle.usart, command_text);
	USART_Transmit_String(esp_handle.usart, "\r\n");

	uint32_t started_at = esp_millis;
	while (esp_handle.command_waiting) {
		ESP_Update();
		if ((uint32_t)(esp_millis - started_at) >= timeout_ms) {
			esp_handle.command_waiting = 0;
		}
	}

	esp_handle.command = ESP_Command_None;
	return esp_handle.command_result;
}

ESP_Result ESP_Init(USART_TypeDef* usart) {
	if (usart == 0) return ESP_Result_InvalidConfig;

	esp_handle.usart = usart;
	ESP_State_Clear();
	ESP_Line_Reset();
	ESP_Payload_Reset();
	esp_handle.command = ESP_Command_None;
	esp_handle.command_waiting = 0;
	esp_handle.response_parsed = 0;
	ESP_TimeoutTimer_Init();

	SHELL_Write("Sending AT...\r\n");
	(void)ESP_Command_Execute(ESP_Command_AT, "AT", ESP_TIMEOUT_MS);

	SHELL_Write("Setting cwmode...\r\n");
	ESP_Result result = ESP_Command_Execute(ESP_Command_CWMODE, "AT+CWMODE=1", ESP_TIMEOUT_MS);
	if (result != ESP_Result_Ok) return result;
	esp_handle.mode = ESP_Mode_Station;

	SHELL_Write("Connecting to wifi...\r\n");
	result = ESP_Command_Execute(ESP_Command_CWJAP, ESP_WIFI_JOIN_COMMAND, ESP_JOIN_TIMEOUT_MS);
	if (result != ESP_Result_Ok) return result;

	uint32_t ssid_length = 0;
	if (!STRING_Append(esp_handle.ssid, ESP_SSID_BUFFER_SIZE, &ssid_length, ESP_WIFI_SSID)) return ESP_Result_Error;

	if (esp_handle.wifi_state != ESP_WifiState_GotIP) {
		esp_handle.wifi_state = ESP_WifiState_Connected;
	}

	result = ESP_Command_Execute(ESP_Command_CIFSR, "AT+CIFSR", ESP_TIMEOUT_MS);
	if (result != ESP_Result_Ok) return result;
	if (!esp_handle.response_parsed || ESP_IP_IsZero(esp_handle.parsed_ip)) {
		ESP_IP_Clear();
		if (esp_handle.wifi_state == ESP_WifiState_GotIP) {
			esp_handle.wifi_state = ESP_WifiState_Connected;
		}
		return ESP_Result_Error;
	}

	result = ESP_Command_Execute(ESP_Command_CIPMUX, "AT+CIPMUX=1", ESP_TIMEOUT_MS);
	if (result != ESP_Result_Ok) return result;
	esp_handle.connection_mode = ESP_ConnectionMode_Multiple;

	result = ESP_Command_Execute(ESP_Command_CIPSERVER, "AT+CIPSERVER=1,3333", ESP_TIMEOUT_MS);
	if (result != ESP_Result_Ok) return result;
	esp_handle.server_state = ESP_ServerState_Started;

	for (uint32_t i = 0; i < ESP_IP_OCTET_COUNT; i++) {
		esp_handle.ip[i] = esp_handle.parsed_ip[i];
	}
	esp_handle.wifi_state = ESP_WifiState_GotIP;
	return ESP_Result_Ok;
}

ESP_Mode ESP_Mode_Get(void) {
	return esp_handle.mode;
}

ESP_WifiState ESP_WifiState_Get(void) {
	return esp_handle.wifi_state;
}

const char* ESP_SSID_Get(void) {
	return esp_handle.ssid[0] != '\0' ? esp_handle.ssid : 0;
}

const uint8_t* ESP_IP_Get(void) {
	return !ESP_IP_IsZero(esp_handle.ip) ? esp_handle.ip : 0;
}

ESP_TCPState ESP_TCPState_Get(void) {
	return esp_handle.tcp_state;
}

uint8_t ESP_ConnectionID_Get(void) {
	return esp_handle.connection_id;
}

ESP_ConnectionMode ESP_ConnectionMode_Get(void) {
	return esp_handle.connection_mode;
}

ESP_ServerState ESP_ServerState_Get(void) {
	return esp_handle.server_state;
}

ESP_Result ESP_TCPStatus_Query(void) {
	return ESP_Command_Execute(ESP_Command_CIPSTATUS, "AT+CIPSTATUS", ESP_TIMEOUT_MS);
}

ESP_Result ESP_TestCommand_Execute(const char* command) {
	return ESP_Command_Execute(ESP_Command_Test, command, ESP_JOIN_TIMEOUT_MS);
}
