#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <tchar.h>
#include <iostream>
#include <vector>
#include <string>
#include <winsock2.h>
#include "SerialClass.h"

#pragma comment(lib, "ws2_32.lib")

// 프로토콜 정의에 따른 구조체 정렬 (1바이트 단위)
#pragma pack(push, 1)
struct PacketHeader {
    uint16_t signature; // 0x4D47
    uint16_t cmdId;     // 명령어 ID
    uint32_t bodySize;  // JSON 바디 크기
};
#pragma pack(pop)

#define PORT 8000
#define IP "10.10.10.109"

// 명령어 ID 상수 정의
enum CmdID : uint16_t {
    VERDICT_PASS = 201,
    VERDICT_FAIL = 202,
    VERDICT_UNCERTAIN = 203,
    VERDICT_TIMEOUT = 204
};

int main() {
    // 1. 시리얼 포트 초기화 (아두이노 연결)
    Serial* SP = new Serial("\\\\.\\COM3");
    if (!SP->IsConnected()) {
        std::cerr << "Serial port connection failed." << std::endl;
        return 1;
    }
    std::cout << "Arduino connected on COM3." << std::endl;

    // 2. 윈도우 소켓 초기화
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed." << std::endl;
        return 1;
    }

    SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (clientSocket == INVALID_SOCKET) {
        std::cerr << "Socket creation failed." << std::endl;
        WSACleanup();
        return 1;
    }

    // 3. 운영 서버 접속 (10.10.10.109:8000)
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(8000);
    serverAddr.sin_addr.s_addr = inet_addr("10.10.10.109");

    if (connect(clientSocket, (SOCKADDR*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "Server connection failed." << std::endl;
        closesocket(clientSocket);
        WSACleanup();
        return 1;
    }
    std::cout << "Connected to MetalGuard Server." << std::endl;

    // 4. 패킷 수신 및 루프
    while (true) {
        PacketHeader header;
        // 헤더 수신 (8바이트)
        int recvLen = recv(clientSocket, (char*)&header, sizeof(PacketHeader), MSG_WAITALL);

        if (recvLen <= 0) {
            std::cout << "Connection closed by server." << std::endl;
            break;
        }

        // 엔디안 변환 (Network Byte Order to Host Byte Order)
        header.signature = ntohs(header.signature);
        header.cmdId = ntohs(header.cmdId);
        header.bodySize = ntohl(header.bodySize);

        // Signature 검증 (0x4D47)
        if (header.signature != 0x4D47) continue;

        // JSON 바디 수신 (필요 시 처리, 본 코드에서는 아두이노 신호 생성에 집중)
        if (header.bodySize > 0) {
            std::vector<char> buffer(header.bodySize);
            recv(clientSocket, buffer.data(), header.bodySize, MSG_WAITALL);
        }

        // 5. 판정 결과에 따른 아두이노 시리얼 송신
        std::string signal = "";
        switch (header.cmdId) {
        case VERDICT_PASS:
            signal = "P\n";
            break;
        case VERDICT_FAIL:
            signal = "F\n";
            break;
        case VERDICT_UNCERTAIN:
            signal = "U\n";
            break;
        case VERDICT_TIMEOUT:
            signal = "T\n";
            break;
        default:
            continue; // 기타 패킷은 무시
        }

        if (!signal.empty()) {
            SP->WriteData((char*)signal.c_str(), (int)signal.length());
            std::cout << "Signal sent to Arduino: " << signal;
        }
    }

    // 자원 해제
    closesocket(clientSocket);
    WSACleanup();
    delete SP;

    return 0;
}