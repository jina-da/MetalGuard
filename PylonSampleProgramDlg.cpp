#include "stdafx.h"
#include "PylonSampleProgram.h"
#include "PylonSampleProgramDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif


CPylonSampleProgramDlg *pMainDlg;
UINT LiveGrabThreadCam0(LPVOID pParam)
{
	int nCamIndex = *(int*)pParam;
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

	while (pMainDlg->bStopThread[nCamIndex] == true)
	{
		if (pMainDlg->m_CameraManager.m_bRemoveCamera[nCamIndex] == true) break;

		// 이미지가 갱신되었는지 확인
		if (pMainDlg->m_CameraManager.CheckCaptureEnd(nCamIndex))
		{
			pMainDlg->nFrameCount[nCamIndex]++;

			// OnPaint를 거치지 않고 바로 화면에 출력
			CWnd* pWnd = pMainDlg->GetDlgItem(IDC_CAM0_DISPLAY); // 리소스 ID 확인 필수
			if (pWnd && !pMainDlg->m_CameraManager.m_matLiveImage[nCamIndex].empty())
			{
				CRect rect;
				pWnd->GetClientRect(&rect);
				CDC* pDC = pWnd->GetDC();

				// OpenCV Mat 리사이즈
				cv::Mat matResized;
				cv::resize(pMainDlg->m_CameraManager.m_matLiveImage[nCamIndex], matResized, cv::Size(rect.Width(), rect.Height()));

				// 비트맵 정보 설정
				BITMAPINFO bitInfo;
				memset(&bitInfo, 0, sizeof(BITMAPINFO));
				bitInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
				bitInfo.bmiHeader.biWidth = matResized.cols;
				bitInfo.bmiHeader.biHeight = -matResized.rows; // 상하 반전 방지
				bitInfo.bmiHeader.biPlanes = 1;
				bitInfo.bmiHeader.biBitCount = 24;
				bitInfo.bmiHeader.biCompression = BI_RGB;

				// DC에 직접 그리기
				pDC->SetStretchBltMode(COLORONCOLOR);
				::SetDIBitsToDevice(pDC->GetSafeHdc(),
					0, 0, matResized.cols, matResized.rows,
					0, 0, 0, matResized.rows,
					matResized.data, &bitInfo, DIB_RGB_COLORS);

				pWnd->ReleaseDC(pDC);
			}

			// 플래그 리셋 (다음 이미지를 받기 위함)
			pMainDlg->m_CameraManager.ReadEnd(nCamIndex);
		}

		// CPU 점유율 조절 (150ms 프로젝트이므로 10ms 정도면 충분히 빠름)
		Sleep(10);
	}
	return 0;
}

// 응용 프로그램 정보에 사용되는 CAboutDlg 대화 상자입니다.
class CAboutDlg : public CDialog
{
public:
	CAboutDlg();

// 대화 상자 데이터입니다.
	enum { IDD = IDD_ABOUTBOX };

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV 지원입니다.

// 구현입니다.
protected:
	DECLARE_MESSAGE_MAP()
};

CAboutDlg::CAboutDlg() : CDialog(CAboutDlg::IDD)
{
}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialog)
	ON_NOTIFY(NM_CLICK, IDC_CAMERA_LIST, &CPylonSampleProgramDlg::OnNMClickListCam)
END_MESSAGE_MAP()


// CPylonSampleProgramDlg 대화 상자


CPylonSampleProgramDlg::CPylonSampleProgramDlg(CWnd* pParent /*=NULL*/)
	: CDialog(CPylonSampleProgramDlg::IDD, pParent)
{

	for(int i=0; i<CAM_NUM; i++)
	{
	   pImageresizeOrgBuffer[i] = NULL;	
	   
	   pImageColorDestBuffer[i] = NULL;		
	   bitmapinfo[i] = NULL;
	   bStopThread[i] = false;
       nFrameCount[i] = 0;
	   time[i] = 0;
	   m_CameraManager.m_iCM_Width[i] = 1;
	   m_CameraManager.m_iCM_Height[i] = 1;
	   QueryPerformanceFrequency(&freq[i]);
	   m_nCamIndexBuf[i] = i;
	}
	m_iCameraIndex = -1;
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);


}

void CPylonSampleProgramDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	DDX_Control(pDX, IDC_CAMERA_LIST, m_ctrlCamList);

}

BEGIN_MESSAGE_MAP(CPylonSampleProgramDlg, CDialog)
	ON_WM_SYSCOMMAND()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	//}}AFX_MSG_MAP
	ON_BN_CLICKED(IDC_FIND_CAM_BTN, &CPylonSampleProgramDlg::OnBnClickedFindCamBtn)
	ON_NOTIFY(NM_CLICK, IDC_CAMERA_LIST, &CPylonSampleProgramDlg::OnNMClickCameraList)
	ON_BN_CLICKED(IDC_OPEN_CAMERA_BTN, &CPylonSampleProgramDlg::OnBnClickedOpenCameraBtn)
	ON_BN_CLICKED(IDC_CONNECT_CAMERA_BTN, &CPylonSampleProgramDlg::OnBnClickedConnectCameraBtn)
	ON_BN_CLICKED(IDC_GRAB_SINGLE_BTN, &CPylonSampleProgramDlg::OnBnClickedGrabSingleBtn)
	ON_BN_CLICKED(IDC_CAM0_LIVE, &CPylonSampleProgramDlg::OnBnClickedCam0Live)
	ON_BN_CLICKED(IDC_CLOSE_CAM_BTN, &CPylonSampleProgramDlg::OnBnClickedCloseCamBtn)

//	ON_WM_RBUTTONDBLCLK()
//	ON_WM_MOUSEMOVE()
	ON_BN_CLICKED(IDC_SOFT_TRIG_BTN, &CPylonSampleProgramDlg::OnBnClickedSoftTrigBtn)
	ON_BN_CLICKED(IDC_EXIT_BTN, &CPylonSampleProgramDlg::OnBnClickedExitBtn)
