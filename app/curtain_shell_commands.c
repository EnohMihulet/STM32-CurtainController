#include "curtain_shell_commands.h"

#include "board_button.h"
#include "board_led.h"
#include "curtain_controller.h"
#include "esp_at.h"
#include "exti.h"
#include "limit_switch.h"
#include "pwm.h"
#include "shell.h"
#include "step_output.h"
#include "stepper_motor.h"
#include "string_helper.h"

#define SHELL_PWM_TIM TIM2
#define SHELL_PWM_CHANNEL TIM_Channel_1

static STEPPER_Handle* shell_stepper;

static const SHELL_Command curtain_commands[];

static SHELL_Result SHELL_ParseUnsigned(const char* s, uint32_t* value) {
	uint32_t result = 0;

	if (*s == '\0') {
		return SHELL_RESULT_EMPTY;
	}

	while (*s != '\0') {
		if (*s < '0' || *s > '9') {
			return SHELL_RESULT_BAD_NUMBER;
		}

		uint32_t digit = (uint32_t)(*s - '0');
		if (result > (((uint32_t)~0U - digit) / 10U)) {
			return SHELL_RESULT_OUT_OF_RANGE;
		}

		result = (result * 10U) + digit;
		s++;
	}

	*value = result;
	return SHELL_RESULT_OK;
}

static SHELL_Result SHELL_ESPResult_ToShell(ESP_Result result) {
	switch (result) {
	case ESP_Result_Ok: return SHELL_RESULT_OK;
	case ESP_Result_Busy: return SHELL_RESULT_BUSY;
	case ESP_Result_Capacity: return SHELL_RESULT_CAPACITY;
	case ESP_Result_Disconnected: return SHELL_RESULT_INVALID_STATE;
	case ESP_Result_InvalidCommand: return SHELL_RESULT_BAD_ARGUMENT;
	case ESP_Result_Error:
	case ESP_Result_Timeout:
	case ESP_Result_InvalidConfig:
		return SHELL_RESULT_INTERNAL;
	}

	return SHELL_RESULT_INTERNAL;
}

static SHELL_Result SHELL_CurtainResult_ToShell(CURTAIN_Result result) {
	switch (result) {
	case CURTAIN_Result_Ok: return SHELL_RESULT_OK;
	case CURTAIN_Result_Busy: return SHELL_RESULT_BUSY;
	case CURTAIN_Result_QueueFull: return SHELL_RESULT_CAPACITY;
	case CURTAIN_Result_InvalidState: return SHELL_RESULT_INVALID_STATE;
	case CURTAIN_Result_InvalidStepperConfig:
	case CURTAIN_Result_InvalidLimitSwitchConfig:
	case CURTAIN_Result_InvalidLimitSwitchEXTIConfig:
	case CURTAIN_Result_InvalidLimitSwitchCallback:
		return SHELL_RESULT_INTERNAL;
	}

	return SHELL_RESULT_INTERNAL;
}

static const char* SHELL_CurtainStateName(CURTAIN_State state) {
	switch (state) {
	case CURTAIN_Unknown: return "unknown";
	case CURTAIN_Up: return "up";
	case CURTAIN_DownOpen: return "down_open";
	case CURTAIN_DownClosed: return "down_closed";
	}

	return "invalid";
}

SHELL_Result SHELL_CommandHelp(const SHELL_Output* output, int argc, const char* argv[]) {
	if (argc == 0) {
		(void)argv;
	
		SHELL_PrintCommandList(output);
		return SHELL_RESULT_OK;
	}

	const SHELL_Command* command = SHELL_LookupCommand(argv[0]);
	if (command == 0) {
		return SHELL_RESULT_UNKNOWN_COMMAND;
	}

	SHELL_PrintCommand(output, command);
	return SHELL_RESULT_OK;
}

SHELL_Result SHELL_CommandLed(const SHELL_Output* output, int argc, const char* argv[]) {
	(void)output;
	(void)argc;

	if (STRING_Equals(argv[0], "on")) {
		LED_On();
		return SHELL_RESULT_OK;
	}

	if (STRING_Equals(argv[0], "off")) {
		LED_Off();
		return SHELL_RESULT_OK;
	}

	return SHELL_RESULT_BAD_ARGUMENT;
}

