#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "pid.h"
#include "pololu.h"

#define constP 120
#define constI 0
#define constD 0.0004

#define TEST_CPU 32000000

// Configuration Values
static TC0_t *pid_tick_timer = &TCC0;
#define PID_TICK_OVF TCC0_OVF_vect

// PORTs to be used with wheel encoders
//	Once port can handle interrupts for 2 encoders
static PORT_t *wheelPort1 = &PORTE;
static PORT_t *wheelPort2 = &PORTB;

static float radPerTick = (2*M_PI)/1856.0;
static const float sampleTime = 10e-3;

static pololu_t pololu_1 = {
	.PORT = &PORTD;
	.TC2 = &TCD2;
	.motor2 = false;
};
static pololu_t pololu_2 = {
	.PORT = &PORTD;
	.TC2 = &TCD2;
	.motor2 = true;
};
static pololu_t pololu_3 = {
	.PORT = &PORTF;
	.TC2 = &TCF2;
	.motor2 = false;
};
static pololu_t pololu_4 = {
	.PORT = &PORTF;
	.TC2 = &TCF2;
	.motor2 = true;
};

static Encoder_History leftfront_history; 
static Encoder_History leftrear_history;
static Encoder_History rightfront_history;
static Encoder_History rightrear_history;

// File-Scope Variables and Structures
//	AVG_speed: Average speed of given wheel in rads/S
//	ticks: Measurement of current ticks for given sample
//	output: Current output of PID controller per wheel
//	setpoint: The desired speed for the PID controller
//	errSum: running sum of speed error per wheel
//	pid_last_ticks: Measurement of ticks from previous sample

typedef struct{
	/*	Values for measuring the speed of the wheel
	 *	AVG_speed:		Current running speed measured in rads/s
	 *	ticks:			Current number of ticks per given sample
	 *	output:			Current output of PID controller
	 *	setpoint:		Desired speed (in rads/s) for given wheel
	 *	errSum:			Running total of error in PID controller
	 *	lastErr:		Error from previous sample
	 *	kp:				PID proportional gain
	 *	ki:				PID integral gain
	 *	kd:				PID derivative gain
	 *	pid_last_ticks:	Number of ticks from the previous sample
	 *  odometry_last_ticks: No idea
	 */
	
	volatile float AVG_speed;
	volatile uint16_t ticks;
	
	// Values for the actual pid controller
	float input, output, setpoint, errSum, lastErr, kp, ki, kd; // all 0
	uint16_t pid_last_ticks; // 0
	uint16_t odometry_last_ticks; // 0
} pid_wheel_data_t;

static const pid_wheel_data_t DEFAULT = {
	.AVG_speed = 0,
	.ticks = 0,
	.input = 0,
	.output = 0,
	.setpoint = 0,
	.errSum = 0,
	.lastErr = 0,
	.kp = 0,
	.ki = 0,
	.kd = 0,
	.pid_last_ticks =0,
	.odometry_last_ticks = 0
};

// Array of wheel data (per wheel?)
static pid_wheel_data_t wheelData[4];

void pid_init() {
  //init all of the history queue
  Encoder_History leftfront_history = {
    .data = uint16_t[64],
    .start = ENCODER_QUEUE_SIZE-1,
  }; 
  Encoder_History leftrear_history = {
    .data = uint16_t[64],
    .start = ENCODER_QUEUE_SIZE-1,
  }; 
  Encoder_History rightfront_history = {
    .data = uint16_t[64],
    .start = ENCODER_QUEUE_SIZE-1,
  }; 
  Encoder_History rightrear_history = {
    .data = uint16_t[64],
    .start = ENCODER_QUEUE_SIZE-1,
  }; 
  
	//wheelPort1 = PORTA;
	//wheelPort2 = PORTB;

	//Initialize Pololus
	pololuInit(&pololu_1);
	pololuInit(&pololu_2);
	pololuInit(&pololu_3);
	pololuInit(&pololu_4);

	//pololu_set_velocity(&pololu_4, 256);

	//Initialize wheelData to default values
	for(int8_t x = 0; x < 4; x++)
		wheelData[x] = DEFAULT;

	// Initialize motor frequency measurement timers.
	wheelPort1->DIRCLR = 0x0F;
	wheelPort1->PIN0CTRL = PORT_ISC_BOTHEDGES_gc;
	wheelPort1->INT0MASK = 0x05;
	wheelPort1->INTCTRL = PORT_INT0LVL_LO_gc | PORT_INT1LVL_LO_gc;
	wheelPort1->PIN1CTRL = PORT_ISC_BOTHEDGES_gc;
	wheelPort1->INT1MASK = 0x0A;

	wheelPort2->DIRCLR = 0x0F;
	wheelPort2->PIN0CTRL = PORT_ISC_BOTHEDGES_gc;
	wheelPort2->INT0MASK = 0x05;
	wheelPort2->INTCTRL = PORT_INT0LVL_LO_gc | PORT_INT1LVL_LO_gc;
	wheelPort2->PIN1CTRL = PORT_ISC_BOTHEDGES_gc;
	wheelPort2->INT1MASK = 0x0A;

	// Initialize the PID tick timer
	// Uses timer on portC
	pid_tick_timer->CTRLA = TC_CLKSEL_DIV64_gc;
	pid_tick_timer->CTRLB = 0x00;
	pid_tick_timer->CTRLC = 0x00;
	pid_tick_timer->CTRLD = 0x00;
	pid_tick_timer->CTRLE = 0x00;

	//pid_tick_timer->PER = round(F_CPU * sampleTime / 64.);
	pid_tick_timer->PER = round(TEST_CPU * sampleTime / 64.);
	pid_tick_timer->INTCTRLA = TC_OVFINTLVL_LO_gc;

	// Set PID gain defaults per wheel
	pid_setTunings(constP, constI, constD, WHEEL1);
	pid_setTunings(constP, constI, constD, WHEEL2);
	pid_setTunings(constP, constI, constD, WHEEL3);
	pid_setTunings(constP, constI, constD, WHEEL4);

	sei();
}

