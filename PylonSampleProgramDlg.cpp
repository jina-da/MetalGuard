#include "stdafx.h"
#include "PylonSampleProgram.h"
#include "PylonSampleProgramDlg.h"
#include "MetalGuardTypes.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

HWND g_hMainWnd = NULL;

CPylonSampleProgramDlg* pMainDlg;

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
void CAboutDlg::DoDataExchange(CDataExchange* pDX) { CDialog::DoDataExchange(pDX); }
BEGIN_MESSAGE_MAP(CAboutDlg, CDialog)
END_MESSAGE_MAP()

CPylonSampleProgramDlg::CPylonSampleProgramDlg(CWnd* pParent)
	: CDialog(CPylonSampleProgramDlg::IDD, pParent)
{
	m_nDBRowCount = 0;
	m_strCurrentVerdict = _T("WAIT");
	for (int i = 0; i < CAM_NUM; i++)
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
	DDX_Control(pDX, IDC_DB_LIST,     m_listDB);
}

BEGIN_MESSAGE_MAP(CPylonSampleProgramDlg, CDialog)
	ON_WM_SYSCOMMAND()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_BN_CLICKED(IDC_FIND_CAM_BTN,        &CPylonSampleProgramDlg::OnBnClickedFindCamBtn)
	ON_NOTIFY(NM_CLICK, IDC_CAMERA_LIST,   &CPylonSampleProgramDlg::OnNMClickCameraList)
	ON_BN_CLICKED(IDC_OPEN_CAMERA_BTN,     &CPylonSampleProgramDlg::OnBnClickedOpenCameraBtn)
	ON_BN_CLICKED(IDC_CONNECT_CAMERA_BTN,  &CPylonSampleProgramDlg::OnBnClickedConnectCameraBtn)
	ON_BN_CLICKED(IDC_GRAB_SINGLE_BTN,     &CPylonSampleProgramDlg::OnBnClickedGrabSingleBtn)
	ON_BN_CLICKED(IDC_CAM0_LIVE,           &CPylonSampleProgramDlg::OnBnClickedCam0Live)
	ON_BN_CLICKED(IDC_CLOSE_CAM_BTN,       &CPylonSampleProgramDlg::OnBnClickedCloseCamBtn)
	ON_BN_CLICKED(IDC_RECLASSIFY_BTN,      &CPylonSampleProgramDlg::OnBnClickedReclassifyBtn)
	ON_BN_CLICKED(IDC_SOFT_TRIG_BTN,       &CPylonSampleProgramDlg::OnBnClickedSoftTrigBtn)
	ON_BN_CLICKED(IDC_EXIT_BTN,            &CPylonSampleProgramDlg::OnBnClickedExitBtn)
	ON_BN_CLICKED(IDC_CAM1_LIVE,           &CPylonSampleProgramDlg::OnBnClickedCam1Live)
	ON_BN_CLICKED(IDC_SAVE_IMG_BTN,        &CPylonSampleProgramDlg::OnBnClickedSaveImgBtn)
	ON_BN_CLICKED(IDC_BUTTON5,             &CPylonSampleProgramDlg::OnBnClickedButton5)
	ON_BN_CLICKED(IDC_TWO_CAMERA_LIVE_BTN, &CPylonSampleProgramDlg::OnBnClickedTwoCameraLiveBtn)
	ON_BN_CLICKED(IDC_CAM2_LIVE,           &CPylonSampleProgramDlg::OnBnClickedCam2Live)
	ON_BN_CLICKED(IDC_CAM3_LIVE,           &CPylonSampleProgramDlg::OnBnClickedCam3Live)
	ON_BN_CLICKED(IDC_ONE_CLICK_START,     &CPylonSampleProgramDlg::OnBnClickedOneClickStart)
	ON_MESSAGE(WM_UPDATE_LOG,     &CPylonSampleProgramDlg::OnUpdateLog)
	ON_MESSAGE(WM_UPDATE_VERDICT, &CPylonSampleProgramDlg::OnUpdateVerdict)
	ON_CBN_SELCHANGE(IDC_MODE_COMBO, &CPylonSampleProgramDlg::OnCbnSelchangeModeCombo)
	ON_WM_CTLCOLOR()
	ON_NOTIFY(NM_CUSTOMDRAW, IDC_DB_LIST, &CPylonSampleProgramDlg::OnNMCustomdrawDbList)
END_MESSAGE_MAP()

