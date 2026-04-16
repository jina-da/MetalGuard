"""
arduino_serial.py
아두이노 시리얼 통신 모듈
- 판정 결과를 단일 문자로 전송: P / F / U / T
- 위치: server/engine/arduino_serial.py
"""

import logging
import threading
import serial
import serial.serialutil

logger = logging.getLogger(__name__)

# 판정 → 시리얼 명령 매핑
VERDICT_CMD: dict[str, bytes] = {
    "PASS":      b"P\n",
    "FAIL":      b"F\n",
    "UNCERTAIN": b"U\n",
    "TIMEOUT":   b"T\n",
    "TEST":      b"T\n",
}


class ArduinoSerial:
    """
    아두이노 시리얼 통신 래퍼.
    쓰레드 안전 전송을 위해 내부 Lock 사용.
    
    Usage:
        arduino = ArduinoSerial()
        arduino.connect()
        arduino.send_verdict("PASS")
        arduino.disconnect()
    """

    def __init__(
        self,
        port: str = "/dev/ttyACM0",
        baudrate: int = 9600,
        timeout: float = 1.0,
    ):
        self._port = port
        self._baudrate = baudrate
        self._timeout = timeout
        self._serial: serial.Serial | None = None
        self._lock = threading.Lock()

    def connect(self) -> bool:
        """
        시리얼 포트 연결.
        
        Returns:
            연결 성공 여부
        """
        try:
            self._serial = serial.Serial(
                port=self._port,
                baudrate=self._baudrate,
                timeout=self._timeout,
            )
            logger.info(f"[Arduino] 연결됨: {self._port} @ {self._baudrate}bps")
            return True
        except serial.serialutil.SerialException as e:
            logger.error(f"[Arduino] 연결 실패: {e}")
            self._serial = None
            return False

    def disconnect(self) -> None:
        """시리얼 포트 닫기"""
        with self._lock:
            if self._serial and self._serial.is_open:
                self._serial.close()
                logger.info("[Arduino] 연결 종료")
            self._serial = None

    def send_verdict(self, verdict: str) -> bool:
        """
        판정 결과를 아두이노로 전송.
        
        Args:
            verdict: "PASS" | "FAIL" | "UNCERTAIN" | "TEST"
        Returns:
            전송 성공 여부
        """
        cmd = VERDICT_CMD.get(verdict)
        if cmd is None:
            logger.error(f"[Arduino] 알 수 없는 판정값: {verdict}")
            return False

        return self._send(cmd)

    def send_test(self) -> bool:
        """연결 확인용 테스트 신호 전송"""
        return self._send(VERDICT_CMD["TEST"])

    @property
    def is_connected(self) -> bool:
        return self._serial is not None and self._serial.is_open

    # ──────────────────────────────────────────────
    # 내부 메서드
    # ──────────────────────────────────────────────

    def _send(self, cmd: bytes) -> bool:
        """쓰레드 안전 시리얼 전송"""
        with self._lock:
            if not self.is_connected:
                logger.warning("[Arduino] 연결되지 않음, 전송 스킵")
                return False
            try:
                self._serial.write(cmd)
                logger.debug(f"[Arduino] 전송: {cmd!r}")
                return True
            except serial.serialutil.SerialException as e:
                logger.error(f"[Arduino] 전송 실패: {e}")
                return False