#include "curtain_controller.h"

#include <stddef.h>
#include "exti.h"
#include "limit_switch.h"
#include "tim.h"
#include "stepper_motor.h"

#define CORE_QUEUE_IMPLEMENTATION
#include "queue.h"

#define STEPPER_DEFAULT_FREQUENCY_HZ 100UL
#define STEPPER_UP_TO_DOWNOPEN_STEPS 10000000
#define STEPPER_DOWNCLOSED_TO_DOWNOPEN_STEPS 5000000

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

QUEUE_DEFINE(CurtainActionQueue, CURTAIN_Action, 4);

static size_t CURTAIN_Controller_QueuedActionSlotsAvailable(void);
static CURTAIN_Result CURTAIN_Controller_StartAction(CURTAIN_Action action);
static void CURTAIN_Controller_CompleteActiveAction(void);

static CURTAIN_Controller controller;

static STEPPER_Handle stepper_handle;
static CurtainActionQueue_Queue action_queue;
static CURTAIN_Action active_action;
static volatile uint8_t active_action_complete;

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

#define UPPER_LIMIT_SWITCH_PORT GPIOA
#define UPPER_LIMIT_SWITCH_PIN GPIO_Pin_9
#define LOWER_LIMIT_SWITCH_PORT GPIOA
#define LOWER_LIMIT_SWITCH_PIN GPIO_Pin_8

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
	if (!controller.busy || active_action.direction != STEPPER_Direction_Reverse) return;

	STEPPER_Stop(&stepper_handle);
	active_action.final_state = CURTAIN_Up;
	active_action_complete = 1;
}

static void lower_limit_switch_callback(void) {
	if (!controller.busy || active_action.direction != STEPPER_Direction_Forward) return;

	STEPPER_Stop(&stepper_handle);
	active_action.final_state = CURTAIN_DownClosed;
	active_action_complete = 1;
}

static void stepper_completion_callback(void* context) {
	(void)context;

	active_action_complete = 1;
}

CURTAIN_Result CURTAIN_Controller_Init() {
	if (!CurtainActionQueue_init(&action_queue)) return CURTAIN_Result_QueueFull;

	if (STEPPER_Init(&stepper_handle, &stepper_config) != STEPPER_Result_Ok)
		return CURTAIN_Result_InvalidStepperConfig;

	STEPPER_CompletionCallback_Register(&stepper_handle, stepper_completion_callback, &controller);

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

	STEPPER_Enable(&stepper_handle);

	controller.state = CURTAIN_Unknown;
	controller.busy = 0;
	active_action.type = CURTAIN_Action_None;
	active_action_complete = 0;

	return CURTAIN_Result_Ok;
}

CURTAIN_Result CURTAIN_Controller_Update() {
	if (active_action_complete) {
		active_action_complete = 0;
		CURTAIN_Controller_CompleteActiveAction();
	}

	if (!controller.busy && CurtainActionQueue_is_empty(&action_queue)) return CURTAIN_Result_Ok;
	if (controller.busy) return CURTAIN_Result_Ok;

	if (!CurtainActionQueue_dequeue(&action_queue, &active_action)) return CURTAIN_Result_Ok;
	return CURTAIN_Controller_StartAction(active_action);
}

static size_t CURTAIN_Controller_QueuedActionSlotsAvailable(void) {
	return action_queue.capacity - action_queue.size;
}

static CURTAIN_Result CURTAIN_Controller_StartAction(CURTAIN_Action action) {
	STEPPER_Result stepper_result = STEPPER_Result_Ok;

	if (action.type == CURTAIN_Action_MoveUntilLimit) {
		STEPPER_Direction_Set(controller.stepper, action.direction);
		stepper_result = STEPPER_Start(controller.stepper, STEPPER_DEFAULT_FREQUENCY_HZ);
	}
	else if (action.type == CURTAIN_Action_MoveSteps) {
		stepper_result = STEPPER_Step(controller.stepper, action.direction, action.steps, STEPPER_DEFAULT_FREQUENCY_HZ);
	}
	else {
		active_action = (CURTAIN_Action){0};
		return CURTAIN_Result_InvalidState;
	}

	if (stepper_result != STEPPER_Result_Ok) {
		active_action = (CURTAIN_Action){0};
		return CURTAIN_Result_InvalidStepperConfig;
	}

	controller.state = CURTAIN_Unknown;
	controller.busy = 1;
	return CURTAIN_Result_Ok;
}

static void CURTAIN_Controller_CompleteActiveAction(void) {
	controller.busy = 0;
	controller.state = active_action.final_state;
	active_action = (CURTAIN_Action){0};
}

