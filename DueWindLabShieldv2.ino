/*
*#### Due WindShield Firmware ####
*version alpha 0.6_beta:
*/

/*
* created: 29.10.2015
* Author: Maik Wagner
* E-Mail: Wagner@mf.tu-berlin.de
* alternative E-Mail: Maik.Wagner@gmx.eu
*/

/*
######## Programming Hint: ########
For Arduino Due, datatype int is the same size as long: -2.147.483.648 to 2.147.483.647
######## Programming Hint  ########
*/

#include "speed_enc.h"
#include "constString.h"
#include "MCP4726.h"
#include <Wire.h>
#include <math.h>

//############### Variables ###############
#pragma region variables	//Atmel Studio variable to collapse code regions

//defines
#define F_CPU 84000000L	//declare CPU Frequency


// header definitions
boolean DEBUG=false;

String version ="Due 0.6_beta";	//version string
const long buad = 250000;			// baud rate
int32_t Number;						// variable that holds the incoming numbers
uint16_t Load;						// contains the Load value to be set
uint16_t WindSpeed=0;				// contains the wind speed value to be set

// ########### Pin definitions #########
// ## Pitch ##
const uint8_t P_DIR		= 22;	//stepper motor Direction Pin
const uint8_t P_STP		= 24;	//stepper motor Step Pin
const uint8_t P_CFG2	= 32;	//stepper motor Configure1 Pin
const uint8_t P_CFG1	= 34;	//stepper motor Configure2 Pin
const uint8_t P_EN		= 36;	//stepper motor Enable Pin

// ## Motor ##
const uint8_t M_BRK = 50;			//Motor Brake activate Pin
const uint8_t M_DIR = 46;			//Motor set direction Pin
const uint8_t M_PWM_out = 3;		//Motor Speed Pin (PWM)
const uint8_t M_VPROPI = A8;		//Motor current sense pin 500mV/1A
const uint8_t  M_SWITCH = 8;		//Motor Switch Pin: set Pin HIGH for Motormode; LOW for Generatormode
const uint8_t M_SLEEP= 48;			//Motor Sleep Pin; sets controller to sleep mode

// ## Fan ##

// ## reverse Protection ##
const uint8_t G_revProt = 45;		//Generator Reverse Protection, activates a relays to cut line if the rotor turns counterclockwise



//########### switches ###########
// activates (=1) or deactivates the according action
uint8_t sendData = 0;		// switch for data stream
uint8_t LoadControl = 0;	// switch for setting the Load
uint8_t setWindSpeed = 0;	// switch for setting wind speed
uint8_t setPitch =0;		// switch for setting pitch angle
uint8_t setMotorSpeed=0;	// switch for setting motor speed (only when motor mode is active)
uint8_t setMotorMode=0;		// switch for setting motor mode
uint8_t setMotorBrake=0;	// switch to activate motor brake
uint8_t newInterface =1;	// enables the extended data stream output format, disabled for old interface( for downward compatibility)
bool revTurn=false;			// saves, if the rotor was turning ccw
uint16_t revCounter=200;		// period of how long the relay is turned on (as hysteresis)
uint16_t revCount=revCounter;// counter variable

// variables for time readings
volatile unsigned long currTime = 0;

// Variables for ADC/Data readings
long mVoltage;
long mCurrent;
long mRPM;
long calcPower;

// Setup ADC
const int ADCbits = 12;
const long ADCresolution= pow(2,ADCbits);

// Variables for DAC
uint8_t DAC_Address = 0x61; // Address for MCP4726A1T-E/CH with A1 = �1100001� I2C Address (Datasheet)

// setup DAC for external Voltage reference
const long V_Ref_ext = 3.000; // From MAX6216 external Voltage reference in [V]
const long V_bit_ref = 3*ADCresolution; // for explanation see function readVcc!

//Variables for Serial Communication
String msg = "";

//Variables for Pitch controll
int32_t Pitch_Target_Angle=-100;		// in deg/10
int32_t Pitch_Current_Angle=-100;		// in deg/10
int32_t Pitch_Current_Step_Position;
int32_t Pitch_Target_Position;

