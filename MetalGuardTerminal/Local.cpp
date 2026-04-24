#pragma comment(lib, "ws2_32.lib")
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iostream>
#include <cstdint>
#include <string>
#include <thread>
#include <atomic>
#include <ctime>

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

// 양방향 신호 매핑 테이블
// 신호 문자열이 바뀌면 여기만 수정
struct SignalMap {
    CmdID       cmdId;
    const char* toArduino;   // 서버 → 아두이노 신호 (없으면 nullptr)
    const char* fromArduino; // 아두이노 → 서버 신호 (없으면 nullptr)
};
const SignalMap SIGNAL_TABLE[] = {
    { VERDICT_PASS,      "P\n",   nullptr  },  // 201
    { VERDICT_FAIL,      "F\n",   nullptr  },  // 202
    { VERDICT_UNCERTAIN, "U\n",   nullptr  },  // 203
    { VERDICT_TIMEOUT,   "T\n",   nullptr  },  // 204
    { DONE_PASS,         nullptr, "P_done" },  // 205
    { DONE_FAIL,         nullptr, "F_done" },  // 206
    { DONE_UNCERTAIN,    nullptr, "U_done" },  // 207
    { DONE_TIMEOUT,      nullptr, "T_done" },  // 208
};
const int SIGNAL_TABLE_COUNT = 8;

const char* cmdName(CmdID id) {
    switch (id) {
    case VERDICT_PASS:      return "VERDICT_PASS";
    case VERDICT_FAIL:      return "VERDICT_FAIL";
    case VERDICT_UNCERTAIN: return "VERDICT_UNCERTAIN";
    case VERDICT_TIMEOUT:   return "VERDICT_TIMEOUT";
    case DONE_PASS:         return "DONE_PASS";
    case DONE_FAIL:         return "DONE_FAIL";
    case DONE_UNCERTAIN:    return "DONE_UNCERTAIN";
    case DONE_TIMEOUT:      return "DONE_TIMEOUT";
    case PING:              return "PING";
    case PONG:              return "PONG";
    default:                return "UNKNOWN";
    }
}

std::atomic<bool> g_running(true);
std::atomic<bool> g_serverConnected(false); // 구분선 출력 이후 heartbeat 허용
std::atomic<time_t> g_lastPongTime(0); // 마지막 PONG 수신 시각 (0 = 아직 미수신)

const int PONG_TIMEOUT_SEC = 8; // 이 시간 안에 PONG 없으면 아두이노 연결 끊김으로 판단
const int RECONNECT_DELAY_SEC = 3;  // 재접속 시도 간격(초)

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

void sendToArduino(HANDLE arduino, CmdID cmdId, const std::string& signal) {
    if (arduino == NULL || arduino == INVALID_HANDLE_VALUE) return; // 핸들 유효성 검사
    DWORD written;
    WriteFile(arduino, signal.c_str(), (DWORD)signal.size(), &written, NULL);
    std::cout << "◁◁◁ [아두이노 전송] CmdID " << cmdId << " / " << cmdName(cmdId) << std::endl;
}