BOOL CPylonSampleProgramDlg::OnInitDialog()
{
	CDialog::OnInitDialog();
	g_hMainWnd = m_hWnd;

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

	// --- 카메라 리스트 컬럼 초기화 ---
	m_ctrlCamList.InsertColumn(0, _T("모델명"),    LVCFMT_CENTER, 130);
	m_ctrlCamList.InsertColumn(1, _T("위치"),      LVCFMT_CENTER, 70);
	m_ctrlCamList.InsertColumn(2, _T("시리얼"),    LVCFMT_CENTER, 90);
	m_ctrlCamList.InsertColumn(3, _T("상태"),      LVCFMT_CENTER, 100);
	m_ctrlCamList.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

	// --- DB 기록 테이블 컬럼 초기화 (모드 컬럼 추가) ---
	m_listDB.InsertColumn(0,  _T("ID"),            LVCFMT_CENTER, 38);
	m_listDB.InsertColumn(1,  _T("시각"),           LVCFMT_CENTER, 112);
	m_listDB.InsertColumn(2,  _T("plate"),          LVCFMT_CENTER, 42);
	m_listDB.InsertColumn(3,  _T("모드"),           LVCFMT_CENTER, 65);
	m_listDB.InsertColumn(4,  _T("판정"),           LVCFMT_CENTER, 60);
	m_listDB.InsertColumn(5,  _T("Normal%"),        LVCFMT_CENTER, 52);
	m_listDB.InsertColumn(6,  _T("Crack%"),         LVCFMT_CENTER, 48);
	m_listDB.InsertColumn(7,  _T("Hole%"),          LVCFMT_CENTER, 48);
	m_listDB.InsertColumn(8,  _T("Rust%"),          LVCFMT_CENTER, 48);
	m_listDB.InsertColumn(9,  _T("Scratch%"),       LVCFMT_CENTER, 52);
	m_listDB.InsertColumn(10, _T("추론(ms)"),       LVCFMT_CENTER, 55);
	m_listDB.InsertColumn(11, _T("파이프라인(ms)"), LVCFMT_CENTER, 72);
	m_listDB.SetExtendedStyle(LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

	// --- 운영 모드 콤보박스 초기화 ---
	CComboBox* pCombo = (CComboBox*)GetDlgItem(IDC_MODE_COMBO);
	if (pCombo) {
		pCombo->AddString(_T("실시간 검출"));
		pCombo->AddString(_T("UNCERTAIN 재분류"));
		pCombo->SetCurSel(0);
	}

	// --- 이미지 버퍼 할당 ---
	for (int i = 0; i < CAM_NUM; i++)
	{
		pImageColorDestBuffer[i] = new unsigned char* [BUF_NUM];
		for (int j = 0; j < BUF_NUM; j++)
		{
			pImageColorDestBuffer[i][j] = new unsigned char[260 * 260 * 3];
			memset(pImageColorDestBuffer[i][j], 0, 260 * 260 * 3);
		}
	}

	// --- HDC 초기화 ---
	CWnd* pWnd = GetDlgItem(IDC_CAM0_DISPLAY);
	if (pWnd) {
		pWnd->GetClientRect(&rectStaticClient[0]);
		hdc[0] = pWnd->GetDC()->GetSafeHdc();
	}
	if (bitmapinfo[0] == NULL) {
		bitmapinfo[0] = (BITMAPINFO*)new BYTE[sizeof(BITMAPINFO)];
		memset(bitmapinfo[0], 0, sizeof(BITMAPINFO));
		bitmapinfo[0]->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bitmapinfo[0]->bmiHeader.biWidth = 260;
		bitmapinfo[0]->bmiHeader.biHeight = -260;
		bitmapinfo[0]->bmiHeader.biPlanes = 1;
		bitmapinfo[0]->bmiHeader.biBitCount = 24;
		bitmapinfo[0]->bmiHeader.biCompression = BI_RGB;
	}

	pMainDlg = this;

	// --- 판정 색상 브러시 초기화 ---
	m_brushPass.CreateSolidBrush(RGB(198, 239, 206));      // 연초록 (PASS)
	m_brushFail.CreateSolidBrush(RGB(255, 199, 206));      // 연빨강 (FAIL)
	m_brushUncertain.CreateSolidBrush(RGB(255, 235, 156)); // 연노랑 (UNCERTAIN)
	m_brushNormal.CreateSolidBrush(RGB(255, 255, 255));    // 흰색 (기본)

	// --- 시작 안내 로그 ---
	WriteLog(_T("[시스템] MetalGuard 초기화 완료. '원클릭 시작' 버튼을 눌러 검출을 시작하세요."));
	return TRUE;
}

// 원클릭 시작 (카메라 찾기 + 열기 + 연결 + 라이브 한번에)
void CPylonSampleProgramDlg::OnBnClickedOneClickStart()
{
	WriteLog(_T("[시스템] 원클릭 시작 - 카메라 초기화 중..."));

	// 1. 카메라 탐색
	GetSerialNumerFromFile();
	m_error = m_CameraManager.FindCamera(m_szCamName, m_szSerialNum, m_szInterface, &m_iCamNumber);
	if (m_error != 0 || m_iCamNumber == 0) {
		WriteLog(_T("[오류] 연결된 카메라를 찾지 못했습니다. 카메라 연결을 확인하세요."));
		AfxMessageBox(_T("카메라를 찾지 못했습니다.\n카메라 연결 상태를 확인하세요."));
		return;
	}

	// 2. 리스트 갱신
	m_ctrlCamList.DeleteAllItems();
	for (int i = 0; i < m_iCamNumber; i++) {
		CString name(m_szCamName[i]);
		CString serial(m_szSerialNum[i]);
		m_ctrlCamList.InsertItem(i, name);
		m_ctrlCamList.SetItemText(i, 1, _T("Inspect0"));
		m_ctrlCamList.SetItemText(i, 2, serial);
		m_ctrlCamList.SetItemText(i, 3, _T("발견됨"));
	}
	m_iCameraIndex = 0;
	m_iListIndex   = 0;

	// 3. 카메라 열기
	int nOpen = m_CameraManager.Open_Camera(0, m_iCamPosition[0]);
	if (nOpen != 0) {
		WriteLog(_T("[오류] 카메라 열기 실패."));
		return;
	}
	m_ctrlCamList.SetItemText(0, 3, _T("열림"));

	// 4. 카메라 연결
	int nConn = m_CameraManager.Connect_Camera(0, 0, 0, 0, 0, _T("Mono8"));
	if (nConn != 0) {
		WriteLog(_T("[오류] 카메라 연결 실패."));
		return;
	}
	m_ctrlCamList.SetItemText(0, 3, _T("연결됨"));

	// 5. 서버 연결
	if (m_CameraManager.ConnectToServer("10.10.10.109", 8000)) {
		SetDlgItemText(IDC_STATUS_SERVER, _T("연결됨 (10.10.10.109:8000)"));
		WriteLog(_T("[시스템] 운용 서버 연결 성공."));
	} else {
		SetDlgItemText(IDC_STATUS_SERVER, _T("서버 연결 실패"));
		WriteLog(_T("[경고] 서버 연결 실패. 카메라 단독 동작합니다."));
	}

	// 6. 버퍼 할당 + 라이브 시작
	AllocImageBuf();
	InitBitmap(0);
	bStopThread[0] = true;
	bLiveFlag[0]   = true;
	m_CameraManager.GrabLive(0, 0);
	m_nCamIndexBuf[0] = 0;
	AfxBeginThread(LiveGrabThreadCam0, &m_nCamIndexBuf[0]);

	SetDlgItemText(IDC_CAM0_LIVE, _T("라이브 정지"));
	WriteLog(_T("[시스템] 라이브 스트리밍 시작. 철판이 카메라 앞을 지나가면 자동 촬영됩니다."));
}

void CPylonSampleProgramDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) == IDM_ABOUTBOX) { CAboutDlg d; d.DoModal(); }
	else CDialog::OnSysCommand(nID, lParam);
}

