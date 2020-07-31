#include "driver_init.h"
/*
 * Sketch2.ino
 *
 * Created: 7/21/2020 8:58:43 AM
 * Author: hp
 */ 
#include "sam.h"
#include "core_cm0plus.h"
#include "samd21/include/samd21g18a.h"

#include <math.h>
#include <stdio.h>
#include <stdbool.h>
#include <time.h>

//USED FOR TESTING
#define  REDLED A1
#define  BLUELED A2
#define  YELLOWLED A5
#define  ANALOG_PIN A6

volatile float measured_voltage=0;

/************************************************************************/
/* PINS DESCRIPTIONS                                                    */
/************************************************************************/


#define CTRL_VSTR A0 //A0 the control signal for the voltage stress circuit, labeled CTRL_VSTR in figure 1
#define HEATER_PWM A3 //A3 This is the control signal for the Helmholtz circuit
#define HELMHOLTZ_PWM A4 //A4 This is the control signal for the Heater board
#define _RESET_ADC 0          //D0 Should be configured as a digital output. Connected to *RESET on the ADC
#define _PWDN_ADC 1           //D1 Should be configured as a digital output. Connected to *PWDN on the ADC
#define START_ADC 2           //D2 Should be configured as a digital output. Connected to START on the ADC.
#define _DRDY_ADC 3           //D3 Should be configured as a digital input. Connected to *DRDY on the ADC.
#define RED_LED 4		      //D4 Should be configured as a digital output. Control signal for the red LED on the heater board.
#define BLUE_LED 5            //D5 Should be configured as a digital output. Control signal for the Blue LED on the heater board.
#define CLR_FREQ_DIVIDER 6    //D6 Should be configured as a digital output. The clear command for the frequency divider. labeled CLR in figure 1.
#define _CS_ADC 7             //D7 Should be configured as a digital output. This is the *CS line for SPI communication with the ADC
#define MOSI_ADC 8            //D8 Should be configured as a digital output. This is the MOSI line on the SPI bus. labeled DIN on the ADC.
#define SCK_ADC 9             //D9 Should be configured as a digital output. This is the SCLK line on the SPI bus.
#define MISO_ADC 10           //D10 Should be configured as a digital input. This is the MISO line on the SPI bus. labeled DOUT on the ADC.
#define Q11_FREQ_COUNTER 11   //D11 Should be configured as a digital input. Connected to Q11 on the frequency divider
#define Q12_FREQ_COUNTER 12   //D12 Should be configured as a digital input. Connected to Q12 on the frequency divider
#define Q13_FREQ_COUNTER 13   //D13 Should be configured as a digital input. Connected to Q13 on the frequency divider
#define Q14_FREQ_COUNTER 14   //D14 Should be configured as a digital input. Connected to Q14 on the frequency divider

//A1, A2, A5, A6   4 MCU pins that are not used in the design and left disconnected. They will be accessible through the male/female headers mounted on the MCU board.

//variables
//volatile is for variables that can always change in the code
volatile int TCC1_compare_value = 3905;  //compare value for 1sec default rate

volatile int raw_ADC_data [29];       //stores raw ADC values
volatile int converted_ADC_data [29]; //stores converted ADC values
volatile int ADC_GAIN = 1;

volatile int test_time_count = 0;     //seconds of test time elapsed
volatile int System_fsm_state = 0;

//Input data from the MCU
volatile bool TEST_START = false;
volatile bool TEST_STOP = false;
volatile float desired_temperature = 43;     //in C
volatile float desired_magnetic_field = 0;  //in mT
volatile int desired_time_for_test = 2;	 //time in minutes
volatile float desired_FPGA_voltage =0;     //in mV
volatile int serial_output_rate=1500;                 //rate to read data in ms

//Variables for Magnetic field FSM
volatile int measured_magnetic_field = 0; // in mT
volatile int magnetic_PWM_duty = 0; //duty cycle value 0-255
volatile int magnetic_fsm_state = 0; //idle state

//Variables for heater FSM
volatile float measured_temperature = 0; // in C
volatile float heater_PWM_duty = 0; //duty cycle value 0-255
volatile int heater_fsm_state = 0; //idle state
volatile bool HEATER_START = false;

void setup() {
	//setting up clocks
	clock_setup(); //set up 1MHz clock for the timers
	init_TC3();	
	init_TC4();    //initialize TC4 timer  TCCO controls the PWM pins so we do not use this timer, TC4 is a 1 second counter, TC5 generates the ADC read interrupt, TC6 serial read interrupt
	init_TC5();
	pin_setup();
	__enable_irq();
	
	//testing
	Serial.begin(115200);
	TEST_START = true;
	
	
}

