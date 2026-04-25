"""
mfc_handler.py - MFC GUI와의 통신 핸들러

역할:
    - 판정 결과 전송 (CmdID.RESULT_SEND: 301) → 운용서버 → MFC

패킷 전송 구조:
    [2B: signature] + [2B: cmdId] + [4B: bodySize] + [JSON 바디]
"""

import json
import socket
import logging

import config
from constants import CmdID, build_packet

logger = logging.getLogger(__name__)


# ──────────────────────────────────────────────
# CmdID.RESULT_SEND (301): 운용서버 → MFC
# ──────────────────────────────────────────────

def send_result_to_mfc(conn: socket.socket, payload: dict) -> bool:
    """
    MFC로 판정 결과 전송 (CmdID.RESULT_SEND: 301).
    config.SEND_RESULT_TO_MFC = False 이면 스킵.

    payload:
    {
        "timestamp": "2025-04-16 14:23:01",
        "verdict": "PASS" | "FAIL" | "UNCERTAIN",
        "defect_class": "normal" | "crack" | "hole" | "rust" | "scratch" | "unknown"
    }
    """
    if not config.SEND_RESULT_TO_MFC:
        logger.debug("[MFC] 전송 비활성 (SEND_RESULT_TO_MFC=False), 스킵")
        return True

    return _send_packet(conn, CmdID.RESULT_SEND, payload, log_tag="RESULT_SEND(301)")


# ──────────────────────────────────────────────
# 공통 전송 유틸
# ──────────────────────────────────────────────

def _send_packet(conn: socket.socket, cmd_id: int, data: dict, log_tag: str = "") -> bool:
    """
    PacketHeader + JSON 바디 전송.
    [2B: sig] + [2B: cmdId] + [4B: bodySize] + [JSON 바디]
    """
    try:
        body = json.dumps(data, ensure_ascii=False).encode("utf-8")
        packet = build_packet(cmd_id, body)
        conn.sendall(packet)
        logger.debug(f"[MFC] {log_tag} 전송 완료")
        return True
    except (socket.error, OSError) as e:
        logger.error(f"[MFC] {log_tag} 전송 실패: {e}")
        return False