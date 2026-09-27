// 1回転の所要時間を測るテストスケッチ
// ご飯アゲアゲ君 / Digispark (ATtiny85)
//
// 使い方:
//   ボタンを押すと、リードスイッチを REVS 回検出するまでモーターを回して止まる。
//   押した瞬間からストップウォッチで測り、止まった時間を REVS で割れば
//   1回転の所要時間が出る。10回分を平均するので、人間の反応の誤差はほぼ消える。
//
//   同時に SPEC §7 の検証にもなる。色違いの粒を1つ目印に入れておき、
//   ちょうど10ピッチ進んでいれば「1回転＝1ピッチ」が成立している。
//
// 見るべきこと:
//   1. 止まるまでの秒数 ÷ REVS = 1回転の時間
//   2. スクリューが REVS 回ちょうど回ったか（軸に印を付けて数える）
//   3. 目印の粒が REVS ピッチ進んだか
//
// 2 が REVS より少ない回数で止まった場合は、磁石の通過で
// リードスイッチが2回反応している（ON→OFF→ON）。
// その場合は磁石の向きを90度変えるか、MIN_GAP_MS を大きくする。

const uint8_t PIN_MOTOR  = 1;
const uint8_t PIN_BUTTON = 0;
const uint8_t PIN_REED   = 2;

// 何回転させるか。多いほど平均の精度が上がるが、餌が出る点に注意。
const uint8_t REVS = 10;

// 検出してから次の検出を受け付けるまでの最小間隔。
// 磁石1回の通過で2度反応するのを防ぐ。
// 1回転の時間より十分短く、磁石の通過時間より長く取る。
const uint32_t MIN_GAP_MS = 100;

// 保険。この時間を過ぎたら回転数に関わらず止める。
// 検出が全く効いていない場合に餌を出し切らないための上限。
const uint32_t ABORT_MS = 60000;

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

  // ボタンが離されるまで待つ（押しっぱなしで再始動しないように）
  while (digitalRead(PIN_BUTTON) == LOW) {
  }

  const uint32_t t0 = millis();
  uint8_t  count      = 0;
  uint32_t lastHitAt  = t0;
  bool     wasLow     = (digitalRead(PIN_REED) == LOW);

  digitalWrite(PIN_MOTOR, HIGH);

  while (count < REVS) {
    if (millis() - t0 >= ABORT_MS) {
      break;
    }

    const bool isLow = (digitalRead(PIN_REED) == LOW);

    // HIGH → LOW の変化を1回とみなす
    if (isLow && !wasLow && (millis() - lastHitAt >= MIN_GAP_MS)) {
      count++;
      lastHitAt = millis();
    }
    wasLow = isLow;
  }

  digitalWrite(PIN_MOTOR, LOW);

  // 次のボタン押下を待つ
}
