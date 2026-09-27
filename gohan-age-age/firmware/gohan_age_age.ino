// ご飯アゲアゲ君 ファームウェア
// Digispark / ATtiny85
// 仕様書: ../SPEC.md §5
//
// 動作の概要:
//   電源投入 → 待機状態（何もしない）
//   ボタン短押し → その時刻を起点として9回の給餌スケジュールを開始
//   ボタン長押し → その場で1回転だけ実行（装填時の位置合わせ用）

// ---- ピン割り当て ----------------------------------------------------------
// SPEC §5 では P0=MOSFET / P1=ボタン だが、ここでは入れ替えている。
//
// 理由: Digispark のオンボードLEDが P1 に載っている基板（Model A）では、
// LED の順方向電圧 約1.9V が内蔵プルアップ（20〜50kΩ）を負けて、
// ボタンを押していなくても P1 が常に LOW と読まれる。
// VIH は 0.6×VCC = 3.0V なので 1.9V では HIGH にならない。
//
// P0 と入れ替えると、ボタンは素のピンで安定して読め、
// LED は給餌中に点灯する動作表示として使える（副産物）。
//
// LED が P0 に載っている基板（Model B）なら SPEC 通りでも動く。
// その場合は下の2行を入れ替えること。
const uint8_t PIN_MOTOR  = 1;  // MOSFETゲート出力
const uint8_t PIN_BUTTON = 0;  // 装填完了ボタン（内蔵プルアップ、押下でLOW）
const uint8_t PIN_REED   = 2;  // リードスイッチ（内蔵プルアップ、磁石検出でLOW）

// ---- 給餌スケジュール ------------------------------------------------------
// 起点+60分に1回目、以降105分間隔、9回目は起点+15時間。
// SPEC §8 の通り、この時間割は運用しながら調整する。
const uint8_t  FEED_COUNT      = 9;
const uint32_t FIRST_DELAY_MIN = 60;
const uint32_t INTERVAL_MIN    = 105;

// ベンチテスト用。1にすると「分」を「秒」として扱うので、
// 15時間のスケジュールが15分で一巡する。
// 実運用の書き込み時は必ず 0 に戻すこと。
#define TEST_MODE 0

#if TEST_MODE
const uint32_t MINUTE_MS = 1000UL;
#else
const uint32_t MINUTE_MS = 60000UL;
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
const uint32_t DEBOUNCE_MS  = 30;

// REED_STABLE_MS: この時間ずっと LOW が続いた時だけ検出とみなす。
//   モーターのブラシノイズが P2 に乗ると、リードスイッチの状態と無関係に
//   瞬間的な LOW が読まれる。1回だけの読み取りでは誤検出する。
//
//   これはあくまで保険で、本命はハード側の対策（P2→5V のプルアップ
//   5.1kΩ と、P2→GND の 1µF）。SPEC §6 を参照。
//   RC時定数 5.1ms に対して余裕を取った値。磁石の検出時間は数百msなので十分短い。
const uint32_t REED_STABLE_MS = 30;

// ---- 状態 ------------------------------------------------------------------
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

// スクリューを1回転させる。成功したら true。
// 回転中は他にやることが無いのでブロッキングで書く。millis() は動き続ける。
bool rotateOnce() {
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
      return false;  // 詰まりなどで1回転できなかった。この回は諦める
    }
  }

  digitalWrite(PIN_MOTOR, LOW);
  return true;
}

// i 回目（0始まり）の給餌が起点から何ミリ秒後かを返す。
// 最大でも 900分 = 54,000,000ms なので uint32_t に収まる。
uint32_t scheduledAt(uint8_t i) {
  return (FIRST_DELAY_MIN + (uint32_t)i * INTERVAL_MIN) * MINUTE_MS;
}

void startDay() {
  startedAt = millis();
  fedCount  = 0;
  running   = true;
}

void loop() {
  const uint32_t now = millis();

  // ---- ボタン ----
  const bool pressed = (digitalRead(PIN_BUTTON) == LOW);

  if (pressed && !btnDown) {
    btnDown     = true;
    btnDownAt   = now;
    longHandled = false;
  } else if (pressed && btnDown) {
    // 長押し判定は押している最中に行う（離す前に反応させる）
    if (!longHandled && now - btnDownAt >= LONGPRESS_MS) {
      longHandled = true;
      rotateOnce();  // 手動で1回転。スケジュールには影響しない
    }
  } else if (!pressed && btnDown) {
    btnDown = false;
    if (!longHandled && now - btnDownAt >= DEBOUNCE_MS) {
      // 短押し。待機中のときだけスケジュールを開始する。
      // 進行中の短押しを無視するのは、誤操作で起点がリセットされ
      // 1日に9回を超えて出てしまうのを防ぐため。
      if (!running) {
        startDay();
      }
    }
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
