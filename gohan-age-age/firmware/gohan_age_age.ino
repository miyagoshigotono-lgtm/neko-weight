// ご飯アゲアゲ君 ファームウェア
// Arduino Nano / ATmega328P
// 仕様書: ../SPEC.md §5
//
// 動作の概要:
//   電源投入 → 待機状態（何もしない）
//   1回目の長押し → 原点出し。リードスイッチが反応する位置まで送って止まる。
//                   電源投入後に1度だけ必要。以降は毎回原点で止まるので不要。
//   2回目以降の長押し → その時刻を起点として FEED_COUNT 回の給餌を開始
//
//   短押しには機能を割り当てていない。ノイズや接触で誤って
//   1日分の予定が始まらないようにするため、長押しのみを受け付ける。

// ---- ピン割り当て ----------------------------------------------------------
// Arduino Nano（ATmega328P / 5V / 16MHz）。
// D13 のオンボードLEDは他と共用していないので、動作表示に使える。
// D0 / D1 は USBシリアルに繋がるため空けておく。
const uint8_t PIN_MOTOR  = 3;  // MOSFETゲート出力
const uint8_t PIN_BUTTON = 4;  // 装填完了ボタン（外付けプルアップ、押下でLOW）
const uint8_t PIN_REED   = 5;  // リードスイッチ（外付けプルアップ、磁石検出でLOW）
const uint8_t PIN_LED    = 13; // オンボードLED。給餌中に点灯

// ---- 給餌スケジュール ------------------------------------------------------
// FEED_COUNT はパイプの部屋数と一致させること。
// 実機のスクリューは6部屋で造形した。
//
// 起点+60分に1回目、以降168分間隔、6回目は起点+15時間。
//   60 + 168 × 5 = 900分 = 15時間
// SPEC §8 の通り、この時間割は運用しながら調整する。
const uint8_t  FEED_COUNT      = 6;
const uint32_t FIRST_DELAY_MIN = 60;
const uint32_t INTERVAL_MIN    = 168;

// ベンチテスト用。1にすると分単位の予定を無視し、
// TEST_INTERVAL_MS 間隔で FEED_COUNT 回ぶん動かす。
// 6回 × (5秒 + 1回転2秒) ≒ 42秒で一巡するので、その場で確認できる。
// 実運用の書き込み時は必ず 0 に戻すこと。
#define TEST_MODE 0

const uint32_t TEST_INTERVAL_MS = 5000UL;

#if TEST_MODE
const uint32_t FIRST_DELAY_MS = TEST_INTERVAL_MS;
const uint32_t INTERVAL_MS    = TEST_INTERVAL_MS;
#else
const uint32_t FIRST_DELAY_MS = FIRST_DELAY_MIN * 60000UL;
const uint32_t INTERVAL_MS    = INTERVAL_MIN * 60000UL;
#endif

// ---- 動作パラメータ --------------------------------------------------------
// REV_MS: ギヤボックスを繋いだ状態での1回転の所要時間（ミリ秒）。
//   **ここだけを実測値に書き換えれば、下の2つは自動で追従する。**
//   測定は firmware/rev_time_test/ のスケッチで行う。
//
//   実測値: 10回転で20.5秒（ストップウォッチ）→ 1回転 2.03秒。
const uint32_t REV_MS = 2030;

// DEADBAND_MS: 回転開始からリードスイッチを無視する時間。
//   停止時の惰性で磁石を僅かに行き過ぎるため、これが無いと
//   回し始めた瞬間に「即検出→即停止」となり1ピッチ回らない。
//
//   必要な条件:  磁石が検出範囲を抜ける時間 < DEADBAND_MS < 1回転の時間
const uint32_t DEADBAND_MS = REV_MS / 3;

// TIMEOUT_MS: この時間内に検出できなければ諦める。
//
//   必要な条件:  1回転の時間 < TIMEOUT_MS < 1回転の時間 × 2
//
//   上限が重要。リードスイッチが反応しなかった場合、モーターはこの時間
//   ずっと回り続けるため、長く取るとその分だけ餌が余計に出る。
//   SPEC §5 の初期値は10秒だが、仮に1回転0.5秒なら20回転に相当し、
//   1日分を全部排出してしまう。固定値ではなく実測値から決めること。
//
//   短すぎる側は安全。検出は時間ではなく磁石の位置で行うので、
//   途中で止まっても次の給餌が続きから回して帳尻が合う。
//   最悪でも「その回が少なめ」で済み、出し過ぎにはならない。
const uint32_t TIMEOUT_MS = REV_MS * 3 / 2;

const uint32_t LONGPRESS_MS = 1000;

// BTN_STABLE_MS: ボタンもこの時間ずっと LOW が続いた時だけ押下とみなす。
//   P0 も P2 と同じくモーターのブラシノイズを拾う。給餌の直後に
//   誤って「押された」と判定されると、予定外のタイミングで回り出す。
//   ハード側でも P0→5V に 5.1kΩ、P0→GND に 1µF を入れること。
const uint32_t BTN_STABLE_MS = 30;

// HOMING_MAX_MS: 原点出しの上限。1回転より少し長く取る。
const uint32_t HOMING_MAX_MS = REV_MS * 3 / 2;

