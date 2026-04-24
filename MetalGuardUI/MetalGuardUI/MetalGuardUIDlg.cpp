// MetalGuardUIDlg.cpp: 구현 파일
#include "pch.h"
#include "framework.h"
#include "MetalGuardUI.h"
#include "MetalGuardUIDlg.h"
#include "afxdialogex.h"
#include "resource.h"
#include <string>
#include <sstream>
#include <ctime>

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// Local.cpp의 아두이노 핸들 직접 접근 (Manual Control용)
// heartbeatThread가 관리하는 핸들과 동일한 변수를 참조
// Local.cpp에서 extern으로 노출
extern HANDLE g_arduinoHandle;

// 타임스탬프 생성 헬퍼
static std::string GetTimestamp()
{
    time_t now = time(nullptr);
    struct tm t;
    localtime_s(&t, &now);
    char buf[16];
    sprintf_s(buf, "[%02d:%02d:%02d]", t.tm_hour, t.tm_min, t.tm_sec);
    return buf;
}

// CAboutDlg
class CAboutDlg : public CDialogEx
{
public:
    CAboutDlg() : CDialogEx(IDD_ABOUTBOX) {}
#ifdef AFX_DESIGN_TIME
    enum { IDD = IDD_ABOUTBOX };
#endif
protected:
    virtual void DoDataExchange(CDataExchange* pDX) { CDialogEx::DoDataExchange(pDX); }
    DECLARE_MESSAGE_MAP()
};
BEGIN_MESSAGE_MAP(CAboutDlg, CDialogEx)
END_MESSAGE_MAP()


// CMetalGuardUIDlg
CMetalGuardUIDlg::CMetalGuardUIDlg(CWnd* pParent)
    : CDialogEx(IDD_METALGUARDUI_DIALOG, pParent)
{
    m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

void CMetalGuardUIDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialogEx::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_EDIT_SERVER_IP, m_editServerIp);
    DDX_Control(pDX, IDC_EDIT_PORT, m_editPort);
    DDX_Control(pDX, IDC_COMBO_ARDUINO_PORT, m_comboArduinoPort);
    DDX_Control(pDX, IDC_LIST_SERVER_LOG, m_listServerLog);
    DDX_Control(pDX, IDC_LIST_ARDUINO_LOG, m_listArduinoLog);
}

BEGIN_MESSAGE_MAP(CMetalGuardUIDlg, CDialogEx)
    ON_WM_SYSCOMMAND()
    ON_WM_PAINT()
    ON_WM_QUERYDRAGICON()
    ON_WM_TIMER()
    ON_BN_CLICKED(IDC_BTN_START, &CMetalGuardUIDlg::OnBnClickedBtnStart)
    ON_BN_CLICKED(IDC_BTN_STOP, &CMetalGuardUIDlg::OnBnClickedBtnStop)
    ON_BN_CLICKED(IDC_BTN_CLEAR_SERVER, &CMetalGuardUIDlg::OnBnClickedBtnClearServer)
    ON_BN_CLICKED(IDC_BTN_CLEAR_ARDUINO, &CMetalGuardUIDlg::OnBnClickedBtnClearArduino)
    ON_BN_CLICKED(IDC_BTN_PASS, &CMetalGuardUIDlg::OnBnClickedBtnPass)
    ON_BN_CLICKED(IDC_BTN_FAIL, &CMetalGuardUIDlg::OnBnClickedBtnFail)
    ON_BN_CLICKED(IDC_BTN_UNCERTAIN, &CMetalGuardUIDlg::OnBnClickedBtnUncertain)
    ON_BN_CLICKED(IDC_BTN_TIMEOUT, &CMetalGuardUIDlg::OnBnClickedBtnTimeout)
    ON_CBN_DROPDOWN(IDC_COMBO_ARDUINO_PORT, &CMetalGuardUIDlg::OnCbnDropdownComboArduinoPort)
END_MESSAGE_MAP()