uint8_t MStep=2;						// initial Micro step setting
uint16_t Pitch_delayTimeHigh = 200;		//usec
uint16_t Pitch_delayTimeLow =  100;		//usec
uint8_t P_FWD = HIGH;
uint8_t P_BWD = LOW;

// variables for motor control
int16_t MotorSpeed = 0;

//External classes binding
speed_enc speed(1024*4, 100);  //Encoder Pulses per Revolution (QuadEncoder), Samples per Seconds
MCP4726 dac=MCP4726(DAC_Address); // object for the external DAC

#pragma endregion //variables

//############### Functions ###############
#pragma region functions

//############### ADC readings ############
#pragma region ADC_Readings
double readVcc(){
	/*
	* read Voltage Reference at Analog input (3.000V)
	* to compensate possible Input Voltage fluctuations
	*
	* V=V_Ref*bit/2^n (n is the bit resolution of the ADC, bit is what the ADC gives back)
	* In this case V_ref is the supply Voltage of the Arduino and is ca 3,3V.
	* This voltage is not supposed to be pretty stable and temperature independent
	* and hence not really good for accurate ADC measurements.
	* This is to improve this fact.
	* We know the Reference Voltage from A2
	* (comes from the MAX6216 External Voltage Reference Chip),
	* which is 3,000V with a maximum error of +-0,02%
	* => V_Ref = (V* 2^n)/ bit
	* 2^n cancel out later so it is not necessary to put into calculation
	* for further calculations see documentation of this project
	*/
	double result;
	return (double)(V_Ref_ext)/analogRead(A2);
}

long readVoltage(double Vcc_bit){
	// measures Voltage, converts it into real Units (with respect to voltage divider infront of the measurement (*3))
	return Vcc_bit*analogRead(A0)*3000; //in mV
}

double corrCurrent(double Current){
	// applies the correction factor for current measurement
	return (Current-(Current*0.0264-1.511));
}

long readCurrent(double Vcc_bit){
	// measures shunt voltage for current, converts it into real Units
	// with respect of the correction calculation
	return (Vcc_bit*analogRead(A1))*1000; //in mA
}

void L_setLoad(uint16_t value){
	// value to bit number
	value = constrain(value,0,4095);
	dac.setVoltage(value);
}

uint16_t L_perMille2bit(int16_t _perMille){
	// converts the Load set value from [%*10] to a bit value
	uint16_t _bitNum =(4095*_perMille)/1000;
	return _bitNum;
}
#pragma endregion //ADC_Readings

//############### Pitch functions #########
#pragma region Pitch_Commands

void P_resetPitchPosition(void){
	//resets the Pitch position variable
	Pitch_Current_Step_Position=P_Angle2Step(-100);
	Pitch_Target_Position=P_Angle2Step(-100);
	Pitch_Target_Angle=-100;
	Pitch_Current_Angle=-100;
}

int32_t P_Distance_to_go(){
	return Pitch_Target_Position-Pitch_Current_Step_Position;
}

void P_Enable(uint8_t activate){
	// dis-/enables the stepper driver
	if (activate)
	{
		delayMicroseconds(2);	//according to datasheet of TMC2100 (silentStepStick Board) chapter 11.1 Timing
		digitalWrite(P_EN,LOW); //pin is active Low
		delayMicroseconds(2);	//according to datasheet of TMC2100 (silentStepStick Board) chapter 11.1 Timing
	}
	else
	{
		delayMicroseconds(2);
		digitalWrite(P_EN,HIGH);
		delayMicroseconds(2);
	}
}

void P_Step(){
	//does one step in specified direction if necessary
	if(P_Distance_to_go()>0){
		digitalWrite(P_DIR,P_FWD);
		P_Enable(1);
		Pitch_Current_Step_Position++;		// Step Position counter
	}
	else if(P_Distance_to_go()<0){
		digitalWrite(P_DIR,P_BWD);
		P_Enable(1);
		Pitch_Current_Step_Position--;		// Step Position counter
	}
	else{
		return;
	}
	digitalWrite(P_STP,HIGH);
	delayMicroseconds(Pitch_delayTimeHigh);
	digitalWrite(P_STP,LOW);
	delayMicroseconds(Pitch_delayTimeLow);
	P_Enable(0);
	
}

