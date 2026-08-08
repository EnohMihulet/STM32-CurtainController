#include "curtain_controller.h"

#include <stddef.h>
#include "exti.h"
#include "limit_switch.h"
#include "tim.h"
#include "stepper_motor.h"

#define CORE_QUEUE_IMPLEMENTATION
#include "queue.h"

#define STEPPER_DEFAULT_FREQUENCY_HZ 2UL
#define STEPPER_UP_TO_DOWNOPEN_STEPS 500
#define STEPPER_DOWNCLOSED_TO_DOWNOPEN_STEPS 200

typedef enum {
	CURTAIN_Action_None = 0,
	CURTAIN_Action_MoveUntilLimit,
	CURTAIN_Action_MoveSteps,
} CURTAIN_ActionType;

typedef struct {
	CURTAIN_ActionType type;
	STEPPER_Direction direction;
	uint32_t steps;
	CURTAIN_State final_state;
} CURTAIN_Action;

QUEUE_DEFINE(CurtainAction, CURTAIN_Action, 4);

static CURTAIN_Result CURTAIN_Controller_QueueAction(CURTAIN_Action action);
static CURTAIN_Result CURTAIN_Controller_StartAction(CURTAIN_Action action);
static CURTAIN_Result CURTAIN_Controller_RunNextQueuedAction(void);
static void CURTAIN_Controller_ActionComplete(CURTAIN_State state);
static CURTAIN_Result CURTAIN_Controller_QueueDownOpenSequence(void);
static void CURTAIN_CompletionCallback(void* context);

static CURTAIN_Controller controller;

static STEPPER_Handle stepper_handle;
static CurtainAction_Queue action_queue;
static CURTAIN_Action active_action;

static const STEPPER_Config stepper_config = {
	.step_timer = TIM2,
	.step_channel = TIM_Channel_2,
	.step_port = GPIOB,
	.step_pin = GPIO_Pin_3,
	.step_alternate_function = AF1,
	.direction_port = GPIOB,
	.direction_pin = GPIO_Pin_4,
	.direction_forward_level = 1U,
	.enable_port = GPIOB,
	.enable_pin = GPIO_Pin_5,
	.enable_polarity = STEPPER_Polarity_ActiveLow,
	.timer_clock_hz = 16000000UL,
	.default_frequency_hz = STEPPER_DEFAULT_FREQUENCY_HZ,
	.step_polarity = TIM_OutputPolarity_ActiveHigh,
};

#define UPPER_LIMIT_SWITCH_PORT GPIOB
#define UPPER_LIMIT_SWITCH_PIN GPIO_Pin_6 
#define LOWER_LIMIT_SWITCH_PORT GPIOB
#define LOWER_LIMIT_SWITCH_PIN GPIO_Pin_7 

GPIO_Config upper_limit_switch = {
	.port = UPPER_LIMIT_SWITCH_PORT,
	.pin = UPPER_LIMIT_SWITCH_PIN,
	.mode = GPIO_Mode_Input,
	.output_type = GPIO_Output_PushPull,
	.speed = GPIO_Speed_Low,
	.pull = GPIO_Pull_Up,
};

GPIO_Config lower_limit_switch = {
	.port = LOWER_LIMIT_SWITCH_PORT,
	.pin = LOWER_LIMIT_SWITCH_PIN,
	.mode = GPIO_Mode_Input,
	.output_type = GPIO_Output_PushPull,
	.speed = GPIO_Speed_Low,
	.pull = GPIO_Pull_Up,
};

EXTI_Config upper_limit_switch_exti = {
	.port = UPPER_LIMIT_SWITCH_PORT,
	.pin = UPPER_LIMIT_SWITCH_PIN,
	.interrupt_enable = 1,
	.event_enable = 0,
};

EXTI_Config lower_limit_switch_exti = {
	.port = LOWER_LIMIT_SWITCH_PORT,
	.pin = LOWER_LIMIT_SWITCH_PIN,
	.interrupt_enable = 1,
	.event_enable = 0,
};

static void upper_limit_switch_callback(void) {
	CURTAIN_Controller_ActionComplete(CURTAIN_Up);
}

static void lower_limit_switch_callback(void) {
	CURTAIN_Controller_ActionComplete(CURTAIN_DownClosed);
}

static void CURTAIN_CompletionCallback(void* context) {
	(void)context;
	CURTAIN_Controller_ActionComplete(active_action.final_state);
}

