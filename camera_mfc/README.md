# MetalGuard MFC 클라이언트

**담당:** 김민기  
**역할:** Basler GigE 카메라 제어 + MFC GUI + 운용 서버 TCP 통신  
**언어/플랫폼:** C++ / MFC / Visual Studio 2022 (x64, Debug)

---

## 목차

1. [프로젝트 개요](#1-프로젝트-개요)
2. [시스템 구성](#2-시스템-구성)
3. [개발 환경 및 의존성](#3-개발-환경-및-의존성)
4. [빌드 방법](#4-빌드-방법)
5. [소스 파일 구조](#5-소스-파일-구조)
6. [핵심 모듈 설명](#6-핵심-모듈-설명)
   - [CCameraManager](#ccameramanager)
   - [DetectObject (움직임 감지)](#detectobject-움직임-감지)
   - [OnImageGrabbed (촬영 제어)](#onimagegrabbed-촬영-제어)
   - [ThreadReceiveFromServer (수신 스레드)](#threadreceivefromserver-수신-스레드)
   - [CPylonSampleProgramDlg (MFC UI)](#cpylonsampleprogramdlg-mfc-ui)
7. [통신 프로토콜](#7-통신-프로토콜)
8. [데이터 흐름](#8-데이터-흐름)
9. [주요 파라미터](#9-주요-파라미터)
10. [알려진 이슈 및 해결 이력](#10-알려진-이슈-및-해결-이력)

---

## 1. 프로젝트 개요

MetalGuard 클라이언트는 컨베이어 벨트 위를 지나는 금속 표면을 실시간으로 촬영하고, 운용 서버(10.10.10.109:8000)로 이미지를 전송하여 AI 판정 결과를 수신·표시하는 MFC 데스크톱 애플리케이션이다.

**주요 기능:**
- Basler GigE 카메라 실시간 라이브 스트리밍
- 프레임 차분(Frame Difference) 기반 물체 감지 및 자동 촬영 트리거
- 철판 1개당 8장을 0.25초 간격으로 촬영 후 운용 서버로 전송
- 운용 서버로부터 수신한 판정 결과(PASS/FAIL/UNCERTAIN) UI 표시
- 판정 결과에 따른 색상 강조 (초록/빨강/노랑) 및 확률값 표시
- 일반 분류 / 재분류 모드 전환

---

## 2. 시스템 구성

```
[Basler GigE 카메라]
        │  Pylon SDK (GigE)
        ▼
[MFC 클라이언트 - 10.10.10.XXX]
  ┌─────────────────────────────────┐
  │  CCameraManager                 │
  │  - OnImageGrabbed()             │  ──→ 이미지 260×260 JPEG
  │  - DetectObject()               │          │ TCP:8000
  │  - ThreadReceiveFromServer()    │  ◀── 판정 결과 JSON
  └─────────────────────────────────┘
        │  WM_UPDATE_VERDICT
        ▼
  [CPylonSampleProgramDlg]
  - 라이브 뷰, 판정 결과, 확률 패널
  - DB 테이블, 로그 리스트박스

        │ TCP:8000
        ▼
[운용 서버 - 10.10.10.109:8000] (이지나)
        │
        ▼
[AI 서버 - 10.10.10.128:9000] (김범준)
```

---

## 3. 개발 환경 및 의존성

| 항목 | 버전/경로 |
|---|---|
| Visual Studio | 2022 (v143 도구 세트) |
| 플랫폼 | x64 / Debug |
| MFC | Visual Studio 포함 |
| Pylon SDK | 5.x (`pylon/` 폴더) |
| OpenCV | 4.13.0 (`opencv_world4130d.lib`) |
| nlohmann/json | `include/nlohmann/json.hpp` (단일 헤더) |
| Winsock2 | Windows SDK 포함 |

### include 순서 (stdafx.h — 반드시 준수)

```cpp
#include <winsock2.h>        // 1. Winsock 먼저 (winsock.h 충돌 방지)
#include <ws2tcpip.h>
#include <pylon/PylonIncludes.h>  // 2. Pylon (MFC보다 먼저)
#include <pylon/PylonGUI.h>
#include <opencv2/core.hpp>       // 3. OpenCV 개별 헤더
#include <opencv2/imgproc.hpp>    //    (opencv.hpp 전체 include 금지
#include <opencv2/imgcodecs.hpp>  //     → Pylon::Stream 심볼 충돌)
#include <afxwin.h>               // 4. MFC
#include <afxext.h>
```

> **주의:** `opencv2/opencv.hpp` 전체 include 시 `Pylon::Stream`과 충돌하여 빌드 실패. 반드시 개별 헤더만 포함할 것.

---

## 4. 빌드 방법

```
1. Visual Studio 2022에서 MetalGuard.sln 열기
2. 솔루션 정리 (빌드 → 솔루션 정리)
3. x64\Debug 폴더 내용 전체 삭제 (PCH 캐시 오염 방지)
4. 빌드 (Ctrl+Shift+B)
5. 실행 파일: x64\Debug\MetalGuard.exe
```

> **빌드 오류 발생 시 체크리스트:**
> - `CameraManager.cpp` 첫 줄이 정확히 `#include "stdafx.h"` 인지 확인 (BOM 문자 유무)
> - `opencv.hpp` 전체 include가 없는지 확인
> - `RegistrationMode_Append`, `Cleanup_None` 앞에 `Pylon::` 접두사가 없는지 확인

---

## 5. 소스 파일 구조

```
MetalGuard/
├── stdafx.h                  # 미리 컴파일된 헤더 (include 순서 핵심)
├── stdafx.cpp
├── targetver.h
│
├── CameraManager.h           # CCameraManager 클래스 선언
├── CameraManager.cpp         # 카메라 제어, 감지, 전송 로직 전체
│
├── MetalGuardTypes.h         # 공유 구조체 (VerdictData, WM 메시지 상수)
│
├── PylonSampleProgram.h      # MFC 앱 클래스
├── PylonSampleProgram.cpp
│
├── PylonSampleProgramDlg.h   # 메인 다이얼로그 클래스 선언
├── PylonSampleProgramDlg.cpp # UI 이벤트 핸들러, 판정 결과 렌더링
│
├── PylonSampleProgram.rc     # 리소스 (버튼, 컨트롤 배치)
└── resource.h                # 컨트롤 ID 정의
```

---

## 6. 핵심 모듈 설명

### CCameraManager

`CImageEventHandler`, `CConfigurationEventHandler`를 상속하여 Pylon SDK의 이벤트 기반 프레임 수신을 처리한다.

**주요 멤버 변수:**

| 변수 | 설명 |
|---|---|
| `m_bObjectDetected[CAM_NUM]` | 물체 감지 상태 플래그 (카메라별) |
| `m_matPrevFrame[CAM_NUM]` | 이전 프레임 (차분 비교용 배경) |
| `m_nConsecutiveDetect[CAM_NUM]` | 연속 감지 프레임 카운터 (오감지 방지) |
| `m_dwLightChangeTime[CAM_NUM]` | 조명 변화 감지 시각 (억제 타이머) |
| `m_nTriggeredShotCount[CAM_NUM]` | 감지 후 현재까지 전송한 장 수 |
| `m_dwLastSendTime[CAM_NUM]` | 마지막 전송 시각 (쿨다운 계산) |
| `m_nNextPlateId` | 다음 철판 ID (전송마다 +1, 8장 완료 시 증가) |
| `nTotalShots` | 철판 1개당 촬영 장 수 (= 8) |
| `m_bIsServerConnected` | 운용 서버 연결 상태 |

---

### DetectObject (움직임 감지)

5단계 구조로 오감지를 방지한다.

```
입력 프레임 (260×260 BGR)
        │
        ▼ 그레이스케일 변환 + GaussianBlur(7×7)
        │
        ├─ 배경 미초기화? → 현재 프레임을 배경으로 저장, 2초 억제 시작 후 false
        │
        ▼ absdiff(배경, 현재) → threshold(50) → erode(1) → dilate(2)
        │
변화 픽셀 수 (nChangedPixels)
        │
        ├─ > 전체 픽셀의 40% → [1단계] 조명 변화: 배경 즉시 리셋, 2초 억제
        │
        ├─ 2초 억제 중?   → [2단계] 억제 기간: 배경 빠른 적응(0.3), false
        │
        ├─ < 500픽셀     → [3단계] 정적: 배경 서서히 업데이트(0.05), false
        │
        ├─ < 2000픽셀    → [4단계] 노이즈/그림자: 배경 중간 업데이트(0.15), false
        │
        └─ ≥ 2000픽셀    → [5단계] 물체 후보: 연속 3프레임 확인
                │
                ├─ < 3프레임 연속 → 카운터 누적, false
                └─ ≥ 3프레임 연속 → 물체 확정! 배경 리셋 + 2초 억제, true
```

---

### OnImageGrabbed (촬영 제어)

Pylon SDK가 프레임 수신 시 자동 호출하는 콜백. 카메라 스레드에서 실행된다.

**처리 흐름:**

```
[1] 프레임 수신 → BGR 변환 (CPylonImage + CImageFormatConverter)
[2] ROI 계산: 중심 기준 260×260 (전송), 600×600 (화면 표시)
[3] 감지 제어:
    - 서버 미연결 → 스킵
    - m_bObjectDetected == false:
        - 쿨다운 2.5초 경과?
            - DetectObject() → true: 감지 확정, 8장 촬영 시작
[4] 라이브 이미지 갱신 (m_matLiveImage) + 박스 그리기
    - 대기 중:  노란색 박스 (255, 255, 0)
    - 촬영 중:  빨간색 박스 (0, 0, 255), 두께 4
    - 연결 없음: 초록색 박스 (0, 255, 0)
[5] 0.25초 간격 체크 후 이미지 전송:
    - 260×260 ROI JPEG 인코딩 (품질 90)
    - PacketHeader + JSON + 이미지 크기(4B) + 이미지 바이트 전송
[6] 8장 완료 시 종료 처리:
    - m_bObjectDetected = false
    - m_nTriggeredShotCount = 0
    - nShotIndex = 1
    - m_nConsecutiveDetect = 0
    - m_nNextPlateId++
    - m_dwLastSendTime = 현재시각      (2.5초 쿨다운 시작)
    - m_dwLightChangeTime = 현재시각   (DetectObject 2초 억제 시작)
    - m_matPrevFrame.release()         (배경 초기화 → 재감지 방지)
```

---

### ThreadReceiveFromServer (수신 스레드)

`AfxBeginThread`로 기동되며 운용 서버로부터 판정 결과를 수신한다.

```
소켓에서 PacketHeader(8B) 수신
        │
        ├─ 시그니처 != 0x4D47 → 스킵
        ├─ bodySize == 0 → PONG 등 바디 없는 패킷 스킵
        ├─ bodySize > 1MB → 비정상 패킷 방지, 스킵
        │
        └─ cmdId == 301 (RESULT_SEND):
            JSON 파싱 → VerdictData* 생성
            → PostMessage(WM_UPDATE_VERDICT) → UI 스레드 전달
```

수신되는 JSON 필드:

```json
{
  "verdict": "FAIL",
  "defect_class": "scratch",
  "prob_normal": 0.6227,
  "prob_crack": 0.1068,
  "prob_hole": 0.0740,
  "prob_rust": 0.0789,
  "prob_scratch": 0.1176,
  "inference_ms": 3.95,
  "pipeline_ms": 6.2,
  "plate_id": 5
}
```

---

### CPylonSampleProgramDlg (MFC UI)

**주요 버튼/기능:**

| 컨트롤 | 기능 |
|---|---|
| `▶ 원클릭 시작` | 카메라 찾기 → 열기 → 연결 → 서버 연결 → 라이브 자동화 |
| `카메라 라이브` | 카메라 라이브 스트리밍 시작/정지 |
| `사진 수동 저장` | 현재 프레임 저장 |
| `운용 모드` 콤보박스 | 일반 분류(1) / 재분류(2) 전환 |

**판정 결과 표시 (`OnUpdateVerdict`):**

| 판정 | 배경색 | 텍스트색 |
|---|---|---|
| PASS | 연초록 (#CCFFCC) | 진초록 |
| FAIL | 연빨강 (#FFCCCC) | 진빨강 |
| UNCERTAIN | 연노랑 (#FFFACC) | 진황색 |

**DB 테이블 컬럼:** ID / 시각 / plate_id / 모드 / 판정 / Normal~Scratch% / 추론ms / 파이프라인ms  
**로그 리스트박스:** 최대 500줄 유지, 이후 선입선출

---

## 7. 통신 프로토콜

### PacketHeader (8 bytes, packed)

```cpp
#pragma pack(push, 1)
struct PacketHeader {
    uint16_t signature = 0x4D47;  // 'M', 'G'  (Big-endian)
    uint16_t cmdId;
    uint32_t bodySize;
};
#pragma pack(pop)
```

### 이미지 전송 패킷 구조

```
[8B: PacketHeader]
  signature = 0x4D47
  cmdId     = 1 (IMG_SEND) | 2 (IMG_RECLASSIFY)
  bodySize  = JSON 길이
[JSON Body]
  {
    "mode": "inspect",       // "inspect" | "reclassify"
    "client_id": "cam_01",
    "timestamp": "2026-04-24 16:54:03",
    "plate_id": 4,
    "shot_index": 1,         // 1 ~ 8
    "total_shots": 8
  }
[4B: 이미지 크기 (Big-endian uint32)]
[이미지 바이트: JPEG 260×260, 품질 90]
```

### CmdID 정의

| cmdId | 이름 | 방향 | 설명 |
|---|---|---|---|
| 1 | IMG_SEND | 클라이언트 → 서버 | 일반 분류 이미지 전송 |
| 2 | IMG_RECLASSIFY | 클라이언트 → 서버 | 재분류 이미지 전송 |
| 301 | RESULT_SEND | 서버 → 클라이언트 | 판정 결과 수신 |

---

## 8. 데이터 흐름

```
카메라 프레임 수신 (OnImageGrabbed, Pylon 스레드)
        │
        ├→ [표시] m_matLiveImage 갱신 → UI 라이브 뷰
        │
        ├→ [감지] DetectObject() → true 시 촬영 시작
        │
        └→ [전송] 0.25초 간격, 8장
                  260×260 JPEG → PacketHeader + JSON + 이미지
                  → 운용 서버 TCP:8000
                          │
                          ▼
                    [ThreadReceiveFromServer]
                    cmdId=301 수신 → VerdictData 파싱
                    → PostMessage(WM_UPDATE_VERDICT)
                          │
                          ▼
                    [OnUpdateVerdict - UI 스레드]
                    판정 색상 갱신, 확률값 표시, DB 테이블 추가
```

---

## 9. 주요 파라미터

| 파라미터 | 값 | 위치 | 설명 |
|---|---|---|---|
| `nTotalShots` | 8 | 생성자 | 철판 1개당 촬영 장 수 |
| 전송 간격 | 250ms | `OnImageGrabbed` | 장별 전송 간격 |
| 쿨다운 | 2,500ms | `OnImageGrabbed` | 8장 완료 후 재감지 대기 |
| 물체 감지 임계 | 2,000 픽셀 | `DetectObject` | 이 이상 변화 시 물체 후보 |
| 연속 감지 프레임 | 3 프레임 | `DetectObject` | 오감지 방지용 연속 확인 수 |
| 조명 변화 임계 | 40% | `DetectObject` | 전체 픽셀 40% 이상 변화 시 조명 변화 판정 |
| 억제 시간 | 2,000ms | `DetectObject` | 조명 변화 / 8장 완료 후 감지 억제 |
| GaussianBlur | 7×7 | `DetectObject` | 노이즈 제거 커널 |
| diff 임계값 | 50 | `DetectObject` | threshold 이진화 기준 |
| ROI 전송 | 260×260 | `OnImageGrabbed` | 이미지 중심 기준 크롭 |
| ROI 표시 | 600×600 | `OnImageGrabbed` | 화면 박스 크기 |
| JPEG 품질 | 90 | `OnImageGrabbed` | 전송 이미지 품질 |
| 서버 IP | 10.10.10.109 | 생성자 | 운용 서버 주소 |
| 서버 Port | 8000 | 생성자 | 운용 서버 포트 |

---

## 10. 알려진 이슈 및 해결 이력

### 빌드 오류

| 증상 | 원인 | 해결 |
|---|---|---|
| `전처리기 명령은 공백 아닌 문자로 시작해야 합니다` | CameraManager.cpp 1번 줄 BOM 문자 | 파일 첫 바이트 제거 |
| `Pylon::Stream` 충돌, `cv` 심볼 오류 | `opencv.hpp` 전체 include | `core/imgproc/imgcodecs.hpp` 개별 사용 |
| `RegistrationMode_Append` 오류 | `Pylon::` 접두사 + `using namespace Pylon` 중복 | 접두사 제거 |
| `fprintf` 한글 오류 | CP949에서 `시/분/초` 바이트 충돌 | `h/m/s` 영문으로 교체 |
| `WM_UPDATE_GRADCAM` 미정의 | `MetalGuardTypes.h`에 정의 누락 | `#define WM_UPDATE_GRADCAM (WM_USER + 103)` 추가 |

### 런타임 오류

| 증상 | 원인 | 해결 |
|---|---|---|
| 8장 이후 계속 촬영 | 8장 완료 후 배경·카운터·쿨다운 미초기화 | 종료 시 5가지 상태 동시 초기화 |
| plate_id 증가 안 됨 | 멤버변수 `nTotalShots=4` vs 지역변수 `nTotalShots=8` 혼용 | 생성자에서 멤버변수 8로 통일 |
| 워밍업 중 이미지 적체 | AI 서버 워밍업 10초 동안 MFC가 계속 전송 | READY(cmdId=500) 신호 수신 후 촬영 시작 |
| 움직임 없는데 빨간 박스 | 8장 완료 후 배경이 철판 상태로 굳어 재감지 | `m_matPrevFrame.release()` 배경 초기화 |
| 0x2022/0x7422 시그니처 오류 | 이전 패킷 바디가 다 안 읽혀 다음 헤더 오독 | 운용 서버 bodySize 계산 방식 통일 요청 |

---

## 참고

- **운용 서버 수정 요청 사항:** `jina_server_spec.txt` 참조
- **AI 서버 담당:** 김범준 (10.10.10.128:9000)
- **운용 서버 담당:** 이지나 (10.10.10.109:8000)
- **아두이노 담당:** 김희창
