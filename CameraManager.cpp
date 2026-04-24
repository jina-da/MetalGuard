#include "stdafx.h"
#include "CameraManager.h"
#include "MetalGuardTypes.h"
#include "resource.h"
#include <time.h>
#include <opencv2/opencv.hpp>
#include <json.hpp>


#ifdef _DEBUG
#pragma comment(lib, "opencv_world4130d.lib")
#else
#pragma comment(lib, "opencv_world4130.lib")
#endif

// 네임스페이스 선언
using namespace cv;
using namespace Pylon;
using namespace GenICam;

extern HWND g_hMainWnd;

CCameraManager::CCameraManager(void)
{
	Pylon::PylonInitialize();

	// --- 통신 및 상태 관련 초기화 ---
	m_hSocket = INVALID_SOCKET;
	m_bIsServerConnected = false; // 서버 연결 상태 플래그
	m_serverIP = "10.10.10.109";
	m_serverPort = 8000;

	// --- 전역 촬영 설정 초기화 ---
	nTotalShots = 4; // 요구사항에 따른 기본 촬영 횟수 설정

	for (int i = 0; i < CAM_NUM; i++)
	{
		// 라이브 이미지 버퍼 초기화
		m_matLiveImage[i] = cv::Mat::zeros(1200, 1920, CV_8UC3);
		m_bCaptureEnd[i] = false;
		m_bRemoveCamera[i] = false;
		m_bCamConnectFlag[i] = false;
		m_bCamOpenFlag[i] = false;
		m_iGrabbedFrame[i] = 0;
		pImage24Buffer[i] = NULL;

		// --- 카메라별 순번 및 전송 시간 초기화 ---
		nShotIndex[i] = 1;      // 1번 사진부터 시작
		m_strJsonBody[i] = "";  // 빈 문자열로 초기화
		m_dwLastSendTime[i] = 0;        // 마지막 전송 시간 초기화
		m_nConsecutiveDetect[i] = 0;    // 연속 감지 카운터 초기화
		m_dwLightChangeTime[i] = 0;     // 조명 변화 시각 초기화
	}
}

CCameraManager::~CCameraManager(void)
{
	for (int i = 0; i < CAM_NUM; i++)
	{
		if (pImage24Buffer[i] != NULL)
		{
			delete[] pImage24Buffer[i];
			pImage24Buffer[i] = NULL;
		}
	}
	Pylon::PylonTerminate();
}

int CCameraManager::FindCamera(char szCamName[CAM_NUM][100],char szCamSerialNumber[CAM_NUM][100],char szInterfacName[CAM_NUM][100],int *nCamNumber)
{
    try
    {

		     bLogUse =true;
			if(bLogUse==true)
			{
			    time(&t);
				strftime(filename, sizeof(filename), "CameraManager_%Y_%m_%d_%H_%M_%S.log",localtime(&t) );	
   				fopen_s(&log,filename,"w");
	
 
				CTime t = CTime::GetCurrentTime();
				Hour = t.GetHour();
				Min = t.GetMinute();
				Sec = t.GetSecond();    		
			}
			
			m_tlFactory = &CTlFactory::GetInstance ();
			devices.clear ();
			int iCamnumber=-1;  
			if ( m_tlFactory->EnumerateDevices(devices) == 0 )
			{
				if(	bLogUse==true)
				{
        			fprintf(log,"[%d시_%d분_%d초] [FindCamera:: No Camera ]\n",Hour,Min,Sec);
				}
				return -1;
			}
			else
			{
				
				m_pCamera.Initialize(CAM_NUM);
				for(DeviceInfoList_t::iterator it = devices.begin (); it != devices.end (); it++)
				{
					iCamnumber++;
					strcpy_s(szInterfacName[iCamnumber],(*it).GetDeviceClass().c_str());
					strcpy_s(szCamName[iCamnumber],(*it).GetModelName().c_str());
					strcpy_s(szCamSerialNumber[iCamnumber],(*it).GetSerialNumber().c_str());
					
				}
				*nCamNumber=iCamnumber+1;
			  



				return 0;
			}
	}
    catch (GenICam::GenericException &e)
    {
			// Error handling
			CString error =  (CString)e.GetDescription();
			if(	bLogUse==true)
			{
				WriteLog(100,_T("[ FindCamera ]\n"),error);
			}
			return -2;
    }
}

int CCameraManager::Open_Camera(int nCamIndex, int nPosition)
{
	try
	{
		if (m_pCamera[nCamIndex].IsOpen()) // 카메라가 이미 open 된경우 보호
		{
			return -1;
		}
		else
		{
			m_bRemoveCamera[nCamIndex] = false;
			m_bCamConnectFlag[nCamIndex] = false;
			m_bCamOpenFlag[nCamIndex] = false;
		}

		// 장치 연결
		m_pCamera[nCamIndex].Attach(m_tlFactory->CreateDevice(devices[0]));

		// --- 오류 수정 구간 시작 ---
		// Pylon 5~7 버전에서는 RegistrationMode와 Ownership을 명확히 구분해야 합니다.
		// Ownership_ExternalOwnership 대신 Pylon::Cleanup_None을 사용하는 경우도 많으나,
		// 현재 코드의 의도를 유지하기 위해 경로를 완전히 명시합니다.

		// 1. 카메라 제거 콜백 등록
		m_pCamera[nCamIndex].RegisterConfiguration(
			this,
			Pylon::RegistrationMode_Append,
			Pylon::Cleanup_None  
		);

		// 2. Grab 완료 이벤트 핸들러 등록
		m_pCamera[nCamIndex].RegisterImageEventHandler(
			this,
			Pylon::RegistrationMode_Append,
			Pylon::Cleanup_None
		);

		m_pCamera[nCamIndex].Open();
		m_pCameraNodeMap[nCamIndex] = &m_pCamera[nCamIndex].GetNodeMap();
		m_bCamOpenFlag[nCamIndex] = true;

		return 0;
	}
	catch (GenICam::GenericException& e)
	{
		// Error handling
		CString error = (CString)e.GetDescription();
		if (bLogUse == true)
		{
			WriteLog(nCamIndex, _T("[ Open_Camera ]\n"), error);
		}
		return -2;
	}
}