//	ON_STN_CLICKED(IDC_CAMERA1_STATS, &CPylonSampleProgramDlg::OnStnClickedCamera1Stats)
ON_BN_CLICKED(IDC_CAM1_LIVE, &CPylonSampleProgramDlg::OnBnClickedCam1Live)
ON_BN_CLICKED(IDC_SAVE_IMG_BTN, &CPylonSampleProgramDlg::OnBnClickedSaveImgBtn)
ON_BN_CLICKED(IDC_BUTTON5, &CPylonSampleProgramDlg::OnBnClickedButton5)
//ON_BN_CLICKED(IDC_LINEAVGCAL_BTN, &CPylonSampleProgramDlg::OnBnClickedLineavgcalBtn)
ON_BN_CLICKED(IDC_TWO_CAMERA_LIVE_BTN, &CPylonSampleProgramDlg::OnBnClickedTwoCameraLiveBtn)
ON_BN_CLICKED(IDC_CAM2_LIVE, &CPylonSampleProgramDlg::OnBnClickedCam2Live)
ON_BN_CLICKED(IDC_CAM3_LIVE, &CPylonSampleProgramDlg::OnBnClickedCam3Live)
END_MESSAGE_MAP()


// CPylonSampleProgramDlg 메시지 처리기

BOOL CPylonSampleProgramDlg::OnInitDialog()
{
	CDialog::OnInitDialog();

	// 시스템 메뉴 및 아이콘 설정
	ASSERT((IDM_ABOUTBOX & 0xFFF0) == IDM_ABOUTBOX);
	ASSERT(IDM_ABOUTBOX < 0xF000);
	CMenu* pSysMenu = GetSystemMenu(FALSE);
	if (pSysMenu != NULL)
	{
		CString strAboutMenu;
		strAboutMenu.LoadString(IDS_ABOUTBOX);
		if (!strAboutMenu.IsEmpty()) {
			pSysMenu->AppendMenu(MF_SEPARATOR);
			pSysMenu->AppendMenu(MF_STRING, IDM_ABOUTBOX, strAboutMenu);
		}
	}
	SetIcon(m_hIcon, TRUE);
	SetIcon(m_hIcon, FALSE);

	// --- 1. 리스트 컨트롤 초기화 ---
	m_ctrlCamList.InsertColumn(0, _T("모델명"), LVCFMT_CENTER, 130, -1);
	m_ctrlCamList.InsertColumn(1, _T("Position"), LVCFMT_CENTER, 80, -1);
	m_ctrlCamList.InsertColumn(2, _T("SerialNum"), LVCFMT_CENTER, 90, -1);
	m_ctrlCamList.InsertColumn(3, _T("Stats"), LVCFMT_CENTER, 150, -1);
	m_ctrlCamList.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_ONECLICKACTIVATE | LVS_EX_HEADERDRAGDROP);

	pMainDlg = this;

	// --- 2. 이미지 버퍼 메모리 할당 ---
	for (int i = 0; i < CAM_NUM; i++)
	{
		// 각 카메라당 BUF_NUM(3)개의 포인터를 담을 배열 할당
		pImageColorDestBuffer[i] = new unsigned char* [BUF_NUM];

		for (int j = 0; j < BUF_NUM; j++)
		{
			// 각 프레임이 저장될 실제 메모리 할당 (260x260x3 BGR)
			size_t nFrameSize = 260 * 260 * 3;
			pImageColorDestBuffer[i][j] = new unsigned char[nFrameSize];
			memset(pImageColorDestBuffer[i][j], 0, nFrameSize); // 0으로 초기화
		}
	}

	// --- 3. 화면 출력용 HDC 및 영역 초기화 ---
	UINT nStaticIDs[] = { IDC_CAM0_DISPLAY, IDC_CAM1_DISPLAY, IDC_CAM2_DISPLAY, IDC_CAM3_DISPLAY };

	for (int i = 0; i < CAM_NUM; i++)
	{
		CWnd* pWnd = GetDlgItem(nStaticIDs[i]);
		if (pWnd)
		{
			// 출력할 영역(Rect) 가져오기
			pWnd->GetClientRect(&rectStaticClient[i]);
			// 그리기 핸들(HDC) 가져오기
			hdc[i] = pWnd->GetDC()->GetSafeHdc();
		}

		// 비트맵 정보 메모리 할당 (24비트 컬러 기준)
		if (bitmapinfo[i] == NULL)
		{
			bitmapinfo[i] = (BITMAPINFO*)new BYTE[sizeof(BITMAPINFO)];
			memset(bitmapinfo[i], 0, sizeof(BITMAPINFO));

			// Bitmap Header 설정 (260x260, 24bit 고정)
			bitmapinfo[i]->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
			bitmapinfo[i]->bmiHeader.biWidth = 260;
			bitmapinfo[i]->bmiHeader.biHeight = -260; // Top-down 방식
			bitmapinfo[i]->bmiHeader.biPlanes = 1;
			bitmapinfo[i]->bmiHeader.biBitCount = 24;
			bitmapinfo[i]->bmiHeader.biCompression = BI_RGB;
		}
	}

	return TRUE;
}

void CPylonSampleProgramDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) == IDM_ABOUTBOX)
	{
		CAboutDlg dlgAbout;
		dlgAbout.DoModal();
	}
	else
	{
		CDialog::OnSysCommand(nID, lParam);
	}
}

