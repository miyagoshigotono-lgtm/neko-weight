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
//
// シリアル出力:
//   115200bps。動作状況と、1回転の実測時間を毎回表示する。
//
//   デバッグ中は Nano の 5V ピンを 5Vラインから外し、USB から給電すること。
//   外部5VとUSBを同時に繋ぐと2つの電源が押し合う。GND は共通のままでよい。

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
const uint8_t  FEED_COUNT      = 6;
const uint32_t FIRST_DELAY_MIN = 60;
const uint32_t INTERVAL_MIN    = 168;

// ベンチテスト用。1にすると分単位の予定を無視し、
// TEST_INTERVAL_MS 間隔で FEED_COUNT 回ぶん動かす。
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
//   ここだけを実測値に書き換えれば、下の2つは自動で追従する。
//   シリアルに毎回の実測値が出るので、ずれていたら合わせること。
const uint32_t REV_MS = 2030;

// DEADBAND_MS: 回転開始からリードスイッチを無視する時間。
//   停止時の惰性で磁石を僅かに行き過ぎるため、これが無いと
//   回し始めた瞬間に「即検出→即停止」となり1ピッチ回らない。
//   必要な条件: 磁石が検出範囲を抜ける時間 < DEADBAND_MS < 1回転の時間
const uint32_t DEADBAND_MS = REV_MS / 3;

// TIMEOUT_MS: この時間内に検出できなければ諦める。
//   必要な条件: 1回転の時間 < TIMEOUT_MS < 1回転の時間 × 2
//
//   上限が重要。検出できなかった場合モーターはこの時間ずっと回り続けるため、
//   長く取るとその分だけ餌が余計に出る。短すぎる側は安全で、検出は時間では
//   なく磁石の位置で行うので、途中で止まっても次回が続きから回して帳尻が合う。
const uint32_t TIMEOUT_MS = REV_MS * 3 / 2;

// LONGPRESS_MS: この時間ずっと押し続けないと受け付けない。
//   猫が偶然ボタンに触れて1日分の予定が始まるのを防ぐため3秒に設定。
//   猫の肉球が3秒間一定の力で押し続けることは考えにくい。
const uint32_t LONGPRESS_MS  = 3000;
const uint32_t HOMING_MAX_MS = REV_MS * 3 / 2;

// 入力はどちらも、この時間ずっと LOW が続いた時だけ有効とみなす。
// モーターのブラシノイズによる瞬間的な誤検出を弾くための保険。
// 本命はハード側（D4 / D5 を 5.1kΩ で 5V へ、1µF で GND へ）。
const uint32_t REED_STABLE_MS = 30;
const uint32_t BTN_STABLE_MS  = 30;

// ---- 状態 ------------------------------------------------------------------
bool     homed     = false;  // 電源投入後に一度でも原点出しをしたか
bool     running   = false;  // false = 待機中, true = スケジュール進行中
uint32_t startedAt = 0;      // 起点（ボタンを押した時刻）
uint8_t  fedCount  = 0;      // 今日すでに出した回数

bool     btnDown     = false;
uint32_t btnDownAt   = 0;
bool     longHandled = false;

bool lastReed = false;       // 待機中にリードの状態変化を表示するため

void setup() {
  digitalWrite(PIN_MOTOR, LOW);
  pinMode(PIN_MOTOR, OUTPUT);

  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);

  // 外付けで5.1kΩのプルアップを入れてあるが、内蔵も併用して構わない。
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_REED, INPUT_PULLUP);

  Serial.begin(115200);
  while (!Serial && millis() < 2000) {
  }

  Serial.println();
  Serial.println(F("=== gohan-age-age ==="));
#if TEST_MODE
  Serial.print(F("MODE   : TEST  interval="));
  Serial.print(TEST_INTERVAL_MS);
  Serial.println(F("ms"));
#else
  Serial.print(F("MODE   : NORMAL  first="));
  Serial.print(FIRST_DELAY_MIN);
  Serial.print(F("min  interval="));
  Serial.print(INTERVAL_MIN);
  Serial.println(F("min"));
