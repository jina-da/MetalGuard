# camera_handler.py - 카메라 PC로부터 이미지 수신 처리
# 역할: JSON 헤더 파싱 → 이미지 바이트 수신 → Queue 적재
# 프로토콜: [4B 헤더크기] + [JSON 헤더] + [이미지 바이트]

import json
import queue
import socket
import struct
import logging
from dataclasses import dataclass, field
from datetime import datetime

logger = logging.getLogger(__name__)


@dataclass
class ImageTask:
    """카메라에서 받은 이미지 1건 단위. Queue에 적재되어 판정 엔진으로 전달됨."""
    mode: str                 # "inspect" or "reclassify"
    inspection_id: int | None # 재분류 시 원본 판정 ID, 첫 분류는 None
    timestamp: str            # 카메라 촬영 타임스탬프
    client_id: str            # 카메라 클라이언트 ID
    image_bytes: bytes        # 수신한 이미지 바이트
    received_at: datetime = field(default_factory=datetime.now)  # T0 기준점


class CameraHandler:
    def __init__(self, image_queue: queue.Queue):
        """
        Args:
            image_queue: 이미지 적재할 Queue (verdict_engine이 꺼내서 처리)
        """
        self.image_queue = image_queue

    def handle(self, sock: socket.socket, addr: tuple):
        """카메라 연결 1개를 처리하는 메인 루프."""
        logger.info(f"카메라 핸들러 시작: {addr}")
        try:
            while True:
                # 1. JSON 헤더 수신
                header = self._recv_header(sock)
                if header is None:
                    break

                # 2. 이미지 바이트 수신
                image_bytes = self._recv_exact(sock, header["image_size"])
                if image_bytes is None:
                    break

                # 3. ImageTask 생성 후 Queue 적재
                task = ImageTask(
                    mode=header["mode"],
                    inspection_id=header.get("inspection_id"),
                    timestamp=header["timestamp"],
                    client_id=header["client_id"],
                    image_bytes=image_bytes,
                )
                self.image_queue.put(task)
                logger.info(
                    f"이미지 Queue 적재: mode={task.mode} "
                    f"size={len(image_bytes)}bytes "
                    f"client={task.client_id}"
                )

        except Exception as e:
            logger.error(f"카메라 핸들러 오류: {e}")
        finally:
            sock.close()
            logger.info(f"카메라 연결 종료: {addr}")

    def _recv_header(self, sock: socket.socket) -> dict | None:
        """JSON 헤더 수신. 프로토콜: [4B: 헤더 크기] + [JSON 바이트]"""
        try:
            raw_size = self._recv_exact(sock, 4)
            if raw_size is None:
                return None
            header_size = struct.unpack(">I", raw_size)[0]
            raw_header = self._recv_exact(sock, header_size)
            if raw_header is None:
                return None
            return json.loads(raw_header.decode("utf-8"))
        except Exception as e:
            logger.error(f"헤더 수신 실패: {e}")
            return None

    def _recv_exact(self, sock: socket.socket, size: int) -> bytes | None:
        """소켓에서 정확히 size 바이트 수신. TCP 쪼개짐 대응."""
        buf = b""
        try:
            while len(buf) < size:
                chunk = sock.recv(size - len(buf))
                if not chunk:
                    return None
                buf += chunk
            return buf
        except Exception as e:
            logger.error(f"데이터 수신 실패: {e}")
            return None