STEPPER_Handle* CURTAIN_Controller_Stepper_Get(void) {
	return &stepper_handle;
}

CURTAIN_State CURTAIN_Controller_State_Get(void) {
	return controller.state;
}

uint8_t CURTAIN_Controller_IsBusy(void) {
	return controller.busy || STEPPER_IsBusy(&stepper_handle) || !CurtainActionQueue_is_empty(&action_queue);
}

static void CURTAIN_Controller_Up(CURTAIN_State new_state) {
	
	CURTAIN_Action action;
	action.final_state = new_state;
	action.steps = 0;

	action.type = CURTAIN_Action_MoveUntilLimit;
	action.direction = STEPPER_Direction_Reverse;
	CurtainActionQueue_enqueue(&action_queue, action);
}

static void CURTAIN_Controller_DownOpen(CURTAIN_State new_state) {

	CURTAIN_Action action1 = {
		.final_state = new_state,
		.steps = 0,
		.type = CURTAIN_Action_MoveSteps,
	};

	if (controller.state == CURTAIN_Up) {
		action1.direction = STEPPER_Direction_Forward;
		action1.steps = STEPPER_UP_TO_DOWNOPEN_STEPS;
		CurtainActionQueue_enqueue(&action_queue, action1);
	}
	else if (controller.state == CURTAIN_DownClosed) {
		action1.direction = STEPPER_Direction_Reverse;
		action1.steps = STEPPER_DOWNCLOSED_TO_DOWNOPEN_STEPS;
		CurtainActionQueue_enqueue(&action_queue, action1);
	}
	else {
		action1.type = CURTAIN_Action_MoveUntilLimit;
		action1.direction = STEPPER_Direction_Forward;
		action1.final_state = CURTAIN_DownClosed;

		CURTAIN_Action action2 = {
			.type = CURTAIN_Action_MoveSteps,
			.direction = STEPPER_Direction_Reverse,
			.steps = STEPPER_DOWNCLOSED_TO_DOWNOPEN_STEPS,
			.final_state = new_state
		};
		
		CurtainActionQueue_enqueue(&action_queue, action1);
		CurtainActionQueue_enqueue(&action_queue, action2);
	}
}

static void CURTAIN_Controller_DownClosed(CURTAIN_State new_state) {

	CURTAIN_Action action = {
		.final_state = new_state,
		.steps = 0,
		.type = CURTAIN_Action_MoveUntilLimit,
		.direction = STEPPER_Direction_Forward,
	};

	CurtainActionQueue_enqueue(&action_queue, action);
}

CURTAIN_Result CURTAIN_Controller_ChangeState(CURTAIN_State new_state) {
	if (controller.state == new_state) return CURTAIN_Result_InvalidState;

	size_t required_actions = 1;
	if (new_state == CURTAIN_DownOpen && controller.state != CURTAIN_Up && controller.state != CURTAIN_DownClosed) {
		required_actions = 2;
	}

	if (CURTAIN_Controller_QueuedActionSlotsAvailable() < required_actions) return CURTAIN_Result_QueueFull;

	switch (new_state) {
	case CURTAIN_Unknown: return CURTAIN_Result_InvalidState;
	case CURTAIN_Up: CURTAIN_Controller_Up(new_state); break;
	case CURTAIN_DownOpen: CURTAIN_Controller_DownOpen(new_state); break;
	case CURTAIN_DownClosed: CURTAIN_Controller_DownClosed(new_state); break;
	}

	return CURTAIN_Result_Ok;
}


CURTAIN_Result CURTAIN_Controller_Move(STEPPER_Direction direction, uint32_t steps) {
	if (steps == 0) return CURTAIN_Result_InvalidState;
	if (CurtainActionQueue_is_full(&action_queue)) return CURTAIN_Result_QueueFull;

	CURTAIN_Action action = {
		.type = CURTAIN_Action_MoveSteps,
		.direction = direction,
		.steps = steps,
		.final_state = CURTAIN_Unknown,
	};
	
	if (!CurtainActionQueue_enqueue(&action_queue, action)) return CURTAIN_Result_QueueFull;

	return CURTAIN_Result_Ok;
}

void CURTAIN_Controller_Stop(CURTAIN_State state) {
	STEPPER_Stop(&stepper_handle);
	CurtainActionQueue_clear(&action_queue);
	active_action = (CURTAIN_Action){0};
	active_action_complete = 0;
	controller.busy = 0;
	controller.state = state;
}