void CPylonSampleProgramDlg::OnPaint()
{
	if (IsIconic())
	{
		CPaintDC dc(this); // 그리기를 위한 디바이스 컨텍스트
		SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CPaintDC dc(this);
		CDialog::OnPaint();

		// 카메라 0번(Live용) 이미지가 비어있지 않으면 화면에 출력
		if (!m_CameraManager.m_matLiveImage[0].empty())
		{
			// 이미지를 표시
			CWnd* pWnd = GetDlgItem(IDC_CAM0_DISPLAY);
			if (pWnd)
			{
				CRect rect;
				pWnd->GetClientRect(&rect);
				CDC* pDC = pWnd->GetDC();

				cv::Mat matResized;
				// Picture Control 크기에 맞게 이미지 리사이즈
				cv::resize(m_CameraManager.m_matLiveImage[0], matResized, cv::Size(rect.Width(), rect.Height()));

				// OpenCV Mat을 MFC 전용 CImage나 Bitmap으로 변환하여 출력
				BITMAPINFO bitInfo;
				bitInfo.bmiHeader.biBitCount = 24;
				bitInfo.bmiHeader.biWidth = matResized.cols;
				bitInfo.bmiHeader.biHeight = -matResized.rows;
				bitInfo.bmiHeader.biPlanes = 1;
				bitInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
				bitInfo.bmiHeader.biCompression = BI_RGB;
				bitInfo.bmiHeader.biClrImportant = 0;
				bitInfo.bmiHeader.biClrUsed = 0;
				bitInfo.bmiHeader.biSizeImage = 0;
				bitInfo.bmiHeader.biXPelsPerMeter = 0;
				bitInfo.bmiHeader.biYPelsPerMeter = 0;

				// 고속 화면 출력
				::SetDIBitsToDevice(pDC->GetSafeHdc(), 0, 0, matResized.cols, matResized.rows,
					0, 0, 0, matResized.rows, matResized.data, &bitInfo, DIB_RGB_COLORS);

				ReleaseDC(pDC);
			}
		}
	}
}

// 사용자가 최소화된 창을 끄는 동안에 커서가 표시되도록 시스템에서 호출
HCURSOR CPylonSampleProgramDlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}


void CPylonSampleProgramDlg::OnBnClickedFindCamBtn()
{
	// 1. 파일로부터 설정된 시리얼 번호 로드
	GetSerialNumerFromFile();

	// 2. 물리적으로 연결된 카메라 검색
	m_error = m_CameraManager.FindCamera(m_szCamName, m_szSerialNum, m_szInterface, &m_iCamNumber);

	if (m_error == 0)
	{
		m_ctrlCamList.DeleteAllItems();
		int nCount = 0;
		CString strSerialNum;

		for (int i = 0; i < m_iCamNumber; i++)
		{
			nCount++;
			CString strcamname = (CString)m_szCamName[i];
			strSerialNum = (CString)m_szSerialNum[i];

			CString strCheck;
			strCheck.Format(_T("인식된 카메라 [%d]\n이름: %s\n시리얼: %s\n\n설정파일 시리얼0: %s\n설정파일 시리얼1: %s"),
				i, strcamname, strSerialNum, m_strCamSerial[0], m_strCamSerial[1]);
			AfxMessageBox(strCheck);

			bool bFound = false;

			// 시리얼 번호 비교 로직
			if (strSerialNum == m_strCamSerial[0])
			{
				m_iCamPosition[0] = nCount; // 실제 연결 순서 저장
				m_ctrlCamList.InsertItem(i, strcamname);
				m_ctrlCamList.SetItemText(i, 1, _T("Inspect0"));
				m_ctrlCamList.SetItemText(i, 2, strSerialNum);
				m_ctrlCamList.SetItemText(i, 3, _T("Find_Success"));
				bFound = true;
			}
			else if (strSerialNum == m_strCamSerial[1])
			{
				m_iCamPosition[1] = nCount;
				m_ctrlCamList.InsertItem(i, strcamname);
				m_ctrlCamList.SetItemText(i, 1, _T("Inspect1"));
				m_ctrlCamList.SetItemText(i, 2, strSerialNum);
				m_ctrlCamList.SetItemText(i, 3, _T("Find_Success"));
				bFound = true;
			}
			else if (strSerialNum == m_strCamSerial[2])
			{
				m_iCamPosition[2] = nCount;
				m_ctrlCamList.InsertItem(i, strcamname);
				m_ctrlCamList.SetItemText(i, 1, _T("Inspect2"));
				m_ctrlCamList.SetItemText(i, 2, strSerialNum);
				m_ctrlCamList.SetItemText(i, 3, _T("Find_Success"));
				bFound = true;
			}
			else if (strSerialNum == m_strCamSerial[3])
			{
				m_iCamPosition[3] = nCount;
				m_ctrlCamList.InsertItem(i, strcamname);
				m_ctrlCamList.SetItemText(i, 1, _T("Inspect3"));
				m_ctrlCamList.SetItemText(i, 2, strSerialNum);
				m_ctrlCamList.SetItemText(i, 3, _T("Find_Success"));
				bFound = true;
			}
			if (!bFound)
			{
				m_ctrlCamList.InsertItem(i, strcamname);
				m_ctrlCamList.SetItemText(i, 1, _T("Unknown"));
				m_ctrlCamList.SetItemText(i, 2, strSerialNum);
				m_ctrlCamList.SetItemText(i, 3, _T("Serial_Mismatch"));
			}
		}
	}
	else if (m_error == -1)
	{
		AfxMessageBox(_T("연결된 카메라가 없습니다."));
	}
	else if (m_error == -2)
	{
		AfxMessageBox(_T("Pylon Function Error (Library Link?)"));
	}
}

void CPylonSampleProgramDlg::GetSerialNumerFromFile(void)
{
    TCHAR buff[100];
	CString strTemp;
	DWORD length;
	//int CamNum 

	//length = GetPrivateProfileInt("CAM_INFO", _T("CamNum"), 1, PATH_CAMERA_INFO);

	for(int i=0; i<CAM_NUM; i++){
		strTemp.Format(_T("CAM%d"), i+1);

		memset(buff, 0x00, sizeof(buff));
		length = GetPrivateProfileString(strTemp, _T("Serial"), _T(""), buff, sizeof(buff), _T(".\\CameraInfo.txt"));					
		m_strCamSerial[i] = (CString)buff;
	}
	
}


