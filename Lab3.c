// Lab3.c
// Runs on LM4F120/TM4C123
// Real Time Operating System for Lab 3

// Jonathan W. Valvano 2/20/17, valvano@mail.utexas.edu
// Modified by Sile Shu 10/4/17, ss5de@virginia.edu
// You may use, edit, run or distribute this file 
// You are free to change the syntax/organization of this file


#include <stdint.h>
#include "OS.h"
#include "tm4c123gh6pm.h"
#include "LCD.h"
#include <string.h> 
#include "UART.h"
#include "FIFO.h"
#include "joystick.h"

//constants
#define BGCOLOR     LCD_BLACK
#define AXISCOLOR   LCD_ORANGE
#define MAGCOLOR    LCD_YELLOW
#define EWMACOLOR   LCD_CYAN
#define SOUNDCOLOR  LCD_CYAN
#define LIGHTCOLOR  LCD_LIGHTGREEN
#define TOPTXTCOLOR LCD_WHITE
#define TOPNUMCOLOR LCD_ORANGE
#define CROSSSIZE            5
#define PERIOD               4000000   // DAS 20Hz sampling period in system time units
#define PSEUDOPERIOD         8000000
#define LIFETIME             1000
#define RUNLENGTH            600 // 30 seconds run length


extern Sema4Type LCDFree;
uint16_t origin[2]; // the original ADC value of x,y if the joystick is not touched
int16_t x = 63;  // horizontal position of the crosshair, initially 63
int16_t y = 63;  // vertical position of the crosshair, initially 63
int16_t prevx,prevy;
uint8_t select;  // joystick push
uint8_t area[2];
uint32_t PseudoCount;

unsigned long NumCreated;   // number of foreground threads created
unsigned long NumSamples;   // incremented every ADC sample, in Producer
unsigned long UpdateWork;   // incremented every update on position values
unsigned long Calculation;  // incremented every cube number calculation
unsigned long DisplayCount; // incremented every time the Display thread prints on LCD 
unsigned long ConsumerCount;// incremented every time the Consumer thread prints on LCD
unsigned long Button1RespTime; // Latency for Task 2 = Time between button1 push and response on LCD 
unsigned long Button2RespTime; // Latency for Task 7 = Time between button2 push and response on LCD
unsigned long PushStart1;
unsigned long PushStart2;

//---------------------User debugging-----------------------
unsigned long DataLost;     // data sent by Producer, but not received by Consumer
long MaxJitter;             // largest time jitter between interrupts in usec
#define JITTERSIZE 64
unsigned long const JitterSize=JITTERSIZE;
unsigned long JitterHistogram[JITTERSIZE]={0,};
unsigned long TotalWithI1;
unsigned short MaxWithI1;

#define PE0  (*((volatile unsigned long *)0x40024004))
#define PE1  (*((volatile unsigned long *)0x40024008))
#define PE2  (*((volatile unsigned long *)0x40024010))
#define PE3  (*((volatile unsigned long *)0x40024020))

void PortE_Init(void){ unsigned long volatile delay;
  SYSCTL_RCGCGPIO_R |= 0x10;       // activate port E
  while((SYSCTL_RCGCGPIO_R & 0x10) == 0){}    
  GPIO_PORTE_DIR_R |= 0x0F;    // make PE3-0 output heartbeats
  GPIO_PORTE_AFSEL_R &= ~0x0F;   // disable alt funct on PE3-0
  GPIO_PORTE_DEN_R |= 0x0F;     // enable digital I/O on PE3-0
  GPIO_PORTE_PCTL_R = ~0x0000FFFF;
  GPIO_PORTE_AMSEL_R &= ~0x0F;;      // disable analog functionality on PF
}


void Device_Init(void){
	UART_Init();
	BSP_LCD_OutputInit();
	BSP_Joystick_Init();
}
//------------------Task 1--------------------------------
// background thread executed at 20 Hz
//******** Producer *************** 
int UpdatePosition(uint16_t rawx, uint16_t rawy, jsDataType* data){
	if (rawx > origin[0]){
		x = x + ((rawx - origin[0]) >> 9);
	}
	else{
		x = x - ((origin[0] - rawx) >> 9);
	}
	if (rawy < origin[1]){
		y = y + ((origin[1] - rawy) >> 9);
	}
	else{
		y = y - ((rawy - origin[1]) >> 9);
	}
	if (x > 127){
		x = 127;}
	if (x < 0){
		x = 0;}
	if (y > 112 - CROSSSIZE){
		y = 112 - CROSSSIZE;}
	if (y < 0){
		y = 0;}
	data->x = x; data->y = y;
	return 1;
}

