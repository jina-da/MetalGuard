#pragma once

// 1. 기존 Pylon 및 기본 헤더 유지
#include <pylon/PylonIncludes.h>
#include <pylon/PylonGUI.h>
#include <pylon/gige/BaslerGigECamera.h>
#include <pylon/gige/GigETransportLayer.h>
#include <pylon/gige/BaslerGigEDeviceInfo.h>

// 2. [추가] OpenCV 헤더 (cv::Mat 인식용)
#include <opencv2/opencv.hpp>

// 3. [추가] 통신 관련 헤더 및 라이브러리
#include <winsock2.h>
#include <ws2tcpip.h>
#include <vector>
#include <string>
#include <stdint.h>

#pragma comment(lib, "ws2_32.lib")

using namespace Pylon;
using namespace GenApi;

#define CAM_NUM   4
#define BUF_NUM   3

// 4. [추가] TCP/IP 프로토콜 정의 (protocol_v2.md 기준)
#pragma pack(push, 1)
struct PacketHeader {
    uint16_t signature = 0x4D47;  // 'M', 'G'
    uint16_t cmdId;
    uint32_t bodySize;
};
#pragma pack(pop)

enum class CmdID : uint16_t {
    IMG_SEND = 201,             // 이미지 전송
    RESULT_SEND = 301,          // 판정 결과 수신
    RECLASSIFY_CONFIRM = 401    // 재분류 확정
};

class CCameraManager : public CImageEventHandler, public CConfigurationEventHandler
{
public:
    CCameraManager(void);
    ~CCameraManager(void);

    // --- [추가] 통신 관련 멤버 변수 ---
    SOCKET m_hSocket;
    bool m_bIsConnected;
    std::string m_serverIP;
    int m_serverPort;

    // --- [추가] 통신 관련 함수 ---
    bool ConnectToServer(std::string ip, int port);
    void DisconnectFromServer();
    bool SendImageToServer(int nCamIndex, const cv::Mat& matEntry);

    // --- [유지] 기존 Camera 관련 멤버 변수 ---
    CTlFactory* m_tlFactory;
    CInstantCameraArray m_pCamera;
    CInstantCameraArray m_pCopyCamera;
    CPylonImage Image[CAM_NUM];
    CImageFormatConverter converter[CAM_NUM];
    CIntegerPtr m_pHeartbeatTimeout[CAM_NUM];
    IGigETransportLayer* m_pIP;

    INodeMap* m_pCameraNodeMap[CAM_NUM];
    DeviceInfoList_t devices;

    CEnumerationPtr ptrEnumeration[CAM_NUM];
    CIntegerPtr ptrInteger[CAM_NUM];
    CBooleanPtr ptrBoolean[CAM_NUM];
    CFloatPtr ptrFloat[CAM_NUM];
    CCommandPtr ptrCommand[CAM_NUM];

    // --- [유지] log 관련 변수 ---
    bool bLogUse;
    bool bMessageBoxUse;
    bool bTraceUse;
    bool bStopLiveThread;
    bool bStopFlag;
    FILE* log;
    char filename[256];
    time_t t;
    // int Hour, Min, Sec; // 아래 중복 선언되어 있어 하나는 주석 처리하거나 유지

    // --- [유지] Image buffer 관련 변수 ---
    unsigned char* pImage8Buffer[CAM_NUM];
    unsigned short* pImage12Buffer[CAM_NUM];
    unsigned char* pImage24Buffer[CAM_NUM];
    unsigned char* pImageBigBuffer[CAM_NUM];

    CString m_strCM_ImageForamt[CAM_NUM];
    int m_iCM_OffsetX[CAM_NUM];
    int m_iCM_OffsetY[CAM_NUM];
    int m_iCM_Width[CAM_NUM];
    int m_iCM_Height[CAM_NUM];
    int m_iCM_reSizeWidth[CAM_NUM];

    bool m_bCaptureEnd[CAM_NUM];
    bool m_bRemoveCamera[CAM_NUM];

    bool m_bCamOpenFlag[CAM_NUM];
    bool m_bCamConnectFlag[CAM_NUM];

    long m_iSkippiedFrame[CAM_NUM];
    long m_iGrabbedFrame[CAM_NUM];
    int m_imgNjm;

    // --- [유지] 기존 멤버 함수 선언 ---
    int FindCamera(char szCamName[CAM_NUM][100], char szCamSerialNumber[CAM_NUM][100], char szInterfacName[CAM_NUM][100], int* nCamNumber);
    int Open_Camera(int nCamIndex, int nPosition);
    int Close_Camera(int nCamIndex);
    int Connect_Camera(int nCamIndex, int nOffsetX, int nOffsetY, int nWidth, int nHeight, CString strImgFormat);
    int SingleGrab(int nCamIndex);
    int GrabLive(int nCamIndex, int nMode);
    int LiveStop(int nCamIndex, int nMode);

    int GetEnumeration(int nCamIndex, char* szValue, char* szNodeName);
    int SetEnumeration(int nCamIndex, char* szValue, char* szNodeName);
    int GetInteger(int nCamIndex, int* nValue, char* szNodeName);
    int SetInteger(int nCamIndex, int nValue, char* szNodeName);
    int GetIntegerMax(int nCamIndex, int* nValue, char* szNodeName);
    int GetIntegerMin(int nCamIndex, int* nValue, char* szNodeName);
    int GetBoolean(int nCamIndex, bool* bValue, char* szNodeName);
    int SetBoolean(int nCamIndex, bool bValue, char* szNodeName);
    int GetFloat(int nCamIndex, float* fValue, char* szNodeName);
    int SetFloat(int nCamIndex, float fValue, char* szNodeName);
    int SetCommand(int nCamIndex, char* szNodeName);

    void ReadEnd(int nCamIndex);
    void WriteLog(int nCamIdex, CString strTemp1, CString strTemp2);
    bool CheckCaptureEnd(int nCamIndex);
    int SaveImage(int nFileFormat, unsigned char* pImage, char* filename, int nPixelType, int width, int height, int nColorband);

    static UINT LiveThread(void* lParam);

    int Hour, Min, Sec; // 시간 정보 변수 유지

    // --- [유지] Pylon 이벤트 핸들러 ---
    virtual void OnImageGrabbed(CInstantCamera& camera, const CGrabResultPtr& ptrGrabResult);
    virtual void OnCameraDeviceRemoved(CInstantCamera& camera);
    virtual void OnImagesSkipped(CInstantCamera& camera, size_t countOfSkippedImages);
};