void CPylonSampleProgramDlg::OnNMClickCameraList(NMHDR *pNMHDR, LRESULT *pResult)
{
	NM_LISTVIEW* pNMListView = (NM_LISTVIEW*)pNMHDR;	
    m_iListIndex = pNMListView->iItem;	   
	SetDlgItemText(IDC_SELECT_CAMERA,m_ctrlCamList.GetItemText(m_iListIndex,1));	
	CString temp;
	if(m_ctrlCamList.GetItemText(pNMListView->iItem,1)==_T("Inspect0"))
	{
         m_iCameraIndex = 0;		 
	}
	else if(m_ctrlCamList.GetItemText(pNMListView->iItem,1)==_T("Inspect1"))
	{      
         m_iCameraIndex = 1;		 
	}
	else if(m_ctrlCamList.GetItemText(pNMListView->iItem,1)==_T("Inspect2"))
	{      
         m_iCameraIndex = 2;		 
	}
	else if(m_ctrlCamList.GetItemText(pNMListView->iItem,1)==_T("Inspect3"))
	{      
         m_iCameraIndex = 3;		 
	}


	*pResult = 0;
}

void CPylonSampleProgramDlg::OnBnClickedOpenCameraBtn()
{
	if (m_iCameraIndex == -1 && m_iCamNumber > 0)
	{
		m_iCameraIndex = 0;
		m_iListIndex = 0;
	}

	if(m_iCameraIndex!=-1)
	{
		int error = m_CameraManager.Open_Camera(m_iCameraIndex, m_iCamPosition[m_iCameraIndex]);
		if(error==0)
		{
			m_ctrlCamList.SetItemText(m_iListIndex,3,_T("Open_Success"));

		}
		else if(error==-1)
		{
			m_ctrlCamList.SetItemText(m_iListIndex,3,_T("Alread_Open"));
		}
		else
		{
			m_ctrlCamList.SetItemText(m_iListIndex,3,_T("Open_Fail"));
		}
	}
	else
	{
		AfxMessageBox(_T("리스트에서 카메라를 먼저 선택하세요!!"));
	}
}
void CPylonSampleProgramDlg::OnBnClickedCloseCamBtn()
{

	
	if(m_CameraManager.m_bCamOpenFlag[m_iCameraIndex] == true)
	{
		if(m_CameraManager.Close_Camera(m_iCameraIndex)==0)
		{
			m_ctrlCamList.SetItemText(m_iListIndex,3,_T("Close_Success"));
 			if(bitmapinfo[m_iCameraIndex])
			{			
	 			delete bitmapinfo[m_iCameraIndex];
				bitmapinfo[m_iCameraIndex]=NULL;
			}
			if(pImageColorDestBuffer[m_iCameraIndex])
			{
				for(int i=0; i<BUF_NUM; i++)
				{
					free(pImageColorDestBuffer[m_iCameraIndex][i]);
				}
				free(pImageColorDestBuffer[m_iCameraIndex]);
				pImageColorDestBuffer[m_iCameraIndex] = NULL;
			}
			if(pImageresizeOrgBuffer[m_iCameraIndex])
			{
				for(int i=0; i<BUF_NUM; i++)
				{
					free(pImageresizeOrgBuffer[m_iCameraIndex][i]);
				}
				free(pImageresizeOrgBuffer[m_iCameraIndex]);
				pImageresizeOrgBuffer[m_iCameraIndex] = NULL;
			}
		}
		else
		{
			m_ctrlCamList.SetItemText(m_iListIndex,3,_T("Close_Fail"));

		}
		
	}
}

void CPylonSampleProgramDlg::OnBnClickedConnectCameraBtn()
{
	// 1. 리스트 컨트롤이 유효한지 확인 (Assertion 방지)
	if (!m_ctrlCamList.GetSafeHwnd()) {
		return;
	}

	// 2. 선택된 아이템이 있는지 확인
	int nIndex = m_ctrlCamList.GetNextItem(-1, LVNI_SELECTED);
	if (nIndex < 0 || nIndex >= CAM_NUM) {
		AfxMessageBox(_T("연결할 카메라를 리스트에서 선택해 주세요."));
		return;
	}

	m_iCameraIndex = nIndex;

	// 3. 카메라 연결 시도
	// 인자값들이 기존에 사용하던 설정값과 맞는지 확인
	int nRet = m_CameraManager.Connect_Camera(m_iCameraIndex, 0, 0, 0, 0, _T("Mono8"));

	if (nRet == 0) // 카메라 연결 성공 시
	{
		m_ctrlCamList.SetItemText(m_iCameraIndex, 3, _T("Connected"));

		// 4. AI 서버 접속 시도 (연결 유지 방식)
		std::string serverIP = "10.10.10.109";
		int serverPort = 8000;

		if (m_CameraManager.ConnectToServer(serverIP, serverPort))
		{
			TRACE(_T("AI Server Connected: %S:%d\n"), serverIP.c_str(), serverPort);
		}
		else
		{
			// 서버가 꺼져있어도 프로그램이 꺼지면 안 되므로 경고만 표시
			TRACE(_T("AI Server Connection Failed. Check Network.\n"));
		}
	}
	else
	{
		m_ctrlCamList.SetItemText(m_iCameraIndex, 3, _T("Connection Fail"));
		AfxMessageBox(_T("카메라 연결에 실패했습니다."));
	}
}

void CPylonSampleProgramDlg::OnBnClickedGrabSingleBtn()
{
	if (m_iCameraIndex < 0 || m_iCameraIndex >= CAM_NUM) return;
	if (m_CameraManager.m_bCamConnectFlag[m_iCameraIndex] == false) return;

	try
	{
		bLiveFlag[m_iCameraIndex] = false;

		// 단일 촬영 실행
		if (m_CameraManager.SingleGrab(m_iCameraIndex) == 0)
		{
			if (m_CameraManager.pImage24Buffer[m_iCameraIndex] != NULL)
			{
				if (pImageColorDestBuffer[m_iCameraIndex] != NULL && pImageColorDestBuffer[m_iCameraIndex][0] != NULL)
				{
					int nRoiSize = 260 * 260 * 3;
					memcpy(pImageColorDestBuffer[m_iCameraIndex][0],
						m_CameraManager.pImage24Buffer[m_iCameraIndex], nRoiSize);

					switch (m_iCameraIndex)
					{
					case 0: DisplayCam0(pImageColorDestBuffer[0][0]); break;
					case 1: DisplayCam1(pImageColorDestBuffer[1][0]); break;
					case 2: DisplayCam2(pImageColorDestBuffer[2][0]); break;
					case 3: DisplayCam3(pImageColorDestBuffer[3][0]); break;
					}

					m_CameraManager.ReadEnd(m_iCameraIndex);
					bLiveFlag[m_iCameraIndex] = true;
				}
			}
		}
	}
	catch (...)
	{
		TRACE(_T("Unknown Exception in GrabSingle Button\n"));
	}
}