void CPylonSampleProgramDlg::OnPaint()
{
	if (IsIconic()) {
		CPaintDC dc(this);
		SendMessage(WM_ICONERASEBKGND, (WPARAM)dc.GetSafeHdc(), 0);
		int x = (GetSystemMetrics(SM_CXICON) + 1) / 2;
		int y = (GetSystemMetrics(SM_CYICON) + 1) / 2;
		dc.DrawIcon(x, y, m_hIcon);
	} else {
		CPaintDC dc(this);
		CDialog::OnPaint();
	}
}

HCURSOR CPylonSampleProgramDlg::OnQueryDragIcon() { return (HCURSOR)m_hIcon; }

void CPylonSampleProgramDlg::OnBnClickedFindCamBtn()
{
	GetSerialNumerFromFile();
	m_error = m_CameraManager.FindCamera(m_szCamName, m_szSerialNum, m_szInterface, &m_iCamNumber);
	if (m_error == 0)
	{
		m_ctrlCamList.DeleteAllItems();
		for (int i = 0; i < m_iCamNumber; i++)
		{
			CString name(m_szCamName[i]);
			CString serial(m_szSerialNum[i]);
			m_ctrlCamList.InsertItem(i, name);
			m_ctrlCamList.SetItemText(i, 1, _T("Inspect0"));
			m_ctrlCamList.SetItemText(i, 2, serial);
			m_ctrlCamList.SetItemText(i, 3, _T("발견됨"));
		}
		WriteLog(_T("[시스템] 카메라 탐색 완료."));
	}
	else if (m_error == -1) { AfxMessageBox(_T("연결된 카메라가 없습니다.")); }
	else { AfxMessageBox(_T("Pylon 라이브러리 오류")); }
}

void CPylonSampleProgramDlg::GetSerialNumerFromFile(void)
{
	TCHAR buff[100];
	CString strTemp;
	for (int i = 0; i < CAM_NUM; i++) {
		strTemp.Format(_T("CAM%d"), i + 1);
		memset(buff, 0, sizeof(buff));
		GetPrivateProfileString(strTemp, _T("Serial"), _T(""), buff, sizeof(buff), _T(".\\CameraInfo.txt"));
		m_strCamSerial[i] = buff;
	}
}

void CPylonSampleProgramDlg::OnNMClickCameraList(NMHDR* pNMHDR, LRESULT* pResult)
{
	NM_LISTVIEW* p = (NM_LISTVIEW*)pNMHDR;
	m_iListIndex = p->iItem;
	SetDlgItemText(IDC_SELECT_CAMERA, m_ctrlCamList.GetItemText(m_iListIndex, 1));
	if      (m_ctrlCamList.GetItemText(p->iItem, 1) == _T("Inspect0")) m_iCameraIndex = 0;
	else if (m_ctrlCamList.GetItemText(p->iItem, 1) == _T("Inspect1")) m_iCameraIndex = 1;
	else if (m_ctrlCamList.GetItemText(p->iItem, 1) == _T("Inspect2")) m_iCameraIndex = 2;
	else if (m_ctrlCamList.GetItemText(p->iItem, 1) == _T("Inspect3")) m_iCameraIndex = 3;
	*pResult = 0;
}

