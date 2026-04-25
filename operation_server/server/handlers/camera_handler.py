# camera_handler.py - 카메라로부터 이미지 수신 처리
#
# 역할: JSON 바디 파싱 → 이미지 바이트 수신 → Queue 적재
# 헤더 파싱은 tcp_server.py에서 완료 후 호출됨
#
# 이미지 수신 구조 (헤더 이후):
#   [JSON 바디] + [4B: 이미지크기] + [이미지 바이트]

import queue
import socket
import struct
import logging
import time
from dataclasses import dataclass, field

from constants import CmdID

logger = logging.getLogger(__name__)


@dataclass
class ImageTask:
    """카메라에서 받은 이미지 1건. Queue에 적재되어 판정 엔진으로 전달됨."""
    cmd_id: int                # CmdID.IMG_SEND | CmdID.IMG_RECLASSIFY
    inspection_id: int | None  # 재분류 시 원본 판정 ID, 첫 분류는 None
    timestamp: str             # 카메라 촬영 타임스탬프
    client_id: str             # 카메라 클라이언트 ID
    image_bytes: bytes         # 수신한 이미지 바이트
    plate_id: int | None       # 철판 단위 ID (민기님 필드 추가 후 사용)
    shot_index: int | None     # 현재 몇 번째 장인지 (1부터 시작)
    total_shots: int | None    # 철판 1개당 총 촬영 장수
    received_at: float = field(default_factory=time.monotonic)  # 파이프라인 시간 측정 기준 (default 있는 필드는 맨 뒤)


class CameraHandler:
    def __init__(self, image_queue: queue.Queue):
        """
        Args:
            image_queue: ImageTask 적재할 Queue (verdict_engine이 소비)
        """
        self.image_queue = image_queue

    def handle_image(self, sock: socket.socket, cmd_id: int, body: dict):
        """
        tcp_server._dispatch()에서 호출됨.
        JSON 바디 파싱 완료 상태로 들어오므로 이미지만 추가 수신.

        Args:
            sock: 클라이언트 소켓 (이미지 바이트 수신용)
            cmd_id: CmdID.IMG_SEND 또는 CmdID.IMG_RECLASSIFY
            body: 파싱된 JSON 바디
        """
        # 이미지 크기 수신 (4B)
        raw_img_size = self._recv_exact(sock, 4)
        if raw_img_size is None:
            logger.error("[CameraHandler] 이미지 크기 수신 실패")
            return
        image_size = struct.unpack(">I", raw_img_size)[0]

        # 이미지 바이트 수신
        image_bytes = self._recv_exact(sock, image_size)
        if image_bytes is None:
            logger.error("[CameraHandler] 이미지 바이트 수신 실패")
            return

        # ImageTask 생성 후 Queue 적재
        task = ImageTask(
            cmd_id=cmd_id,
            inspection_id=body.get("inspection_id"),
            timestamp=body.get("timestamp", ""),
            client_id=body.get("client_id", "unknown"),
            image_bytes=image_bytes,
            plate_id=body.get("plate_id"),        # 철판 ID (민기님 필드 추가 후 채워짐)
            shot_index=body.get("shot_index"),    # 몇 번째 장인지
            total_shots=body.get("total_shots"),  # 총 촬영 장수
        )
        self.image_queue.put(task)
        logger.debug(
            f"[CameraHandler] Queue 적재: cmd_id={task.cmd_id} "
            f"plate={task.plate_id} shot={task.shot_index}/{task.total_shots} "
            f"size={image_size}bytes client={task.client_id}"
        )

    def _recv_exact(self, sock: socket.socket, size: int) -> bytes | None:
        """소켓에서 정확히 size 바이트 수신 (TCP 쪼개짐 대응)"""
        buf = b""
        while len(buf) < size:
            chunk = sock.recv(size - len(buf))
            if not chunk:
                return None
            buf += chunk
        return buf