// the loop function runs over and over again forever
void loop() {
		measured_voltage = (analogRead(ANALOG_PIN))*(3.3/1024);
		measured_temperature = volt_to_temperature(1000*measured_voltage);
		//measured_magnetic_field = magnetic_field_mT(1000*measured_voltage);
		System_fsm_Run();
		System_fsm_Transition();


}

/**
\brief Sets up 1MHz clock used by TC3,TC4,TCC2,TC5
\param[in] N/A
\param[out] N/A
*/
void clock_setup(){ //setting up the clock speeds
	
	// Set up the generic clock (GCLK4) used to clock timers
	REG_GCLK_GENDIV = GCLK_GENDIV_DIV(48) |          // Divide the 48MHz clock source by divisor 8: 8MHz/8=1MHz
	GCLK_GENDIV_ID(4);            // Select Generic Clock (GCLK) 4
	while (GCLK->STATUS.bit.SYNCBUSY);              // Wait for synchronization

	REG_GCLK_GENCTRL = GCLK_GENCTRL_IDC |           // Set the duty cycle to 50/50 HIGH/LOW
	GCLK_GENCTRL_GENEN |         // Enable GCLK4
	GCLK_GENCTRL_SRC_DFLL48M |   // Set the 48MHz clock source
	GCLK_GENCTRL_ID(4);          // Select GCLK4
	while (GCLK->STATUS.bit.SYNCBUSY);              // Wait for synchronization
	
	REG_GCLK_CLKCTRL = GCLK_CLKCTRL_CLKEN |         // Enable GCLK4 to TC4 and TC5
	GCLK_CLKCTRL_GEN_GCLK4 |     // Select GCLK4
	GCLK_CLKCTRL_ID_TC4_TC5; // Feed the GCLK4 to TC4 and TC5  
	while (GCLK->STATUS.bit.SYNCBUSY);	
	
	REG_GCLK_CLKCTRL = GCLK_CLKCTRL_CLKEN |         // Enable GCLK4 to TC4 and TC5
	GCLK_CLKCTRL_GEN_GCLK4 |     // Select GCLK4
	GCLK_CLKCTRL_ID_TCC2_TC3; // Feed the GCLK4 to TC4 and TC5
	while (GCLK->STATUS.bit.SYNCBUSY);
	
	return;
}

/**
\brief Sets up TC4
\param[in] N/A
\param[out] N/A
*/
void init_TC4(){ //initialize TC4 timer
	// Configure TC4 (16 bit counter by default)
	REG_TC4_COUNT16_CC0 = countervalue(1,256,200);  //set period for timer
	while (TC4->COUNT16.STATUS.bit.SYNCBUSY);        // Wait for synchronization
	
	//Enabling interrupts
	NVIC_EnableIRQ(TC4_IRQn);         // Connect TC4 to Nested Vector Interrupt Controller (NVIC)
	
	REG_TC4_INTFLAG |= TC_INTFLAG_OVF; //TC_INTFLAG_MC1 | TC_INTFLAG_MC0 ;        // Clear the interrupt flags (overflow flag)
	REG_TC4_INTENSET |= TC_INTENSET_OVF; //TC_INTENSET_MC1 | TC_INTENSET_MC0 ;     // Enable TC4 counter interrupts
	// REG_TC4_INTENCLR = TC_INTENCLR_MC1 | TC_INTENCLR_MC0 ;     // Disable TC4 interrupts

	REG_TC4_CTRLA = TC_CTRLA_WAVEGEN_MFRQ | //this makes CC0 register have the top  value before the overflow interrupt
	TC_CTRLA_MODE_COUNT16 |         //set as a 16 bit counter
	TC_CTRLA_PRESCALER_DIV256 |     // Set prescaler to 256,
	TC_CTRLA_ENABLE;               // Enable TC4
	while (TC4->COUNT16.STATUS.bit.SYNCBUSY);        // Wait for synchronization
	
	return;
}

