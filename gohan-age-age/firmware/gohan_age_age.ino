// ご飯アゲアゲ君 ファームウェア
// Arduino Nano / ATmega328P
// 仕様書: ../SPEC.md §5
//
// ベンチテストで時間を縮めたい時は INTERVAL_MS だけを書き換える。
// 条件コンパイルは使わない。
//
// 動作:
//   電源投入       → 待機。何もしない
//   1回目の3秒長押し → 原点出し。リードスイッチが反応する位置まで送って止まる
//   2回目の3秒長押し → その場で1回目を給餌し、そこからインターバルを開始。
//                      すぐ回ることが「タイマーが始まった」合図になる
//   進行中の長押し   → 無視

// ---- ピン ------------------------------------------------------------------
const uint8_t PIN_MOTOR  = 3;   // MOSFETゲート出力
const uint8_t PIN_BUTTON = 4;   // ボタン。押下で GND
const uint8_t PIN_REED   = 5;   // リードスイッチ。磁石検出で GND
const uint8_t PIN_LED    = 13;  // オンボードLED。動作中に点灯

// ---- スケジュール ----------------------------------------------------------
// 部屋数と一致させること。実機のスクリューは6部屋。
const uint8_t FEED_COUNT = 6;

// ボタンを押した時点で1回目を出し、以降この間隔で残り5回。
//   180分 × 5 = 900分 = 15時間後に6回目
// ベンチテストの時は 5000UL（5秒）にする。
const uint32_t INTERVAL_MS = 10800000UL;  // 180分

// ---- 動作パラメータ --------------------------------------------------------
// 1回転の実測時間。ここを変えれば下の2つが追従する。
const uint32_t REV_MS = 2030;

// この時間で検出できなければ諦めて止める。回転開始からの合計時間。
// 長く取ると検出失敗時に餌が出過ぎるので、1回転と2回転の間に収める。
// 検出は時間ではなく磁石の位置で行うので、途中で止まっても次回が
// 続きから回して帳尻が合う。短すぎる側は安全。
const uint32_t TIMEOUT_MS = REV_MS * 3 / 2;   // 3045ms

// 原点出しの上限。
const uint32_t HOMING_MAX_MS = REV_MS * 3 / 2;

// ボタンをこの時間押し続けたら受け付ける。
// 猫が偶然触れて1日分の予定が始まるのを防ぐため長めに取る。
const uint32_t LONGPRESS_MS = 3000;

// ボタンがこの時間ずっと離されていたら「離した」と判定する。
// これより短い接触切れはチャタリングとみなし、長押しの計測を継続する。
// これが無いと、3秒の途中で一瞬でも接触が切れた時に振り出しへ戻る。
const uint32_t RELEASE_MS = 50;

// 回転開始からこの時間より前の検出は無視する。
// 1回転が約2秒なので、1.2秒より前に磁石が戻ってくることはない。
// 磁石から離れる判定（reedCleared）とは独立しているので、
// 検出範囲が広くても成立する。ノイズによる早期停止を防ぐための下限。
const uint32_t MIN_RUN_MS = REV_MS * 3 / 5;   // 1218ms

// リードスイッチの判定時間。この時間ずっと同じ状態が続いたら確定とみなす。
// モーターのブラシノイズによる瞬間的な誤検出を弾く。
// 磁石の検出時間は数百msあるので、100msでも十分短い。
const uint32_t REED_STABLE_MS = 100;

// ---- 状態 ------------------------------------------------------------------
bool     homed       = false;  // 電源投入後に原点出しを済ませたか
bool     running     = false;  // スケジュール進行中か
uint32_t startedAt   = 0;      // 起点の時刻
uint8_t  fedCount    = 0;      // 今日すでに出した回数

bool     btnDown     = false;  // ボタンを押していると判定中か
uint32_t btnDownAt   = 0;      // 押し始めた時刻
uint32_t releasedAt  = 0;      // 離れ始めた時刻。0 なら離れていない
bool     longHandled = false;  // この押下で既に処理を実行したか

void setup() {
  // モーターを確実に止めてから出力に切り替える。
  // ハード側の10kΩプルダウンと合わせて二重に担保する。
  digitalWrite(PIN_MOTOR, LOW);
  pinMode(PIN_MOTOR, OUTPUT);

  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);

  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_REED, INPUT_PULLUP);

  // 停電・再起動後は待機状態。人がボタンを押すまで一切動かさない。
  // 残り回数を推定して出すような復旧動作は、誤爆で1日分をまとめて
  // 排出する事故につながるため意図的に実装しない。
}

