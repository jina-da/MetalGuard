# ai_client.py - AI 서버와의 TCP 통신 담당
# 역할: 이미지 전송 → 추론 결과(확률값) 수신
# 주의: 연결을 끊지 않고 유지해야 10ms 달성 가능 (재연결 시 ~50ms 오버헤드)

import socket
import struct
import json
import logging

import config

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

    def infer(self, image_bytes: bytes) -> dict | None:
        """
        이미지를 AI 서버로 전송하고 추론 결과 수신.
        프로토콜: [4B: 이미지 크기 (uint32 big-endian)] + [JPEG 이미지 바이트]
        
        Returns:
            dict: 추론 결과 (prob_normal, prob_crack 등)
            None: 실패 시
        """
        try:
            # 이미지 크기를 4바이트 big-endian으로 먼저 전송
            image_size = len(image_bytes)
            self.sock.sendall(struct.pack(">I", image_size))  # >I = big-endian uint32

            # 이미지 바이트 전송
            self.sock.sendall(image_bytes)

            # 응답 수신 (JSON)
            response = self._recv_response()
            return response

        except Exception as e:
            logger.error(f"AI 추론 요청 실패: {e}")
            return None

    def _recv_response(self) -> dict | None:
        """
        AI 서버 응답 수신.
        프로토콜: [4B: JSON 크기 (uint32 big-endian)] + [JSON 바이트]
        """
        try:
            # 4바이트 헤더 먼저 읽어서 JSON 크기 파악
            header = self._recv_exact(4)
            res_size = struct.unpack(">I", header)[0]  # big-endian uint32 → int

            # JSON 크기만큼 정확히 읽기
            res_json = self._recv_exact(res_size)
            return json.loads(res_json.decode("utf-8"))

        except Exception as e:
            logger.error(f"AI 서버 응답 수신 실패: {e}")
            return None

    def _recv_exact(self, size: int) -> bytes:
        """
        소켓에서 정확히 size 바이트만큼 수신.
        TCP는 데이터가 쪼개져서 올 수 있어서 루프로 보장.
        """
        buf = b""
        while len(buf) < size:
            chunk = self.sock.recv(size - len(buf))
            if not chunk:
                raise ConnectionError("소켓 연결 끊김")
            buf += chunk
        return buf

    def close(self):
        """소켓 종료."""
        if self.sock:
            self.sock.close()
            self.sock = None
            logger.info("AI 서버 연결 종료")