/**
\brief Sets up TC5
\param[in] N/A
\param[out] N/A
*/
void init_TC5(){ //initialize T5 timer

	REG_TC5_COUNT16_CC0 =  countervalue(1,64,1000);                     // Set the TC4 CC1 register to  decimal 3905, 1 sec
	while (TC5->COUNT16.STATUS.bit.SYNCBUSY);        // Wait for synchronization
	
	//Enabling interrupts
	NVIC_EnableIRQ(TC5_IRQn);         // Connect TC4 to Nested Vector Interrupt Controller (NVIC)
	NVIC_SetPriority(TC5_IRQn,0);

	REG_TC5_INTFLAG |= TC_INTFLAG_OVF;        // Clear the interrupt flags
	REG_TC5_INTENSET |= TC_INTENSET_OVF;     // Enable TC5 Match CC0 and CC1 interrupts
	// REG_TC5_INTENCLR = TC_INTENCLR_MC1 | TC_INTENCLR_MC0 ;     // Disable TC5 interrupts

	REG_TC5_CTRLA = TC_CTRLA_WAVEGEN_MFRQ | //this makes CC0 register have the top  value before the overflow interrupt
	TC_CTRLA_MODE_COUNT16 |        //set as a 16 bit counter
	TC_CTRLA_PRESCALER_DIV64 |     // Set prescaler to 256,
	TC_CTRLA_ENABLE;               // Enable TC5
	while (TC5->COUNT16.STATUS.bit.SYNCBUSY);        // Wait for synchronization
	
	return;
}
/**
\brief Sets up TC3
\param[in] N/A
\param[out] N/A
*/
void init_TC3(){ //initialize TC3 timer, this timer controls the rate at which serial data is sent
	
	REG_TC3_COUNT16_CC0 =  countervalue(1,1024,1000);                     // Set the TC3 CC0 register 
	while (TC3->COUNT16.STATUS.bit.SYNCBUSY);        // Wait for synchronization
	
	//Enabling interrupts
	NVIC_EnableIRQ(TC3_IRQn);         // Connect TC3 to Nested Vector Interrupt Controller (NVIC)

	REG_TC3_INTFLAG |= TC_INTFLAG_OVF;        // Clear the interrupt flags
	REG_TC3_INTENSET |= TC_INTENSET_OVF;     // Enable TC3 Match CC0 and CC1 interrupts
	// REG_TC3_INTENCLR = TC_INTENCLR_MC1 | TC_INTENCLR_MC0 ;     // Disable TC3 interrupts

	REG_TC3_CTRLA = TC_CTRLA_WAVEGEN_MFRQ | //this makes CC0 register have the top  value before the overflow interrupt
	TC_CTRLA_MODE_COUNT16 |        //set as a 16 bit counter
	TC_CTRLA_PRESCALER_DIV1024 |     // Set prescaler to 1024, with a 1Mhz clock 67 seconds is the slowest we can go
	TC_CTRLA_ENABLE;               // Enable TC3
	while (TC3->COUNT16.STATUS.bit.SYNCBUSY);        // Wait for synchronization
	
	return;
}


void TC4_Handler()                              // Interrupt Service Routine (ISR) for timer TC4
{
	// Check for OVF interrupt
	if (TC4->COUNT16.INTFLAG.bit.OVF && TC4->COUNT16.INTENSET.bit.OVF)
	{
		//Overflow interrupt code here:
		
		if (digitalRead(REDLED)==LOW)
		{
			digitalWrite(REDLED,HIGH);
		} 
		else
		{
			digitalWrite(REDLED,LOW);
		}	
		// Clear the interrupt flag
		REG_TC4_INTFLAG = TC_INTFLAG_OVF;         
	}
	return;
}

void TC5_Handler(){
	// Check for OVF interrupt
	if (TC5->COUNT16.INTFLAG.bit.OVF && TC5->COUNT16.INTENSET.bit.OVF)
	{
		//Overflow interrupt code here:
		
		test_time_count++; //increment +1 every second
		if (digitalRead(BLUELED)==HIGH)
		{
			digitalWrite(BLUELED,LOW);
		}
		else
		{
			digitalWrite(BLUELED,HIGH);
		}
		REG_TC5_INTFLAG = TC_INTFLAG_OVF;        // Clear the MC1 interrupt flag
	}
	return;
}