void CPylonSampleProgramDlg::OnBnClickedOpenCameraBtn()
{
	if (m_iCameraIndex == -1 && m_iCamNumber > 0) { m_iCameraIndex = 0; m_iListIndex = 0; }
	if (m_iCameraIndex != -1) {
		int e = m_CameraManager.Open_Camera(m_iCameraIndex, m_iCamPosition[m_iCameraIndex]);
		if (e == 0)       m_ctrlCamList.SetItemText(m_iListIndex, 3, _T("열림"));
		else if (e == -1) m_ctrlCamList.SetItemText(m_iListIndex, 3, _T("이미 열림"));
		else              m_ctrlCamList.SetItemText(m_iListIndex, 3, _T("열기 실패"));
	} else AfxMessageBox(_T("리스트에서 카메라를 먼저 선택하세요."));
}

void CPylonSampleProgramDlg::OnBnClickedCloseCamBtn()
{
	if (m_CameraManager.m_bCamOpenFlag[m_iCameraIndex])
	{
		if (m_CameraManager.Close_Camera(m_iCameraIndex) == 0)
		{
			m_ctrlCamList.SetItemText(m_iListIndex, 3, _T("닫힘"));
			if (bitmapinfo[m_iCameraIndex]) { delete bitmapinfo[m_iCameraIndex]; bitmapinfo[m_iCameraIndex] = NULL; }
			if (pImageColorDestBuffer[m_iCameraIndex]) {
				for (int i = 0; i < BUF_NUM; i++) free(pImageColorDestBuffer[m_iCameraIndex][i]);
				free(pImageColorDestBuffer[m_iCameraIndex]); pImageColorDestBuffer[m_iCameraIndex] = NULL;
			}
		}
		else m_ctrlCamList.SetItemText(m_iListIndex, 3, _T("닫기 실패"));
	}
}

void CPylonSampleProgramDlg::OnBnClickedConnectCameraBtn()
{
	if (!m_ctrlCamList.GetSafeHwnd()) return;
	int nIndex = m_ctrlCamList.GetNextItem(-1, LVNI_SELECTED);
	if (nIndex < 0 || nIndex >= CAM_NUM) { AfxMessageBox(_T("연결할 카메라를 선택하세요.")); return; }
	m_iCameraIndex = nIndex;

	int nRet = m_CameraManager.Connect_Camera(m_iCameraIndex, 0, 0, 0, 0, _T("Mono8"));
	if (nRet == 0)
	{
		m_ctrlCamList.SetItemText(m_iCameraIndex, 3, _T("연결됨"));
		if (m_CameraManager.ConnectToServer("10.10.10.109", 8000)) {
			SetDlgItemText(IDC_STATUS_SERVER, _T("연결됨 (10.10.10.109:8000)"));
		} else {
			SetDlgItemText(IDC_STATUS_SERVER, _T("서버 연결 실패"));
		}
	}
	else { m_ctrlCamList.SetItemText(m_iCameraIndex, 3, _T("연결 실패")); AfxMessageBox(_T("카메라 연결에 실패했습니다.")); }
}

void CPylonSampleProgramDlg::OnBnClickedGrabSingleBtn()
{
	if (m_iCameraIndex < 0 || !m_CameraManager.m_bCamConnectFlag[m_iCameraIndex]) return;
	try {
		bLiveFlag[m_iCameraIndex] = false;
		if (m_CameraManager.SingleGrab(m_iCameraIndex) == 0 &&
		    m_CameraManager.pImage24Buffer[m_iCameraIndex] &&
		    pImageColorDestBuffer[m_iCameraIndex] &&
		    pImageColorDestBuffer[m_iCameraIndex][0])
		{
			memcpy(pImageColorDestBuffer[m_iCameraIndex][0], m_CameraManager.pImage24Buffer[m_iCameraIndex], 260*260*3);
			DisplayCam0(pImageColorDestBuffer[0][0]);
			m_CameraManager.ReadEnd(m_iCameraIndex);
			bLiveFlag[m_iCameraIndex] = true;
		}
	} catch (...) {}
}

void CPylonSampleProgramDlg::AllocImageBuf(void)
{
	UpdateData();
	if (pImageColorDestBuffer[m_iCameraIndex] == NULL)
	{
		pImageColorDestBuffer[m_iCameraIndex] = (unsigned char**)malloc(BUF_NUM * sizeof(unsigned char*));
		for (int i = 0; i < BUF_NUM; i++) {
			pImageColorDestBuffer[m_iCameraIndex][i] = (unsigned char*)malloc(260*260*3);
			memset(pImageColorDestBuffer[m_iCameraIndex][i], 0, 260*260*3);
		}
	}
	hWnd[0] = GetDlgItem(IDC_CAM0_DISPLAY)->GetSafeHwnd();
	GetDlgItem(IDC_CAM0_DISPLAY)->GetClientRect(&rectStaticClient[0]);
	hdc[0] = ::GetDC(hWnd[0]);
}

