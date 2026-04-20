//#define _CRT_SECURE_NO_WARNINGS
//#include <stdio.h>
//#include <tchar.h>
//#include "SerialClass.h"	// Library described above
//#include <string>
//
//// application reads from the specified serial port and reports the collected data
//int main()
//{
//	printf("Welcome to the serial test app!\n\n");
//
//	Serial* SP = new Serial("\\\\.\\COM3");    // adjust as needed
//
//	if (SP->IsConnected())
//		printf("We're connected\n\n");
//
//	char incomingData[256] = "";			// don't forget to pre-allocate memory
//	//printf("%s\n",incomingData);
//	int dataLength = 255;
//	int readResult = 0;
//
//	int index = 0;
//	int data = 0;
//
//
//	while (SP->IsConnected())
//	{
//		char cmd[255];
//		scanf("%s", &cmd);
//		SP->WriteData(cmd, strlen(cmd));
//	}
//	return 0;
//}
