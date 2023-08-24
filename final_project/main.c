#include <stdio.h>
#include <avr/io.h>
#include <stdbool.h>
#include <string.h>
#include <avr/interrupt.h>
#include "DS3232_lib.h"
#include "i2c_lib.h"
#include "liquid_crystal_i2c_lib.h"
#include "Serial_lib.h"
#include <util/delay.h>
#define BUZZER_PIN  PD7
#define TRIG_PIN PB1
#define ECHO_PIN PB0
#define Desired_Distance 20
// Vars
LiquidCrystalDevice_t lcd1;
DateTime_t t;

	// Set the time for the alarm (7 PM)
uint8_t alarmHours = 19;   // 24-hour format
uint8_t alarmMinutes = 0;
uint8_t alarmSeconds = 0;
	//
bool isTrashOpen = false;  // Flag indicating trash duration(open or close)
bool isTrashCompleteOpen = false; // Flag indicating if trash is completely open
bool isTrashCompleteClose = false; // Flag indicating if trash is completely closed
bool isMotorWorking = false; // Flag indicating motor status (working or not)
bool isInAutomaticMode = true; // Flag indicating the mode
bool buzzerOn =false; // when alarm flag on
bool isObjectDetected  =false; // when something detected near trash
char receivedString[200]; // Declare a character array to store the received string
uint8_t counter = 0; // Counter to keep track of the string length
char setTimeCommand[] = "set time";
char closeTrashCommand[] = "close";
char openTrashCommand[] = "open";

volatile uint16_t echoStartTime = 0;
volatile uint16_t echoEndTime = 0;
volatile bool measurementFlag = false;

volatile uint16_t timeCounter = 0;
volatile uint16_t trashOpentimeCounter = 0;
volatile uint16_t distanceTimeCounter= 0;
//function
void init();
void printClock();
void turnOffBuzzer();
int  processCommand();
float calculateDistance();
void sendTriggerPulse();
/*bool isAlarmTime();*/
void ADC_Init();
void calculateTemp();

#if SERIAL_INTERRUPT == 1
ISR(USART_RXC_vect) {
	char c = UDR;
	UDR = c;
	timeCounter = 0;
	// Save the received character
	receivedString[counter] = c;
	counter++;
	if (c == '\r') { // If user has pressed ENTER (in Proteus)
		receivedString[counter] = '\0'; // Null-terminate the string
		serial_send_string(receivedString);
		processCommand(receivedString);
		// Reset the counter and clear the received string for the next input
		counter = 0;
		memset(receivedString, 0, sizeof(receivedString));
	}
}
#endif

ISR(TIMER0_OVF_vect) { // Timer0 overflow interrupt
	TCNT0 = 6;
	TIFR |= (1 << TOV0);
	distanceTimeCounter++;
	printClock();
	//=========
	//buzzer
	if (buzzerOn){
	timeCounter++;}
	if (timeCounter == 33 && buzzerOn) { // Adjusted for approximately 33 for 1 sec
		//turn off buzzer after 1 sec
		timeCounter = 0;
		TCNT0 = 6;
		buzzerOn=false;
	}
	//========
	//sensor
	if(isObjectDetected){
		trashOpentimeCounter++;
	}

	if (timeCounter == 325 && isObjectDetected) { // Adjusted for approximately 325 for 10 sec
		// closing trash after 10s
		trashOpentimeCounter = 0;
		TCNT0 = 6;
		isObjectDetected=false;
		isTrashOpen=false;
		isMotorWorking=true;
	}
	if(distanceTimeCounter==16 && !measurementFlag){
		if(isInAutomaticMode){
			measurementFlag=true;
		}
		distanceTimeCounter=0;
		
	}
}
ISR(INT0_vect){
	buzzerOn=true;
	turnOnBuzzer();
	serial_send_string(" buzzzzer \r");
}
int main(void) {

	init();
	serial_send_string(" Enter command: "); // Look at how \r works
	while (1) {
		//motor process
		if (isMotorWorking) {
			serial_send_string(" motor working...\r");
			if (isTrashOpen && !isTrashCompleteOpen) {
				serial_send_string(" motor working->open...\r");
				openTrash();
				} else if (!isTrashOpen && isInAutomaticMode &&!isTrashCompleteClose) {
					serial_send_string(" motor working->close...\r");
				closeTrash();
			}
			else{
				isMotorWorking=false;
			}
		}
		// alarm process
// 		if (isAlarmTime()) {
// 			buzzerOn=true;
// 			turnOnBuzzer();  // 
// 		}
		if (measurementFlag) {
			serial_send_string(" enter sensor\r");
			measurementFlag = false;
			sendTriggerPulse();
			// Calculate distance and set objectDetected flag
			float distance = calculateDistance();
			if (distance < Desired_Distance) {
				isObjectDetected = true;
							serial_send_string(" object found\r");
				} else {
												serial_send_string(" object not found\r");
				isObjectDetected = false;
			}
		}
	}

	return 0;
}