void CPylonSampleProgramDlg::InitBitmap(int nCamIndex)
{
	if (bitmapinfo[nCamIndex]) { delete bitmapinfo[nCamIndex]; bitmapinfo[nCamIndex] = NULL; }
	bitmapinfo[nCamIndex] = (BITMAPINFO*)(new char[sizeof(BITMAPINFOHEADER) + 256*sizeof(RGBQUAD)]);
	bitmapinfo[nCamIndex]->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	bitmapinfo[nCamIndex]->bmiHeader.biWidth = 260;
	bitmapinfo[nCamIndex]->bmiHeader.biHeight = -260;
	bitmapinfo[nCamIndex]->bmiHeader.biPlanes = 1;
	bitmapinfo[nCamIndex]->bmiHeader.biCompression = BI_RGB;
	bitmapinfo[nCamIndex]->bmiHeader.biBitCount = 24;
	bitmapinfo[nCamIndex]->bmiHeader.biSizeImage = 260*260*3;
	bitmapinfo[nCamIndex]->bmiHeader.biClrUsed = 0;
}

void CPylonSampleProgramDlg::DisplayCam0(void* p)
{
	if (!p || !hdc[0]) return;
	SetStretchBltMode(hdc[0], COLORONCOLOR);
	StretchDIBits(hdc[0],0,0,rectStaticClient[0].Width(),rectStaticClient[0].Height(),0,0,260,260,p,bitmapinfo[0],DIB_RGB_COLORS,SRCCOPY);
}
void CPylonSampleProgramDlg::DisplayCam1(void* p) { DisplayCam0(p); }
void CPylonSampleProgramDlg::DisplayCam2(void* p) { DisplayCam0(p); }
void CPylonSampleProgramDlg::DisplayCam3(void* p) { DisplayCam0(p); }

void CPylonSampleProgramDlg::OnBnClickedCam0Live()
{
	if (!m_CameraManager.m_bCamConnectFlag[0]) { AfxMessageBox(_T("먼저 카메라를 연결하세요.")); CButton*p=(CButton*)GetDlgItem(IDC_CAM0_LIVE); if(p)p->SetCheck(0); return; }
	bStopThread[0] = !bStopThread[0];
	if (bStopThread[0]) {
		bLiveFlag[0]=true; m_CameraManager.GrabLive(0,0);
		SetDlgItemText(IDC_CAM0_LIVE,_T("라이브 정지"));
		m_nCamIndexBuf[0]=0; AfxBeginThread(LiveGrabThreadCam0,&m_nCamIndexBuf[0]);
	} else {
		bLiveFlag[0]=false; SetDlgItemText(IDC_CAM0_LIVE,_T("라이브 시작"));
		m_CameraManager.LiveStop(0,0);
	}
}

void CPylonSampleProgramDlg::OnBnClickedCam1Live() { OnBnClickedCam0Live(); }
void CPylonSampleProgramDlg::OnBnClickedCam2Live() { OnBnClickedCam0Live(); }
void CPylonSampleProgramDlg::OnBnClickedCam3Live() { OnBnClickedCam0Live(); }

void CPylonSampleProgramDlg::OnBnClickedTwoCameraLiveBtn()
{
	OnBnClickedCam0Live();
}

void CPylonSampleProgramDlg::OnBnClickedSoftTrigBtn()
{
	m_CameraManager.SetCommand(m_iCameraIndex, "TriggerSoftware");
}

void CPylonSampleProgramDlg::OnBnClickedExitBtn()
{
	if (m_CameraManager.m_bIsServerConnected) m_CameraManager.DisconnectFromServer();
	for (int k = 0; k < CAM_NUM; k++) {
		bStopThread[k] = false; m_CameraManager.LiveStop(k, 0);
		if (bitmapinfo[k]) { delete bitmapinfo[k]; bitmapinfo[k]=NULL; }
		if (pImageColorDestBuffer[k]) {
			for (int i=0;i<BUF_NUM;i++) if(pImageColorDestBuffer[k][i]) free(pImageColorDestBuffer[k][i]);
			free(pImageColorDestBuffer[k]); pImageColorDestBuffer[k]=NULL;
		}
		if (m_CameraManager.m_bCamOpenFlag[k]) m_CameraManager.Close_Camera(k);
	}
	CDialog::OnOK();
}

void CPylonSampleProgramDlg::OnBnClickedSaveImgBtn()
{
	CString strPath = _T("C:\\Temp");
	if (!PathFileExists(strPath)) CreateDirectory(strPath, NULL);
	char sz[256]; sprintf_s(sz,"C:\\Temp\\MetalGuard_Cam%d.bmp",m_iCameraIndex);
	if (m_CameraManager.pImage24Buffer[m_iCameraIndex])
	{
		int r = m_CameraManager.SaveImage(0,m_CameraManager.pImage24Buffer[m_iCameraIndex],sz,1,260,260,3);
		AfxMessageBox(r==0?_T("저장 성공"):_T("저장 실패"));
	} else AfxMessageBox(_T("저장할 이미지가 없습니다."));
}

void CPylonSampleProgramDlg::OnBnClickedButton5()
{
	static bool bTrig=false;
	if(!bTrig){SetDlgItemText(IDC_BUTTON5,_T("트리거 해제"));bTrig=true;m_CameraManager.SetEnumeration(m_iCameraIndex,"FrameStart","TriggerSelector");m_CameraManager.SetEnumeration(m_iCameraIndex,"On","TriggerMode");m_CameraManager.SetEnumeration(m_iCameraIndex,"Software","TriggerSource");}
	else{SetDlgItemText(IDC_BUTTON5,_T("트리거 설정"));bTrig=false;m_CameraManager.SetEnumeration(m_iCameraIndex,"Off","TriggerMode");}
}

