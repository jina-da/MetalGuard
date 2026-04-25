# ai_client.py - AI 서버와의 TCP 통신 담당
# 역할: 이미지 전송(INFER_REQ) → 추론 결과(INFER_RES) 수신
# 프로토콜: [8B: PacketHeader] + [JSON] + [4B: 이미지크기] + [이미지]
# 주의: 연결 유지 방식 (재연결 시 ~50ms 오버헤드 → 150ms 목표 달성 불가)

import socket
import struct
import json
import logging

import config
from constants import (
    CmdID,
    PACKET_HEADER_SIZE,
    build_packet,
    parse_header,
)

logger = logging.getLogger(__name__)


class AIClient:
    def __init__(self):
        self.sock: socket.socket | None = None  # AI 서버 소켓 (지속 유지)

    def connect(self) -> bool:
        """AI 서버에 TCP 연결. 서버 시작 시 1회만 호출."""
        try:
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.sock.connect((config.AI_HOST, config.AI_PORT))
            logger.info(f"AI 서버 연결 성공: {config.AI_HOST}:{config.AI_PORT}")
            return True
        except Exception as e:
            logger.error(f"AI 서버 연결 실패: {e}")
            return False

    def infer(self, image_bytes: bytes, inspection_id: int) -> dict | None:
        """
        이미지를 AI 서버로 전송하고 추론 결과 수신.

        송신 패킷 구조:
            [8B: PacketHeader (INFER_REQ=101)]
            + [JSON 바디: {"inspection_id": ...}]
            + [4B: 이미지 크기 (big-endian uint32)]
            + [이미지 바이트 (JPEG)]

        수신 패킷 구조:
            [8B: PacketHeader (INFER_RES=102)]
            + [JSON 바디: 추론 결과]

        Args:
            image_bytes: JPEG 이미지 바이트
            inspection_id: 검사 ID (응답 매칭용)

        Returns:
            dict: 추론 결과 (inspection_id, prob_normal 등 포함)
            None: 실패 시
        """
        if self.sock is None:
            logger.error("AI 서버 소켓 없음 (연결 안 됨)")
            return None

        try:
            self._send_infer_request(image_bytes, inspection_id)
            return self._recv_infer_response()
        except Exception as e:
            logger.error(f"AI 추론 요청 실패: {e}")
            return None

    def _send_infer_request(self, image_bytes: bytes, inspection_id: int) -> None:
        """INFER_REQ(101) 패킷 전송."""
        # JSON 바디 구성
        body_bytes: bytes = json.dumps({"inspection_id": inspection_id}).encode("utf-8")

        # PacketHeader + JSON 바디 (build_packet 사용)
        packet: bytes = build_packet(CmdID.INFER_REQ, body_bytes)

        # 이미지 크기(4B) + 이미지 바이트 추가
        image_size_bytes: bytes = struct.pack(">I", len(image_bytes))

        self.sock.sendall(packet + image_size_bytes + image_bytes)
        logger.debug(f"INFER_REQ 전송: inspection_id={inspection_id}, image={len(image_bytes)}B")

    def _recv_infer_response(self) -> dict | None:
        """
        INFER_RES(102) 패킷 수신.
        수신 구조: [8B: PacketHeader] + [JSON 바디]
        """
        # PacketHeader 수신 및 파싱 (parse_header가 signature 검증 포함)
        header_raw: bytes = self._recv_exact(PACKET_HEADER_SIZE)
        cmd_id, body_size = parse_header(header_raw)  # 시그니처 불일치 시 ValueError 발생

        # cmdId 검증
        if cmd_id != CmdID.INFER_RES:
            logger.error(f"예상치 못한 cmdId: {cmd_id} (INFER_RES={CmdID.INFER_RES} 기대)")
            return None

        # JSON 바디 수신
        body_bytes: bytes = self._recv_exact(body_size)
        result: dict = json.loads(body_bytes.decode("utf-8"))

        logger.debug(f"INFER_RES 수신: inspection_id={result.get('inspection_id')}, "
                     f"inference_ms={result.get('inference_ms')}ms")
        return result

    def _recv_exact(self, size: int) -> bytes:
        """
        소켓에서 정확히 size 바이트만큼 수신.
        TCP는 데이터가 쪼개져서 올 수 있어서 루프로 보장.
        """
        buf = b""
        while len(buf) < size:
            chunk = self.sock.recv(size - len(buf))
            if not chunk:
                raise ConnectionError("AI 서버 소켓 연결 끊김")
            buf += chunk
        return buf

    def close(self) -> None:
        """소켓 종료."""
        if self.sock:
            self.sock.close()
            self.sock = None
            logger.info("AI 서버 연결 종료")