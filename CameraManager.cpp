#include "stdafx.h"
#include "CameraManager.h"
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

CCameraManager::CCameraManager(void)
{
	Pylon::PylonInitialize();

	// --- 통신 및 상태 관련 초기화 ---
	m_hSocket = INVALID_SOCKET;
	m_bIsConnected = false;
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
		m_dwLastSendTime[i] = 0; // 마지막 전송 시간 초기화
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
	// 1. 시간 계산
	CTime t = CTime::GetCurrentTime();
	int Hour = t.GetHour();
	int Min = t.GetMinute();
	int Sec = t.GetSecond();

	// 2. 출력창(Visual Studio Output)에 즉시 표시 (거슬리는 Lambda 메시지 사이에서 찾기 쉬움)
	CString strConsole;
	strConsole.Format(_T("%s: [%04d-%02d-%02d %02d:%02d:%02d] CAM%d: %s\n"),
		strStatus, t.GetYear(), t.GetMonth(), t.GetDay(), Hour, Min, Sec, nCamIdx, strDetail);
	OutputDebugString(strConsole);

	// 3. 파일 기록 (파일 포인터 'log'가 유효한지 체크)
	// 만약 log가 전역변수나 멤버변수로 선언되어 있고, fopen이 되어 있어야 합니다.
	if (log != NULL)
	{
		fprintf(log, "---------------------------------------------------------------------------\n");
		// [수정 전] fprintf(log, "[%02d:%02d:%02d] [ CAM : %d ] [%s] \n", Hour, Min, Sec, nCamIdx, CT2A(strStatus));
		// [수정 후] (LPCSTR)를 명시적으로 붙여줍니다.
		fprintf(log, "[%02d:%02d:%02d] [ CAM : %d ] [%s] \n", Hour, Min, Sec, nCamIdx, (LPCSTR)CT2A(strStatus));

		fprintf(log, "---------------------------------------------------------------------------\n");
		fprintf(log, "[Detail]\n");

		// [수정 후] 여기도 마찬가지로 (LPCSTR) 추가
		fprintf(log, "%s\n", (LPCSTR)CT2A(strDetail));

		fprintf(log, "---------------------------------------------------------------------------\n\n");

		fflush(log);
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

bool CCameraManager::ConnectToServer(std::string ip, int port)
{
	WSADATA wsaData;
	if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return false;

	m_hSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (m_hSocket == INVALID_SOCKET) return false;

	// TCP_NODELAY 설정 (Nagle 알고리즘 비활성화 - 150ms 목표 달성에 필수)
	BOOL bOptVal = TRUE;
	setsockopt(m_hSocket, IPPROTO_TCP, TCP_NODELAY, (char*)&bOptVal, sizeof(BOOL));

	sockaddr_in serverAddr;
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(port);

	// inet_ptr 대신 inet_pton 사용 (VS2022 표준)
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
	return true;
}

void CCameraManager::DisconnectFromServer()
{
	if (m_hSocket != INVALID_SOCKET) {
		closesocket(m_hSocket);
		WSACleanup();
	}
	m_bIsConnected = false;
}

bool CCameraManager::SendImageToServer(int nCamIndex, const cv::Mat& matEntry)
{
	// 멤버 변수 사용 시 클래스 내부 함수임을 명시
	if (!m_bIsConnected || matEntry.empty()) return false;

	try {
		// 1. 이미지 JPG 인코딩
		std::vector<uchar> buf;
		std::vector<int> params = { cv::IMWRITE_JPEG_QUALITY, 90 };
		cv::imencode(".jpg", matEntry, buf, params);

		// 2. JSON 바디 생성
		std::string jsonBody = "{ \"cam_id\": " + std::to_string(nCamIndex) + " }";

		// 3. 헤더 구성 (Big-endian)
		PacketHeader header;
		header.signature = htons(0x4D47);
		header.cmdId = htons(static_cast<uint16_t>(CmdID::IMG_SEND));
		header.bodySize = htonl((uint32_t)jsonBody.size());

		uint32_t imageSize = htonl((uint32_t)buf.size());

		// 4. 전송
		send(m_hSocket, (char*)&header, sizeof(header), 0);
		send(m_hSocket, jsonBody.c_str(), (int)jsonBody.size(), 0);
		send(m_hSocket, (char*)&imageSize, sizeof(imageSize), 0);
		send(m_hSocket, (char*)buf.data(), (int)buf.size(), 0);

		return true;
	}
	catch (...) {
		return false;
	}
}

bool CCameraManager::CheckServerConnection()
{
	if (m_hSocket == INVALID_SOCKET) {
		m_bIsServerConnected = false;
		return false;
	}

	// 소켓 상태를 비동기적으로 체크 (서버가 강제로 끊었는지 확인)
	fd_set readfds;
	FD_ZERO(&readfds);
	FD_SET(m_hSocket, &readfds);

	timeval timeout;
	timeout.tv_sec = 0;
	timeout.tv_usec = 0;

	// 읽기 준비 상태를 체크함과 동시에 연결 끊김 확인
	int result = select(0, &readfds, NULL, NULL, &timeout);

	if (result > 0) {
		char buf[1];
		// 실제로 데이터를 1바이트 읽어보려 시도 (MSG_PEEK로 데이터는 유지)
		int recvRet = recv(m_hSocket, buf, 1, MSG_PEEK);
		if (recvRet <= 0) { // 서버가 닫았거나 에러 발생
			closesocket(m_hSocket);
			m_hSocket = INVALID_SOCKET;
			m_bIsServerConnected = false;
			TRACE(_T("Server connection lost (Closed by server).\n"));
			return false;
		}
	}
	else if (result < 0) { // 소켓 에러
		closesocket(m_hSocket);
		m_hSocket = INVALID_SOCKET;
		m_bIsServerConnected = false;
		return false;
	}

	m_bIsServerConnected = true;
	return true;
}

bool CCameraManager::DetectObject(int nCamIndex, cv::Mat& currentFrame)
{
	if (currentFrame.empty()) return false;

	cv::Mat gray, diff;
	cv::cvtColor(currentFrame, gray, cv::COLOR_BGR2GRAY);
	cv::GaussianBlur(gray, gray, cv::Size(3, 3), 0);

	// 첫 실행 시 이전 프레임이 없으면 배경만 저장하고 즉시 리턴
	if (m_matPrevFrame[nCamIndex].empty()) {
		gray.copyTo(m_matPrevFrame[nCamIndex]);
		return false; // 처음엔 무조건 false를 반환하여 초록색으로 시작하게 함
	}

	// 차이 계산
	cv::absdiff(m_matPrevFrame[nCamIndex], gray, diff);
	cv::threshold(diff, diff, 45, 255, cv::THRESH_BINARY); // 임계값 살짝 상향 (노이즈 방지)

	int nChangedPixels = cv::countNonZero(diff);

	// 다음 비교를 위해 현재 프레임을 저장
	gray.copyTo(m_matPrevFrame[nCamIndex]);

	// 픽셀 변화량이 너무 작으면 인식하지 않음
	// 처음 시작 시 카메라 노이즈가 보통 500~1000픽셀 정도 발생하므로 기준을 확실히 둠
	return (nChangedPixels > 5000);
}

void CCameraManager::OnImageGrabbed(CInstantCamera& camera, const CGrabResultPtr& ptrGrabResult)
{
	int nCameraIndex = (int)ptrGrabResult->GetCameraContext();

	try
	{
		if (ptrGrabResult->GrabSucceeded())
		{
			// 1. 이미지 변환
			CPylonImage pylonImage;
			pylonImage.AttachGrabResultBuffer(ptrGrabResult);
			CImageFormatConverter converter;
			converter.OutputPixelFormat = PixelType_BGR8packed;
			CPylonImage targetImage;
			converter.Convert(targetImage, pylonImage);

			cv::Mat matOriginal(ptrGrabResult->GetHeight(), ptrGrabResult->GetWidth(), CV_8UC3, (uint8_t*)targetImage.GetBuffer());

			// 2. ROI 설정
			int centerX = matOriginal.cols / 2;
			int centerY = matOriginal.rows / 2;
			cv::Rect roiSend(centerX - 130, centerY - 130, 260, 260);
			cv::Rect roiDisplay(centerX - 300, centerY - 300, 600, 600);
			roiSend &= cv::Rect(0, 0, matOriginal.cols, matOriginal.rows);
			roiDisplay &= cv::Rect(0, 0, matOriginal.cols, matOriginal.rows);

			// 3. 서버 연결 체크
			bool bIsConnected = CheckServerConnection();

			// 4. 상태 결정 및 시각화 준비
			cv::Scalar rectColor;
			int thickness = 2;
			CString strStatusText = _T("");

			if (!bIsConnected) {
				rectColor = cv::Scalar(128, 128, 128); // 회색
				strStatusText = _T("SERVER DISCONNECTED");
				thickness = 1;
				m_bObjectDetected[nCameraIndex] = false;
			}
			else {
				// 감지 로직
				if (!m_bObjectDetected[nCameraIndex]) {
					cv::Mat matForDetect = matOriginal(roiSend).clone();
					if (DetectObject(nCameraIndex, matForDetect)) {
						m_bObjectDetected[nCameraIndex] = true;
						m_nTriggeredShotCount[nCameraIndex] = 0;
						nShotIndex[nCameraIndex] = 1;
						WriteLog(nCameraIndex, _T("정상"), _T("물체 감지 - 촬영 시작"));
					}
				}

				if (m_bObjectDetected[nCameraIndex]) {
					rectColor = cv::Scalar(0, 0, 255); // 빨간색
					strStatusText = _T("CAPTURING...");
					thickness = 5;
				}
				else {
					rectColor = cv::Scalar(0, 255, 0); // 초록색
					thickness = 2;
				}
			}

			// 5. 화면 출력 (캐스팅 수정 부분)
			cv::rectangle(matOriginal, roiDisplay, rectColor, thickness);
			if (!strStatusText.IsEmpty()) {
				// (LPCSTR) 캐스팅을 추가하여 cv::String 변환 에러 해결
				cv::putText(matOriginal, (LPCSTR)CT2A(strStatusText), cv::Point(roiDisplay.x, roiDisplay.y - 15),
					cv::FONT_HERSHEY_SIMPLEX, 1.0, rectColor, 2);
			}

			// 라이브 뷰 업데이트
			matOriginal.copyTo(m_matLiveImage[nCameraIndex]);

			// 6. 서버 전송
			if (bIsConnected && m_bObjectDetected[nCameraIndex])
			{
				DWORD dwCurrentTime = GetTickCount();
				if (dwCurrentTime - m_dwLastSendTime[nCameraIndex] >= 250)
				{
					cv::Mat matCropped = matOriginal(roiSend).clone();
					std::vector<uchar> imgBuf;
					std::vector<int> params = { cv::IMWRITE_JPEG_QUALITY, 90 };
					cv::imencode(".jpg", matCropped, imgBuf, params);

					nlohmann::json j;
					j["mode"] = "inspect";
					j["client_id"] = "cam_0" + std::to_string(nCameraIndex + 1);
					j["timestamp"] = (LPCSTR)CT2A(CTime::GetCurrentTime().Format(_T("%Y-%m-%d %H:%M:%S")));
					j["plate_id"] = 1;
					j["shot_index"] = nShotIndex[nCameraIndex];
					j["total_shots"] = nTotalShots;

					std::string jsonPayload = j.dump();
					PacketHeader header;
					header.signature = htons(0x4D47);
					header.cmdId = htons(1);
					header.bodySize = htonl((uint32_t)jsonPayload.length());

					if (send(m_hSocket, (char*)&header, sizeof(header), 0) == SOCKET_ERROR) {
						WriteLog(nCameraIndex, _T("에러"), _T("전송 중 소켓 에러 발생"));
						m_bIsServerConnected = false;
						return;
					}
					send(m_hSocket, jsonPayload.c_str(), (int)jsonPayload.length(), 0);

					uint32_t netImageLen = htonl((uint32_t)imgBuf.size());
					send(m_hSocket, (char*)&netImageLen, sizeof(netImageLen), 0);
					send(m_hSocket, (char*)imgBuf.data(), (int)imgBuf.size(), 0);

					CString strLog;
					strLog.Format(_T("%d번 이미지 전송 완료"), nShotIndex[nCameraIndex]);
					WriteLog(nCameraIndex, _T("정상"), strLog);

					m_dwLastSendTime[nCameraIndex] = dwCurrentTime;
					m_nTriggeredShotCount[nCameraIndex]++;
					nShotIndex[nCameraIndex]++;

					if (m_nTriggeredShotCount[nCameraIndex] >= nTotalShots) {
						m_bObjectDetected[nCameraIndex] = false;
						WriteLog(nCameraIndex, _T("정상"), _T("촬영 시퀀스 완료"));
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
