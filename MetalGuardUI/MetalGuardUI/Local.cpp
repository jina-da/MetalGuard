#include "pch.h"
#pragma comment(lib, "ws2_32.lib")
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <cstdint>
#include <string>
#include <thread>
#include <atomic>
#include <ctime>
#include <functional>

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

// ── 로그 콜백 타입 정의 ──────────────────────────────────────────────
// UI에서 등록, 각 로그 출력 시 호출됨
using LogCallback = std::function<void(const std::string&)>;

LogCallback g_serverLogCb;   // 서버 송수신 로그
LogCallback g_arduinoLogCb;  // 아두이노 송수신 로그
LogCallback g_statusLogCb;   // 연결 상태 변경 로그

// 콜백 등록 함수 (MetalGuardUIDlg에서 호출)
void SetLogCallbacks(LogCallback serverCb, LogCallback arduinoCb, LogCallback statusCb) {
    g_serverLogCb = serverCb;
    g_arduinoLogCb = arduinoCb;
    g_statusLogCb = statusCb;
}

// 로그 출력 헬퍼
static void LogServer(const char* fmt, ...) {
    char buf[512]; va_list a; va_start(a, fmt); vsprintf_s(buf, fmt, a); va_end(a);
    if (g_serverLogCb) g_serverLogCb(buf);
}
static void LogArduino(const char* fmt, ...) {
    char buf[512]; va_list a; va_start(a, fmt); vsprintf_s(buf, fmt, a); va_end(a);
    if (g_arduinoLogCb) g_arduinoLogCb(buf);
}
static void LogStatus(const char* fmt, ...) {
    char buf[512]; va_list a; va_start(a, fmt); vsprintf_s(buf, fmt, a); va_end(a);
    if (g_statusLogCb) g_statusLogCb(buf);
}

// 전역 변수
std::atomic<bool> g_running(true);
std::atomic<bool> g_serverConnected(false);
std::atomic<bool> g_serverDisconnected(false); // 서버 끊김 신호
std::atomic<time_t> g_lastPongTime(0);

HANDLE g_arduinoHandle = INVALID_HANDLE_VALUE;          // Manual Control 버튼에서 직접 아두이노로 신호 전송하기 위한 전역 핸들

const int PONG_TIMEOUT_SEC = 8;                 // 이 시간 안에 PONG 없으면 아두이노 연결 끊김으로 판단
const int RECONNECT_DELAY_SEC = 3;              // 재접속 시도 간격(초)

// Manual Control: UI에서 직접 아두이노 신호 전송
void ManualSendToArduino(const std::string& signal) {
    if (g_arduinoHandle == NULL || g_arduinoHandle == INVALID_HANDLE_VALUE) return;
    DWORD written;
    WriteFile(g_arduinoHandle, signal.c_str(), (DWORD)signal.size(), &written, NULL);
    std::string trimmed = signal;
    while (!trimmed.empty() && (trimmed.back() == '\n' || trimmed.back() == '\r'))
        trimmed.pop_back();
    LogArduino("◁ [수동 조작] 아두이노 전송: %s", trimmed.c_str());
}

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
        LogStatus("[송신 실패] 헤더 오류: %d", (int)WSAGetLastError());
        return false;
    }
    if (!body.empty()) {
        if (send(sock, body.c_str(), (int)body.size(), 0) == SOCKET_ERROR) {
            LogStatus("[송신 실패] 바디 오류: %d", (int)WSAGetLastError());
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

// HEARTBEAT(H) 전송은 로그 생략, 나머지만 출력
void sendToArduino(HANDLE arduino, CmdID cmdId, const std::string& signal) {
    if (arduino == NULL || arduino == INVALID_HANDLE_VALUE) return;
    DWORD written;
    WriteFile(arduino, signal.c_str(), (DWORD)signal.size(), &written, NULL);
    if (cmdId != PING)
        LogArduino("◁ [아두이노 전송] CmdID %d / %s", (int)cmdId, cmdName(cmdId));
}

// 아두이노 수신 루프
// 아두이노로부터 완료 신호 수신 → 매핑 테이블로 CmdID 찾아 서버에 전송
void arduinoRecvLoop(HANDLE& arduino, SOCKET sock) {
    std::string buffer;

    while (g_running && !g_serverDisconnected) {
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
                    LogArduino("▶ [아두이노 수신] CmdID %d / %s", (int)cmdId, cmdName(cmdId));
                    LogServer("◁ [서버 전송] CmdID %d / %s", (int)cmdId, cmdName(cmdId));
                    sendPacket(sock, cmdId);
                    matched = true;
                    break;
                }
            }

            // PONG 수신: 첫 수신이면 "아두이노 연결 확인" 출력
            if (!matched && trimmed == "PONG") {
                bool firstPong = (g_lastPongTime.load() == 0);
                g_lastPongTime = time(nullptr);
                if (firstPong)
                    LogArduino("[아두이노 연결] 연결 완료");
                //LogArduino("▶ [아두이노 수신] CmdID %d / %s", (int)PONG, cmdName(PONG));
                matched = true;
            }

            if (!matched)
                LogArduino("▶ [아두이노 수신] 알 수 없는 신호: %s", trimmed.c_str());

            buffer.clear();
        }
    }
}

