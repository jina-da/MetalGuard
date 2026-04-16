# MetalGuard 운용 서버

## 실행
cd ~/metal_guard_server && source .venv/bin/activate && python3 main.py

## 팀 구성
- 카메라 PC (C++): 10.10.10.xxx → Port 8000
- MFC GUI (C++): 10.10.10.xxx → Port 8001
- AI 서버: 10.10.10.128:9000
- DB: 10.10.10.101:3306 (MariaDB / metalguard_db)

## 통신 프로토콜
- 카메라→서버: [4B 헤더] + [JSON] + [이미지]
- 서버→AI: [4B 크기] + [이미지]
- AI→서버: [4B 크기] + [JSON]
- 서버→아두이노: "P\n" / "F\n" / "U\n"

## 판정 임계값
- PASS: normal >= 0.80
- UNCERTAIN: max_prob < 0.60
- FAIL: 나머지

## 코드 스타일
- Python, ESLint 없음 (Black + 명시적 타입)
- 에러 처리 항상 포함
- 함수 단일 책임 원칙