void CPylonSampleProgramDlg::AllocImageBuf(void)
{
	UpdateData();

	// 1. 기존 Mono용 버퍼 할당 로직
	if (m_CameraManager.m_strCM_ImageForamt[m_iCameraIndex] == "Mono8" ||
		m_CameraManager.m_strCM_ImageForamt[m_iCameraIndex] == "Mono12" ||
		m_CameraManager.m_strCM_ImageForamt[m_iCameraIndex] == "Mono16")
	{
		pImageresizeOrgBuffer[m_iCameraIndex] = (unsigned char**)malloc(BUF_NUM * sizeof(unsigned char*));
		for (int i = 0; i < BUF_NUM; i++)
		{
			pImageresizeOrgBuffer[m_iCameraIndex][i] = (unsigned char*)malloc(m_CameraManager.m_iCM_reSizeWidth[m_iCameraIndex] * m_CameraManager.m_iCM_Height[m_iCameraIndex]);
		}
	}

	// 2. 전처리된 260x260 컬러 이미지를 담을 버퍼는 '반드시' 할당해야 함
	// 이미지 형식과 상관없이 3채널(RGB) 버퍼를 할당
	if (pImageColorDestBuffer[m_iCameraIndex] == NULL)
	{
		pImageColorDestBuffer[m_iCameraIndex] = (unsigned char**)malloc(BUF_NUM * sizeof(unsigned char*));
		for (int i = 0; i < BUF_NUM; i++)
		{
			pImageColorDestBuffer[m_iCameraIndex][i] = (unsigned char*)malloc(260 * 260 * 3);
			memset(pImageColorDestBuffer[m_iCameraIndex][i], 0, 260 * 260 * 3);
		}
	}

	// Display 핸들 설정
	switch (m_iCameraIndex)
	{
	case 0:
		hWnd[0] = GetDlgItem(IDC_CAM0_DISPLAY)->GetSafeHwnd();
		GetDlgItem(IDC_CAM0_DISPLAY)->GetClientRect(&rectStaticClient[0]);
		hdc[0] = ::GetDC(hWnd[0]);
		break;
	case 1:
		hWnd[1] = GetDlgItem(IDC_CAM1_DISPLAY)->GetSafeHwnd();
		GetDlgItem(IDC_CAM1_DISPLAY)->GetClientRect(&rectStaticClient[1]);
		hdc[1] = ::GetDC(hWnd[1]);
		break;
	case 2:
		hWnd[2] = GetDlgItem(IDC_CAM2_DISPLAY)->GetSafeHwnd();
		GetDlgItem(IDC_CAM2_DISPLAY)->GetClientRect(&rectStaticClient[2]);
		hdc[2] = ::GetDC(hWnd[2]);
		break;
	case 3:
		hWnd[3] = GetDlgItem(IDC_CAM3_DISPLAY)->GetSafeHwnd();
		GetDlgItem(IDC_CAM3_DISPLAY)->GetClientRect(&rectStaticClient[3]);
		hdc[3] = ::GetDC(hWnd[3]);
		break;
	}
}

void CPylonSampleProgramDlg::InitBitmap(int nCamIndex)
{
   if(m_CameraManager.m_strCM_ImageForamt[m_iCameraIndex]=="Mono8" || m_CameraManager.m_strCM_ImageForamt[m_iCameraIndex]=="Mono16") 
   {
		if(bitmapinfo[nCamIndex])
		{			
			delete bitmapinfo[nCamIndex];
			bitmapinfo[nCamIndex]=NULL;
		}	
		bitmapinfo[nCamIndex]=(BITMAPINFO*)(new char[sizeof(BITMAPINFOHEADER)+
					256*sizeof(RGBQUAD)]);
		                    

		bitmapinfo[nCamIndex]->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bitmapinfo[nCamIndex]->bmiHeader.biWidth = (int)m_CameraManager.m_iCM_reSizeWidth[nCamIndex];
		bitmapinfo[nCamIndex]->bmiHeader.biHeight = -(int)m_CameraManager.m_iCM_Height[nCamIndex];
		bitmapinfo[nCamIndex]->bmiHeader.biPlanes = 1;
		bitmapinfo[nCamIndex]->bmiHeader.biCompression = BI_RGB;				
		bitmapinfo[nCamIndex]->bmiHeader.biBitCount = 8;					
		bitmapinfo[nCamIndex]->bmiHeader.biSizeImage = (int)(m_CameraManager.m_iCM_reSizeWidth[nCamIndex]*m_CameraManager.m_iCM_Height[nCamIndex]);        
		bitmapinfo[nCamIndex]->bmiHeader.biXPelsPerMeter = 0;
		bitmapinfo[nCamIndex]->bmiHeader.biYPelsPerMeter = 0;
		bitmapinfo[nCamIndex]->bmiHeader.biClrUsed = 256;
		bitmapinfo[nCamIndex]->bmiHeader.biClrImportant = 0;

		for(int j=0;j<256;j++)
		{
			bitmapinfo[nCamIndex]->bmiColors[j].rgbRed=(unsigned char)j;
			bitmapinfo[nCamIndex]->bmiColors[j].rgbGreen=(unsigned char)j;
			bitmapinfo[nCamIndex]->bmiColors[j].rgbBlue=(unsigned char)j;
			bitmapinfo[nCamIndex]->bmiColors[j].rgbReserved=0;
		}
   }
   else  // Bayer & YUV
   {
			if(bitmapinfo[nCamIndex])
			{			
				delete bitmapinfo[nCamIndex];
				bitmapinfo[nCamIndex]=NULL;
			}	
	
			bitmapinfo[nCamIndex]=(BITMAPINFO*)(new char[sizeof(BITMAPINFOHEADER)+
						256*sizeof(RGBQUAD)]);

			bitmapinfo[nCamIndex]->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
			bitmapinfo[nCamIndex]->bmiHeader.biWidth = (int)m_CameraManager.m_iCM_reSizeWidth[nCamIndex];
			bitmapinfo[nCamIndex]->bmiHeader.biHeight = -(int)m_CameraManager.m_iCM_Height[nCamIndex];
			bitmapinfo[nCamIndex]->bmiHeader.biPlanes = 1;
			bitmapinfo[nCamIndex]->bmiHeader.biCompression = BI_RGB;				
			bitmapinfo[nCamIndex]->bmiHeader.biBitCount = 24;					
			bitmapinfo[nCamIndex]->bmiHeader.biSizeImage = (int)m_CameraManager.m_iCM_reSizeWidth[nCamIndex]*(int)m_CameraManager.m_iCM_Height[nCamIndex]*3;        
			bitmapinfo[nCamIndex]->bmiHeader.biXPelsPerMeter = 0;
			bitmapinfo[nCamIndex]->bmiHeader.biYPelsPerMeter = 0;
			bitmapinfo[nCamIndex]->bmiHeader.biClrUsed = 256;
			bitmapinfo[nCamIndex]->bmiHeader.biClrImportant = 0;
			
			for(int j=0;j<256;j++)
			{
				bitmapinfo[nCamIndex]->bmiColors[j].rgbRed=(unsigned char)j;
				bitmapinfo[nCamIndex]->bmiColors[j].rgbGreen=(unsigned char)j;
				bitmapinfo[nCamIndex]->bmiColors[j].rgbBlue=(unsigned char)j;
				bitmapinfo[nCamIndex]->bmiColors[j].rgbReserved=0;
			}
   }
}