// 서버 수신 루프
void serverRecvLoop(SOCKET sock, HANDLE arduino) {
    while (g_running && !g_serverDisconnected) {
        PacketHeader header{};
        if (!recvExact(sock, (char*)&header, sizeof(header))) {
            //if (g_running)
            //    LogServer("▶ [서버 수신] 연결 끊김");
            g_serverDisconnected = true;
            break;
        }

        uint16_t sig = swap16(header.signature);
        CmdID cmdId = static_cast<CmdID>(swap16(header.cmdId));
        uint32_t bodySize = swap32(header.bodySize);

        if (sig != 0x4D47) {
            char buf[64];
            sprintf_s(buf, "▶ [서버 수신] 잘못된 signature: 0x%X", sig);
            LogServer(buf);
            g_serverDisconnected = true;
            break;
        }

        std::string body;
        if (bodySize > 0) {
            body.resize(bodySize);
            recvExact(sock, &body[0], (int)bodySize);
        }

        // 매핑 테이블에서 CmdID 검색
        bool matched = false;
        for (int i = 0; i < SIGNAL_TABLE_COUNT; i++) {
            if (SIGNAL_TABLE[i].cmdId == cmdId && SIGNAL_TABLE[i].toArduino != nullptr) {
                LogServer("▶ [서버 수신] CmdID %d / %s", (int)cmdId, cmdName(cmdId));
                sendToArduino(arduino, cmdId, SIGNAL_TABLE[i].toArduino);
                matched = true;
                break;
            }
        }
        if (!matched) {
            if (cmdId == PONG)
                //LogServer("▶ [서버 수신] CmdID %d / %s", (int)cmdId, cmdName(cmdId));
                LogServer("[서버 연결] 연결 완료");
        }
    }
}

// 하트비트 스레드 : BridgeMain에서 사용하는 arduinoPort를 저장해두고 재접속 시 재사용

static std::string g_arduinoPortForReconnect;

void heartbeatThread(HANDLE& arduino) {
    // 아두이노 핸들이 유효해질 때까지만 대기 (서버 연결 불필요)
    while ((arduino == NULL || arduino == INVALID_HANDLE_VALUE) && g_running)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

    while (g_running) {
        if (arduino != NULL && arduino != INVALID_HANDLE_VALUE)
            sendToArduino(arduino, PING, "H\n");        // 아두이노 핸들이 유효하면 H 전송

        std::this_thread::sleep_for(std::chrono::seconds(5));   // sleep 후 PONG 도착 시간 확보

        // PONG 타임아웃 체크
        time_t elapsed = time(nullptr) - g_lastPongTime.load();
        if (elapsed >= PONG_TIMEOUT_SEC) {
            LogArduino("[아두이노 연결] 연결 끊김. %d초 후 재시도...", RECONNECT_DELAY_SEC);

            if (arduino != NULL && arduino != INVALID_HANDLE_VALUE) {
                CloseHandle(arduino);
                arduino = INVALID_HANDLE_VALUE;
                g_arduinoHandle = INVALID_HANDLE_VALUE;
            }

            // 재접속 루프
            while (g_running) {
                std::this_thread::sleep_for(std::chrono::seconds(RECONNECT_DELAY_SEC));
                HANDLE newHandle = openArduino(g_arduinoPortForReconnect.c_str());
                if (newHandle != INVALID_HANDLE_VALUE) {
                    arduino = newHandle;
                    g_arduinoHandle = newHandle;
                    g_lastPongTime = 0;                                     // 재접속 후 첫 PONG 때 "연결 확인" 재출력
                    LogArduino("[아두이노 연결] 연결 시도");
                    break;
                }
                LogArduino("[아두이노 연결] 연결 실패, %d초 후 재시도...", RECONNECT_DELAY_SEC);
            }
        }
    }
}