// REED_STABLE_MS: この時間ずっと LOW が続いた時だけ検出とみなす。
//   モーターのブラシノイズが P2 に乗ると、リードスイッチの状態と無関係に
//   瞬間的な LOW が読まれる。1回だけの読み取りでは誤検出する。
//
//   これはあくまで保険で、本命はハード側の対策（P2→5V のプルアップ
//   5.1kΩ と、P2→GND の 1µF）。SPEC §6 を参照。
//   RC時定数 5.1ms に対して余裕を取った値。磁石の検出時間は数百msなので十分短い。
const uint32_t REED_STABLE_MS = 30;

// ---- 状態 ------------------------------------------------------------------
bool     homed     = false;  // 電源投入後に一度でも原点出しをしたか
bool     running   = false;  // false = 待機中, true = スケジュール進行中
uint32_t startedAt = 0;      // 起点（ボタンを押した時刻）
uint8_t  fedCount  = 0;      // 今日すでに出した回数

bool     btnDown     = false;
uint32_t btnDownAt   = 0;
bool     longHandled = false;

void setup() {
  // 電源投入直後にモーターが回らないよう、出力を LOW にしてから出力に切り替える。
  // ハードウェア側のゲートプルダウン10kΩと合わせて二重に担保する。
  digitalWrite(PIN_MOTOR, LOW);
  pinMode(PIN_MOTOR, OUTPUT);

  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);

  // 外付けで5.1kΩのプルアップを入れてあるが、内蔵も併用して構わない。
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_REED, INPUT_PULLUP);

  // 停電・再起動後は待機状態。人がボタンを押すまで一切動かさない。
  // 「残り回数を推定して出す」といった復旧動作は、誤爆で1日分を
  // まとめて排出する事故につながるため意図的に実装しない（SPEC §5）。
}

// REED_STABLE_MS の間ずっと LOW が続いたら true。
// 途中で一度でも HIGH に戻ればノイズとみなして false。
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

// ボタンが BTN_STABLE_MS のあいだ押され続けていたら true。
bool buttonPressed() {
  if (digitalRead(PIN_BUTTON) == HIGH) {
    return false;
  }
  const uint32_t s = millis();
  while (millis() - s < BTN_STABLE_MS) {
    if (digitalRead(PIN_BUTTON) == HIGH) {
      return false;
    }
  }
  return true;
}

// スクリューを1回転させる。成功したら true。
// 回転中は他にやることが無いのでブロッキングで書く。millis() は動き続ける。
bool rotateOnce() {
  digitalWrite(PIN_LED, HIGH);
  digitalWrite(PIN_MOTOR, HIGH);
  const uint32_t t0 = millis();

  // 1. 不感帯: 磁石が現在位置から抜けるまで検出しない
  while (millis() - t0 < DEADBAND_MS) {
    // 待つだけ
  }

  // 2. 磁石の検出を待つ
  while (!reedDetected()) {
    if (millis() - t0 >= TIMEOUT_MS) {
      digitalWrite(PIN_MOTOR, LOW);
      digitalWrite(PIN_LED, LOW);
      return false;  // 詰まりなどで1回転できなかった。この回は諦める
    }
  }

  digitalWrite(PIN_MOTOR, LOW);
  digitalWrite(PIN_LED, LOW);
  return true;
}

// 原点出し。リードスイッチが反応する位置までスクリューを送る。
// 既に反応している位置なら動かさない。
void homing() {
  if (reedDetected()) {
    return;
  }

  digitalWrite(PIN_LED, HIGH);
  digitalWrite(PIN_MOTOR, HIGH);
  const uint32_t t0 = millis();

  while (!reedDetected()) {
    if (millis() - t0 >= HOMING_MAX_MS) {
      break;
    }
  }

  digitalWrite(PIN_MOTOR, LOW);
  digitalWrite(PIN_LED, LOW);
}

// i 回目（0始まり）の給餌が起点から何ミリ秒後かを返す。
// 最大でも 900分 = 54,000,000ms なので uint32_t に収まる。
uint32_t scheduledAt(uint8_t i) {
  return FIRST_DELAY_MS + (uint32_t)i * INTERVAL_MS;
}

void startDay() {
  startedAt = millis();
  fedCount  = 0;
  running   = true;
}

void loop() {
  const uint32_t now = millis();

  // ---- ボタン ----
  // 受け付けるのは長押しのみ。短押しには機能を割り当てていない。
  const bool pressed = buttonPressed();

  if (pressed && !btnDown) {
    btnDown     = true;
    btnDownAt   = now;
    longHandled = false;
  } else if (pressed && btnDown) {
    if (!longHandled && now - btnDownAt >= LONGPRESS_MS) {
      longHandled = true;

      if (!homed) {
        // 電源投入後の1回目。原点を決めるだけで、予定は始めない。
        homing();
        homed = true;
      } else if (!running) {
        // 2回目以降。ここからが1日分の予定。
        startDay();
      }
      // 進行中の長押しは無視する。誤操作で起点がリセットされ、
      // 1日に FEED_COUNT 回を超えて出てしまうのを防ぐため。

      // 処理が終わるまでボタンが押しっぱなしのことがある。
      // 離されるまで待ってから次の判定に進む。
      while (digitalRead(PIN_BUTTON) == LOW) {
      }
      btnDown = false;
    }
  } else if (!pressed && btnDown) {
    btnDown = false;
  }

  // ---- スケジュール ----
  if (running && fedCount < FEED_COUNT) {
    if (now - startedAt >= scheduledAt(fedCount)) {
      rotateOnce();  // 失敗しても回数は進める。次回に持ち越さない
      fedCount++;
      if (fedCount >= FEED_COUNT) {
        running = false;  // 今日の分は終わり。次の装填を待つ
      }
    }
  }
}