SHELL_Result SHELL_CommandButton(const SHELL_Output* output, int argc, const char* argv[]) {
	(void)argc;
	(void)argv;

	if (Board_Button_IsPressed()) {
		SHELL_Write(output, "Button: pressed\r\n");
	}
	else {
		SHELL_Write(output, "Button: released\r\n");
	}

	return SHELL_RESULT_OK;
}

SHELL_Result SHELL_CommandPwm(const SHELL_Output* output, int argc, const char* argv[]) {
	if (STRING_Equals(argv[0], "start")) {
		if (argc != 1) return SHELL_RESULT_ARGUMENT_COUNT;

		if (PWM_Channel_Start(SHELL_PWM_TIM, SHELL_PWM_CHANNEL) != PWM_Result_OK) return SHELL_RESULT_INVALID_STATE;
		SHELL_Write(output, "PWM: started\r\n");
		return SHELL_RESULT_OK;
	}

	if (STRING_Equals(argv[0], "stop")) {
		if (argc != 1) return SHELL_RESULT_ARGUMENT_COUNT;

		if (PWM_Channel_Stop(SHELL_PWM_TIM, SHELL_PWM_CHANNEL) != PWM_Result_OK) return SHELL_RESULT_INVALID_STATE;
		SHELL_Write(output, "PWM: stopped\r\n");
		return SHELL_RESULT_OK;
	}

	if (STRING_Equals(argv[0], "status")) {
		if (argc != 1) return SHELL_RESULT_ARGUMENT_COUNT;

		SHELL_Write(output, "PWM: ");
		SHELL_Write(output, PWM_Channel_IsStarted(SHELL_PWM_TIM, SHELL_PWM_CHANNEL) ? "started" : "stopped");
		SHELL_Write(output, ", duty=");
		SHELL_WriteUnsigned(output, PWM_Duty_Get(SHELL_PWM_TIM, SHELL_PWM_CHANNEL));
		SHELL_Write(output, "/1000 (approx ");
		SHELL_WriteUnsigned(output, PWM_Duty_Get(SHELL_PWM_TIM, SHELL_PWM_CHANNEL) / 10U);
		SHELL_Write(output, "%)\r\n");
		return SHELL_RESULT_OK;
	}

	if (STRING_Equals(argv[0], "set")) {
		if (argc != 2) return SHELL_RESULT_ARGUMENT_COUNT;

		uint32_t duty = 0;
		SHELL_Result result = SHELL_ParseUnsigned(argv[1], &duty);
		if (result != SHELL_RESULT_OK) return result;
		if (duty > PWM_DUTY_MAX) return SHELL_RESULT_OUT_OF_RANGE;

		if (PWM_Duty_Set(SHELL_PWM_TIM, SHELL_PWM_CHANNEL, (uint16_t)duty) != PWM_Result_OK) return SHELL_RESULT_INVALID_STATE;
		SHELL_Write(output, "PWM: duty set to ");
		SHELL_WriteUnsigned(output, duty);
		SHELL_Write(output, "/1000\r\n");
		return SHELL_RESULT_OK;
	}

	if (STRING_Equals(argv[0], "duty")) {
		if (argc != 2) return SHELL_RESULT_ARGUMENT_COUNT;

		uint32_t percent = 0;
		SHELL_Result result = SHELL_ParseUnsigned(argv[1], &percent);
		if (result != SHELL_RESULT_OK) return result;
		if (percent > 100U) return SHELL_RESULT_OUT_OF_RANGE;

		uint32_t duty = percent * 10U;
		if (PWM_Duty_Set(SHELL_PWM_TIM, SHELL_PWM_CHANNEL, (uint16_t)duty) != PWM_Result_OK) return SHELL_RESULT_INVALID_STATE;
		SHELL_Write(output, "PWM: duty set to ");
		SHELL_WriteUnsigned(output, percent);
		SHELL_Write(output, "%\r\n");
		return SHELL_RESULT_OK;
	}

	return SHELL_RESULT_BAD_ARGUMENT;
}