//when the automatic button pressed(to be auto)
//isInAutomaticMode=true;
// timer should count from now

//when the manual button pressed
//isInAutomaticMode=false;
// timer should stop
//isTrashOpen=true;
//isTrashCompleteClose=false;
//if (TrashCompleteOpen==true){isMotorWorking=false;}
//else{isMotorWorking=true}

//===================

void openTrash() {
	// Code to open the curtain
	for (int i = 0; i < 5; i++) {
		PORTA = 0x04;
		_delay_ms(200);
		PORTA = 0x02;
		_delay_ms(200);
		PORTA = 0x08;
		_delay_ms(200);
		PORTA = 0x01;
		_delay_ms(200);
		serial_send_string(" opening...\r");

	}
	isTrashCompleteOpen = true;
	isMotorWorking = false;
}

void closeTrash() {
	// Code to close the curtain
	for (int i = 0; i < 5; i++) {
		PORTA = 0x01;
		_delay_ms(200);
		PORTA = 0x08;
		_delay_ms(200);
		PORTA = 0x02;
		_delay_ms(200);
		PORTA = 0x04;
		_delay_ms(200);
		serial_send_string(" closing...\r");

	}
	isTrashCompleteClose = true;
	isMotorWorking = false;
}

//===================
// alarm
void initBuzzer() {
	// Initialize buzzer pin as output
	DDRD |= (1 << BUZZER_PIN);
}

void turnOnBuzzer() {
	PORTD |= (1 << BUZZER_PIN); // Set the pin high to turn on the buzzer
}

void turnOffBuzzer() {
	PORTD &= ~(1 << BUZZER_PIN); // Set the pin low to turn off the buzzer
}

void initClock() {
	DateTime_t t;
	t.Second = 55;
	t.Minute = 59;
	t.Hour = 18;
	t.Day = Sunday;
	t.Date = 29;
	t.Month = June;
	t.Year = 2025;
	RTC_Set(t);
}

void setupAlarm() {
	// Set the alarm to trigger every day at the specified time
    DDRD |= (1 << 7);  // Clear the corresponding bit in DDRA register
    PORTD |= (1 << PD2);  // Set the corresponding bit in PORTA register	RTC_AlarmInterrupt(Alarm_1,1);
	RTC_AlarmSet(Alarm1_Match_Hours, t.Date, alarmHours, alarmMinutes, alarmSeconds);
}
// bool isAlarmTime() {
// 	t = RTC_Get();
// 	// Compare the current time with the alarm time
// 	if (t.Hour == alarmHours &&
// 	t.Minute == alarmMinutes &&
// 	t.Second == alarmSeconds) {
// 		return true;  // Alarm time has been reached
// 		} else {
// 		return false;  // Alarm time has not been reached
// 	}
// }

//=============================================
// sensor
void sensorInit() {
	// Set TRIGGER_PIN as output and ECHO_PIN as input
	DDRB |= (1 << TRIG_PIN); // Set TRIGGER_PIN as output

}

void sendTriggerPulse() {
	// Generate a pulse on TRIGGER_PIN to trigger the ultrasonic sensor
	PORTB |= (1 << TRIG_PIN); // Set TRIGGER_PIN high
	_delay_us(15);               // Wait for a short duration
	PORTB &= ~(1 << TRIG_PIN);// Set TRIGGER_PIN low
}

float calculateDistance() {
	// Measure the duration of the pulse on the ECHO_PIN
	while (!(PINA & (1 << ECHO_PIN))) {
		// Wait for the pulse to start
	}
	// Record the start time
	echoStartTime = TCNT0;
	while (PINA & (1 << ECHO_PIN)) {
		// Wait for the pulse to end
	}
	// Record the end time
	echoEndTime = TCNT0;
	// Calculate the duration of the pulse
	uint16_t echoDuration = echoEndTime - echoStartTime;
	// Convert the duration to distance using the speed of sound
	// Speed of sound = 34300 cm/s (for air at room temperature)
	// Distance = (duration / 2) * speed of sound
	float distance = (echoDuration * 0.01715); // 0.01715 = 34300 cm/s / 2
	return distance;
}

void printDistanceInSerial(float distance){
	
	char distanceStr[20];
	snprintf(distanceStr, sizeof(distanceStr), "%.2f", distance);
	
	// Send the distance string over UART
	serial_send_string(distanceStr);
}

//======================
//temp

void ADC_Init() {
	//ADMUX |= (1 << REFS0);
	//ADCSRA |= (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0); //set prescaler to 128
	ADMUX = (1 << REFS1) | (1 << REFS0);
	ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
}


