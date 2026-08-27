#include "board_button.h"
#include "board_led.h"
#include "curtain_controller.h"
#include "curtain_shell_commands.h"
#include "esp_at.h"
#include "remote_shell.h"
#include "limit_switch.h"
#include "stepper_motor.h"
#include "exti.h"
#include "shell.h"
#include "usart.h"

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

#define SYSTICK_CTRL (*(volatile uint32_t*)0xE000E010UL)
#define SYSTICK_LOAD (*(volatile uint32_t*)0xE000E014UL)
#define SYSTICK_VALUE (*(volatile uint32_t*)0xE000E018UL)
#define SYSTICK_CTRL_ENABLE (1UL << 0)
#define SYSTICK_CTRL_TICKINT (1UL << 1)
#define SYSTICK_CTRL_CLKSOURCE (1UL << 2)

static const ESP_Config esp_config = {
	.usart = USART1,
	.ssid = WIFI_SSID,
	.password = WIFI_PASSWORD,
	.server_port = 3333U,
};

void SysTick_Handler(void) {
	ESP_Tick();
}

static void APP_SystemTick_Init(void) {
	SYSTICK_LOAD = (CLOCK_SPEED_HZ / 1000UL) - 1UL;
	SYSTICK_VALUE = 0;
	SYSTICK_CTRL = SYSTICK_CTRL_ENABLE | SYSTICK_CTRL_TICKINT | SYSTICK_CTRL_CLKSOURCE;
}

int main(void) {
	CURTAIN_Controller_Init();

	USART2_Init();
	USART1_Init();
	APP_SystemTick_Init();
	ESP_Result esp_init_result = ESP_Init(&esp_config);

	CURTAIN_ShellCommands_Init(CURTAIN_Controller_Stepper_Get());
	SHELL_Init();

	uint8_t remote_shell_enabled = 0;
	if (esp_init_result == ESP_Result_Ok) {
		REMOTE_SHELL_Init();
		remote_shell_enabled = 1;
	}

	while (1) {
		CURTAIN_Controller_Update();
		ESP_Update();
		if (remote_shell_enabled) REMOTE_SHELL_Update();
		SHELL_Update();
	}

	return 0;
}
