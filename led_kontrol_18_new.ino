#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

#define LED_PIN 21
#define LED_COUNT 18
#define LEDS_PER_GROUP 9
#define MAX_BRIGHTNESS 200
#define BLE_NAME "LED KONTROL"
#define SERVICE_UUID "f15e43a0-7a7f-4ff8-9a9a-822277011000"
#define WRITE_UUID "f15e43a0-7a7f-4ff8-9a9a-822277011001"

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

struct Group {
  uint8_t r, g, b, brightness;
  bool enabled;
  uint8_t mode, speed;
};
Group groups[2] = {{255, 48, 62, 80, false, 0, 5},
                   {39, 113, 255, 80, false, 0, 5}};
uint32_t lastFrame = 0;
uint32_t frameNumber = 0;

uint32_t scaled(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness) {
  return strip.Color((uint16_t)r * brightness / 255,
                     (uint16_t)g * brightness / 255,
                     (uint16_t)b * brightness / 255);
}
uint32_t wheelColor(uint8_t pos, uint8_t br) {
  pos = 255 - pos;
  if (pos < 85) return scaled(255 - pos * 3, 0, pos * 3, br);
  if (pos < 170) { pos -= 85; return scaled(0, pos * 3, 255 - pos * 3, br); }
  pos -= 170;
  return scaled(pos * 3, 255 - pos * 3, 0, br);
}
void renderLeds() {
  for (int i = 0; i < LED_COUNT; ++i) {
    Group &g = groups[i / LEDS_PER_GROUP];
    int p = i % LEDS_PER_GROUP;
    uint8_t br = min((int)g.brightness, MAX_BRIGHTNESS);
    if (!g.enabled || br == 0) { strip.setPixelColor(i, 0); continue; }
    uint32_t color = 0;
    uint32_t phase = frameNumber * g.speed;
    switch (g.mode) {
      case 1: // rainbow
        color = wheelColor((uint8_t)(p * 256 / LEDS_PER_GROUP + phase / 2), br); break;
      case 2: { // breathe
        uint8_t x = (uint8_t)(phase / 3);
        uint8_t triangle = x < 128 ? x * 2 : (255 - x) * 2;
        color = scaled(g.r, g.g, g.b, (uint16_t)br * (20 + (uint16_t)triangle * 235 / 255) / 255);
        break;
      }
      case 3: { // chase
        uint8_t head = (phase / 9) % LEDS_PER_GROUP;
        uint8_t distance = (p + LEDS_PER_GROUP - head) % LEDS_PER_GROUP;
        uint8_t intensity = distance == 0 ? br : (distance == 1 ? br / 3 : 0);
        color = scaled(g.r, g.g, g.b, intensity);
        break;
      }
      case 4: // color cycle
        color = wheelColor((uint8_t)(phase / 3), br); break;
      case 5: { // sparkles (deterministic pseudorandom)
        uint32_t t = frameNumber / (uint32_t)max(1, 12 - (int)g.speed);
        uint32_t z = (t * 747796405UL + i * 2891336453UL + 12345UL);
        z = (z ^ (z >> 16)) * 2246822519UL;
        uint8_t sparkle = ((z >> 24) % 10) < 2 ? br : br / 18;
        color = scaled(g.r, g.g, g.b, sparkle); break;
      }
      case 6: { // fill then empty
        uint8_t stage = (phase / 10) % (LEDS_PER_GROUP * 2);
        bool lit = stage < LEDS_PER_GROUP ? p <= stage : p > stage - LEDS_PER_GROUP;
        color = lit ? scaled(g.r, g.g, g.b, br) : 0; break;
      }
      default: color = scaled(g.r, g.g, g.b, br);
    }
    strip.setPixelColor(i, color);
  }
  strip.show();
}
void parseCommand(String cmd) {
  cmd.trim();
  int i, r, g, b, brightness, enabled, mode, speed;
  if (sscanf(cmd.c_str(), "G,%d,%d,%d,%d,%d,%d", &i, &r, &g, &b, &brightness, &enabled) == 6) {
    if (i < 0 || i > 1 || r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255 ||
        brightness < 0 || brightness > MAX_BRIGHTNESS || enabled < 0 || enabled > 1) return;
    groups[i].r = r; groups[i].g = g; groups[i].b = b;
    groups[i].brightness = brightness; groups[i].enabled = enabled;
    groups[i].mode = 0;
  } else if (sscanf(cmd.c_str(), "E,%d,%d,%d", &i, &mode, &speed) == 3) {
    if (i < 0 || i > 1 || mode < 0 || mode > 6 || speed < 1 || speed > 10) return;
    groups[i].mode = mode; groups[i].speed = speed;
  } else if (sscanf(cmd.c_str(), "B,%d,%d", &i, &brightness) == 2) {
    if (i < 0 || i > 1 || brightness < 0 || brightness > MAX_BRIGHTNESS) return;
    groups[i].brightness = brightness;
  } else if (sscanf(cmd.c_str(), "P,%d,%d", &i, &enabled) == 2) {
    if (i < 0 || i > 1 || enabled < 0 || enabled > 1) return;
    groups[i].enabled = enabled;
  } else if (cmd == "OFF") {
    groups[0].enabled = false; groups[1].enabled = false;
  } else {
    Serial.println("Gecersiz komut: " + cmd); return;
  }
  renderLeds();
}
class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer *server) override { Serial.println("BLE baglandi"); }
  void onDisconnect(BLEServer *server) override {
    BLEDevice::startAdvertising();
    Serial.println("BLE ayrildi; reklam yeniden basladi");
  }
};
class WriteCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *characteristic) override {
    auto value = characteristic->getValue();
    if (value.length() > 0) parseCommand(String(value.c_str()));
  }
};
void setup() {
  Serial.begin(115200);
  strip.begin(); strip.clear(); strip.show();
  BLEDevice::init(BLE_NAME);
  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());
  BLEService *service = server->createService(SERVICE_UUID);
  BLECharacteristic *writeCharacteristic = service->createCharacteristic(
    WRITE_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  writeCharacteristic->setCallbacks(new WriteCallbacks());
  service->start();
  BLEAdvertising *advertising = BLEDevice::getAdvertising();
  advertising->addServiceUUID(SERVICE_UUID);
  advertising->start();
  Serial.println("LED KONTROL BLE hazir - 18 LED");
}
void loop() {
  if (millis() - lastFrame >= 35) {
    lastFrame = millis();
    frameNumber++;
    renderLeds();
  }
  delay(1);
}
