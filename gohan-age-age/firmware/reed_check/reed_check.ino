// リードスイッチ動作確認スケッチ
// ご飯アゲアゲ君 / Digispark (ATtiny85)
//
// ウォームギヤのセルフロックで手回しができないため、
// モーターで回しながらリードスイッチの反応を確認する。
//
// 使い方:
//   パイプを空にしてからボタンを押す。モーターが回り出す。
//
// 判定:
//   MAX_RUN_MS より前に止まった  → リードが反応している。
//                                   止まるまでの秒数が1回転の時間。
//   MAX_RUN_MS きっちり回り続けた → リードが反応していない。
//                                   磁石が遠い、向きが悪い、
//                                   またはリードスイッチの不良。
//
// 繰り返し押すと毎回同じ秒数で止まるはず。
// 秒数がばらつく場合は磁石の検出が不安定なので、
// 距離を詰めるか磁石を強いものに替える。

const uint8_t PIN_MOTOR  = 1;
const uint8_t PIN_BUTTON = 0;
const uint8_t PIN_REED   = 2;

// 検出を無視する時間。前回の停止位置が磁石の上だった場合に
// 即停止してしまうのを防ぐ。1回転の時間より十分短く取る。
const uint32_t DEADBAND_MS = 1000;

// これを過ぎたら検出できなくても止める。
// 1回転（実測で約6秒）より確実に長く、かつ餌が出過ぎない範囲で。
const uint32_t MAX_RUN_MS = 15000;

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

  const uint32_t t0 = millis();
  digitalWrite(PIN_MOTOR, HIGH);

  while (millis() - t0 < DEADBAND_MS) {
  }

  while (digitalRead(PIN_REED) == HIGH) {
    if (millis() - t0 >= MAX_RUN_MS) {
      break;
    }
  }

  digitalWrite(PIN_MOTOR, LOW);
}
