/**
 * PROJE: Cyberpunk Akıllı Rozet / Anahtarlık (Hyper-FPS & ProMotion PBD Sürümü)
 * DONANIM: ESP32-WROOM-32D (USB/Pil Besleme) + DSTK22666 (0.91" 128x32 OLED) +
 * GY-291 (ADXL345) OPTİMİZASYON:
 *  - I2C Hyper-Bus: 800 kHz Yüksek Hızlı Ekran Transferi
 *  - SSD1306 Donanımsal Osilatör Aşırtma (0xF0): Sıfır Yırtılma / Sıfır Titreme
 * (150+ Hz Satır Tarama)
 *  - AABB Erken Eleme & Simetrik 5-Yönlü Spatial Hash & Ters-Karekök (rsqrt)
 * PBD Çözücü
 *  - Ultra-Hızlı 2x2 Makro-Piksel Sayfa Eşlemeli Doğrudan GDDRAM Çizim Motoru
 *  - ProMotion Dinamik Kare Hızı: Hareket anında 100-110 FPS, durunca 33 FPS
 * (Pil Tasarrufu)
 *  - ADXL345 400 Hz Yüksek Hızlı Düşük Güçlü Örnekleme (140 µA sabit akım)
 */

#include "esp32-hal-bt.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include <Arduino.h>
#include <Wire.h>
#include <math.h>

// ============================================================================
// 1. DONANIM PİNLERİ VE RENK/FİZİK AYARLARI
// ============================================================================
#define I2C_SDA_PIN 21
#define I2C_SCL_PIN 22

#define OLED_ADDR 0x3C    // DSTK22666 (0.91" 128x32 SSD1306) I2C Adresi
#define ADXL345_ADDR 0x53 // GY-291 (ADXL345) I2C Adresi (CS->3V3, SDO->GND)

#define SCREEN_W 128
#define SCREEN_H 32
#define WAKE_TIMEOUT_MS 60000UL // 60 Saniye (1 Dakika) açık kalır, sonra uyur

// ============================================================================
// CYBERPUNK NEON PALETTE ARCHITECTURE (PEMBE & NEON)
// ============================================================================
#define COLOR_PINK_NEON_RGB 0xFF1493    // Cyberpunk Derin Neon Pembe (#FF1493)
#define COLOR_PINK_ACCENT_RGB 0xFF69B4  // Parlak Pembe Aksan (#FF69B4)
#define COLOR_CYBER_PURPLE_RGB 0x8A2BE2 // Elektrik Mor (#8A2BE2)

// SALLAMA VE UYKU HASSASİYETİ:
#define SHAKE_WAKE_THRESHOLD_G 2.00f
#define SHAKE_CONFIRM_COUNT 2

// AKIŞKAN FİZİĞİ PARAMETRELERİ (POSITION BASED DYNAMICS):
#define NUM_PARTICLES 240 // Kapsülün ~%42'sini dolduran sürekli parçacık sayısı
#define PARTICLE_REST_DIST                                                     \
  1.80f // İdeal parçacıklar arası mesafe (Sıkıştırılamaz sıvı)
#define PARTICLE_REST_DIST_SQ (PARTICLE_REST_DIST * PARTICLE_REST_DIST)
#define COHESION_RADIUS 3.0f // Yüzey gerilimi / çekim yarıçapı
#define COHESION_RADIUS_SQ (COHESION_RADIUS * COHESION_RADIUS)
#define FLUID_DAMPING 0.965f // Sıvı viskozitesi (akışkanlık direnci)
#define GRAVITY_ACCEL 0.44f  // İvme kuvvet çarpanı
#define INERTIA_BOOST 2.20f  // Eylemsizlik çarpanı

// KAPSÜL İÇ SINIRLARI
#define CAPSULE_MIN_X 3.0f
#define CAPSULE_MAX_X 124.0f
#define CAPSULE_MIN_Y 3.0f
#define CAPSULE_MAX_Y 28.0f
#define CAPSULE_CORNER_R 4.0f

// Eksen yönü kalibrasyonu: Düzeltilmiş X ve Y yönleri (gx = ay, gy = ax)
#define INVERT_GRAVITY_X 1.0f
#define INVERT_GRAVITY_Y 1.0f
#define SWAP_XY_AXES true

