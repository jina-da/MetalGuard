#include <Servo.h>
#include <Wire.h> 
#include <LiquidCrystal_I2C.h>

// 핀 번호 정의
const int BTN_RESET = 2;        // 수동 리셋 버튼
const int LED_POWER = 5;        // Whilte LED
const int LED_PASS = 6;         // Green LED
const int LED_FAIL = 7;         // Red LED
const int LED_UNCERTAIN = 8;    // Yellow LED
const int BUZZER = 11;          // 부저
const int SERVO_PIN = 12;       // 게이트 서보 모터

// 게이트 각도 설정
const int GATE_NORMAL = 90;    // 기본 위치
const int GATE_A = 0;          // 폐기 (FAIL)
const int GATE_B = 180;        // 재분류 (TIMEOUT, UNCERTAIN)

// 작업 완료 신호
const char* PASS_DONE      = "P_done";
const char* FAIL_DONE      = "F_done";
const char* UNCERTAIN_DONE = "U_done";
const char* TIMEOUT_DONE   = "T_done";

// 연결 확인 신호
unsigned long last_heartbeat = 0;
const unsigned long HEARTBEAT_TIMEOUT = 8000; // 일정 시간 동안 신호 없으면 끊겼다고 간주함
bool isConnected = false;

// 객체 생성
Servo gateServo;

// I2C 주소 0x27, 16열 2행 설정
LiquidCrystal_I2C lcd(0x27, 16, 2);

void setup() {
  Serial.begin(9600);
  
  // 핀 모드 설정
  pinMode(BTN_RESET, INPUT_PULLUP);
  pinMode(LED_PASS, OUTPUT);
  pinMode(LED_FAIL, OUTPUT);
  pinMode(LED_UNCERTAIN, OUTPUT);
  pinMode(BUZZER, OUTPUT);
  
  gateServo.attach(SERVO_PIN);
  gateServo.write(GATE_NORMAL);

  digitalWrite(LED_POWER, HIGH);

  lcd.init();
  lcd.backlight();
  lcd_display("ARDUINO POWER ON");
}

void loop() {
  if (digitalRead(BTN_RESET) == HIGH) {
    sys_reset();                   // 수동 리셋
    if(isConnected)
      lcd_display("READY...");
    else
      lcd_display("DISCONNECTED");
  }

  if (Serial.available() > 0) {
    char verdict = Serial.read();
    
    if (verdict == '\n' || verdict == '\r') return;

    last_heartbeat = millis();
    if (!isConnected) {
      isConnected = true;
      lcd_display("READY...");      // 연결 확인 후 화면 갱신
    }

    switch (verdict) {
      case 'P': // VERDICT_PASS
        led_on(HIGH, LOW, LOW);     // Green
        lcd_display("PASS");
        motor_normal();
        led_off();
        notify_done(PASS_DONE);
        break;

      case 'F': // VERDICT_FAIL
        led_on(LOW, HIGH, LOW);     // Red
        buzzer_on(1);
        lcd_display("FAIL");
        motor_on(GATE_A);
        led_off();
        notify_done(FAIL_DONE);
        break;

      case 'T': // VERDICT_TIMEOUT
        led_on(LOW, LOW, HIGH);      // Yellow ON
        buzzer_on(2);
        lcd_display("TIMEOUT");
        motor_on(GATE_B);
        led_off();
        notify_done(TIMEOUT_DONE);
        break;

      case 'U': // VERDICT_UNCERTAIN
        led_on(LOW, LOW, HIGH);      // Yellow ON
        buzzer_on(2);
        lcd_display("UNCERTAIN");
        motor_on(GATE_B);
        led_off();
        notify_done(UNCERTAIN_DONE);
        break;

      case 'N':
        sys_reset();
        break;

      case 'H': // Heartbeat 신호 (PC에서 주기적으로 송신)
        Serial.println("PONG");
      break;

      default:
        // 정의되지 않은 데이터 수신 시 유지
        break;
    }
    lcd_display("READY...");    
  }

  // 연결 체크
  if (isConnected && (millis() - last_heartbeat > HEARTBEAT_TIMEOUT)) {
    isConnected = false;
    lcd_display("DISCONNECTED");
  }
}

// LED 제어
void led_on(int p, int f, int u) {
  digitalWrite(LED_PASS, p);
  digitalWrite(LED_FAIL, f);
  digitalWrite(LED_UNCERTAIN, u);
}

void led_off(){
  digitalWrite(LED_PASS, LOW);
  digitalWrite(LED_FAIL, LOW);
  digitalWrite(LED_UNCERTAIN, LOW);
}

// 부저 제어
void buzzer_on(int count) {
  for (int i = 0; i < count; i++) {
    tone(BUZZER, 1000);
    delay(100);
    noTone(BUZZER);
    if (count > 1) delay(100);
  }
}

// 모터 제어
void motor_on(int gate) {
  if (!gateServo.attached())
    gateServo.attach(SERVO_PIN);

  gateServo.write(gate);
  delay(500);
  gateServo.write(GATE_NORMAL);
  delay(500);
  gateServo.detach();
}

// 모터 원위치
void motor_normal() {
  if (!gateServo.attached())
    gateServo.attach(SERVO_PIN);

  gateServo.write(GATE_NORMAL);
  delay(500);
  gateServo.detach();
}

// LCD 제어
void lcd_display(const char* notice) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(notice);
}

// 작업 완료 알림
void notify_done(const char* signal) {
  Serial.println(signal);
}

// 리셋 함수
void sys_reset() {
  led_off();
  lcd_display("RESET");
  motor_normal();
}
