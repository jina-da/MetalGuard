"""
mfc_handler.py - MFC GUI와의 통신 핸들러

역할:
    - 판정 결과 전송 (CmdID.RESULT_SEND: 301) → 운용서버 → MFC
    - 재분류 확정 처리 (CmdID.RECLASSIFY_CONFIRM: 401) → MFC → 운용서버

패킷 전송 구조:
    [2B: signature] + [2B: cmdId] + [4B: bodySize] + [JSON 바디]
"""

import json
import socket
import logging

import config
from constants import CmdID, build_packet
from server.db.db_manager import DBManager

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
# CmdID.RECLASSIFY_CONFIRM (401): MFC → 운용서버
# ──────────────────────────────────────────────

def handle_reclassify_confirm(conn: socket.socket, body: dict, db_manager: DBManager) -> None:
    """
    재분류 확정 수신 후 DB 저장, RECLASSIFY_CONFIRM_RES(402) 응답.
    tcp_server._dispatch()에서 호출됨.

    수신 body:
    {
        "inspection_id": 1042,
        "final_verdict": "PASS" | "FAIL",
        "final_defect_class": "normal" | "crack" | ...,
        "confidence": 0.95,
        "reason": "조명 반사로 인한 오탐"
    }
    """
    required_keys = {"inspection_id", "final_verdict", "final_defect_class", "confidence", "reason"}
    missing = required_keys - body.keys()
    if missing:
        logger.error(f"[MFC] 재분류 누락 필드: {missing}")
        _send_packet(conn, CmdID.ERROR_RES,
                     {"error_code": "MISSING_FIELD", "message": f"누락 필드: {missing}"},
                     log_tag="ERROR_RES(503)")
        return

    final_verdict = body["final_verdict"]
    if final_verdict not in ("PASS", "FAIL"):
        _send_packet(conn, CmdID.ERROR_RES,
                     {"error_code": "INVALID_VERDICT", "message": f"유효하지 않은 verdict: {final_verdict}"},
                     log_tag="ERROR_RES(503)")
        return

    try:
        db_manager.insert_reclassify_result(
            inspection_id=body["inspection_id"],
            final_verdict=final_verdict,
            final_defect_class=body["final_defect_class"],
            confidence=body["confidence"],
            reason=body["reason"],
        )
        logger.info(f"[MFC] 재분류 DB 저장 완료: inspection_id={body['inspection_id']}")
        _send_packet(conn, CmdID.RECLASSIFY_CONFIRM_RES,
                     {"status": "SUCCESS"},
                     log_tag="RECLASSIFY_CONFIRM_RES(402)")

    except Exception as e:
        logger.error(f"[MFC] 재분류 DB 저장 실패: {e}")
        _send_packet(conn, CmdID.RECLASSIFY_CONFIRM_RES,
                     {"status": "FAIL"},
                     log_tag="RECLASSIFY_CONFIRM_RES(402)")


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