void CPylonSampleProgramDlg::DisplayCam0(void* pImageBuf)
{
	if (pImageBuf == NULL || hdc[0] == NULL) return;

	// 24비트 BGR 이미지에 맞게 헤더 재설정
	bitmapinfo[0]->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bitmapinfo[0]->bmiHeader.biWidth = 260;
	bitmapinfo[0]->bmiHeader.biHeight = -260; // Top-down
	bitmapinfo[0]->bmiHeader.biPlanes = 1;
	bitmapinfo[0]->bmiHeader.biBitCount = 24; // 3채널 컬러
	bitmapinfo[0]->bmiHeader.biCompression = BI_RGB;
	bitmapinfo[0]->bmiHeader.biSizeImage = 260 * 260 * 3;
	bitmapinfo[0]->bmiHeader.biClrUsed = 0;

	SetStretchBltMode(hdc[0], COLORONCOLOR);

	int nRet = StretchDIBits(hdc[0],
		0, 0, rectStaticClient[0].Width(), rectStaticClient[0].Height(),
		0, 0, 260, 260,
		pImageBuf,
		bitmapinfo[0],
		DIB_RGB_COLORS,
		SRCCOPY);

	if (nRet == GDI_ERROR) {
		TRACE(_T("Cam0 Display Error!\n"));
	}
}

void CPylonSampleProgramDlg::DisplayCam1(void* pImageBuf)
{
	if (pImageBuf == NULL || hdc[1] == NULL) return;
	bitmapinfo[1]->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bitmapinfo[1]->bmiHeader.biWidth = 260;
	bitmapinfo[1]->bmiHeader.biHeight = -260;
	bitmapinfo[1]->bmiHeader.biPlanes = 1;
	bitmapinfo[1]->bmiHeader.biBitCount = 24;
	bitmapinfo[1]->bmiHeader.biCompression = BI_RGB;
	bitmapinfo[1]->bmiHeader.biSizeImage = 260 * 260 * 3;
	SetStretchBltMode(hdc[1], COLORONCOLOR);
	StretchDIBits(hdc[1], 0, 0, rectStaticClient[1].Width(), rectStaticClient[1].Height(), 0, 0, 260, 260, pImageBuf, bitmapinfo[1], DIB_RGB_COLORS, SRCCOPY);
}

void CPylonSampleProgramDlg::DisplayCam2(void* pImageBuf)
{
	if (pImageBuf == NULL || hdc[2] == NULL) return;
	bitmapinfo[2]->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bitmapinfo[2]->bmiHeader.biWidth = 260;
	bitmapinfo[2]->bmiHeader.biHeight = -260;
	bitmapinfo[2]->bmiHeader.biPlanes = 1;
	bitmapinfo[2]->bmiHeader.biBitCount = 24;
	bitmapinfo[2]->bmiHeader.biCompression = BI_RGB;
	bitmapinfo[2]->bmiHeader.biSizeImage = 260 * 260 * 3;
	SetStretchBltMode(hdc[2], COLORONCOLOR);
	StretchDIBits(hdc[2], 0, 0, rectStaticClient[2].Width(), rectStaticClient[2].Height(), 0, 0, 260, 260, pImageBuf, bitmapinfo[2], DIB_RGB_COLORS, SRCCOPY);
}

void CPylonSampleProgramDlg::DisplayCam3(void* pImageBuf)
{
	if (pImageBuf == NULL || hdc[3] == NULL) return;
	bitmapinfo[3]->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bitmapinfo[3]->bmiHeader.biWidth = 260;
	bitmapinfo[3]->bmiHeader.biHeight = -260;
	bitmapinfo[3]->bmiHeader.biPlanes = 1;
	bitmapinfo[3]->bmiHeader.biBitCount = 24;
	bitmapinfo[3]->bmiHeader.biCompression = BI_RGB;
	bitmapinfo[3]->bmiHeader.biSizeImage = 260 * 260 * 3;
	SetStretchBltMode(hdc[3], COLORONCOLOR);
	StretchDIBits(hdc[3], 0, 0, rectStaticClient[3].Width(), rectStaticClient[3].Height(), 0, 0, 260, 260, pImageBuf, bitmapinfo[3], DIB_RGB_COLORS, SRCCOPY);
}