static void pid_compute(wheelNum num) {
	// Compute all working error variables
	float error = wheelData[num].setpoint - wheelData[num].AVG_speed;
	wheelData[num].errSum += error;
	float dErr = (error - wheelData[num].lastErr);
	
	if(wheelData[num].ki * wheelData[num].errSum >= 1024) {
		wheelData[num].errSum = 1024/wheelData[num].ki;
	} 
	else if(wheelData[num].ki * wheelData[num].errSum <= -1024) {
		wheelData[num].errSum = -1024/wheelData[num].ki;
	}
	
	// Compute the output
	wheelData[num].output = (wheelData[num].kp * error) + (wheelData[num].ki * wheelData[num].errSum) + (wheelData[num].kd * dErr);
	
	// Remember some things for later
	wheelData[num].lastErr = error;
}

// Set the default PID gain values for a given wheel
void pid_setTunings(float Kp, float Ki, float Kd, wheelNum num) {
	wheelData[num].kp = Kp;
	wheelData[num].ki = Ki * sampleTime;
	wheelData[num].kd = Kd / sampleTime;
}

static void pid_measureSpeed(wheelNum num) {
	// Pull current number of ticks for given motor 'num'
	int16_t ticks = wheelData[num].ticks;
	// Average Speed would be the change in ticks converted to rads/S where S = 1ms
	wheelData[num].AVG_speed = (ticks - wheelData[num].pid_last_ticks)*radPerTick/sampleTime;
	// Replace the current number ticks as the previous number of ticks
	wheelData[num].pid_last_ticks = ticks;
}

float pid_getSpeed(wheelNum num) {
	return wheelData.AVG_speed;
}

void pid_setSpeed(float speed, wheelNum num) {
	wheelData[num].setpoint = speed;
}

/*******************************************************************
-----> Handler Functions <-----
*******************************************************************/
// Handler that sets wheel speeds based on message.
// Arguments:
// message := [Wheel1B0, Wheel1B1, Wheel1B2, Wheel1B3, ... , Wheel4B0, Wheel4B1, Wheel4B2, Wheel4B3]
// 16 bytes, little endian 32bit numbers that represent the desired wheel speed
// multiplied by 1000. Pass a pointer to the low byte of
// len := the length of the message. E.g. the number of bytes in the array
void pid_speed_msg(float speed) {
	//for(int i = 0; i < 4; i++) pid_setSpeed(speed, (wheelNum)i);
	/*
		Will be used as callback to set current desired wheel speeds
	*/
}

static void pid_get_odometry(float* returnData) {
	for(int i = 0; i < 4; i++) {
		int16_t ticks = wheelData[i].ticks;
		volatile int16_t tmp = ticks - wheelData[i].odometry_last_ticks;
		volatile float tmp2 = tmp * radPerTick;
		returnData[i] = tmp2;
		wheelData[i].odometry_last_ticks = ticks;
	}
}

static inline void pid_set_speed_multiplier(float val) {
	radPerTick = val;
}

static inline float pid_get_speed_multiplier(void) {
	return radPerTick;
}

// Because the multiplier is a floating point value, we'll multiply it by 1000 first, and then send it.
// Or, if we're recieving it, we'll divide it by 1000.
/*
void pid_get_speed_multiplier_handler(char* message, uint8_t len) {
	char multiplier[2];
	uart_float_to_char16(multiplier, pid_get_speed_multiplier());
	uart_send_msg_block(PIDgetMultiplier, multiplier, 3);
}
*/

/*
void pid_set_speed_multiplier_handler(char* message, uint8_t len) {
	pid_set_speed_multiplier(uart_int16_to_float(message));
}
*/