// 아두이노 수신 루프
// 아두이노로부터 완료 신호 수신 → 매핑 테이블로 CmdID 찾아 서버에 전송
void arduinoRecvLoop(HANDLE& arduino, SOCKET sock) {
    std::string buffer;

    while (g_running) {
        // 핸들이 유효해질 때까지 대기 (heartbeatThread가 재접속 처리)
        if (arduino == NULL || arduino == INVALID_HANDLE_VALUE) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            buffer.clear();
            continue;
        }

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

            // 매핑 테이블에서 CmdID 검색
            bool matched = false;
            for (int i = 0; i < SIGNAL_TABLE_COUNT; i++) {
                if (SIGNAL_TABLE[i].fromArduino != nullptr &&
                    trimmed == SIGNAL_TABLE[i].fromArduino) {
                    CmdID cmdId = SIGNAL_TABLE[i].cmdId;
                    std::cout << "▶▶▶ [아두이노 수신] CmdID " << cmdId << " / " << cmdName(cmdId) << std::endl;
                    std::cout << "◁◁◁ [서버 전송] CmdID " << cmdId << " / " << cmdName(cmdId) << std::endl;
                    sendPacket(sock, cmdId);
                    matched = true;
                    break;
                }
            }

            // PONG 수신
            if (!matched && trimmed == "PONG") {
                g_lastPongTime = time(nullptr);
                std::cout << "▶▶▶ [아두이노 수신] CmdID " << PONG << " / " << cmdName(PONG) << std::endl;
                matched = true;
            }

            if (!matched)
                std::cout << "▶▶▶ [아두이노 수신] 알 수 없는 신호: " << trimmed << std::endl;

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
                std::cerr << "▶▶▶ [서버 수신] 연결 끊김" << std::endl;
            g_running = false;
            break;
        }

        uint16_t sig = swap16(header.signature);
        CmdID cmdId = static_cast<CmdID>(swap16(header.cmdId));
        uint32_t bodySize = swap32(header.bodySize);

        if (sig != 0x4D47) {
            std::cerr << "▶▶▶ [서버 수신] 잘못된 signature: 0x"
                << std::hex << sig << std::dec << std::endl;
            g_running = false;
            break;
        }

        std::string body;
        if (bodySize > 0) {
            body.resize(bodySize);
            recvExact(sock, body.data(), (int)bodySize);
        }

        // 매핑 테이블에서 CmdID 검색
        bool matched = false;
        for (int i = 0; i < SIGNAL_TABLE_COUNT; i++) {
            if (SIGNAL_TABLE[i].cmdId == cmdId &&
                SIGNAL_TABLE[i].toArduino != nullptr) {
                std::cout << "▶▶▶ [서버 수신] CmdID " << cmdId << " / " << cmdName(cmdId) << std::endl;
                sendToArduino(arduino, cmdId, SIGNAL_TABLE[i].toArduino);
                matched = true;
                break;
            }
        }
        if (!matched) {
            if (cmdId == PONG)
                std::cout << "▶▶▶ [서버 수신] CmdID " << cmdId << " / " << cmdName(cmdId) << std::endl;
        }
    }
}

// 하트비트 전송 스레드 함수 + PONG 타임아웃 감지 스레드
// H 전송 → 아두이노가 PONG 응답 → g_lastPongTime 갱신
// PONG_TIMEOUT_SEC 안에 응답 없으면 아두이노 재접속 시도
void heartbeatThread(HANDLE& arduino){    
    // 서버 연결 + 구분선 출력 완료까지 대기
    while (!g_serverConnected && g_running)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

    while (g_running) {
        // 서버 미연결 상태면 대기 (재연결 후 재개)
        if (!g_serverConnected) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // 서버 연결 직후 타임아웃 기준 리셋 (대기 시간 누적 방지)
        g_lastPongTime = time(nullptr);

        if (arduino != NULL && arduino != INVALID_HANDLE_VALUE)
            sendToArduino(arduino, PING, "H\n");        // 아두이노 핸들이 유효하면 H 전송

        std::this_thread::sleep_for(std::chrono::seconds(5));   // sleep 후 PONG 도착 시간 확보

        // PONG 타임아웃 체크
        time_t elapsed = time(nullptr) - g_lastPongTime.load();
        if (elapsed >= PONG_TIMEOUT_SEC) {
            std::cerr << "[아두이노 연결] 연결 끊김. 아두이노 재연결 시도" << std::endl;

            if (arduino != NULL && arduino != INVALID_HANDLE_VALUE) {            // 기존 핸들 닫기
                CloseHandle(arduino);
                arduino = INVALID_HANDLE_VALUE;
            }

            // 재접속 루프
            while (g_running) {
                std::this_thread::sleep_for(std::chrono::seconds(RECONNECT_DELAY_SEC));
                HANDLE newHandle = openArduino(ARDUINO_PORT);
                if (newHandle != INVALID_HANDLE_VALUE) {
                    arduino = newHandle;
                    g_lastPongTime = time(nullptr); // 타임아웃 리셋
                    std::cout << "[아두이노 연결] 연결 성공!" << std::endl;
                    break;
                }
                std::cerr << "[아두이노 연결] 연결 실패, " << RECONNECT_DELAY_SEC << "초 후 재시도..." << std::endl;
            }            
        }
    }
}

