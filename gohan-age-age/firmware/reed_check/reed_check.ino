// リードスイッチ確認用スケッチ
// ご飯アゲアゲ君 / Arduino Nano (ATmega328P)
//
// モーターを回さずにリードスイッチ単体を確認する。
// D13 のオンボードLEDがリードの状態をそのまま映す。
//
// 使い方:
//   書き込んで通電し、磁石を近づけたり離したりする。
//     磁石が近い → LED 点灯
//     磁石が遠い → LED 消灯
//
// 見ること:
//   1. そもそも反応するか（磁石の強さ・向き・距離）
//   2. 何mmまで離すと消えるか。これが円板とリードの隙間の上限になる
//   3. 磁石の向きを変えた時の感度の差
//
// 磁石を回転と同じ動かし方で横切らせた時、ON→OFF→ON と2回反応する
// 場合は、磁石の極方向とリード軸が平行になっていない。向きを90度変える。
//
// 注意:
//   D3（モーター）は能動的に LOW を出すので回らない。
//   ハード側の10kΩプルダウンが外れていても止まったままになる。

const uint8_t PIN_MOTOR = 3;
const uint8_t PIN_REED  = 5;
const uint8_t PIN_LED   = 13;

void setup() {
  // モーターを確実に止めてから他を初期化する。
  digitalWrite(PIN_MOTOR, LOW);
  pinMode(PIN_MOTOR, OUTPUT);

  pinMode(PIN_REED, INPUT_PULLUP);
  pinMode(PIN_LED, OUTPUT);
}

void loop() {
  // リードの状態をそのまま LED に出す。
  // ここでは連続判定をしない。生の反応を見るためのスケッチなので、
  // チャタリングも含めてそのまま表示する。
  digitalWrite(PIN_LED, digitalRead(PIN_REED) == LOW ? HIGH : LOW);
}
