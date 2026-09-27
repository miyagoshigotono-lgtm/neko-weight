// 1回転の所要時間を測るスケッチ
// ご飯アゲアゲ君 / Digispark (ATtiny85)
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
//   P1 はオンボードLEDと共用なので、回転中はLEDが点灯する。

const uint8_t PIN_MOTOR  = 1;
const uint8_t PIN_BUTTON = 0;
const uint8_t PIN_REED   = 2;

// 計測する回転数。多いほど平均の精度が上がる。
const uint8_t REVS = 10;

// 原点出しの上限。1回転より短くてよい。
// これを使い切った場合はリードが反応していない。
const uint32_t HOMING_MAX_MS = 5000;

// 計測の上限（全 REVS 回ぶんの合計）。
// リードが途中で反応しなくなった場合に止めるための保険。
const uint32_t MEASURE_MAX_MS = 120000;

// 磁石から離れた直後に再検出しないための不感帯。
// 1回転の時間より十分短く取る。
const uint32_t DEADBAND_MS = 1000;

// この時間ずっと LOW が続いた時だけ検出とみなす。
// RC時定数 5.1kΩ × 1µF = 5.1ms に対して余裕を取って 30ms。
const uint32_t REED_STABLE_MS = 30;

bool pressCount = false;  // false = 次は奇数回（原点出し）

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

// 原点出し。磁石を検出する位置まで送る。
void homing() {
  if (reedDetected()) {
    return;  // 既に原点にいる
  }

  const uint32_t t0 = millis();
  digitalWrite(PIN_MOTOR, HIGH);

  while (!reedDetected()) {
    if (millis() - t0 >= HOMING_MAX_MS) {
      break;
    }
  }

  digitalWrite(PIN_MOTOR, LOW);
}

// REVS 回ぶん回して止まる。
void measure() {
  const uint32_t t0 = millis();
  digitalWrite(PIN_MOTOR, HIGH);

  // 原点の磁石から離れるまで待つ
  while (millis() - t0 < DEADBAND_MS) {
  }

  uint8_t count = 0;
  while (count < REVS) {
    if (millis() - t0 >= MEASURE_MAX_MS) {
      break;
    }

    if (reedDetected()) {
      count++;
      if (count >= REVS) {
        break;
      }
      // 次を数える前に、磁石が検出範囲から抜けるのを待つ
      while (digitalRead(PIN_REED) == LOW) {
        if (millis() - t0 >= MEASURE_MAX_MS) {
          break;
        }
      }
    }
  }

  digitalWrite(PIN_MOTOR, LOW);
}

void setup() {
  digitalWrite(PIN_MOTOR, LOW);
  pinMode(PIN_MOTOR, OUTPUT);
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