ISR(PID_TICK_OVF) {
	pid_measureSpeed(WHEEL1);
	pid_measureSpeed(WHEEL2);
	pid_measureSpeed(WHEEL3);
	pid_measureSpeed(WHEEL4);
	pid_compute(WHEEL1);
	pid_compute(WHEEL2);
	pid_compute(WHEEL3);
	pid_compute(WHEEL4);
	//Divide by 100 to smooth output during testing. 
	pololu_set_velocity(&pololu_1, wheelData[WHEEL1].output);
	pololu_set_velocity(&pololu_2, wheelData[WHEEL2].output);
	pololu_set_velocity(&pololu_3, wheelData[WHEEL3].output);
	pololu_set_velocity(&pololu_4, wheelData[WHEEL4].output);
}

unsigned int grayToBinary(unsigned int num)
{
	unsigned int mask;
	for (mask = num >> 1; mask != 0; mask = mask >> 1)
	{
		num = num ^ mask;
	}
	return num;
}

//push an encoder sample to specified queue
void encoder_history_push(uint16_t data, uint8_t motor){ 
  Encoder_History* encoder;
  switch (motor){
    case LEFT_FRONT_MOTOR:  encoder = &leftfront_encoder;
    case LEFT_REAR_MOTOR:   encoder = &leftrear_encoder;
    case RIGHT_FRONT_MOTOR: encoder = &rightfront_encoder;
    case RIGHT_REAR_MOTOR:  encoder = &rightrear_encoder;
  }
  encoder->data[encoder->start] = data;
  encoder->start = (encoder->start + 1) % ENCODER_QUEUE_SIZE;
}

//get a single history entry
uint16_t encoder_history_at(uint8_t index, uint8_t motor){ 
  Encoder_History* encoder;
  switch (motor){
    case LEFT_FRONT_MOTOR:  encoder = &leftfront_encoder;
    case LEFT_REAR_MOTOR:   encoder = &leftrear_encoder;
    case RIGHT_FRONT_MOTOR: encoder = &rightfront_encoder;
    case RIGHT_REAR_MOTOR:  encoder = &rightrear_encoder;
  }
  return encoder->data[encoder->start];
}

//return batched history entries
void encoder_history_batch(uint16_t* buffer, uint8_t size){ 
  Encoder_History* encoder;
  switch (motor){
    case LEFT_FRONT_MOTOR:  encoder = &leftfront_encoder;
    case LEFT_REAR_MOTOR:   encoder = &leftrear_encoder;
    case RIGHT_FRONT_MOTOR: encoder = &rightfront_encoder;
    case RIGHT_REAR_MOTOR:  encoder = &rightrear_encoder;
  }
  
  if(encoder->start - size < 0){ //check value range
    int overfolw_addr = 64 + encoder->start - size;
    int overflow_amt = size-encoder->start-1;
    memcpy(buffer, encoder->data+overflow_addr, overflow_amt)
    memcpy(buffer+overflow_amt, encoder->data, encoder->start+1); //copy newer data
  } else { //no overflow
    memcpy(buffer, encoder->data + encoder->start, size)
  }
}

//call a pid update
void update_pid(void){ 
  //placeholder
}

ISR(PORTE_INT0_vect){
	// pins 0 and 2
	static int8_t old_value = 0;
	int8_t new_value = grayToBinary(((wheelPort1->IN & 4) >> 1) | (wheelPort1->IN & 1));
	int8_t difference = new_value - old_value;
	if(difference > 2) difference -= 4;
	if(difference < -2) difference += 4;
	wheelData[WHEEL1].ticks += difference;
	old_value = new_value;
}

ISR(PORTE_INT1_vect){
	// pins 1 and 3
	static int8_t old_value = 0;
	int8_t new_value = grayToBinary(((wheelPort1->IN & 8) >> 2) | ((wheelPort1->IN & 2) >> 1));
	int8_t difference = new_value - old_value;
	if(difference > 2) difference -= 4;
	if(difference < -2) difference += 4;
	wheelData[WHEEL2].ticks += difference;
	old_value = new_value;
}

ISR(PORTB_INT0_vect){
	// pins 0 and 2
	static int8_t old_value = 0;
	int8_t new_value = grayToBinary(((wheelPort2->IN & 4) >> 1) | (wheelPort2->IN & 1));
	int8_t difference = new_value - old_value;
	if(difference > 2) difference -= 4;
	if(difference < -2) difference += 4;
	wheelData[WHEEL3].ticks += difference;
	old_value = new_value;
}

ISR(PORTB_INT1_vect){
	// pins 1 and 3
	static int8_t old_value = 0;
	int8_t new_value = grayToBinary(((wheelPort2->IN & 8) >> 2) | ((wheelPort2->IN & 2) >> 1));
	int8_t difference = new_value - old_value;
	if(difference > 2) difference -= 4;
	if(difference < -2) difference += 4;
	wheelData[WHEEL4].ticks += difference;
	old_value = new_value;
}