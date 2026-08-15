#include "board_button.h"
#include "curtain_shell_commands.h"
#include "board_led.h"
#include "curtain_controller.h"
#include "limit_switch.h"
#include "stepper_motor.h"
#include "exti.h"
#include "shell.h"
#include "usart.h"


int main(void) {
	CURTAIN_Controller_Init();
	Curtain_ShellCommands_Init(CURTAIN_Controller_Stepper_Get());

	USART2_Init();
	SHELL_Init();
	
	while (1) {
		SHELL_Update();
		CURTAIN_Controller_Update();
	}

	return 0;
}
