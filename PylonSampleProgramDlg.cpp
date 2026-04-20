// PylonSampleProgramDlg.cpp : 구현 파일
//

#include "stdafx.h"
#include "PylonSampleProgram.h"
#include "PylonSampleProgramDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif


CPylonSampleProgramDlg *pMainDlg;
UINT LiveGrabThreadCam0(LPVOID pParam)
{
	int nindex = 0;
	int nCamIndex = *(int*)pParam;

	// 프레임 측정을 위한 타이머 초기화
	QueryPerformanceFrequency(&(pMainDlg->freq[nCamIndex]));
	QueryPerformanceCounter(&(pMainDlg->start[nCamIndex]));
	pMainDlg->nFrameCount[nCamIndex] = 0;
	CString Info;

	while (pMainDlg->bStopThread[nCamIndex] == true)
	{
		if (pMainDlg->m_CameraManager.m_bRemoveCamera[nCamIndex] == true)
		{
			pMainDlg->m_CameraManager.m_bRemoveCamera[nCamIndex] = false;
			pMainDlg->m_ctrlCamList.SetItemText(nCamIndex, 3, _T("LostConnection"));
		}
		else
		{
			if (pMainDlg->m_CameraManager.CheckCaptureEnd(nCamIndex))
			{
				pMainDlg->nFrameCount[nCamIndex]++;
				QueryPerformanceCounter(&(pMainDlg->end[nCamIndex]));

				// [수정 지점] pImageColorDestBuffer가 NULL인지, nindex가 유효한지 철저히 검사
				if (pMainDlg->m_CameraManager.pImage24Buffer[nCamIndex] != NULL)
				{
					int nRoiSize = 260 * 260 * 3;

					// 1. pImageColorDestBuffer[nCamIndex] 배열 자체가 할당되었는지 확인
					// 2. nindex가 BUF_NUM 범위 내에 있는지 확인
					// 3. 해당 칸의 메모리가 NULL이 아닌지 확인
					if (pMainDlg->pImageColorDestBuffer[nCamIndex] != NULL &&
						nindex >= 0 && nindex < BUF_NUM &&
						pMainDlg->pImageColorDestBuffer[nCamIndex][nindex] != NULL)
					{
						memcpy(pMainDlg->pImageColorDestBuffer[nCamIndex][nindex],
							pMainDlg->m_CameraManager.pImage24Buffer[nCamIndex], nRoiSize);

						pMainDlg->m_CameraManager.ReadEnd(nCamIndex);

						switch (nCamIndex)
						{
						case 0: pMainDlg->DisplayCam0(pMainDlg->pImageColorDestBuffer[0][nindex]); break;
						case 1: pMainDlg->DisplayCam1(pMainDlg->pImageColorDestBuffer[1][nindex]); break;
						case 2: pMainDlg->DisplayCam2(pMainDlg->pImageColorDestBuffer[2][nindex]); break;
						case 3: pMainDlg->DisplayCam3(pMainDlg->pImageColorDestBuffer[3][nindex]); break;
						}
					}
					else
					{
						// 버퍼가 준비되지 않았다면 상태만 초기화하고 넘어감
						pMainDlg->m_CameraManager.ReadEnd(nCamIndex);
					}
				}

				nindex++;
				if (nindex >= BUF_NUM) nindex = 0;

				double dElapsed = (double)(pMainDlg->end[nCamIndex].QuadPart - pMainDlg->start[nCamIndex].QuadPart) / pMainDlg->freq[nCamIndex].QuadPart;
				if (dElapsed > 1.0)
				{
					CString temp;
					temp.Format(_T("%d fps"), pMainDlg->nFrameCount[nCamIndex]);
					pMainDlg->SetDlgItemText(IDC_CAMERA0_STATS + nCamIndex, temp);
					pMainDlg->nFrameCount[nCamIndex] = 0;
					QueryPerformanceCounter(&(pMainDlg->start[nCamIndex]));
				}

				Info.Format(_T("Grabbed Frame = %d , SkippedFrame = %d"),
					pMainDlg->m_CameraManager.m_iGrabbedFrame[nCamIndex],
					pMainDlg->m_CameraManager.m_iSkippiedFrame[nCamIndex]);

				switch (nCamIndex)
				{
				case 0: pMainDlg->SetDlgItemText(IDC_CAM0_INFO, Info); break;
				case 1: pMainDlg->SetDlgItemText(IDC_CAM1_INFO, Info); break;
				case 2: pMainDlg->SetDlgItemText(IDC_CAM2_INFO, Info); break;
				case 3: pMainDlg->SetDlgItemText(IDC_CAM3_INFO, Info); break;
				}
			}
		}
		Sleep(1);
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

	// 시스템 메뉴 및 아이콘 설정 (기존 로직)
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

	// --- 2. [핵심 수정] 이미지 버퍼(pImageColorDestBuffer) 메모리 할당 ---
	// 이 부분이 누락되어 스레드에서 예외가 발생했던 것입니다.
	for (int i = 0; i < CAM_NUM; i++)
	{
		// 1단계: 각 카메라당 BUF_NUM(3)개의 포인터를 담을 배열 할당
		pImageColorDestBuffer[i] = new unsigned char* [BUF_NUM];

		for (int j = 0; j < BUF_NUM; j++)
		{
			// 2단계: 각 프레임이 저장될 실제 메모리 할당 (260x260x3 BGR)
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

// 대화 상자에 최소화 단추를 추가할 경우 아이콘을 그리려면
//  아래 코드가 필요합니다. 문서/뷰 모델을 사용하는 MFC 응용 프로그램의 경우에는
//  프레임워크에서 이 작업을 자동으로 수행합니다.

void CPylonSampleProgramDlg::OnPaint()
{
	if (IsIconic())
	{
		CPaintDC dc(this); // 그리기를 위한 디바이스 컨텍스트

		SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

		// 클라이언트 사각형에서 아이콘을 가운데에 맞춥니다.
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		// 아이콘을 그립니다.
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CDialog::OnPaint();
	}
}

// 사용자가 최소화된 창을 끄는 동안에 커서가 표시되도록 시스템에서
//  이 함수를 호출합니다.
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

			// ==========================================================
			// [민기용 디버깅 로그] 실제 인식된 시리얼 번호를 확인합니다.
			// 만약 리스트에 아무것도 안 뜬다면, 아래 창에 뜨는 번호와 
			// m_strCamSerial[0] 등의 값이 달라서 발생하는 문제입니다.
			// ==========================================================
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

			// ==========================================================
			// [임시 강제추가 로직] 
			// 만약 시리얼 번호가 불일치하더라도 리스트에 띄워서 실행 가능하게 함
			// ==========================================================
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
	// 인자값들이 기존에 사용하던 설정값과 맞는지 확인하세요.
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
	if (m_CameraManager.m_bCamConnectFlag[m_iCameraIndex] == true)
	{
		bLiveFlag[m_iCameraIndex] = false;

		if (m_CameraManager.SingleGrab(m_iCameraIndex) == 0)
		{
			while (bLiveFlag[m_iCameraIndex] == false)
			{
				if (m_CameraManager.CheckCaptureEnd(m_iCameraIndex))
				{
					// [민기 파트] 전처리된 260x260 이미지 복사
					if (m_CameraManager.pImage24Buffer[m_iCameraIndex] != NULL)
					{
						// [안전장치] 만약 버퍼 할당이 안되어 있다면 여기서라도 수행
						if (pImageColorDestBuffer[m_iCameraIndex] == NULL) {
							AllocImageBuf();
						}

						int nRoiSize = 260 * 260 * 3;
						// pImageColorDestBuffer가 NULL이 아니므로 더 이상 중단점이 걸리지 않습니다.
						memcpy(pImageColorDestBuffer[m_iCameraIndex][0], m_CameraManager.pImage24Buffer[m_iCameraIndex], nRoiSize);

						m_CameraManager.ReadEnd(m_iCameraIndex);

						// 화면 출력
						switch (m_iCameraIndex)
						{
						case 0: DisplayCam0(pImageColorDestBuffer[0][0]); break;
						case 1: DisplayCam1(pImageColorDestBuffer[1][0]); break;
						case 2: DisplayCam2(pImageColorDestBuffer[2][0]); break;
						case 3: DisplayCam3(pImageColorDestBuffer[3][0]); break;
						}
					}

					bLiveFlag[m_iCameraIndex] = true;
				}
			}
		}
	}
}

void CPylonSampleProgramDlg::AllocImageBuf(void)
{
	UpdateData();

	// 1. 기존 Mono용 버퍼 할당 로직 (기존 유지)
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

	// 2. [민기 수정] 전처리된 260x260 컬러 이미지를 담을 버퍼는 '반드시' 할당해야 함
	// 이미지 형식과 상관없이 3채널(RGB) 버퍼를 할당합니다.
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

// 1, 2, 3번 카메라도 동일한 로직으로 수정 (생략 없이 적용하세요)
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
	// 통신 중인 소켓을 먼저 닫아 서버 측의 자원을 정리합니다.
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
	CString t =_T("test1.bmp");
    if(m_CameraManager.m_strCM_ImageForamt[m_iCameraIndex]=="Mono8")
	{
		 //int nFileFormat(0 = bmp, 1=tiff, 2=jpeg, 3= png)
		 //unsigned char* pImage, char *filename
		 //int nPixelType( 0 = mono8 , 1= Color),int width, int height,int nColorband(1,3)		                                       
		m_CameraManager.SaveImage(0,m_CameraManager.pImage8Buffer[m_iCameraIndex],CT2A(t),0,m_CameraManager.m_iCM_Width[m_iCameraIndex],m_CameraManager.m_iCM_Height[m_iCameraIndex],1);
	}
	else
	{
		m_CameraManager.SaveImage(0,m_CameraManager.pImage24Buffer[m_iCameraIndex],CT2A(t),1,m_CameraManager.m_iCM_Width[m_iCameraIndex],m_CameraManager.m_iCM_Height[m_iCameraIndex],3);
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

		// [추가] 클릭이 되는지 확인하는 로그
		CString str;
		str.Format(_T("선택된 인덱스: %d"), m_iCameraIndex);
		AfxMessageBox(str);
	}
	*pResult = 0;
}