SHELL_Result SHELL_CommandStep(const SHELL_Output* output, int argc, const char* argv[]) {
	if (STRING_Equals(argv[0], "start")) {
		if (argc != 1) return SHELL_RESULT_ARGUMENT_COUNT;

		if (STEP_Output_Start() != STEP_Result_OK) return SHELL_RESULT_INVALID_STATE;
		SHELL_Write(output, "STEP: started\r\n");
		return SHELL_RESULT_OK;
	}

	if (STRING_Equals(argv[0], "stop")) {
		if (argc != 1) return SHELL_RESULT_ARGUMENT_COUNT;

		if (STEP_Output_Stop() != STEP_Result_OK) return SHELL_RESULT_INVALID_STATE;
		SHELL_Write(output, "STEP: stopped\r\n");
		return SHELL_RESULT_OK;
	}

	if (STRING_Equals(argv[0], "status")) {
		if (argc != 1) return SHELL_RESULT_ARGUMENT_COUNT;

		SHELL_Write(output, "STEP: ");
		SHELL_Write(output, STEP_Output_IsStarted() ? "started" : "stopped");
		SHELL_Write(output, ", frequency=");
		SHELL_WriteUnsigned(output, STEP_Output_Frequency_Get());
		SHELL_Write(output, " Hz, duty=50%\r\n");
		return SHELL_RESULT_OK;
	}

	if (STRING_Equals(argv[0], "freq") || STRING_Equals(argv[0], "set")) {
		if (argc != 2) return SHELL_RESULT_ARGUMENT_COUNT;

		uint32_t frequency_hz = 0;
		SHELL_Result result = SHELL_ParseUnsigned(argv[1], &frequency_hz);
		if (result != SHELL_RESULT_OK) return result;
		if (frequency_hz == 0) return SHELL_RESULT_OUT_OF_RANGE;

		if (STEP_Output_Frequency_Set(frequency_hz) != STEP_Result_OK) return SHELL_RESULT_INVALID_STATE;
		SHELL_Write(output, "STEP: frequency set to ");
		SHELL_WriteUnsigned(output, STEP_Output_Frequency_Get());
		SHELL_Write(output, " Hz\r\n");
		return SHELL_RESULT_OK;
	}

	return SHELL_RESULT_BAD_ARGUMENT;
}