uint16_t adcRead()
{
	/*ADMUX = (ADMUX & 0xF0) | (ADC_CHANNEL & 0x0F); //clear previous channel and set a new channel
	ADCSRA |= (1 << ADSC); //start a single ADC conversion by setting the ADSC bit
	while(ADCSRA & (1 << ADSC));//wait for conversion to complete
	return ADC;
	*/
	ADCSRA |= (1 << ADSC) | (1 << ADIF);
	while (ADCSRA & (1 << ADIF) == 0);
	return ADC;
}
void calculateTemp(){
		uint16_t adcValue;
		float temperature;
		adcValue = adcRead();
		temperature = (float)adcRead() / 4;
		char tempStr[10];
		dtostrf(temperature, 4, 1, tempStr);
		lq_setCursor(&lcd1, 1, 13);
		lq_print(&lcd1, tempStr);
}
//=====================================
// common methods

void printClock() {
	// Print time on LCD
	t = RTC_Get();
	if (RTC_Status() == RTC_OK) {
		// Print time on lcd
		lq_setCursor(&lcd1, 0, 0);
		char timeArr[10];
		sprintf(timeArr, "%02d:%02d:%02d", t.Hour, t.Minute, t.Second); // Google about sprintf()
		// Also change "%d:%d:%d" to "%02d:%02d:%02d" and see what happens for 1 digit numbers
		lq_print(&lcd1, timeArr);

		// Print date on lcd
		lq_setCursor(&lcd1, 1, 0);
		char dateArr[10];
		sprintf(dateArr, "%02d/%02d/%02d", t.Year, t.Month, t.Date);
		lq_print(&lcd1, dateArr);
		
		calculateTemp();
	}
}

void setTimeFromReceivedString(const char* str) {
	char formatString[] = "set time %d:%d:%d %d/%d/%d";
	DateTime_t t1 ;
	sscanf(str, formatString, &t1.Hour, &t1.Minute, &t1.Second, &t1.Month, &t1.Date, &t1.Year);
	RTC_Set(t1);
}

int processCommand(char* str) {
	if (strstr(str, setTimeCommand) != NULL) {
		setTimeFromReceivedString(str);
	}
	if (strstr(str, openTrashCommand) != NULL) {
		serial_send_string("open command");
		isMotorWorking=true;
		isTrashOpen=true;

	}
	if (strstr(str, closeTrashCommand) != NULL) {
		serial_send_string("close command");
		isMotorWorking=true;
		isTrashOpen=false;
	}
	return 0;
}
void init(){
	ADC_Init();
	initClock();
	sensorInit();
	DDRD |= (1 << BUZZER_PIN);  //buzzer init
	DDRA |= (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4);// motor init
	i2c_master_init(I2C_SCL_FREQUENCY_400);
	lcd1 = lq_init(0x27, 16, 2, LCD_5x8DOTS);
	cli(); // Disable interrupts during timer setup
	setupAlarm();
	initBuzzer();
	TIMSK |= (1 << TOIE0);
	TCNT0 = 5; // Timer start
	TCCR0 = (1 << CS02) | (1 << CS00); // 101: Prescaler = 1024
	serial_init();
	sei(); // Enable interrupts after timer setup

}
int extractTimeValues(const char *input, uint8_t index)
{
	uint8_t wordCount = 0;
	uint8_t i = 0;
	uint8_t j = 0;
	char *output;
	while (input[i] != '\0'){
		while (input[i] == ' ' || input[i] == '\t' || input[i] == ':' || input[i] == '/')
		i++;
		if (input[i] != ' ' && input[i] != '\t')
		{
			wordCount++;
			if (wordCount == index)
			{
				while (input[i] != ' ' && input[i] != '\t' && input[i] != '\0' && input[i] != ':' && input[i] != '/')
				{
					output[j] = input[i];
					i++;
					j++;
				}
				output[j] = '\0';
				return;
			}
			while (input[i] != ' ' && input[i] != '\t' && input[i] != '\0' && input[i] != ':' && input[i] != '/')
			i++;
		}
	}
	output[0] = '\0';
	
	//convert char to int
	int outputInt =0;
	int x = 0;
	
	while(output[x] != '\0'){
		outputInt = outputInt * 10 + (output[x] - '0');
		x++;
	}
	return outputInt;
}

void setTimeAndDate(char *str)
{
	t = RTC_Get();
	char hour[10];
	char min[10];
	char sec[10];
	char day[10];
	char month[10];
	char year[10];
	
	t.Hour = extractTimeValues(hour, 3);
	t.Minute = extractTimeValues(min, 4);
	t.Second = extractTimeValues(sec,5);
	t.Date = extractTimeValues(day, 6);
	t.Month = extractTimeValues(month, 7);
	t.Year = extractTimeValues(year, 8) + 1988;
	RTC_Set(t);
}