void Producer(void){
	uint16_t rawX,rawY; // raw adc value
	uint8_t select;
	jsDataType data;
	unsigned static long LastTime;  // time at previous ADC sample
	unsigned long thisTime;         // time at current ADC sample
	long jitter;                    // time between measured and expected, in us
	if (NumSamples < RUNLENGTH){
		BSP_Joystick_Input(&rawX,&rawY,&select);
		thisTime = OS_Time();       // current time, 12.5 ns
		UpdateWork += UpdatePosition(rawX,rawY,&data); // calculation work
		NumSamples++;               // number of samples
		if(JsFifo_Put(data) == 0){ // send to consumer
			DataLost++;
		}
	//calculate jitter
		if(UpdateWork > 1){    // ignore timing of first interrupt
			unsigned long diff = OS_TimeDifference(LastTime,thisTime);
			if(diff > PERIOD){
				jitter = (diff-PERIOD+4)/8;  // in 0.1 usec
			}
			else{
				jitter = (PERIOD-diff+4)/8;  // in 0.1 usec
			}
			if(jitter > MaxJitter){
				MaxJitter = jitter; // in usec
			}       // jitter should be 0
			if(jitter >= JitterSize){
				jitter = JITTERSIZE-1;
			}
			JitterHistogram[jitter]++; 
		}
		LastTime = thisTime;
	}
}

//--------------end of Task 1-----------------------------

//------------------Task 2--------------------------------
// background thread executes with SW1 button
// one foreground task created with button push
// foreground treads run for 2 sec and die
// ***********ButtonWork*************
void ButtonWork(void){
	uint32_t StartTime,CurrentTime,ElapsedTime;
	StartTime = OS_MsTime();
	ElapsedTime = 0;
	OS_bWait(&LCDFree);
	Button1RespTime = OS_MsTime() - PushStart1;
	BSP_LCD_FillScreen(BGCOLOR);
	while (ElapsedTime < LIFETIME){
		CurrentTime = OS_MsTime();
		ElapsedTime = CurrentTime - StartTime;
		BSP_LCD_Message(0,5,0,"Life Time:",LIFETIME);
		BSP_LCD_Message(1,0,0,"Horizontal Area:",area[0]);
		BSP_LCD_Message(1,1,0,"Vertical Area:",area[1]);
		BSP_LCD_Message(1,2,0,"Elapsed Time:",ElapsedTime);
		OS_Sleep(50);
	}
	BSP_LCD_FillScreen(BGCOLOR);
	OS_bSignal(&LCDFree);
  OS_Kill();  // done, OS does not return from a Kill
} 

//************SW1Push*************
// Called when SW1 Button pushed
// Adds another foreground task
// background threads execute once and return
void SW1Push(void){
  if(OS_MsTime() > 20 ){ // debounce
    if(OS_AddThread(&ButtonWork,128,4)){
      NumCreated++; 
    }
    OS_ClearMsTime();  // at least 20ms between touches
		PushStart1 = OS_MsTime();
  }
}

//--------------end of Task 2-----------------------------

//------------------Task 3--------------------------------

//******** Consumer *************** 
// foreground thread, accepts data from producer
// Display crosshair and its positions
// inputs:  none
// outputs: none
void Consumer(void){
	while(NumSamples < RUNLENGTH){
	jsDataType data;
	JsFifo_Get(&data);
	OS_bWait(&LCDFree);
	ConsumerCount++;
	BSP_LCD_DrawFastVLine(prevx,prevy-CROSSSIZE,2*CROSSSIZE,LCD_BLACK);
	BSP_LCD_DrawFastHLine(prevx-CROSSSIZE,prevy,2*CROSSSIZE,LCD_BLACK);
	BSP_LCD_DrawFastVLine(data.x,data.y-CROSSSIZE,2*CROSSSIZE,LCD_GREEN);
	BSP_LCD_DrawFastHLine(data.x-CROSSSIZE,data.y,2*CROSSSIZE,LCD_GREEN);
	BSP_LCD_Message(1,5,0,"X: ",data.x);
	BSP_LCD_Message(1,5,10,"Y: ",data.y);
	OS_bSignal(&LCDFree);
	prevx = data.x; prevy = data.y;
	}
  OS_Kill();  // done
}


//--------------end of Task 3-----------------------------

//------------------Task 4--------------------------------
// foreground thread that runs without waiting or sleeping
// it executes some calculation related to the position of crosshair 
//******** CubeNumCalc *************** 
// foreground thread, calculates the virtual cube number for the crosshair
// never blocks, never sleeps, never dies
// inputs:  none
// outputs: none

void CubeNumCalc(void){ 
	uint16_t CurrentX,CurrentY;
  while(1) {
		if(NumSamples < RUNLENGTH){
			CurrentX = x; CurrentY = y;
			area[0] = CurrentX / 22;
			area[1] = CurrentY / 20;
			Calculation++;
		}
  }
}
//--------------end of Task 4-----------------------------

//------------------Task 5--------------------------------
// UART background ISR performs serial input/output
// Two software fifos are used to pass I/O data to foreground
// The interpreter runs as a foreground thread
// inputs:  none
// outputs: none

