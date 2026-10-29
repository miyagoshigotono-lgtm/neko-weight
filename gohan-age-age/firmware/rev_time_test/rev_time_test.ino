// 1回転の所要時間を測るスケッチ
// ご飯アゲアゲ君 / Arduino Nano (ATmega328P)
//
// ボタンを押すたびに、奇数回と偶数回で動作が切り替わる。
//
//   奇数回（1, 3, 5...）= 原点出し
//     磁石を検出する位置までスクリューを送る。最大 HOMING_MAX_MS。
//     既に検出位置にいる場合は何もしない（10回転の直後はこの状態）。
//
//   偶数回（2, 4, 6...）= 計測
//     REVS 回ぶん回して止まる。原点から始まり原点で終わるので、
//     押した瞬間から止まるまでの時間を REVS で割れば1回転の時間になる。
//
// 測り方:
//   1回目を押して原点出し。止まるのを待つ。
//   2回目を押すのと同時にストップウォッチ開始。
//   モーターが止まったらストップウォッチ停止。その秒数 ÷ REVS。
//
//   3回目以降も同じ手順で繰り返せる。何度か測って値が揃うか確認する。
//
// 注意:
//   パイプは空にしておく。REVS 回ぶんの餌が出る。
//   MOTOR_DUTY は本体側と同じ値にすること。

const uint8_t PIN_MOTOR  = 3;
const uint8_t PIN_BUTTON = 4;
const uint8_t PIN_REED   = 5;

// 本体側の MOTOR_DUTY と必ず同じ値にすること。
// 違う値で測ると、得られた REV_MS が本番の速度と合わない。
const uint8_t  MOTOR_DUTY = 100;
const uint32_t KICK_MS    = 150;

void motorOn() {
  OCR2B = 255;
  TCCR2A |= _BV(COM2B1);
  delay(KICK_MS);
  OCR2B = MOTOR_DUTY;
}

void motorOff() {
  TCCR2A &= ~_BV(COM2B1);
  digitalWrite(PIN_MOTOR, LOW);
}

// 計測する回転数。多いほど平均の精度が上がる。
const uint8_t REVS = 10;

// 原点出しの上限。1回転より長く取る。
// デューティを下げると1回転が伸びるので、余裕を持たせてある。
// これを使い切った場合はリードが反応していない。
const uint32_t HOMING_MAX_MS = 15000;

// 計測の上限（全 REVS 回ぶんの合計）。
// リードが途中で反応しなくなった場合に止めるための保険。
const uint32_t MEASURE_MAX_MS = 120000;

// この時間ずっと同じ状態が続いたら確定とみなす。
// 本体側と同じ値にすること。判定方法が違うと測定値が本番と合わない。
const uint32_t REED_STABLE_MS = 100;

bool pressCount = false;  // false = 次は奇数回（原点出し）

// 磁石を検出した（REED_STABLE_MS のあいだ連続で LOW）。
bool reedDetected() {
  if (digitalRead(PIN_REED) == HIGH) {
    return false;
  }
  const uint32_t s = millis();
  while (millis() - s < REED_STABLE_MS) {
    if (digitalRead(PIN_REED) == HIGH) {
      return false;
    }
  }
  return true;
}

// 磁石から離れた（REED_STABLE_MS のあいだ連続で HIGH）。
bool reedCleared() {
  if (digitalRead(PIN_REED) == LOW) {
    return false;
  }
  const uint32_t s = millis();
  while (millis() - s < REED_STABLE_MS) {
    if (digitalRead(PIN_REED) == LOW) {
      return false;
    }
  }
  return true;
}

// 原点出し。磁石を検出する位置まで送る。
void homing() {
  if (reedDetected()) {
    return;  // 既に原点にいる
  }

  const uint32_t t0 = millis();
  motorOn();

  while (!reedDetected()) {
    if (millis() - t0 >= HOMING_MAX_MS) {
      break;
    }
  }

  motorOff();
}

// REVS 回ぶん回して止まる。
// 判定は本体の rotateOnce() と同じ「離れた確認 → 検出」の繰り返し。
// 固定時間の不感帯は使わない。磁石の検出範囲が何度ぶんあるか分からないため、
// 時間で待つと範囲の広さ次第で即検出してしまう。
void measure() {
  const uint32_t t0 = millis();
  motorOn();

  uint8_t count = 0;
  while (count < REVS) {
    // 1. 磁石から離れるまで
    while (!reedCleared()) {
      if (millis() - t0 >= MEASURE_MAX_MS) {
        motorOff();
        return;
      }
    }

    // 2. 次に磁石を検出するまで
    while (!reedDetected()) {
      if (millis() - t0 >= MEASURE_MAX_MS) {
        motorOff();
        return;
      }
    }

    count++;
  }

  motorOff();
}

void setup() {
  digitalWrite(PIN_MOTOR, LOW);
  pinMode(PIN_MOTOR, OUTPUT);

  // D3 = OC2B。分周なし Fast PWM で 62.5kHz。
  TCCR2A = _BV(WGM21) | _BV(WGM20);
  TCCR2B = _BV(CS20);
  OCR2B  = 0;

  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_REED, INPUT_PULLUP);
}

void loop() {
  if (digitalRead(PIN_BUTTON) != LOW) {
    return;
  }

  // 押しっぱなしで連続始動しないよう、離されるまで待つ
  while (digitalRead(PIN_BUTTON) == LOW) {
  }

  if (!pressCount) {
    homing();
  } else {
    measure();
  }
  pressCount = !pressCount;
}