CURTAIN_Result CURTAIN_Controller_Init() {
	if (!CurtainAction_init(&action_queue)) return CURTAIN_Result_QueueFull;

	if (STEPPER_Init(&stepper_handle, &stepper_config) != STEPPER_Result_Ok)
		return CURTAIN_Result_InvalidStepperConfig;

	controller.stepper = &stepper_handle;
	controller.upper_limit_switch = &upper_limit_switch;
	controller.lower_limit_switch = &lower_limit_switch;

	LIMIT_SWITCH_Init(&upper_limit_switch);
	LIMIT_SWITCH_Init(&lower_limit_switch);

	upper_limit_switch_exti.trigger = LIMIT_SWITCH_EXTITrigger_Get();
	lower_limit_switch_exti.trigger = LIMIT_SWITCH_EXTITrigger_Get();

	if (EXTI_Callback_Register((EXTI_Line)upper_limit_switch_exti.pin, upper_limit_switch_callback) != EXTI_Result_OK)
		return CURTAIN_Result_InvalidLimitSwitchCallback;
	if (EXTI_Callback_Register((EXTI_Line)lower_limit_switch_exti.pin, lower_limit_switch_callback) != EXTI_Result_OK)
		return CURTAIN_Result_InvalidLimitSwitchCallback;

	if (EXTI_Line_Configure(&upper_limit_switch_exti) != EXTI_Result_OK) return CURTAIN_Result_InvalidLimitSwitchEXTIConfig;
	if (EXTI_Line_Configure(&lower_limit_switch_exti) != EXTI_Result_OK) return CURTAIN_Result_InvalidLimitSwitchEXTIConfig;

	STEPPER_CompletionCallback_Register(&stepper_handle, CURTAIN_CompletionCallback, 0);
	STEPPER_Enable(&stepper_handle);

	controller.state = CURTAIN_Unknown;
	controller.busy = 0;
	active_action.type = CURTAIN_Action_None;

	return CURTAIN_Result_Ok;
}

STEPPER_Handle* CURTAIN_Controller_Stepper_Get(void) {
	return &stepper_handle;
}

CURTAIN_State CURTAIN_Controller_State_Get(void) {
	return controller.state;
}

uint8_t CURTAIN_Controller_IsBusy(void) {
	return STEPPER_IsBusy(&stepper_handle) || !CurtainAction_is_empty(&action_queue);
}

CURTAIN_Result CURTAIN_Controller_ChangeState(CURTAIN_State new_state) {
	if (new_state == controller.state) return CURTAIN_Result_Ok;
	if (new_state == CURTAIN_Unknown) return CURTAIN_Result_InvalidState;

	switch (new_state) {
	case CURTAIN_Up: return CURTAIN_Controller_Up();
	case CURTAIN_DownOpen: return CURTAIN_Controller_DownOpen();
	case CURTAIN_DownClosed: return CURTAIN_Controller_DownClosed();
	case CURTAIN_Unknown: return CURTAIN_Result_InvalidState;
	}
	
	return CURTAIN_Result_InvalidState;
}

CURTAIN_Result CURTAIN_Controller_Up() {
	CURTAIN_Action action = {
		.type = CURTAIN_Action_MoveUntilLimit,
		.direction = STEPPER_Direction_Forward,
		.steps = 0,
		.final_state = CURTAIN_Up,
	};

	if (STEPPER_IsBusy(&stepper_handle)) return CURTAIN_Controller_QueueAction(action);
	return CURTAIN_Controller_StartAction(action);
}

CURTAIN_Result CURTAIN_Controller_DownOpen() {
	if (STEPPER_IsBusy(&stepper_handle)) return CURTAIN_Controller_QueueDownOpenSequence();

	if (controller.state == CURTAIN_Up) {
		CURTAIN_Action action = {
			.type = CURTAIN_Action_MoveSteps,
			.direction = STEPPER_Direction_Reverse,
			.steps = STEPPER_UP_TO_DOWNOPEN_STEPS,
			.final_state = CURTAIN_DownOpen,
		};
		return CURTAIN_Controller_StartAction(action);
	}
	else if (controller.state == CURTAIN_DownClosed){
		CURTAIN_Action action = {
			.type = CURTAIN_Action_MoveSteps,
			.direction = STEPPER_Direction_Forward,
			.steps = STEPPER_DOWNCLOSED_TO_DOWNOPEN_STEPS,
			.final_state = CURTAIN_DownOpen,
		};
		return CURTAIN_Controller_StartAction(action);
	}

	return CURTAIN_Controller_QueueDownOpenSequence();
}

