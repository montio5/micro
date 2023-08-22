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
bool isTrashOpen = false;  // Flag indicating trash duration(open or close)
bool isTrashCompleteOpen = false; // Flag indicating if trash is completely open
bool isTrashCompleteColse = false; // Flag indicating if trash is completely closed
bool isMotorWorking = false; // Flag indicating motor status (working or not)
bool isInAutomaticMode = true; // Flag indicating the mode
bool buzzerOn =false; // when alarm flag on
bool isObjectDetected  =false; // when something detected near trash
char receivedString[200]; // Declare a character array to store the received string
uint8_t counter = 0; // Counter to keep track of the string length
char setTimeCommand[] = "set time";
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
		// 		serial_send_string(receivedString);
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
	printClock();
	distanceTimeCounter++;
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
		isTrashCompleteOpen=false;
		isMotorWorking=true;
	}
	if(distanceTimeCounter==16 && !measurementFlag){
		distanceTimeCounter=0;
		measurementFlag=true;
	}
}

int main(void) {

	init();
	while (1) {
		//motor process
		if (isMotorWorking) {
			if (isTrashOpen) {
				openTrash();
				} else if (!isTrashOpen && isInAutomaticMode) {
				closeTrash();
			}
		}
		
		// alarm process
		if(buzzerOn){
			turnOnBuzzer();
		}
		if(!buzzerOn){
			turnOffBuzzer();
		}
		if (measurementFlag) {
			measurementFlag = false;
			sendTriggerPulse();
			// Calculate distance and set objectDetected flag
			float distance = calculateDistance();
			if (distance < Desired_Distance) {
				isObjectDetected = true;
				} else {
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
	isTrashCompleteColse = true;
	isMotorWorking = false;
}

//===================

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

//===================

void initClock() {
	DateTime_t t;
	t.Second = 57;
	t.Minute = 59;
	t.Hour = 18;
	t.Day = Sunday;
	t.Date = 29;
	t.Month = June;
	t.Year = 2025;
	RTC_Set(t);

}

void setupAlarm() {
	// Set the time for the alarm (7 PM)
	uint8_t alarmHours = 19;   // 24-hour format
	uint8_t alarmMinutes = 0;
	uint8_t alarmSeconds = 0;

	// Set the alarm to trigger every day at the specified time
	RTC_AlarmSet(Alarm1_Match_Hours, 0, alarmHours, alarmMinutes, alarmSeconds);
}

//=============================================
void sensorInit() {
	// Set TRIGGER_PIN as output and ECHO_PIN as input
	DDRB |= (1 << TRIG_PIN); // Set TRIGGER_PIN as output
	DDRB &= ~(1 << ECHO_PIN);   // Set ECHO_PIN as input
	// Enable pull-up resistor for ECHO_PIN
	PORTB |= (1 << ECHO_PIN);
}

void init(){
	initClock();
	setupAlarm();
	initBuzzer();
	sensorInit();
	DDRD |= (1 << BUZZER_PIN);  //buzzer init
	DDRA |= (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3);// motor init
	i2c_master_init(I2C_SCL_FREQUENCY_400);
	lcd1 = lq_init(0x27, 16, 2, LCD_5x8DOTS);
	cli(); // Disable interrupts during timer setup
	TIMSK |= (1 << TOIE0);
	TCNT0 = 5; // Timer start
	TCCR0 = (1 << CS02) | (1 << CS00); // 101: Prescaler = 1024
	sei(); // Enable interrupts after timer setup
	serial_init();
	serial_send_string(" Enter timer command: "); // Look at how \r works
}
//======================

void sendTriggerPulse() {
	// Generate a pulse on TRIGGER_PIN to trigger the ultrasonic sensor
	PORTA |= (1 << TRIG_PIN); // Set TRIGGER_PIN high
	_delay_us(10);               // Wait for a short duration
	PORTA &= ~(1 << TRIG_PIN);// Set TRIGGER_PIN low
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


//=====================

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
	}
}

void setTimeFromReceivedString(const char* str) {
	// 	serial_send_string(str);
	char formatString[] = "set time %d:%d:%d %d/%d/%d";
	// Extract the values from the received string
	DateTime_t t1 ;
	sscanf(str, formatString, &t1.Hour, &t1.Minute, &t1.Second, &t1.Month, &t1.Date, &t1.Year);
	// 	char numarr[50];
	// 	sprintf(numarr,"%02d:%02d:%02d %02d/%02d/%02d",t1.Hour,t1.Minute,t1.Second, t1.Month, t1.Date, t1.Year);
	// 	serial_send_string(numarr);
	RTC_Set(t1);
}

int processCommand(char* str) {
	if (strstr(str, setTimeCommand) != NULL) {
		setTimeFromReceivedString(str);
	}
	return 0;
}

void printDistanceInSerial(float distance){
	
	char distanceStr[20];
	snprintf(distanceStr, sizeof(distanceStr), "%.2f", distance);
	
	// Send the distance string over UART
	serial_send_string(distanceStr);
}