/////////
//matlab replacement functions
float sind(float _Angle){
	// sin of an Arguments in degree
	float _rad = _Angle *PI/180;
	//Serial.print("sind: ");
	//Serial.println((float_t)sin(_rad));
	return (float)sin(_rad);
}

float tand(float _Angle){
	// tan of an Arguments in degree
	float _rad = _Angle *PI/180;
	//Serial.print("tand: ");
	//Serial.println((float_t)tan(_rad));
	return (float)tan(_rad);
}
//////////


int32_t long P_Angle2Step(int32_t _gamma){
	// converts an angle to respective steps
	// author : Dipl. Ing. Staffan Wiens
	// converted to C++ function: Maik Wagner
	int32_t _step;
	const float _R		= 3.0;			//[mm] radius of bolt center in blade root;
	const float _r		= 1.0;			//[mm] radius of bolt
	const float _beta	= 8.53076561;	//[deg] angle of channel in shaft
	
	const float _alfa0	= 30.0;			//[deg] starting value of alfa corresponding to xS = 0
	
	if (_gamma<-100){
		_gamma=-100;
	}
	else if (_gamma>500){
		_gamma=500;
	}
	//float _gamma_f = _gamma/10;
	const float _alfa		= 20.0 - _gamma/10.0;	//[deg] conversion of pitch angle to angle between x axis and bolt center / blade center
	// Definitions linear actuator
	float _leadPerFullStep = 0.0025; // [mm/1.8�] Spindel AA Adrive CanStack NEMA8
	float _stepMode = 1/(float)MStep; // 1/2 = half step, 1/4 = quater step, ...
	float _lead = _leadPerFullStep*_stepMode; // [mm/step]
	// Calculation
	float _xS = (_R*(sind(_alfa0)-sind(_alfa))/tand(_beta));
	_step = round(_xS/_lead);
	return _step;
}


void P_setMSteps(uint8_t _msteps){
	//Sets the micro step mode
	
	if (_msteps==2){
		// half step mode
		pinMode(P_CFG2, OUTPUT);
		digitalWrite(P_CFG2,LOW);
		digitalWrite(P_CFG1,LOW);	// to be sure the internal pull-up resistor is disabled
		pinMode(P_CFG1,INPUT);		// set the digital pin in quasi tri-state (high impedance state)
		// SpreadCycle Mode \w 256 Steps Interpolation
	}
	if (_msteps==4){
		// quarter step mode
		pinMode(P_CFG2,INPUT);
		pinMode(P_CFG1,OUTPUT);
		digitalWrite(P_CFG1,HIGH);	//stelthChop Mode \w 256 Steps Interpolation
	}
	else{
		MStep=2;					//set half Steps as default
		pinMode(P_CFG2, OUTPUT);
		digitalWrite(P_CFG2,LOW);
		digitalWrite(P_CFG1,LOW);	// to be sure the internal pull-up resistor is disabled
		pinMode(P_CFG1,INPUT);		// set the digital pin in quasi tri-state (high impedance state)
		// SpreadCycle Mode \w 256 Steps Interpolation
	}
}
#pragma endregion //Pitch_Commands

//############### Windspeed commands ######
#pragma region WindSpeed_Commands

void Fan(int Speed){
	// Sets the wind speed
	if(!DEBUG){
		if(Speed&&(Speed<=140)){
			Serial1.println("GO");
			Serial1.print("F");
			Serial1.print(round(0.69*Speed+3.79));
			Serial1.println("%");
		}
		else{
			Serial1.println("X");
		}
	}
	else{
		if(Speed&&(Speed<=140)){
			Serial.println("GO");
			Serial.print("F");
			Serial.print(round(0.69*Speed+3.79));
			Serial.println("%");
		}
		else{
			Serial.println("X");
		}
	}
}
#pragma endregion //WindSpeed_Commands

//############### Motormode commands ######
#pragma region Motormode_Commands

void M_Brake_active(){
	digitalWrite(M_BRK,HIGH);
}

void M_Brake_deactive(){
	digitalWrite(M_BRK,LOW);
}

void M_forward(){
	M_Brake_active();	//Set Brake for better Performance according to DRV8801 Truth Table on page: https://www.pololu.com/product/2136
	digitalWrite(M_DIR,LOW);
	
}