void CPylonSampleProgramDlg::OnNMClickListCam(NMHDR* pNMHDR, LRESULT* pResult)
{
	LPNMITEMACTIVATE p=reinterpret_cast<LPNMITEMACTIVATE>(pNMHDR);
	if(p->iItem!=-1){m_iListIndex=p->iItem;m_iCameraIndex=p->iItem;}
	*pResult=0;
}

void CPylonSampleProgramDlg::OnBnClickedReclassifyBtn()
{
	if (m_CameraManager.m_nCurrentMode == SystemMode::NORMAL) {
		m_CameraManager.m_nCurrentMode = SystemMode::RECLASSIFY;
		SetDlgItemText(IDC_RECLASSIFY_BTN, _T("일반모드 복귀"));
		CComboBox*p=(CComboBox*)GetDlgItem(IDC_MODE_COMBO); if(p)p->SetCurSel(1);
		WriteLog(_T("[모드] UNCERTAIN 재분류 모드로 전환"));
	} else {
		m_CameraManager.m_nCurrentMode = SystemMode::NORMAL;
		SetDlgItemText(IDC_RECLASSIFY_BTN, _T("재분류 모드"));
		CComboBox*p=(CComboBox*)GetDlgItem(IDC_MODE_COMBO); if(p)p->SetCurSel(0);
		WriteLog(_T("[모드] 실시간 검출 모드로 전환"));
	}
}

void CPylonSampleProgramDlg::OnCbnSelchangeModeCombo()
{
	CComboBox*p=(CComboBox*)GetDlgItem(IDC_MODE_COMBO); if(!p)return;
	if(p->GetCurSel()==1){m_CameraManager.m_nCurrentMode=SystemMode::RECLASSIFY;SetDlgItemText(IDC_RECLASSIFY_BTN,_T("일반모드 복귀"));WriteLog(_T("[모드] UNCERTAIN 재분류 모드"));}
	else{m_CameraManager.m_nCurrentMode=SystemMode::NORMAL;SetDlgItemText(IDC_RECLASSIFY_BTN,_T("재분류 모드"));WriteLog(_T("[모드] 실시간 검출 모드"));}
}

UINT CPylonSampleProgramDlg::ThreadReclassify(LPVOID pParam)
{
	ReclassifyParam*p=static_cast<ReclassifyParam*>(pParam);
	if(p){p->pDlg->m_CameraManager.SendImageToAI(0,p->matImage,p->nPlateId,1,CmdID::IMG_RECLASSIFY);delete p;}
	return 0;
}

// 로그 헬퍼 (UI 스레드 안전)
void CPylonSampleProgramDlg::WriteLog(const CString& strMsg)
{
	if (g_hMainWnd == NULL || !::IsWindow(g_hMainWnd)) return;
	CString* pStr = new CString(strMsg);
	::PostMessage(g_hMainWnd, WM_UPDATE_LOG, (WPARAM)pStr, 0);
}

LRESULT CPylonSampleProgramDlg::OnUpdateLog(WPARAM wParam, LPARAM lParam)
{
	CString* pStr = (CString*)wParam;
	if (!pStr) return 0;
	CListBox* pLB = (CListBox*)GetDlgItem(IDC_LIST_LOG);
	if (pLB) {
		int n = pLB->AddString(*pStr);
		pLB->SetCurSel(n);
		if (pLB->GetCount() > 500) pLB->DeleteString(0);
	}
	delete pStr;
	return 0;
}

