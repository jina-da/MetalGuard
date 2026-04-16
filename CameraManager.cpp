#include "stdafx.h"
#include "CameraManager.h"
#include <time.h>
#include <opencv2/opencv.hpp>


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

	for (int i = 0; i < CAM_NUM; i++)
	{
		m_bCaptureEnd[i] = false;
		m_bRemoveCamera[i] = false;
		m_bCamConnectFlag[i] = false;
		m_bCamOpenFlag[i] = false;
		m_iGrabbedFrame[i] = 0;
		m_iSkippiedFrame[i] = 0;
		m_pCameraNodeMap[i] = NULL;

		// [필수 추가] 포인터를 NULL로 초기화해야 delete 시 에러가 안 납니다.
		pImage24Buffer[i] = NULL;
	}
	m_imgNjm = 0;
	bLogUse = false;
}

CCameraManager::~CCameraManager(void)
{
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
/*
     nCamIndex  = 프로그램에서 사용할 카메라 인덱스
	 nPosition  = 시스템에서 연결된 실제 순서
*/
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
			Pylon::Cleanup_None  // 또는 Pylon::Ownership_ExternalOwnership
		);

		// 2. Grab 완료 이벤트 핸들러 등록
		m_pCamera[nCamIndex].RegisterImageEventHandler(
			this,
			Pylon::RegistrationMode_Append,
			Pylon::Cleanup_None  // 또는 Pylon::Ownership_ExternalOwnership
		);
		// --- 오류 수정 구간 끝 ---

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
		m_iCM_reSizeWidth[nCamIndex] = (((nWidth * 8) + 31) / 32 * 4);                 // width 4byte 배수계산

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

		// Image Format 설정  
		SetEnumeration(nCamIndex, CT2A(strImgFormat), "PixelFormat");
		m_strCM_ImageForamt[nCamIndex] = strImgFormat;

		// [민기 파트 핵심 추가] 24비트 이미지 버퍼 메모리 할당
		// OnImageGrabbed에서 memset/memcpy 시 중단점이 발생하는 것을 방지합니다.
		if (pImage24Buffer[nCamIndex] != NULL)
		{
			delete[] pImage24Buffer[nCamIndex];
			pImage24Buffer[nCamIndex] = NULL;
		}

		// 3채널(RGB) 이미지이므로 가로 * 세로 * 3 크기만큼 할당
		pImage24Buffer[nCamIndex] = new unsigned char[(size_t)nWidth * nHeight * 3];

		// 할당된 메모리를 안전하게 0으로 초기화
		memset(pImage24Buffer[nCamIndex], 0, (size_t)nWidth * nHeight * 3);

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
		
			m_pCamera[ nCamIndex ].StartGrabbing(1,GrabStrategy_OneByOne, GrabLoop_ProvidedByInstantCamera); 
			/*m_pCamera[ nCamIndex ].StartGrabbing(1);
			
             CGrabResultPtr ptrGrabResult;
       
            m_pCamera[nCamIndex].RetrieveResult( 5000, ptrGrabResult, TimeoutHandling_ThrowException);
		    
			if(m_strCM_ImageForamt[ptrGrabResult->GetCameraContext()]=="Mono8")
			{
				converter[ptrGrabResult->GetCameraContext()].OutputPixelFormat = PixelType_Mono8;					
				converter[ptrGrabResult->GetCameraContext()].Convert( pCameraManager->Image[ptrGrabResult->GetCameraContext()], ptrGrabResult);  					
				pImage8Buffer[ptrGrabResult->GetCameraContext()] =(unsigned char*)Image[ptrGrabResult->GetCameraContext()].GetBuffer();
			}
			else  // Bayer  && YUV422Packed 
			{
                 
				//converter[ptrGrabResult->GetCameraContext()].OutputPixelFormat = PixelType_BGR8packed;
				//converter[ptrGrabResult->GetCameraContext()].Convert( pCameraManager->Image[ptrGrabResult->GetCameraContext()], ptrGrabResult);  					
				//pImage24Buffer[ptrGrabResult->GetCameraContext()] =(unsigned char*)pCameraManager->Image[ptrGrabResult->GetCameraContext()].GetBuffer();
				
				
				pImage8Buffer[ptrGrabResult->GetCameraContext()] =(unsigned char*)ptrGrabResult->GetBuffer();
			   
					
			}
            m_bCaptureEnd[ptrGrabResult->GetCameraContext()] = true;*/
			return 0;   
	}
    catch (GenICam::GenericException &e)
    {
			// Error handling
			CString error =  (CString)e.GetDescription();
			if(	bLogUse==true)
			{
				WriteLog(nCamIndex,_T("[ SingleGrab ]\n"),error);
			}
			return -2;
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

int CCameraManager::SaveImage(int nFileFormat, unsigned char* pImage, char *filename,int nPixelType,int width, int height,int nColorband)
{
	EImageFileFormat ImageFileFormat;
	switch(nFileFormat)
	{
	   case 0:
		   ImageFileFormat = ImageFileFormat_Bmp;
		   break;
	   case 1:
		   ImageFileFormat = ImageFileFormat_Tiff;
		   break;
	   case 2:
		   ImageFileFormat = ImageFileFormat_Jpeg;
		   break;
	   case 3:
		   ImageFileFormat = ImageFileFormat_Png;
		   break;
	}
    EPixelType  ImagePixleType;
	switch(nPixelType)
	{
	   case 0:
		   ImagePixleType = PixelType_Mono8;
		   break;
	   case 1:
		   ImagePixleType = PixelType_BGR8packed;
		   break;

	}

	CImagePersistence::Save( ImageFileFormat,filename,pImage,width*height*nColorband,ImagePixleType,width,height,0,ImageOrientation_TopDown);
	return 0;
}

void CCameraManager::OnImagesSkipped( CInstantCamera& camera, size_t countOfSkippedImages)
{   

      m_iSkippiedFrame[CInstantCamera().GetCameraContext()] = countOfSkippedImages;
	 if(bLogUse==true)
	 {
		fprintf(log,"CAM = %d , SkippedFrame = %d\n",CInstantCamera().GetCameraContext(),m_iSkippiedFrame[CInstantCamera().GetCameraContext()]);

	 }
}

void CCameraManager::OnImageGrabbed(CInstantCamera& camera, const CGrabResultPtr& ptrGrabResult)
{
	try
	{
		if (ptrGrabResult->GrabSucceeded())
		{
			int nCameraIndex = (int)ptrGrabResult->GetCameraContext();

			// 1. Pylon 버퍼 -> OpenCV Mat (원본 해상도)
			int width = (int)ptrGrabResult->GetWidth();
			int height = (int)ptrGrabResult->GetHeight();
			cv::Mat matRaw(height, width, CV_8UC1, (uint8_t*)ptrGrabResult->GetBuffer());

			if (!matRaw.empty())
			{
				// 2. ROI 설정 (중앙 260x260 크롭)
				int roiW = 260;
				int roiH = 260;
				int startX = (matRaw.cols - roiW) / 2;
				int startY = (matRaw.rows - roiH) / 2;

				if (startX >= 0 && startY >= 0 && matRaw.cols >= roiW && matRaw.rows >= roiH)
				{
					cv::Rect roiRect(startX, startY, roiW, roiH);

					// 3. 전처리: 크롭 후 3채널(BGR)로 변환
					cv::Mat croppedImg = matRaw(roiRect).clone();
					cv::Mat finalImg;
					cv::cvtColor(croppedImg, finalImg, cv::COLOR_GRAY2BGR);

					// 4. 전처리된 이미지를 관리 버퍼에 복사 (260x260x3)
					if (pImage24Buffer[nCameraIndex] != NULL)
					{
						size_t nCopySize = (size_t)finalImg.total() * finalImg.elemSize();
						memcpy(pImage24Buffer[nCameraIndex], finalImg.data, nCopySize);
					}
					// ** cv::imshow는 스레드 충돌 방지를 위해 제거됨 **
				}
			}

			m_bCaptureEnd[nCameraIndex] = true;
			m_iGrabbedFrame[nCameraIndex] = (int)ptrGrabResult->GetImageNumber();
		}
	}
	catch (const std::exception& e)
	{
		TRACE(_T("OnImageGrabbed Error: %S\n"), e.what());
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
