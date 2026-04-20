# timeout_handler.py - 타임아웃 처리 모듈 (미구현)
#
# 현재 타임아웃 처리는 verdict_engine.py에서 담당:
#   - AI 응답 없음 → timeout_flag = True → 아두이노 "T\n" 전송
#
# 추후 구현 예정:
#   - plate_id 기반 타임아웃 처리
#     · 철판 1개(N=4장) 중 일부 장이 수신되지 않을 경우 처리
#     · 첫 장 수신 시점 T0 기준으로 일정 시간 초과 시
#       지금까지 받은 장수로 종합 판정 후 아두이노/MFC 전송
#     · 예: T0 + (N × SHOT_INTERVAL_SEC) + 여유시간(500ms) 초과 시 강제 종합 판정