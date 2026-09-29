#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <SimpleKalmanFilter.h>

// ================= KONFIGURASI =================
const char *ssid        = "";
const char *password    = "";
const char *mqtt_server = "";
const int   mqtt_port   = 1883;
const char *mqtt_topic  = "ESP32/data";

#define PIN_LED    13
#define PIN_WDT    14
#define SAMPLE_MS  5          // periode sampling PCF (pulsa AC ~10 ms)

const uint8_t PCF_ADDR[3] = {0x20, 0x21, 0x22};

// Hysteresis pada estimasi duty cycle (0.0 - 1.0), sesuaikan lewat serial
const float TH_ON  = 0.15f;
const float TH_OFF = 0.05f;

// Kalman: 3 chip x 8 channel
#define KF_INIT SimpleKalmanFilter(1, 1, 0.001)
#define KF_ROW  { KF_INIT, KF_INIT, KF_INIT, KF_INIT, KF_INIT, KF_INIT, KF_INIT, KF_INIT }
SimpleKalmanFilter kf[3][8] = { KF_ROW, KF_ROW, KF_ROW };

// Channel yang dipublish (chip 0=pcf1, 1=pcf2, 2=pcf3) -- sama dengan mapping kode asli
struct Ch { const char *name; uint8_t chip; uint8_t pin; };
const Ch CH[] = {
  {"R1", 1, 7}, {"S1", 1, 6}, {"T1", 1, 5},
  {"R2", 1, 4}, {"S2", 2, 6}, {"T2", 2, 7},
  {"R3", 2, 5}, {"S3", 2, 4}, {"T3", 2, 0}
};
const int CH_COUNT = sizeof(CH) / sizeof(CH[0]);

// ================= DATA BERSAMA =================
volatile uint8_t  gState[3]   = {0, 0, 0};   // status ON/OFF akhir per chip (1 bit per pin)
float             gEst[3][8];                // estimasi Kalman (untuk debug)
volatile uint32_t gI2cErr     = 0;
volatile uint32_t sampleBeat  = 0;           // penanda task sampling hidup
volatile uint32_t netBeat     = 0;           // penanda task network hidup
volatile uint32_t interval    = 1000;        // interval publish (ms)
volatile bool     intervalDirty = false;

WiFiClient   espClient;
PubSubClient mqttClient(espClient);
Preferences  preferences;

