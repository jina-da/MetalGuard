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
		m_bCaptureEnd[i] = false;
		m_bRemoveCamera[i] = false;
		m_bCamConnectFlag[i] = false;
		m_bCamOpenFlag[i] = false;
		m_iGrabbedFrame[i] = 0;
		pImage24Buffer[i] = NULL;

		// --- 카메라별 순번 및 JSON 초기화 ---
		nShotIndex[i] = 1;      // 1번 사진부터 시작
		m_strJsonBody[i] = "";  // 빈 문자열로 초기화
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

void CCameraManager::WriteLog(int nCamIdex, CString strTemp1, CString strTemp2)
{
		CTime t = CTime::GetCurrentTime();
		Hour = t.GetHour();
		Min = t.GetMinute();
		Sec = t.GetSecond(); 
		fprintf(log,"---------------------------------------------------------------------------\n");
		fprintf(log,"[%d시_%d분_%d초] [ CAM : %d ] Error \n",Hour,Min,Sec,nCamIdex);
		fprintf(log,"---------------------------------------------------------------------------\n");
		fprintf(log,"[Error position]\n");
		fprintf(log,CT2A(strTemp1));
		fprintf(log,"---------------------------------------------------------------------------\n");
		fprintf(log,"[Error Detail]\n");
		fprintf(log,CT2A(strTemp2));
		fprintf(log,"\n---------------------------------------------------------------------------\n");
}

bool CCameraManager::CheckCaptureEnd(int nCamIndex)
{
		return m_bCaptureEnd[nCamIndex];
}

void CCameraManager::ReadEnd(int nCamIndex)
{
	    m_bCaptureEnd[nCamIndex] = false;
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

	sockaddr_in serverAddr;
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons(port);
	inet_pton(AF_INET, ip.c_str(), &serverAddr.sin_addr);

	if (connect(m_hSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
		closesocket(m_hSocket);
		m_bIsConnected = false;
		return false;
	}

	m_bIsConnected = true;
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

void CCameraManager::OnImageGrabbed(CInstantCamera& camera, const CGrabResultPtr& ptrGrabResult)
{
	// 1. 유효성 검사
	if (!ptrGrabResult.IsValid()) return;

	int nCameraIndex = (int)ptrGrabResult->GetCameraContext();
	if (nCameraIndex < 0 || nCameraIndex >= CAM_NUM) return;

	try
	{
		if (ptrGrabResult->GrabSucceeded())
		{
			// 2. 원본 데이터 획득
			uint8_t* pBuffer = (uint8_t*)ptrGrabResult->GetBuffer();
			int nWidth = (int)ptrGrabResult->GetWidth();
			int nHeight = (int)ptrGrabResult->GetHeight();

			if (pBuffer == NULL || nWidth <= 0 || nHeight <= 0) return;

			// 3. OpenCV 전처리 (Crop & Resize)
			cv::Mat matRaw = cv::Mat(nHeight, nWidth, CV_8UC1, pBuffer);

			int startX = std::min(470, nWidth - 1);
			int startY = std::min(270, nHeight - 1);
			int targetW = std::min(1000, nWidth - startX);
			int targetH = std::min(1000, nHeight - startY);

			if (targetW > 0 && targetH > 0)
			{
				cv::Mat croppedImg = matRaw(cv::Rect(startX, startY, targetW, targetH)).clone();
				cv::Mat finalImg;

				// 흑백 -> 컬러 변환 및 260x260 리사이즈
				cv::cvtColor(croppedImg, finalImg, cv::COLOR_GRAY2BGR);
				cv::resize(finalImg, finalImg, cv::Size(260, 260));

				// 전처리 이미지 버퍼 복사
				if (pImage24Buffer[nCameraIndex] != NULL && !finalImg.empty())
				{
					memcpy(pImage24Buffer[nCameraIndex], finalImg.data, 260 * 260 * 3);
				}
			}

			// 4. 서버 요청 사항 반영된 JSON 바디 구성
			// shot_index는 각 카메라 별로 1~4까지 순환하도록 관리
			static int nShotIndex[CAM_NUM] = { 1, 1, 1, 1 };
			int nTotalShots = 4;

			// 현재 시간 획득 (YYYY-MM-DD HH:MM:SS)
			CTime t = CTime::GetCurrentTime();
			CString strTime;
			strTime.Format(_T("%04d-%02d-%02d %02d:%02d:%02d"),
				t.GetYear(), t.GetMonth(), t.GetDay(), t.GetHour(), t.GetMinute(), t.GetSecond());

			// nlohmann/json 객체 생성
			nlohmann::json j;
			j["mode"] = "inspect";
			j["plate_id"] = 1; // 필요 시 전역 변수나 UI 입력값으로 변경 가능
			j["shot_index"] = nShotIndex[nCameraIndex];
			j["total_shots"] = nTotalShots;
			j["client_id"] = "cam_0" + std::to_string(nCameraIndex + 1); // cam_01, cam_02...
			j["timestamp"] = (CT2A)strTime;

			// 생성된 JSON 문자열을 멤버 변수나 별도 버퍼에 저장 (서버 전송용)
			// 예: m_strJsonBody[nCameraIndex] = j.dump();

			// 다음 촬영을 위해 shot_index 증가 (1~4 순환)
			nShotIndex[nCameraIndex]++;
			if (nShotIndex[nCameraIndex] > nTotalShots) nShotIndex[nCameraIndex] = 1;

			// 5. 완료 상태 갱신
			m_bCaptureEnd[nCameraIndex] = true;
			m_iGrabbedFrame[nCameraIndex] = (int)ptrGrabResult->GetImageNumber();
		}
	}
	catch (const cv::Exception& e)
	{
		TRACE(_T("OpenCV Error in OnImageGrabbed: %S\n"), e.what());
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
