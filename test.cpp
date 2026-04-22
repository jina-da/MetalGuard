//#define _CRT_SECURE_NO_WARNINGS
//#include <stdio.h>
//#include <tchar.h>
//#include "SerialClass.h"	// Library described above
//#include <string>
//#include <thread>
//#include <chrono>
//
//Serial* SP = new Serial("\\\\.\\COM3");    // adjust as needed
//
//// 하트비트 전송 스레드 함수
//void heartbeatThread() {
//	while (true) {
//		SP->WriteData("H\n", strlen("H\n"));
//		std::this_thread::sleep_for(std::chrono::seconds(5)); // 5초 간격
//	}
//}
//
//// application reads from the specified serial port and reports the collected data
//int main()
//{
//	printf("Welcome to the serial test app!\n\n");
//
//	//Serial* SP = new Serial("\\\\.\\COM3");    // adjust as needed
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
//	// 2. 하트비트 스레드 시작
//	std::thread hb(heartbeatThread);
//	hb.detach(); // 백그라운드에서 실행
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
