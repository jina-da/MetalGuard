#pragma comment(lib, "ws2_32.lib")
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iostream>
#include <cstdint>
#include <string>
#include <thread>
#include <atomic>

#pragma pack(push, 1)
struct PacketHeader {
    uint16_t signature;
    uint16_t cmdId;
    uint32_t bodySize;
};
#pragma pack(pop)

enum CmdID : uint16_t {
    VERDICT_PASS = 201,
    VERDICT_FAIL = 202,
    VERDICT_UNCERTAIN = 203,
    VERDICT_TIMEOUT = 204,
    DONE_PASS = 205,
    DONE_FAIL = 206,
    DONE_UNCERTAIN = 207,
    DONE_TIMEOUT = 208,
    PING = 501,
    PONG = 502,
};

const char* SERVER_IP = "10.10.10.109";
const int   SERVER_PORT = 8000;
const char* ARDUINO_PORT = "\\\\.\\COM3";

// 아두이노 완료 신호 → CmdID 매핑 테이블
// 신호 문자열이 바뀌면 여기만 수정
struct DoneSignalMap {
    const char* signal;
    CmdID       cmdId;
};
const DoneSignalMap DONE_SIGNAL_TABLE[] = {
    { "P_done", DONE_PASS      },  // 205
    { "F_done", DONE_FAIL      },  // 206
    { "U_done", DONE_UNCERTAIN },  // 207
    { "T_done", DONE_TIMEOUT   },  // 208
};
const int DONE_SIGNAL_COUNT = 4;

std::atomic<bool> g_running(true);

// 유틸
bool recvExact(SOCKET sock, char* buf, int size) {
    int received = 0;
    while (received < size) {
        int ret = recv(sock, buf + received, size - received, 0);
        if (ret <= 0) return false;
        received += ret;
    }
    return true;
}

// 운용 서버(Linux)는 big-endian, Windows는 little-endian
uint16_t swap16(uint16_t val) { return (val >> 8) | (val << 8); }
uint32_t swap32(uint32_t val) {
    return ((val >> 24) & 0xFF) |
        ((val >> 8) & 0xFF00) |
        ((val << 8) & 0xFF0000) |
        ((val << 24) & 0xFF000000);
}

// 송신
bool sendPacket(SOCKET sock, uint16_t cmdId, const std::string& body = "") {
    PacketHeader header{};
    header.signature = swap16(0x4D47);
    header.cmdId = swap16(cmdId);
    header.bodySize = swap32((uint32_t)body.size());

    if (send(sock, (char*)&header, sizeof(header), 0) == SOCKET_ERROR) {
        std::cerr << "[송신 실패] 헤더 오류: " << WSAGetLastError() << std::endl;
        return false;
    }
    if (!body.empty()) {
        if (send(sock, body.c_str(), (int)body.size(), 0) == SOCKET_ERROR) {
            std::cerr << "[송신 실패] 바디 오류: " << WSAGetLastError() << std::endl;
            return false;
        }
    }
    return true;
}

// 아두이노
HANDLE openArduino(const char* port) {
    HANDLE h = CreateFileA(
        port, GENERIC_READ | GENERIC_WRITE, 0, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL
    );
    if (h == INVALID_HANDLE_VALUE) return h;

    DCB dcb{};
    dcb.DCBlength = sizeof(dcb);
    GetCommState(h, &dcb);
    dcb.BaudRate = CBR_9600;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    SetCommState(h, &dcb);

    COMMTIMEOUTS timeouts{};
    timeouts.ReadIntervalTimeout = 100;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = 100;
    SetCommTimeouts(h, &timeouts);

    return h;
}

void sendToArduino(HANDLE arduino, const std::string& signal) {
    DWORD written;
    WriteFile(arduino, signal.c_str(), (DWORD)signal.size(), &written, NULL);
    std::cout << "  → 아두이노 전송: " << signal;
}