// OnInitDialog
BOOL CMetalGuardUIDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();

    // 시스템 메뉴 - 정보
    ASSERT((IDM_ABOUTBOX & 0xFFF0) == IDM_ABOUTBOX);
    ASSERT(IDM_ABOUTBOX < 0xF000);
    CMenu* pSysMenu = GetSystemMenu(FALSE);
    if (pSysMenu != nullptr) {
        CString strAboutMenu;
        if (strAboutMenu.LoadString(IDS_ABOUTBOX) && !strAboutMenu.IsEmpty()) {
            pSysMenu->AppendMenu(MF_SEPARATOR);
            pSysMenu->AppendMenu(MF_STRING, IDM_ABOUTBOX, strAboutMenu);
        }
    }
    SetIcon(m_hIcon, TRUE);
    SetIcon(m_hIcon, FALSE);

    // 기본값 설정
    m_editServerIp.SetWindowTextW(_T("10.10.10.109"));
    m_editPort.SetWindowTextW(_T("8000"));

    // COM 포트 탐색
    PopulateComPorts();

    // 로그 콜백 등록 - 백그라운드 스레드에서 호출되므로 PostMessage로 UI 스레드에 전달
    HWND hWnd = GetSafeHwnd();
    SetLogCallbacks(
        // Server Log 콜백
        [hWnd](const std::string& msg) {
            char buf[600];
            sprintf_s(buf, "%s %s", GetTimestamp().c_str(), msg.c_str());
            std::string* p = new std::string(buf);
            ::PostMessage(hWnd, WM_APP + 1, 0, (LPARAM)p);
        },
        // Arduino Log 콜백
        [hWnd](const std::string& msg) {
            char buf[600];
            sprintf_s(buf, "%s %s", GetTimestamp().c_str(), msg.c_str());
            std::string* p = new std::string(buf);
            ::PostMessage(hWnd, WM_APP + 2, 0, (LPARAM)p);
        },
        // Status Log 콜백 (서버 로그)
        [hWnd](const std::string& msg) {
            char buf[600];
            sprintf_s(buf, "%s %s", GetTimestamp().c_str(), msg.c_str());
            std::string* p = new std::string(buf);
            ::PostMessage(hWnd, WM_APP + 1, 0, (LPARAM)p);
        }
    );

    // 상태 갱신 타이머 (1초)
    SetTimer(1, 1000, nullptr);

    // STOP 버튼 비활성화
    GetDlgItem(IDC_BTN_STOP)->EnableWindow(FALSE);

    return TRUE;
}

// 로그 추가 헬퍼 (UI 스레드에서 호출)
void CMetalGuardUIDlg::AppendServerLog(const std::string& msg)
{
    CString cs(msg.c_str());
    int idx = m_listServerLog.AddString(cs);
    // 최대 500줄 유지
    while (m_listServerLog.GetCount() > 500)
        m_listServerLog.DeleteString(0);
    m_listServerLog.SetTopIndex(idx);
}

void CMetalGuardUIDlg::AppendArduinoLog(const std::string& msg)
{
    CString cs(msg.c_str());
    int idx = m_listArduinoLog.AddString(cs);
    while (m_listArduinoLog.GetCount() > 500)
        m_listArduinoLog.DeleteString(0);
    m_listArduinoLog.SetTopIndex(idx);
}

void CMetalGuardUIDlg::AppendStatusLog(const std::string& msg)
{
    // 상태 메시지는 양쪽 로그에 모두 표시
    AppendServerLog(msg);
    AppendArduinoLog(msg);
}


// WM_APP 커스텀 메시지 처리 (백그라운드 → UI 스레드 로그 전달)
LRESULT CMetalGuardUIDlg::WindowProc(UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_APP + 1) {
        std::string* p = reinterpret_cast<std::string*>(lParam);
        if (p) { AppendServerLog(*p); delete p; }
        return 0;
    }
    if (message == WM_APP + 2) {
        std::string* p = reinterpret_cast<std::string*>(lParam);
        if (p) { AppendArduinoLog(*p); delete p; }
        return 0;
    }
    if (message == WM_APP + 3) {
        std::string* p = reinterpret_cast<std::string*>(lParam);
        if (p) { AppendStatusLog(*p); delete p; }
        return 0;
    }
    return CDialogEx::WindowProc(message, wParam, lParam);
}


// COM 포트 탐색
void CMetalGuardUIDlg::PopulateComPorts()
{
    CString selected;
    m_comboArduinoPort.GetWindowTextW(selected); // 현재 선택값 기억
    m_comboArduinoPort.ResetContent();

    // HKLM\HARDWARE\DEVICEMAP\SERIALCOMM 에 현재 시스템의 COM 포트 목록이 등록됨
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DEVICEMAP\\SERIALCOMM",
        0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        char  valueName[256];
        char  valueData[256];
        DWORD index = 0;

        while (true) {
            DWORD nameLen = sizeof(valueName);
            DWORD dataLen = sizeof(valueData);
            DWORD type;

            LONG ret = RegEnumValueA(hKey, index++, valueName, &nameLen,
                NULL, &type, (LPBYTE)valueData, &dataLen);
            if (ret == ERROR_NO_MORE_ITEMS) break;
            if (ret != ERROR_SUCCESS) continue;

            // valueData = "COM3" 형태
            m_comboArduinoPort.AddString(CString(valueData));
        }
        RegCloseKey(hKey);
    }

    if (m_comboArduinoPort.GetCount() > 0) {
        // 이전 선택값이 아직 있으면 복원, 없으면 첫 번째 선택
        int idx = m_comboArduinoPort.FindStringExact(-1, selected);
        m_comboArduinoPort.SetCurSel(idx != CB_ERR ? idx : 0);
    }
    else
        m_comboArduinoPort.AddString(_T("COM 포트 없음"));
}