void TC3_Handler(){
	//If test is completed return test complete
	if (TEST_STOP){
		Serial.println("Test Completed");
		return;
	}
	
	// Check for match counter 1 (MC1) interrupt
	if (TC3->COUNT16.INTFLAG.bit.OVF && TC3->COUNT16.INTENSET.bit.OVF)
	{
		//Overflow interrupt code here:
		Serial.print(measured_voltage); //print voltage
		Serial.println(" V");
		
		Serial.print(measured_temperature); //print the temperature
		Serial.println(" C");
		
		Serial.print("Current test time in seconds ");
		Serial.println(test_time_count);
		
		Serial.print("Current PWM duty % ");
		Serial.println((heater_PWM_duty/255)*100);
		
		Serial.print("Current system FSM state");
		Serial.println(System_fsm_state);
		
// 		Serial.print("Current Magnetic field in mT ");
// 		Serial.println(measured_magnetic_field);
// 		
// 		Serial.print("Current magnetic PWM duty%");
// 		Serial.println((magnetic_PWM_duty/255)*100);
		
		if (digitalRead(YELLOWLED)==HIGH)
		{
			digitalWrite(YELLOWLED,LOW);
		}
		else
		{
			digitalWrite(YELLOWLED,HIGH);
		}
		REG_TC3_INTFLAG = TC_INTFLAG_OVF;        // Clear the OVF interrupt flag
	}
	return;
}

/**
\brief Returns value for the overflow counter
\param[in] Clock Frequency in MHz, prescaler value, period of timer in milliseconds
\param[out] integer count value
*/
int countervalue (float Clock_FrequencyMHz,float prescaler, float period_ms){
	return (int) (( (Clock_FrequencyMHz*1000*period_ms) / (prescaler) )-1) ;
	
}

/**
\Name : Pin setup
\brief Sets us the pins
\param[in] N/A
\param[out] N/A
*/
void pin_setup(void){
	// initialize digital pin LED_BUILTIN as an output.	
	pinMode(REDLED,OUTPUT);
	pinMode(BLUELED, OUTPUT);
	pinMode(YELLOWLED,OUTPUT);
	pinMode (ANALOG_PIN, INPUT);
	
	pinMode(CTRL_VSTR,OUTPUT);
	pinMode(HEATER_PWM,OUTPUT);
	pinMode(HELMHOLTZ_PWM,OUTPUT);
	pinMode(_RESET_ADC,OUTPUT);
	pinMode(_PWDN_ADC,OUTPUT);
	pinMode(START_ADC,OUTPUT);
	pinMode(_DRDY_ADC,INPUT);
	pinMode(RED_LED,OUTPUT);
	pinMode(BLUE_LED,OUTPUT);
	pinMode(CLR_FREQ_DIVIDER,OUTPUT);
	pinMode(MOSI_ADC,OUTPUT);
	pinMode(SCK_ADC,OUTPUT);
	pinMode(MISO_ADC,INPUT);
	pinMode(Q11_FREQ_COUNTER,INPUT);
	pinMode(Q12_FREQ_COUNTER,INPUT);
	pinMode(Q13_FREQ_COUNTER,INPUT);
	pinMode(Q14_FREQ_COUNTER,INPUT);
	
	return;
}


/*
Function :volt_to_temperature
Input: Digital Voltage from ADC in millivolts
Output: Temperature conversion in C from TMP235
Formula : T = (V_out - V_offset)/Tc) + T_INFL
Data sheet: https://www.ti.com/lit/ds/symlink/tmp235.pdf?ts=1594142817615&ref_url=https%253A%252F%252Fwww.ti.com%252Fproduct%252FTMP235
*/
float volt_to_temperature(float milli_volts)
{
	#define V_OFFSET_0 500
	#define V_OFFSET_100 1500
	#define V_OFFSET_125 1752.5

	#define T_INFL_0 0
	#define T_INFL_100 100
	#define T_INFL_125 125

	#define T_COEFF_0 10
	#define T_COEFF_100 10.1
	#define T_COEFF_125 10.6

	if (milli_volts < V_OFFSET_100) return (((milli_volts - V_OFFSET_0)/T_COEFF_0) + T_INFL_0); // -40 to 100C
	
	else if (milli_volts >= V_OFFSET_100 && milli_volts <= V_OFFSET_125) return (((milli_volts - V_OFFSET_100)/T_COEFF_100) + T_INFL_100); // 100 to 125C
	
	else return (((milli_volts - V_OFFSET_125)/T_COEFF_125) + T_INFL_125); // 125 to 150C
}

/*
Function :twos_complement_to_int
Purpose:Returns an integer value of a twos complement number
Input: value and number of bits
Output: integer
*/

