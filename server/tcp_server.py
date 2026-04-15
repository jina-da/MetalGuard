# tcp_server.py - 운용 서버 메인 TCP 서버
# 역할: 카메라 PC / MFC GUI 연결 수락 후 각각 전용 쓰레드로 분리
# 구조: Accept 루프(메인) → camera_thread / mfc_thread 생성

import socket
import threading
import logging

import config

from server.handlers.camera_handler import CameraHandler

logger = logging.getLogger(__name__)


class TCPServer:
    def __init__(self, image_queue):
        """
        Args:
            image_queue: 카메라에서 받은 이미지를 판정 엔진으로 넘기는 Queue
        """
        self.image_queue = image_queue

        # 카메라 수신용 서버 소켓
        self.camera_server_sock: socket.socket | None = None

        # MFC 수신용 서버 소켓
        self.mfc_server_sock: socket.socket | None = None

        # 연결된 MFC 소켓 (결과 전송 시 사용)
        self.mfc_client_sock: socket.socket | None = None
        self.mfc_lock = threading.Lock()  # MFC 소켓 동시 접근 방지
        self.camera_handler = CameraHandler(image_queue)  # 카메라 핸들러

        self.running = False  # 서버 실행 상태 플래그

    def start(self):
        """카메라/MFC 서버 소켓 바인딩 후 Accept 루프 쓰레드 시작."""
        self.running = True

        # 카메라용 서버 소켓 생성 및 바인딩
        self.camera_server_sock = self._create_server_socket(config.CAMERA_PORT)
        # MFC용 서버 소켓 생성 및 바인딩
        self.mfc_server_sock = self._create_server_socket(config.MFC_PORT)

        # 카메라 Accept 루프 쓰레드 시작
        threading.Thread(
            target=self._accept_loop,
            args=(self.camera_server_sock, self._handle_camera),
            name="CameraAcceptThread",
            daemon=True  # 메인 프로세스 종료 시 같이 종료
        ).start()

        # MFC Accept 루프 쓰레드 시작
        threading.Thread(
            target=self._accept_loop,
            args=(self.mfc_server_sock, self._handle_mfc),
            name="MFCAcceptThread",
            daemon=True
        ).start()

        logger.info(f"카메라 서버 대기 중: 포트 {config.CAMERA_PORT}")
        logger.info(f"MFC 서버 대기 중: 포트 {config.MFC_PORT}")

    def _create_server_socket(self, port: int) -> socket.socket:
        """
        서버 소켓 생성 및 바인딩.
        SO_REUSEADDR: 서버 재시작 시 'Address already in use' 에러 방지
        """
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind((config.OPERATION_HOST, port))
        sock.listen(5)  # 최대 5개 연결 대기
        return sock

    def _accept_loop(self, server_sock: socket.socket, handler):
        """
        클라이언트 연결 대기 루프.
        연결 들어오면 handler 함수를 새 쓰레드로 실행.
        """
        while self.running:
            try:
                client_sock, addr = server_sock.accept()
                logger.info(f"새 연결: {addr} → {handler.__name__}")

                # 연결마다 전용 쓰레드 생성
                threading.Thread(
                    target=handler,
                    args=(client_sock, addr),
                    daemon=True
                ).start()

            except Exception as e:
                if self.running:
                    logger.error(f"Accept 오류: {e}")

    def _handle_camera(self, sock: socket.socket, addr: tuple):
        """카메라 연결을 CameraHandler에게 위임."""
        self.camera_handler.handle(sock, addr)

    def _handle_mfc(self, sock: socket.socket, addr: tuple):
        """
        MFC 연결 처리 쓰레드.
        실제 송수신 로직은 mfc_handler.py 에서 구현 예정.
        """
        logger.info(f"MFC 연결됨: {addr}")
        with self.mfc_lock:
            self.mfc_client_sock = sock  # 결과 전송용으로 저장

        try:
            # TODO: mfc_handler.py 연동 예정
            while self.running:
                data = sock.recv(1024)
                if not data:
                    break
                logger.info(f"MFC로부터 데이터 수신: {len(data)} bytes")
        except Exception as e:
            logger.error(f"MFC 핸들러 오류: {e}")
        finally:
            with self.mfc_lock:
                self.mfc_client_sock = None
            sock.close()
            logger.info(f"MFC 연결 종료: {addr}")

    def stop(self):
        """서버 종료."""
        self.running = False
        if self.camera_server_sock:
            self.camera_server_sock.close()
        if self.mfc_server_sock:
            self.mfc_server_sock.close()
        logger.info("TCP 서버 종료")