#include "stdafx.h"
#include "PylonSampleProgram.h"
#include "PylonSampleProgramDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

HWND g_hMainWnd = NULL;

CPylonSampleProgramDlg *pMainDlg;
UINT LiveGrabThreadCam0(LPVOID pParam)
{
	int nCamIndex = *(int*)pParam;
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

	while (pMainDlg->bStopThread[nCamIndex] == true)
	{
		if (pMainDlg->m_CameraManager.m_bRemoveCamera[nCamIndex] == true) break;

		if (pMainDlg->m_CameraManager.CheckCaptureEnd(nCamIndex))
		{
			pMainDlg->nFrameCount[nCamIndex]++;

			CWnd* pWnd = pMainDlg->GetDlgItem(IDC_CAM0_DISPLAY);
			if (pWnd && !pMainDlg->m_CameraManager.m_matLiveImage[nCamIndex].empty())
			{
				CRect rect;
				pWnd->GetClientRect(&rect);
				CDC* pDC = pWnd->GetDC();

				// --- 더블 버퍼링 (깜빡임 방지) ---
				CDC memDC;
				CBitmap memBitmap;
				memDC.CreateCompatibleDC(pDC);
				memBitmap.CreateCompatibleBitmap(pDC, rect.Width(), rect.Height());
				CBitmap* pOldBitmap = memDC.SelectObject(&memBitmap);

				memDC.FillSolidRect(&rect, RGB(0, 0, 0));

				cv::Mat& matRaw = pMainDlg->m_CameraManager.m_matLiveImage[nCamIndex];
				int imgW = matRaw.cols;
				int imgH = matRaw.rows;

				float fImgAspect = (float)imgW / imgH;
				float fWinAspect = (float)rect.Width() / rect.Height();
				int drawW, drawH, offsetX = 0, offsetY = 0;

				if (fImgAspect > fWinAspect) {
					drawW = rect.Width(); drawH = (int)(drawW / fImgAspect);
					offsetY = (rect.Height() - drawH) / 2;
				}
				else {
					drawH = rect.Height(); drawW = (int)(drawH * fImgAspect);
					offsetX = (rect.Width() - drawW) / 2;
				}

				BITMAPINFO bitInfo;
				memset(&bitInfo, 0, sizeof(BITMAPINFO));
				bitInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
				bitInfo.bmiHeader.biWidth = imgW;
				bitInfo.bmiHeader.biHeight = -imgH;
				bitInfo.bmiHeader.biPlanes = 1;
				bitInfo.bmiHeader.biBitCount = 24;
				bitInfo.bmiHeader.biCompression = BI_RGB;

				memDC.SetStretchBltMode(COLORONCOLOR);
				::StretchDIBits(memDC.GetSafeHdc(), offsetX, offsetY, drawW, drawH,
					0, 0, imgW, imgH, matRaw.data, &bitInfo, DIB_RGB_COLORS, SRCCOPY);

				pDC->BitBlt(0, 0, rect.Width(), rect.Height(), &memDC, 0, 0, SRCCOPY);

				// --- 자동 재분류 로직 ---
				bool bTriggerReclassify = false;
				if (bTriggerReclassify && pMainDlg->m_CameraManager.m_bIsServerConnected)
				{
					AsyncSendParam* pData = new AsyncSendParam;
					pData->pMgr = &pMainDlg->m_CameraManager;
					pData->matImage = matRaw.clone();
					pData->nPlateId = pMainDlg->m_nPlateId;
					pData->nShotIdx = 1;
					pData->cmd = CmdID::IMG_RECLASSIFY;
					AfxBeginThread(CCameraManager::ThreadAsyncSend, pData);
				}

				memDC.SelectObject(pOldBitmap);
				memBitmap.DeleteObject();
				memDC.DeleteDC();
				pWnd->ReleaseDC(pDC);
			}
			pMainDlg->m_CameraManager.ReadEnd(nCamIndex);
		}
		Sleep(10);
	}
	return 0;
}

// CAboutDlg 대화 상자
class CAboutDlg : public CDialog
{
public:
	CAboutDlg();
	enum { IDD = IDD_ABOUTBOX };
protected:
	virtual void DoDataExchange(CDataExchange* pDX);
	DECLARE_MESSAGE_MAP()
};

CAboutDlg::CAboutDlg() : CDialog(CAboutDlg::IDD) {}

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
	m_nDBRowCount = 0;
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
	DDX_Control(pDX, IDC_LIST_LOG,    m_listLog);
	DDX_Control(pDX, IDC_DB_LIST,     m_listDB);    // ← 신규
}