int twos_complement_to_int (int value, int num_bits)
{
	// msb =  value >> (num_bits-1); //collecting MSB
	if (num_bits <= 2 || num_bits == '\0' || value > (pow(2,num_bits)-1)) return '\0'; //error checking

	else if (value >> (num_bits-1)){	//if msb ==1, negative values
		return (( value & (int)(pow(2,num_bits-1)-1)) - (pow(2,num_bits-1)));
	}

	else {	//positive values
		return value;
	}
}

/*
Function: ADC_mV
Purpose: converts ADV value into millivolts
Input: int reading, int gain
Return: int millivolts
mV = ADC_data * range
	 ----------------
	 Gain * ADC_Resolution
Source: http://scientific-solutions.com/products/faq/ssi_faq_adc_equations%20VER2.shtml#:~:text=2%27s%20Complement%20ADC%20Data%20with%208%2Dbit%20resolution
*/

int ADC_mv(int ADC_reading, int adc_gain){
	#define MAX_POSITIVE_VOLTAGE 0x7FFF
	#define MAX_NEGATIVE_VOLTAGE 0x8000
	#define REFERENCE_VOLTAGE_ADC_mV 65536
	#define ADC_RESOLUTION 32768 //2^15 bits as 16th bit is used for 2's complement
	#define ADC_RANGE 4900 //4900mV (-1.6V to 3.3V)
	
	return (ADC_RANGE*ADC_reading)/(adc_gain*ADC_RESOLUTION);
}

/*
Function: magnetic_field_mT
Purpose: Converts voltage to corresponding magnetic field (reference is for the  magnetic sensor)
Input: Voltage in millivolts
Output: Magnetic field density in milli_Tesla (1G = 0.1mT)
Data sheet: https://www.google.com/url?sa=t&rct=j&q=&esrc=s&source=web&cd=&ved=2ahUKEwjxps3qg8PqAhXBo54KHUn5CvwQFjAAegQIBRAB&url=https%3A%2F%2Fwww.allegromicro.com%2F~%2Fmedia%2FFiles%2FDatasheets%2FA1318-A1319-Datasheet.ashx&usg=AOvVaw39zGCju7QuDLgpcH9PKde_
*/

float magnetic_field_mT(float voltage){
	#define QUIESCENT_VOLTAGE 1650              //1.65V Low->1.638, mean-> 1.65, high-> 1.662 measure this
	#define MAGNETIC_SENSITIVITY 13.5           //1.35mv/G --> 13.5mV/mT Low->1.289, mean-> 1.3, high-> 1.411
	#define TEMPERATURE_SENSITIVITY   0.12      //0.12%/C
	#define magnetic_sesnor_ERROR 1.5							//1.5% ERROR
	#define VOLTAGE_CLAMP_HIGH 2970			    //2.97V +40 mT
	#define VOLTAGE_CLAMP_LOW 0.33			    //2.97V -40mT
	#define MAX_MAGNETIC_FIELD 40              //40mT
	#define MIN_MAGNETIC_FIELD -40              //-40mT
	
	if (voltage == '\0') return '\0';
	else if (voltage == VOLTAGE_CLAMP_HIGH ) return MAX_MAGNETIC_FIELD ; //we can find some sort of error to throw here
	else if (voltage == VOLTAGE_CLAMP_LOW) return MIN_MAGNETIC_FIELD ;
	else return (float)((voltage-QUIESCENT_VOLTAGE)/(float)MAGNETIC_SENSITIVITY);   //polarity depends on the direction of the field
	
}

/*
Function: System_FSM
input : Void
Output : Void
Purpose : This state_machine runs the system

*/

//STATES {IDLE, SPI_UPDATE, serial_update, Heater_update, Magnetic_field_update}
	