SHELL_Result SHELL_CommandStepper(const SHELL_Output* output, int argc, const char* argv[]) {
	if (shell_stepper == 0) return SHELL_RESULT_INVALID_STATE;

	if (STRING_Equals(argv[0], "start")) {
		if (argc > 2) return SHELL_RESULT_ARGUMENT_COUNT;

		uint32_t frequency_hz = shell_stepper->step_frequency_hz;
		if (argc == 2) {
			SHELL_Result result = SHELL_ParseUnsigned(argv[1], &frequency_hz);
			if (result != SHELL_RESULT_OK) return result;
			if (frequency_hz == 0) return SHELL_RESULT_OUT_OF_RANGE;
		}

		if (STEPPER_Enable(shell_stepper) != STEPPER_Result_Ok) return SHELL_RESULT_INVALID_STATE;
		if (STEPPER_Start(shell_stepper, frequency_hz) != STEPPER_Result_Ok) return SHELL_RESULT_INVALID_STATE;
		SHELL_Write(output, "Stepper: started at ");
		SHELL_WriteUnsigned(output, shell_stepper->step_frequency_hz);
		SHELL_Write(output, " Hz\r\n");
		return SHELL_RESULT_OK;
	}

	if (STRING_Equals(argv[0], "stop")) {
		if (argc != 1) return SHELL_RESULT_ARGUMENT_COUNT;

		STEPPER_Stop(shell_stepper);
		SHELL_Write(output, "Stepper: stopped\r\n");
		return SHELL_RESULT_OK;
	}

	if (STRING_Equals(argv[0], "status")) {
		if (argc != 1) return SHELL_RESULT_ARGUMENT_COUNT;

		SHELL_Write(output, "Stepper: ");
		SHELL_Write(output, STEPPER_IsBusy(shell_stepper) ? "started" : "stopped");
		SHELL_Write(output, ", direction=");
		SHELL_Write(output, shell_stepper->direction == STEPPER_Direction_Forward ? "fwd" : "rev");
		SHELL_Write(output, ", speed=");
		SHELL_WriteUnsigned(output, shell_stepper->step_frequency_hz);
		SHELL_Write(output, " Hz, completed=");
		SHELL_WriteUnsigned(output, STEPPER_CompletedSteps_Get(shell_stepper));
		SHELL_Write(output, ", ");
		SHELL_Write(output, shell_stepper->enabled ? "enabled" : "disabled");
		SHELL_Write(output, "\r\n");
		return SHELL_RESULT_OK;
	}

	if (STRING_Equals(argv[0], "dir")) {
		if (argc != 2) return SHELL_RESULT_ARGUMENT_COUNT;

		if (STRING_Equals(argv[1], "fwd") || STRING_Equals(argv[1], "forward")) {
			STEPPER_Direction_Set(shell_stepper, STEPPER_Direction_Forward);
		}
		else if (STRING_Equals(argv[1], "rev") || STRING_Equals(argv[1], "reverse")) {
			STEPPER_Direction_Set(shell_stepper, STEPPER_Direction_Reverse);
		}
		else {
			return SHELL_RESULT_BAD_ARGUMENT;
		}

		SHELL_Write(output, "Stepper: direction set to ");
		SHELL_Write(output, shell_stepper->direction == STEPPER_Direction_Forward ? "fwd" : "rev");
		SHELL_Write(output, "\r\n");
		return SHELL_RESULT_OK;
	}

	if (STRING_Equals(argv[0], "speed")) {
		if (argc != 2) return SHELL_RESULT_ARGUMENT_COUNT;

		uint32_t frequency_hz = 0;
		SHELL_Result result = SHELL_ParseUnsigned(argv[1], &frequency_hz);
		if (result != SHELL_RESULT_OK) return result;
		if (frequency_hz == 0) return SHELL_RESULT_OUT_OF_RANGE;

		if (STEPPER_Start(shell_stepper, frequency_hz) != STEPPER_Result_Ok) return SHELL_RESULT_INVALID_STATE;
		SHELL_Write(output, "Stepper: speed set to ");
		SHELL_WriteUnsigned(output, shell_stepper->step_frequency_hz);
		SHELL_Write(output, " Hz\r\n");
		return SHELL_RESULT_OK;
	}

	if (STRING_Equals(argv[0], "move")) {
		if (argc < 2 || argc > 3) return SHELL_RESULT_ARGUMENT_COUNT;

		uint32_t steps = 0;
		SHELL_Result result = SHELL_ParseUnsigned(argv[1], &steps);
		if (result != SHELL_RESULT_OK) return result;
		if (steps == 0) return SHELL_RESULT_OUT_OF_RANGE;

		uint32_t frequency_hz = shell_stepper->step_frequency_hz;
		if (argc == 3) {
			result = SHELL_ParseUnsigned(argv[2], &frequency_hz);
			if (result != SHELL_RESULT_OK) return result;
			if (frequency_hz == 0) return SHELL_RESULT_OUT_OF_RANGE;
		}

		if (STEPPER_Enable(shell_stepper) != STEPPER_Result_Ok) return SHELL_RESULT_INVALID_STATE;
		if (STEPPER_Step(shell_stepper, shell_stepper->direction, steps, frequency_hz) != STEPPER_Result_Ok) return SHELL_RESULT_INVALID_STATE;
		SHELL_Write(output, "Stepper: moving ");
		SHELL_WriteUnsigned(output, steps);
		SHELL_Write(output, " steps at ");
		SHELL_WriteUnsigned(output, shell_stepper->step_frequency_hz);
		SHELL_Write(output, " Hz\r\n");
		return SHELL_RESULT_OK;
	}

	return SHELL_RESULT_BAD_ARGUMENT;
}

