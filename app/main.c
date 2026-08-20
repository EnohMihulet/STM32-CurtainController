#include "board_button.h"
#include "board_led.h"
#include "curtain_controller.h"
#include "curtain_shell_commands.h"
#include "esp.h"
#include "limit_switch.h"
#include "stepper_motor.h"
#include "exti.h"
#include "shell.h"
#include "usart.h"

int main(void) {
	CURTAIN_Controller_Init();

	USART2_Init();
	USART1_Init();
	ESP_Init(USART1);
	CURTAIN_ShellCommands_Init(CURTAIN_Controller_Stepper_Get());
	SHELL_Init();

	while (1) {
		CURTAIN_Controller_Update();
		SHELL_Update();
		ESP_Update();
	}

	return 0;
}