void System_fsm_Run (void){
	
	#define IDLE 0 //idle state
	#define ADC_UPDATE 1 //reading data from the ADC using SPI at intervals
	#define SERIAL_UPDATE 2//sending data back to the computer through serial at intervals
	#define Heater_Update 3 //running the heater_FSM and get the updates
	#define Magnetic_Field_update 4 //running the magnetic field FSM and get the updates
	
	//variables
	float current_test_time = 0; //test time in minutes
	float starting_test_count = 0;
	
	
	switch (System_fsm_state){
		case IDLE : {
				if (TEST_STOP){
					//clear and turn off all the outputs
					//turn off heater click
					//turn off magnetic field
					
						digitalWrite(CTRL_VSTR,LOW);
						analogWrite(HEATER_PWM,LOW);
						analogWrite(HELMHOLTZ_PWM,LOW);
						digitalWrite(_RESET_ADC,LOW);
						digitalWrite(_PWDN_ADC,LOW);
						digitalWrite(START_ADC,LOW);
						digitalWrite(RED_LED,LOW);
						digitalWrite(BLUE_LED,LOW);
						digitalWrite(CLR_FREQ_DIVIDER,LOW);
						digitalWrite(MOSI_ADC,LOW);
						digitalWrite(SCK_ADC,LOW);
						heater_PWM_duty = 0;
						magnetic_PWM_duty = 0;
				}
				else if (TEST_START){
					//1. Setting voltage for test
					
					//2. Setting total time for test
					starting_test_count = test_time_count; //initializing starting point
					current_test_time = (test_time_count - starting_test_count)/60; //current test time in minutes
					
					//3. Setting time interval for sending data rate
					REG_TC3_COUNT16_CC0 =  countervalue(1,1024,serial_output_rate);                     // Set the TC3 CC0 register
					while (TC3->COUNT16.STATUS.bit.SYNCBUSY);											// Wait for synchronization
					
					//4. Setting desired magnetic field in mT
					//desired_magnetic_field = ;
					
					//5. Setting desired temperature in C
					//desired_temperature = ;
					Serial.println("Finished setting up");
					
				}
				
			}
			break;
		case ADC_UPDATE: {

				//1. Update test timer and if time is hit, stop
				current_test_time = (test_time_count - starting_test_count)/60;
				if (current_test_time > desired_time_for_test) TEST_STOP=true;
	
				//2. Convert Raw ADC data to understandable values
				//ADC_array_convert();
		}
			break;
		case SERIAL_UPDATE:{
			//1. Set Data to be sent to the user from the ADC update, data is sent in intervals in an Interrupt service routine
			
			//out current_test_timer, ADC converted data at intervals 
			
		}
			break;
		case Heater_Update:{
			//update actual and desired temperature, run heater click FSM
			HEATER_START = true;  //enable heater
			heater_fsm_RUN();
			heater_fsm_transition();
			HEATER_START = false; //disable heater
			
		}
			break;
		case Magnetic_Field_update:{
			//update actual and desired magnetic field, fun magnetic field FSM
			//1.state_run
			magnetic_fsm_run();
			//2. state_transtions
			magnetic_fsm_transition();
			
		}
			break;
		default :System_fsm_state = IDLE;
	}
	return;
}

void System_fsm_Transition(void)
{
	#define IDLE 0 //idle state
	#define ADC_UPDATE 1 //reading data from the ADC using SPI at intervals
	#define SERIAL_UPDATE 2 //sending data back to the computer through serial at intervals
	#define Heater_Update 3 //running the heater_FSM and get the updates
	#define Magnetic_Field_update 4 //running the magnetic field FSM and get the updates

	switch (System_fsm_state){
		case IDLE : {
			if (TEST_STOP) System_fsm_state = IDLE;	//else state = idle
			else if (TEST_START) System_fsm_state =ADC_UPDATE;
		}
		break;
		case ADC_UPDATE: {
			if (TEST_STOP) System_fsm_state = IDLE;
			else System_fsm_state = SERIAL_UPDATE;
		}
		break;
		case SERIAL_UPDATE:{
			if (TEST_STOP) System_fsm_state = IDLE;
			else System_fsm_state = Heater_Update;

		}
		break;
		case Heater_Update:{
			if (TEST_STOP) System_fsm_state = IDLE;
			else System_fsm_state = Magnetic_Field_update;
		}
		break;
		case Magnetic_Field_update:{
			if (TEST_STOP) System_fsm_state = IDLE;
			else System_fsm_state = ADC_UPDATE;
		}
		break;
		default :System_fsm_state = IDLE;
	}
	return;
}