void Interpreter(void){
	char command[80];
  while(1){
    OutCRLF(); UART_OutString(">>");
		UART_InString(command,79);
		OutCRLF();
		if (!(strcmp(command,"NumSamples"))){
			UART_OutString("NumSamples: ");
			UART_OutUDec(NumSamples);
		}
		else if (!(strcmp(command,"NumCreated"))){
			UART_OutString("NumCreated: ");
			UART_OutUDec(NumCreated);
		}
		else if (!(strcmp(command,"MaxJitter"))){
			UART_OutString("MaxJitter: ");
			UART_OutUDec(MaxJitter);
		}
		else if (!(strcmp(command,"DataLost"))){
			UART_OutString("DataLost: ");
			UART_OutUDec(DataLost);
		}
		else if (!(strcmp(command,"UpdateWork"))){
			UART_OutString("UpdateWork: ");
			UART_OutUDec(UpdateWork);
		}
	  else if (!(strcmp(command,"Calculations"))){
			UART_OutString("Calculations: ");
			UART_OutUDec(Calculation);
		}
		else if (!(strcmp(command,"FifoSize"))){
			UART_OutString("JSFifoSize: ");
			UART_OutUDec(JSFIFOSIZE);
		}
	  else if (!(strcmp(command,"Display"))){
			UART_OutString("DisplayWork: ");
			UART_OutUDec(DisplayCount);
		}
		else if (!(strcmp(command,"Consumer"))){
			UART_OutString("ConsumerWork: ");
			UART_OutUDec(ConsumerCount);
		}
		else{
			UART_OutString("Command incorrect!");
		}
  }
}
//--------------end of Task 5-----------------------------

//------------------Task 6--------------------------------

//************ PeriodicUpdater *************** 
// background thread, do some pseudo works to test if you can add multiple periodic threads
// inputs:  none
// outputs: none
void PeriodicUpdater(void){
	PseudoCount++;
}

//************ Display *************** 
// foreground thread, do some pseudo works to test if you can add multiple periodic threads
// inputs:  none
// outputs: none
void Display(void){
	while(NumSamples < RUNLENGTH){
		OS_bWait(&LCDFree);
		DisplayCount++;
		BSP_LCD_Message(1,4,0,"PseudoCount: ",PseudoCount);
		OS_bSignal(&LCDFree);
	}
  OS_Kill();  // done
}




//--------------end of Task 6-----------------------------

//------------------Task 7--------------------------------
// background thread executes with button2
// one foreground task created with button push
// ***********ButtonWork2*************
void Restart(void){
	uint32_t StartTime,CurrentTime,ElapsedTime;
	NumSamples = RUNLENGTH; // first kill the foreground threads
	OS_Sleep(50); // wait
	StartTime = OS_MsTime();
	ElapsedTime = 0;
	OS_bWait(&LCDFree);
	Button2RespTime = OS_MsTime() - PushStart2;
	BSP_LCD_FillScreen(BGCOLOR);
	while (ElapsedTime < 500){
		CurrentTime = OS_MsTime();
		ElapsedTime = CurrentTime - StartTime;
		BSP_LCD_DrawString(5,6,"Restarting",LCD_WHITE);
	}
	BSP_LCD_FillScreen(BGCOLOR);
	OS_bSignal(&LCDFree);
	// restart
	DataLost = 0;        // lost data between producer and consumer
    NumSamples = 0;
    UpdateWork = 0;
	MaxJitter = 0;       // in 1us units
	PseudoCount = 0;
	x = 63; y = 63;
	NumCreated += OS_AddThread(&Consumer,128,1); 
	NumCreated += OS_AddThread(&Display,128,3);
  OS_Kill();  // done, OS does not return from a Kill
} 

//************SW1Push*************
// Called when Button2 pushed
// Adds another foreground task
// background threads execute once and return
void SW2Push(void){
  if(OS_MsTime() > 20 ){ // debounce
    if(OS_AddThread(&Restart,128,4)){
      NumCreated++; 
    }
    OS_ClearMsTime();  // at least 20ms between touches
		PushStart2 = OS_MsTime();
  }
}

//--------------end of Task 7-----------------------------




void CrossHair_Init(void){
	BSP_LCD_FillScreen(BGCOLOR);
	BSP_Joystick_Input(&origin[0],&origin[1],&select);
}

//*******************final user main DEMONTRATE THIS TO TA**********
int main(void){ 
  OS_Init();           // initialize, disable interrupts
	Device_Init();
  CrossHair_Init();
  DataLost = 0;        // lost data between producer and consumer
  NumSamples = 0;
  MaxJitter = 0;       // in 1us units
	PseudoCount = 0;

//********initialize communication channels
  JsFifo_Init();

//*******attach background tasks***********
  OS_AddSW1Task(&SW1Push,4);
	OS_AddSW2Task(&SW2Push,4);
  OS_AddPeriodicThread(&Producer,PERIOD,3); // 2 kHz real time sampling of PD3
	OS_AddPeriodicThread(&PeriodicUpdater,PSEUDOPERIOD,3);
	
  NumCreated = 0 ;
// create initial foreground threads
  NumCreated += OS_AddThread(&Interpreter,128,2); 
  NumCreated += OS_AddThread(&Consumer,128,1); 
	NumCreated += OS_AddThread(&CubeNumCalc,128,3); 
	NumCreated += OS_AddThread(&Display,128,3);
 
  OS_Launch(TIME_2MS); // doesn't return, interrupts enabled in here
	return 0;            // this never executes
}