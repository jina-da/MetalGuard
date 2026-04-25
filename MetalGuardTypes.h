#pragma once
// MetalGuardTypes.h
// CameraManager.cpp와 PylonSampleProgramDlg.cpp가 공유하는 구조체/상수

#include <string>

// WM_UPDATE_VERDICT 메시지 ID
#ifndef WM_UPDATE_VERDICT
#define WM_UPDATE_VERDICT (WM_USER + 102)
#endif

// 판정 결과 데이터 - 수신 스레드 -> 메인 다이얼로그 전달용
// PostMessage(WM_UPDATE_VERDICT, (WPARAM)pV, 0) 으로 전달
// OnUpdateVerdict()에서 delete 처리
struct VerdictData {
    std::string verdict;        // "PASS" | "FAIL" | "UNCERTAIN"
    std::string defect;         // "normal" | "crack" | "hole" | "rust" | "scratch"
    float prob_normal   = 0.f;  // 0~100 (%)
    float prob_crack    = 0.f;
    float prob_hole     = 0.f;
    float prob_rust     = 0.f;
    float prob_scratch  = 0.f;
    float inference_ms  = 0.f;
    float pipeline_ms   = 0.f;
    int   plate_id      = 0;
};
