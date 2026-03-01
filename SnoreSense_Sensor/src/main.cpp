#include <Arduino.h>
#include <driver/i2s.h>

// --- Hardware Pins ---
#define I2S_WS 5
#define I2S_SD 4
#define I2S_SCK 3
#define LED_PIN 20  // D7/GPIO 20 for your specific board layout

// --- Detection Settings ---
#define SNORE_THRESHOLD 150000000 
#define BOUT_GAP_MS 120000       
#define MAX_HISTORY 50           

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

void setup() {
  Serial.begin(115200);
  pinMode(LED_PIN, OUTPUT);
  
  // Turn LED ON immediately to indicate the machine is active
  digitalWrite(LED_PIN, HIGH); 
  
  delay(3000); 
  setup_i2s();
  Serial.println("--- SnoreSense: Power Indicator Active ---");
}

void loop() {
  int32_t maxSample = -2147483648;
  int32_t minSample = 2147483647;
  
  unsigned long start = millis();
  while(millis() - start < 200) {
    int32_t sample = 0;
    size_t bytes_read;
    i2s_read(I2S_NUM_0, &sample, sizeof(sample), &bytes_read, 10);
    if(bytes_read > 0 && sample != 0) {
      if(sample > maxSample) maxSample = sample;
      if(sample < minSample) minSample = sample;
    }
  }

  int32_t peakToPeak = (maxSample > minSample) ? (maxSample - minSample) : 0;

  // Logic for counting snores (LED remains steady regardless of detection)
  int momentaryStatus = 0; 
  if (peakToPeak > SNORE_THRESHOLD) {
    totalSnoreCount++;
    if (lastSnoreTime == 0 || (millis() - lastSnoreTime) > BOUT_GAP_MS) {
      boutCount++;
    }
    lastSnoreTime = millis();
  }

  if (totalSnoreCount > 50 || boutCount > 5) momentaryStatus = 2;      
  else if (totalSnoreCount > 10 || boutCount > 1) momentaryStatus = 1; 
  else momentaryStatus = 0;                                          

  history[historyIndex] = momentaryStatus;
  historyIndex = (historyIndex + 1) % MAX_HISTORY;
  if (historyIndex == 0) historyFull = true;

  int countGood = 0, countFair = 0, countPoor = 0;
  int totalSamples = historyFull ? MAX_HISTORY : historyIndex;
  for (int i = 0; i < totalSamples; i++) {
    if (history[i] == 2) countPoor++;
    else if (history[i] == 1) countFair++;
    else countGood++;
  }

  float pPoor = (float)countPoor / totalSamples;
  float pFair = (float)countFair / totalSamples;
  float pGood = (float)countGood / totalSamples;

  String finalGrade = "CALIBRATING";
  if (pPoor > 0.50) finalGrade = "POOR";
  else if (pFair > 0.60) finalGrade = "FAIR";
  else if (pGood > 0.70) finalGrade = "GOOD";

  Serial.printf("Peak: %d | Snores: %d | Grade: %s\n", peakToPeak, totalSnoreCount, finalGrade.c_str());
  delay(800); 
}