SHELL_Result SHELL_CommandExti(const SHELL_Output* output, int argc, const char* argv[]) {
	(void)argc;

	uint32_t line_number = 0;
	SHELL_Result result = SHELL_ParseUnsigned(argv[1], &line_number);
	if (result != SHELL_RESULT_OK) return result;
	if (line_number >= EXTI_LINE_COUNT) return SHELL_RESULT_OUT_OF_RANGE;

	EXTI_Line line = (EXTI_Line)line_number;

	if (STRING_Equals(argv[0], "swier")) {
		if (EXTI_SoftwareInterrupt_Generate(line) != EXTI_Result_OK) return SHELL_RESULT_INVALID_STATE;
		SHELL_Write(output, "EXTI: SWIER generated on line ");
		SHELL_WriteUnsigned(output, line_number);
		SHELL_Write(output, "\r\n");
		return SHELL_RESULT_OK;
	}

	if (STRING_Equals(argv[0], "pending")) {
		SHELL_Write(output, "EXTI: line ");
		SHELL_WriteUnsigned(output, line_number);
		SHELL_Write(output, EXTI_IsPending(line) ? " pending\r\n" : " not pending\r\n");
		return SHELL_RESULT_OK;
	}

	if (STRING_Equals(argv[0], "clear")) {
		if (EXTI_Pending_Clear(line) != EXTI_Result_OK) return SHELL_RESULT_INVALID_STATE;
		SHELL_Write(output, "EXTI: pending cleared on line ");
		SHELL_WriteUnsigned(output, line_number);
		SHELL_Write(output, "\r\n");
		return SHELL_RESULT_OK;
	}

	return SHELL_RESULT_BAD_ARGUMENT;
}

SHELL_Result SHELL_CommandSwitch(const SHELL_Output* output, int argc, const char* argv[]) {
	(void)argc;
	(void)argv;
	GPIO_Config switch_config = {
		.port = GPIOB,
		.pin = GPIO_Pin_6,
		.mode = GPIO_Mode_Input,
		.output_type = GPIO_Output_PushPull,
		.speed = GPIO_Speed_Low,
		.pull = GPIO_Pull_Up,
	};

	SHELL_Write(output, "Limit: ");
	SHELL_Write(output, LIMIT_SWITCH_IsActive(&switch_config) ? "active" : "released");
	SHELL_Write(output, "\r\n");
	return SHELL_RESULT_OK;
}

SHELL_Result SHELL_CommandCurtain(const SHELL_Output* output, int argc, const char* argv[]) {
	if (STRING_Equals(argv[0], "status")) {
		if (argc != 1) return SHELL_RESULT_ARGUMENT_COUNT;

		SHELL_Write(output, "Curtain: state=");
		SHELL_Write(output, SHELL_CurtainStateName(CURTAIN_Controller_State_Get()));
		SHELL_Write(output, ", ");
		SHELL_Write(output, CURTAIN_Controller_IsBusy() ? "busy" : "idle");
		SHELL_Write(output, "\r\n");
		return SHELL_RESULT_OK;
	}

	if (STRING_Equals(argv[0], "up")) {
		if (argc != 1) return SHELL_RESULT_ARGUMENT_COUNT;
		return SHELL_CurtainResult_ToShell(CURTAIN_Controller_ChangeState(CURTAIN_Up));
	}

	if (STRING_Equals(argv[0], "open") || STRING_Equals(argv[0], "downopen")) {
		if (argc != 1) return SHELL_RESULT_ARGUMENT_COUNT;
		return SHELL_CurtainResult_ToShell(CURTAIN_Controller_ChangeState(CURTAIN_DownOpen));
	}

	if (STRING_Equals(argv[0], "closed") || STRING_Equals(argv[0], "downclosed")) {
		if (argc != 1) return SHELL_RESULT_ARGUMENT_COUNT;
		return SHELL_CurtainResult_ToShell(CURTAIN_Controller_ChangeState(CURTAIN_DownClosed));
	}

	if (STRING_Equals(argv[0], "stop")) {
		if (argc != 1) return SHELL_RESULT_ARGUMENT_COUNT;
		CURTAIN_Controller_Stop(CURTAIN_Unknown);
		SHELL_Write(output, "Curtain: stopped\r\n");
		return SHELL_RESULT_OK;
	}

	if (STRING_Equals(argv[0], "move")) {
		if (argc != 3) return SHELL_RESULT_ARGUMENT_COUNT;

		STEPPER_Direction direction;
		if (STRING_Equals(argv[1], "up") || STRING_Equals(argv[1], "rev") || STRING_Equals(argv[1], "reverse")) {
			direction = STEPPER_Direction_Reverse;
		}
		else if (STRING_Equals(argv[1], "down") || STRING_Equals(argv[1], "fwd") || STRING_Equals(argv[1], "forward")) {
			direction = STEPPER_Direction_Forward;
		}
		else {
			return SHELL_RESULT_BAD_ARGUMENT;
		}

		uint32_t steps = 0;
		SHELL_Result parse_result = SHELL_ParseUnsigned(argv[2], &steps);
		if (parse_result != SHELL_RESULT_OK) return parse_result;
		if (steps == 0) return SHELL_RESULT_OUT_OF_RANGE;

		CURTAIN_Result move_result = CURTAIN_Controller_Move(direction, steps);
		if (move_result != CURTAIN_Result_Ok) return SHELL_CurtainResult_ToShell(move_result);

		SHELL_Write(output, "Curtain: moving ");
		SHELL_Write(output, direction == STEPPER_Direction_Reverse ? "up " : "down ");
		SHELL_WriteUnsigned(output, steps);
		SHELL_Write(output, " steps\r\n");
		return SHELL_RESULT_OK;
	}

	return SHELL_RESULT_BAD_ARGUMENT;
}

