#include <Servo.h>
#include <Wire.h> 
#include <LiquidCrystal_I2C.h>

// 핀 번호 정의
const int LED_POWER = 5;     // Whilte LED
const int LED_PASS = 6;      // Green LED
const int LED_FAIL = 7;      // Red LED
const int LED_UNCERTAIN = 8; // Yellow LED
const int BUZZER = 11;         // 부저
const int SERVO_PIN = 12;      // 게이트 서보 모터

// 게이트 각도 설정
const int GATE_NORMAL = 90;    // 기본 위치
const int GATE_A = 0;          // 폐기 (FAIL)
const int GATE_B = 180;        // 재분류 (TIMEOUT, UNCERTAIN)

// 객체 생성
Servo gateServo;

// I2C 주소 0x27, 16열 2행 설정
LiquidCrystal_I2C lcd(0x27, 16, 2);

void setup() {
  Serial.begin(9600);
  
  // LED 핀 모드 설정
  pinMode(LED_PASS, OUTPUT);
  pinMode(LED_FAIL, OUTPUT);
  pinMode(LED_UNCERTAIN, OUTPUT);

  pinMode(BUZZER, OUTPUT);
  
  gateServo.attach(SERVO_PIN);
  gateServo.write(GATE_NORMAL);

  digitalWrite(LED_POWER, HIGH);

  // 기존 설정 생략
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("READY...");
}

void loop() {
  if (Serial.available() > 0) {
    char verdict = Serial.read();
    
    if (verdict == '\n' || verdict == '\r') return;

    switch (verdict) {
      case 'P': // VERDICT_PASS
        led_on(HIGH, LOW, LOW);     // Green
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("PASS");

        if (!gateServo.attached())
          gateServo.attach(SERVO_PIN);
        delay(500);
        gateServo.write(GATE_NORMAL);
        delay(500);
        gateServo.detach();

        led_off();
        break;

      case 'F': // VERDICT_FAIL
        led_on(LOW, HIGH, LOW);     // Red
        triggerBuzzer(1);
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("FAIL");

        if (!gateServo.attached())
          gateServo.attach(SERVO_PIN);

        gateServo.write(GATE_A);
        delay(500);
        gateServo.write(GATE_NORMAL);
        delay(500);
        gateServo.detach();     // 정지 명령어 : 5V 전압 문제 해결용

        led_off();
        break;

      case 'T': // VERDICT_TIMEOUT
        led_on(LOW, LOW, HIGH);      // Yellow ON
        triggerBuzzer(2);
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("TIMEOUT");
        
        if (!gateServo.attached())
          gateServo.attach(SERVO_PIN);

        gateServo.write(GATE_B);
        delay(500);
        gateServo.write(GATE_NORMAL);
        delay(500);
        gateServo.detach();

        led_off();
        break;

      case 'U': // VERDICT_UNCERTAIN
        led_on(LOW, LOW, HIGH);      // Yellow ON
        triggerBuzzer(2);
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("UNCERTAIN");

        if (!gateServo.attached())
          gateServo.attach(SERVO_PIN);
        
        gateServo.write(GATE_B);
        delay(500);
        gateServo.write(GATE_NORMAL);
        delay(500);
        gateServo.detach();

        led_off();
        break;

      case 'N':
        led_off();
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("READY...");

        if (!gateServo.attached())
          gateServo.attach(SERVO_PIN);

        gateServo.write(GATE_NORMAL);
        delay(500);
        gateServo.detach();
        break;

      default:
        // 정의되지 않은 데이터 수신 시 유지
        break;
    }

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("READY...");
  }
}

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
void triggerBuzzer(int count) {
  for (int i = 0; i < count; i++) {
    tone(BUZZER, 1000);
    delay(100);
    noTone(BUZZER);
    if (count > 1) delay(100);
  }
}