void M_backward(){
	M_Brake_active();	//Set Brake for better Performance according to DRV8801 Truth Table on page: https://www.pololu.com/product/2136
	digitalWrite(M_DIR,HIGH);
}

void M_Brake(){
	analogWrite(M_PWM_out,0);
	M_Brake_active();
}

void M_Brake_release(){
	analogWrite(M_PWM_out,0);
	M_Brake_deactive();
}

void M_SleepMode(int _Mode){
	// activate sleep mode, puts motor output into high impedance (virtually open circuit)
	if(_Mode){
		digitalWrite(M_SLEEP,LOW);
	}
	else{
		digitalWrite(M_SLEEP,HIGH);
		delay(2);
	}
}

void M_SwitchMotorModeOff(){
	//deactivate Motor mode
	M_backward();
	M_Brake_release();
	M_SleepMode(1);
	digitalWrite(M_SWITCH,LOW); //switches the relay
	MotorSpeed=0;
	setMotorMode=0;
}

void M_SwitchMotorModeOn(){
	digitalWrite(M_SWITCH,HIGH);  // activates the relay and connects the Turbine generator to the motor controller
	setMotorMode=1;
	MotorSpeed=0;
}


void M_SetMotorSpeed(long _speed){
	M_SleepMode(0);
	M_SwitchMotorModeOn();
	
	long _Motorspeed= constrain(_speed,0,4095);
	
	if(_Motorspeed!=0){
		M_Brake_active();
		M_forward();
		//analogWrite(M_PWM_out,4095);
		//delay((500));
		analogWrite(M_PWM_out,_speed);
		
		
	}
	else{
		M_Brake();
	}
}

long M_Percent2bit(int _percent){
	return (long)(4095*_percent/100);
}
#pragma endregion //Motormode_Commands

//############### serial communication ####
#pragma region Serial_Communication

void printHelp(){
	//Help Massage
	Serial.println();
	Serial.println("WindShield");
	Serial.print("Firmware Version: ");
	Serial.println(version);
	Serial.println("##########################");
	Serial.println("###### Commands ##########");
	Serial.println("###### general ###########");
	Serial.println("HH : prints this message");
	Serial.println("DD : toggle debug mode on and off");
	Serial.println("ST : Stops the Fan and turns off the Load ");
	Serial.println("##### Data stream ########");
	Serial.println("A0 : print Volt, Current, RPM, Power and a Timestamp (since last restart) in actual Units ([mV],[mA],[1/min],[mW],[us])");
	Serial.println("     Output is in JSON Style!!!");
	Serial.println("##### Load control #######");
	Serial.println("IIx: Set the Load to value x in [mA] (0 - 1500mA)");
	Serial.println("##### Fan control ########");
	Serial.println("WSx: Set The fan to value x in [cm/s] (0 - 140 cm/s)");
	Serial.println("##### Pitch control ######");
	Serial.println("PAx: The Pitch slide goes to angle in [deg/10] (-100 - +500 deg/10)");
	Serial.println("PFx: The Pitch slide goes x Steps forward (to rotor plane) (for firmware backward compatibility)");
	Serial.println("PBx: The Pitch slide goes x Steps backward (away from rotor plane) (for firmware backward compatibility)");
	Serial.println("PP : The Pitch slide goes 1000 Steps forward (Maintenance Mode)");
	Serial.println("PM : The Pitch slide goes 1000 Steps backward (Maintenance Mode)");
	Serial.println("P0 : Resets the current Position variable to -100 [deg/10]");
	Serial.println("##### Motor mode #########");
	Serial.println("MM : Switch to Motor mode");
	Serial.println("MSx: Set  the Motor speed in [%] (0-100%)");
	Serial.println("MB : Brakes the Motor until stand still (0rpm)");
	Serial.println("MR : Release the Brake");
	Serial.println("MG : Switch to Generator mode");
	Serial.println("##### End of Help ########");
	Serial.println("##########################");
	Serial.println("NI : Switch the Interface from old to new one");
}