// 아두이노 수신 루프
// 아두이노로부터 완료 신호 수신 → 매핑 테이블로 CmdID 찾아 서버에 전송
void arduinoRecvLoop(HANDLE arduino, SOCKET sock) {
    std::string buffer;

    while (g_running) {
        char ch = '\0';
        DWORD bytesRead = 0;

        BOOL ok = ReadFile(arduino, &ch, 1, &bytesRead, NULL);
        if (!ok || bytesRead == 0) continue;

        buffer += ch;

        if (ch == '\n') {
            // 개행 문자 제거 후 비교
            std::string trimmed = buffer;
            while (!trimmed.empty() &&
                (trimmed.back() == '\n' || trimmed.back() == '\r'))
                trimmed.pop_back();

            std::cout << "[아두이노 수신] " << trimmed << std::endl;

            // 매핑 테이블에서 CmdID 검색
            bool matched = false;
            for (int i = 0; i < DONE_SIGNAL_COUNT; i++) {
                if (trimmed == DONE_SIGNAL_TABLE[i].signal) {
                    CmdID cmdId = DONE_SIGNAL_TABLE[i].cmdId;
                    std::cout << "[서버 전송] CmdID " << cmdId << std::endl;
                    sendPacket(sock, cmdId);
                    matched = true;
                    break;
                }
            }
            if (!matched)
                std::cout << "[아두이노 수신] 알 수 없는 신호: " << trimmed << std::endl;

            buffer.clear();
        }
    }
}

// 서버 수신 루프
void serverRecvLoop(SOCKET sock, HANDLE arduino) {
    while (g_running) {
        PacketHeader header{};
        if (!recvExact(sock, (char*)&header, sizeof(header))) {
            if (g_running)
                std::cerr << "[서버 수신] 연결 끊김" << std::endl;
            g_running = false;
            break;
        }

        uint16_t sig = swap16(header.signature);
        uint16_t cmdId = swap16(header.cmdId);
        uint32_t bodySize = swap32(header.bodySize);

        if (sig != 0x4D47) {
            std::cerr << "[서버 수신] 잘못된 signature: 0x"
                << std::hex << sig << std::dec << std::endl;
            g_running = false;
            break;
        }

        std::string body;
        if (bodySize > 0) {
            body.resize(bodySize);
            recvExact(sock, body.data(), (int)bodySize);
        }

        switch (cmdId) {
        case VERDICT_PASS:
            std::cout << "[서버 수신] VERDICT_PASS (201) → ";
            sendToArduino(arduino, "P\n");
            break;
        case VERDICT_FAIL:
            std::cout << "[서버 수신] VERDICT_FAIL (202) → ";
            sendToArduino(arduino, "F\n");
            break;
        case VERDICT_UNCERTAIN:
            std::cout << "[서버 수신] VERDICT_UNCERTAIN (203) → ";
            sendToArduino(arduino, "U\n");
            break;
        case VERDICT_TIMEOUT:
            std::cout << "[서버 수신] VERDICT_TIMEOUT (204) → ";
            sendToArduino(arduino, "T\n");
            break;
        case PONG:
            std::cout << "[서버 수신] PONG (502)" << std::endl;
            break;
        default:
            std::cout << "[서버 수신] UNKNOWN cmdId: " << cmdId << std::endl;
            break;
        }
    }
}

// 하트비트 전송 스레드 함수
void heartbeatThread(HANDLE arduino) {
    while (true) {
        sendToArduino(arduino, "H\n"); // 아두이노의 lastHeartbeat 갱신용
        std::this_thread::sleep_for(std::chrono::seconds(5)); // 5초 간격
    }
}

// main
int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "MetalGuard 아두이노 브리지 시작" << std::endl;
    std::cout << "========================================" << std::endl;

    HANDLE arduino = openArduino(ARDUINO_PORT);
    if (arduino == INVALID_HANDLE_VALUE) {
        std::cerr << "아두이노 연결 실패: " << ARDUINO_PORT << std::endl;
        return 1;
    }
    std::cout << "아두이노 연결 성공!" << std::endl;

    // 하트비트 스레드 시작
    std::thread hb(heartbeatThread, arduino);
    hb.detach(); // 백그라운드에서 실행

    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

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

    sendPacket(sock, PING);
    std::cout << "PING 전송 완료" << std::endl;
    std::cout << "대기 중... (Ctrl+C로 종료)" << std::endl;
    std::cout << "----------------------------------------" << std::endl;

    std::thread serverRecvThread(serverRecvLoop, sock, arduino);
    std::thread arduinoRecvThread(arduinoRecvLoop, arduino, sock);

    serverRecvThread.join();
    arduinoRecvThread.join();

    CloseHandle(arduino);
    closesocket(sock);
    WSACleanup();
    std::cout << "종료" << std::endl;
    return 0;
}