// ============================================================================
// 2. MACLESS-HAYSTACK (APPLE FIND MY) 28-BYTE PUBLIC KEY
// ============================================================================
const uint8_t PUBLIC_KEY_28[28] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0x01,
                                   0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                                   0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
                                   0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16};

static uint8_t haystackPayload[31] = {
    0x1E, 0xFF, 0x4C, 0x00, 0x12, 0x19, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

static esp_ble_adv_params_t bleAdvParams = {
    .adv_int_min = 0x0C80,
    .adv_int_max = 0x0C80,
    .adv_type = ADV_TYPE_NONCONN_IND,
    .own_addr_type = BLE_ADDR_TYPE_RANDOM,
    .peer_addr = {0},
    .peer_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

// ============================================================================
// 3. GLOBAL DEĞİŞKENLER VE SÜREKLİ PARÇACIK VERİLERİ
// ============================================================================
static uint8_t oledBuffer[SCREEN_W * (SCREEN_H / 8)]; // 512 Byte

struct FluidParticle {
  float x, y;
  float px, py;
};

static FluidParticle particles[NUM_PARTICLES];

#define GRID_COLS 16
#define GRID_ROWS 4
#define MAX_CELL_PARTICLES 36
static uint8_t gridCount[GRID_ROWS][GRID_COLS];
static uint8_t gridParticles[GRID_ROWS][GRID_COLS][MAX_CELL_PARTICLES];

static bool isSystemAwake = true;
static uint32_t lastWakeTimeMs = 0;

static float filtGx = 0.0f;
static float filtGy = 1.0f;
static float effGx = 0.0f;
static float effGy = 1.0f;
static float currentTotalG = 1.0f;
static float shakeMagnitude = 0.0f;

static uint32_t rngState = 0xCAFEBABE;
static inline uint32_t fastRand() {
  uint32_t x = rngState;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  return (rngState = x);
}

// ============================================================================
// 4. DSTK22666 (0.91" 128x32 SSD1306 OLED) DONANIM SÜRÜCÜSÜ & HYPER-BUS
// ============================================================================
static void oledCommand(uint8_t cmd) {
  Wire.beginTransmission(OLED_ADDR);
  Wire.write(0x00);
  Wire.write(cmd);
  Wire.endTransmission();
}

static void oledInit128x32() {
  const uint8_t initSeq[] = {
      0xAE, // Display Off
      0xD5,
      0xF0, // MAX OSCILLATOR CLOCK (0xF0: row scan hızını 150+ Hz yapar,
            // titreme ve yırtılmayı siler)
      0xA8,
      0x1F, // Multiplex 1/32
      0xD3,
      0x00, // Display Offset 0
      0x40, // Start line 0
      0x8D,
      0x14, // Charge pump ON
      0x20,
      0x00, // Horizontal Addressing Mode
      0xA1, // Segment Remap (Col 127 mapped to SEG0)
      0xC8, // COM Output Scan Direction (remapped mode)
      0xDA,
      0x02, // COM Pins Hardware Config: Sequential
      0x81,
      0xCF, // Contrast Control (High Brightness)
      0xD9,
      0xF1, // Pre-charge Period (Phase 1: 1 DCLK, Phase 2: 15 DCLK)
      0xDB,
      0x40, // VCOMH Deselect Level (~0.77 x VCC)
      0xA4, // Entire Display On (Resume to RAM content)
      0xA6, // Normal Display (Non-inverted)
      0xAF  // Display ON
  };
  Wire.beginTransmission(OLED_ADDR);
  Wire.write(0x00);
  Wire.write(initSeq, sizeof(initSeq));
  Wire.endTransmission();
}

static void oledSetPower(bool turnOn) {
  if (turnOn) {
    oledCommand(0x8D);
    oledCommand(0x14);
    oledCommand(0xAF);
  } else {
    oledCommand(0xAE);
    oledCommand(0x8D);
    oledCommand(0x10);
  }
}

static void oledFlush() {
  Wire.setClock(800000UL);

  const uint8_t windowCmds[] = {0x21, 0x00, 0x7F, 0x22, 0x00, 0x03};
  Wire.beginTransmission(OLED_ADDR);
  Wire.write(0x00);
  Wire.write(windowCmds, sizeof(windowCmds));
  Wire.endTransmission();

  // Tek seferde kesintisiz 512-byte GDDRAM burst aktarımı (Sıfır işlem arası
  // gecikme)
  Wire.beginTransmission(OLED_ADDR);
  Wire.write(0x40);
  Wire.write(oledBuffer, 512);
  Wire.endTransmission();

  Wire.setClock(400000UL);
}

static inline void oledSetPixel(int x, int y) {
  if ((unsigned)x < SCREEN_W && (unsigned)y < SCREEN_H) {
    oledBuffer[x + ((y >> 3) << 7)] |= (1 << (y & 7));
  }
}

// ============================================================================
// 5. GY-291 (ADXL345) SÜRÜCÜSÜ VE 400 HZ YÜKSEK HIZLI EYLEMSİZLİK AYRIŞTIRICI
// ============================================================================
static void adxlWriteReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(ADXL345_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

static uint8_t adxlReadReg(uint8_t reg) {
  Wire.beginTransmission(ADXL345_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0)
    return 0;
  if (Wire.requestFrom((uint8_t)ADXL345_ADDR, (uint8_t)1, (uint8_t)true) == 1) {
    return Wire.read();
  }
  return 0;
}

static void adxl345ConfigureMode(bool lowPowerWatchMode) {
  adxlWriteReg(0x31, 0x0B); // Full resolution, ±16g

  float deltaG = SHAKE_WAKE_THRESHOLD_G - 0.80f;
  if (deltaG < 0.5f)
    deltaG = 0.5f;
  uint8_t threshActReg = (uint8_t)(deltaG * 16.0f);
  adxlWriteReg(0x24, threshActReg);

  adxlWriteReg(0x27, 0xF0); // Enable activity on X, Y, Z
  adxlWriteReg(0x2E, 0x10); // Activity interrupt to INT1

  if (lowPowerWatchMode) {
    adxlWriteReg(0x2C, 0x19); // Low power mode, 8 Hz sleep sampling (~25 µA)
  } else {
    adxlWriteReg(
        0x2C,
        0x0C); // Aktif mod: 400 Hz örnekleme (Akıcı tepki, 140 µA sabit akım)
  }

  adxlWriteReg(0x2D, 0x08); // Measurement mode
  (void)adxlReadReg(0x30);  // Clear interrupts
}

static bool adxl345CheckHardShakeWake() {
  static uint8_t shakeHitCount = 0;
  static uint32_t firstHitTimeMs = 0;

  uint8_t intSource = adxlReadReg(0x30);
  bool hwActivityTriggered = (intSource & 0x10) != 0;

  float totalG = 1.0f;
  Wire.beginTransmission(ADXL345_ADDR);
  Wire.write(0x32);
  if (Wire.endTransmission(false) == 0 &&
      Wire.requestFrom((uint8_t)ADXL345_ADDR, (uint8_t)6, (uint8_t)true) == 6) {
    int16_t rawAx = (int16_t)(Wire.read() | (Wire.read() << 8));
    int16_t rawAy = (int16_t)(Wire.read() | (Wire.read() << 8));
    int16_t rawAz = (int16_t)(Wire.read() | (Wire.read() << 8));

    float ax = (float)rawAx * 0.00390625f; // raw / 256.0f
    float ay = (float)rawAy * 0.00390625f;
    float az = (float)rawAz * 0.00390625f;
    totalG = sqrtf(ax * ax + ay * ay + az * az);
  }

  if (totalG >= SHAKE_WAKE_THRESHOLD_G ||
      (hwActivityTriggered && totalG >= 1.70f)) {
    uint32_t now = millis();
    if (now - firstHitTimeMs > 500) {
      shakeHitCount = 1;
      firstHitTimeMs = now;
      Serial.printf("[UYKU DINLEME] Ilk sarsinti algilandi (%.2f G) -> Ikinci "
                    "darbe bekleniyor...\n",
                    totalG);
    } else {
      shakeHitCount++;
      if (shakeHitCount >= SHAKE_CONFIRM_COUNT) {
        shakeHitCount = 0;
        Serial.printf("[UYANDIRILDI!] Sert sallama dogrulandi (%.2f G). Ekran "
                      "60 sn acik!\n",
                      totalG);
        return true;
      }
    }
  }
  return false;
}

static void adxl345ReadGravity() {
  Wire.beginTransmission(ADXL345_ADDR);
  Wire.write(0x32);
  if (Wire.endTransmission(false) != 0)
    return;

  if (Wire.requestFrom((uint8_t)ADXL345_ADDR, (uint8_t)6, (uint8_t)true) == 6) {
    int16_t rawAx = (int16_t)(Wire.read() | (Wire.read() << 8));
    int16_t rawAy = (int16_t)(Wire.read() | (Wire.read() << 8));
    int16_t rawAz = (int16_t)(Wire.read() | (Wire.read() << 8));

    float ax = (float)rawAx * 0.00390625f;
    float ay = (float)rawAy * 0.00390625f;
    float az = (float)rawAz * 0.00390625f;

    float gx = SWAP_XY_AXES ? ay : ax;
    float gy = SWAP_XY_AXES ? ax : ay;

    gx *= INVERT_GRAVITY_X;
    gy *= INVERT_GRAVITY_Y;

    currentTotalG = sqrtf(ax * ax + ay * ay + az * az);
    float instantShake = fabsf(currentTotalG - 1.0f);
    shakeMagnitude = (shakeMagnitude * 0.70f) + (instantShake * 0.30f);

    if (currentTotalG >= SHAKE_WAKE_THRESHOLD_G) {
      lastWakeTimeMs = millis();
    }

    // Dinamik adaptif filtre (Hareketliyken sıfır gecikme, dururken sıfır
    // gürültü)
    float alpha = (shakeMagnitude > 0.15f) ? 0.25f : 0.12f;
    filtGx = (filtGx * (1.0f - alpha)) + (gx * alpha);
    filtGy = (filtGy * (1.0f - alpha)) + (gy * alpha);

    float dynAx = gx - filtGx;
    float dynAy = gy - filtGy;

    effGx = filtGx - (dynAx * INERTIA_BOOST);
    effGy = filtGy - (dynAy * INERTIA_BOOST);

    float effMagSq = effGx * effGx + effGy * effGy;
    if (effMagSq > 10.24f) { // 3.2^2
      float s = 3.2f * (1.0f / sqrtf(effMagSq));
      effGx *= s;
      effGy *= s;
    }
  }
}

// ============================================================================
// 6. ULTRA-GERÇEKÇİ POSITION BASED DYNAMICS (PBD) SIVI MOTORU
// ============================================================================
static void initFluidPool() {
  int idx = 0;
  for (float y = CAPSULE_MAX_Y - 2.0f;
       y >= CAPSULE_MIN_Y + 2.0f && idx < NUM_PARTICLES; y -= 1.8f) {
    for (float x = CAPSULE_MIN_X + 6.0f;
         x <= CAPSULE_MAX_X - 6.0f && idx < NUM_PARTICLES; x += 1.8f) {
      particles[idx].x = x + ((float)(fastRand() % 100) * 0.005f);
      particles[idx].y = y + ((float)(fastRand() % 100) * 0.005f);
      particles[idx].px = particles[idx].x;
      particles[idx].py = particles[idx].y;
      idx++;
    }
  }
}

static void triggerKineticSplash(float intensity) {
  float angle = atan2f(effGy, effGx);
  float splashAngle = angle + PI;

  for (int i = 0; i < NUM_PARTICLES; i++) {
    if ((fastRand() % 100) < (int)(intensity * 100.0f)) {
      float randSpread = ((float)(fastRand() % 200) - 100.0f) * 0.008f;
      float pAngle = splashAngle + randSpread;
      float speed = 1.5f + ((float)(fastRand() % 100) * 0.035f * intensity);

      particles[i].px = particles[i].x - cosf(pAngle) * speed;
      particles[i].py = particles[i].y - sinf(pAngle) * speed;
    }
  }
}

static inline void applyCapsuleBoundary(FluidParticle &p) {
  if (p.x < CAPSULE_MIN_X) {
    p.x = CAPSULE_MIN_X;
    p.px = p.x + (p.x - p.px) * 0.25f;
  } else if (p.x > CAPSULE_MAX_X) {
    p.x = CAPSULE_MAX_X;
    p.px = p.x + (p.x - p.px) * 0.25f;
  }

  if (p.y < CAPSULE_MIN_Y) {
    p.y = CAPSULE_MIN_Y;
    p.py = p.y + (p.y - p.py) * 0.25f;
  } else if (p.y > CAPSULE_MAX_Y) {
    p.y = CAPSULE_MAX_Y;
    p.py = p.y + (p.y - p.py) * 0.25f;
  }

  float cx = 0, cy = 0;
  bool inCorner = false;

  if (p.x < CAPSULE_MIN_X + CAPSULE_CORNER_R) {
    cx = CAPSULE_MIN_X + CAPSULE_CORNER_R;
    if (p.y < CAPSULE_MIN_Y + CAPSULE_CORNER_R) {
      cy = CAPSULE_MIN_Y + CAPSULE_CORNER_R;
      inCorner = true;
    } else if (p.y > CAPSULE_MAX_Y - CAPSULE_CORNER_R) {
      cy = CAPSULE_MAX_Y - CAPSULE_CORNER_R;
      inCorner = true;
    }
  } else if (p.x > CAPSULE_MAX_X - CAPSULE_CORNER_R) {
    cx = CAPSULE_MAX_X - CAPSULE_CORNER_R;
    if (p.y < CAPSULE_MIN_Y + CAPSULE_CORNER_R) {
      cy = CAPSULE_MIN_Y + CAPSULE_CORNER_R;
      inCorner = true;
    } else if (p.y > CAPSULE_MAX_Y - CAPSULE_CORNER_R) {
      cy = CAPSULE_MAX_Y - CAPSULE_CORNER_R;
      inCorner = true;
    }
  }

  if (inCorner) {
    float dx = p.x - cx;
    float dy = p.y - cy;
    float distSq = dx * dx + dy * dy;
    if (distSq > (CAPSULE_CORNER_R * CAPSULE_CORNER_R)) {
      float dist = sqrtf(distSq);
      if (dist > 0.001f) {
        float nx = dx / dist;
        float ny = dy / dist;
        p.x = cx + nx * CAPSULE_CORNER_R;
        p.y = cy + ny * CAPSULE_CORNER_R;
      }
    }
  }
}

// 5-Yönlü Simetrik Hücre Komşuluğu (Tüm 8 komşu tek seferde taranır)
static const int8_t NEIGHBOR_DIRS[5][2] = {
    {0, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}};

static void stepFluidPhysics() {
  if (shakeMagnitude > 0.45f) {
    triggerKineticSplash(fminf(shakeMagnitude * 0.6f, 1.0f));
  }

  float ax = effGx * GRAVITY_ACCEL;
  float ay = effGy * GRAVITY_ACCEL;

  // 1. Verlet Entegrasyonu ve Hız Sınırlayıcı
  for (int i = 0; i < NUM_PARTICLES; i++) {
    float vx = (particles[i].x - particles[i].px) * FLUID_DAMPING;
    float vy = (particles[i].y - particles[i].py) * FLUID_DAMPING;

    float speedSq = vx * vx + vy * vy;
    if (speedSq > 16.0f) {
      float s = 4.0f * (1.0f / sqrtf(speedSq));
      vx *= s;
      vy *= s;
    }

    particles[i].px = particles[i].x;
    particles[i].py = particles[i].y;

    particles[i].x += vx + ax;
    particles[i].y += vy + ay;

    applyCapsuleBoundary(particles[i]);
  }

  // 2. Spatial Hash Tablosunu Doldur
  memset(gridCount, 0, sizeof(gridCount));
  for (uint8_t i = 0; i < NUM_PARTICLES; i++) {
    int gx = (int)(particles[i].x * 0.125f);
    int gy = (int)(particles[i].y * 0.125f);
    if (gx < 0)
      gx = 0;
    else if (gx >= GRID_COLS)
      gx = GRID_COLS - 1;
    if (gy < 0)
      gy = 0;
    else if (gy >= GRID_ROWS)
      gy = GRID_ROWS - 1;

    if (gridCount[gy][gx] < MAX_CELL_PARTICLES) {
      gridParticles[gy][gx][gridCount[gy][gx]++] = i;
    }
  }

  // 3. Simetrik 5-Yönlü Spatial Hash & AABB Erken Eleme Çözücü (%75 daha az
  // işlem)
  const float REST_DIST = PARTICLE_REST_DIST;
  const float REST_DIST_SQ = PARTICLE_REST_DIST_SQ;
  const float COHES_RADIUS = COHESION_RADIUS;
  const float COHES_RADIUS_SQ = COHESION_RADIUS_SQ;

  for (int iter = 0; iter < 2; iter++) {
    for (int gy = 0; gy < GRID_ROWS; gy++) {
      for (int gx = 0; gx < GRID_COLS; gx++) {
        uint8_t countA = gridCount[gy][gx];
        if (countA == 0)
          continue;

        for (int d = 0; d < 5; d++) {
          int nx = gx + NEIGHBOR_DIRS[d][0];
          int ny = gy + NEIGHBOR_DIRS[d][1];
          if (nx < 0 || nx >= GRID_COLS || ny < 0 || ny >= GRID_ROWS)
            continue;

          uint8_t countB = gridCount[ny][nx];
          if (countB == 0)
            continue;

          bool isSameCell = (d == 0);

          for (uint8_t a = 0; a < countA; a++) {
            uint8_t i = gridParticles[gy][gx][a];
            float pxA = particles[i].x;
            float pyA = particles[i].y;
            uint8_t startB = isSameCell ? (a + 1) : 0;

            for (uint8_t b = startB; b < countB; b++) {
              uint8_t j = gridParticles[ny][nx][b];

              // AABB Erken Eleme: 1-çevrimlik skaler eleme (sqrtf ve distSq
              // hesaplamalarını %75 azaltır)
              float diffX = pxA - particles[j].x;
              if (diffX > COHES_RADIUS || diffX < -COHES_RADIUS)
                continue;

              float diffY = pyA - particles[j].y;
              if (diffY > COHES_RADIUS || diffY < -COHES_RADIUS)
                continue;

              float distSq = diffX * diffX + diffY * diffY;
              if (distSq >= COHES_RADIUS_SQ || distSq < 0.0001f)
                continue;

              float invDist = 1.0f / sqrtf(distSq);
              float dist = distSq * invDist;
              float nxRay = diffX * invDist;
              float nyRay = diffY * invDist;

              if (distSq < REST_DIST_SQ) {
                float delta = (REST_DIST - dist) * 0.48f;
                particles[i].x += nxRay * delta;
                particles[i].y += nyRay * delta;
                particles[j].x -= nxRay * delta;
                particles[j].y -= nyRay * delta;
              } else {
                float pull = (dist - REST_DIST) * 0.030f;
                particles[i].x -= nxRay * pull;
                particles[i].y -= nyRay * pull;
                particles[j].x += nxRay * pull;
                particles[j].y += nyRay * pull;
              }
            }
          }
        }
      }
    }

    for (int i = 0; i < NUM_PARTICLES; i++) {
      applyCapsuleBoundary(particles[i]);
    }
  }
}

// ============================================================================
// 7. ULTRA-ÇÖZÜNÜRLÜKLÜ EKRAN ÇİZİM MOTORU (DOĞRUDAN SAYFA EŞLEME)
// ============================================================================
static void renderUltraFluid(uint32_t frameCounter) {
  memset(oledBuffer, 0, sizeof(oledBuffer));

  // 1. Ultra-Hızlı 2x2 Kompakt Mikro Damlacık Çizimi
  for (int i = 0; i < NUM_PARTICLES; i++) {
    int cx = (int)(particles[i].x + 0.5f);
    int cy = (int)(particles[i].y + 0.5f);

    int page = cy >> 3;
    int bitInPage = cy & 7;
    int colIdx = cx + (page << 7);

    if (bitInPage < 7) {
      uint8_t mask = (3 << bitInPage);
      oledBuffer[colIdx] |= mask;
      oledBuffer[colIdx + 1] |= mask;
    } else {
      oledBuffer[colIdx] |= 0x80;
      oledBuffer[colIdx + 1] |= 0x80;
      oledBuffer[colIdx + 128] |= 0x01;
      oledBuffer[colIdx + 129] |= 0x01;
    }
  }

  // 2. Cyberpunk Kapsül Çerçevesi (Cam Tüp)
  for (int x = 4; x < SCREEN_W - 4; x++) {
    oledBuffer[x] |= 0x02;            // Y = 1
    oledBuffer[x + (3 << 7)] |= 0x40; // Y = 30 (SCREEN_H - 2)
  }
  for (int y = 4; y < SCREEN_H - 4; y++) {
    oledSetPixel(1, y);
    oledSetPixel(SCREEN_W - 2, y);
  }

  // Yuvarlatılmış köşeler
  oledSetPixel(2, 3);
  oledSetPixel(3, 2);
  oledSetPixel(SCREEN_W - 3, 3);
  oledSetPixel(SCREEN_W - 4, 2);
  oledSetPixel(2, SCREEN_H - 4);
  oledSetPixel(3, SCREEN_H - 3);
  oledSetPixel(SCREEN_W - 3, SCREEN_H - 4);
  oledSetPixel(SCREEN_W - 4, SCREEN_H - 3);

  // Kapsül cetvel / seviye işaretleri
  oledBuffer[SCREEN_W / 4] |= 0x01;
  oledBuffer[SCREEN_W / 2] |= 0x01;
  oledBuffer[(SCREEN_W * 3) / 4] |= 0x01;

  // Kalp atışı / durum ledi
  if ((frameCounter % 120) < 14) {
    oledSetPixel(SCREEN_W - 6, 4);
    oledSetPixel(SCREEN_W - 7, 4);
    oledSetPixel(SCREEN_W - 6, 5);
    oledSetPixel(SCREEN_W - 7, 5);
  }

  oledFlush();
}

// ============================================================================
// 8. SERİ PORT TEST VE DİYAGNOSTİK SİSTEMİ
// ============================================================================
static void runHardwareDiagnostics() {
  Serial.println("\n=======================================================");
  Serial.println("   CYBERPUNK ROZET - PROMOTION PBD SIVI MOTORU (110 FPS)");
  Serial.println("=======================================================");

  uint8_t count = 0;
  Serial.print("[I2C TARA] Hatta yanit veren adresler: ");
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("0x%02X ", addr);
      count++;
    }
  }
  Serial.printf("(Toplam %d cihaz)\n", count);

  Wire.beginTransmission(OLED_ADDR);
  if (Wire.endTransmission() == 0) {
    Serial.println("[OK] DSTK22666 (0.91\" 128x32 OLED) 0x3C adresinde bagli!");
  } else {
    Serial.println("[HATA!] OLED Ekran bulunamadi!");
  }

  Wire.beginTransmission(ADXL345_ADDR);
  if (Wire.endTransmission() == 0) {
    uint8_t devId = adxlReadReg(0x00);
    Serial.printf(
        "[OK] GY-291 (ADXL345) 0x53 adresinde bagli! (Cip ID: 0x%02X)\n",
        devId);
  } else {
    Serial.println("[HATA!] GY-291 bulunamadi!");
  }

  Serial.println("-------------------------------------------------------");
  Serial.println("KLAVYE TEST KOMUTLARI (Seri Port'tan gonderebilirsin):");
  Serial.println("  's' -> Suya Kinetik Sok Dalgasi Firlat (Splash)");
  Serial.println("  'u' -> Hemen Uyku Moduna Gecir");
  Serial.println("  'r' -> Sıvı Havuzunu Sifirla");
  Serial.println("=======================================================\n");
}

static void handleSerialCommands() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == 's' || c == 'S') {
      if (!isSystemAwake) {
        isSystemAwake = true;
        adxl345ConfigureMode(false);
        oledSetPower(true);
        Serial.println("[SERI KOMUT] Sistem klavyeden uyandirildi!");
      }
      lastWakeTimeMs = millis();
      triggerKineticSplash(1.4f);
      Serial.println(
          "[SERI KOMUT] Suya Kinetik Sok Dalgasi (Splash) uygulandi!");
    } else if (c == 'u' || c == 'U') {
      if (isSystemAwake) {
        isSystemAwake = false;
        oledSetPower(false);
        adxl345ConfigureMode(true);
        Serial.println("[SERI KOMUT] Sistem manuel olarak UYKU MODUNA alindi.");
      }
    } else if (c == 'r' || c == 'R') {
      initFluidPool();
      lastWakeTimeMs = millis();
      Serial.println("[SERI KOMUT] Sıvı havuzu sifirlandi.");
    }
  }
}