static SHELL_Result SHELL_CommandESP(const SHELL_Output* output, int argc, const char* argv[]) {
	(void)output;
	if (argc != 1) return SHELL_RESULT_ARGUMENT_COUNT;
	return SHELL_ESPResult_ToShell(ESP_TestCommand_Execute(argv[0]));
}

static SHELL_Result SHELL_CommandTCP(const SHELL_Output* output, int argc, const char* argv[]) {
	(void)output;
	(void)argc;
	if (!STRING_Equals(argv[0], "status")) return SHELL_RESULT_BAD_ARGUMENT;
	return SHELL_ESPResult_ToShell(ESP_TCPStatus_Query());
}

static const char* SHELL_ESPModeName(ESP_Mode mode) {
	switch (mode) {
	case ESP_Mode_Unknown: return "unknown";
	case ESP_Mode_Null: return "null (RF off)";
	case ESP_Mode_Station: return "station";
	case ESP_Mode_SoftAP: return "soft AP";
	case ESP_Mode_StationSoftAP: return "station + soft AP";
	}

	return "invalid";
}

static const char* SHELL_ESPWifiStateName(ESP_WifiState state) {
	switch (state) {
	case ESP_WifiState_Unknown: return "unknown";
	case ESP_WifiState_Disconnected: return "disconnected";
	case ESP_WifiState_Connected: return "associated; IP not confirmed";
	case ESP_WifiState_GotIP: return "associated; IP acquired";
	}

	return "invalid";
}

static void SHELL_WifiSSID_Write(const SHELL_Output* output) {
	const char* ssid = ESP_SSID_Get();
	SHELL_Write(output, "SSID: ");
	if (ssid != 0) {
		SHELL_Write(output, "\"");
		SHELL_Write(output, ssid);
		SHELL_Write(output, "\"");
	}
	else if (ESP_WifiState_Get() == ESP_WifiState_Disconnected) {
		SHELL_Write(output, "none (disconnected)");
	}
	else {
		SHELL_Write(output, "unknown (not queried)");
	}
	SHELL_Write(output, "\r\n");
}

