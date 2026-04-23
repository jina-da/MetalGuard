// PylonSampleProgramDlg.h : 헤더 파일
//

#pragma once
#include "CameraManager.h"
#include "afxcmn.h"
#include "afxwin.h"

// 메시지 정의
#define WM_UPDATE_LOG     (WM_USER + 100)
#define WM_USER_ADD_LOG   (WM_USER + 101)
#define WM_UPDATE_VERDICT (WM_USER + 102)   // ← 신규: 판정 결과 UI 업데이트

class CPylonSampleProgramDlg;

struct ReclassifyParam {
    CPylonSampleProgramDlg* pDlg;
    cv::Mat matImage;
    int nPlateId;
};

// ─────────────────────────────────────────────────────────────
//  VerdictData: CameraManager.cpp 수신 스레드 → 메인 다이얼로그
//  WM_UPDATE_VERDICT 메시지의 WPARAM으로 힙 할당 후 전달.
//  OnUpdateVerdict()에서 delete 처리.
// ─────────────────────────────────────────────────────────────
struct VerdictData {
    std::string verdict;        // "PASS" | "FAIL" | "UNCERTAIN"
    std::string defect;         // "normal" | "crack" | "hole" | "rust" | "scratch"
    float prob_normal   = 0.f;  // 0~100 (%)
    float prob_crack    = 0.f;
    float prob_hole     = 0.f;
    float prob_rust     = 0.f;
    float prob_scratch  = 0.f;
    float inference_ms  = 0.f;
    float pipeline_ms   = 0.f;
    int   plate_id      = 0;
};


// CPylonSampleProgramDlg 대화 상자
class CPylonSampleProgramDlg : public CDialog
{
public:
    CPylonSampleProgramDlg(CWnd* pParent = NULL);

    enum { IDD = IDD_PYLONSAMPLEPROGRAM_DIALOG };

protected:
    virtual void DoDataExchange(CDataExchange* pDX);

protected:
    HICON m_hIcon;

    virtual BOOL OnInitDialog();
    afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
    afx_msg void OnPaint();
    afx_msg HCURSOR OnQueryDragIcon();

    // 로그 메시지 핸들러
    afx_msg LRESULT OnUserAddLog(WPARAM wParam, LPARAM lParam);
    afx_msg LRESULT OnUpdateLog(WPARAM wParam, LPARAM lParam);

    // ─── 신규: 판정 결과 UI 업데이트 핸들러 ───────────────────
    afx_msg LRESULT OnUpdateVerdict(WPARAM wParam, LPARAM lParam);

    DECLARE_MESSAGE_MAP()

public:
    // ── 카메라 관련 ──────────────────────────────────────────
    CCameraManager m_CameraManager;

    char m_szCamName[CAM_NUM][100];
    char m_szSerialNum[CAM_NUM][100];
    char m_szInterface[CAM_NUM][100];
    int  m_iCamNumber;
    int  m_iCamPosition[CAM_NUM];
    int  m_iCameraIndex;
    int  m_error;
    int  m_nPlateId;
    CString m_strCamSerial[CAM_NUM];

    HDC   hdc[CAM_NUM];
    HWND  hWnd[CAM_NUM];
    CRect rectStaticClient[CAM_NUM];

    BITMAPFILEHEADER fileheader;
    LPBITMAPINFO     bitmapinfo[CAM_NUM];

    unsigned char** pImageresizeOrgBuffer[CAM_NUM];
    unsigned char** pImageColorDestBuffer[CAM_NUM];

    bool bStopThread[CAM_NUM];
    bool bLiveFlag[CAM_NUM];
    int  m_iListIndex;

    int    nFrameCount[CAM_NUM];
    double time[CAM_NUM];
    LARGE_INTEGER freq[CAM_NUM], start[CAM_NUM], end[CAM_NUM];

    int m_nCamIndexBuf[CAM_NUM];

    // ── UI 컨트롤 바인딩 ─────────────────────────────────────
    CListCtrl m_ctrlCamList;    // 카메라 목록
    CListBox  m_listLog;        // 시스템 로그
    CListCtrl m_listDB;         // DB 기록 테이블 (신규)

    // ── 신규: DB 테이블 행 카운터 ────────────────────────────
    int m_nDBRowCount;

public:
    void GetSerialNumerFromFile(void);
    afx_msg void OnBnClickedFindCamBtn();
    afx_msg void OnNMClickCameraList(NMHDR* pNMHDR, LRESULT* pResult);
    afx_msg void OnBnClickedOpenCameraBtn();
    afx_msg void OnBnClickedConnectCameraBtn();
    afx_msg void OnBnClickedGrabSingleBtn();
    afx_msg void OnBnClickedCam0Live();
    afx_msg void OnBnClickedCloseCamBtn();
    afx_msg void OnBnClickedSoftTrigBtn();
    afx_msg void OnBnClickedExitBtn();
    afx_msg void OnBnClickedCam1Live();
    afx_msg void OnNMClickListCam(NMHDR* pNMHDR, LRESULT* pResult);

    void AllocImageBuf(void);
    void InitBitmap(int nCamIndex);
    void DisplayCam0(void* pImageBuf);
    void DisplayCam1(void* pImageBuf);
    void DisplayCam2(void* pImageBuf);
    void DisplayCam3(void* pImageBuf);

    afx_msg void OnBnClickedSaveImgBtn();
    afx_msg void OnBnClickedButton5();
    afx_msg void OnBnClickedTwoCameraLiveBtn();
    afx_msg void OnBnClickedCam2Live();
    afx_msg void OnBnClickedCam3Live();
    afx_msg void OnBnClickedReclassifyBtn();

    // ── 신규: 운영 모드 콤보박스 변경 핸들러 ─────────────────
    afx_msg void OnCbnSelchangeModeCombo();

    static UINT ThreadReclassify(LPVOID pParam);
};
