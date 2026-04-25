// MetalGuardUIDlg.h: 헤더 파일
#pragma once
#include <string>
#include <thread>
#include <atomic>
#include <functional>

// BridgeMain 전방 선언 (Local.cpp에 정의)
void BridgeMain(const std::string& serverIp, int port, const std::string& arduinoPort);

// 로그 콜백 등록 함수 전방 선언
using LogCallback = std::function<void(const std::string&)>;
void SetLogCallbacks(LogCallback serverCb, LogCallback arduinoCb, LogCallback statusCb);

// Manual Control 버튼 → 아두이노 직접 전송
void ManualSendToArduino(const std::string& signal);

// g_running 외부 참조 (STOP 버튼용)
extern std::atomic<bool> g_running;
extern std::atomic<bool> g_serverConnected;
extern std::atomic<time_t> g_lastPongTime;

// CMetalGuardUIDlg 대화 상자
class CMetalGuardUIDlg : public CDialogEx
{
public:
    CMetalGuardUIDlg(CWnd* pParent = nullptr);

#ifdef AFX_DESIGN_TIME
    enum { IDD = IDD_METALGUARDUI_DIALOG };
#endif

protected:
    virtual void DoDataExchange(CDataExchange* pDX);

    // 멤버 변수
protected:
    HICON       m_hIcon;
    std::thread m_bridgeThread;

    CEdit       m_editServerIp;
    CEdit       m_editPort;
    CComboBox   m_comboArduinoPort;
    CListBox    m_listServerLog;
    CListBox    m_listArduinoLog;

    void AppendServerLog(const std::string& msg);
    void AppendArduinoLog(const std::string& msg);
    void AppendStatusLog(const std::string& msg);
    void PopulateComPorts();
    void UpdateStatusLabels();

protected:
    virtual BOOL OnInitDialog();
    virtual LRESULT WindowProc(UINT message, WPARAM wParam, LPARAM lParam);
    afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
    afx_msg void OnPaint();
    afx_msg HCURSOR OnQueryDragIcon();
    afx_msg void OnTimer(UINT_PTR nIDEvent);
    afx_msg void OnBnClickedBtnStart();
    afx_msg void OnBnClickedBtnStop();
    afx_msg void OnBnClickedBtnClearServer();
    afx_msg void OnBnClickedBtnClearArduino();
    afx_msg void OnBnClickedBtnPass();
    afx_msg void OnBnClickedBtnFail();
    afx_msg void OnBnClickedBtnUncertain();
    afx_msg void OnBnClickedBtnTimeout();
    afx_msg void OnCbnDropdownComboArduinoPort();
    DECLARE_MESSAGE_MAP()
};