// 판정 결과 UI 업데이트 (WM_UPDATE_VERDICT)
LRESULT CPylonSampleProgramDlg::OnUpdateVerdict(WPARAM wParam, LPARAM lParam)
{
	VerdictData* pV = reinterpret_cast<VerdictData*>(wParam);
	if (!pV) return 0;

	CString sv(pV->verdict.c_str());
	CString sd(pV->defect.c_str());
	CString tmp;

	// 현재 판정 저장 후 색상 갱신 트리거
	m_strCurrentVerdict = sv;

	SetDlgItemText(IDC_VERDICT_DISPLAY, sv);
	SetDlgItemText(IDC_DEFECT_CLASS, sd);

	tmp.Format(_T("%.1f %%"), pV->prob_normal);  SetDlgItemText(IDC_PROB_NORMAL,  tmp);
	tmp.Format(_T("%.1f %%"), pV->prob_crack);   SetDlgItemText(IDC_PROB_CRACK,   tmp);
	tmp.Format(_T("%.1f %%"), pV->prob_hole);    SetDlgItemText(IDC_PROB_HOLE,    tmp);
	tmp.Format(_T("%.1f %%"), pV->prob_rust);    SetDlgItemText(IDC_PROB_RUST,    tmp);
	tmp.Format(_T("%.1f %%"), pV->prob_scratch); SetDlgItemText(IDC_PROB_SCRATCH, tmp);
	tmp.Format(_T("%.1f ms"), pV->inference_ms); SetDlgItemText(IDC_INFER_MS,     tmp);
	tmp.Format(_T("%.1f ms"), pV->pipeline_ms);  SetDlgItemText(IDC_PIPELINE_MS,  tmp);

	// LED
	{ CString s = (pV->verdict=="PASS")      ? _T("ON") : _T("OFF"); SetDlgItemText(IDC_LED_GREEN,  s); }
	{ CString s = (pV->verdict=="FAIL")      ? _T("ON") : _T("OFF"); SetDlgItemText(IDC_LED_RED,    s); }
	{ CString s = (pV->verdict=="UNCERTAIN") ? _T("ON") : _T("OFF"); SetDlgItemText(IDC_LED_YELLOW, s); }

	// 부저
	if      (pV->verdict=="PASS")      SetDlgItemText(IDC_CAM0_INFO,_T("없음"));
	else if (pV->verdict=="FAIL")      SetDlgItemText(IDC_CAM0_INFO,_T("단음 (FAIL)"));
	else if (pV->verdict=="UNCERTAIN") SetDlgItemText(IDC_CAM0_INFO,_T("이중음 (UNCERTAIN)"));
	else                               SetDlgItemText(IDC_CAM0_INFO,_T("단음 (TIMEOUT)"));

	// 게이트
	{ CString s = (pV->verdict=="FAIL")      ? _T("구동 중") : _T("대기"); SetDlgItemText(IDC_STATUS_GATE_A, s); }
	{ CString s = (pV->verdict=="UNCERTAIN") ? _T("구동 중") : _T("대기"); SetDlgItemText(IDC_STATUS_GATE_B, s); }

	// 현재 모드 문자열
	CString strMode = (m_CameraManager.m_nCurrentMode == SystemMode::RECLASSIFY)
	                  ? _T("재분류") : _T("일반");

	// DB 테이블 행 추가 (모드 컬럼 포함)
	CTime t = CTime::GetCurrentTime();
	CString strTime = t.Format(_T("%Y-%m-%d %H:%M:%S"));
	m_nDBRowCount++;
	int nRow = m_listDB.InsertItem(0, _T(""));
	tmp.Format(_T("%d"), m_nDBRowCount);      m_listDB.SetItemText(nRow, 0, tmp);
	m_listDB.SetItemText(nRow, 1, strTime);
	tmp.Format(_T("%d"), pV->plate_id);       m_listDB.SetItemText(nRow, 2, tmp);
	m_listDB.SetItemText(nRow, 3, strMode);   // 모드 컬럼
	m_listDB.SetItemText(nRow, 4, sv);
	tmp.Format(_T("%.0f%%"),pV->prob_normal);  m_listDB.SetItemText(nRow,5,tmp);
	tmp.Format(_T("%.0f%%"),pV->prob_crack);   m_listDB.SetItemText(nRow,6,tmp);
	tmp.Format(_T("%.0f%%"),pV->prob_hole);    m_listDB.SetItemText(nRow,7,tmp);
	tmp.Format(_T("%.0f%%"),pV->prob_rust);    m_listDB.SetItemText(nRow,8,tmp);
	tmp.Format(_T("%.0f%%"),pV->prob_scratch); m_listDB.SetItemText(nRow,9,tmp);
	tmp.Format(_T("%.1f"),  pV->inference_ms); m_listDB.SetItemText(nRow,10,tmp);
	tmp.Format(_T("%.1f"),  pV->pipeline_ms);  m_listDB.SetItemText(nRow,11,tmp);
	if (m_listDB.GetItemCount()>100) m_listDB.DeleteItem(m_listDB.GetItemCount()-1);

	// 컨트롤 색상 갱신 (판정 결과 패널 전체)
	if (GetDlgItem(IDC_VERDICT_DISPLAY)) GetDlgItem(IDC_VERDICT_DISPLAY)->Invalidate();
	if (GetDlgItem(IDC_LED_GREEN))       GetDlgItem(IDC_LED_GREEN)->Invalidate();
	if (GetDlgItem(IDC_LED_RED))         GetDlgItem(IDC_LED_RED)->Invalidate();
	if (GetDlgItem(IDC_LED_YELLOW))      GetDlgItem(IDC_LED_YELLOW)->Invalidate();
	if (GetDlgItem(IDC_PROB_NORMAL))     GetDlgItem(IDC_PROB_NORMAL)->Invalidate();
	if (GetDlgItem(IDC_PROB_CRACK))      GetDlgItem(IDC_PROB_CRACK)->Invalidate();
	if (GetDlgItem(IDC_PROB_HOLE))       GetDlgItem(IDC_PROB_HOLE)->Invalidate();
	if (GetDlgItem(IDC_PROB_RUST))       GetDlgItem(IDC_PROB_RUST)->Invalidate();
	if (GetDlgItem(IDC_PROB_SCRATCH))    GetDlgItem(IDC_PROB_SCRATCH)->Invalidate();

	delete pV;
	return 0;
}

