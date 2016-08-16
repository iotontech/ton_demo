// Includes --------------------------------------------------------------------
#include "main.h"

// Defines ---------------------------------------------------------------------
#define MY_SSID		"mySSID"	// Enter router ssid inside the quotes
#define MY_PASS		"myPASS"	// Enter router password inside the quotes
#define MY_API_KEY	"myAPIKEY"	// Enter ThingSpeak API KEY of channel


// Private variables -----------------------------------------------------------
PwmOut pwm(PIN13);				// PWM output
DigitalOut out(PIN14);			// Digital output
AnalogIn ain(PIN15);			// Analog input
DigitalIn in(PIN16);			// Digital input

InterruptIn button(SW_USER);	// USER button - interrupt mode
int userCount = 0;				// USER button counter

char bufferRxBluetooth[20];		// Buffer of Bluetooth commands
volatile uint8_t bufferCountRxBluetooth = 0;	// Index counter - Blutooth buffer
volatile bool flagCmdBluetooth = false;	// Indicates new Bluetooth command received

char* replyHttp;				// Buffer of reply http get method
uint8_t countThingSpeak = 0;	// Auxiliar counter - ThingSpeak

Ticker imuTick;					// IMU Tick Timer


// *****************************************************************************
// Private functions ***********************************************************
// *****************************************************************************
// Runs the AHRS algorithm - Rate: 0.1 s - 10 Hz -------------------------------
// AHRS provide: roll, pitch and yaw
void imuCallback(void)
{
	imu.runAHRS(0.1);
}	// end of imuCallback


// Called everytime a new character goes into the RX buffer --------------------
void bluetoothRxCallback(void)
{
	bufferRxBluetooth[bufferCountRxBluetooth] = bluetooth.getc();
	bufferCountRxBluetooth++;

	if (bluetooth.readable() == 0) flagCmdBluetooth = true;
}	// end of bluetoothRxCallback


// USER button interrupt callback ----------------------------------------------
void userCallback(void)
{
	userCount++;
}	// end of userCallback


// Parse the Bluetooth commands ------------------------------------------------
// COMMAND          |   DESCRIPTION
// -----------------------------------------------------------------------------
// #PWM:float       | Update pwm output. Ex: #PWM:0.5
// #OUT:int         | Update digital output. Ex: #OUT:1
// #RGB:rgbcode     | Update LED RGB color. Ex: #RGB:00AFEF
// #AIN?            | Return analog input
// #DIN?            | Return digital input
// #BAT?            | Return battery voltage
// #IMU?            | Return imu data (roll, pitch and yaw)
// #ALL?            | Return all values
void parseCmdBluetooth(char* str)
{
	if (strncmp("#PWM:", str, 5) == 0)
	{
		pwm = strtof(str + 5, NULL);
	}
	else if (strncmp("#OUT:", str, 5) == 0)
	{
		out = atoi(str + 5);
	}
	else if (strncmp("#RGB:", str, 5) == 0)
	{
		ton.setLED(str + 5);
	}
	else if (strncmp("#AIN?", str, 5) == 0)
	{
		bluetooth.printf("#AIN:%0.2f\r\n", ain.read());
	}
	else if (strncmp("#DIN?", str, 5) == 0)
	{
		bluetooth.printf("#DIN:%d\r\n", in.read());
	}
	else if (strncmp("#BAT?", str, 5) == 0)
	{
		bluetooth.printf("#BAT:%0.2fV\r\n", ton.getBattery());
	}
	else if (strncmp("#IMU?", str, 5) == 0)
	{
		bluetooth.printf("pitch: %0.2f\r\nroll: %0.2f\r\nyaw: %0.2f\r\n",
			imu.getPitch(), imu.getRoll(), imu.getYaw());
	}
	else if (strncmp("#ALL?", str, 5) == 0)
	{
		bluetooth.printf("#AIN:%0.2f\r\n#DIN:%d\r\n#BAT:%0.2fV\r\n",
			ain.read(), in.read(), ton.getBattery());

		bluetooth.printf("pitch: %0.2f\r\nroll: %0.2f\r\nyaw: %0.2f\r\n",
			imu.getPitch(), imu.getRoll(), imu.getYaw());
	}
	else
	{
		bluetooth.printf("Invalid command!\r\n");
	}
}	// end of parseCmdBluetooth function


// Data format: #rgbcode,float,int	| Ex: #00AFEF,0.5,1 ------------------------
void parseCmdWifi(char* str)
{
	char* tmp = strstr(str, "#");	// Search the first character of the command

	if (tmp != NULL)
	{
		tmp = strtok(tmp, ",\n");
		ton.setLED(tmp + 1);		// Remove the '#' and update LED RGB color

		tmp = strtok(NULL,",\n");
		pwm = strtof(tmp, NULL);	// Convert to float and update pwm output

		tmp = strtok(NULL,",\n");
		out = atoi(tmp);			// Convert to int and update digital output
	}
}	// end of parseCmdWifi function


// *****************************************************************************
// MAIN PROGRAM ****************************************************************
// *****************************************************************************
int main(void)
{
	// Initializations
	ton.setLED(RED);
	ton.enableBluetooth();
	ton.enableWifi();
	ton.enableIMU();    // Leave standing the TON board at initialization (~5s)
	ton.setLED(BLUE);

	// Configure Tick Timer for IMU reads - interval (0.1 second)
	imuTick.attach(&imuCallback, 0.1);

	// Configure RX interrupt of Bluetooth
	bluetooth.attach(&bluetoothRxCallback, Serial::RxIrq);

	// Try to connect to the access point
	if (wifi.connect(MY_SSID, MY_PASS) == true) ton.setLED(GREEN);
	else ton.setLED(YELLOW);

	// Configure USER button interrupt
	button.rise(&userCallback);

	// The main LOOP -----------------------------------------------------------
	while (1)
	{
		// Prints messages to the USB ------------------------------------------
		usb.printf("pitch: %0.3f\r\n", imu.getPitch());
		usb.printf("roll: %0.3f\r\n", imu.getRoll());
		usb.printf("yaw: %0.3f\r\n", imu.getYaw());
		usb.printf("---\r\n");
		usb.printf("battery: %0.3fV\r\n", ton.getBattery());
		usb.printf("count: %d\r\n", userCount);
		usb.printf("\r\n");

		// Checks new Blutooth commands ----------------------------------------
		if (flagCmdBluetooth == true)
		{
			// Parse the Bluetooth commands
			parseCmdBluetooth(bufferRxBluetooth);

			bufferCountRxBluetooth = 0;
			memset(bufferRxBluetooth, 0, sizeof(bufferRxBluetooth));
			flagCmdBluetooth = false;
		}	// end of check new Bluetooth commands

		// Checks if wifi is connected and run wifi tasks ----------------------
		if (wifi.isConnected() == true)
		{
			// Parse the http get method
			replyHttp = wifi.httpGet("ioton.cc", "/ton-demo.txt");
			parseCmdWifi(replyHttp);

			// Send data via ThingSpeak (does not support high rates)
			if (++countThingSpeak == 5)
			{
				char thingspeak[50];

				sprintf(thingspeak, "field1=%0.3f&field2=%d&field3=%0.3f",
					ain.read(), in.read(), ton.getBattery());

				usb.printf("Send to ThingSpeak: %s\r\n", thingspeak);
				wifi.sendThingSpeak(thingspeak, MY_API_KEY);

				countThingSpeak = 0;
			}	// end of send to ThingSpeak
		}	// end of wifi tasks

		wait(1);
	}   // end of main LOOP
}   // end of main function