// main
int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "MetalGuard 아두이노 브리지 시작" << std::endl;
    std::cout << "========================================" << std::endl;

    // 아두이노 연결 루프 - 연결될 때까지 재시도
    HANDLE arduino = INVALID_HANDLE_VALUE;
    while (arduino == INVALID_HANDLE_VALUE) {
        arduino = openArduino(ARDUINO_PORT);
        if (arduino == INVALID_HANDLE_VALUE) {
            std::cerr << "[아두이노 연결] 연결 실패 → " << RECONNECT_DELAY_SEC << "초 후 재시도..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(RECONNECT_DELAY_SEC));
        }
    }
    std::cout << "[아두이노 연결] 아두이노 연결 성공!" << std::endl;

    // 하트비트 스레드 시작 (arduino 핸들을 참조로 전달 - 재접속 시 핸들 교체 필요)
    std::thread hb(heartbeatThread, std::ref(arduino));
    hb.detach(); // 백그라운드에서 실행

    // WSAStartup 재시도 루프
    WSADATA wsaData;
    while (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "[WSAStartup] 초기화 실패 → " << RECONNECT_DELAY_SEC << "초 후 재시도..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(RECONNECT_DELAY_SEC));
    }

    // 서버 연결 루프 - 연결 끊기면 재시도
    while (g_running) {
        SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock == INVALID_SOCKET) {
            std::cerr << "[서버 연결] 소켓 생성 실패: " << WSAGetLastError()
                << " → " << RECONNECT_DELAY_SEC << "초 후 재시도..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(RECONNECT_DELAY_SEC));
            continue;
        }

        sockaddr_in serverAddr{};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(SERVER_PORT);
        inet_pton(AF_INET, SERVER_IP, &serverAddr.sin_addr);

        if (connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
            std::cerr << "[서버 연결] 연결 실패: " << WSAGetLastError()
                << " → " << RECONNECT_DELAY_SEC << "초 후 재시도..." << std::endl;
            closesocket(sock);
            std::this_thread::sleep_for(std::chrono::seconds(RECONNECT_DELAY_SEC));
            continue;
        }

        std::cout << "[서버 연결] 연결 성공! (" << SERVER_IP << ":" << SERVER_PORT << ")" << std::endl;
        std::cout << "대기 중... (Ctrl+C로 종료)" << std::endl;
        std::cout << "----------------------------------------" << std::endl;

        g_serverConnected = true; // 구분선 이후부터 heartbeat 허용
        g_running = true; // 재접속 후 루프 재개를 위해 플래그 리셋

        sendPacket(sock, PING);
        std::cout << "◁◁◁ [서버 전송] CmdID " << PING << " / " << cmdName(PING) << std::endl;

        std::thread serverRecvThread(serverRecvLoop, sock, arduino);
        std::thread arduinoRecvThread(arduinoRecvLoop, std::ref(arduino), sock);

        serverRecvThread.join();
        arduinoRecvThread.join();

        closesocket(sock);

        if (g_running) {
            g_serverConnected = false; // 재연결 시 구분선 다시 출력될 때까지 heartbeat 중단
            std::cerr << "[서버 연결] 연결 끊김 " << RECONNECT_DELAY_SEC << "초 후 재시도..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(RECONNECT_DELAY_SEC));
            g_running = true;
        }
    }

    CloseHandle(arduino);
    WSACleanup();
    std::cout << "종료" << std::endl;
    return 0;
}