// 상태 레이블 갱신 (타이머 1초 주기)
void CMetalGuardUIDlg::UpdateStatusLabels()
{
    // 서버 상태
    CString serverStatus = g_serverConnected ? _T("운용서버 연결 상태:   연결됨") : _T("운용서버 연결 상태:   연결 끊김");
    SetDlgItemText(IDC_STATIC_SERVER_STATUS, serverStatus);

    // 아두이노 상태 (마지막 PONG으로부터 경과 시간으로 판단)
    time_t elapsed = time(nullptr) - g_lastPongTime.load();
    CString arduinoStatus = (g_lastPongTime > 0 && elapsed < 15)
        ? _T("아두이노 연결 상태:   연결됨")
        : _T("아두이노 연결 상태:   연결 끊김");
    SetDlgItemText(IDC_STATIC_ARDUINO_STATUS, arduinoStatus);

    // Last Heartbeat
    if (g_lastPongTime > 0) {
        struct tm t;
        time_t pt = g_lastPongTime.load();
        localtime_s(&t, &pt);
        CString hbStr;
        //hbStr.Format(_T("아두이노 연결 확인 시간:   %04d-%02d-%02d %02d:%02d:%02d"),
        //    t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
        //    t.tm_hour, t.tm_min, t.tm_sec);
        hbStr.Format(_T("아두이노 연결 확인 시간:   %02d:%02d:%02d"),
            t.tm_hour, t.tm_min, t.tm_sec);
        SetDlgItemText(IDC_STATIC_HEARTBEAT, hbStr);
    }
    else {
        SetDlgItemText(IDC_STATIC_HEARTBEAT, _T("아두이노 연결 확인 시간:   ---"));
    }
}

void CMetalGuardUIDlg::OnTimer(UINT_PTR nIDEvent)
{
    if (nIDEvent == 1)
        UpdateStatusLabels();
    CDialogEx::OnTimer(nIDEvent);
}

// START / STOP 버튼
void CMetalGuardUIDlg::OnBnClickedBtnStart()
{
    // UI에서 설정값 읽기
    CString ip, portStr, comPort;
    m_editServerIp.GetWindowTextW(ip);
    m_editPort.GetWindowTextW(portStr);
    m_comboArduinoPort.GetWindowTextW(comPort);

    std::string serverIp = CT2A(ip);
    int         port = _ttoi(portStr);
    // "COM3" → "\\\\.\\COM3" 변환
    char arduinoPortBuf[32];
    sprintf_s(arduinoPortBuf, "\\\\.\\%s", CT2A(comPort).m_psz);
    std::string arduinoPort = arduinoPortBuf;

    // 버튼 상태 전환
    GetDlgItem(IDC_BTN_START)->EnableWindow(FALSE);
    GetDlgItem(IDC_BTN_STOP)->EnableWindow(TRUE);

    // BridgeMain을 백그라운드 스레드로 실행
    g_running = true;
    m_bridgeThread = std::thread(BridgeMain, serverIp, port, arduinoPort);
    m_bridgeThread.detach();
}

void CMetalGuardUIDlg::OnBnClickedBtnStop()
{
    g_running = false;
    g_serverConnected = false;

    GetDlgItem(IDC_BTN_START)->EnableWindow(TRUE);
    GetDlgItem(IDC_BTN_STOP)->EnableWindow(FALSE);

    char stopBuf[64];
    sprintf_s(stopBuf, "%s [시스템] 브리지 중지 요청", GetTimestamp().c_str());
    AppendStatusLog(stopBuf);
}

// Clear 버튼
void CMetalGuardUIDlg::OnBnClickedBtnClearServer() { m_listServerLog.ResetContent(); }
void CMetalGuardUIDlg::OnBnClickedBtnClearArduino() { m_listArduinoLog.ResetContent(); }

// Manual Control (Debug) 버튼
void CMetalGuardUIDlg::OnBnClickedBtnPass() { ManualSendToArduino("P\n"); }
void CMetalGuardUIDlg::OnBnClickedBtnFail() { ManualSendToArduino("F\n"); }
void CMetalGuardUIDlg::OnBnClickedBtnUncertain() { ManualSendToArduino("U\n"); }
void CMetalGuardUIDlg::OnBnClickedBtnTimeout() { ManualSendToArduino("T\n"); }

// 드롭다운 열릴 때 COM 포트 재탐색
void CMetalGuardUIDlg::OnCbnDropdownComboArduinoPort()
{
    PopulateComPorts(); // 선택값 기억 및 복원이 내부에서 처리됨
}

// 기본 MFC 핸들러
void CMetalGuardUIDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
    if ((nID & 0xFFF0) == IDM_ABOUTBOX) {
        CAboutDlg dlgAbout;
        dlgAbout.DoModal();
    }
    else {
        CDialogEx::OnSysCommand(nID, lParam);
    }
}

void CMetalGuardUIDlg::OnPaint()
{
    if (IsIconic()) {
        CPaintDC dc(this);
        SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);
        int cxIcon = GetSystemMetrics(SM_CXICON);
        int cyIcon = GetSystemMetrics(SM_CYICON);
        CRect rect;
        GetClientRect(&rect);
        dc.DrawIcon((rect.Width() - cxIcon + 1) / 2, (rect.Height() - cyIcon + 1) / 2, m_hIcon);
    }
    else {
        CDialogEx::OnPaint();
    }
}

HCURSOR CMetalGuardUIDlg::OnQueryDragIcon()
{
    return static_cast<HCURSOR>(m_hIcon);
}