#endif
  Serial.print(F("FEEDS  : "));
  Serial.println(FEED_COUNT);
  Serial.print(F("REV_MS : "));
  Serial.print(REV_MS);
  Serial.print(F("  deadband="));
  Serial.print(DEADBAND_MS);
  Serial.print(F("  timeout="));
  Serial.println(TIMEOUT_MS);
  Serial.print(F("reed   : "));
  Serial.println(digitalRead(PIN_REED) == LOW ? F("DETECT") : F("--"));
  Serial.println(F("ready. long-press to home."));

  lastReed = (digitalRead(PIN_REED) == LOW);

  // 停電・再起動後は待機状態。人がボタンを押すまで一切動かさない。
  // 「残り回数を推定して出す」といった復旧動作は、誤爆で1日分を
  // まとめて排出する事故につながるため意図的に実装しない（SPEC §5）。
}

// REED_STABLE_MS の間ずっと LOW が続いたら true。
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

// BTN_STABLE_MS の間ずっと押され続けていたら true。
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
  }

  // 2. 磁石の検出を待つ
  bool ok = true;
  while (!reedDetected()) {
    if (millis() - t0 >= TIMEOUT_MS) {
      ok = false;
      break;
    }
  }

  digitalWrite(PIN_MOTOR, LOW);
  digitalWrite(PIN_LED, LOW);

  const uint32_t took = millis() - t0;
  Serial.print(ok ? F("  OK      ") : F("  TIMEOUT "));
  Serial.print(took);
  Serial.print(F("ms"));
  if (ok) {
    Serial.print(F("   (REV_MS="));
    Serial.print(REV_MS);
    Serial.print(F(")"));
  }
  Serial.println();

  return ok;
}

// 原点出し。リードスイッチが反応する位置までスクリューを送る。
// 既に反応している位置なら動かさない。
void homing() {
  if (reedDetected()) {
    Serial.println(F("[home] already at origin"));
    return;
  }

  Serial.println(F("[home] start"));
  digitalWrite(PIN_LED, HIGH);
  digitalWrite(PIN_MOTOR, HIGH);
  const uint32_t t0 = millis();

  bool ok = true;
  while (!reedDetected()) {
    if (millis() - t0 >= HOMING_MAX_MS) {
      ok = false;
      break;
    }
  }

  digitalWrite(PIN_MOTOR, LOW);
  digitalWrite(PIN_LED, LOW);

  Serial.print(ok ? F("[home] done ") : F("[home] TIMEOUT "));
  Serial.print(millis() - t0);
  Serial.println(F("ms"));
}

// i 回目（0始まり）の給餌が起点から何ミリ秒後かを返す。
uint32_t scheduledAt(uint8_t i) {
  return FIRST_DELAY_MS + (uint32_t)i * INTERVAL_MS;
}

void startDay() {
  startedAt = millis();
  fedCount  = 0;
  running   = true;
  Serial.print(F("[sched] start. "));
  Serial.print(FEED_COUNT);
  Serial.println(F(" feeds queued."));
}

void loop() {
  const uint32_t now = millis();

  // ---- 待機中のリード状態の変化を表示 ----
  if (!running) {
    const bool r = (digitalRead(PIN_REED) == LOW);
    if (r != lastReed) {
      lastReed = r;
      Serial.print(F("[reed] "));
      Serial.println(r ? F("DETECT") : F("--"));
    }
  }

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
      Serial.println(F("[btn] long press"));

      if (!homed) {
        homing();
        homed = true;
        Serial.println(F("ready. long-press to start schedule."));
      } else if (!running) {
        startDay();
      } else {
        Serial.println(F("[btn] ignored (schedule running)"));
      }

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
      Serial.print(F("[feed "));
      Serial.print(fedCount + 1);
      Serial.print(F("/"));
      Serial.print(FEED_COUNT);
      Serial.println(F("]"));

      rotateOnce();  // 失敗しても回数は進める。次回に持ち越さない
      fedCount++;

      if (fedCount >= FEED_COUNT) {
        running = false;  // 今日の分は終わり。次の装填を待つ
        Serial.println(F("[sched] done. long-press to start again."));
        lastReed = (digitalRead(PIN_REED) == LOW);
      }
    }
  }
}