// ================= I2C =================
static bool pcfWrite(uint8_t addr, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

static bool pcfRead(uint8_t addr, uint8_t &val) {
  if (Wire.requestFrom(addr, (uint8_t)1) != 1) return false;
  val = Wire.read();
  return true;
}

static inline int getCh(uint8_t chip, uint8_t pin) {
  return (gState[chip] >> pin) & 1;
}

// ================= TASK SAMPLING =================
void taskSampling(void *pv) {
  TickType_t last = xTaskGetTickCount();
  uint8_t bits[3] = {0, 0, 0};

  for (;;) {
    for (int c = 0; c < 3; c++) {
      uint8_t raw;
      if (pcfRead(PCF_ADDR[c], raw)) {
        for (int p = 0; p < 8; p++) {
          int active = !((raw >> p) & 1);            // aktif-low seperti kode asli
          float est = kf[c][p].updateEstimate((float)active);
          gEst[c][p] = est;

          if (est > TH_ON)       bits[c] |=  (1 << p);
          else if (est < TH_OFF) bits[c] &= ~(1 << p);
        }
        gState[c] = bits[c];
      } else {
        gI2cErr++;                                   // gagal baca: pertahankan status terakhir
      }
    }
    sampleBeat++;
    vTaskDelayUntil(&last, pdMS_TO_TICKS(SAMPLE_MS));
  }
}

// ================= MQTT =================
void mqttCallback(char *topic, byte *payload, unsigned int len) {
  char msg[16];
  unsigned int n = (len < sizeof(msg) - 1) ? len : sizeof(msg) - 1;
  memcpy(msg, payload, n);
  msg[n] = 0;

  if (strcmp(topic, "event/interval") == 0) {
    uint32_t v = strtoul(msg, NULL, 10);
    if (v < 100) v = 100;                            // minimum 100 ms
    interval = v;
    intervalDirty = true;                            // simpan ke flash di taskNetwork
    Serial.printf("Interval publish: %lu ms\n", (unsigned long)v);
  }
}

static bool mqttTryConnect() {
  char clientId[24];
  snprintf(clientId, sizeof(clientId), "ESP32Client-%04X", (uint16_t)esp_random());
  Serial.print("MQTT connecting... ");
  if (mqttClient.connect(clientId)) {
    Serial.println("connected");
    mqttClient.subscribe("event/interval");
    mqttClient.subscribe("esp32/output");
    return true;
  }
  Serial.printf("failed, rc=%d\n", mqttClient.state());
  return false;
}

static void publishData() {
  char buf[192];
  size_t n = 0;
  buf[n++] = '{';
  for (int i = 0; i < CH_COUNT; i++) {
    n += snprintf(buf + n, sizeof(buf) - n, "%s\"%s\":\"%d\"",
                  i ? "," : "", CH[i].name, getCh(CH[i].chip, CH[i].pin));
  }
  snprintf(buf + n, sizeof(buf) - n, "}");

  if (mqttClient.publish(mqtt_topic, buf)) {
    Serial.println(buf);
  } else {
    Serial.println("Publish gagal");
  }
}

// ================= TASK NETWORK =================
// PubSubClient tidak thread-safe: connect, loop, callback, dan publish
// semuanya dijalankan di task ini saja.
void taskNetwork(void *pv) {
  uint32_t lastWifiTry = 0, lastMqttTry = 0, lastPublish = 0;

  for (;;) {
    uint32_t now = millis();
    netBeat++;

    if (WiFi.status() != WL_CONNECTED) {
      if (now - lastWifiTry >= 10000) {
        lastWifiTry = now;
        Serial.println("WiFi reconnect...");
        WiFi.disconnect();
        WiFi.begin(ssid, password);
      }
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }

    if (!mqttClient.connected()) {
      if (now - lastMqttTry >= 5000) {
        lastMqttTry = now;
        mqttTryConnect();                            // blocking hanya di task ini
      }
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    mqttClient.loop();

    if (now - lastPublish >= interval) {
      lastPublish = now;
      publishData();
    }

    if (intervalDirty) {
      intervalDirty = false;
      preferences.begin("config", false);
      preferences.putULong("interval", interval);
      preferences.end();
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// ================= TASK DEBUG =================
void taskDebug(void *pv) {
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(3000));
    for (int c = 1; c < 3; c++) {
      Serial.printf("kf%d:", c + 1);
      for (int p = 0; p < 8; p++) Serial.printf("%.2f,", gEst[c][p]);
      Serial.println();
    }
    Serial.print("state:");
    for (int c = 0; c < 3; c++) {
      for (int p = 0; p < 8; p++) Serial.print(getCh(c, p));
      Serial.print(" ");
    }
    Serial.printf("| i2cErr=%lu\n", (unsigned long)gI2cErr);
  }
}

// ================= TASK HEARTBEAT =================
// Pin WDT eksternal hanya di-toggle jika sampling DAN network masih berjalan.
// Jika salah satu hang, pin berhenti toggle dan WDT eksternal me-reset board.
void taskHeartbeat(void *pv) {
  uint32_t lastS = 0, lastN = 0;
  bool s = false;

  for (;;) {
    s = !s;
    digitalWrite(PIN_LED, s);

    uint32_t bs = sampleBeat, bn = netBeat;
    if (bs != lastS && bn != lastN) digitalWrite(PIN_WDT, s);
    lastS = bs;
    lastN = bn;

    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  delay(100);

  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_WDT, OUTPUT);

  preferences.begin("config", false);
  interval = preferences.getULong("interval", 1000);
  preferences.end();

  Wire.begin();
  Wire.setClock(400000);
  for (int c = 0; c < 3; c++) {
    // Tulis 0xFF = semua pin jadi input (quasi-bidirectional)
    Serial.printf("Init pcf%d (0x%02X)... %s\n", c + 1, PCF_ADDR[c],
                  pcfWrite(PCF_ADDR[c], 0xFF) ? "OK" : "KO");
  }

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(ssid, password);

  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setSocketTimeout(3);                    // batasi blocking saat connect

  xTaskCreatePinnedToCore(taskSampling,  "sampling",  4096, NULL, 3, NULL, 1);
  xTaskCreatePinnedToCore(taskNetwork,   "network",   6144, NULL, 2, NULL, 0);
  xTaskCreatePinnedToCore(taskDebug,     "debug",     3072, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(taskHeartbeat, "heartbeat", 2048, NULL, 1, NULL, 1);
}

void loop() {
  vTaskDelete(NULL);   // semua kerja sudah di task
}