static void SHELL_WifiIP_Write(const SHELL_Output* output) {
	const uint8_t* ip = ESP_IP_Get();
	SHELL_Write(output, "Station IP: ");
	if (ip != 0) {
		char buffer[16];
		(void)STRING_snprintf(
			buffer,
			sizeof(buffer),
			"%u.%u.%u.%u",
			(unsigned int)ip[0],
			(unsigned int)ip[1],
			(unsigned int)ip[2],
			(unsigned int)ip[3]
		);
		SHELL_Write(output, buffer);
	}
	else if (ESP_WifiState_Get() == ESP_WifiState_Disconnected) {
		SHELL_Write(output, "none (disconnected)");
	}
	else if (ESP_WifiState_Get() == ESP_WifiState_GotIP) {
		SHELL_Write(output, "acquired; address not queried");
	}
	else {
		SHELL_Write(output, "unknown");
	}
	SHELL_Write(output, "\r\n");
}

static SHELL_Result SHELL_CommandWifi(const SHELL_Output* output, int argc, const char* argv[]) {
	(void)argc;

	if (STRING_Equals(argv[0], "status")) {
		SHELL_Write(output, "WiFi status (cached):\r\n");
		SHELL_Write(output, "Mode: ");
		SHELL_Write(output, SHELL_ESPModeName(ESP_Mode_Get()));
		SHELL_Write(output, "\r\nState: ");
		SHELL_Write(output, SHELL_ESPWifiStateName(ESP_WifiState_Get()));
		SHELL_Write(output, "\r\n");
		SHELL_WifiSSID_Write(output);
		SHELL_WifiIP_Write(output);
		return SHELL_RESULT_OK;
	}

	if (STRING_Equals(argv[0], "mode")) {
		SHELL_Write(output, "WiFi mode: ");
		SHELL_Write(output, SHELL_ESPModeName(ESP_Mode_Get()));
		SHELL_Write(output, "\r\n");
		return SHELL_RESULT_OK;
	}

	if (STRING_Equals(argv[0], "connected") || STRING_Equals(argv[0], "state")) {
		SHELL_Write(output, "WiFi state: ");
		SHELL_Write(output, SHELL_ESPWifiStateName(ESP_WifiState_Get()));
		SHELL_Write(output, "\r\n");
		return SHELL_RESULT_OK;
	}

	if (STRING_Equals(argv[0], "ssid")) {
		SHELL_WifiSSID_Write(output);
		return SHELL_RESULT_OK;
	}

	if (STRING_Equals(argv[0], "ip")) {
		SHELL_WifiIP_Write(output);
		return SHELL_RESULT_OK;
	}

	return SHELL_RESULT_BAD_ARGUMENT;
}

static const SHELL_Command curtain_commands[] = {
	{"help", SHELL_CommandHelp, 0, 1, "List available commands"},
	{"led", SHELL_CommandLed, 1, 1, "Set onboard LED: led on|off"},
	{"button", SHELL_CommandButton, 0, 0, "Read onboard button state"},
	{"pwm", SHELL_CommandPwm, 1, 2, "Test LED PWM: pwm start|stop|status|set <0-1000>|duty <0-100>"},
	{"step", SHELL_CommandStep, 1, 2, "Test STEP output: step start|stop|status|freq <hz>|set <hz>"},
	{"exti", SHELL_CommandExti, 2, 2, "Test EXTI: exti swier|pending|clear <0-22>"},
	{"switch", SHELL_CommandSwitch, 0, 0, "Read switch state as active or released"},
	{"stepper", SHELL_CommandStepper, 1, 3, "Control stepper: stepper start [hz]|stop|status|dir fwd|rev|speed <hz>|move <steps> [hz]"},
	{"curtain", SHELL_CommandCurtain, 1, 3, "Control curtain: curtain up|open|closed|status|stop|move up|down <steps>"},
	{"esp", SHELL_CommandESP, 1, 1, "Send one ESP-AT test command: esp <command>"},
	{"tcp", SHELL_CommandTCP, 1, 1, "Query ESP TCP connections: tcp status"},
	{"wifi", SHELL_CommandWifi, 1, 1, "Show cached WiFi data: wifi status|mode|state|connected|ssid|ip"},
};

void CURTAIN_ShellCommands_Init(STEPPER_Handle* stepper) {
	shell_stepper = stepper;
	SHELL_Commands_Set(curtain_commands, (uint32_t)(sizeof(curtain_commands) / sizeof(curtain_commands[0])));
}
