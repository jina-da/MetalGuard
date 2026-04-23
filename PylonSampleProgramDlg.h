// PylonSampleProgramDlg.h : 헤더 파일
//

#pragma once
#include "CameraManager.h"
#include "afxcmn.h"
#include "afxwin.h"

// 메시지 정의
#define WM_UPDATE_LOG (WM_USER + 100)
#define WM_USER_ADD_LOG (WM_USER + 101)

class CPylonSampleProgramDlg;

struct ReclassifyParam {
	CPylonSampleProgramDlg* pDlg;
	cv::Mat matImage; // 스냅샷 이미지
	int nPlateId;
};

// CPylonSampleProgramDlg 대화 상자
class CPylonSampleProgramDlg : public CDialog
{
	// 생성입니다.
public:
	CPylonSampleProgramDlg(CWnd* pParent = NULL);	// 표준 생성자입니다.

	// 대화 상자 데이터입니다.
	enum { IDD = IDD_PYLONSAMPLEPROGRAM_DIALOG };

protected:
	virtual void DoDataExchange(CDataExchange* pDX);	// DDX/DDV 지원입니다.


	// 구현입니다.
protected:
	HICON m_hIcon;

	// 생성된 메시지 맵 함수
	virtual BOOL OnInitDialog();
	afx_msg void OnSysCommand(UINT nID, LPARAM lParam);
	afx_msg void OnPaint();
	afx_msg HCURSOR OnQueryDragIcon();

	// [추가] 서버 수신 스레드로부터 로그를 받기 위한 메시지 핸들러
	afx_msg LRESULT OnUserAddLog(WPARAM wParam, LPARAM lParam);
	afx_msg LRESULT OnUpdateLog(WPARAM wParam, LPARAM lParam);

	DECLARE_MESSAGE_MAP()

public:
	// [수정] 변수명 뒤의 보이지 않는 공백을 제거하고 깔끔하게 선언
	CCameraManager m_CameraManager;

	char m_szCamName[CAM_NUM][100];          // 모델 이름
	char m_szSerialNum[CAM_NUM][100];        // Serial Number
	char m_szInterface[CAM_NUM][100];        // Inter face 방식
	int  m_iCamNumber;                       // 연결된 카메라 수
	int  m_iCamPosition[CAM_NUM];            // 연결된 카메라 순서
	int  m_iCameraIndex;                     // 프로그램에서 사용할 카메라 인덱스 넘버 
	int  m_error;
	int m_nPlateId;
	CString   m_strCamSerial[CAM_NUM];       // Serial number를 파일에서 가져옴

	HDC hdc[CAM_NUM];
	HWND hWnd[CAM_NUM];
	CRect rectStaticClient[CAM_NUM];

	BITMAPFILEHEADER fileheader;
	LPBITMAPINFO  bitmapinfo[CAM_NUM];

	unsigned char** pImageresizeOrgBuffer[CAM_NUM];                   // Mono resize 이미지 버퍼     	
	unsigned char** pImageColorDestBuffer[CAM_NUM];                   // Color resize 이미지 버퍼

	bool   bStopThread[CAM_NUM];                // LiveThread flag
	bool   bLiveFlag[CAM_NUM];                  // Live mode flag
	int    m_iListIndex;                        // Listcontrol 인덱스

	// Frame rate
	int nFrameCount[CAM_NUM];
	double time[CAM_NUM];
	LARGE_INTEGER freq[CAM_NUM], start[CAM_NUM], end[CAM_NUM];


	int m_nCamIndexBuf[CAM_NUM];

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

	CListCtrl m_ctrlCamList;

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
	afx_msg void OnStnClickedCam0Info();
	afx_msg void OnBnClickedReclassifyBtn(); // 버튼 이벤트

	static UINT ThreadReclassify(LPVOID pParam); // 전송용 백그라운드 스레드
	CListBox m_listLog;
};