// BridgeMain - UI의 START 버튼에서 백그라운드 스레드로 호출
// serverIp, port, arduinoPort: UI에서 입력받은 값
void BridgeMain(const std::string& serverIp, int port, const std::string& arduinoPort) {
    // 전역 설정 적용
    const char* usedIp = serverIp.c_str();
    int         usedPort = port;

    g_arduinoPortForReconnect = arduinoPort; // heartbeatThread 재접속용

    LogStatus("[시작] MetalGuard 아두이노 브리지 시작");
    LogArduino("[시작] MetalGuard 아두이노 브리지 시작");
    g_running = true;
    g_serverConnected = false;
    g_serverDisconnected = false;
    g_lastPongTime = 0;  // 초기화 → 첫 PONG 수신 시 "연결 확인" 출력

    // 아두이노 포트 열기 (실제 통신 확인은 PONG 수신 시)
    HANDLE arduino = INVALID_HANDLE_VALUE;
    while (g_running && arduino == INVALID_HANDLE_VALUE) {
        arduino = openArduino(arduinoPort.c_str());
        if (arduino == INVALID_HANDLE_VALUE) {
            LogArduino("[아두이노 연결] 연결 실패. %d초 후 재시도...", RECONNECT_DELAY_SEC);
            std::this_thread::sleep_for(std::chrono::seconds(RECONNECT_DELAY_SEC));
        }
    }
    if (!g_running) return;

    // 포트 열기 성공 ≠ 아두이노 연결 확인 → 명확하게 구분
    const char* ard_port = strrchr(arduinoPort.c_str(), '\\');
    ard_port = ard_port ? ard_port + 1 : arduinoPort.c_str();
    LogArduino("[아두이노 연결] 연결 시도 (%s)", ard_port);
    g_arduinoHandle = arduino; // Manual Control용 핸들 공개

    // 하트비트 스레드 시작
    std::thread hb(heartbeatThread, std::ref(arduino));
    hb.detach();

    // WSAStartup 재시도 루프
    WSADATA wsaData;
    while (g_running && WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        LogStatus("[WSAStartup] 초기화 실패 → %d초 후 재시도...", RECONNECT_DELAY_SEC);
        std::this_thread::sleep_for(std::chrono::seconds(RECONNECT_DELAY_SEC));
    }

    // 서버 연결 루프 - 연결 끊기면 재시도
    while (g_running) {
        g_serverDisconnected = false; // 새 연결 시도 전 리셋

        SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock == INVALID_SOCKET) {
            LogStatus("[서버 연결] 소켓 생성 실패. %d초 후 재시도...", RECONNECT_DELAY_SEC);
            std::this_thread::sleep_for(std::chrono::seconds(RECONNECT_DELAY_SEC));
            continue;
        }

        sockaddr_in serverAddr{};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons((u_short)usedPort);
        inet_pton(AF_INET, usedIp, &serverAddr.sin_addr);


        // connect() 타임아웃 5초 설정 (논블로킹 방식)
        u_long nonBlocking = 1;
        ioctlsocket(sock, FIONBIO, &nonBlocking);

        connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr));

        fd_set writeSet;
        FD_ZERO(&writeSet);
        FD_SET(sock, &writeSet);
        timeval tv{ 5, 0 }; // 5초 타임아웃
        int sel = select(0, NULL, &writeSet, NULL, &tv);

        // 블로킹 모드로 복원
        u_long blocking = 0;
        ioctlsocket(sock, FIONBIO, &blocking);

        if (sel <= 0) {
            LogStatus("[서버 연결] 연결 실패: %d초 후 재시도...", RECONNECT_DELAY_SEC);
            closesocket(sock);
            std::this_thread::sleep_for(std::chrono::seconds(RECONNECT_DELAY_SEC));
            continue;
        }

        // connect 성공 여부 최종 확인
        int err = 0;
        int errLen = sizeof(err);
        getsockopt(sock, SOL_SOCKET, SO_ERROR, (char*)&err, &errLen);
        if (err != 0) {
            LogStatus("[서버 연결] 연결 실패. %d초 후 재시도...", RECONNECT_DELAY_SEC);
            closesocket(sock);
            std::this_thread::sleep_for(std::chrono::seconds(RECONNECT_DELAY_SEC));
            continue;
        }

        LogStatus("[서버 연결] 연결 시도 (%s:%d)", usedIp, usedPort);

        g_serverConnected = true;

        sendPacket(sock, PING);

        std::thread serverRecvThread(serverRecvLoop, sock, arduino);
        std::thread arduinoRecvThread(arduinoRecvLoop, std::ref(arduino), sock);

        serverRecvThread.join();
        arduinoRecvThread.join();

        closesocket(sock);

        if (g_running) {
            g_serverConnected = false;
            LogStatus("[서버 연결] 연결 끊김 → %d초 후 재시도...", RECONNECT_DELAY_SEC);
            std::this_thread::sleep_for(std::chrono::seconds(RECONNECT_DELAY_SEC));
        }
    }

    CloseHandle(arduino);
    g_arduinoHandle = INVALID_HANDLE_VALUE;
    WSACleanup();
    LogStatus("[종료] 브리지 종료");
}