void CPylonSampleProgramDlg::OnBnClickedCam0Live()
{
	if (m_CameraManager.m_bCamConnectFlag[0] == true)
	{
		// 토글 로직 (0 -> 1, 1 -> 0)
		bStopThread[0] = !bStopThread[0];

		if (bStopThread[0])
		{
			bLiveFlag[0] = true;
			m_CameraManager.GrabLive(0, 0);
			SetDlgItemText(IDC_CAM0_LIVE, _T("Live_Cam0_Stop"));

			// 인덱스 버퍼 설정 후 스레드 시작
			m_nCamIndexBuf[0] = 0;
			AfxBeginThread(LiveGrabThreadCam0, &m_nCamIndexBuf[0]);
		}
		else
		{
			bLiveFlag[0] = false;
			SetDlgItemText(IDC_CAM0_LIVE, _T("Live_Cam0_Start"));
			m_CameraManager.LiveStop(0, 0);
		}
	}
	else
	{
		AfxMessageBox(_T("Camera0 Connect를 먼저 하세요!!"));
		CButton* pButton = (CButton*)GetDlgItem(IDC_CAM0_LIVE);
		if (pButton) pButton->SetCheck(0);
	}
}

void CPylonSampleProgramDlg::OnBnClickedCam1Live()
{

	if(m_CameraManager.m_bCamConnectFlag[1] == true)
	{
		 bStopThread[1]=(bStopThread[1]+1)&0x01;   
		 if(bStopThread[1])
		 {
			bLiveFlag[1] = true;
			m_CameraManager.GrabLive(1,0);
			SetDlgItemText(IDC_CAM1_LIVE,_T("Live_Cam1_Stop"));
			AfxBeginThread(LiveGrabThreadCam0,&m_nCamIndexBuf[1]);
		 }
		 else
		 {
			bLiveFlag[1] = false;
			SetDlgItemText(IDC_CAM1_LIVE,_T("Live_Cam1_Start"));
			m_CameraManager.LiveStop(1,0);			
		 }	
	}
	else
	{
		AfxMessageBox(_T("Camera1 Connect를 하세요!!"));
		CButton *pButton = (CButton*)pMainDlg->GetDlgItem(IDC_CAM1_LIVE);
		pButton->SetCheck(0);

	}
}
void CPylonSampleProgramDlg::OnBnClickedCam2Live()
{

	if(m_CameraManager.m_bCamConnectFlag[2] == true)
	{
		 bStopThread[2]=(bStopThread[2]+1)&0x01;   
		 if(bStopThread[2])
		 {
			bLiveFlag[2] = true;
			m_CameraManager.GrabLive(2,0);
			SetDlgItemText(IDC_CAM2_LIVE,_T("Live_Cam2_Stop"));
			AfxBeginThread(LiveGrabThreadCam0,&m_nCamIndexBuf[2]);
		 }
		 else
		 {
			bLiveFlag[2] = false;
			SetDlgItemText(IDC_CAM2_LIVE,_T("Live_Cam2_Start"));
			m_CameraManager.LiveStop(2,0);			
		 }	
	}
	else
	{
		AfxMessageBox(_T("Camera2 Connect를 하세요!!"));
		CButton *pButton = (CButton*)pMainDlg->GetDlgItem(IDC_CAM2_LIVE);
		pButton->SetCheck(0);

	}
}

void CPylonSampleProgramDlg::OnBnClickedCam3Live()
{

	if(m_CameraManager.m_bCamConnectFlag[3] == true)
	{
		 bStopThread[3]=(bStopThread[3]+1)&0x01;   
		 if(bStopThread[3])
		 {
			bLiveFlag[3] = true;
			m_CameraManager.GrabLive(3,0);
			SetDlgItemText(IDC_CAM3_LIVE,_T("Live_Cam3_Stop"));
			AfxBeginThread(LiveGrabThreadCam0,&m_nCamIndexBuf[3]);
		 }
		 else
		 {
			bLiveFlag[3] = false;
			SetDlgItemText(IDC_CAM3_LIVE,_T("Live_Cam3_Start"));
			m_CameraManager.LiveStop(3,0);			
		 }	
	}
	else
	{
		AfxMessageBox(_T("Camera3 Connect를 하세요!!"));
		CButton *pButton = (CButton*)pMainDlg->GetDlgItem(IDC_CAM3_LIVE);
		pButton->SetCheck(0);

	}
}

void CPylonSampleProgramDlg::OnBnClickedTwoCameraLiveBtn()
{
	if(m_CameraManager.m_bCamConnectFlag[0] == true)
	{
		 bStopThread[0]=(bStopThread[0]+1)&0x01;   
		 if(bStopThread[0])
		 {
			bLiveFlag[0] = true;
			m_CameraManager.GrabLive(0,1);
			SetDlgItemText(IDC_CAM0_LIVE,_T("Live_Cam0_Stop"));
			AfxBeginThread(LiveGrabThreadCam0,&m_nCamIndexBuf[0]);
		 }
		 else
		 {
			bLiveFlag[0] = false;
			SetDlgItemText(IDC_CAM0_LIVE,_T("Live_Cam0_Start"));
			//m_CameraManager.m_pCamera[0].StopGrabbing();				
			m_CameraManager.LiveStop(0,1);
		 }	
	}
	else
	{
		AfxMessageBox(_T("Camera0 Connect를 하세요!!"));
		CButton *pButton = (CButton*)pMainDlg->GetDlgItem(IDC_CAM0_LIVE);
		pButton->SetCheck(0);
	}	
	if(m_CameraManager.m_bCamConnectFlag[1] == true)
	{
		 bStopThread[1]=(bStopThread[1]+1)&0x01;   
		 if(bStopThread[1])
		 {
			bLiveFlag[1] = true;
			//m_CameraManager.GrabLive(1);
			SetDlgItemText(IDC_CAM1_LIVE,_T("Live_Cam1_Stop"));
			AfxBeginThread(LiveGrabThreadCam0,&m_nCamIndexBuf[1]);
		 }
		 else
		 {
			bLiveFlag[1] = false;
			SetDlgItemText(IDC_CAM1_LIVE,_T("Live_Cam1_Start"));
			//m_CameraManager.m_pCamera[1].StopGrabbing();				
		 }	
	}
	else
	{
		AfxMessageBox(_T("Camera1 Connect를 하세요!!"));
		CButton *pButton = (CButton*)pMainDlg->GetDlgItem(IDC_CAM1_LIVE);
		pButton->SetCheck(0);

	}
}