int CCameraManager::Close_Camera(int nCamIndex)
{
	try
	{


		    m_pCameraNodeMap[ nCamIndex ] = NULL;
	 		m_pCamera[ nCamIndex ].Close();
			m_pCamera[ nCamIndex ].DestroyDevice();
			m_pCamera[ nCamIndex ].DetachDevice();
			
			m_bCamOpenFlag[nCamIndex] = false;
			m_bCamConnectFlag[nCamIndex] = false; 

			return 0;
	}
    catch (GenICam::GenericException &e)
    {
			// Error handling
			CString error =  (CString)e.GetDescription();
			if(	bLogUse==true)
			{
				WriteLog(nCamIndex,_T("[ Close_Camera ]\n"),error);
			}
			return -2;
    }
}

int CCameraManager::Connect_Camera(int nCamIndex, int nOffsetX, int nOffsetY, int nWidth, int nHeight, CString strImgFormat)
{
	try
	{
		m_pCamera[nCamIndex].MaxNumBuffer = BUF_NUM;
		m_iCM_reSizeWidth[nCamIndex] = (((nWidth * 8) + 31) / 32 * 4);

		// AOI 설정 
		int nTemp;
		GetIntegerMax(nCamIndex, &nTemp, "Width");
		if (nWidth > nTemp)
		{
			SetInteger(nCamIndex, nOffsetX, "OffsetX");
			SetInteger(nCamIndex, nWidth, "Width");
			m_iCM_OffsetX[nCamIndex] = nOffsetX;
			m_iCM_Width[nCamIndex] = nWidth;
		}
		else
		{
			SetInteger(nCamIndex, nWidth, "Width");
			m_iCM_Width[nCamIndex] = nWidth;
			SetInteger(nCamIndex, nOffsetX, "OffsetX");
			m_iCM_OffsetX[nCamIndex] = nOffsetX;
		}

		GetIntegerMax(nCamIndex, &nTemp, "Height");
		if (nHeight > nTemp)
		{
			SetInteger(nCamIndex, nOffsetY, "OffsetY");
			m_iCM_OffsetY[nCamIndex] = nOffsetY;
			SetInteger(nCamIndex, nHeight, "Height");
			m_iCM_Height[nCamIndex] = nHeight;
		}
		else
		{
			SetInteger(nCamIndex, nHeight, "Height");
			m_iCM_Height[nCamIndex] = nHeight;
			SetInteger(nCamIndex, nOffsetY, "OffsetY");
			m_iCM_OffsetY[nCamIndex] = nOffsetY;
		}

		SetEnumeration(nCamIndex, CT2A(strImgFormat), "PixelFormat");
		m_strCM_ImageForamt[nCamIndex] = strImgFormat;

		// 버퍼 할당 (260x260x3)
		if (pImage24Buffer[nCamIndex] != NULL)
		{
			delete[] pImage24Buffer[nCamIndex];
			pImage24Buffer[nCamIndex] = NULL;
		}

		size_t nBufferSize = 260 * 260 * 3;
		pImage24Buffer[nCamIndex] = new unsigned char[nBufferSize];
		memset(pImage24Buffer[nCamIndex], 0, nBufferSize);

		// 서버 전송용 상태 초기화
		// 실제 운용 시에는 이 값들을 외부에서 동적으로 받아올 수 있게 확장 가능
		m_bCamConnectFlag[nCamIndex] = true;

		return 0;
	}
	catch (const GenericException& e)
	{
		if (bLogUse == true)
		{
			WriteLog(nCamIndex, _T("[ Connect_Camera Exception ]\n"), (CString)e.GetDescription());
		}
		return -1;
	}
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////   SingleGrab
int CCameraManager::SingleGrab(int nCamIndex)
{
	try
	{
		if (!m_pCamera[nCamIndex].IsPylonDeviceAttached()) return -1;

		// 1. 이미 그랩 중(라이브)이라면 중단하지 말고 그대로 진행하거나
		// 만약 정지 상태라면 1장만 찍도록 시작
		if (!m_pCamera[nCamIndex].IsGrabbing())
		{
			m_pCamera[nCamIndex].StartGrabbing(1, GrabStrategy_LatestImageOnly);
		}

		CGrabResultPtr ptrGrabResult;
		// 2. RetrieveResult를 통해 스트림 스레드를 파괴하지 않고 데이터만 가로챔
		if (m_pCamera[nCamIndex].RetrieveResult(5000, ptrGrabResult, TimeoutHandling_ThrowException))
		{
			if (ptrGrabResult->GrabSucceeded())
			{
				uint8_t* pBuffer = (uint8_t*)ptrGrabResult->GetBuffer();
				int nW = (int)ptrGrabResult->GetWidth();
				int nH = (int)ptrGrabResult->GetHeight();

				cv::Mat matRaw = cv::Mat(nH, nW, CV_8UC1, pBuffer);

				// 안전한 ROI 설정 (이전의 방어 코드 적용)
				int startX = std::max(0, std::min(470, nW - 260));
				int startY = std::max(0, std::min(270, nH - 260));
				int targetW = std::min(1000, nW - startX);
				int targetH = std::min(1000, nH - startY);

				cv::Mat croppedImg = matRaw(cv::Rect(startX, startY, targetW, targetH)).clone();
				cv::Mat finalImg;

				cv::cvtColor(croppedImg, finalImg, cv::COLOR_GRAY2BGR);
				cv::resize(finalImg, finalImg, cv::Size(260, 260));

				if (pImage24Buffer[nCamIndex] != NULL)
				{
					memcpy(pImage24Buffer[nCamIndex], finalImg.data, 260 * 260 * 3);
				}

				m_bCaptureEnd[nCamIndex] = true;
				return 0;
			}
		}
		return -2;
	}
	catch (const GenericException& e)
	{
		TRACE(_T("SingleGrab Final Exception: %S\n"), e.GetDescription());
		return -3;
	}
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////    GrabLive
int CCameraManager::GrabLive(int nCamIndex,int nMode)
{
	try
	{
		    bStopFlag = true;
		    bStopLiveThread=true;
			if(nMode==0)
			{
		        m_pCamera[ nCamIndex ].StartGrabbing(GrabStrategy_OneByOne, GrabLoop_ProvidedByInstantCamera);
			}
			else
			{
				m_pCamera.StartGrabbing(GrabStrategy_OneByOne, GrabLoop_ProvidedByInstantCamera);
			}
			//m_pCamera[ nCamIndex ].StartGrabbing();
            //AfxBeginThread(LiveThread,this);
			return 0;
	}
    catch (GenICam::GenericException &e)
    {
			// Error handling
			CString error =  (CString)e.GetDescription();
			if(	bLogUse==true)
			{
				WriteLog(nCamIndex,_T("[ GrabLive ]\n"),error);
			}
			return -2;   
    }
}

UINT CCameraManager::LiveThread(void *lParam)
{        
	  CCameraManager* pDlg = (CCameraManager*)lParam;
	   
      CGrabResultPtr ptrGrabResult;
        while(pDlg->bStopLiveThread)  
		{
       
			
			pDlg->m_pCamera.RetrieveResult( 5000, ptrGrabResult, TimeoutHandling_ThrowException);
			if (ptrGrabResult->GrabSucceeded())
			{
				if(pDlg->m_strCM_ImageForamt[ptrGrabResult->GetCameraContext()]=="Mono8")
				{
					pDlg->converter[ptrGrabResult->GetCameraContext()].OutputPixelFormat = PixelType_Mono8;					
					pDlg->converter[ptrGrabResult->GetCameraContext()].Convert( pDlg->Image[ptrGrabResult->GetCameraContext()], ptrGrabResult);  					
					pDlg->pImage8Buffer[ptrGrabResult->GetCameraContext()] =(unsigned char*)pDlg->Image[ptrGrabResult->GetCameraContext()].GetBuffer();
				}
				else  // Bayer  && YUV422Packed 
				{

					pDlg->pImage8Buffer[ptrGrabResult->GetCameraContext()] = (unsigned char*)ptrGrabResult->GetBuffer();
					//pDlg->converter[ptrGrabResult->GetCameraContext()].OutputPixelFormat = PixelType_BGR8packed;
					//pDlg->converter[ptrGrabResult->GetCameraContext()].Convert( pCameraManager->Image[ptrGrabResult->GetCameraContext()], ptrGrabResult);  					
					//pDlg->pImage24Buffer[ptrGrabResult->GetCameraContext()] =(unsigned char*)pCameraManager->Image[ptrGrabResult->GetCameraContext()].GetBuffer();
						
						
				}

			}
            pDlg->m_bCaptureEnd[ptrGrabResult->GetCameraContext()] = true;
			if(pDlg->bStopFlag==false)
			{

				pDlg->bStopLiveThread = false;
			    pDlg->m_pCamera[ 0 ].StopGrabbing();	
			
			}
        }	  
	  
	return 0;
}
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////    LiveStop
int CCameraManager::LiveStop(int nCamIndex, int nMode)
{
	try
	{
		    bStopFlag = false;
			if(nMode==0)
			{
				m_pCamera[ nCamIndex ].StopGrabbing();	
			}
			else
			{
				m_pCamera.StopGrabbing();	

			}
			return 0;
	}
    catch (GenICam::GenericException &e)
    {
			// Error handling
			CString error =  (CString)e.GetDescription();
			if(	bLogUse==true)
			{
				WriteLog(nCamIndex,_T("[ LiveStop ]\n"),error);
			}
			return -2;   
    }
}

void CCameraManager::WriteLog(int nCamIdx, CString strStatus, CString strDetail)
{
	// 시간 계산 및 포맷팅
	CTime t = CTime::GetCurrentTime();
	int Hour = t.GetHour();
	int Min = t.GetMinute();
	int Sec = t.GetSecond();

	// Visual Studio 출력창(Output) 표시
	CString strConsole;
	strConsole.Format(_T("%s: [%04d-%02d-%02d %02d:%02d:%02d] CAM%d: %s\n"),
		strStatus, t.GetYear(), t.GetMonth(), t.GetDay(), Hour, Min, Sec, nCamIdx, strDetail);
	OutputDebugString(strConsole);

	// 파일 기록
	if (log != NULL)
	{
		fprintf(log, "[%02d:%02d:%02d] [ CAM : %d ] [%s] %s\n", Hour, Min, Sec, nCamIdx, (LPCSTR)CT2A(strStatus), (LPCSTR)CT2A(strDetail));
		fflush(log);
	}

	// UI 리스트박스 출력 (전역 핸들 사용)
	if (g_hMainWnd != NULL && ::IsWindow(g_hMainWnd))
	{
		HWND hWndList = ::GetDlgItem(g_hMainWnd, IDC_LIST_LOG);

		if (hWndList != NULL && ::IsWindow(hWndList))
		{
			CString strListLine;
			// [수정] 서버 판정 결과는 로그에서 더 잘 보이도록 특수문자 추가
			if (strStatus == _T("판정결과")) {
				strListLine.Format(_T("[%02d:%02d:%02d] ★ %s"), Hour, Min, Sec, strDetail);
			}
			else {
				strListLine.Format(_T("[%02d:%02d:%02d] CAM%d: %s"), Hour, Min, Sec, nCamIdx, strDetail);
			}

			// 스레드 안전을 위해 SendMessage 사용
			::SendMessage(hWndList, LB_ADDSTRING, 0, (LPARAM)(LPCTSTR)strListLine);

			// 자동 스크롤
			int nCount = (int)::SendMessage(hWndList, LB_GETCOUNT, 0, 0);
			::SendMessage(hWndList, LB_SETCURSEL, nCount - 1, 0);

			if (nCount > 500) ::SendMessage(hWndList, LB_DELETESTRING, 0, 0);
		}
	}
}

bool CCameraManager::CheckCaptureEnd(int nCamIndex)
{
	return m_bCaptureEnd[nCamIndex];
}

void CCameraManager::ReadEnd(int nCamIndex)
{
	m_bCaptureEnd[nCamIndex] = false; // 여기서 false를 해줘야 다음 콜백을 기다림
}
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/*
    Paramter 설정 함수 Read & Wirte
*/

//Enumeration Get/Set example
//char Temp[20];
//(CString)Temp==_T("");
//m_CameraManager.GetEnumeration(0,Temp,"GainAuto");          
//m_CameraManager.SetEnumeration(0,"Once","GainAuto");
//
//Integer Get/Set example
//int nTemp;
//m_CameraManager.GetInteger(0,&nTemp,"GainRaw");
//m_CameraManager.SetInteger(0,400,"GainRaw");
//
//Boolean  Get/Set example
//bool bTemp;
//m_CameraManager.GetBoolean(0,&bTemp,"ReverseX");
//m_CameraManager.SetBoolean(0,true,"ReverseX");

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
int CCameraManager::GetEnumeration(int nCamIndex, char *szValue, char *szNodeName)
{
    try
	{
			if(m_pCameraNodeMap[nCamIndex])
			{
			   ptrEnumeration[nCamIndex]	= m_pCameraNodeMap[nCamIndex]->GetNode(szNodeName);
			   if(IsAvailable(ptrEnumeration[nCamIndex])){
					strcpy_s(szValue,ptrEnumeration[nCamIndex]->ToString().length()+1,ptrEnumeration[nCamIndex]->ToString ());	
					return 0;
			   }else{
					CString str;
					str.Format(_T("Camera%d, It is not available to get %s\n"),nCamIndex, (CString)szNodeName);
					AfxMessageBox(str);
					return 1;
			   }
			}
			else
			{
				return -1;
			}
	}
	
    catch (GenICam::GenericException &e)
    {
			// Error handling
			CString error =  (CString)e.GetDescription();
			if(	bLogUse==true)
			{
				WriteLog(nCamIndex,_T("[ GetEnumeration ]\n"),error);
			}
			return -2;   
    }
}

int CCameraManager::SetEnumeration(int nCamIndex, char *szValue, char *szNodeName)
{
    try
	{
			if(m_pCameraNodeMap[nCamIndex])
			{
				ptrEnumeration[nCamIndex]	= m_pCameraNodeMap[nCamIndex]->GetNode(szNodeName);
				if(IsAvailable(ptrEnumeration[nCamIndex])){
					ptrEnumeration[nCamIndex]->FromString(szValue);
					return 0;
				}else{
					CString str;
					str.Format(_T("Camera%d, It is not available to set %s\n"),nCamIndex, (CString)szNodeName);
					AfxMessageBox(str);
					return 1;
			   }
			}
			else
			{
				return -1;
			}
	}
    catch (GenICam::GenericException &e)
    {
			// Error handling
			CString error =  (CString)e.GetDescription();
			if(	bLogUse==true)
			{
				WriteLog(nCamIndex,_T("[ SetEnumeration ]\n"),error);
			}
			return -2;   
    }
}

int CCameraManager::GetInteger(int nCamIndex, int *nValue, char *szNodeName)
{
    try
	{
		    if(m_pCameraNodeMap[nCamIndex])
			{
				ptrInteger[nCamIndex] =  m_pCameraNodeMap[nCamIndex]->GetNode(szNodeName);
				if(IsAvailable(ptrInteger[nCamIndex])){
					*nValue = (int) ptrInteger[nCamIndex]->GetValue ();
					return 0;
				}else{
					CString str;
					str.Format(_T("Camera%d, It is not available to get %s\n"),nCamIndex, (CString)szNodeName);
					AfxMessageBox(str);
					return 1;
			   }
			}
			else
			{
				return -1;
			}
	}
    catch (GenICam::GenericException &e)
    {
			// Error handling
			CString error =  (CString)e.GetDescription();
			if(	bLogUse==true)
			{
				WriteLog(nCamIndex,_T("[ GetInteger ]\n"),error);
			}
			return -2;   
    }
}

int CCameraManager::GetIntegerMax(int nCamIndex, int *nValue, char *szNodeName)
{
    try
	{
			if(m_pCameraNodeMap[nCamIndex])
			{
				ptrInteger[nCamIndex] =  m_pCameraNodeMap[nCamIndex]->GetNode(szNodeName);
				if(IsAvailable(ptrInteger[nCamIndex])){
					*nValue = (int) ptrInteger[nCamIndex]->GetMax();
					return 0;
				}else{
					CString str;
					str.Format(_T("Camera%d, It is not available to get %s\n"),nCamIndex, (CString)szNodeName);
					AfxMessageBox(str);
					return 1;
			   }
			}
			else
			{
				return -1;
			}
	}
    catch (GenICam::GenericException &e)
    {
			// Error handling
			CString error =  (CString)e.GetDescription();
			if(	bLogUse==true)
			{
				WriteLog(nCamIndex,_T("[ GetIntegerMax ]\n"),error);
			}
			return -2;   
    }
}

int CCameraManager::GetIntegerMin(int nCamIndex, int *nValue, char *szNodeName)
{
    try
	{
		    if(m_pCameraNodeMap[nCamIndex])
			{
				ptrInteger[nCamIndex] =  m_pCameraNodeMap[nCamIndex]->GetNode(szNodeName);
				if(IsAvailable(ptrInteger[nCamIndex])){
					*nValue = (int) ptrInteger[nCamIndex]->GetMin ();
					return 0;
				}else{
					CString str;
					str.Format(_T("Camera%d, It is not available to get %s\n"),nCamIndex, (CString)szNodeName);
					AfxMessageBox(str);
					return 1;
			   }
			}
			else
			{
				return -1;
			}
	}
    catch (GenICam::GenericException &e)
    {
			// Error handling
			CString error =  (CString)e.GetDescription();
			if(	bLogUse==true)
			{
				WriteLog(nCamIndex,_T("[ GetIntegerMin ]\n"),error);
			}
			return -2;   
    }
}

int CCameraManager::SetInteger(int nCamIndex, int nValue, char *szNodeName)
{
    try
	{
		   if(m_pCameraNodeMap[nCamIndex])
			{
				ptrInteger[nCamIndex]=  m_pCameraNodeMap[nCamIndex]->GetNode(szNodeName);
				if(IsAvailable(ptrInteger[nCamIndex])){
					ptrInteger[nCamIndex]->SetValue(nValue);
					return 0;
				}else{
					CString str;
					str.Format(_T("Camera%d, It is not available to set %s\n"),nCamIndex, (CString)szNodeName);
					AfxMessageBox(str);
					return 1;
			   }
			}
			else
			{
				return -1;
			}
	}
    catch (GenICam::GenericException &e)
    {
			// Error handling
			CString error =  (CString)e.GetDescription();
			if(	bLogUse==true)
			{
				WriteLog(nCamIndex,_T("[ SetInteger ]\n"),error);
			}
			return -2;   
    }
}

int CCameraManager::GetBoolean(int nCamIndex, bool *bValue, char *szNodeName)
{
    try
	{
		    if(m_pCameraNodeMap[nCamIndex])
			{
				ptrBoolean[nCamIndex] =  m_pCameraNodeMap[nCamIndex]->GetNode(szNodeName);
				if(IsAvailable(ptrInteger[nCamIndex])){
					*bValue = ptrBoolean[nCamIndex]->GetValue();
					return 0;
				}else{
					CString str;
					str.Format(_T("Camera%d, It is not available to get %s\n"),nCamIndex, (CString)szNodeName);
					AfxMessageBox(str);
					return 1;
			   }
					
			}
			else
			{
				return -1;
			}
	}
    catch (GenICam::GenericException &e)
    {
			// Error handling
			CString error =  (CString)e.GetDescription();
			if(	bLogUse==true)
			{
				WriteLog(nCamIndex,_T("[ GetBoolean ]\n"),error);
			}
			return -2;   
    }
}

int CCameraManager::SetBoolean(int nCamIndex, bool bValue, char *szNodeName)
{
    try
	{
		    if(m_pCameraNodeMap[nCamIndex])
			{
				ptrBoolean[nCamIndex] =  m_pCameraNodeMap[nCamIndex]->GetNode(szNodeName);
				if(IsAvailable(ptrInteger[nCamIndex])){
					ptrBoolean[nCamIndex]->SetValue(bValue);
					return 0;
				}else{
					CString str;
					str.Format(_T("Camera%d, It is not available to set %s\n"),nCamIndex, (CString)szNodeName);
					AfxMessageBox(str);
					return 1;
			   }
			}
			else
			{
				return -1;
			}
	}
    catch (GenICam::GenericException &e)
    {
			// Error handling
			CString error =  (CString)e.GetDescription();
			if(	bLogUse==true)
			{
				WriteLog(nCamIndex,_T("[ SetBoolean ]\n"),error);
			}
			return -2;   
    }
}

int CCameraManager::GetFloat(int nCamIndex, float *fValue, char *szNodeName)
{
    try
	{
		    if(m_pCameraNodeMap[nCamIndex])
			{
				ptrFloat[nCamIndex] =  m_pCameraNodeMap[nCamIndex]->GetNode(szNodeName);
				if(IsAvailable(ptrInteger[nCamIndex])){
					*fValue = (float)ptrFloat[nCamIndex]->GetValue();
					return 0;
				}else{
					CString str;
					str.Format(_T("Camera%d, It is not available to get %s\n"),nCamIndex, (CString)szNodeName);
					AfxMessageBox(str);
					return 1;
			   }
			}
			else
			{
				return -1;
			}
	}
    catch (GenICam::GenericException &e)
    {
			// Error handling
			CString error =  (CString)e.GetDescription();
			if(	bLogUse==true)
			{
				WriteLog(nCamIndex,_T("[ GetFloat ]\n"),error);
			}
			return -2;   
    }
}

int CCameraManager::SetFloat(int nCamIndex, float fValue, char *szNodeName)
{
    try
	{
		    if(m_pCameraNodeMap[nCamIndex])
			{
				ptrFloat[nCamIndex] =  m_pCameraNodeMap[nCamIndex]->GetNode(szNodeName);
				if(IsAvailable(ptrInteger[nCamIndex])){
					ptrFloat[nCamIndex]->SetValue(fValue);
					return 0;
				}else{
					CString str;
					str.Format(_T("Camera%d, It is not available to set %s\n"),nCamIndex, (CString)szNodeName);
					AfxMessageBox(str);
					return 1;
			   }
			}
			else
			{
				return -1;
			}
	}
    catch (GenICam::GenericException &e)
    {
			// Error handling
			CString error =  (CString)e.GetDescription();
			if(	bLogUse==true)
			{
				WriteLog(nCamIndex,_T("[ SetFloat ]\n"),error);
			}
			return -2;   
    }
}

int CCameraManager::SetCommand(int nCamIndex, char *szNodeName)
{
    try
	{
		    if(m_pCameraNodeMap[nCamIndex])
			{
				ptrCommand[nCamIndex] =  m_pCameraNodeMap[nCamIndex]->GetNode(szNodeName);
				if(IsAvailable(ptrInteger[nCamIndex])){
					ptrCommand[nCamIndex]->Execute();
					return 0;
				}else{
					CString str;
					str.Format(_T("Camera%d, It is not available to send cammand %s\n"),nCamIndex, (CString)szNodeName);
					AfxMessageBox(str);
					return 1;
			   }
			}
			else
			{
				return -1;
			}
	}
    catch (GenICam::GenericException &e)
    {
			// Error handling
			CString error =  (CString)e.GetDescription();
			if(	bLogUse==true)
			{
				WriteLog(nCamIndex,_T("[ SetCommand ]\n"),error);
			}
			return -2;   
    }
}

int CCameraManager::SaveImage(int nFileFormat, unsigned char* pImage, char* filename, int nPixelType, int width, int height, int nColorband)
{
	// 1. 기본 유효성 검사
	if (pImage == NULL || filename == NULL) {
		TRACE(_T("SaveImage Error: pImage or filename is NULL\n"));
		return -1;
	}

	try
	{
		// 2. 파일 포맷 설정
		EImageFileFormat ImageFileFormat;
		switch (nFileFormat)
		{
		case 0: ImageFileFormat = ImageFileFormat_Bmp; break;
		case 1: ImageFileFormat = ImageFileFormat_Tiff; break;
		case 2: ImageFileFormat = ImageFileFormat_Jpeg; break;
		case 3: ImageFileFormat = ImageFileFormat_Png; break;
		default: ImageFileFormat = ImageFileFormat_Bmp; break;
		}

		// 3. 픽셀 타입 및 규격 강제 동기화
		EPixelType ImagePixleType = PixelType_BGR8packed;
		int finalW = 260;
		int finalH = 260;
		int finalBand = 3;
		size_t finalBufferSize = (size_t)finalW * finalH * finalBand;

		// 4. Pylon 저장 함수 호출
		CImagePersistence::Save(
			ImageFileFormat,
			filename,
			pImage,
			finalBufferSize,
			ImagePixleType,
			finalW,
			finalH,
			0, // Padding
			ImageOrientation_TopDown
		);

		TRACE(_T("SaveImage Success: %S\n"), filename);
		return 0;
	}
	catch (const Pylon::GenericException& e)
	{
		TRACE(_T("Pylon Save Runtime Error: %S\n"), e.GetDescription());
		return -2;
	}
	catch (...)
	{
		TRACE(_T("SaveImage: Unknown Exception\n"));
		return -3;
	}
}

void CCameraManager::OnImagesSkipped( CInstantCamera& camera, size_t countOfSkippedImages)
{   

      m_iSkippiedFrame[CInstantCamera().GetCameraContext()] = countOfSkippedImages;
	 if(bLogUse==true)
	 {
		fprintf(log,"CAM = %d , SkippedFrame = %d\n",CInstantCamera().GetCameraContext(),m_iSkippiedFrame[CInstantCamera().GetCameraContext()]);

	 }
}

UINT CCameraManager::ThreadReceiveFromServer(LPVOID pParam)
{
	CCameraManager* pMgr = static_cast<CCameraManager*>(pParam);
	if (!pMgr) return 0;

	while (pMgr->m_bIsServerConnected)
	{
		PacketHeader header;
		// 헤더 수신 (8바이트)
		int nRet = recv(pMgr->m_hSocket, (char*)&header, sizeof(header), MSG_WAITALL);
		if (nRet <= 0) break; // 연결 종료 - 로그 없이 조용히 종료
		if (nRet != sizeof(header)) continue;

		// 시그니처 확인
		if (ntohs(header.signature) != 0x4D47) continue;

		uint16_t cmdId    = ntohs(header.cmdId);
		uint32_t bodySize = ntohl(header.bodySize);

		// bodySize == 0: PONG 등 바디 없는 패킷 스킵
		if (bodySize == 0) continue;
		if (bodySize > 1024 * 1024) continue; // 비정상 패킷 방지

		// 바디(JSON) 수신
		std::vector<char> buffer(bodySize + 1, 0);
		int nBodyRet = recv(pMgr->m_hSocket, buffer.data(), bodySize, MSG_WAITALL);
		if (nBodyRet <= 0) break;

		std::string jsonStr(buffer.data());

		// 판정 결과 패킷(301) 처리
		if (cmdId == 301)
		{
			try {
				auto j = nlohmann::json::parse(jsonStr);

				// VerdictData 힙 할당 후 메인 윈도우로 PostMessage
				VerdictData* pV = new VerdictData;
				pV->verdict      = j.value("verdict",      "UNKNOWN");
				pV->defect       = j.value("defect_class", "none");
				pV->prob_normal  = j.value("prob_normal",  0.f) * 100.f;
				pV->prob_crack   = j.value("prob_crack",   0.f) * 100.f;
				pV->prob_hole    = j.value("prob_hole",    0.f) * 100.f;
				pV->prob_rust    = j.value("prob_rust",    0.f) * 100.f;
				pV->prob_scratch = j.value("prob_scratch", 0.f) * 100.f;
				pV->inference_ms = j.value("inference_ms", 0.f);
				pV->pipeline_ms  = j.value("pipeline_ms",  0.f);
				pV->plate_id     = j.value("plate_id",     0);

				if (g_hMainWnd != NULL && ::IsWindow(g_hMainWnd))
					::PostMessage(g_hMainWnd, WM_UPDATE_VERDICT, (WPARAM)pV, 0);
				else
					delete pV;
			}
			catch (...) {
				// JSON 파싱 실패 - 조용히 무시 (PING/PONG 등 비JSON 패킷)
			}
		}
	}

	pMgr->m_bIsServerConnected = false;
	return 0;
}

// 3. 서버 연결 함수 (수신 스레드 시작 추가)
bool CCameraManager::ConnectToServer(std::string ip, int port)
{
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return false;

	m_hSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (m_hSocket == INVALID_SOCKET) return false;

	BOOL bOptVal = TRUE;
	setsockopt(m_hSocket, IPPROTO_TCP, TCP_NODELAY, (char*)&bOptVal, sizeof(BOOL));

	sockaddr_in serverAddr;
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(port);

	if (inet_pton(AF_INET, ip.c_str(), &serverAddr.sin_addr) <= 0) {
		closesocket(m_hSocket);
		return false;
	}

	if (connect(m_hSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
		closesocket(m_hSocket);
		m_bIsServerConnected = false;
		return false;
	}

	m_bIsServerConnected = true;

	// [추가] 서버에서 오는 판정 결과를 실시간으로 받기 위해 수신 스레드 시작
	AfxBeginThread(ThreadReceiveFromServer, this);

	return true;
}

// 4. 이미지 전송 함수 (기존 로직 유지)
void CCameraManager::SendImageToAI(int nCameraIndex, cv::Mat& matImage, int nPlateId, int nShotIdx, CmdID cmd)
{
	if (!m_bIsServerConnected || m_hSocket == INVALID_SOCKET) return;

	try {
		std::vector<uchar> imgBuf;
		std::vector<int> params = { cv::IMWRITE_JPEG_QUALITY, 90 };
		cv::imencode(".jpg", matImage, imgBuf, params);

		nlohmann::json j;
		j["client_id"] = "cam_01";
		j["plate_id"] = nPlateId;
		j["shot_index"] = nShotIdx;
		j["total_shots"] = 8; // 서버 요청에 따라 8장으로 수정됨
		std::string jsonPayload = j.dump();

		PacketHeader header;
		header.signature = htons(0x4D47);
		header.cmdId = htons((uint16_t)cmd);
		header.bodySize = htonl((uint32_t)jsonPayload.length());

		if (send(m_hSocket, (char*)&header, sizeof(header), 0) == SOCKET_ERROR) throw std::runtime_error("Header 전송 실패");
		if (send(m_hSocket, jsonPayload.c_str(), (int)jsonPayload.length(), 0) == SOCKET_ERROR) throw std::runtime_error("JSON 전송 실패");

		uint32_t netImageLen = htonl((uint32_t)imgBuf.size());
		send(m_hSocket, (char*)&netImageLen, sizeof(netImageLen), 0);
		if (send(m_hSocket, (char*)imgBuf.data(), (int)imgBuf.size(), 0) == SOCKET_ERROR) throw std::runtime_error("이미지 전송 실패");

		// 전송 로그는 간소화 (UI 복잡도 방지)
		if (nShotIdx == 8) {
			WriteLog(nCameraIndex, _T("정상"), _T("물체 전송 완료 (AI 판정 대기 중...)"));
		}
	}
	catch (const std::exception& e) {
		WriteLog(nCameraIndex, _T("에러"), (CString)e.what());
	}
}

// 5. 서버 연결 체크 및 나머지 유틸리티 함수 (기존 유지)
bool CCameraManager::CheckServerConnection()
{
	if (m_hSocket == INVALID_SOCKET) {
		m_bIsServerConnected = false;
		return false;
	}

	fd_set readfds;
	FD_ZERO(&readfds);
	FD_SET(m_hSocket, &readfds);

	timeval timeout = { 0, 0 };
	int result = select(0, &readfds, NULL, NULL, &timeout);

	if (result > 0) {
		char buf[1];
		if (recv(m_hSocket, buf, 1, MSG_PEEK) == 0) {
			closesocket(m_hSocket);
			m_hSocket = INVALID_SOCKET;
			m_bIsServerConnected = false;
			return false;
		}
	}
	return m_bIsServerConnected;
}

void CCameraManager::DisconnectFromServer()
{
	m_bIsServerConnected = false; // 수신 스레드 종료 유도
	if (m_hSocket != INVALID_SOCKET) {
		closesocket(m_hSocket);
		m_hSocket = INVALID_SOCKET;
		WSACleanup();
	}
}

// 백그라운드 전송 스레드 구현
UINT CCameraManager::ThreadAsyncSend(LPVOID pParam)
{
	AsyncSendParam* pData = static_cast<AsyncSendParam*>(pParam);
	if (pData) {
		pData->pMgr->SendImageToAI(0, pData->matImage, pData->nPlateId, pData->nShotIdx, pData->cmd);
		delete pData;
	}
	return 0;
}

bool CCameraManager::DetectObject(int nCamIndex, cv::Mat& currentFrame)
{
	if (currentFrame.empty()) return false;

	cv::Mat gray, diff;
	cv::cvtColor(currentFrame, gray, cv::COLOR_BGR2GRAY);
	cv::GaussianBlur(gray, gray, cv::Size(7, 7), 0);

	// 배경 초기화 (최초 실행 시)
	if (m_matPrevFrame[nCamIndex].empty()) {
		gray.copyTo(m_matPrevFrame[nCamIndex]);
		m_dwLightChangeTime[nCamIndex] = GetTickCount(); // 시작 직후 2초 억제
		return false;
	}

	// 프레임 차분
	cv::absdiff(m_matPrevFrame[nCamIndex], gray, diff);
	cv::threshold(diff, diff, 50, 255, cv::THRESH_BINARY);
	cv::erode (diff, diff, cv::Mat(), cv::Point(-1,-1), 1);
	cv::dilate(diff, diff, cv::Mat(), cv::Point(-1,-1), 2);

	int nChangedPixels = cv::countNonZero(diff);
	int nTotalPixels   = diff.rows * diff.cols;
	DWORD dwNow        = GetTickCount();

	// ── 1단계: 조명 꺼짐/켜짐 감지 (전체 픽셀 40% 이상 변화) ──
	if (nChangedPixels > nTotalPixels * 40 / 100)
	{
		gray.copyTo(m_matPrevFrame[nCamIndex]);   // 배경 즉시 리셋
		m_nConsecutiveDetect[nCamIndex] = 0;
		m_dwLightChangeTime[nCamIndex]  = dwNow;
		return false;
	}

	// ── 2단계: 조명 변화 직후 2초간 감지 억제 ──
	if (m_dwLightChangeTime[nCamIndex] > 0 &&
	    dwNow - m_dwLightChangeTime[nCamIndex] < 2000)
	{
		cv::addWeighted(m_matPrevFrame[nCamIndex], 0.7, gray, 0.3, 0, m_matPrevFrame[nCamIndex]);
		m_nConsecutiveDetect[nCamIndex] = 0;
		return false;
	}

	// ── 3단계: 변화 없음 → 배경 서서히 업데이트 ──
	if (nChangedPixels < 500)
	{
		cv::addWeighted(m_matPrevFrame[nCamIndex], 0.95, gray, 0.05, 0, m_matPrevFrame[nCamIndex]);
		m_nConsecutiveDetect[nCamIndex] = 0;
		return false;
	}

	// ── 4단계: 500~2000픽셀 사이는 노이즈/그림자로 간주, 배경 중간 속도 업데이트 ──
	if (nChangedPixels < 2000)
	{
		cv::addWeighted(m_matPrevFrame[nCamIndex], 0.85, gray, 0.15, 0, m_matPrevFrame[nCamIndex]);
		m_nConsecutiveDetect[nCamIndex] = 0;
		return false;
	}

	// ── 5단계: 2000픽셀 이상 → 연속 3프레임 유지 시 물체 확정 ──
	m_nConsecutiveDetect[nCamIndex]++;
	if (m_nConsecutiveDetect[nCamIndex] >= 3)
	{
		m_nConsecutiveDetect[nCamIndex] = 0;
		// 감지 확정 후 배경을 현재 프레임으로 업데이트
		// (물체가 있는 상태를 새 배경으로 인식해서 중복 감지 방지)
		gray.copyTo(m_matPrevFrame[nCamIndex]);
		m_dwLightChangeTime[nCamIndex] = dwNow;  // 8장 촬영 후 2초 억제
		return true;
	}

	return false;
}

void CCameraManager::OnImageGrabbed(CInstantCamera& camera, const CGrabResultPtr& ptrGrabResult)
{
	int nCameraIndex = (int)ptrGrabResult->GetCameraContext();
	const int nTotalShots = 8; // 서버 요청에 따라 4 -> 8장으로 변경

	try {
		if (ptrGrabResult->GrabSucceeded()) {
			// 1. 이미지 변환 및 Mat 생성
			CPylonImage pylonImage;
			pylonImage.AttachGrabResultBuffer(ptrGrabResult);
			CImageFormatConverter converter;
			converter.OutputPixelFormat = PixelType_BGR8packed;
			CPylonImage targetImage;
			converter.Convert(targetImage, pylonImage);
			cv::Mat matOriginal(ptrGrabResult->GetHeight(), ptrGrabResult->GetWidth(), CV_8UC3, (uint8_t*)targetImage.GetBuffer());

			// 2. ROI 및 시간 변수
			int centerX = matOriginal.cols / 2;
			int centerY = matOriginal.rows / 2;
			cv::Rect roiSend(centerX - 130, centerY - 130, 260, 260);
			cv::Rect roiDisplay(centerX - 300, centerY - 300, 600, 600);
			roiSend &= cv::Rect(0, 0, matOriginal.cols, matOriginal.rows);
			roiDisplay &= cv::Rect(0, 0, matOriginal.cols, matOriginal.rows);

			DWORD dwCurrentTime = GetTickCount();
			bool bIsConnected = CheckServerConnection();
			cv::Scalar rectColor = cv::Scalar(0, 255, 0);
			int thickness = 2;

			// 3. 감지 및 촬영 제어 로직
			if (bIsConnected) {
				if (!m_bObjectDetected[nCameraIndex]) {
					// [쿨다운 설정] 촬영 시간이 2초(8장 x 0.25)이므로, 
					// 물체가 완전히 빠져나갈 때까지 최소 2.5초 이상 대기하도록 설정
					if (dwCurrentTime - m_dwLastSendTime[nCameraIndex] > 2500) {
						cv::Mat matForDetect = matOriginal(roiSend).clone();
						if (DetectObject(nCameraIndex, matForDetect)) {
							m_bObjectDetected[nCameraIndex] = true;
							m_nTriggeredShotCount[nCameraIndex] = 0;
							nShotIndex[nCameraIndex] = 1;
							m_dwLastSendTime[nCameraIndex] = dwCurrentTime - 250; // 즉시 전송 시작
							WriteLog(nCameraIndex, _T("알림"), _T("새로운 물체 감지 - 8장 촬영 시작"));
						}
					}
					else {
						rectColor = cv::Scalar(255, 255, 0); // 노란색: 다음 물체 대기 중
					}
				}

				if (m_bObjectDetected[nCameraIndex]) {
					rectColor = cv::Scalar(0, 0, 255); // 빨간색: 촬영 중
					thickness = 4;
				}
			}

			// 4. 화면 출력 (테두리 및 라이브 이미지 복사)
			cv::rectangle(matOriginal, roiDisplay, rectColor, thickness);
			matOriginal.copyTo(m_matLiveImage[nCameraIndex]);

			// 5. 서버 전송 로직 (8장 전송)
			if (bIsConnected && m_bObjectDetected[nCameraIndex]) {
				// 0.25초(250ms) 간격 체크
				if (dwCurrentTime - m_dwLastSendTime[nCameraIndex] >= 250) {
					cv::Mat matCropped = matOriginal(roiSend).clone();
					std::vector<uchar> imgBuf;
					std::vector<int> params = { cv::IMWRITE_JPEG_QUALITY, 90 };
					cv::imencode(".jpg", matCropped, imgBuf, params);

					nlohmann::json j;
					j["mode"] = (m_nCurrentMode == SystemMode::RECLASSIFY) ? "reclassify" : "inspect";
					j["client_id"] = "cam_0" + std::to_string(nCameraIndex + 1);
					j["timestamp"] = (LPCSTR)CT2A(CTime::GetCurrentTime().Format(_T("%Y-%m-%d %H:%M:%S")));
					j["plate_id"] = m_nNextPlateId;
					j["shot_index"] = nShotIndex[nCameraIndex];
					j["total_shots"] = nTotalShots; // 8장으로 전송됨

					std::string jsonPayload = j.dump();
					PacketHeader header;
					header.signature = htons(0x4D47);
					header.cmdId = htons(static_cast<uint16_t>(m_nCurrentMode));
					header.bodySize = htonl((uint32_t)jsonPayload.length());

					// 패킷 전송
					send(m_hSocket, (char*)&header, sizeof(header), 0);
					send(m_hSocket, jsonPayload.c_str(), (int)jsonPayload.length(), 0);
					uint32_t netImageLen = htonl((uint32_t)imgBuf.size());
					send(m_hSocket, (char*)&netImageLen, sizeof(netImageLen), 0);
					send(m_hSocket, (const char*)imgBuf.data(), (int)imgBuf.size(), 0);

					m_dwLastSendTime[nCameraIndex] = dwCurrentTime;
					m_nTriggeredShotCount[nCameraIndex]++;
					nShotIndex[nCameraIndex]++;

					// [종료 조건] 8장을 다 찍었을 때
					if (m_nTriggeredShotCount[nCameraIndex] >= nTotalShots) {
						m_bObjectDetected[nCameraIndex] = false;
						m_nNextPlateId++;
						m_dwLastSendTime[nCameraIndex] = GetTickCount(); // 2.5초 쿨다운 시작점
						WriteLog(nCameraIndex, _T("정상"), _T("8장 촬영 및 전송 완료"));
					}
				}
			}
			m_bCaptureEnd[nCameraIndex] = true;
		}
	}
	catch (const std::exception& e) {
		if (nCameraIndex >= 0) WriteLog(nCameraIndex, _T("에러"), (CString)e.what());
	}
}

void CCameraManager::OnCameraDeviceRemoved( CInstantCamera& camera)
{
		CTime t = CTime::GetCurrentTime();
		Hour = t.GetHour();
		Min = t.GetMinute();
		Sec = t.GetSecond();

		if((CString)m_pCamera[0].GetDeviceInfo().GetSerialNumber() == (CString)camera.GetDeviceInfo().GetSerialNumber())
		{
		
			m_bRemoveCamera[0] = true;
			if(	bLogUse==true)
			{
				fprintf(log,"[%d시_%d분_%d초] [OnCameraDeviceRemoved::  Camera 0 removed ]\n",Hour,Min,Sec);
			}
			Close_Camera(0);
		}
		else if((CString)m_pCamera[1].GetDeviceInfo().GetSerialNumber() == (CString)camera.GetDeviceInfo().GetSerialNumber())
		{
		
			m_bRemoveCamera[1] = true;
			if(	bLogUse==true)
			{
				fprintf(log,"[%d시_%d분_%d초] [OnCameraDeviceRemoved::  Camera 1 removed ]\n",Hour,Min,Sec);
			}
			Close_Camera(1);
		}

}