/*
Function : ADC_array_convert
Purpose : Converts raw_ADC_Data into converted_ADC_data
Input: void
Output: void

*/
void ADC_array_convert(void){
	//0:23 is all in voltage
	//24 OFFSET, 25-> ADC VCC in mV,26-> ADC TEMP in C, 27-> GAIN V/V, 28-> REF
	#define ADC_temp_sensor_coefficient 563 //563uV if ADS1158 and test PCB temperatures are forced together, 
											//394uV if ADS1158 temperature is forced and test PCB is in free air
	 for (int i =0; i <= 23; i++)
	 {
		 converted_ADC_data[i] = ADC_mv(raw_ADC_data[i], ADC_GAIN); //converting all the voltages to millivolts
	 }
	//for OFFSET conversion
	converted_ADC_data[24] = raw_ADC_data[24];
	//for VCC conversion
	converted_ADC_data[25] = ((float) raw_ADC_data[25]/(float) 3072);
	//for ADC temp conversion
	converted_ADC_data[26] =( ((float) (1000*ADC_mv( raw_ADC_data[26], ADC_GAIN) - 168000)/(float) ADC_temp_sensor_coefficient)+ 25);
	//ADC GAIN conversion
	converted_ADC_data[27] = ((float) raw_ADC_data[27]/(float) 30720);
	//ADC REF conversion
	converted_ADC_data[28] = ((float) raw_ADC_data[28]/(float) 3072);
	
return;
}

/************************************************************************/
/* MAGNETIC FIELD FSM                                                   */
/************************************************************************/
/*	This is a state machine for the Magnetic circuit program--
	An external function is needed to keep updating the actual and desired Magnetic field values
*/
#define magnetic_fsm_idle_state 0
#define magnetic_fsm_increase_state 1
#define magnetic_fsm_decrease_state 2
#define magnetic_error 0.015 //error is 1.5% gotten from the magnetic field data sheet in mT

void magnetic_fsm_transition(void)
{
	float magnetic_field_difference = (float) (measured_magnetic_field - desired_magnetic_field); //getting the error
	
	if (magnetic_fsm_state == magnetic_fsm_idle_state) 
	{
// 		if (measured_magnetic_field < (desired_magnetic_field*(1-magnetic_error)) ) magnetic_fsm_state = magnetic_fsm_increase_state;
// 		else if (measured_magnetic_field > (desired_magnetic_field*(1 + magnetic_error)) ) magnetic_fsm_state = magnetic_fsm_decrease_state; //else if equal, state is unchanged
		if ( magnetic_field_difference > magnetic_error) magnetic_fsm_state = magnetic_fsm_decrease_state; //cool temp is above desired + threshold
		else if ( abs(magnetic_field_difference) <= magnetic_error) magnetic_fsm_state = magnetic_fsm_idle_state; //maintain heat if it between 0.5C threshold
		else magnetic_fsm_state = magnetic_fsm_increase_state; //heat up if is below the desired+threshold
	}
	else if (magnetic_fsm_state == magnetic_fsm_increase_state) 
	{
		/*if (measured_magnetic_field >= desired_magnetic_field) magnetic_fsm_state = magnetic_fsm_idle_state; //else state is unchanged*/
		if ( magnetic_field_difference > magnetic_error) magnetic_fsm_state = magnetic_fsm_decrease_state; //cool temp is above desired + threshold
		else if ( abs(magnetic_field_difference) <= magnetic_error) magnetic_fsm_state = magnetic_fsm_idle_state; //maintain heat if it between 0.5C threshold
		else magnetic_fsm_state = magnetic_fsm_increase_state; //heat up if is below the desired+threshold
	}
	else if (magnetic_fsm_state == magnetic_fsm_decrease_state) 
	{
		/*if (measured_magnetic_field <= desired_magnetic_field) magnetic_fsm_state = magnetic_fsm_idle_state; //else state is unchanged*/
		if ( magnetic_field_difference > magnetic_error) magnetic_fsm_state = magnetic_fsm_decrease_state; //cool temp is above desired + threshold
		else if ( abs(magnetic_field_difference) <= magnetic_error) magnetic_fsm_state = magnetic_fsm_idle_state; //maintain heat if it between 0.5C threshold
		else magnetic_fsm_state = magnetic_fsm_increase_state; //heat up if is below the desired+threshold
	}
	return;
}

/*
float temperature_difference =  (float)(measured_temperature - desired_temperature);

switch(heater_fsm_state){
	case heater_fsm_OFF: {
		heater_fsm_state = heater_fsm_idle;
		break;
	}
	case heater_fsm_idle: {
		if ( temperature_difference > heater_fsm_margin) heater_fsm_state = heater_fsm_cooling; //cool temp is above desired + threshold
		else if ( abs(temperature_difference) <= heater_fsm_margin) heater_fsm_state = heater_fsm_idle; //maintain heat if it between 0.5C threshold
		else heater_fsm_state = heater_fsm_heating; //heat up if is below the desired+threshold
		
		break;
*/

