#pragma comment(lib, "ws2_32.lib")
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iostream>
#include <cstdint>
#include <string>

// ── 패킷 헤더 구조 (8바이트 고정) ──────────────────
#pragma pack(push, 1)
struct PacketHeader {
    uint16_t signature;  // 0x4D47
    uint16_t cmdId;
    uint32_t bodySize;
};
#pragma pack(pop)

// ── CmdID 정의 ──────────────────────────────────────
enum CmdID : uint16_t {
    VERDICT_PASS = 201,
    VERDICT_FAIL = 202,
    VERDICT_UNCERTAIN = 203,
    VERDICT_TIMEOUT = 204,
    PING = 501,
    PONG = 502,
};

// ── 운용 서버 설정 ───────────────────────────────────
const char* SERVER_IP = "10.10.10.109";
const int   SERVER_PORT = 8000;

// ── 아두이노 포트 설정 ───────────────────────────────
const char* ARDUINO_PORT = "\\\\.\\COM3";  // 예: COM3 → "\\\\.\\COM3"

// ── 유틸: 정확히 size 바이트 수신 ───────────────────
// TCP는 데이터가 쪼개져서 올 수 있어서 루프로 보장
bool recvExact(SOCKET sock, char* buf, int size) {
    int received = 0;
    while (received < size) {
        int ret = recv(sock, buf + received, size - received, 0);
        if (ret <= 0) return false;
        received += ret;
    }
    return true;
}

// ── 유틸: big-endian → little-endian 변환 ───────────
// 운용 서버(Linux)는 big-endian, Windows는 little-endian
uint16_t swap16(uint16_t val) { return (val >> 8) | (val << 8); }
uint32_t swap32(uint32_t val) {
    return ((val >> 24) & 0xFF) | ((val >> 8) & 0xFF00) |
        ((val << 8) & 0xFF0000) | ((val << 24) & 0xFF000000);
}

// ── 아두이노 시리얼 포트 열기 ────────────────────────
HANDLE openArduino(const char* port) {
    HANDLE h = CreateFileA(
        port, GENERIC_WRITE, 0, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL
    );
    if (h == INVALID_HANDLE_VALUE) return h;

    // 시리얼 설정 (9600bps, 8N1)
    DCB dcb{};
    dcb.DCBlength = sizeof(dcb);
    GetCommState(h, &dcb);
    dcb.BaudRate = CBR_9600;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    SetCommState(h, &dcb);
    return h;
}

// ── 아두이노로 시리얼 전송 ───────────────────────────
void sendToArduino(HANDLE arduino, const std::string& signal) {
    DWORD written;
    WriteFile(arduino, signal.c_str(), (DWORD)signal.size(), &written, NULL);
    std::cout << "  → 아두이노 전송: " << signal;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "MetalGuard 아두이노 브리지 시작" << std::endl;
    std::cout << "========================================" << std::endl;

    // ── 1. 아두이노 시리얼 연결 ──────────────────────
    HANDLE arduino = openArduino(ARDUINO_PORT);
    if (arduino == INVALID_HANDLE_VALUE) {
        std::cerr << "아두이노 연결 실패 (포트 확인 필요: " << ARDUINO_PORT << ")" << std::endl;
        return 1;
    }
    std::cout << "아두이노 연결 성공!" << std::endl;

    // ── 2. Winsock 초기화 ────────────────────────────
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    // ── 3. 운용 서버 TCP 연결 ────────────────────────
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &serverAddr.sin_addr);

    if (connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "운용 서버 연결 실패: " << WSAGetLastError() << std::endl;
        CloseHandle(arduino);
        WSACleanup();
        return 1;
    }
    std::cout << "운용 서버 연결 성공! (" << SERVER_IP << ":" << SERVER_PORT << ")" << std::endl;

    // ── 4. PING 전송 → 아두이노 클라이언트로 등록 ───
    // 반드시 PING을 먼저 보내야 운용 서버가 아두이노 클라이언트로 인식함
    PacketHeader ping{};
    ping.signature = swap16(0x4D47);
    ping.cmdId = swap16(PING);
    ping.bodySize = 0;
    send(sock, (char*)&ping, sizeof(ping), 0);
    std::cout << "PING 전송 완료 → 운용 서버에 아두이노 클라이언트로 등록" << std::endl;
    std::cout << "대기 중... (Ctrl+C로 종료)" << std::endl;
    std::cout << "----------------------------------------" << std::endl;

    // ── 5. 수신 루프 ─────────────────────────────────
    while (true) {
        // 헤더 수신 (8바이트 고정)
        PacketHeader header{};
        if (!recvExact(sock, (char*)&header, sizeof(header))) {
            std::cerr << "연결 끊김" << std::endl;
            break;
        }

        // big-endian → little-endian 변환
        uint16_t sig = swap16(header.signature);
        uint16_t cmdId = swap16(header.cmdId);
        uint32_t bodySize = swap32(header.bodySize);

        // signature 검증
        if (sig != 0x4D47) {
            std::cerr << "잘못된 signature: 0x" << std::hex << sig << std::endl;
            break;
        }

        // body 수신 (있으면 읽고 버림 — 판정 패킷은 body 없음)
        if (bodySize > 0) {
            std::string body(bodySize, '\0');
            recvExact(sock, body.data(), (int)bodySize);
        }

        // cmdId별 아두이노 시리얼 전송
        switch (cmdId) {
        case VERDICT_PASS:
            std::cout << "[PASS]      ";
            sendToArduino(arduino, "P\n");
            break;
        case VERDICT_FAIL:
            std::cout << "[FAIL]      ";
            sendToArduino(arduino, "F\n");
            break;
        case VERDICT_UNCERTAIN:
            std::cout << "[UNCERTAIN] ";
            sendToArduino(arduino, "U\n");
            break;
        case VERDICT_TIMEOUT:
            std::cout << "[TIMEOUT]   ";
            sendToArduino(arduino, "T\n");
            break;
        case PONG:
            // 운용 서버가 PING에 응답한 것 (무시)
            std::cout << "[PONG] 운용 서버 응답 확인" << std::endl;
            break;
        default:
            std::cout << "[UNKNOWN] cmdId: " << cmdId << std::endl;
            break;
        }
    }

    // ── 6. 종료 처리 ─────────────────────────────────
    CloseHandle(arduino);
    closesocket(sock);
    WSACleanup();
    std::cout << "종료" << std::endl;
    return 0;
}