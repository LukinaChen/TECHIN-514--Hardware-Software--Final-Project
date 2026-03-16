#include <Arduino.h>
#include <driver/i2s.h>
#include <stdint.h>
#include <WiFi.h>
#include <esp_now.h>

// --- Hardware Pins ---
#define I2S_WS 5
#define I2S_SD 4
#define I2S_SCK 3
#define LED_PIN 20  // D7/GPIO20 on Seeed XIAO ESP32C3

// --- ESP-NOW ---
uint8_t displayMAC[] = {0x58, 0x8C, 0x81, 0xAE, 0x84, 0x18};

typedef struct {
  int snoreCount;
  int boutCount;
  int score;  // 0=GOOD, 1=FAIR, 2=POOR
} SleepData;

esp_now_peer_info_t peerInfo;
int lastSentScore = -1;

void onDataSent(const uint8_t *mac, esp_now_send_status_t status) {
  Serial.printf("[ESP-NOW TX] %s\n",
    status == ESP_NOW_SEND_SUCCESS ? "SUCCESS" : "FAIL");
  
  if (status == ESP_NOW_SEND_SUCCESS) {
    for (int i = 0; i < 3; i++) {
      digitalWrite(LED_PIN, LOW);  delay(150);
      digitalWrite(LED_PIN, HIGH); delay(150);
    }
  }
}

// --- Detection Settings ---
#define SNORE_THRESHOLD 100000000LL
#define BOUT_GAP_MS 120000
#define MAX_HISTORY 50
#define SCORE_DELAY_MS 20000  // 20s before scoring

// --- Tracking Variables ---
int totalSnoreCount = 0;
int boutCount = 0;
unsigned long lastSnoreTime = 0;
int history[MAX_HISTORY];
int historyIndex = 0;
bool historyFull = false;

void setup_i2s() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = 16000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 1024
  };
  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_in_num = I2S_SD
  };
  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
}

void setup_espnow() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  if (esp_now_init() != ESP_OK) {
    Serial.println("[ERROR] ESP-NOW init failed!");
    return;
  }
  esp_now_register_send_cb(onDataSent);
  memcpy(peerInfo.peer_addr, displayMAC, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);
  Serial.println("[ESP-NOW] Ready");
}

int calcScore(int snores, int bouts) {
  if (snores < 5 && bouts < 2) return 0;    // GOOD
  if (snores > 15 || bouts > 3) return 2;   // POOR
  return 1;                                  // FAIR
}

void sendScore(int score) {
  SleepData data;
  data.snoreCount = totalSnoreCount;
  data.boutCount = boutCount;
  data.score = score;
  
  esp_err_t result = esp_now_send(displayMAC, (uint8_t*)&data, sizeof(data));
  Serial.printf("[TX] score=%d snore=%d bout=%d esp_err=%d\n",
    score, totalSnoreCount, boutCount, result);
}

void blink_startup_test() {
  for (int i = 0; i < 3; i++) {
    digitalWrite(LED_PIN, HIGH);
    delay(250);
    digitalWrite(LED_PIN, LOW);
    delay(250);
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);

  blink_startup_test();
  digitalWrite(LED_PIN, HIGH);

  setup_i2s();
  setup_espnow();
  Serial.println("--- SnoreSense: Power Indicator Active ---");
}

void loop() {
  int32_t maxSample = -2147483648;
  int32_t minSample = 2147483647;
  bool hasValidSample = false;

  unsigned long start = millis();
  while (millis() - start < 200) {
    int32_t sample = 0;
    size_t bytes_read;
    i2s_read(I2S_NUM_0, &sample, sizeof(sample), &bytes_read, 10);
    if (bytes_read > 0 && sample != 0) {
      hasValidSample = true;
      if (sample > maxSample) maxSample = sample;
      if (sample < minSample) minSample = sample;
    }
  }

  int64_t peakToPeak = 0;
  if (hasValidSample && maxSample > minSample) {
    peakToPeak = (int64_t)maxSample - (int64_t)minSample;
  }

  bool detected = peakToPeak > SNORE_THRESHOLD;
  bool newBout = false;
  if (detected) {
    totalSnoreCount++;
    if (lastSnoreTime == 0 || (millis() - lastSnoreTime) > BOUT_GAP_MS) {
      boutCount++;
      newBout = true;
    }
    lastSnoreTime = millis();
  }

  history[historyIndex] = newBout ? 2 : (detected ? 1 : 0);
  historyIndex = (historyIndex + 1) % MAX_HISTORY;
  if (historyIndex == 0) historyFull = true;

  int recentDetections = 0;
  int recentBouts = 0;
  int totalSamples = historyFull ? MAX_HISTORY : historyIndex;
  for (int i = 0; i < totalSamples; i++) {
    if (history[i] >= 1) recentDetections++;
    if (history[i] == 2) recentBouts++;
  }

  String finalGrade = "CALIBRATING";
  if (totalSamples >= 10) {
    if (recentDetections >= 12 || recentBouts >= 3) finalGrade = "POOR";
    else if (recentDetections >= 5 || recentBouts >= 2) finalGrade = "FAIR";
    else finalGrade = "GOOD";
  }

  // change to send each sec(Only send after 1 minute and when score changes 
  if (millis() >= SCORE_DELAY_MS) {
    int currentScore = calcScore(totalSnoreCount, boutCount);
    sendScore(currentScore);
    lastSentScore = currentScore;
  }

  digitalWrite(LED_PIN, HIGH);
  Serial.printf(
    "Peak: %lld | Detect: %s | Snores: %d | Bouts: %d | RecentDetections: %d | RecentBouts: %d | Grade: %s\n",
    peakToPeak,
    detected ? "DETECTED" : "NO",
    totalSnoreCount,
    boutCount,
    recentDetections,
    recentBouts,
    finalGrade.c_str()
  );
  delay(800);
}