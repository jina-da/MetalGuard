# constants.py - MetalGuard 통신 프로토콜 상수 정의
#
# 패킷 구조:
#   [2B: signature] + [2B: cmdId] + [4B: bodySize] + [JSON 바디]
#   이미지 포함 시:
#   [2B: signature] + [2B: cmdId] + [4B: bodySize] + [JSON 바디] + [4B: 이미지크기] + [이미지]
#
# C++ 측 동일 구조:
#   struct PacketHeader {
#       uint16_t signature = 0x4D47;  // 'M', 'G'
#       uint16_t cmdId;
#       uint32_t bodySize;
#   };

import struct

# ── 패킷 시그니처 ─────────────────────────────
PACKET_SIGNATURE = 0x4D47       # 'M', 'G' (MetalGuard)
PACKET_HEADER_SIZE = 8          # 2B(sig) + 2B(cmdId) + 4B(bodySize)
PACKET_HEADER_FORMAT = ">HHI"   # big-endian: uint16 + uint16 + uint32


class CmdID:
    """프로토콜 명령어 ID 정의. C++ enum class CmdID : uint16_t 와 동일."""

    # ── 이미지 전송 (카메라+MFC → 운용서버) ──
    IMG_SEND           = 1    # 촬영 이미지 전송 (첫 분류)
    IMG_RECLASSIFY     = 2    # 재분류 이미지 전송

    # ── AI 추론 (운용서버 ↔ AI서버) ──────────
    INFER_REQ          = 101  # 추론 요청: 운용서버 → AI서버
    INFER_RES          = 102  # 추론 응답: AI서버 → 운용서버

    # ── 아두이노 판정 신호 ────────────────────
    VERDICT_PASS       = 201  # 정상: "P\n"
    VERDICT_FAIL       = 202  # 불량: "F\n"
    VERDICT_UNCERTAIN  = 203  # 미분류: "U\n"
    VERDICT_TIMEOUT    = 204  # 타임아웃(150ms 초과): "T\n"

    # ── MFC 판정 결과 전송 ────────────────────
    RESULT_SEND        = 301  # 판정 결과: 운용서버 → MFC

    # ── 재분류 ───────────────────────────────
    RECLASSIFY_CONFIRM     = 401  # 재분류 확정: MFC → 운용서버
    RECLASSIFY_CONFIRM_RES = 402  # 재분류 확정 응답: 운용서버 → MFC (미정)
    UNCERTAIN_LIST_REQ     = 403  # UNCERTAIN 목록 요청: MFC → 운용서버 (미정)
    UNCERTAIN_LIST_RES     = 404  # UNCERTAIN 목록 응답: 운용서버 → MFC (미정)

    # ── 시스템 ───────────────────────────────
    PING               = 501  # 연결 확인 (미정)
    PONG               = 502  # 연결 확인 응답 (미정)
    ERROR_RES          = 503  # 에러 응답 (미정)


def build_packet(cmd_id: int, body: bytes) -> bytes:
    """
    패킷 생성 유틸.
    [2B: sig] + [2B: cmdId] + [4B: bodySize] + [body]
    """
    header = struct.pack(PACKET_HEADER_FORMAT, PACKET_SIGNATURE, cmd_id, len(body))
    return header + body


def parse_header(raw: bytes) -> tuple[int, int]:
    """
    8바이트 헤더 파싱.
    Returns:
        (cmd_id, body_size)
    Raises:
        ValueError: 시그니처 불일치
    """
    sig, cmd_id, body_size = struct.unpack(PACKET_HEADER_FORMAT, raw)
    if sig != PACKET_SIGNATURE:
        raise ValueError(f"잘못된 시그니처: 0x{sig:04X} (expected 0x{PACKET_SIGNATURE:04X})")
    return cmd_id, body_size