int SetCommand(String _msg){
	// Definition of the Serial Commands and the send back Data
	if (_msg.equals("")) {return 0;}
	if (_msg.equals("DD")){
		// de-/&active Debug mode
		DEBUG=!DEBUG;
		if (DEBUG){
			Serial.println("Debug: On");
		}
		else{
			Serial.println("Debug: Off");
		}
		sendData=0;
	}
	else if (_msg.equals("A0")){
		//stops data stream
		sendData =0;
	}
	else if (_msg.equals("AA")){
		//starts data stream
		sendData =1;
	}
	else if (msg.equals("NI")){			//toggles between new and old data output
		if (newInterface==1){
			newInterface=0;
		}
		else if (newInterface==0){
			newInterface=1;
		}
	}
	else if (msg.equals("II")){
		//Set Load value
		LoadControl=1;
		Load=(uint16_t)Number;
	}
	else if(_msg.equals("WS")){
		//set Wind speed value
		setWindSpeed=1;
		WindSpeed=(uint16_t)Number;
	}
	else if (_msg.equals("HH")){
		printHelp();
	}

	else if(_msg.equals("ST")){
		TurnOffWindLab();

	}
	// ########## pitch ##############
	else if(_msg.equals("PF")){
		//Set the forward Pitch (old behavior, awaits steps not angle)
		Pitch_Target_Position=Pitch_Current_Step_Position-Number;
	}
	else if(_msg.equals("PB")){
		//Set the backward Pitch (old behavior, awaits steps not angle)
		Pitch_Target_Position=Pitch_Current_Step_Position+Number;
	}
	else if(_msg.equals("PP")){
		//Set the forward Pitch in maintenance mode (1000 steps forward)(old behavior, awaits steps not angle)
		Pitch_Target_Position=Pitch_Current_Step_Position-1000;
	}
	else if(_msg.equals("PM")){
		//Set the backward Pitch in maintenance mode (1000 steps backward) (old behavior, awaits steps not angle)
		Pitch_Target_Position=Pitch_Current_Step_Position+1000;
	}
	else if (msg.equals("P0")){
		P_resetPitchPosition();
	}
	else if(_msg.equals("PA")){
		// calculates and set the new target position
		Pitch_Target_Angle=Number;
		Pitch_Target_Position=P_Angle2Step(Pitch_Target_Angle);
	}
	//########## Motor mode ######################
	else if(msg.equals("MM")){
		// switch to motor mode
		M_SwitchMotorModeOn();
	}
	else if(msg.equals("MS")){
		// set motor speed value
		setMotorSpeed=1;
		MotorSpeed=Number;
	}
	else if(msg.equals("MB")){
		// activate motor brake
		setMotorBrake=1;
		M_Brake();
	}
	else if(msg.equals("MR")){
		// release Brake
		M_Brake_release();
	}
	else if(msg.equals("MG")){
		// switch to generator mode
		M_SwitchMotorModeOff();
	}
	else{
		msg="";
		Number=0;
		
	}
	msg="";
	Number=0;
}
#pragma endregion //Serial_Communication

// ############## internal communication ##
#pragma region Internal_Comm_and_setup
void TurnOffWindLab(void){
	// turn off WInd tunnel and resets some switches and variables
	sendData=0;
	LoadControl=0;
	setWindSpeed=1;
	WindSpeed=0;
	sendData=0;
	
	//Turn off controll
	Load=0;
	WindSpeed=0;
	
}

//######### minor setup  ###########
void PinSetup(){
	// sets pin direction and state
	//Pitch
	pinMode(P_CFG1,OUTPUT);
	pinMode(P_CFG2,OUTPUT);
	pinMode(P_DIR,OUTPUT);
	pinMode(P_EN,OUTPUT);
	pinMode(P_STP,OUTPUT);
	digitalWrite(P_CFG1,LOW);
	digitalWrite(P_CFG2,LOW);
	digitalWrite(P_DIR,LOW);
	digitalWrite(P_EN,HIGH);
	digitalWrite(P_STP,LOW);
	
	// motor controller
	pinMode(M_BRK,OUTPUT);
	pinMode(M_DIR,OUTPUT);
	pinMode(M_PWM_out,OUTPUT);
	pinMode(M_SWITCH,OUTPUT);
	pinMode(M_SLEEP,OUTPUT);
	digitalWrite(M_BRK,LOW);
	digitalWrite(M_DIR,LOW);
	digitalWrite(M_PWM_out,LOW);
	digitalWrite(M_SWITCH,LOW);
	digitalWrite(M_SLEEP,LOW);
	
	//reverse Protection Pin to switch a relay from closed to open circuit if the rotor turns counterclockwise
	pinMode(G_revProt,OUTPUT);
	digitalWrite(G_revProt,LOW);
	
}
#pragma endregion //Internal_Comm_and_setup