BEGIN_MESSAGE_MAP(CPylonSampleProgramDlg, CDialog)
	ON_WM_SYSCOMMAND()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_BN_CLICKED(IDC_FIND_CAM_BTN,       &CPylonSampleProgramDlg::OnBnClickedFindCamBtn)
	ON_NOTIFY(NM_CLICK, IDC_CAMERA_LIST,  &CPylonSampleProgramDlg::OnNMClickCameraList)
	ON_BN_CLICKED(IDC_OPEN_CAMERA_BTN,    &CPylonSampleProgramDlg::OnBnClickedOpenCameraBtn)
	ON_BN_CLICKED(IDC_CONNECT_CAMERA_BTN, &CPylonSampleProgramDlg::OnBnClickedConnectCameraBtn)
	ON_BN_CLICKED(IDC_GRAB_SINGLE_BTN,    &CPylonSampleProgramDlg::OnBnClickedGrabSingleBtn)
	ON_BN_CLICKED(IDC_CAM0_LIVE,          &CPylonSampleProgramDlg::OnBnClickedCam0Live)
	ON_BN_CLICKED(IDC_CLOSE_CAM_BTN,      &CPylonSampleProgramDlg::OnBnClickedCloseCamBtn)
	ON_BN_CLICKED(IDC_RECLASSIFY_BTN,     &CPylonSampleProgramDlg::OnBnClickedReclassifyBtn)
	ON_BN_CLICKED(IDC_SOFT_TRIG_BTN,      &CPylonSampleProgramDlg::OnBnClickedSoftTrigBtn)
	ON_BN_CLICKED(IDC_EXIT_BTN,           &CPylonSampleProgramDlg::OnBnClickedExitBtn)
	ON_BN_CLICKED(IDC_CAM1_LIVE,          &CPylonSampleProgramDlg::OnBnClickedCam1Live)
	ON_BN_CLICKED(IDC_SAVE_IMG_BTN,       &CPylonSampleProgramDlg::OnBnClickedSaveImgBtn)
	ON_BN_CLICKED(IDC_BUTTON5,            &CPylonSampleProgramDlg::OnBnClickedButton5)
	ON_BN_CLICKED(IDC_TWO_CAMERA_LIVE_BTN,&CPylonSampleProgramDlg::OnBnClickedTwoCameraLiveBtn)
	ON_BN_CLICKED(IDC_CAM2_LIVE,          &CPylonSampleProgramDlg::OnBnClickedCam2Live)
	ON_BN_CLICKED(IDC_CAM3_LIVE,          &CPylonSampleProgramDlg::OnBnClickedCam3Live)
	ON_MESSAGE(WM_UPDATE_LOG,     &CPylonSampleProgramDlg::OnUpdateLog)
	ON_MESSAGE(WM_UPDATE_VERDICT, &CPylonSampleProgramDlg::OnUpdateVerdict)  // ← 신규
	ON_CBN_SELCHANGE(IDC_MODE_COMBO, &CPylonSampleProgramDlg::OnCbnSelchangeModeCombo) // ← 신규
END_MESSAGE_MAP()


// CPylonSampleProgramDlg 메시지 처리기

BOOL CPylonSampleProgramDlg::OnInitDialog()
{
	CDialog::OnInitDialog();

	g_hMainWnd = m_hWnd;

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

	// --- 1. 카메라 리스트 컨트롤 초기화 ---
	m_ctrlCamList.InsertColumn(0, _T("모델명"),    LVCFMT_CENTER, 130, -1);
	m_ctrlCamList.InsertColumn(1, _T("Position"), LVCFMT_CENTER, 80,  -1);
	m_ctrlCamList.InsertColumn(2, _T("SerialNum"),LVCFMT_CENTER, 90,  -1);
	m_ctrlCamList.InsertColumn(3, _T("Stats"),    LVCFMT_CENTER, 150, -1);
	m_ctrlCamList.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES |
	                               LVS_EX_ONECLICKACTIVATE | LVS_EX_HEADERDRAGDROP);

	// --- 2. DB 기록 리스트 컨트롤 초기화 (신규) ---
	m_listDB.InsertColumn(0,  _T("ID"),           LVCFMT_CENTER, 40);
	m_listDB.InsertColumn(1,  _T("타임스탬프"),    LVCFMT_CENTER, 118);
	m_listDB.InsertColumn(2,  _T("plate_id"),      LVCFMT_CENTER, 55);
	m_listDB.InsertColumn(3,  _T("최종 판정"),     LVCFMT_CENTER, 65);
	m_listDB.InsertColumn(4,  _T("Normal%"),       LVCFMT_CENTER, 55);
	m_listDB.InsertColumn(5,  _T("Crack%"),        LVCFMT_CENTER, 50);
	m_listDB.InsertColumn(6,  _T("Hole%"),         LVCFMT_CENTER, 50);
	m_listDB.InsertColumn(7,  _T("Rust%"),         LVCFMT_CENTER, 50);
	m_listDB.InsertColumn(8,  _T("Scratch%"),      LVCFMT_CENTER, 55);
	m_listDB.InsertColumn(9,  _T("추론(ms)"),      LVCFMT_CENTER, 55);
	m_listDB.InsertColumn(10, _T("파이프라인(ms)"),LVCFMT_CENTER, 72);
	m_listDB.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

	// --- 3. 운영 모드 콤보박스 초기화 (신규) ---
	CComboBox* pCombo = (CComboBox*)GetDlgItem(IDC_MODE_COMBO);
	if (pCombo)
	{
		pCombo->AddString(_T("실시간 검출"));
		pCombo->AddString(_T("UNCERTAIN 재분류"));
		pCombo->SetCurSel(0);
	}

	// --- 4. 이미지 버퍼 메모리 할당 ---
	for (int i = 0; i < CAM_NUM; i++)
	{
		pImageColorDestBuffer[i] = new unsigned char* [BUF_NUM];
		for (int j = 0; j < BUF_NUM; j++)
		{
			size_t nFrameSize = 260 * 260 * 3;
			pImageColorDestBuffer[i][j] = new unsigned char[nFrameSize];
			memset(pImageColorDestBuffer[i][j], 0, nFrameSize);
		}
	}

	// --- 5. 화면 출력용 HDC 및 영역 초기화 ---
	UINT nStaticIDs[] = { IDC_CAM0_DISPLAY, IDC_CAM1_DISPLAY, IDC_CAM2_DISPLAY, IDC_CAM3_DISPLAY };
	for (int i = 0; i < CAM_NUM; i++)
	{
		CWnd* pWnd = GetDlgItem(nStaticIDs[i]);
		if (pWnd)
		{
			pWnd->GetClientRect(&rectStaticClient[i]);
			hdc[i] = pWnd->GetDC()->GetSafeHdc();
		}
		if (bitmapinfo[i] == NULL)
		{
			bitmapinfo[i] = (BITMAPINFO*)new BYTE[sizeof(BITMAPINFO)];
			memset(bitmapinfo[i], 0, sizeof(BITMAPINFO));
			bitmapinfo[i]->bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
			bitmapinfo[i]->bmiHeader.biWidth        = 260;
			bitmapinfo[i]->bmiHeader.biHeight       = -260;
			bitmapinfo[i]->bmiHeader.biPlanes        = 1;
			bitmapinfo[i]->bmiHeader.biBitCount      = 24;
			bitmapinfo[i]->bmiHeader.biCompression   = BI_RGB;
		}
	}

	pMainDlg = this;

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
		CPaintDC dc(this);
		SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width()  - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CPaintDC dc(this);
		CDialog::OnPaint();

		if (!m_CameraManager.m_matLiveImage[0].empty())
		{
			CWnd* pWnd = GetDlgItem(IDC_CAM0_DISPLAY);
			if (pWnd)
			{
				CRect rect;
				pWnd->GetClientRect(&rect);
				CDC* pDC = pWnd->GetDC();

				cv::Mat matResized;
				cv::resize(m_CameraManager.m_matLiveImage[0], matResized,
				           cv::Size(rect.Width(), rect.Height()));

				BITMAPINFO bitInfo;
				bitInfo.bmiHeader.biBitCount      = 24;
				bitInfo.bmiHeader.biWidth          = matResized.cols;
				bitInfo.bmiHeader.biHeight         = -matResized.rows;
				bitInfo.bmiHeader.biPlanes          = 1;
				bitInfo.bmiHeader.biSize            = sizeof(BITMAPINFOHEADER);
				bitInfo.bmiHeader.biCompression     = BI_RGB;
				bitInfo.bmiHeader.biClrImportant    = 0;
				bitInfo.bmiHeader.biClrUsed         = 0;
				bitInfo.bmiHeader.biSizeImage       = 0;
				bitInfo.bmiHeader.biXPelsPerMeter   = 0;
				bitInfo.bmiHeader.biYPelsPerMeter   = 0;

				::SetDIBitsToDevice(pDC->GetSafeHdc(), 0, 0,
				    matResized.cols, matResized.rows,
				    0, 0, 0, matResized.rows,
				    matResized.data, &bitInfo, DIB_RGB_COLORS);
				ReleaseDC(pDC);
			}
		}
	}
}