void magnetic_fsm_run(void)
{
	if (magnetic_fsm_state == magnetic_fsm_idle_state){	} //no change
	else if (magnetic_fsm_state == magnetic_fsm_increase_state) 
	{
		//PWM is increased until desired state
		if (magnetic_PWM_duty >= 255) magnetic_PWM_duty=255;
		else magnetic_PWM_duty++;
	}
	else if (magnetic_fsm_state == magnetic_fsm_decrease_state) 
	{
		//PWM is decreased until desired state
		if (magnetic_PWM_duty <= 0) magnetic_PWM_duty=0;
		else magnetic_PWM_duty--;
	}
	analogWrite(HELMHOLTZ_PWM, magnetic_PWM_duty); //write to the pin after changing the duty
}

/************************************************************************/
/* HEATER CLICK FSM                                                     */
/************************************************************************/
/*	This is a state machine for the Magnetic circuit program--
	An external function is needed to keep updating the actual and desired Magnetic field values
*/
	#define heater_fsm_OFF 0
	#define heater_fsm_idle 1 //state where temperature is maintained
	#define heater_fsm_heating 2
	#define heater_fsm_cooling 3
	#define heater_fsm_margin 4 //error is +/- 1C as we are using integers
	#define heater_threshold_temp 50 // above 40C is hot for human touch
void heater_fsm_transition(void)
{	
	if (HEATER_START){
		float temperature_difference =  (float)(measured_temperature - desired_temperature);
		
		switch(heater_fsm_state){
			case heater_fsm_OFF: {
				heater_fsm_state = heater_fsm_idle;
				break;
			}
			case heater_fsm_cooling:
			case heater_fsm_heating:
			case heater_fsm_idle: {
				if ( temperature_difference > heater_fsm_margin) heater_fsm_state = heater_fsm_cooling; //cool temp is above desired + threshold
				else if ( abs(temperature_difference) <= heater_fsm_margin) heater_fsm_state = heater_fsm_idle; //maintain heat if it between 0.5C threshold
				else heater_fsm_state = heater_fsm_heating; //heat up if is below the desired+threshold
				
				break;
			}
// 			case heater_fsm_heating: {
// 				if ( temperature_difference > heater_fsm_margin) heater_fsm_state = heater_fsm_cooling; //cool temp is above desired + threshold
// 				else if ( abs(temperature_difference) <= heater_fsm_margin) heater_fsm_state = heater_fsm_idle; //maintain heat if it between 0.5C threshold
// 				else heater_fsm_state = heater_fsm_heating; //heat up if is below the desired+threshold
// 				break;
// 			}
// 			case heater_fsm_cooling: {
// 				if ( temperature_difference > heater_fsm_margin) heater_fsm_state = heater_fsm_cooling; //cool temp is above desired + threshold
// 				else if ( abs(temperature_difference) <= heater_fsm_margin) heater_fsm_state = heater_fsm_idle; //maintain heat if it between 0.5C threshold
// 				else heater_fsm_state = heater_fsm_heating; //heat up if is below the desired+threshold
// 				break;
// 			}
			default: heater_fsm_state = heater_fsm_OFF;
		}
	}
	else heater_fsm_state = heater_fsm_OFF; //if the heater is stopped at any moment, it goes off
	
	return;
}

void heater_fsm_RUN(void){
	if (measured_temperature < heater_threshold_temp && digitalRead(BLUE_LED)!=HIGH) digitalWrite(BLUE_LED,HIGH);//turn on safe to touch LED if not high already
	else digitalWrite(BLUE_LED,LOW);  //turn off safe to touch LED
	
	switch(heater_fsm_state){
		case heater_fsm_OFF: {
			//all pins off
			digitalWrite(RED_LED, LOW); //turn of heating signal
			heater_PWM_duty =0;
			break;
		}
		case heater_fsm_idle: {
			digitalWrite(RED_LED, LOW); 
			break;
		}
		case heater_fsm_heating: {
			digitalWrite(RED_LED,HIGH); //turn on heating signal
			if (heater_PWM_duty >= 255) heater_PWM_duty=255;
			else heater_PWM_duty++;	
			break;
		}
		case heater_fsm_cooling: {
			digitalWrite(RED_LED, LOW); //turn of heating signal
			if (heater_PWM_duty <= 0) heater_PWM_duty=0;
			else heater_PWM_duty--;	
			break;
		}
		default: heater_fsm_state = heater_fsm_OFF;
	}
	
	analogWrite(HEATER_PWM,heater_PWM_duty);
	return;
}