#pragma region reverseProtection


void G_revProtect(uint8_t protPin){
	if (revTurn==true&&revCount!=0){
		revCount--;		
		digitalWrite(protPin,HIGH);
		//Serial.println(revCount);
		return;
	}
	else if ((speed.speed()<0)&&(revTurn==false)){
		revTurn=true;
		revCount=revCounter;
		digitalWrite(protPin,HIGH);
		return;
	}
	else{
		digitalWrite(protPin,LOW);
		revTurn=false;
		revCount=0;
	}
	
	
}

#pragma endregion	// region reverseProtection

#pragma endregion	//region functions

//############### main setup ##############
#pragma region Main_Setup
void setup(){
	//// setup at programm start
	//setup serial communication
	Serial.begin(buad);
	Serial1.begin(9600);
	//setup Analog read/write resolustion
	analogReadResolution(ADCbits);
	analogWriteResolution(ADCbits);
	// setup ADC measuring speed
	adc_init(ADC, SystemCoreClock, 21000000L, 3); // (xxx,xxx, ADC_CLock, ADC_Startup_time in us) refer to Datasheet!
	//Pin Setup
	PinSetup();
	// Pitch Setup
	P_Enable(0);
	P_setMSteps(MStep);
	Pitch_Current_Step_Position=P_Angle2Step(Pitch_Current_Angle );
	Pitch_Target_Position=P_Angle2Step(Pitch_Target_Angle);
	//Dac Setup
	//start DAC and set to 0A (Open circuit)
	dac.begin();
	L_setLoad(0);
	
	//Wind Speed setup
	Serial1.println("GO");	// activates safe start condition

}
#pragma endregion //Main_Setup

//############### main loop ###############
#pragma region Main_Loop

