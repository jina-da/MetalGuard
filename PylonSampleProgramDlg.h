// PylonSampleProgramDlg.h
#pragma once
#include "CameraManager.h"
#include "afxcmn.h"
#include "afxwin.h"

#include "MetalGuardTypes.h"

#define WM_UPDATE_LOG     (WM_USER + 100)
#define WM_USER_ADD_LOG   (WM_USER + 101)
#define IDC_ONE_CLICK_START 1080

class CPylonSampleProgramDlg;

struct ReclassifyParam {
	CPylonSampleProgramDlg* pDlg;
	cv::Mat matImage;
	int nPlateId;
};

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
	afx_msg LRESULT OnUserAddLog(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnUpdateLog(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnUpdateVerdict(WPARAM wParam, LPARAM lParam);
	afx_msg HBRUSH OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor);
	DECLARE_MESSAGE_MAP()

public:
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

	CListCtrl m_ctrlCamList;
	CListBox  m_listLog;
	CListCtrl m_listDB;
	int       m_nDBRowCount;

	// 판정 결과 색상 브러시
	CBrush    m_brushPass;       // 초록
	CBrush    m_brushFail;       // 빨강
	CBrush    m_brushUncertain;  // 노랑
	CBrush    m_brushNormal;     // 기본
	CString   m_strCurrentVerdict; // 현재 판정 상태 저장

	// 로그 헬퍼 (UI 스레드 안전 PostMessage 방식)
	void WriteLog(const CString& strMsg);

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
	afx_msg void OnCbnSelchangeModeCombo();
	afx_msg void OnBnClickedOneClickStart();
	afx_msg void OnNMCustomdrawDbList(NMHDR* pNMHDR, LRESULT* pResult);

	static UINT ThreadReclassify(LPVOID pParam);
};
