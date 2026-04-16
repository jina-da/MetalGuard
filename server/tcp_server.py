# tcp_server.py - 운용 서버 메인 TCP 서버
#
# 구조:
#   단일 포트(8000)로 모든 클라이언트(카메라+MFC) 연결 수락
#   PacketHeader의 cmdId로 switch 분기 → 각 핸들러 호출
#
# 패킷 구조:
#   [2B: signature] + [2B: cmdId] + [4B: bodySize] + [JSON 바디]
#   이미지 포함 시 JSON 바디 뒤에 [4B: 이미지크기] + [이미지] 추가

import json
import socket
import struct
import threading
import logging

import config
from constants import CmdID, PACKET_HEADER_SIZE, parse_header
from server.db.db_manager import DBManager
from server.handlers.camera_handler import CameraHandler
from server.handlers.mfc_handler import handle_reclassify_confirm

logger = logging.getLogger(__name__)


class TCPServer:
    def __init__(self, image_queue, db_manager: DBManager):
        """
        Args:
            image_queue: 카메라 이미지를 판정 엔진으로 넘기는 Queue
            db_manager: 재분류 수신 시 DB 저장용
        """
        self.image_queue = image_queue
        self.db_manager = db_manager

        self.server_sock: socket.socket | None = None

        # 현재 연결된 MFC 소켓 (판정 결과 전송 시 사용)
        self.mfc_client_sock: socket.socket | None = None
        self.mfc_lock = threading.Lock()

        self.camera_handler = CameraHandler(image_queue)
        self.running = False

    def start(self):
        """단일 포트(8000)로 서버 소켓 바인딩 후 Accept 루프 시작."""
        self.running = True
        self.server_sock = self._create_server_socket(config.SERVER_PORT)

        threading.Thread(
            target=self._accept_loop,
            name="AcceptThread",
            daemon=True
        ).start()

        logger.info(f"서버 대기 중: 포트 {config.SERVER_PORT}")

    def _create_server_socket(self, port: int) -> socket.socket:
        """서버 소켓 생성 및 바인딩. SO_REUSEADDR로 재시작 시 에러 방지."""
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind((config.OPERATION_HOST, port))
        sock.listen(5)
        return sock

    def _accept_loop(self):
        """클라이언트 연결 대기 루프. 연결마다 별도 쓰레드 생성."""
        while self.running:
            try:
                client_sock, addr = self.server_sock.accept()
                logger.info(f"새 연결: {addr}")
                threading.Thread(
                    target=self._handle_client,
                    args=(client_sock, addr),
                    daemon=True
                ).start()
            except Exception as e:
                if self.running:
                    logger.error(f"Accept 오류: {e}")

    def _handle_client(self, sock: socket.socket, addr: tuple):
        """
        클라이언트 연결 처리 루프.
        PacketHeader의 cmdId를 읽어서 switch 분기.
        """
        logger.info(f"클라이언트 연결됨: {addr}")
        try:
            while self.running:
                # 1. 8바이트 헤더 수신
                raw_header = self._recv_exact(sock, PACKET_HEADER_SIZE)
                if not raw_header:
                    break
                logger.info(f"수신 raw 헤더: {raw_header.hex()}") 

                # 2. 헤더 파싱 (시그니처 검증 포함)
                try:
                    cmd_id, body_size = parse_header(raw_header)
                except ValueError as e:
                    logger.error(f"[{addr}] 헤더 파싱 실패: {e}")
                    break

                # 3. JSON 바디 수신
                raw_body = self._recv_exact(sock, body_size)
                if not raw_body:
                    break
                body = json.loads(raw_body.decode("utf-8"))

                # 4. cmdId로 switch 분기
                self._dispatch(sock, addr, cmd_id, body)

        except Exception as e:
            logger.error(f"[{addr}] 클라이언트 핸들러 예외: {e}")
        finally:
            # MFC 소켓이면 참조 제거
            with self.mfc_lock:
                if self.mfc_client_sock is sock:
                    self.mfc_client_sock = None
            sock.close()
            logger.info(f"연결 종료: {addr}")

    def _dispatch(self, sock: socket.socket, addr: tuple, cmd_id: int, body: dict):
        """
        cmdId 기준 switch 분기.
        각 핸들러로 처리 위임.
        """
        match cmd_id:

            # ── 이미지 전송 ───────────────────────────
            case CmdID.IMG_SEND | CmdID.IMG_RECLASSIFY:
                # MFC 소켓 등록 (첫 이미지 전송 시 MFC로 간주)
                with self.mfc_lock:
                    if self.mfc_client_sock is None:
                        self.mfc_client_sock = sock
                self.camera_handler.handle_image(sock, cmd_id, body)

            # ── 재분류 확정 ───────────────────────────
            case CmdID.RECLASSIFY_CONFIRM:
                handle_reclassify_confirm(sock, body, self.db_manager)

            # ── 미정 항목 ─────────────────────────────
            case CmdID.UNCERTAIN_LIST_REQ:
                logger.info(f"[{addr}] UNCERTAIN_LIST_REQ 수신 (미구현)")

            case CmdID.PING:
                logger.info(f"[{addr}] PING 수신 (미구현)")

            case _:
                logger.warning(f"[{addr}] 알 수 없는 cmdId: {cmd_id}")

    def get_mfc_conn(self) -> socket.socket | None:
        """
        현재 연결된 MFC 소켓 반환.
        verdict_engine에서 판정 결과 전송 시 호출.
        """
        with self.mfc_lock:
            return self.mfc_client_sock

    def stop(self):
        """서버 종료."""
        self.running = False
        if self.server_sock:
            self.server_sock.close()
        logger.info("TCP 서버 종료")

    def _recv_exact(self, sock: socket.socket, size: int) -> bytes | None:
        """소켓에서 정확히 size 바이트 수신 (TCP 쪼개짐 대응)"""
        buf = b""
        while len(buf) < size:
            chunk = sock.recv(size - len(buf))
            if not chunk:
                return None
            buf += chunk
        return buf