// 판정 결과에 따른 컨트롤 배경색 처리
HBRUSH CPylonSampleProgramDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	HBRUSH hbr = CDialog::OnCtlColor(pDC, pWnd, nCtlColor);

	if (nCtlColor == CTLCOLOR_STATIC)
	{
		UINT nID = pWnd->GetDlgCtrlID();

		// 판정 결과 텍스트 (크게 표시되는 PASS/FAIL/UNCERTAIN)
		if (nID == IDC_VERDICT_DISPLAY)
		{
			if (m_strCurrentVerdict == _T("PASS")) {
				pDC->SetTextColor(RGB(0, 128, 0));   // 진초록
				pDC->SetBkColor(RGB(198, 239, 206)); // 연초록 배경
				return (HBRUSH)m_brushPass;
			}
			else if (m_strCurrentVerdict == _T("FAIL")) {
				pDC->SetTextColor(RGB(180, 0, 0));   // 진빨강
				pDC->SetBkColor(RGB(255, 199, 206)); // 연빨강 배경
				return (HBRUSH)m_brushFail;
			}
			else if (m_strCurrentVerdict == _T("UNCERTAIN")) {
				pDC->SetTextColor(RGB(130, 90, 0));  // 진황색
				pDC->SetBkColor(RGB(255, 235, 156)); // 연노랑 배경
				return (HBRUSH)m_brushUncertain;
			}
		}

		// AI 확률값 텍스트 (최고 확률 클래스에 색상)
		if (nID == IDC_PROB_NORMAL || nID == IDC_PROB_CRACK ||
		    nID == IDC_PROB_HOLE   || nID == IDC_PROB_RUST  || nID == IDC_PROB_SCRATCH)
		{
			if (m_strCurrentVerdict == _T("PASS")) {
				if (nID == IDC_PROB_NORMAL) {
					pDC->SetTextColor(RGB(0, 128, 0));
					pDC->SetBkColor(RGB(198, 239, 206));
					return (HBRUSH)m_brushPass;
				}
			}
			else if (m_strCurrentVerdict == _T("FAIL")) {
				if (nID != IDC_PROB_NORMAL) {
					pDC->SetTextColor(RGB(180, 0, 0));
					pDC->SetBkColor(RGB(255, 199, 206));
					return (HBRUSH)m_brushFail;
				}
			}
			else if (m_strCurrentVerdict == _T("UNCERTAIN")) {
				pDC->SetTextColor(RGB(130, 90, 0));
				pDC->SetBkColor(RGB(255, 235, 156));
				return (HBRUSH)m_brushUncertain;
			}
		}

		// LED 상태 텍스트 색상
		if (nID == IDC_LED_GREEN) {
			if (m_strCurrentVerdict == _T("PASS")) {
				pDC->SetTextColor(RGB(0, 160, 0));
				pDC->SetBkColor(RGB(198, 239, 206));
				return (HBRUSH)m_brushPass;
			}
		}
		if (nID == IDC_LED_RED) {
			if (m_strCurrentVerdict == _T("FAIL")) {
				pDC->SetTextColor(RGB(200, 0, 0));
				pDC->SetBkColor(RGB(255, 199, 206));
				return (HBRUSH)m_brushFail;
			}
		}
		if (nID == IDC_LED_YELLOW) {
			if (m_strCurrentVerdict == _T("UNCERTAIN")) {
				pDC->SetTextColor(RGB(160, 100, 0));
				pDC->SetBkColor(RGB(255, 235, 156));
				return (HBRUSH)m_brushUncertain;
			}
		}

		// DB 테이블 행 색상은 NM_CUSTOMDRAW로 처리 (아래 참조)
	}

	return hbr;
}

// DB 테이블 행 색상 (판정 결과에 따라)
void CPylonSampleProgramDlg::OnNMCustomdrawDbList(NMHDR* pNMHDR, LRESULT* pResult)
{
	NMLVCUSTOMDRAW* pNMCD = reinterpret_cast<NMLVCUSTOMDRAW*>(pNMHDR);
	*pResult = CDRF_DODEFAULT;

	switch (pNMCD->nmcd.dwDrawStage)
	{
	case CDDS_PREPAINT:
		*pResult = CDRF_NOTIFYITEMDRAW;
		break;

	case CDDS_ITEMPREPAINT:
	{
		int nRow = (int)pNMCD->nmcd.dwItemSpec;
		// 판정 컬럼(4번)에서 텍스트 가져오기
		TCHAR szVerdict[32] = { 0 };
		LVITEM lvi = { 0 };
		lvi.mask       = LVIF_TEXT;
		lvi.iItem      = nRow;
		lvi.iSubItem   = 4;
		lvi.pszText    = szVerdict;
		lvi.cchTextMax = 32;
		m_listDB.GetItem(&lvi);

		CString strV(szVerdict);
		if (strV == _T("PASS")) {
			pNMCD->clrText   = RGB(0, 128, 0);
			pNMCD->clrTextBk = RGB(198, 239, 206);
		}
		else if (strV == _T("FAIL")) {
			pNMCD->clrText   = RGB(180, 0, 0);
			pNMCD->clrTextBk = RGB(255, 199, 206);
		}
		else if (strV == _T("UNCERTAIN")) {
			pNMCD->clrText   = RGB(130, 90, 0);
			pNMCD->clrTextBk = RGB(255, 235, 156);
		}
		*pResult = CDRF_NEWFONT;
		break;
	}
	default:
		*pResult = CDRF_DODEFAULT;
		break;
	}
}