void loop(){
	//! Read the Serial Data in for the commands
	// receive command
	if (!setMotorMode){
		G_revProtect(G_revProt);
	}
	
	if(Serial.available()>0){
		msg="";
		while(Serial.available()>0){
			msg+=char(Serial.read());
			delay(10);
		}
		Number= msg.substring(2).toInt();
		msg = msg.substring(0,2);
		msg.toUpperCase();
		
		//rated command
		SetCommand(msg);
	}// end if receive command

	//// performs commands
	// set wind speed
	if (setWindSpeed){
		Fan(WindSpeed);
		setWindSpeed=0;
	}
	
	//Loadcontrol
	if(LoadControl){
		//Serial.print("II: ");
		//Serial.println(Load);
		L_setLoad(L_perMille2bit(Load));
		Number=0;
		msg="";
		LoadControl=0;
	}
	
	// Pitchcontrol
	if(!setPitch){
		P_Step();
	}
	
	//motor mode control
	if (setMotorSpeed&&setMotorMode){
		M_SetMotorSpeed(M_Percent2bit(MotorSpeed));
		setMotorSpeed=0;
	}
	
	//output data stream
	if (sendData){
		if(!DEBUG&&!newInterface){
			//JSON Output, old interface style
			float VCC = readVcc();
			currTime = micros();
			mVoltage	 = readVoltage(VCC);	//in mV
			mCurrent	 = readCurrent(VCC);	//in mA
			mRPM		 = speed.speed();		//in rpm
			calcPower	 = mVoltage*mCurrent/1000;	//in milliWatt
			
			Serial.println("{");
				Serial.print("\t\"voltage\": ");
				Serial.print(constStringLength(mVoltage,5));
				Serial.println(",");
				Serial.print("\t\"current\": ");
				Serial.print(constStringLength(mCurrent,4));
				Serial.println(",");
				Serial.print("\t\"rpm\": ");
				Serial.print(constStringLength(mRPM,8));
				Serial.println(",");
				Serial.print("\t\"power\": ");
				Serial.print(constStringLength(calcPower,4));
				Serial.println(",");
				Serial.print("\t\"timestamp\": ");
				Serial.println(constStringLength(currTime,10));
			Serial.println("}");
			Serial.println("EOL");
		}
		else if(!DEBUG&&newInterface)
		{
			// measures and calculate data
			double VCC					= readVcc();			// reads supply voltage
			currTime					= micros();				// timestamp in uSec since last restart
			mVoltage					= readVoltage(VCC);		// in mV
			mCurrent					= readCurrent(VCC);		// in mA
			mRPM						= speed.speed();		// in rpm
			uint16_t windVelocity		= WindSpeed;			// send back the set wind speed for the moment
			int16_t pitchTargetAngle	= Pitch_Target_Angle;	// send back the target pitch angle
			uint16_t current_sink		= Load;					// send back the load setting
			
			// in preparation for the sensor board, constant output at the moment
			int16_t	temperature			= 23*10;				// temperature in deg C*10
			unsigned long pressure_amb	= 101300000;			// ambient pressure in mPa
			uint16_t humidity			= 1000;					// humidity in %*10
			int32_t g_x					= 0;					// Acceleration against wind flow direction in mg
			int32_t g_y					= 0;					// Acceleration perpendicular to wind flow in horizontal direction in mg
			int32_t g_z					= -1;					// Acceleration in up direction  in mg
			
			
			//  send the data in JSON format, in between the pitch steps one step
			Serial.println("{");
				Serial.print("\t\"voltage\": ");						// voltage
				P_Step();
				Serial.print(constStringLength(mVoltage,5));
				P_Step();
				Serial.println(",");
				Serial.print("\t\"current\": ");						// current
				P_Step();
				Serial.print(constStringLength(mCurrent,4));
				P_Step();
				Serial.println(",");
				Serial.print("\t\"rpm\": ");							// rpm
				P_Step();
				Serial.print(constStringLength(mRPM,8));
				P_Step();
				Serial.println(",");
				Serial.print("\t\"windSpeed\": ");						// wind speed
				P_Step();
				Serial.print(constStringLength(windVelocity,4));
				P_Step();
				Serial.println(",");
				Serial.print("\t\"pitchAngle\": ");						// target pitch angle
				P_Step();
				Serial.print(constStringLength(pitchTargetAngle,4));
				P_Step();
				Serial.println(",");
				Serial.print("\t\"temperature\": ");					// temperature
				P_Step();
				Serial.print(constStringLength(temperature,4));
				P_Step();
				Serial.println(",");
				Serial.print("\t\"ambientPressure\": ");				// ambient pressure
				P_Step();
				Serial.print(constStringLength(pressure_amb,9));
				P_Step();
				Serial.println(",");
				Serial.print("\t\"humidity\": ");						// humidity
				P_Step();
				Serial.print(constStringLength(humidity,4));
				P_Step();
				Serial.println(",");
				Serial.print("\t\"accelerationX\": ");					// x accelerometer
				P_Step();
				Serial.print(constStringLength(g_x,5));
				P_Step();
				Serial.println(",");
				Serial.print("\t\"accelerationY\": ");					// y accelerometer
				P_Step();
				Serial.print(constStringLength(g_y,5));
				P_Step();
				Serial.println(",");
				Serial.print("\t\"accelerationZ\": ");					// z accelerometer
				P_Step();
				Serial.print(constStringLength(g_z,5));
				P_Step();
				Serial.println(",");
				Serial.print("\t\"currentSink\": ");					// dummy load setting
				P_Step();
				Serial.print(constStringLength(current_sink,4));
				P_Step();
				Serial.println(",");
				Serial.print("\t\"timestamp\": ");						// timestamp
				P_Step();
				Serial.println(constStringLength(currTime,10));
				P_Step();
			Serial.println("}");
			Serial.println("EOL");
			P_Step();
			
		}
		else{
			//Debug
			//Raw data output
			double VCC		= readVcc();
			currTime		= micros();
			mVoltage		= readVoltage(VCC);		// in mV
			mCurrent		= readCurrent(VCC);		// in mA
			mRPM			= speed.speed();		// in rpm
			Serial.print("V: ");
			Serial.print(mVoltage);
			Serial.print("; I: ");
			Serial.print(mCurrent);
			Serial.print("; N: ");
			Serial.print(mRPM);
			Serial.print("; t: ");
			Serial.println(currTime);
			
			
		}
	}


}
#pragma endregion // Main_Loop