/*
      Software Trigger Execute
*/
void CPylonSampleProgramDlg::OnBnClickedSoftTrigBtn()
{
	m_CameraManager.SetCommand(m_iCameraIndex,"TriggerSoftware");
}

// close 카메라 추가해야 됨 2/5일
void CPylonSampleProgramDlg::OnBnClickedExitBtn()
{
	// 1. AI 서버 연결 종료 (추가)
	// 통신 중인 소켓을 먼저 닫아 서버 측의 자원을 정리
	if (m_CameraManager.m_bIsConnected)
	{
		m_CameraManager.DisconnectFromServer();
	}

	// 2. 기존 종료 및 자원 해제 로직 (유지)
	for (int k = 0; k < CAM_NUM; k++)
	{
		// 스레드 중지 플래그 설정 및 라이브 정지
		bStopThread[k] = false;
		m_CameraManager.LiveStop(k, 0);

		// 비트맵 정보 메모리 해제
		if (bitmapinfo[k])
		{
			delete bitmapinfo[k];
			bitmapinfo[k] = NULL;
		}

		// 컬러 데스트 버퍼 메모리 해제
		if (pImageColorDestBuffer[k])
		{
			for (int i = 0; i < BUF_NUM; i++)
			{
				if (pImageColorDestBuffer[k][i])
				{
					free(pImageColorDestBuffer[k][i]);
				}
			}
			free(pImageColorDestBuffer[k]);
			pImageColorDestBuffer[k] = NULL;
		}

		// 리사이즈 원본 버퍼 메모리 해제
		if (pImageresizeOrgBuffer[k])
		{
			for (int i = 0; i < BUF_NUM; i++)
			{
				if (pImageresizeOrgBuffer[k][i])
				{
					free(pImageresizeOrgBuffer[k][i]);
				}
			}
			free(pImageresizeOrgBuffer[k]);
			pImageresizeOrgBuffer[k] = NULL;
		}

		// 카메라 장치 닫기
		if (m_CameraManager.m_bCamOpenFlag[k] == true)
		{
			m_CameraManager.Close_Camera(k);
		}
	}

	// 모든 정리가 끝나면 대화 상자 종료
	CDialog::OnOK();
}


void CPylonSampleProgramDlg::OnBnClickedSaveImgBtn()
{
	// 1. 현재 선택된 카메라 인덱스 사용 (m_iCameraIndex)
	int nCam = m_iCameraIndex;

	// 2. 저장 경로 설정 및 폴더 생성 체크
	CString strPath = _T("C:\\Temp");
	if (!PathFileExists(strPath))
	{
		CreateDirectory(strPath, NULL); // 폴더가 없으면 생성
	}

	// 파일명을 매번 다르게 저장할 수 있도록 시스템 시간 등을 섞어줌
	// 일단 테스트를 위해 고정 경로를 사용하되, char* 변환을 안전하게 수행
	char szFileName[256];
	sprintf_s(szFileName, "C:\\Temp\\MetalGuard_Cam%d.bmp", nCam);

	// 3. 버퍼 체크 및 저장 호출
	if (m_CameraManager.pImage24Buffer[nCam] != NULL)
	{
		int nResult = m_CameraManager.SaveImage(
			0,                                      // 0: BMP 포맷
			m_CameraManager.pImage24Buffer[nCam],    // 전처리된 260x260 BGR 버퍼
			szFileName,                             // 저장 파일 풀 경로
			1,                                      // 1: BGR 타입 (내부에서 고정하므로 의미는 적음)
			260,                                    // Width
			260,                                    // Height
			3                                       // ColorBand (3채널)
		);

		if (nResult == 0) {
			AfxMessageBox(_T("이미지 저장 성공! (C:\\Temp)"));
		}
		else {
			// 실패 시 원인은 TRACE 창(출력 창)을 확인해야 함
			AfxMessageBox(_T("이미지 저장 실패! 출력창의 RuntimeException 내용을 확인하세요."));
		}
	}
	else
	{
		AfxMessageBox(_T("저장할 이미지 버퍼가 비어있습니다. 먼저 촬영(Grab)을 진행하세요."));
	}
}

// 현재 테스트 중입니다.


void CPylonSampleProgramDlg::OnBnClickedButton5()
{
	static bool bTrig = false;
	if(bTrig==false)
	{
		SetDlgItemText(IDC_BUTTON5, _T("Trigger_해제"));
		bTrig = true;
		// Triggr selctor
		m_CameraManager.SetEnumeration(m_iCameraIndex, "FrameStart", "TriggerSelector");
		// Trigger Mode
		m_CameraManager.SetEnumeration(m_iCameraIndex, "On", "TriggerMode");
		// Trigger Source
		m_CameraManager.SetEnumeration(m_iCameraIndex, "Software", "TriggerSource");
	}
	else
	{
		SetDlgItemText(IDC_BUTTON5, _T("Trigger_설정"));
		bTrig = false;
// 		// Triggr selctor
// 		m_CameraManager.SetEnumeration(m_iCameraIndex, "FrameStart", "TriggerSelector");
		// Trigger Mode
		m_CameraManager.SetEnumeration(m_iCameraIndex, "Off", "TriggerMode");
// 		//Trigger Source
// 		m_CameraManager.SetEnumeration(m_iCameraIndex, "Software", "TriggerSource");
	}

}

void CPylonSampleProgramDlg::OnNMClickListCam(NMHDR* pNMHDR, LRESULT* pResult)
{
	LPNMITEMACTIVATE pNMItemActivate = reinterpret_cast<LPNMITEMACTIVATE>(pNMHDR);
	int nItem = pNMItemActivate->iItem;

	if (nItem != -1)
	{
		m_iListIndex = nItem;
		m_iCameraIndex = nItem;

		// 클릭이 되는지 확인하는 로그
		CString str;
		str.Format(_T("선택된 인덱스: %d"), m_iCameraIndex);
		AfxMessageBox(str);
	}
	*pResult = 0;
}