// リードスイッチが REED_STABLE_MS のあいだ連続で LOW なら true（磁石を検出）。
bool reedDetected() {
  if (digitalRead(PIN_REED) == HIGH) {
    return false;
  }
  uint32_t s = millis();
  while (millis() - s < REED_STABLE_MS) {
    if (digitalRead(PIN_REED) == HIGH) {
      return false;
    }
  }
  return true;
}

// リードスイッチが REED_STABLE_MS のあいだ連続で HIGH なら true（磁石から離れた）。
bool reedCleared() {
  if (digitalRead(PIN_REED) == LOW) {
    return false;
  }
  uint32_t s = millis();
  while (millis() - s < REED_STABLE_MS) {
    if (digitalRead(PIN_REED) == LOW) {
      return false;
    }
  }
  return true;
}

// スクリューを1回転させる。
// 回転中は他にやることが無いのでブロッキングで書く。millis() は動き続ける。
//
// 固定時間の不感帯は使わない。磁石の検出範囲が何度ぶんあるか分からないため、
// 時間で待つと「まだ磁石の上にいるのに不感帯が明ける」ことが起きる。
// 代わりに、まず磁石から離れたことを確認してから、次の検出を待つ。
// これなら検出範囲の広さに関係なく、必ず1回転で止まる。
void rotateOnce() {
  digitalWrite(PIN_LED, HIGH);
  digitalWrite(PIN_MOTOR, HIGH);
  uint32_t t0 = millis();

  // 1. 磁石から離れるまで
  while (!reedCleared()) {
    if (millis() - t0 >= TIMEOUT_MS) {
      break;
    }
  }

  // 2. 次に磁石を検出するまで。ただし MIN_RUN_MS より前の検出は信じない。
  while (true) {
    if (millis() - t0 >= TIMEOUT_MS) {
      break;
    }
    if (millis() - t0 >= MIN_RUN_MS && reedDetected()) {
      break;
    }
  }

  digitalWrite(PIN_MOTOR, LOW);
  digitalWrite(PIN_LED, LOW);
}

// 原点出し。磁石を検出する位置まで送る。既にそこなら動かさない。
void homing() {
  if (reedDetected()) {
    return;
  }

  digitalWrite(PIN_LED, HIGH);
  digitalWrite(PIN_MOTOR, HIGH);
  uint32_t t0 = millis();

  while (!reedDetected()) {
    if (millis() - t0 >= HOMING_MAX_MS) {
      break;
    }
  }

  digitalWrite(PIN_MOTOR, LOW);
  digitalWrite(PIN_LED, LOW);
}

void loop() {
  uint32_t now = millis();

  // ---- ボタン ----
  if (digitalRead(PIN_BUTTON) == LOW) {
    releasedAt = 0;

    if (!btnDown) {
      btnDown     = true;
      btnDownAt   = now;
      longHandled = false;
    }

    if (!longHandled && now - btnDownAt >= LONGPRESS_MS) {
      longHandled = true;

      if (!homed) {
        homing();
        homed = true;
      } else if (!running) {
        // その場で1回目を出す。すぐ回ることが
        // 「タイマーが始まった」という目に見える合図になる。
        startedAt = now;
        rotateOnce();
        fedCount = 1;
        running  = (FEED_COUNT > 1);
      }
      // 進行中なら無視する。起点がリセットされて
      // 1日に FEED_COUNT 回を超えて出るのを防ぐため。
    }
  } else {
    // 離れている。ただし RELEASE_MS より短ければチャタリングとみなす。
    if (btnDown) {
      if (releasedAt == 0) {
        releasedAt = now;
      } else if (now - releasedAt >= RELEASE_MS) {
        btnDown    = false;
        releasedAt = 0;
      }
    }
  }

  // ---- スケジュール ----
  if (running && fedCount < FEED_COUNT) {
    uint32_t due = (uint32_t)fedCount * INTERVAL_MS;
    if (now - startedAt >= due) {
      rotateOnce();
      fedCount++;
      if (fedCount >= FEED_COUNT) {
        running = false;
      }
    }
  }
}
