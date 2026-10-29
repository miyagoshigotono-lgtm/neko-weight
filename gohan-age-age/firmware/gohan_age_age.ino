// ご飯アゲアゲ君 ファームウェア
// Arduino Nano / ATmega328P
// 仕様書: ../SPEC.md §5
//
// ベンチテストで時間を縮めたい時は UNIT_MS と SCHEDULE を書き換える。
// 条件コンパイルは使わない。
//
// 動作:
//   電源投入       → 待機。何もしない
//   1回目の3秒長押し → 原点出し。リードスイッチが反応する位置まで送って止まる
//   2回目の3秒長押し → その場で1回目を給餌し、そこから SCHEDULE に従って
//                      残りを出す。すぐ回ることが「タイマーが始まった」合図になる
//   進行中の長押し   → 無視

// ---- ピン ------------------------------------------------------------------
const uint8_t PIN_MOTOR  = 3;   // MOSFETゲート出力
const uint8_t PIN_BUTTON = 4;   // ボタン。押下で GND
const uint8_t PIN_REED   = 5;   // リードスイッチ。磁石検出で GND
const uint8_t PIN_LED    = 13;  // オンボードLED。動作中に点灯

// ---- スケジュール ----------------------------------------------------------
// 部屋数と一致させること。実機のスクリューは6部屋。
const uint8_t FEED_COUNT = 6;

// 1回目の給餌で回す回転数。2回目以降は常に1回転。
const uint8_t FIRST_FEED_REVS = 2;

// ボタンを押してから何分後に出すか。要素数は FEED_COUNT と一致させること。
// 先頭が 0 なので、ボタンを押した瞬間に1回目が出る。
//
// 毎朝7時ちょうどにボタンを押す運用。その場合の時刻:
//    0分 →  7:00  （ボタンと同時）
//   30分 →  7:30   ← 朝に大半を食べるので早い時間に寄せている
//  120分 →  9:00   ← 10時台はもう寝ているので、そこでは出さない
//  300分 → 12:00
//  480分 → 15:00   ← 9時からと12時からの各3時間が昼寝の時間
//  600分 → 17:00   ← 最後。16時だとまだ寝ている
//
// 朝を多めにしたい場合は時刻ではなく装填で調整する。
// 先に出る部屋に多く入れればよい（SPEC §2）。
//
// 時刻は固定ではない。RTCを持たないので全てボタンを押した時刻からの
// 相対になり、装填が遅れればその分だけ全体がずれる。
const uint16_t SCHEDULE[FEED_COUNT] = { 0, 30, 120, 300, 480, 600 };

// SCHEDULE の単位。本番は 60000UL（分）。
// ベンチテストでは 1000UL にすると分が秒になり、9分で一巡する。
// もっと短くしたい時は SCHEDULE を { 0, 5, 10, 15, 20, 25 } にする。
const uint32_t UNIT_MS = 60000UL;

// ---- モーターの速度 --------------------------------------------------------
// FA-130 を 5V で回すと高い回転音が出て猫が怖がる。PWM で平均電圧を下げる。
// FA-130 の定格は 1.5〜3V なので、下げることは定格内に戻すことでもある。
//
// D3 は OC2B（Timer2）。分周なしの Fast PWM で 16MHz / 256 = 62.5kHz。
// 猫の可聴上限（約64kHz）の際どい所だが、感度は40kHz以上で急落するため
// 実用上は聞こえない。Nano 標準の490Hz/980Hz だと「ブーン」が可聴域に出る。
//
// Timer2 は millis() に影響しない（millis は Timer0）。tone() と競合するが
// 本スケッチでは使っていない。
//
// 減速比が約1:360あり、必要トルクに対して数倍の余裕がある。
// 遅くしても止まらない。ウォームのセルフロックも効いたままになる。
const uint8_t MOTOR_DUTY = 20;    // 0〜255。20 ≒ 8% ≒ 平均0.4V

// 低いデューティでは起動しきらないことがあるため、最初だけ全開で蹴る。
const uint32_t KICK_MS = 150;

// ---- 動作パラメータ --------------------------------------------------------
// 1回転の実測時間。ここを変えれば下の2つが追従する。
const uint32_t REV_MS = 3456;

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

// スケジュール進行中であることを示す点滅。
// 給餌が来ないときに「動いているのか、止まっているのか」を
// シリアル無しで判別するための表示。消えていたら電源断かリセット。
const uint32_t BEAT_PERIOD_MS = 3000;  // 3秒ごとに
const uint32_t BEAT_ON_MS     = 60;    // 60ms だけ光る
uint32_t beatAt = 0;

// モーターを MOTOR_DUTY で回し始める。
// 起動だけ全開で蹴ってから落とす。
void motorOn() {
  OCR2B = 255;
  TCCR2A |= _BV(COM2B1);   // OC2B をピンに接続
  delay(KICK_MS);
  OCR2B = MOTOR_DUTY;
}

void motorOff() {
  TCCR2A &= ~_BV(COM2B1);  // OC2B を切り離す
  digitalWrite(PIN_MOTOR, LOW);
}

void setup() {
  // モーターを確実に止めてから出力に切り替える。
  // ハード側の10kΩプルダウンと合わせて二重に担保する。
  digitalWrite(PIN_MOTOR, LOW);
  pinMode(PIN_MOTOR, OUTPUT);

  // Timer2 を分周なしの Fast PWM に設定（62.5kHz）。
  // この時点では OC2B を接続しないので、モーターは止まったまま。
  TCCR2A = _BV(WGM21) | _BV(WGM20);
  TCCR2B = _BV(CS20);
  OCR2B  = 0;

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
  uint32_t t0 = millis();
  motorOn();

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

  motorOff();
  digitalWrite(PIN_LED, LOW);
}

// 原点出し。磁石を検出する位置まで送る。既にそこなら動かさない。
void homing() {
  if (reedDetected()) {
    return;
  }

  digitalWrite(PIN_LED, HIGH);
  uint32_t t0 = millis();
  motorOn();

  while (!reedDetected()) {
    if (millis() - t0 >= HOMING_MAX_MS) {
      break;
    }
  }

  motorOff();
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
        beatAt    = now;

        // 1回目だけ多めに回して、境目に残った餌を出し切る。
        for (uint8_t i = 0; i < FIRST_FEED_REVS; i++) {
          rotateOnce();
        }

        fedCount = 1;   // 回転数ではなく「給餌の回数」なので 1
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

  // ---- 進行中の表示 ----
  // 3秒ごとに短く光る。消えていたら電源断かリセット。
  if (running) {
    uint32_t phase = now - beatAt;
    if (phase >= BEAT_PERIOD_MS) {
      beatAt = now;
      digitalWrite(PIN_LED, HIGH);
    } else if (phase >= BEAT_ON_MS) {
      digitalWrite(PIN_LED, LOW);
    }
  }

  // ---- スケジュール ----
  if (running && fedCount < FEED_COUNT) {
    uint32_t due = (uint32_t)SCHEDULE[fedCount] * UNIT_MS;
    if (now - startedAt >= due) {
      rotateOnce();
      fedCount++;
      if (fedCount >= FEED_COUNT) {
        running = false;
        digitalWrite(PIN_LED, LOW);
      }
    }
  }
}