HCURSOR CPylonSampleProgramDlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}


void CPylonSampleProgramDlg::OnBnClickedFindCamBtn()
{
	GetSerialNumerFromFile();
	m_error = m_CameraManager.FindCamera(m_szCamName, m_szSerialNum, m_szInterface, &m_iCamNumber);

	if (m_error == 0)
	{
		m_ctrlCamList.DeleteAllItems();
		int nCount = 0;
		CString strSerialNum;

		for (int i = 0; i < m_iCamNumber; i++)
		{
			nCount++;
			CString strcamname  = (CString)m_szCamName[i];
			strSerialNum = (CString)m_szSerialNum[i];

			CString strCheck;
			strCheck.Format(_T("인식된 카메라 [%d]\n이름: %s\n시리얼: %s\n\n설정파일 시리얼0: %s\n설정파일 시리얼1: %s"),
				i, strcamname, strSerialNum, m_strCamSerial[0], m_strCamSerial[1]);
			AfxMessageBox(strCheck);

			bool bFound = false;
			if (strSerialNum == m_strCamSerial[0])
			{
				m_iCamPosition[0] = nCount;
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
	SetDlgItemText(IDC_SELECT_CAMERA, m_ctrlCamList.GetItemText(m_iListIndex, 1));
	if      (m_ctrlCamList.GetItemText(pNMListView->iItem, 1) == _T("Inspect0")) m_iCameraIndex = 0;
	else if (m_ctrlCamList.GetItemText(pNMListView->iItem, 1) == _T("Inspect1")) m_iCameraIndex = 1;
	else if (m_ctrlCamList.GetItemText(pNMListView->iItem, 1) == _T("Inspect2")) m_iCameraIndex = 2;
	else if (m_ctrlCamList.GetItemText(pNMListView->iItem, 1) == _T("Inspect3")) m_iCameraIndex = 3;
	*pResult = 0;
}

void CPylonSampleProgramDlg::OnBnClickedOpenCameraBtn()
{
	if (m_iCameraIndex == -1 && m_iCamNumber > 0)
	{
		m_iCameraIndex = 0;
		m_iListIndex   = 0;
	}
	if(m_iCameraIndex != -1)
	{
		int error = m_CameraManager.Open_Camera(m_iCameraIndex, m_iCamPosition[m_iCameraIndex]);
		if      (error == 0)  m_ctrlCamList.SetItemText(m_iListIndex, 3, _T("Open_Success"));
		else if (error == -1) m_ctrlCamList.SetItemText(m_iListIndex, 3, _T("Alread_Open"));
		else                  m_ctrlCamList.SetItemText(m_iListIndex, 3, _T("Open_Fail"));
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
			m_ctrlCamList.SetItemText(m_iListIndex, 3, _T("Close_Success"));
 			if(bitmapinfo[m_iCameraIndex]) { delete bitmapinfo[m_iCameraIndex]; bitmapinfo[m_iCameraIndex]=NULL; }
			if(pImageColorDestBuffer[m_iCameraIndex])
			{
				for(int i=0; i<BUF_NUM; i++) free(pImageColorDestBuffer[m_iCameraIndex][i]);
				free(pImageColorDestBuffer[m_iCameraIndex]);
				pImageColorDestBuffer[m_iCameraIndex] = NULL;
			}
			if(pImageresizeOrgBuffer[m_iCameraIndex])
			{
				for(int i=0; i<BUF_NUM; i++) free(pImageresizeOrgBuffer[m_iCameraIndex][i]);
				free(pImageresizeOrgBuffer[m_iCameraIndex]);
				pImageresizeOrgBuffer[m_iCameraIndex] = NULL;
			}
		}
		else { m_ctrlCamList.SetItemText(m_iListIndex, 3, _T("Close_Fail")); }
	}
}

void CPylonSampleProgramDlg::OnBnClickedConnectCameraBtn()
{
	if (!m_ctrlCamList.GetSafeHwnd()) return;

	int nIndex = m_ctrlCamList.GetNextItem(-1, LVNI_SELECTED);
	if (nIndex < 0 || nIndex >= CAM_NUM) {
		AfxMessageBox(_T("연결할 카메라를 리스트에서 선택해 주세요."));
		return;
	}
	m_iCameraIndex = nIndex;

	int nRet = m_CameraManager.Connect_Camera(m_iCameraIndex, 0, 0, 0, 0, _T("Mono8"));
	if (nRet == 0)
	{
		m_ctrlCamList.SetItemText(m_iCameraIndex, 3, _T("Connected"));

		// ── 서버 연결 시도 + UI 상태 업데이트 ──
		std::string serverIP   = "10.10.10.109";
		int         serverPort = 8000;
		if (m_CameraManager.ConnectToServer(serverIP, serverPort))
		{
			SetDlgItemText(IDC_STATUS_SERVER, _T("연결됨 (10.10.10.109:8000)"));
			TRACE(_T("AI Server Connected\n"));
		}
		else
		{
			SetDlgItemText(IDC_STATUS_SERVER, _T("서버 연결 실패"));
			TRACE(_T("AI Server Connection Failed\n"));
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
		if (m_CameraManager.SingleGrab(m_iCameraIndex) == 0)
		{
			if (m_CameraManager.pImage24Buffer[m_iCameraIndex] != NULL)
			{
				if (pImageColorDestBuffer[m_iCameraIndex] != NULL &&
				    pImageColorDestBuffer[m_iCameraIndex][0] != NULL)
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
	catch (...) { TRACE(_T("Unknown Exception in GrabSingle Button\n")); }
}

void CPylonSampleProgramDlg::AllocImageBuf(void)
{
	UpdateData();
	if (m_CameraManager.m_strCM_ImageForamt[m_iCameraIndex] == "Mono8" ||
	    m_CameraManager.m_strCM_ImageForamt[m_iCameraIndex] == "Mono12" ||
	    m_CameraManager.m_strCM_ImageForamt[m_iCameraIndex] == "Mono16")
	{
		pImageresizeOrgBuffer[m_iCameraIndex] = (unsigned char**)malloc(BUF_NUM * sizeof(unsigned char*));
		for (int i = 0; i < BUF_NUM; i++)
			pImageresizeOrgBuffer[m_iCameraIndex][i] = (unsigned char*)malloc(
			    m_CameraManager.m_iCM_reSizeWidth[m_iCameraIndex] *
			    m_CameraManager.m_iCM_Height[m_iCameraIndex]);
	}

	if (pImageColorDestBuffer[m_iCameraIndex] == NULL)
	{
		pImageColorDestBuffer[m_iCameraIndex] = (unsigned char**)malloc(BUF_NUM * sizeof(unsigned char*));
		for (int i = 0; i < BUF_NUM; i++)
		{
			pImageColorDestBuffer[m_iCameraIndex][i] = (unsigned char*)malloc(260 * 260 * 3);
			memset(pImageColorDestBuffer[m_iCameraIndex][i], 0, 260 * 260 * 3);
		}
	}

	switch (m_iCameraIndex)
	{
	case 0: hWnd[0]=GetDlgItem(IDC_CAM0_DISPLAY)->GetSafeHwnd(); GetDlgItem(IDC_CAM0_DISPLAY)->GetClientRect(&rectStaticClient[0]); hdc[0]=::GetDC(hWnd[0]); break;
	case 1: hWnd[1]=GetDlgItem(IDC_CAM1_DISPLAY)->GetSafeHwnd(); GetDlgItem(IDC_CAM1_DISPLAY)->GetClientRect(&rectStaticClient[1]); hdc[1]=::GetDC(hWnd[1]); break;
	case 2: hWnd[2]=GetDlgItem(IDC_CAM2_DISPLAY)->GetSafeHwnd(); GetDlgItem(IDC_CAM2_DISPLAY)->GetClientRect(&rectStaticClient[2]); hdc[2]=::GetDC(hWnd[2]); break;
	case 3: hWnd[3]=GetDlgItem(IDC_CAM3_DISPLAY)->GetSafeHwnd(); GetDlgItem(IDC_CAM3_DISPLAY)->GetClientRect(&rectStaticClient[3]); hdc[3]=::GetDC(hWnd[3]); break;
	}
}

void CPylonSampleProgramDlg::InitBitmap(int nCamIndex)
{
   if(m_CameraManager.m_strCM_ImageForamt[m_iCameraIndex]=="Mono8" || m_CameraManager.m_strCM_ImageForamt[m_iCameraIndex]=="Mono16")
   {
		if(bitmapinfo[nCamIndex]) { delete bitmapinfo[nCamIndex]; bitmapinfo[nCamIndex]=NULL; }
		bitmapinfo[nCamIndex]=(BITMAPINFO*)(new char[sizeof(BITMAPINFOHEADER)+256*sizeof(RGBQUAD)]);
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
		for(int j=0;j<256;j++){
			bitmapinfo[nCamIndex]->bmiColors[j].rgbRed=(unsigned char)j;
			bitmapinfo[nCamIndex]->bmiColors[j].rgbGreen=(unsigned char)j;
			bitmapinfo[nCamIndex]->bmiColors[j].rgbBlue=(unsigned char)j;
			bitmapinfo[nCamIndex]->bmiColors[j].rgbReserved=0;
		}
   }
   else
   {
		if(bitmapinfo[nCamIndex]) { delete bitmapinfo[nCamIndex]; bitmapinfo[nCamIndex]=NULL; }
		bitmapinfo[nCamIndex]=(BITMAPINFO*)(new char[sizeof(BITMAPINFOHEADER)+256*sizeof(RGBQUAD)]);
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
		for(int j=0;j<256;j++){
			bitmapinfo[nCamIndex]->bmiColors[j].rgbRed=(unsigned char)j;
			bitmapinfo[nCamIndex]->bmiColors[j].rgbGreen=(unsigned char)j;
			bitmapinfo[nCamIndex]->bmiColors[j].rgbBlue=(unsigned char)j;
			bitmapinfo[nCamIndex]->bmiColors[j].rgbReserved=0;
		}
   }
}

// ── Display 함수들 ────────────────────────────────────────────

void CPylonSampleProgramDlg::DisplayCam0(void* pImageBuf)
{
	if (pImageBuf == NULL || hdc[0] == NULL) return;
	bitmapinfo[0]->bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
	bitmapinfo[0]->bmiHeader.biWidth=260; bitmapinfo[0]->bmiHeader.biHeight=-260;
	bitmapinfo[0]->bmiHeader.biPlanes=1; bitmapinfo[0]->bmiHeader.biBitCount=24;
	bitmapinfo[0]->bmiHeader.biCompression=BI_RGB; bitmapinfo[0]->bmiHeader.biSizeImage=260*260*3;
	bitmapinfo[0]->bmiHeader.biClrUsed=0;
	SetStretchBltMode(hdc[0], COLORONCOLOR);
	StretchDIBits(hdc[0], 0,0, rectStaticClient[0].Width(), rectStaticClient[0].Height(),
	    0,0,260,260, pImageBuf, bitmapinfo[0], DIB_RGB_COLORS, SRCCOPY);
}

void CPylonSampleProgramDlg::DisplayCam1(void* pImageBuf)
{
	if (pImageBuf == NULL || hdc[0] == NULL) return;
	bitmapinfo[0]->bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
	bitmapinfo[0]->bmiHeader.biWidth=260; bitmapinfo[0]->bmiHeader.biHeight=-260;
	bitmapinfo[0]->bmiHeader.biPlanes=1; bitmapinfo[0]->bmiHeader.biBitCount=24;
	bitmapinfo[0]->bmiHeader.biCompression=BI_RGB; bitmapinfo[0]->bmiHeader.biSizeImage=260*260*3;
	bitmapinfo[0]->bmiHeader.biClrUsed=0;
	SetStretchBltMode(hdc[0], COLORONCOLOR);
	StretchDIBits(hdc[0], 0,0, rectStaticClient[0].Width(), rectStaticClient[0].Height(),
	    0,0,260,260, pImageBuf, bitmapinfo[0], DIB_RGB_COLORS, SRCCOPY);
}

void CPylonSampleProgramDlg::DisplayCam2(void* pImageBuf)
{
	if (pImageBuf == NULL || hdc[0] == NULL) return;
	bitmapinfo[0]->bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
	bitmapinfo[0]->bmiHeader.biWidth=260; bitmapinfo[0]->bmiHeader.biHeight=-260;
	bitmapinfo[0]->bmiHeader.biPlanes=1; bitmapinfo[0]->bmiHeader.biBitCount=24;
	bitmapinfo[0]->bmiHeader.biCompression=BI_RGB; bitmapinfo[0]->bmiHeader.biSizeImage=260*260*3;
	bitmapinfo[0]->bmiHeader.biClrUsed=0;
	SetStretchBltMode(hdc[0], COLORONCOLOR);
	StretchDIBits(hdc[0], 0,0, rectStaticClient[0].Width(), rectStaticClient[0].Height(),
	    0,0,260,260, pImageBuf, bitmapinfo[0], DIB_RGB_COLORS, SRCCOPY);
}

void CPylonSampleProgramDlg::DisplayCam3(void* pImageBuf)
{
	if (pImageBuf == NULL || hdc[0] == NULL) return;
	bitmapinfo[0]->bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
	bitmapinfo[0]->bmiHeader.biWidth=260; bitmapinfo[0]->bmiHeader.biHeight=-260;
	bitmapinfo[0]->bmiHeader.biPlanes=1; bitmapinfo[0]->bmiHeader.biBitCount=24;
	bitmapinfo[0]->bmiHeader.biCompression=BI_RGB; bitmapinfo[0]->bmiHeader.biSizeImage=260*260*3;
	bitmapinfo[0]->bmiHeader.biClrUsed=0;
	SetStretchBltMode(hdc[0], COLORONCOLOR);
	StretchDIBits(hdc[0], 0,0, rectStaticClient[0].Width(), rectStaticClient[0].Height(),
	    0,0,260,260, pImageBuf, bitmapinfo[0], DIB_RGB_COLORS, SRCCOPY);
}

// ── 라이브 버튼들 ─────────────────────────────────────────────

void CPylonSampleProgramDlg::OnBnClickedCam0Live()
{
	if (m_CameraManager.m_bCamConnectFlag[0] == true)
	{
		bStopThread[0] = !bStopThread[0];
		if (bStopThread[0]) {
			bLiveFlag[0] = true;
			m_CameraManager.GrabLive(0, 0);
			SetDlgItemText(IDC_CAM0_LIVE, _T("Live_Cam0_Stop"));
			m_nCamIndexBuf[0] = 0;
			AfxBeginThread(LiveGrabThreadCam0, &m_nCamIndexBuf[0]);
		} else {
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
	if (m_CameraManager.m_bCamConnectFlag[0] == true)
	{
		bStopThread[0] = !bStopThread[0];
		if (bStopThread[0]) {
			bLiveFlag[0]=true; m_CameraManager.GrabLive(0,0);
			SetDlgItemText(IDC_CAM0_LIVE,_T("Live_Cam0_Stop"));
			m_nCamIndexBuf[0]=0; AfxBeginThread(LiveGrabThreadCam0,&m_nCamIndexBuf[0]);
		} else {
			bLiveFlag[0]=false; SetDlgItemText(IDC_CAM0_LIVE,_T("Live_Cam0_Start"));
			m_CameraManager.LiveStop(0,0);
		}
	}
	else { AfxMessageBox(_T("Camera0 Connect를 먼저 하세요!!")); CButton*p=(CButton*)GetDlgItem(IDC_CAM0_LIVE); if(p)p->SetCheck(0); }
}

void CPylonSampleProgramDlg::OnBnClickedCam2Live()
{
	if (m_CameraManager.m_bCamConnectFlag[0] == true)
	{
		bStopThread[0]=!bStopThread[0];
		if(bStopThread[0]){bLiveFlag[0]=true;m_CameraManager.GrabLive(0,0);SetDlgItemText(IDC_CAM0_LIVE,_T("Live_Cam0_Stop"));m_nCamIndexBuf[0]=0;AfxBeginThread(LiveGrabThreadCam0,&m_nCamIndexBuf[0]);}
		else{bLiveFlag[0]=false;SetDlgItemText(IDC_CAM0_LIVE,_T("Live_Cam0_Start"));m_CameraManager.LiveStop(0,0);}
	}
	else{AfxMessageBox(_T("Camera0 Connect를 먼저 하세요!!"));CButton*p=(CButton*)GetDlgItem(IDC_CAM0_LIVE);if(p)p->SetCheck(0);}
}

void CPylonSampleProgramDlg::OnBnClickedCam3Live()
{
	if (m_CameraManager.m_bCamConnectFlag[0] == true)
	{
		bStopThread[0]=!bStopThread[0];
		if(bStopThread[0]){bLiveFlag[0]=true;m_CameraManager.GrabLive(0,0);SetDlgItemText(IDC_CAM0_LIVE,_T("Live_Cam0_Stop"));m_nCamIndexBuf[0]=0;AfxBeginThread(LiveGrabThreadCam0,&m_nCamIndexBuf[0]);}
		else{bLiveFlag[0]=false;SetDlgItemText(IDC_CAM0_LIVE,_T("Live_Cam0_Start"));m_CameraManager.LiveStop(0,0);}
	}
	else{AfxMessageBox(_T("Camera0 Connect를 먼저 하세요!!"));CButton*p=(CButton*)GetDlgItem(IDC_CAM0_LIVE);if(p)p->SetCheck(0);}
}

void CPylonSampleProgramDlg::OnBnClickedTwoCameraLiveBtn()
{
	if(m_CameraManager.m_bCamConnectFlag[0] == true)
	{
		bStopThread[0]=(bStopThread[0]+1)&0x01;
		if(bStopThread[0]){bLiveFlag[0]=true;m_CameraManager.GrabLive(0,1);SetDlgItemText(IDC_CAM0_LIVE,_T("Live_Cam0_Stop"));AfxBeginThread(LiveGrabThreadCam0,&m_nCamIndexBuf[0]);}
		else{bLiveFlag[0]=false;SetDlgItemText(IDC_CAM0_LIVE,_T("Live_Cam0_Start"));m_CameraManager.LiveStop(0,1);}
	}
	else{AfxMessageBox(_T("Camera0 Connect를 하세요!!"));CButton*p=(CButton*)pMainDlg->GetDlgItem(IDC_CAM0_LIVE);p->SetCheck(0);}

	if(m_CameraManager.m_bCamConnectFlag[1] == true)
	{
		bStopThread[1]=(bStopThread[1]+1)&0x01;
		if(bStopThread[1]){bLiveFlag[1]=true;SetDlgItemText(IDC_CAM1_LIVE,_T("Live_Cam1_Stop"));AfxBeginThread(LiveGrabThreadCam0,&m_nCamIndexBuf[1]);}
		else{bLiveFlag[1]=false;SetDlgItemText(IDC_CAM1_LIVE,_T("Live_Cam1_Start"));}
	}
	else{AfxMessageBox(_T("Camera1 Connect를 하세요!!"));CButton*p=(CButton*)pMainDlg->GetDlgItem(IDC_CAM1_LIVE);p->SetCheck(0);}
}

void CPylonSampleProgramDlg::OnBnClickedSoftTrigBtn()
{
	m_CameraManager.SetCommand(m_iCameraIndex, "TriggerSoftware");
}

void CPylonSampleProgramDlg::OnBnClickedExitBtn()
{
	if (m_CameraManager.m_bIsServerConnected)
		m_CameraManager.DisconnectFromServer();

	for (int k = 0; k < CAM_NUM; k++)
	{
		bStopThread[k] = false;
		m_CameraManager.LiveStop(k, 0);
		if(bitmapinfo[k]){ delete bitmapinfo[k]; bitmapinfo[k]=NULL; }
		if(pImageColorDestBuffer[k])
		{
			for(int i=0;i<BUF_NUM;i++) if(pImageColorDestBuffer[k][i]) free(pImageColorDestBuffer[k][i]);
			free(pImageColorDestBuffer[k]); pImageColorDestBuffer[k]=NULL;
		}
		if(pImageresizeOrgBuffer[k])
		{
			for(int i=0;i<BUF_NUM;i++) if(pImageresizeOrgBuffer[k][i]) free(pImageresizeOrgBuffer[k][i]);
			free(pImageresizeOrgBuffer[k]); pImageresizeOrgBuffer[k]=NULL;
		}
		if(m_CameraManager.m_bCamOpenFlag[k]==true) m_CameraManager.Close_Camera(k);
	}
	CDialog::OnOK();
}

void CPylonSampleProgramDlg::OnBnClickedSaveImgBtn()
{
	int nCam = m_iCameraIndex;
	CString strPath = _T("C:\\Temp");
	if (!PathFileExists(strPath)) CreateDirectory(strPath, NULL);

	char szFileName[256];
	sprintf_s(szFileName, "C:\\Temp\\MetalGuard_Cam%d.bmp", nCam);

	if (m_CameraManager.pImage24Buffer[nCam] != NULL)
	{
		int nResult = m_CameraManager.SaveImage(0, m_CameraManager.pImage24Buffer[nCam],
		                                        szFileName, 1, 260, 260, 3);
		if (nResult == 0) AfxMessageBox(_T("이미지 저장 성공! (C:\\Temp)"));
		else              AfxMessageBox(_T("이미지 저장 실패! 출력창을 확인하세요."));
	}
	else { AfxMessageBox(_T("저장할 이미지 버퍼가 비어있습니다. 먼저 촬영(Grab)을 진행하세요.")); }
}

void CPylonSampleProgramDlg::OnBnClickedButton5()
{
	static bool bTrig = false;
	if(bTrig==false)
	{
		SetDlgItemText(IDC_BUTTON5, _T("Trigger_해제")); bTrig=true;
		m_CameraManager.SetEnumeration(m_iCameraIndex, "FrameStart", "TriggerSelector");
		m_CameraManager.SetEnumeration(m_iCameraIndex, "On",         "TriggerMode");
		m_CameraManager.SetEnumeration(m_iCameraIndex, "Software",   "TriggerSource");
	}
	else
	{
		SetDlgItemText(IDC_BUTTON5, _T("Trigger_설정")); bTrig=false;
		m_CameraManager.SetEnumeration(m_iCameraIndex, "Off", "TriggerMode");
	}
}

void CPylonSampleProgramDlg::OnNMClickListCam(NMHDR* pNMHDR, LRESULT* pResult)
{
	LPNMITEMACTIVATE pNMItemActivate = reinterpret_cast<LPNMITEMACTIVATE>(pNMHDR);
	int nItem = pNMItemActivate->iItem;
	if (nItem != -1)
	{
		m_iListIndex   = nItem;
		m_iCameraIndex = nItem;
		CString str;
		str.Format(_T("선택된 인덱스: %d"), m_iCameraIndex);
		AfxMessageBox(str);
	}
	*pResult = 0;
}

void CPylonSampleProgramDlg::OnBnClickedReclassifyBtn()
{
	if (m_CameraManager.m_nCurrentMode == SystemMode::NORMAL)
	{
		m_CameraManager.m_nCurrentMode = SystemMode::RECLASSIFY;
		m_CameraManager.WriteLog(0, _T("알림"), _T("시스템 모드 변경: [재분류 모드]"));
		SetDlgItemText(IDC_RECLASSIFY_BTN, _T("일반모드 복귀"));

		// 콤보박스도 동기화
		CComboBox* pCombo = (CComboBox*)GetDlgItem(IDC_MODE_COMBO);
		if (pCombo) pCombo->SetCurSel(1);
	}
	else
	{
		m_CameraManager.m_nCurrentMode = SystemMode::NORMAL;
		m_CameraManager.WriteLog(0, _T("알림"), _T("시스템 모드 변경: [일반 분류 모드]"));
		SetDlgItemText(IDC_RECLASSIFY_BTN, _T("재분류 모드"));

		// 콤보박스도 동기화
		CComboBox* pCombo = (CComboBox*)GetDlgItem(IDC_MODE_COMBO);
		if (pCombo) pCombo->SetCurSel(0);
	}
}

// ── 신규: 운영 모드 콤보박스 변경 핸들러 ──────────────────────
void CPylonSampleProgramDlg::OnCbnSelchangeModeCombo()
{
	CComboBox* pCombo = (CComboBox*)GetDlgItem(IDC_MODE_COMBO);
	if (!pCombo) return;

	int nSel = pCombo->GetCurSel();
	if (nSel == 1)
	{
		m_CameraManager.m_nCurrentMode = SystemMode::RECLASSIFY;
		SetDlgItemText(IDC_RECLASSIFY_BTN, _T("일반모드 복귀"));
		m_CameraManager.WriteLog(0, _T("알림"), _T("운영 모드: UNCERTAIN 재분류"));
	}
	else
	{
		m_CameraManager.m_nCurrentMode = SystemMode::NORMAL;
		SetDlgItemText(IDC_RECLASSIFY_BTN, _T("재분류 모드"));
		m_CameraManager.WriteLog(0, _T("알림"), _T("운영 모드: 실시간 검출"));
	}
}

UINT CPylonSampleProgramDlg::ThreadReclassify(LPVOID pParam)
{
	ReclassifyParam* pData = static_cast<ReclassifyParam*>(pParam);
	if (pData != nullptr)
	{
		pData->pDlg->m_CameraManager.SendImageToAI(0, pData->matImage,
		    pData->nPlateId, 1, CmdID::IMG_RECLASSIFY);
		delete pData;
	}
	return 0;
}

// ── 로그 메시지 핸들러 ────────────────────────────────────────

LRESULT CPylonSampleProgramDlg::OnUpdateLog(WPARAM wParam, LPARAM lParam)
{
	CString* pStrLog = (CString*)wParam;
	if (pStrLog)
	{
		CListBox* pListBox = (CListBox*)GetDlgItem(IDC_LIST_LOG);
		if (pListBox)
		{
			int nIndex = pListBox->AddString(*pStrLog);
			pListBox->SetCurSel(nIndex);
			if (pListBox->GetCount() > 500) pListBox->DeleteString(0);
		}
		delete pStrLog;
	}
	return 0;
}

// ── 신규: 판정 결과 UI 업데이트 핸들러 (WM_UPDATE_VERDICT) ───
//
//  CameraManager.cpp의 ThreadReceiveFromServer()에서
//  RESULT_SEND(301) 수신 후 VerdictData*를 힙 할당하여
//  ::PostMessage(pMgr->m_hMainWnd, WM_UPDATE_VERDICT, (WPARAM)pV, 0) 로 전달.
//  본 함수에서 UI 업데이트 후 delete 처리.
//
LRESULT CPylonSampleProgramDlg::OnUpdateVerdict(WPARAM wParam, LPARAM lParam)
{
	VerdictData* pV = reinterpret_cast<VerdictData*>(wParam);
	if (!pV) return 0;

	CString sv(pV->verdict.c_str());
	CString sd(pV->defect.c_str());
	CString tmp;

	// ── 최종 판정 텍스트 ──────────────────────────────────────
	SetDlgItemText(IDC_VERDICT_DISPLAY, sv);
	SetDlgItemText(IDC_DEFECT_CLASS,    sd);

	// ── AI 확률값 ─────────────────────────────────────────────
	tmp.Format(_T("%.1f %%"), pV->prob_normal);  SetDlgItemText(IDC_PROB_NORMAL,  tmp);
	tmp.Format(_T("%.1f %%"), pV->prob_crack);   SetDlgItemText(IDC_PROB_CRACK,   tmp);
	tmp.Format(_T("%.1f %%"), pV->prob_hole);    SetDlgItemText(IDC_PROB_HOLE,    tmp);
	tmp.Format(_T("%.1f %%"), pV->prob_rust);    SetDlgItemText(IDC_PROB_RUST,    tmp);
	tmp.Format(_T("%.1f %%"), pV->prob_scratch); SetDlgItemText(IDC_PROB_SCRATCH, tmp);

	// ── 타이밍 ───────────────────────────────────────────────
	tmp.Format(_T("%.1f ms"), pV->inference_ms); SetDlgItemText(IDC_INFER_MS,    tmp);
	tmp.Format(_T("%.1f ms"), pV->pipeline_ms);  SetDlgItemText(IDC_PIPELINE_MS, tmp);

	// ── LED 상태 ──────────────────────────────────────────────
	SetDlgItemText(IDC_LED_GREEN,  pV->verdict == "PASS"      ? _T("ON  ?") : _T("OFF"));
	SetDlgItemText(IDC_LED_RED,    pV->verdict == "FAIL"      ? _T("ON  ?") : _T("OFF"));
	SetDlgItemText(IDC_LED_YELLOW, pV->verdict == "UNCERTAIN" ? _T("ON  ?") : _T("OFF"));

	// ── 부저 상태 ─────────────────────────────────────────────
	if      (pV->verdict == "PASS")      SetDlgItemText(IDC_CAM0_INFO, _T("없음"));
	else if (pV->verdict == "FAIL")      SetDlgItemText(IDC_CAM0_INFO, _T("단음 발신 (FAIL)"));
	else if (pV->verdict == "UNCERTAIN") SetDlgItemText(IDC_CAM0_INFO, _T("이중음 발신 (UNCERTAIN)"));
	else                                 SetDlgItemText(IDC_CAM0_INFO, _T("단음 발신 (TIMEOUT)"));

	// ── 게이트 상태 ───────────────────────────────────────────
	SetDlgItemText(IDC_STATUS_GATE_A,
	    pV->verdict == "FAIL" ? _T("구동 중") : _T("대기"));
	SetDlgItemText(IDC_STATUS_GATE_B,
	    pV->verdict == "UNCERTAIN" ? _T("구동 중") : _T("대기"));

	// ── DB 기록 테이블에 행 추가 (맨 위에 삽입) ──────────────
	CTime t = CTime::GetCurrentTime();
	CString strTime = t.Format(_T("%Y-%m-%d %H:%M:%S"));

	m_nDBRowCount++;
	int nRow = m_listDB.InsertItem(0, _T(""));  // 맨 위에 삽입

	tmp.Format(_T("%d"), m_nDBRowCount);
	m_listDB.SetItemText(nRow, 0, tmp);
	m_listDB.SetItemText(nRow, 1, strTime);
	tmp.Format(_T("%d"), pV->plate_id);
	m_listDB.SetItemText(nRow, 2, tmp);
	m_listDB.SetItemText(nRow, 3, sv);
	tmp.Format(_T("%.0f%%"), pV->prob_normal);  m_listDB.SetItemText(nRow, 4,  tmp);
	tmp.Format(_T("%.0f%%"), pV->prob_crack);   m_listDB.SetItemText(nRow, 5,  tmp);
	tmp.Format(_T("%.0f%%"), pV->prob_hole);    m_listDB.SetItemText(nRow, 6,  tmp);
	tmp.Format(_T("%.0f%%"), pV->prob_rust);    m_listDB.SetItemText(nRow, 7,  tmp);
	tmp.Format(_T("%.0f%%"), pV->prob_scratch); m_listDB.SetItemText(nRow, 8,  tmp);
	tmp.Format(_T("%.1f"),   pV->inference_ms); m_listDB.SetItemText(nRow, 9,  tmp);
	tmp.Format(_T("%.1f"),   pV->pipeline_ms);  m_listDB.SetItemText(nRow, 10, tmp);

	// 100행 초과 시 오래된 행 삭제
	if (m_listDB.GetItemCount() > 100)
		m_listDB.DeleteItem(m_listDB.GetItemCount() - 1);

	delete pV;
	return 0;
}
