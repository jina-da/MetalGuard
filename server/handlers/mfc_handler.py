"""
mfc_handler.py
MFC GUI(C++)와의 TCP 통신 핸들러
- 판정 결과 전송: RESULT_SEND (No.301)
- 재분류 결과 수신: RECLASSIFY (No.401 예상)
"""

import json
import socket
import logging
import struct
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from server.engine.verdict_engine import VerdictResult

logger = logging.getLogger(__name__)


def send_result_to_mfc(conn: socket.socket, verdict_result: dict) -> bool:
    """
    MFC로 판정 결과 전송 (RESULT_SEND No.301)
    
    프로토콜: JSON 문자열을 UTF-8 인코딩 후 전송
    MFC 측에서 수신 포트 8001로 연결되어 있음
    
    Args:
        conn: MFC 클라이언트 소켓
        verdict_result: {
            "cmd_no": 301,
            "image_id": str,
            "verdict": "PASS" | "FAIL" | "UNCERTAIN",
            "normal_prob": float,
            "max_prob": float,
            "defect_type": str | None,
            "timestamp": str
        }
    Returns:
        전송 성공 여부
    """
    try:
        payload = json.dumps(verdict_result, ensure_ascii=False).encode("utf-8")
        # 4바이트 길이 헤더 + JSON 페이로드
        header = struct.pack(">I", len(payload))
        conn.sendall(header + payload)
        logger.info(f"[MFC] 결과 전송 완료: image_id={verdict_result.get('image_id')}, "
                    f"verdict={verdict_result.get('verdict')}")
        return True
    except (socket.error, OSError) as e:
        logger.error(f"[MFC] 결과 전송 실패: {e}")
        return False


def recv_reclassify_from_mfc(conn: socket.socket) -> dict | None:
    """
    MFC로부터 재분류 결과 수신 (No.401 예상)
    
    프로토콜: 4바이트 헤더 + JSON
    
    Returns:
        {
            "cmd_no": 401,
            "image_id": str,
            "reclassify_verdict": "PASS" | "FAIL",
            "operator_id": str
        }
        수신 실패 시 None
    """
    try:
        # 4바이트 헤더 수신
        raw_len = _recv_exact(conn, 4)
        if not raw_len:
            logger.warning("[MFC] 재분류 헤더 수신 실패 (연결 종료 가능)")
            return None

        msg_len = struct.unpack(">I", raw_len)[0]

        # 페이로드 수신
        raw_payload = _recv_exact(conn, msg_len)
        if not raw_payload:
            logger.warning("[MFC] 재분류 페이로드 수신 실패")
            return None

        data = json.loads(raw_payload.decode("utf-8"))
        logger.info(f"[MFC] 재분류 수신: image_id={data.get('image_id')}, "
                    f"reclassify_verdict={data.get('reclassify_verdict')}")
        return data

    except (json.JSONDecodeError, struct.error) as e:
        logger.error(f"[MFC] 재분류 파싱 오류: {e}")
        return None
    except (socket.error, OSError) as e:
        logger.error(f"[MFC] 재분류 수신 소켓 오류: {e}")
        return None


def handle_mfc_connection(conn: socket.socket, addr: tuple, db_manager) -> None:
    """
    MFC 클라이언트 연결 처리 루프
    
    tcp_server.py에서 MFC 연결 수락 후 별도 쓰레드로 실행됨.
    재분류 요청을 수신하여 DB에 저장.
    
    Args:
        conn: MFC 소켓
        addr: MFC 주소
        db_manager: DB 저장용 (reclassify_result INSERT)
    """
    logger.info(f"[MFC] 연결됨: {addr}")
    try:
        while True:
            data = recv_reclassify_from_mfc(conn)
            if data is None:
                logger.info(f"[MFC] 연결 종료: {addr}")
                break

            cmd_no = data.get("cmd_no")

            if cmd_no == 401:  # 재분류 수신
                _process_reclassify(data, db_manager)
            else:
                logger.warning(f"[MFC] 알 수 없는 cmd_no: {cmd_no}")

    except Exception as e:
        logger.error(f"[MFC] 핸들러 예외: {e}")
    finally:
        conn.close()
        logger.info(f"[MFC] 소켓 종료: {addr}")


def _process_reclassify(data: dict, db_manager) -> None:
    """재분류 데이터 유효성 검사 후 DB 저장"""
    required_keys = {"image_id", "reclassify_verdict", "operator_id"}
    if not required_keys.issubset(data.keys()):
        logger.error(f"[MFC] 재분류 데이터 누락 필드: {required_keys - data.keys()}")
        return

    verdict = data["reclassify_verdict"]
    if verdict not in ("PASS", "FAIL"):
        logger.error(f"[MFC] 유효하지 않은 재분류 판정값: {verdict}")
        return

    try:
        db_manager.insert_reclassify_result(
            image_id=data["image_id"],
            reclassify_verdict=verdict,
            operator_id=data["operator_id"],
        )
        logger.info(f"[MFC] 재분류 DB 저장 완료: image_id={data['image_id']}")
    except Exception as e:
        logger.error(f"[MFC] 재분류 DB 저장 실패: {e}")


def _recv_exact(conn: socket.socket, n: int) -> bytes | None:
    """소켓에서 정확히 n바이트 수신 (short read 방지)"""
    buf = b""
    while len(buf) < n:
        chunk = conn.recv(n - len(buf))
        if not chunk:
            return None
        buf += chunk
    return buf