#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>
#include "SwitecX25.h"
#include <driver/gpio.h>

#define LED_PIN   D8
#define BTN_PIN   D9

#define MOTOR_STEPS  1500
#define POS_START    1450
#define POS_GOOD     1286
#define POS_FAIR     958
#define POS_POOR     630

SwitecX25 motor(MOTOR_STEPS, 4, 2, 5, 3);

typedef struct {
  int snoreCount;
  int boutCount;
  int score;
} SleepData;

SleepData latestData;
bool dataReceived = false;

void onDataRecv(const uint8_t *mac, const uint8_t *data, int len) {
  memcpy(&latestData, data, sizeof(latestData));
  dataReceived = true;
  Serial.printf("[ESP-NOW RX] snore=%d bout=%d score=%d\n",
    latestData.snoreCount, latestData.boutCount, latestData.score);
}

void homing() {
  Serial.println("[HOMING] starting...");
  motor.zero();
  delay(500);
  motor.setPosition(POS_START);
  motor.updateBlocking();
  Serial.println("[HOMING] done");
}

void setup() {
  Serial.begin(115200);
  delay(500);

  gpio_reset_pin(GPIO_NUM_8);
  gpio_set_direction(GPIO_NUM_8, GPIO_MODE_OUTPUT);

  pinMode(LED_PIN, OUTPUT);
 pinMode(BTN_PIN, INPUT_PULLUP);

  for (int i = 0; i < 5; i++) {
    digitalWrite(LED_PIN, HIGH); delay(200);
    digitalWrite(LED_PIN, LOW);  delay(200);
  }
  digitalWrite(LED_PIN, HIGH);

  Serial.println("=== SnoreSense Display BOOT ===");

  homing();

  WiFi.mode(WIFI_STA);
  WiFi.channel(1);
  if (esp_now_init() != ESP_OK) {
    Serial.println("[ERROR] ESP-NOW init failed!");
  } else {
    esp_now_register_recv_cb(onDataRecv);
    Serial.println("[ESP-NOW] ready - waiting for sensor...");
    Serial.print("[INFO] Display MAC: ");
    Serial.println(WiFi.macAddress());
  }
}

unsigned long btnPressStart = 0;
bool btnWasPressed = false;
#define LONG_PRESS_MS 2000

void loop() {
  if (dataReceived) {
    dataReceived = false;
    int target = POS_GOOD;
    if (latestData.score == 1) target = POS_FAIR;
    if (latestData.score == 2) target = POS_POOR;
    motor.setPosition(target);
    motor.updateBlocking();
  }

  bool btnPressed = (digitalRead(BTN_PIN) == LOW);
  if (btnPressed && !btnWasPressed) {
    btnPressStart = millis();
    btnWasPressed = true;
  }
  if (!btnPressed && btnWasPressed) {
    if (millis() - btnPressStart >= LONG_PRESS_MS) {
      homing();
      digitalWrite(LED_PIN, HIGH);
    }
    btnWasPressed = false;
  }

  delay(20);
}