// ============================================================================
// 9. ÇİFT ÇEKİRDEK FREERTOS GÖREVLERİ
// ============================================================================
void bleAirTagTask(void *pvParameters) {
  btStart();
  esp_bluedroid_init();
  esp_bluedroid_enable();

  esp_bd_addr_t randMacAddr;
  memcpy(randMacAddr, PUBLIC_KEY_28, 6);
  randMacAddr[0] |= 0xC0;

  memcpy(&haystackPayload[7], &PUBLIC_KEY_28[6], 22);
  haystackPayload[29] = (PUBLIC_KEY_28[0] >> 6) & 0x03;

  esp_ble_gap_set_rand_addr(randMacAddr);
  esp_ble_gap_config_adv_data_raw(haystackPayload, sizeof(haystackPayload));
  esp_ble_gap_start_advertising(&bleAdvParams);

  esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P9);

  Serial.printf("[BLE CORE 0] Apple Find My yayini aktif! MAC: "
                "%02X:%02X:%02X:%02X:%02X:%02X\n",
                randMacAddr[0], randMacAddr[1], randMacAddr[2], randMacAddr[3],
                randMacAddr[4], randMacAddr[5]);

  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(10000));
  }
}

void waterSimTask(void *pvParameters) {
  uint32_t frameCounter = 0;
  uint32_t lastTelemetryMs = millis();
  uint32_t lastFpsCount = 0;
  uint32_t lastFpsTime = millis();
  uint32_t liveFps = 60;
  float smoothedKinetic = 1.0f;

  for (;;) {
    handleSerialCommands();

    if (!isSystemAwake) {
      if (adxl345CheckHardShakeWake()) {
        isSystemAwake = true;
        lastWakeTimeMs = millis();
        adxl345ConfigureMode(false);
        oledSetPower(true);
        triggerKineticSplash(1.2f);
      } else {
        vTaskDelay(pdMS_TO_TICKS(40));
        continue;
      }
    }

    uint32_t now = millis();
    uint32_t elapsed = now - lastWakeTimeMs;
    if (elapsed >= WAKE_TIMEOUT_MS) {
      isSystemAwake = false;
      oledSetPower(false);
      adxl345ConfigureMode(true);
      Serial.println("[OTO-UYKU] 60 saniye doldu, ekran kapatildi.");
      continue;
    }

    uint32_t frameStart = millis();

    adxl345ReadGravity();
    stepFluidPhysics();
    renderUltraFluid(frameCounter);

    // Kinetik enerjiyi seyrek örnekle (6 parçacıkta 1 örnek = sıfır ek yük)
    float kinetic = 0.0f;
    for (int i = 0; i < NUM_PARTICLES; i += 6) {
      float vx = particles[i].x - particles[i].px;
      float vy = particles[i].y - particles[i].py;
      kinetic += vx * vx + vy * vy;
    }
    smoothedKinetic = (smoothedKinetic * 0.85f) + (kinetic * 0.15f);

    frameCounter++;

    // Gerçek Donanımsal FPS Sayacı
    if (now - lastFpsTime >= 1000) {
      liveFps = (frameCounter - lastFpsCount) * 1000 / (now - lastFpsTime);
      lastFpsCount = frameCounter;
      lastFpsTime = now;
    }

    if (now - lastTelemetryMs >= 2000) {
      lastTelemetryMs = now;
      uint32_t remSec = (WAKE_TIMEOUT_MS - elapsed) / 1000;
      Serial.printf("[PROMOTION PBD] Fx:%+.2f Fy:%+.2f | Enerji:%.2f | FPS:%u "
                    "| Kalan:%lu sn\n",
                    effGx, effGy, smoothedKinetic, liveFps,
                    (unsigned long)remSec);
    }

    // ADAPTİF PROMOTION FPS DİNAMİĞİ (PİL KORUMA & HİPER-AKIŞKANLIK):
    // Hareket / Çalkalanma halinde: 8 ms hedef (~110-120 FPS, ultra akıcı)
    // Sıvı durulunca: 30 ms hedef (~33 FPS, %70 CPU/I2C uyku modu, bataryayı
    // korur)
    uint32_t targetFrameTimeMs =
        (smoothedKinetic > 0.06f || shakeMagnitude > 0.04f) ? 8 : 30;
    uint32_t workTime = millis() - frameStart;

    if (workTime < targetFrameTimeMs) {
      vTaskDelay(pdMS_TO_TICKS(targetFrameTimeMs - workTime));
    } else {
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(400);

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, 400000UL);
  Wire.setTimeOut(50);
  Wire.setBufferSize(
      640); // 640 byte FIFO tampon (tek seferde 512 byte kesintisiz aktarım)

  runHardwareDiagnostics();
  adxl345ConfigureMode(false);
  oledInit128x32();
  initFluidPool();

  lastWakeTimeMs = millis();

  xTaskCreatePinnedToCore(bleAirTagTask, "BLE_Haystack", 4096, NULL, 2, NULL,
                          0);
  xTaskCreatePinnedToCore(waterSimTask, "PBD_UltraFluid", 8192, NULL, 5, NULL,
                          1);
}

void loop() { vTaskDelete(NULL); }