CURTAIN_Result CURTAIN_Controller_DownClosed() {
	CURTAIN_Action action = {
		.type = CURTAIN_Action_MoveUntilLimit,
		.direction = STEPPER_Direction_Reverse,
		.steps = 0,
		.final_state = CURTAIN_DownClosed,
	};

	if (STEPPER_IsBusy(&stepper_handle)) return CURTAIN_Controller_QueueAction(action);
	return CURTAIN_Controller_StartAction(action);
}

CURTAIN_Result CURTAIN_Controller_Move(STEPPER_Direction direction, uint32_t steps) {
	if (steps == 0) return CURTAIN_Result_InvalidState;

	CURTAIN_Action action = {
		.type = CURTAIN_Action_MoveSteps,
		.direction = direction,
		.steps = steps,
		.final_state = CURTAIN_Unknown,
	};

	if (STEPPER_IsBusy(&stepper_handle)) return CURTAIN_Controller_QueueAction(action);
	return CURTAIN_Controller_StartAction(action);

}

static CURTAIN_Result CURTAIN_Controller_QueueAction(CURTAIN_Action action) {
	if (!CurtainAction_enqueue(&action_queue, action)) return CURTAIN_Result_QueueFull;
	return CURTAIN_Result_Ok;
}

static CURTAIN_Result CURTAIN_Controller_StartAction(CURTAIN_Action action) {
	active_action = action;
	controller.busy = 1;

	controller.state = CURTAIN_Unknown;
	if (action.type == CURTAIN_Action_MoveSteps) {
		if (STEPPER_MoveSteps(&stepper_handle, action.direction, action.steps, STEPPER_DEFAULT_FREQUENCY_HZ) != STEPPER_Result_Ok) return CURTAIN_Result_InvalidStepperConfig;
		return CURTAIN_Result_Ok;
	}

	if (action.type == CURTAIN_Action_MoveUntilLimit) {
		STEPPER_Direction_Set(&stepper_handle, action.direction);
		if (STEPPER_Start(&stepper_handle, STEPPER_DEFAULT_FREQUENCY_HZ) != STEPPER_Result_Ok) return CURTAIN_Result_InvalidStepperConfig;
		return CURTAIN_Result_Ok;
	}

	active_action.type = CURTAIN_Action_None;
	controller.busy = 0;
	return CURTAIN_Result_InvalidState;
}

static CURTAIN_Result CURTAIN_Controller_RunNextQueuedAction(void) {
	CURTAIN_Action action;
	if (!CurtainAction_dequeue(&action_queue, &action)) {
		active_action.type = CURTAIN_Action_None;
		controller.busy = 0;
		return CURTAIN_Result_Ok;
	}

	return CURTAIN_Controller_StartAction(action);
}

static void CURTAIN_Controller_ActionComplete(CURTAIN_State state) {
	STEPPER_Stop(&stepper_handle);
	controller.state = state;
	(void)CURTAIN_Controller_RunNextQueuedAction();
}

static CURTAIN_Result CURTAIN_Controller_QueueDownOpenSequence(void) {
	if (action_queue.capacity - action_queue.size < 2) return CURTAIN_Result_QueueFull;

	CURTAIN_Action down_closed = {
		.type = CURTAIN_Action_MoveUntilLimit,
		.direction = STEPPER_Direction_Reverse,
		.steps = 0,
		.final_state = CURTAIN_DownClosed,
	};
	CURTAIN_Action down_open = {
		.type = CURTAIN_Action_MoveSteps,
		.direction = STEPPER_Direction_Forward,
		.steps = STEPPER_DOWNCLOSED_TO_DOWNOPEN_STEPS,
		.final_state = CURTAIN_DownOpen,
	};

	if (!CurtainAction_enqueue(&action_queue, down_closed)) return CURTAIN_Result_QueueFull;
	if (!CurtainAction_enqueue(&action_queue, down_open)) return CURTAIN_Result_QueueFull;

	if (!STEPPER_IsBusy(&stepper_handle)) return CURTAIN_Controller_RunNextQueuedAction();
	return CURTAIN_Result_Ok;
}

void CURTAIN_Controller_Stop(CURTAIN_State state) {
	STEPPER_Stop(&stepper_handle);
	CurtainAction_clear(&action_queue);
	active_action.type = CURTAIN_Action_None;
	controller.state = state;
	controller.busy = 0;
}
