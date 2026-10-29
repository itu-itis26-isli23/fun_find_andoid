/**
 * PROJE: Cyberpunk Akıllı Rozet / Anahtarlık (Hyper-FPS, Romantik Etkileşim & Eko-Pil Sürümü)
 * DONANIM: ESP32-WROOM-32D (1000 mAh Pil / USB) + DSTK22666 (0.91" 128x32 OLED) +
 *          GY-291 (ADXL345) + MH-FMD Pasif/Aktif Buzzer (GPIO 25)
 *
 * ÖZELLİKLER VE OPTİMİZASYONLAR:
 *  - 80 MHz Aktif CPU + Hibrit Eko-Uyku Mimarisi: 1000 mAh batarya ile 3-4+ gün kullanım
 *  - I2C Hyper-Bus (800 kHz) + SSD1306 0xF0 Osilatör Aşırtma (110 FPS Sıfır Yırtılma)
 *  - "H ♥ T" Başlangıç Dizilimi (~1.8 sn sabit) -> Esnek Ragdoll Salınımı (~2.2 sn) -> PBD + XSPH Gerçekçi Su Fiziği
 *  - Pürüzsüz 10-Bit LEDC Müzik Kutusu (Music Box / Celesta) Ses Sentezleyici (ADSR Zarfı + 5.5 Hz Sinüs Vibrato)
 *  - ~1 Sn Buffer ve 2 Sn Seçim Kilitli Akıllı Sert Sallama Algılayıcı (Her sallamada tiz onay sesi: A6, C#7, E7)
 *  - 1x Sert Sallama: "H ♥ T" -> Ragdoll -> Gerçekçi Su Modu
 *  - 2x Sert Sallama: ADXL345 Kontrollü "Flying Heart" Oyunu (Love Story / Amélie müziği, 7 engelde Kalp Atma / Yanınca Kalp Kırılma animasyonu)
 *  - 3x Sert Sallama: "TUFAN HİLALİ ÇOK SEVİYOR" Kayan Yazı Şeridi (Can't Help Falling in Love müziği eşliğinde)
 */

#include "driver/uart.h"
#include "esp32-hal-bt.h"
#include "esp32-hal-cpu.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_sleep.h"
#include <Arduino.h>
#include <Wire.h>
#include <math.h>

// ============================================================================
// 1. DONANIM PİNLERİ VE SİSTEM PARAMETRELERİ
// ============================================================================
#define I2C_SDA_PIN 21
#define I2C_SCL_PIN 22
#define BUZZER_PIN 25 // MH-FMD Buzzer 'I/O' pini -> GPIO 25 (Active-LOW PNP)

#define BUZZER_IS_PASSIVE true

#define OLED_ADDR 0x3C    // DSTK22666 (0.91" 128x32 SSD1306) I2C Adresi
#define ADXL345_ADDR 0x53 // GY-291 (ADXL345) I2C Adresi

#define SCREEN_W 128
#define SCREEN_H 32

// UYKU VE GÜÇ YÖNETİMİ ZAMANLAYICILARI
#define WAKE_TIMEOUT_FLUID_MS 60000UL    // Su modunda 60 sn hareketsizlikte uyur
#define WAKE_TIMEOUT_SPECIAL_MS 180000UL // Oyun / Yazı modunda 180 sn açık kalır

// SERT SALLAMA (HARD SHAKE) VE MOD GEÇİŞ AYARLARI
#define SHAKE_WAKE_THRESHOLD_G 1.92f // Sert sallama ivme eşiği (G)
#define SHAKE_REARM_BELOW_G 1.42f    // Yeni sallama saymak için ivmenin düşmesi gereken seviye
#define SHAKE_BUFFER_MS 880UL        // Her sert sallama arası ~1 saniye buffer (karmaşayı önler)
#define SHAKE_LOCK_WAIT_MS 2000UL    // Son sallamadan 2 sn sonra o bölümde kalınır

// "H ♥ T" BAŞLANGIÇ VE RAGDOLL GEÇİŞ SÜRELERİ
#define HT_HOLD_DURATION_MS 1800UL    // İlk ~1.8 sn "H ♥ T" hareketsiz sabit durur
#define HT_RAGDOLL_DURATION_MS 2200UL // Sonraki ~2.2 sn yavaş ragdoll hareketi ve suya erime

// GERÇEKÇİ AKIŞKAN FİZİĞİ PARAMETRELERİ (PBD + XSPH + C2 SURFACE TENSION)
#define NUM_PARTICLES 240
#define PARTICLE_REST_DIST 1.82f
#define PARTICLE_REST_DIST_SQ (PARTICLE_REST_DIST * PARTICLE_REST_DIST)
#define COHESION_RADIUS 3.20f
#define COHESION_RADIUS_SQ (COHESION_RADIUS * COHESION_RADIUS)
#define FLUID_DAMPING 0.985f  // Yüksek akışkanlık (düşük hava direnci, gerçekçi dalga)
#define XSPH_VISCOSITY 0.042f // Komşu parçacıklar arası laminer su viskozitesi
#define GRAVITY_ACCEL 0.44f
#define INERTIA_BOOST 2.25f

// KAPSÜL İÇ SINIRLARI
#define CAPSULE_MIN_X 3.0f
#define CAPSULE_MAX_X 124.0f
#define CAPSULE_MIN_Y 3.0f
#define CAPSULE_MAX_Y 28.0f
#define CAPSULE_CORNER_R 4.0f

// EKSEN KALİBRASYONU
#define INVERT_GRAVITY_X 1.0f
#define INVERT_GRAVITY_Y 1.0f
#define SWAP_XY_AXES true

// FLYING HEART OYUN PARAMETRELERİ
#define GAME_TARGET_WINS 7           // 7 engel geçince kazanılır
#define GAME_ANIM_DURATION_MS 3500UL // Kalp kırılma / kalp atma animasyon süresi (~3.5 sn)
#define GAME_HEART_X 38.0f           // Ortadaki kalbin yatay konumu
#define GAME_OBSTACLE_COUNT 2        // Aynı anda ekrandaki engel çifti sayısı
#define GAME_OBSTACLE_SPACING 62.0f  // Engeller arası yatay mesafe
#define GAME_GAP_HEIGHT 15.0f        // Kalbin geçeceği dikey boşluk yüksekliği

// ============================================================================
// 2. MACLESS-HAYSTACK (APPLE FIND MY) 28-BYTE PUBLIC KEY & BLE AYARLARI
// ============================================================================
const uint8_t PUBLIC_KEY_28[28] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0x01,
                                   0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                                   0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
                                   0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16};

static uint8_t haystackPayload[31] = {
    0x1E, 0xFF, 0x4C, 0x00, 0x12, 0x19, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

static esp_ble_adv_params_t bleAdvParamsActive = {
    .adv_int_min = 0x0C80, // 2.0 sn (Aktif mod)
    .adv_int_max = 0x0C80,
    .adv_type = ADV_TYPE_NONCONN_IND,
    .own_addr_type = BLE_ADDR_TYPE_RANDOM,
    .peer_addr = {0},
    .peer_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static esp_ble_adv_params_t bleAdvParamsEcoSleep = {
    .adv_int_min = 0x0028, // 25 ms (Eko-Uyku periyodik yayın penceresi için hızlı burst)
    .adv_int_max = 0x0030,
    .adv_type = ADV_TYPE_NONCONN_IND,
    .own_addr_type = BLE_ADDR_TYPE_RANDOM,
    .peer_addr = {0},
    .peer_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static volatile bool bleReady = false;
static bool bleSuspendedForLightSleep = false;

// Arduino-ESP32 v3.x initArduino() varsayılan olarak BLE belleğini serbest bırakır;
// doğrudan ESP-IDF BLE GAP API kullanıldığı için BLE belleğinin korunmasını zorunlu kıl:
extern "C" bool bleInUse(void) { return true; }

// ============================================================================
// 3. UYGULAMA MODLARI, VERİ YAPILARI VE GLOBAL DEĞİŞKENLER
// ============================================================================
enum AppMode : uint8_t {
  APP_MODE_FLUID = 1,   // 1x Sallama: "H ♥ T" -> Ragdoll -> Gerçekçi Su
  APP_MODE_GAME = 2,    // 2x Sallama: ADXL345 Flying Heart Oyunu + Aşk Müziği
  APP_MODE_MARQUEE = 3  // 3x Sallama: "TUFAN HİLALİ ÇOK SEVİYOR" + Aşk Müziği
};

enum FluidStage : uint8_t {
  FLUID_STAGE_HT_HOLD = 0,    // İlk ~1.8 sn sabit "H ♥ T"
  FLUID_STAGE_HT_RAGDOLL = 1, // Sonraki ~2.2 sn esnek ragdoll salınımı ve erime
  FLUID_STAGE_WATER = 2       // Tam gerçekçi su fiziği
};

enum GameState : uint8_t {
  GAME_STATE_PLAYING = 0,
  GAME_STATE_LOSE_ANIM = 1, // Kalp çıkıp kırılma animasyonu
  GAME_STATE_WIN_ANIM = 2   // 7 engelde kalp çıkıp kalp atma animasyonu
};

static uint8_t oledBuffer[SCREEN_W * (SCREEN_H / 8)]; // 512 Byte GDDRAM tamponu

struct FluidParticle {
  float x, y;
  float px, py;
  float homeX, homeY;
  uint8_t clusterId; // 0: 'H', 1: '♥', 2: 'T'
};

static FluidParticle particles[NUM_PARTICLES];

// Ragdoll Küme Durumları (0: 'H', 1: '♥', 2: 'T')
static const float CLUSTER_REST_CX[3] = {29.0f, 64.0f, 97.0f};
static const float CLUSTER_REST_CY[3] = {15.3f, 15.3f, 15.3f};
static float ragDx[3] = {0.0f, 0.0f, 0.0f};
static float ragDy[3] = {0.0f, 0.0f, 0.0f};
static float ragVx[3] = {0.0f, 0.0f, 0.0f};
static float ragVy[3] = {0.0f, 0.0f, 0.0f};
static float ragAngle[3] = {0.0f, 0.0f, 0.0f};
static float ragOmega[3] = {0.0f, 0.0f, 0.0f};

#define GRID_COLS 16
#define GRID_ROWS 4
#define MAX_CELL_PARTICLES 40
static uint8_t gridCount[GRID_ROWS][GRID_COLS];
static uint8_t gridParticles[GRID_ROWS][GRID_COLS][MAX_CELL_PARTICLES];

static AppMode currentMode = APP_MODE_FLUID;
static FluidStage fluidStage = FLUID_STAGE_HT_HOLD;
static uint32_t fluidStageStartMs = 0;

static bool isSystemAwake = true;
static bool isSoundMuted = false;
static bool buzzerInitialized = false;
static uint32_t lastWakeTimeMs = 0;

// Sert Sallama Sayaç ve Zamanlayıcıları
static uint8_t pendingShakeCount = 0;
static uint32_t lastShakeDetectedMs = 0;
static bool shakeRearmed = true;
static bool wokeOnFirstShake = false;

// ADXL345 Fiziksel Sensör Değişkenleri
static float rawGVecX = 0.0f;
static float rawGVecY = 1.0f;
static float filtGx = 0.0f;
static float filtGy = 1.0f;
static float effGx = 0.0f;
static float effGy = 1.0f;
static float currentTotalG = 1.0f;
static float shakeMagnitude = 0.0f;
static float adxlJerkMagnitude = 0.0f;
static float adxlJerkDerivative = 0.0f;
static bool adxlFirstSampleAfterWake = true;

// Flying Heart Oyun Değişkenleri
struct GameObstacle {
  float x;
  float gapCenterY;
  bool passed;
};

static GameState gameState = GAME_STATE_PLAYING;
static uint32_t gameStateStartMs = 0;
static float gameHeartY = 16.0f;
static float gameHeartVy = 0.0f;
static float gameNeutralGx = 0.0f;
static float gameNeutralGy = 0.25f;
static uint8_t gameScore = 0;
static GameObstacle gameObstacles[GAME_OBSTACLE_COUNT];

// Kayan Yazı (Marquee) Değişkenleri
static float marqueeScrollX = (float)SCREEN_W;
static uint32_t marqueeStartMs = 0;

static uint32_t rngState = 0xCAFEBABE;
static inline uint32_t fastRand() {
  uint32_t x = rngState;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  return (rngState = x);
}

// ============================================================================
// 4. PÜRÜZSÜZ 10-BİT MÜZİK KUTUSU (MUSIC BOX) SES VE MELODİ MOTORU
// ============================================================================
#define LEDC_BUZZER_RES 10
#define LEDC_BUZZER_MAX_DUTY 1024 // %100 HIGH (PNP Kesimde -> 0 mA Tam Sessizlik)

static uint32_t lastHwFreqHz = 0;
static float smoothedFreq = 0.0f;
static float smoothedDuty = 0.0f;

struct MelodyNote {
  uint16_t freqHz; // 0 = Es (Sessizlik)
  uint16_t durMs;  // Nota süresi (ms)
};

// 2x Sert Sallama (Oyun Modu): "Love Story" / Amélie Romantik Müzik Kutusu Valsi
static const MelodyNote MELODY_LOVE_STORY[] = {
    {1319, 210}, {659, 210},  {659, 210},  {1319, 420}, {1319, 210},
    {1397, 210}, {1319, 210}, {1175, 210}, {1319, 210}, {1175, 210},
    {587, 210},  {587, 210},  {1175, 420}, {1175, 210}, {1319, 210},
    {1175, 210}, {1047, 210}, {1175, 210}, {1047, 210}, {523, 210},
    {523, 210},  {1047, 420}, {1047, 210}, {1175, 210}, {1047, 210},
    {988, 210},  {1047, 210}, {988, 380},  {880, 220},  {831, 220},
    {880, 560},  {0, 160},
    {880, 200},  {1047, 200}, {1319, 200}, {1760, 400}, {1568, 200},
    {1319, 200}, {1175, 400}, {988, 200},  {1175, 200}, {1397, 400},
    {1319, 200}, {1175, 200}, {1047, 400}, {988, 200},  {880, 550},
    {0, 180}};
static const uint16_t MELODY_LOVE_STORY_LEN =
    sizeof(MELODY_LOVE_STORY) / sizeof(MELODY_LOVE_STORY[0]);

// 3x Sert Sallama ("TUFAN HİLALİ ÇOK SEVİYOR"): "Can't Help Falling in Love"
static const MelodyNote MELODY_CANT_HELP_LOVE[] = {
    {784, 520},  {1175, 520}, {784, 680},  {0, 120},    {880, 260},
    {988, 260},  {1047, 500}, {988, 500},  {880, 680},  {0, 140},
    {587, 280},  {659, 500},  {740, 500},  {784, 500},  {880, 260},
    {988, 260},  {1047, 500}, {988, 500},  {880, 500},  {784, 720},
    {988, 160},  {1175, 160}, {1568, 420}, {0, 180},
    {988, 250},  {1319, 480}, {1480, 250}, {1568, 480}, {1480, 250},
    {1319, 520}, {988, 250},  {1319, 480}, {1480, 250}, {1568, 480},
    {1480, 250}, {1319, 520}, {988, 250},  {1175, 480}, {988, 250},
    {880, 520},  {988, 260},  {880, 620},  {0, 140},
    {784, 520},  {1175, 520}, {784, 680},  {0, 120},    {880, 260},
    {988, 260},  {1047, 500}, {988, 500},  {880, 680},  {0, 140},
    {587, 280},  {659, 500},  {740, 500},  {784, 500},  {880, 260},
    {988, 260},  {1047, 500}, {988, 500},  {880, 500},  {784, 800},
    {1175, 180}, {1568, 500}, {0, 260}};
static const uint16_t MELODY_CANT_HELP_LOVE_LEN =
    sizeof(MELODY_CANT_HELP_LOVE) / sizeof(MELODY_CANT_HELP_LOVE[0]);

struct SfxEvent {
  uint16_t freq1;
  uint16_t dur1;
  uint16_t freq2;
  uint16_t dur2;
  uint32_t startMs;
  bool active;
};

static SfxEvent activeSfx = {0, 0, 0, 0, 0, false};
static const MelodyNote *currentMelody = NULL;
static uint16_t currentMelodyLen = 0;
static uint16_t currentNoteIdx = 0;
static uint32_t noteStartMs = 0;

static void buzzerInit() {
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, HIGH);
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  if (!buzzerInitialized) {
    ledcAttach(BUZZER_PIN, 1000, LEDC_BUZZER_RES);
    ledcWrite(BUZZER_PIN, LEDC_BUZZER_MAX_DUTY);
    buzzerInitialized = true;
    lastHwFreqHz = 1000;
  }
#else
  if (!buzzerInitialized) {
    ledcSetup(0, 1000, LEDC_BUZZER_RES);
    ledcAttachPin(BUZZER_PIN, 0);
    ledcWrite(0, LEDC_BUZZER_MAX_DUTY);
    buzzerInitialized = true;
    lastHwFreqHz = 1000;
  }
#endif
}

static void buzzerSilence() {
  if (buzzerInitialized) {
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    ledcWrite(BUZZER_PIN, LEDC_BUZZER_MAX_DUTY);
#else
    ledcWrite(0, LEDC_BUZZER_MAX_DUTY);
#endif
  } else {
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, HIGH);
  }
  smoothedFreq = 0.0f;
  smoothedDuty = 0.0f;
}

static void buzzerWriteSmoothPWM(uint32_t freqHz, float dutyPercent) {
  if (isSoundMuted || freqHz < 110 || dutyPercent <= 0.05f) {
    if (buzzerInitialized) {
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
      ledcWrite(BUZZER_PIN, LEDC_BUZZER_MAX_DUTY);
#else
      ledcWrite(0, LEDC_BUZZER_MAX_DUTY);
#endif
    }
    return;
  }

  if (!buzzerInitialized) {
    buzzerInit();
  }

  float clampedDuty = constrain(dutyPercent, 0.10f, 12.0f);
  uint32_t activeTicks = (uint32_t)(clampedDuty * 10.24f);
  if (activeTicks < 1)
    activeTicks = 1;
  uint32_t duty = LEDC_BUZZER_MAX_DUTY - activeTicks;

#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  if (freqHz != lastHwFreqHz) {
    ledcWrite(BUZZER_PIN, LEDC_BUZZER_MAX_DUTY);
    ledcChangeFrequency(BUZZER_PIN, freqHz, LEDC_BUZZER_RES);
    lastHwFreqHz = freqHz;
  }
  ledcWrite(BUZZER_PIN, duty);
#else
  if (freqHz != lastHwFreqHz) {
    ledcWrite(0, LEDC_BUZZER_MAX_DUTY);
    ledcSetup(0, freqHz, LEDC_BUZZER_RES);
    lastHwFreqHz = freqHz;
  }
  ledcWrite(0, duty);
#endif
}

static void playMusicBoxEnvelope(uint16_t baseFreqHz, uint32_t elapsedMs,
                                 uint32_t totalDurMs, float peakDuty) {
  if (baseFreqHz == 0 || elapsedMs >= totalDurMs) {
    buzzerWriteSmoothPWM(0, 0.0f);
    return;
  }

  uint32_t soundDurMs =
      (totalDurMs > 40) ? (totalDurMs - 16)
                        : ((totalDurMs > 28) ? (totalDurMs - 10) : totalDurMs);
  if (elapsedMs >= soundDurMs) {
    buzzerWriteSmoothPWM(0, 0.0f);
    return;
  }

  // Yeni nota başlarken (genlik 0 iken) frekansı yalnızca 1 kez ayarla;
  // nota süresince baseFreqHz sabit kalır ve timer reset oluşmaz.
  if (!isSoundMuted && baseFreqHz >= 110 && baseFreqHz != lastHwFreqHz) {
    if (!buzzerInitialized) {
      buzzerInit();
    }
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    ledcWrite(BUZZER_PIN, LEDC_BUZZER_MAX_DUTY);
    ledcChangeFrequency(BUZZER_PIN, baseFreqHz, LEDC_BUZZER_RES);
#else
    ledcWrite(0, LEDC_BUZZER_MAX_DUTY);
    ledcSetup(0, baseFreqHz, LEDC_BUZZER_RES);
#endif
    lastHwFreqHz = baseFreqHz;
  }

  float env = 1.0f;
  if (elapsedMs < 16) {
    env = (float)(elapsedMs + 1) / 16.0f;
  } else {
    float decayProgress =
        (float)(elapsedMs - 16) / (float)(soundDurMs - 16 + 1);
    env = expf(-2.1f * decayProgress);
    if (soundDurMs - elapsedMs < 24) {
      env *= (float)(soundDurMs - elapsedMs) / 24.0f;
    }
  }

  // Frekansı değiştirmeden yalnızca 10-bit genlik üzerinde yumuşak tremolo:
  float tremolo = 1.0f + (sinf((float)elapsedMs * 0.0345f) * 0.08f);
  buzzerWriteSmoothPWM(baseFreqHz, peakDuty * env * tremolo);
}

static void triggerSweetSfx(uint16_t freq1, uint16_t dur1, uint16_t freq2 = 0,
                            uint16_t dur2 = 0) {
  if (isSoundMuted)
    return;
  activeSfx.freq1 = freq1;
  activeSfx.dur1 = dur1;
  activeSfx.freq2 = freq2;
  activeSfx.dur2 = dur2;
  activeSfx.startMs = millis();
  activeSfx.active = true;
}

static void triggerShakeConfirmBeep(uint8_t shakeNum) {
  if (shakeNum == 1) {
    triggerSweetSfx(1319, 45, 1760, 90);
  } else if (shakeNum == 2) {
    triggerSweetSfx(1760, 50, 2217, 100);
  } else {
    triggerSweetSfx(2093, 55, 2637, 115);
  }
}

static void startBackgroundMelody(const MelodyNote *melody, uint16_t length) {
  currentMelody = melody;
  currentMelodyLen = length;
  currentNoteIdx = 0;
  noteStartMs = millis();
}

static void stopBackgroundMelody() {
  currentMelody = NULL;
  currentMelodyLen = 0;
  currentNoteIdx = 0;
}

static void buzzerDirectTone(uint32_t freqHz, uint32_t durationMs,
                             float dutyPercent = 3.2f) {
  if (isSoundMuted || freqHz < 80)
    return;
  uint32_t start = millis();
  while (millis() - start < durationMs) {
    playMusicBoxEnvelope((uint16_t)freqHz, millis() - start, durationMs,
                         dutyPercent);
    delay(4);
  }
  buzzerSilence();
}

static void buzzerPulseActive(uint32_t durationMs) {
  if (isSoundMuted)
    return;
  buzzerWriteSmoothPWM(1800, 5.0f);
  delay(durationMs);
  buzzerSilence();
}

static void buzzerPlayWakeSound() {
  if (isSoundMuted)
    return;
  const uint16_t notes[4] = {1047, 1319, 1568, 2093};
  for (int i = 0; i < 4; i++) {
    uint32_t t0 = millis();
    while (millis() - t0 < 52) {
      playMusicBoxEnvelope(notes[i], millis() - t0, 52, 3.6f);
      delay(3);
    }
  }
  buzzerSilence();
}

static void updateFluidMotionSound(uint32_t nowMs) {
  float motionEnergy =
      (adxlJerkMagnitude * 0.72f) + (adxlJerkDerivative * 0.28f);
  const float MOTION_THRESHOLD = 0.038f;
  float tremolo = 1.0f + (sinf((float)nowMs * 0.032f) * 0.06f);

  if (motionEnergy < MOTION_THRESHOLD) {
    smoothedDuty *= 0.82f;
    smoothedFreq *= 0.92f;
    if (smoothedDuty < 0.18f || smoothedFreq < 220.0f) {
      buzzerSilence();
      return;
    }
    uint32_t freqHz = (uint32_t)(smoothedFreq + 0.5f);
    if (lastHwFreqHz >= 110 && abs((int)freqHz - (int)lastHwFreqHz) < 8) {
      freqHz = lastHwFreqHz;
    }
    buzzerWriteSmoothPWM(freqHz, smoothedDuty * tremolo);
    return;
  }

  float excessMotion = motionEnergy - MOTION_THRESHOLD;
  float logFactor = logf(1.0f + (excessMotion * 0.24f));

  float tiltMod = (filtGx * 150.0f) + (filtGy * 120.0f);
  float targetFreq = 740.0f + tiltMod + (logFactor * 520.0f);
  targetFreq = constrain(targetFreq, 520.0f, 1720.0f);

  float targetDuty = 1.6f + (logFactor * 7.5f);
  targetDuty = constrain(targetDuty, 1.4f, 6.8f);

  if (smoothedFreq < 200.0f) {
    smoothedFreq = targetFreq;
    smoothedDuty = 0.4f;
  } else {
    smoothedFreq = (smoothedFreq * 0.84f) + (targetFreq * 0.16f);
    smoothedDuty = (smoothedDuty * 0.82f) + (targetDuty * 0.18f);
  }

  uint32_t freqHz = (uint32_t)(smoothedFreq + 0.5f);
  if (lastHwFreqHz >= 110 && abs((int)freqHz - (int)lastHwFreqHz) < 8) {
    freqHz = lastHwFreqHz;
  }

  buzzerWriteSmoothPWM(freqHz, smoothedDuty * tremolo);
}

static void updateSoundEngine(uint32_t nowMs) {
  if (isSoundMuted || !isSystemAwake) {
    buzzerSilence();
    return;
  }

  // 1. Öncelik: Sert sallama onay sesi veya oyun efektleri (SFX)
  if (activeSfx.active) {
    uint32_t el = nowMs - activeSfx.startMs;
    if (el < activeSfx.dur1) {
      playMusicBoxEnvelope(activeSfx.freq1, el, activeSfx.dur1, 4.8f);
      return;
    } else if (activeSfx.dur2 > 0 && el < (activeSfx.dur1 + activeSfx.dur2)) {
      playMusicBoxEnvelope(activeSfx.freq2, el - activeSfx.dur1, activeSfx.dur2,
                           5.2f);
      return;
    } else {
      activeSfx.active = false;
    }
  }

  // Sallama sayımı (2 sn'lik seçim penceresi) esnasında arka plan sesini sustur (karmaşa olmasın)
  if (pendingShakeCount > 0) {
    buzzerSilence();
    return;
  }

  // 2. Öncelik: Oyun Kaybetme / Kazanma Özel Animasyon Sesleri
  if (currentMode == APP_MODE_GAME && gameState != GAME_STATE_PLAYING) {
    uint32_t animEl = nowMs - gameStateStartMs;
    if (gameState == GAME_STATE_LOSE_ANIM) {
      static const uint16_t sadNotes[5] = {1319, 1245, 1175, 988, 784};
      uint32_t step = animEl / 420;
      if (step < 5) {
        playMusicBoxEnvelope(sadNotes[step], animEl % 420, 400, 4.0f);
      } else {
        buzzerWriteSmoothPWM(0, 0.0f);
      }
      return;
    } else if (gameState == GAME_STATE_WIN_ANIM) {
      uint32_t beatPhase = animEl % 750;
      if (beatPhase < 110) {
        playMusicBoxEnvelope(1047, beatPhase, 110, 4.6f);
      } else if (beatPhase >= 150 && beatPhase < 290) {
        playMusicBoxEnvelope(1568, beatPhase - 150, 140, 5.2f);
      } else if (beatPhase >= 340 && beatPhase < 520) {
        playMusicBoxEnvelope(2093, beatPhase - 340, 180, 3.4f);
      } else {
        buzzerWriteSmoothPWM(0, 0.0f);
      }
      return;
    }
  }

  // 3. Öncelik: Arka Plan Aşk Müzikleri (Oyun ve Kayan Yazı Modları)
  if (currentMelody != NULL && currentMelodyLen > 0) {
    const MelodyNote &note = currentMelody[currentNoteIdx];
    uint32_t noteElapsed = nowMs - noteStartMs;
    if (noteElapsed >= note.durMs) {
      currentNoteIdx = (currentNoteIdx + 1) % currentMelodyLen;
      noteStartMs = nowMs;
      noteElapsed = 0;
    }
    const MelodyNote &cur = currentMelody[currentNoteIdx];
    playMusicBoxEnvelope(cur.freqHz, noteElapsed, cur.durMs, 4.2f);
    return;
  }

  // 4. Öncelik: Su Modu İvme Duyarlı Celesta Sesi ("H ♥ T" sabit beklerken sessiz kalır)
  if (currentMode == APP_MODE_FLUID && fluidStage != FLUID_STAGE_HT_HOLD) {
    updateFluidMotionSound(nowMs);
  } else {
    buzzerSilence();
  }
}

// ============================================================================
// 5. DSTK22666 (0.91" 128x32 SSD1306 OLED) DONANIM SÜRÜCÜSÜ & ÇİZİM ARAÇLARI
// ============================================================================
static void oledCommand(uint8_t cmd) {
  Wire.beginTransmission(OLED_ADDR);
  Wire.write(0x00);
  Wire.write(cmd);
  Wire.endTransmission();
}

static void oledInit128x32() {
  const uint8_t initSeq[] = {
      0xAE,       // Display Off
      0xD5, 0xF0, // MAX OSCILLATOR CLOCK (150+ Hz yenileme, sıfır titreme)
      0xA8, 0x1F, // Multiplex 1/32
      0xD3, 0x00, // Display Offset 0
      0x40,       // Start line 0
      0x8D, 0x14, // Charge pump ON
      0x20, 0x00, // Horizontal Addressing Mode
      0xA1,       // Segment Remap
      0xC8,       // COM Output Scan Direction
      0xDA, 0x02, // Sequential COM pins
      0x81, 0xCF, // Contrast Control
      0xD9, 0xF1, // Pre-charge Period
      0xDB, 0x40, // VCOMH Deselect Level
      0xA4,       // Resume to RAM content
      0xA6,       // Normal Display
      0xAF        // Display ON
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

static uint32_t currentI2cClockHz = 400000UL;

static inline void setI2cClockHz(uint32_t clockHz) {
  if (currentI2cClockHz != clockHz) {
    Wire.setClock(clockHz);
    currentI2cClockHz = clockHz;
  }
}

static void oledFlush() {
  if (currentI2cClockHz != 800000UL) {
    setI2cClockHz(800000UL);
  }

  const uint8_t windowCmds[] = {0x21, 0x00, 0x7F, 0x22, 0x00, 0x03};
  Wire.beginTransmission(OLED_ADDR);
  Wire.write(0x00);
  Wire.write(windowCmds, sizeof(windowCmds));
  Wire.endTransmission();

  Wire.beginTransmission(OLED_ADDR);
  Wire.write(0x40);
  Wire.write(oledBuffer, 512);
  Wire.endTransmission();
}

static inline void oledSetPixel(int x, int y) {
  if ((unsigned)x < SCREEN_W && (unsigned)y < SCREEN_H) {
    oledBuffer[x + ((y >> 3) << 7)] |= (1 << (y & 7));
  }
}

static inline void oledClearPixel(int x, int y) {
  if ((unsigned)x < SCREEN_W && (unsigned)y < SCREEN_H) {
    oledBuffer[x + ((y >> 3) << 7)] &= ~(1 << (y & 7));
  }
}

static void drawMiniHeart(int cx, int cy) {
  static const uint8_t rows[6] = {
      0b0110110, // .##.##.
      0b1111111, // #######
      0b1111111, // #######
      0b0111110, // .#####.
      0b0011100, // ..###..
      0b0001000  // ...#...
  };
  for (int r = 0; r < 6; r++) {
    uint8_t bits = rows[r];
    for (int c = 0; c < 7; c++) {
      if (bits & (1 << (6 - c))) {
        oledSetPixel(cx - 3 + c, cy - 3 + r);
      }
    }
  }
}

// ============================================================================
// 6. GY-291 (ADXL345) SÜRÜCÜSÜ VE AKILLI SALLAMA / EĞİM AYRIŞTIRICI
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

  // DC-coupled Activity & Inactivity (0x77):
  // Activity (28 * 62.5 mg = 1.75 G): 1G yerçekimi/eğim eşiği aşamaz, sert sallamayı yakalar.
  // Inactivity (24 * 62.5 mg = 1.50 G): 1G sabit duruşta 2 sn sonra AUTO_SLEEP'e sokar.
  adxlWriteReg(0x24, 28);   // THRESH_ACT = 1.75 G
  adxlWriteReg(0x25, 24);   // THRESH_INACT = 1.50 G
  adxlWriteReg(0x26, 0x02); // TIME_INACT = 2 sn
  adxlWriteReg(0x27, 0x77); // DC-coupled Activity X,Y,Z + DC-coupled Inactivity X,Y,Z
  adxlWriteReg(0x2E, 0x18); // Activity (0x10) + Inactivity (0x08) interrupt latch

  if (lowPowerWatchMode) {
    adxlWriteReg(0x2C, 0x19); // Low-power 50 Hz (~40 µA, AUTO_SLEEP'te 8 Hz)
    adxlWriteReg(0x2D, 0x38); // LINK (0x20) | AUTO_SLEEP (0x10) | Measure (0x08)
  } else {
    adxlWriteReg(0x2C, 0x0C); // Aktif 400 Hz
    adxlWriteReg(0x2D, 0x08); // Measure (0x08)
  }
  (void)adxlReadReg(0x30);
}

static bool adxl345CheckEcoWake() {
  uint8_t intSource = adxlReadReg(0x30);
  bool hwActivityTriggered = (intSource & 0x10) != 0;

  Wire.beginTransmission(ADXL345_ADDR);
  Wire.write(0x32);
  if (Wire.endTransmission(false) == 0 &&
      Wire.requestFrom((uint8_t)ADXL345_ADDR, (uint8_t)6, (uint8_t)true) == 6) {
    int16_t rawAx = (int16_t)(Wire.read() | (Wire.read() << 8));
    int16_t rawAy = (int16_t)(Wire.read() | (Wire.read() << 8));
    int16_t rawAz = (int16_t)(Wire.read() | (Wire.read() << 8));

    float ax = (float)rawAx * 0.00390625f;
    float ay = (float)rawAy * 0.00390625f;
    float az = (float)rawAz * 0.00390625f;
    float totalG = sqrtf(ax * ax + ay * ay + az * az);

    if (totalG >= SHAKE_WAKE_THRESHOLD_G || hwActivityTriggered) {
      Serial.printf("[EKO-UYANIS] Sert sallama algilandi (%.2f G, HW:%u)!\n",
                    totalG, (unsigned)hwActivityTriggered);
      return true;
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

    static float prevAx = 0.0f;
    static float prevAy = 0.0f;
    static float prevAz = 1.0f;
    static float prevJerkMag = 0.0f;

    float gx = SWAP_XY_AXES ? ay : ax;
    float gy = SWAP_XY_AXES ? ax : ay;

    gx *= INVERT_GRAVITY_X;
    gy *= INVERT_GRAVITY_Y;

    if (adxlFirstSampleAfterWake) {
      prevAx = ax;
      prevAy = ay;
      prevAz = az;
      prevJerkMag = 0.0f;
      adxlJerkMagnitude = 0.0f;
      adxlJerkDerivative = 0.0f;
      filtGx = gx;
      filtGy = gy;
      adxlFirstSampleAfterWake = false;
    }

    float dAx = ax - prevAx;
    float dAy = ay - prevAy;
    float dAz = az - prevAz;
    prevAx = ax;
    prevAy = ay;
    prevAz = az;

    float currentJerk = sqrtf(dAx * dAx + dAy * dAy + dAz * dAz);
    float dJerk = fabsf(currentJerk - prevJerkMag);
    prevJerkMag = currentJerk;

    adxlJerkMagnitude = (adxlJerkMagnitude * 0.30f) + (currentJerk * 0.70f);
    adxlJerkDerivative = (adxlJerkDerivative * 0.30f) + (dJerk * 0.70f);

    rawGVecX = gx;
    rawGVecY = gy;

    currentTotalG = sqrtf(ax * ax + ay * ay + az * az);
    float instantShake = fabsf(currentTotalG - 1.0f);
    shakeMagnitude = (shakeMagnitude * 0.70f) + (instantShake * 0.30f);

    float alpha = (shakeMagnitude > 0.15f) ? 0.25f : 0.14f;
    filtGx = (filtGx * (1.0f - alpha)) + (gx * alpha);
    filtGy = (filtGy * (1.0f - alpha)) + (gy * alpha);

    float dynAx = gx - filtGx;
    float dynAy = gy - filtGy;

    effGx = filtGx - (dynAx * INERTIA_BOOST);
    effGy = filtGy - (dynAy * INERTIA_BOOST);

    float effMagSq = effGx * effGx + effGy * effGy;
    if (effMagSq > 10.24f) {
      float s = 3.2f * (1.0f / sqrtf(effMagSq));
      effGx *= s;
      effGy *= s;
    }
  }
}

// ============================================================================
// 7. "H ♥ T" DİZİLİMİ, RAGDOLL GEÇİŞİ VE GERÇEKÇİ PBD + XSPH SU MOTORU
// ============================================================================
static const uint16_t HEART_TEMPLATE_15X12[12] = {
    0b001110000011100, // 6
    0b011111000111110, // 10
    0b111111101111111, // 14
    0b111111111111111, // 15
    0b111111111111111, // 15
    0b011111111111110, // 13
    0b001111111111100, // 11
    0b000111111111000, // 9
    0b000011111110000, // 7
    0b000001111100000, // 5
    0b000000111000000, // 3
    0b000000010000000  // 1 -> Toplam = 109
};

static void initHTFormation() {
  int idx = 0;

  // 1. 'H' Harfi (Küme 0: 71 tanecik)
  for (int r = 0; r < 14 && idx < NUM_PARTICLES; r++) {
    for (int c = 0; c < 2 && idx < NUM_PARTICLES; c++) {
      particles[idx].homeX = 21.6f + (float)c * 1.6f;
      particles[idx].homeY = 6.5f + (float)r * 1.35f;
      particles[idx].clusterId = 0;
      idx++;
    }
  }
  for (int r = 0; r < 14 && idx < NUM_PARTICLES; r++) {
    for (int c = 0; c < 2 && idx < NUM_PARTICLES; c++) {
      particles[idx].homeX = 34.4f + (float)c * 1.6f;
      particles[idx].homeY = 6.5f + (float)r * 1.35f;
      particles[idx].clusterId = 0;
      idx++;
    }
  }
  for (int r = 0; r < 3 && idx < NUM_PARTICLES; r++) {
    for (int c = 0; c < 5 && idx < NUM_PARTICLES; c++) {
      particles[idx].homeX = 25.4f + (float)c * 1.8f;
      particles[idx].homeY = 14.0f + (float)r * 1.35f;
      particles[idx].clusterId = 0;
      idx++;
    }
  }

  // 2. '♥' Kalp Sembolü (Küme 1: 109 tanecik)
  for (int r = 0; r < 12 && idx < NUM_PARTICLES; r++) {
    uint16_t rowMask = HEART_TEMPLATE_15X12[r];
    for (int c = 0; c < 15 && idx < NUM_PARTICLES; c++) {
      if (rowMask & (1 << (14 - c))) {
        particles[idx].homeX = 64.0f + (float)(c - 7) * 1.45f;
        particles[idx].homeY = 6.8f + (float)r * 1.50f;
        particles[idx].clusterId = 1;
        idx++;
      }
    }
  }

  // 3. 'T' Harfi (Küme 2: 60 tanecik)
  for (int r = 0; r < 3 && idx < NUM_PARTICLES; r++) {
    for (int c = 0; c < 12 && idx < NUM_PARTICLES; c++) {
      particles[idx].homeX = 89.0f + (float)c * 1.45f;
      particles[idx].homeY = 6.5f + (float)r * 1.40f;
      particles[idx].clusterId = 2;
      idx++;
    }
  }
  for (int r = 0; r < 12 && idx < NUM_PARTICLES; r++) {
    for (int c = 0; c < 2 && idx < NUM_PARTICLES; c++) {
      particles[idx].homeX = 96.2f + (float)c * 1.6f;
      particles[idx].homeY = 10.8f + (float)r * 1.18f;
      particles[idx].clusterId = 2;
      idx++;
    }
  }

  for (int i = 0; i < NUM_PARTICLES; i++) {
    particles[i].x = particles[i].homeX;
    particles[i].y = particles[i].homeY;
    particles[i].px = particles[i].homeX;
    particles[i].py = particles[i].homeY;
  }

  for (int c = 0; c < 3; c++) {
    ragDx[c] = 0.0f;
    ragDy[c] = 0.0f;
    ragVx[c] = 0.0f;
    ragVy[c] = 0.0f;
    ragAngle[c] = 0.0f;
    ragOmega[c] = 0.0f;
  }

  fluidStage = FLUID_STAGE_HT_HOLD;
  fluidStageStartMs = millis();
}

static void triggerKineticSplash(float intensity) {
  if (fluidStage != FLUID_STAGE_WATER) {
    fluidStage = FLUID_STAGE_WATER;
  }
  float angle = atan2f(effGy, effGx);
  float splashAngle = angle + PI;

  for (int i = 0; i < NUM_PARTICLES; i++) {
    if ((fastRand() % 100) < (int)(intensity * 100.0f)) {
      float randSpread = ((float)(fastRand() % 200) - 100.0f) * 0.008f;
      float pAngle = splashAngle + randSpread;
      float speed = 1.6f + ((float)(fastRand() % 100) * 0.036f * intensity);

      particles[i].px = particles[i].x - cosf(pAngle) * speed;
      particles[i].py = particles[i].y - sinf(pAngle) * speed;
    }
  }
}

static inline void applyCapsuleBoundary(FluidParticle &p,
                                        bool reflectVelocity = false) {
  float vx = p.x - p.px;
  float vy = p.y - p.py;

  float cx = 0.0f, cy = 0.0f;
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
      float invDist = 1.0f / sqrtf(distSq);
      float nx = dx * invDist;
      float ny = dy * invDist;
      p.x = cx + nx * CAPSULE_CORNER_R;
      p.y = cy + ny * CAPSULE_CORNER_R;
      if (reflectVelocity) {
        float vn = vx * nx + vy * ny;
        if (vn > 0.0f) {
          vx -= (1.35f * vn) * nx;
          vy -= (1.35f * vn) * ny;
        }
        p.px = p.x - vx;
        p.py = p.y - vy;
      }
    }
    return;
  }

  if (p.x < CAPSULE_MIN_X) {
    p.x = CAPSULE_MIN_X;
    if (reflectVelocity && vx < 0.0f) {
      p.px = p.x + vx * 0.35f;
    }
  } else if (p.x > CAPSULE_MAX_X) {
    p.x = CAPSULE_MAX_X;
    if (reflectVelocity && vx > 0.0f) {
      p.px = p.x + vx * 0.35f;
    }
  }

  if (p.y < CAPSULE_MIN_Y) {
    p.y = CAPSULE_MIN_Y;
    if (reflectVelocity && vy < 0.0f) {
      p.py = p.y + vy * 0.35f;
    }
  } else if (p.y > CAPSULE_MAX_Y) {
    p.y = CAPSULE_MAX_Y;
    if (reflectVelocity && vy > 0.0f) {
      p.py = p.y + vy * 0.35f;
    }
  }
}

static const int8_t NEIGHBOR_DIRS[5][2] = {
    {0, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}};

static void stepFluidPhysics(uint32_t nowMs) {
  if (fluidStage == FLUID_STAGE_HT_HOLD && pendingShakeCount >= 2) {
    fluidStageStartMs = nowMs;
  }
  uint32_t stageElapsed = nowMs - fluidStageStartMs;

  // EVRE 0: İlk ~1.8 sn "H ♥ T" tamamen hareketsiz durur
  if (fluidStage == FLUID_STAGE_HT_HOLD) {
    if (stageElapsed < HT_HOLD_DURATION_MS) {
      return;
    }
    fluidStage = FLUID_STAGE_HT_RAGDOLL;
    fluidStageStartMs = nowMs;
    stageElapsed = 0;
  }

  float gravityScale = 1.0f;
  float pbdStrength = 1.0f;

  // EVRE 1: Yavaş yavaş esnek Ragdoll hareketi (~2.2 sn) ve suya erime
  if (fluidStage == FLUID_STAGE_HT_RAGDOLL) {
    if (stageElapsed >= HT_RAGDOLL_DURATION_MS) {
      fluidStage = FLUID_STAGE_WATER;
    } else {
      float ragdollProgress =
          (float)stageElapsed / (float)HT_RAGDOLL_DURATION_MS;
      float swayRamp = fminf(ragdollProgress / 0.65f, 1.0f);
      float swayEnv = sinf(swayRamp * 1.570796f);

      float meltProg = (ragdollProgress > 0.52f)
                           ? ((ragdollProgress - 0.52f) / 0.48f)
                           : 0.0f;
      float meltEase = meltProg * meltProg * (3.0f - 2.0f * meltProg);
      gravityScale = meltEase;
      pbdStrength = meltEase;

      for (int c = 0; c < 3; c++) {
        float phase = (float)c * 1.35f;
        float targetDx = (sinf(ragdollProgress * 9.5f + phase) * 3.2f +
                          effGx * 2.5f) *
                         swayEnv;
        float targetDy = (cosf(ragdollProgress * 7.5f + phase) * 2.2f +
                          (effGy - 0.5f) * 2.2f) *
                         swayEnv;
        float targetAng = (sinf(ragdollProgress * 8.5f + phase) * 0.24f +
                           effGx * 0.15f) *
                          swayEnv;

        ragVx[c] = (ragVx[c] * 0.82f) + (targetDx - ragDx[c]) * 0.14f;
        ragVy[c] = (ragVy[c] * 0.82f) + (targetDy - ragDy[c]) * 0.14f;
        ragDx[c] = constrain(ragDx[c] + ragVx[c], -5.5f, 5.5f);
        ragDy[c] = constrain(ragDy[c] + ragVy[c], -4.0f, 5.0f);

        ragOmega[c] = (ragOmega[c] * 0.82f) + (targetAng - ragAngle[c]) * 0.15f;
        ragAngle[c] = constrain(ragAngle[c] + ragOmega[c], -0.35f, 0.35f);
      }

      float tether = 1.0f - meltEase;
      if (tether > 0.02f) {
        float springK = 0.28f * tether;
        float velDamp = 1.0f - (0.65f * tether);
        float cosA[3], sinA[3];
        for (int c = 0; c < 3; c++) {
          cosA[c] = cosf(ragAngle[c]);
          sinA[c] = sinf(ragAngle[c]);
        }

        for (int i = 0; i < NUM_PARTICLES; i++) {
          uint8_t cid = particles[i].clusterId;
          float lx = particles[i].homeX - CLUSTER_REST_CX[cid];
          float ly = particles[i].homeY - CLUSTER_REST_CY[cid];
          float bendX =
              (ly * 0.12f) * sinf(ragdollProgress * 11.0f + (float)cid) * swayEnv;
          float targetX = CLUSTER_REST_CX[cid] + ragDx[cid] +
                          (lx * cosA[cid] - ly * sinA[cid]) + bendX;
          float targetY = CLUSTER_REST_CY[cid] + ragDy[cid] +
                          (lx * sinA[cid] + ly * cosA[cid]);

          float oldX = particles[i].x;
          float oldY = particles[i].y;
          particles[i].x += (targetX - oldX) * springK;
          particles[i].y += (targetY - oldY) * springK;
          float vx = (oldX - particles[i].px) * velDamp;
          float vy = (oldY - particles[i].py) * velDamp;
          particles[i].px = particles[i].x - vx;
          particles[i].py = particles[i].y - vy;
        }
      }
    }
  } else {
    if (shakeMagnitude > 0.45f) {
      triggerKineticSplash(fminf(shakeMagnitude * 0.60f, 1.0f));
    }
  }

  float ax = effGx * GRAVITY_ACCEL * gravityScale;
  float ay = effGy * GRAVITY_ACCEL * gravityScale;

  // 1. Verlet Entegrasyonu, Sınır Çarpışması ve Hız Yansıması
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

    applyCapsuleBoundary(particles[i], true);
  }

  if (pbdStrength <= 0.005f) {
    return;
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

  // 3. PBD + C2 Yüzey Gerilimi + XSPH Laminer Viskozite Çözücü
  const float REST_DIST = PARTICLE_REST_DIST;
  const float REST_DIST_SQ = PARTICLE_REST_DIST_SQ;
  const float COHES_RADIUS = COHESION_RADIUS;
  const float COHES_RADIUS_SQ = COHESION_RADIUS_SQ;
  const float INV_COHES_RADIUS_SQ = 1.0f / COHESION_RADIUS_SQ;

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
            float ppxA = particles[i].px;
            float ppyA = particles[i].py;
            uint8_t startB = isSameCell ? (a + 1) : 0;

            for (uint8_t b = startB; b < countB; b++) {
              uint8_t j = gridParticles[ny][nx][b];

              float diffX = pxA - particles[j].x;
              if (diffX > COHES_RADIUS || diffX < -COHES_RADIUS)
                continue;

              float diffY = pyA - particles[j].y;
              if (diffY > COHES_RADIUS || diffY < -COHES_RADIUS)
                continue;

              float distSq = diffX * diffX + diffY * diffY;
              if (distSq >= COHES_RADIUS_SQ || distSq < 0.0001f)
                continue;

              if (iter == 0) {
                float vix = pxA - ppxA;
                float viy = pyA - ppyA;
                float vjx = particles[j].x - particles[j].px;
                float vjy = particles[j].y - particles[j].py;
                float visc = XSPH_VISCOSITY * pbdStrength *
                             (1.0f - distSq * INV_COHES_RADIUS_SQ);
                float dvx = (vjx - vix) * visc;
                float dvy = (vjy - viy) * visc;
                ppxA -= dvx;
                ppyA -= dvy;
                particles[j].px += dvx;
                particles[j].py += dvy;
              }

              if (distSq < REST_DIST_SQ) {
                float invDist = 1.0f / sqrtf(distSq);
                float dist = distSq * invDist;
                float nxRay = diffX * invDist;
                float nyRay = diffY * invDist;

                float delta = (REST_DIST - dist) * 0.48f * pbdStrength;
                pxA += nxRay * delta;
                pyA += nyRay * delta;
                particles[j].x -= nxRay * delta;
                particles[j].y -= nyRay * delta;
              } else {
                float falloff =
                    (COHES_RADIUS_SQ - distSq) * (distSq - REST_DIST_SQ);
                float pull = falloff * 0.0042f * pbdStrength;
                pxA -= diffX * pull;
                pyA -= diffY * pull;
                particles[j].x += diffX * pull;
                particles[j].y += diffY * pull;
              }
            }

            particles[i].x = pxA;
            particles[i].y = pyA;
            if (iter == 0) {
              particles[i].px = ppxA;
              particles[i].py = ppyA;
            }
          }
        }
      }
    }

    for (int i = 0; i < NUM_PARTICLES; i++) {
      applyCapsuleBoundary(particles[i], false);
    }
  }
}

// ============================================================================
// 8. SALLAMA DURUM GÖSTERGESİ VE SU MODU EKRAN ÇİZİM MOTORU
// ============================================================================
static void drawCapsuleFrame(uint32_t frameCounter) {
  for (int x = 4; x < SCREEN_W - 4; x++) {
    oledBuffer[x] |= 0x02;            // Y = 1
    oledBuffer[x + (3 << 7)] |= 0x40; // Y = 30
  }
  for (int y = 4; y < SCREEN_H - 4; y++) {
    oledSetPixel(1, y);
    oledSetPixel(SCREEN_W - 2, y);
  }

  oledSetPixel(2, 3);
  oledSetPixel(3, 2);
  oledSetPixel(SCREEN_W - 3, 3);
  oledSetPixel(SCREEN_W - 4, 2);
  oledSetPixel(2, SCREEN_H - 4);
  oledSetPixel(3, SCREEN_H - 3);
  oledSetPixel(SCREEN_W - 3, SCREEN_H - 4);
  oledSetPixel(SCREEN_W - 4, SCREEN_H - 3);

  oledBuffer[SCREEN_W / 4] |= 0x01;
  oledBuffer[SCREEN_W / 2] |= 0x01;
  oledBuffer[(SCREEN_W * 3) / 4] |= 0x01;

  if ((frameCounter % 120) < 14) {
    oledSetPixel(SCREEN_W - 6, 4);
    oledSetPixel(SCREEN_W - 7, 4);
    oledSetPixel(SCREEN_W - 6, 5);
    oledSetPixel(SCREEN_W - 7, 5);
  }
}

static void drawShakeOverlayHUD(uint32_t nowMs) {
  if (pendingShakeCount == 0)
    return;
  if (wokeOnFirstShake && pendingShakeCount == 1 &&
      currentMode == APP_MODE_FLUID && fluidStage == FLUID_STAGE_HT_HOLD)
    return;
  uint32_t el = nowMs - lastShakeDetectedMs;
  if (el >= SHAKE_LOCK_WAIT_MS)
    return;

  for (int y = 2; y <= 11; y++) {
    for (int x = 45; x <= 83; x++) {
      if (y == 2 || y == 11 || x == 45 || x == 83) {
        oledSetPixel(x, y);
      } else {
        oledClearPixel(x, y);
      }
    }
  }

  int startX = 64 - ((pendingShakeCount - 1) * 5);
  for (uint8_t i = 0; i < pendingShakeCount; i++) {
    drawMiniHeart(startX + i * 10, 6);
  }
}

static void renderUltraFluid(uint32_t frameCounter, uint32_t nowMs) {
  memset(oledBuffer, 0, sizeof(oledBuffer));

  for (int i = 0; i < NUM_PARTICLES; i++) {
    int cx = (int)(particles[i].x + 0.5f);
    int cy = (int)(particles[i].y + 0.5f);

    if (cx < 0)
      cx = 0;
    else if (cx > SCREEN_W - 2)
      cx = SCREEN_W - 2;
    if (cy < 0)
      cy = 0;
    else if (cy > SCREEN_H - 2)
      cy = SCREEN_H - 2;

    int page = cy >> 3;
    int bitInPage = cy & 7;
    int colIdx = cx + (page << 7);

    if (bitInPage < 7) {
      uint8_t mask = (3 << bitInPage);
      oledBuffer[colIdx] |= mask;
      oledBuffer[colIdx + 1] |= mask;
    } else if (page < 3) {
      oledBuffer[colIdx] |= 0x80;
      oledBuffer[colIdx + 1] |= 0x80;
      oledBuffer[colIdx + 128] |= 0x01;
      oledBuffer[colIdx + 129] |= 0x01;
    } else {
      oledBuffer[colIdx] |= 0x80;
      oledBuffer[colIdx + 1] |= 0x80;
    }
  }

  drawCapsuleFrame(frameCounter);
  drawShakeOverlayHUD(nowMs);
  oledFlush();
}

// ============================================================================
// 9. MOD 2 (2x SALLAMA): ADXL345 "FLYING HEART" OYUNU & KALP ANİMASYONLARI
// ============================================================================
static void initFlyingHeartRound() {
  gameState = GAME_STATE_PLAYING;
  gameStateStartMs = millis();
  gameHeartY = 16.0f;
  gameHeartVy = 0.0f;
  gameScore = 0;

  gameNeutralGx = constrain(filtGx, -0.85f, 0.85f);
  gameNeutralGy = constrain(filtGy, -0.85f, 0.85f);

  for (int i = 0; i < GAME_OBSTACLE_COUNT; i++) {
    gameObstacles[i].x = 96.0f + (float)i * GAME_OBSTACLE_SPACING;
    gameObstacles[i].gapCenterY = 10.0f + (float)(fastRand() % 13);
    gameObstacles[i].passed = false;
  }
}

static void drawScaledBigHeart(int centerX, int centerY, float scale,
                               int splitOffsetPx, float crackProgress) {
  static const uint32_t BIG_HEART_19X15[15] = {
      0b0001111000001111000, // 0
      0b0011111100011111100, // 1
      0b0111111110111111110, // 2
      0b1111111111111111111, // 3
      0b1111111111111111111, // 4
      0b1111111111111111111, // 5
      0b0111111111111111110, // 6
      0b0011111111111111100, // 7
      0b0001111111111111000, // 8
      0b0000111111111110000, // 9
      0b0000011111111100000, // 10
      0b0000001111111000000, // 11
      0b0000000111110000000, // 12
      0b0000000011100000000, // 13
      0b0000000001000000000  // 14
  };

  static const int8_t CRACK_ZIGZAG[15] = {0,  -1, -2, -1, 0,  1,  2, 1,
                                          0,  -1, -2, -1, 0,  1,  0};

  int maxCrackRow = (int)(crackProgress * 15.0f);

  for (int r = 0; r < 15; r++) {
    uint32_t rowMask = BIG_HEART_19X15[r];
    int crackCol = 9 + CRACK_ZIGZAG[r];

    int py0 = centerY + (int)roundf(((float)r - 7.5f) * scale);
    int py1 = centerY + (int)roundf(((float)r - 6.5f) * scale) - 1;
    if (py1 < py0)
      py1 = py0;

    for (int c = 0; c < 19; c++) {
      if (!(rowMask & (1UL << (18 - c))))
        continue;

      if (r < maxCrackRow && abs(c - crackCol) <= 1) {
        continue;
      }

      int sideShift = 0;
      int dropShift = 0;
      if (splitOffsetPx > 0) {
        if (c < crackCol) {
          sideShift = -splitOffsetPx;
          dropShift = (splitOffsetPx >> 1) + ((9 - c) * splitOffsetPx) / 14;
        } else {
          sideShift = splitOffsetPx;
          dropShift = (splitOffsetPx >> 1) + ((c - 9) * splitOffsetPx) / 14;
        }
      }

      int px0 = centerX + sideShift + (int)roundf(((float)c - 9.5f) * scale);
      int px1 =
          centerX + sideShift + (int)roundf(((float)c - 8.5f) * scale) - 1;
      if (px1 < px0)
        px1 = px0;

      for (int y = py0; y <= py1; y++) {
        for (int x = px0; x <= px1; x++) {
          oledSetPixel(x, y + dropShift);
        }
      }
    }
  }
}

static void stepAndRenderFlyingHeartGame(uint32_t frameCounter, uint32_t nowMs) {
  memset(oledBuffer, 0, sizeof(oledBuffer));

  if (gameState == GAME_STATE_PLAYING) {
    float dyTilt = filtGy - gameNeutralGy;
    float dxTilt = filtGx - gameNeutralGx;
    float deltaTilt = dyTilt + (dxTilt * 0.50f);
    if (fabsf(deltaTilt) > 0.04f) {
      lastWakeTimeMs = nowMs;
    }

    if (pendingShakeCount == 0) {
      float targetY = 16.0f + (deltaTilt * 28.0f);
      targetY = constrain(targetY, 5.0f, 26.0f);

      gameHeartVy = (gameHeartVy * 0.42f) + ((targetY - gameHeartY) * 0.22f);
      gameHeartY = constrain(gameHeartY + gameHeartVy, 4.0f, 27.0f);

      for (int i = 0; i < GAME_OBSTACLE_COUNT; i++) {
        gameObstacles[i].x -= 0.78f;

        float obsLeft = gameObstacles[i].x - 3.0f;
        float obsRight = gameObstacles[i].x + 3.0f;
        float gapTop = gameObstacles[i].gapCenterY - (GAME_GAP_HEIGHT * 0.5f);
        float gapBot = gameObstacles[i].gapCenterY + (GAME_GAP_HEIGHT * 0.5f);

        if (obsRight >= (GAME_HEART_X - 2.2f) &&
            obsLeft <= (GAME_HEART_X + 2.2f)) {
          if ((gameHeartY - 2.2f) < gapTop || (gameHeartY + 2.2f) > gapBot) {
            activeSfx.active = false;
            gameState = GAME_STATE_LOSE_ANIM;
            gameStateStartMs = nowMs;
            break;
          }
        }

        if (!gameObstacles[i].passed && obsRight < (GAME_HEART_X - 3.0f)) {
          gameObstacles[i].passed = true;
          gameScore++;
          triggerSweetSfx(1568, 40, 2093, 60);

          if (gameScore >= GAME_TARGET_WINS) {
            gameState = GAME_STATE_WIN_ANIM;
            gameStateStartMs = nowMs;
            break;
          }
        }

        if (gameObstacles[i].x < -6.0f) {
          int otherIdx = (i + 1) % GAME_OBSTACLE_COUNT;
          gameObstacles[i].x =
              gameObstacles[otherIdx].x + GAME_OBSTACLE_SPACING;
          gameObstacles[i].gapCenterY = 9.0f + (float)(fastRand() % 15);
          gameObstacles[i].passed = false;
        }
      }
    }

    for (int x = 0; x < SCREEN_W; x += 2) {
      oledSetPixel(x, 0);
      oledSetPixel(x, SCREEN_H - 1);
    }

    for (int s = 0; s < GAME_TARGET_WINS; s++) {
      int sx = 4 + s * 4;
      oledSetPixel(sx, 2);
      oledSetPixel(sx + 1, 2);
      if (s < gameScore) {
        oledSetPixel(sx, 3);
        oledSetPixel(sx + 1, 3);
      }
    }

    for (int i = 0; i < GAME_OBSTACLE_COUNT; i++) {
      int ox = (int)roundf(gameObstacles[i].x);
      int gapTop =
          (int)roundf(gameObstacles[i].gapCenterY - (GAME_GAP_HEIGHT * 0.5f));
      int gapBot =
          (int)roundf(gameObstacles[i].gapCenterY + (GAME_GAP_HEIGHT * 0.5f));

      for (int x = ox - 2; x <= ox + 2; x++) {
        for (int y = 1; y < gapTop; y++) {
          oledSetPixel(x, y);
        }
        for (int y = gapBot + 1; y < SCREEN_H - 1; y++) {
          oledSetPixel(x, y);
        }
      }
      for (int x = ox - 3; x <= ox + 3; x++) {
        if (gapTop >= 2)
          oledSetPixel(x, gapTop - 1);
        if (gapBot < SCREEN_H - 2)
          oledSetPixel(x, gapBot + 1);
      }
    }

    int hx = (int)GAME_HEART_X;
    int hy = (int)roundf(gameHeartY);
    drawMiniHeart(hx, hy);
    if ((frameCounter & 4) != 0) {
      oledSetPixel(hx - 5, hy - 2);
      oledSetPixel(hx - 6, hy - 3);
    } else {
      oledSetPixel(hx - 5, hy);
      oledSetPixel(hx - 6, hy + 1);
    }
  } else if (gameState == GAME_STATE_LOSE_ANIM) {
    uint32_t animEl = nowMs - gameStateStartMs;
    if (animEl >= GAME_ANIM_DURATION_MS) {
      initFlyingHeartRound();
    } else {
      float crackProg = 0.0f;
      int splitPx = 0;
      int shakeX = 0;

      if (animEl < 650) {
        shakeX = ((animEl / 45) & 1) ? 1 : -1;
      } else if (animEl < 1550) {
        crackProg = (float)(animEl - 650) / 900.0f;
        shakeX = ((animEl / 35) & 1) ? 1 : -1;
      } else {
        crackProg = 1.0f;
        splitPx = 1 + (int)((animEl - 1550) / 180);
        if (splitPx > 10)
          splitPx = 10;
      }

      drawScaledBigHeart(64 + shakeX, 16, 1.45f, splitPx, crackProg);

      if (splitPx > 0) {
        for (int p = 0; p < 6; p++) {
          int fx = 54 + p * 4 + ((p & 1) ? splitPx : -splitPx);
          int fy = 14 + ((int)(animEl - 1550) / (70 + p * 15)) + (p * 2);
          oledSetPixel(fx, fy);
          oledSetPixel(fx + 1, fy);
        }
      }
    }
  } else if (gameState == GAME_STATE_WIN_ANIM) {
    uint32_t animEl = nowMs - gameStateStartMs;
    if (animEl >= GAME_ANIM_DURATION_MS) {
      initFlyingHeartRound();
    } else {
      uint32_t beatPhase = animEl % 750;
      float scale = 1.25f;
      if (beatPhase < 110) {
        scale = 1.48f;
      } else if (beatPhase >= 150 && beatPhase < 280) {
        scale = 1.65f;
      } else if (beatPhase >= 280 && beatPhase < 450) {
        scale = 1.65f - ((float)(beatPhase - 280) / 170.0f) * 0.40f;
      }

      drawScaledBigHeart(64, 16, scale, 0, 0.0f);

      int floatY1 = 26 - (int)((animEl / 70) % 22);
      int floatY2 = 26 - (int)(((animEl + 350) / 70) % 22);
      drawMiniHeart(24, floatY1);
      drawMiniHeart(104, floatY2);
      drawMiniHeart(14, floatY2);
      drawMiniHeart(114, floatY1);
    }
  }

  drawShakeOverlayHUD(nowMs);
  oledFlush();
}

// ============================================================================
// 10. MOD 3 (3x SALLAMA): "TUFAN HİLALİ ÇOK SEVİYOR" KAYAN YAZI ŞERİDİ
// ============================================================================
static void getGlyph5x8(char code, uint8_t outCols[5]) {
  switch (code) {
  case 'T':
    outCols[0] = 0x02;
    outCols[1] = 0x02;
    outCols[2] = 0x7E;
    outCols[3] = 0x02;
    outCols[4] = 0x02;
    break;
  case 'U':
    outCols[0] = 0x3E;
    outCols[1] = 0x40;
    outCols[2] = 0x40;
    outCols[3] = 0x40;
    outCols[4] = 0x3E;
    break;
  case 'F':
    outCols[0] = 0x7E;
    outCols[1] = 0x12;
    outCols[2] = 0x12;
    outCols[3] = 0x02;
    outCols[4] = 0x02;
    break;
  case 'A':
    outCols[0] = 0x7C;
    outCols[1] = 0x12;
    outCols[2] = 0x12;
    outCols[3] = 0x12;
    outCols[4] = 0x7C;
    break;
  case 'N':
    outCols[0] = 0x7E;
    outCols[1] = 0x08;
    outCols[2] = 0x10;
    outCols[3] = 0x20;
    outCols[4] = 0x7E;
    break;
  case 'H':
    outCols[0] = 0x7E;
    outCols[1] = 0x10;
    outCols[2] = 0x10;
    outCols[3] = 0x10;
    outCols[4] = 0x7E;
    break;
  case 'I': // Türkçe Büyük 'İ'
    outCols[0] = 0x00;
    outCols[1] = 0x44;
    outCols[2] = 0x7D;
    outCols[3] = 0x44;
    outCols[4] = 0x00;
    break;
  case 'L':
    outCols[0] = 0x7E;
    outCols[1] = 0x40;
    outCols[2] = 0x40;
    outCols[3] = 0x40;
    outCols[4] = 0x40;
    break;
  case 'C': // Türkçe Büyük 'Ç'
    outCols[0] = 0x3C;
    outCols[1] = 0x42;
    outCols[2] = 0xC2;
    outCols[3] = 0x42;
    outCols[4] = 0x24;
    break;
  case 'O':
    outCols[0] = 0x3C;
    outCols[1] = 0x42;
    outCols[2] = 0x42;
    outCols[3] = 0x42;
    outCols[4] = 0x3C;
    break;
  case 'K':
    outCols[0] = 0x7E;
    outCols[1] = 0x10;
    outCols[2] = 0x28;
    outCols[3] = 0x44;
    outCols[4] = 0x42;
    break;
  case 'S':
    outCols[0] = 0x24;
    outCols[1] = 0x4A;
    outCols[2] = 0x4A;
    outCols[3] = 0x4A;
    outCols[4] = 0x30;
    break;
  case 'E':
    outCols[0] = 0x7E;
    outCols[1] = 0x4A;
    outCols[2] = 0x4A;
    outCols[3] = 0x42;
    outCols[4] = 0x42;
    break;
  case 'V':
    outCols[0] = 0x1E;
    outCols[1] = 0x20;
    outCols[2] = 0x40;
    outCols[3] = 0x20;
    outCols[4] = 0x1E;
    break;
  case 'Y':
    outCols[0] = 0x06;
    outCols[1] = 0x08;
    outCols[2] = 0x70;
    outCols[3] = 0x08;
    outCols[4] = 0x06;
    break;
  case 'R':
    outCols[0] = 0x7E;
    outCols[1] = 0x12;
    outCols[2] = 0x32;
    outCols[3] = 0x52;
    outCols[4] = 0x4C;
    break;
  case '*': // Kalp '♥' Sembolü
    outCols[0] = 0x1C;
    outCols[1] = 0x3E;
    outCols[2] = 0x7C;
    outCols[3] = 0x3E;
    outCols[4] = 0x1C;
    break;
  default:
    outCols[0] = 0x00;
    outCols[1] = 0x00;
    outCols[2] = 0x00;
    outCols[3] = 0x00;
    outCols[4] = 0x00;
    break;
  }
}

static const char MARQUEE_BANNER_CODES[] = "* TUFAN HILALI COK SEVIYOR *   ";
static const int MARQUEE_BANNER_LEN = sizeof(MARQUEE_BANNER_CODES) - 1;
#define MARQUEE_CHAR_ADVANCE 13

static void initLoveMarquee() {
  marqueeScrollX = (float)SCREEN_W;
  marqueeStartMs = millis();
}

static void stepAndRenderLoveMarquee(uint32_t frameCounter, uint32_t nowMs) {
  memset(oledBuffer, 0, sizeof(oledBuffer));

  marqueeScrollX -= 0.95f;
  float totalPixelWidth = (float)(MARQUEE_BANNER_LEN * MARQUEE_CHAR_ADVANCE);
  if (marqueeScrollX < -totalPixelWidth) {
    marqueeScrollX += totalPixelWidth;
  }

  for (int x = 0; x < SCREEN_W; x += 4) {
    int phase = (x + (int)(frameCounter >> 1)) & 7;
    if (phase < 4) {
      oledSetPixel(x, 1);
      oledSetPixel(SCREEN_W - 1 - x, SCREEN_H - 2);
    }
  }

  for (int pass = 0; pass < 2; pass++) {
    float baseX = marqueeScrollX + (float)pass * totalPixelWidth;
    for (int i = 0; i < MARQUEE_BANNER_LEN; i++) {
      int charX = (int)roundf(baseX + (float)(i * MARQUEE_CHAR_ADVANCE));
      if (charX > SCREEN_W || charX < -12)
        continue;

      int waveY =
          8 + (int)roundf(sinf((float)nowMs * 0.0045f + (float)i * 0.45f) * 2.0f);

      uint8_t cols[5];
      getGlyph5x8(MARQUEE_BANNER_CODES[i], cols);

      for (int c = 0; c < 5; c++) {
        uint8_t colBits = cols[c];
        if (colBits == 0)
          continue;
        int px = charX + (c * 2);
        for (int r = 0; r < 8; r++) {
          if (colBits & (1 << r)) {
            int py = waveY + (r * 2);
            oledSetPixel(px, py);
            oledSetPixel(px + 1, py);
            oledSetPixel(px, py + 1);
            oledSetPixel(px + 1, py + 1);
          }
        }
      }
    }
  }

  drawShakeOverlayHUD(nowMs);
  oledFlush();
}

// ============================================================================
// 11. MOD GEÇİŞ YÖNETİCİSİ VE ~1 SN BUFFER / 2 SN KİLİTLİ SALLAMA SİSTEMİ
// ============================================================================
static void switchToMode(AppMode targetMode, bool skipHTIfAlreadyRunning) {
  currentMode = targetMode;
  lastWakeTimeMs = millis();

  if (targetMode == APP_MODE_FLUID) {
    stopBackgroundMelody();
    if (!skipHTIfAlreadyRunning) {
      initHTFormation();
    }
    Serial.println(
        "[MOD 1] 'H ♥ T' -> Ragdoll -> Gercekci Su Fizigi Modu Aktif!");
  } else if (targetMode == APP_MODE_GAME) {
    initFlyingHeartRound();
    startBackgroundMelody(MELODY_LOVE_STORY, MELODY_LOVE_STORY_LEN);
    Serial.println("[MOD 2] ADXL345 Flying Heart Oyunu & Love Story Muzigi Aktif!");
  } else if (targetMode == APP_MODE_MARQUEE) {
    initLoveMarquee();
    startBackgroundMelody(MELODY_CANT_HELP_LOVE, MELODY_CANT_HELP_LOVE_LEN);
    Serial.println(
        "[MOD 3] 'TUFAN HILALI COK SEVIYOR' Yazi Seridi & Ask Muzigi Aktif!");
  }
}

static void updateHardShakeSelector(uint32_t nowMs) {
  if (!shakeRearmed && (nowMs - lastShakeDetectedMs >= 320UL) &&
      (currentTotalG < SHAKE_REARM_BELOW_G)) {
    shakeRearmed = true;
  }

  bool isHardShakePeak =
      (currentTotalG >= SHAKE_WAKE_THRESHOLD_G) ||
      (currentTotalG >= 1.72f && adxlJerkMagnitude >= 0.42f);

  if (isHardShakePeak) {
    lastWakeTimeMs = nowMs;

    if (pendingShakeCount == 0 ||
        (shakeRearmed && (nowMs - lastShakeDetectedMs >= SHAKE_BUFFER_MS))) {
      if (pendingShakeCount < 3) {
        pendingShakeCount++;
      }
      lastShakeDetectedMs = nowMs;
      shakeRearmed = false;
      wokeOnFirstShake = false;

      triggerShakeConfirmBeep(pendingShakeCount);

      Serial.printf("[SERT SALLAMA %ux] Algilandi! (%.2f G) -> 2 sn icinde "
                    "islem yapilmazsa Mod %u secilecek.\n",
                    pendingShakeCount, currentTotalG, pendingShakeCount);
    }
  }

  if (pendingShakeCount > 0 &&
      (nowMs - lastShakeDetectedMs >= SHAKE_LOCK_WAIT_MS)) {
    uint8_t finalCount = pendingShakeCount;
    bool skipReset = (finalCount == 1 && wokeOnFirstShake &&
                      currentMode == APP_MODE_FLUID);
    pendingShakeCount = 0;
    wokeOnFirstShake = false;

    if (finalCount == 1) {
      switchToMode(APP_MODE_FLUID, skipReset);
    } else if (finalCount == 2) {
      switchToMode(APP_MODE_GAME, false);
    } else {
      switchToMode(APP_MODE_MARQUEE, false);
    }
  }
}

// ============================================================================
// 12. SERİ PORT TEST VE DİYAGNOSTİK SİSTEMİ
// ============================================================================
static void runHardwareDiagnostics() {
  Serial.println("\n=======================================================");
  Serial.println("   CYBERPUNK ROZET - H ♥ T RAGDOLL, OYUN & ASK SERIDI");
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
  Serial.println("  'r' -> Mod 1: 'H ♥ T' -> Ragdoll -> Su Modunu Baslat");
  Serial.println("  'g' -> Mod 2: Flying Heart Oyunu + Ask Muzigini Baslat");
  Serial.println(
      "  't' -> Mod 3: 'TUFAN HILALI COK SEVIYOR' Seridini Baslat");
  Serial.println("  's' -> Suya Kinetik Sok Dalgasi Firlat (Splash)");
  Serial.println("  'u' -> Hemen Eko-Uyku Moduna Gecir");
  Serial.println("  'm' -> Buzzer Sesini Ac / Kapat (Mute)");
  Serial.println("=======================================================\n");
}

static uint8_t ecoSleepSliceCounter = 0;

static void configureBleHaystackBeacon(bool ecoBurstMode) {
  esp_bd_addr_t randMacAddr;
  memcpy(randMacAddr, PUBLIC_KEY_28, 6);
  randMacAddr[0] |= 0xC0;

  memcpy(&haystackPayload[7], &PUBLIC_KEY_28[6], 22);
  haystackPayload[29] = (PUBLIC_KEY_28[0] >> 6) & 0x03;

  esp_ble_gap_set_rand_addr(randMacAddr);
  esp_ble_gap_config_adv_data_raw(haystackPayload, sizeof(haystackPayload));
  if (ecoBurstMode) {
    esp_ble_gap_start_advertising(&bleAdvParamsEcoSleep);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_N0);
  } else {
    esp_ble_gap_start_advertising(&bleAdvParamsActive);
    esp_ble_tx_power_set(ESP_BLE_PWR_TYPE_ADV, ESP_PWR_LVL_P3);
  }
}

static void suspendBleForLightSleep() {
  if (!bleReady || bleSuspendedForLightSleep)
    return;
  esp_ble_gap_stop_advertising();
  esp_bluedroid_disable();
  esp_bt_controller_disable();
  bleSuspendedForLightSleep = true;
}

static void resumeBleFromLightSleep(bool ecoBurstMode) {
  if (!bleReady)
    return;
  if (bleSuspendedForLightSleep) {
    if (esp_bt_controller_enable(ESP_BT_MODE_BLE) == ESP_OK) {
      esp_bt_sleep_enable();
      esp_bluedroid_enable();
      bleSuspendedForLightSleep = false;
    }
  }
  if (!bleSuspendedForLightSleep) {
    configureBleHaystackBeacon(ecoBurstMode);
  }
}

static void enterEcoSleepMode() {
  isSystemAwake = false;
  pendingShakeCount = 0;
  wokeOnFirstShake = false;
  ecoSleepSliceCounter = 0;
  adxlFirstSampleAfterWake = true;
  stopBackgroundMelody();
  buzzerSilence();
  oledSetPower(false);
  setI2cClockHz(400000UL);
  adxl345ConfigureMode(true);
  suspendBleForLightSleep();
}

static void wakeUpFromEcoSleep(bool countAsFirstShake) {
  isSystemAwake = true;
  lastWakeTimeMs = millis();
  adxlFirstSampleAfterWake = true;
  setI2cClockHz(800000UL);
  adxl345ConfigureMode(false);
  resumeBleFromLightSleep(false);
  oledSetPower(true);

  currentMode = APP_MODE_FLUID;
  stopBackgroundMelody();
  initHTFormation();

  if (countAsFirstShake) {
    pendingShakeCount = 1;
    lastShakeDetectedMs = millis();
    shakeRearmed = false;
    wokeOnFirstShake = true;
    triggerShakeConfirmBeep(1);
  } else {
    buzzerPlayWakeSound();
  }
}

static void handleSerialCommands() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == 's' || c == 'S') {
      if (!isSystemAwake) {
        wakeUpFromEcoSleep(false);
      }
      lastWakeTimeMs = millis();
      triggerKineticSplash(1.4f);
      Serial.println(
          "[SERI KOMUT] Suya Kinetik Sok Dalgasi (Splash) uygulandi!");
    } else if (c == 'u' || c == 'U') {
      if (isSystemAwake) {
        Serial.println("[SERI KOMUT] Sistem Eko-Uyku Moduna alindi.");
        Serial.flush();
        enterEcoSleepMode();
      }
    } else if (c == 'm' || c == 'M') {
      isSoundMuted = !isSoundMuted;
      if (isSoundMuted)
        buzzerSilence();
      Serial.printf("[SERI KOMUT] Buzzer Sesi: %s\n",
                    isSoundMuted ? "KAPALI (MUTE)" : "ACIK");
    } else if (c == 'r' || c == 'R') {
      if (!isSystemAwake)
        wakeUpFromEcoSleep(false);
      switchToMode(APP_MODE_FLUID, false);
    } else if (c == 'g' || c == 'G') {
      if (!isSystemAwake)
        wakeUpFromEcoSleep(false);
      switchToMode(APP_MODE_GAME, false);
    } else if (c == 't' || c == 'T') {
      if (!isSystemAwake)
        wakeUpFromEcoSleep(false);
      switchToMode(APP_MODE_MARQUEE, false);
    } else if (c == '1') {
      buzzerPulseActive(500);
    } else if (c == '3') {
      buzzerDirectTone(2000, 600);
    } else if (c == '4') {
      buzzerDirectTone(1000, 600);
    } else if (c == '5') {
      buzzerPlayWakeSound();
    }
  }
}

// ============================================================================
// 13. ÇİFT ÇEKİRDEK FREERTOS GÖREVLERİ (BLE FIND MY & ANA SİMÜLASYON)
// ============================================================================
void bleAirTagTask(void *pvParameters) {
  esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
  btStartMode(BT_MODE_BLE);
  esp_bt_sleep_enable();
  esp_bluedroid_init();
  esp_bluedroid_enable();

  configureBleHaystackBeacon(false);
  bleReady = true;
  if (!isSystemAwake) {
    suspendBleForLightSleep();
  }

  Serial.printf("[BLE CORE 0] Apple Find My yayini aktif! MAC: "
                "%02X:%02X:%02X:%02X:%02X:%02X\n",
                (unsigned)(PUBLIC_KEY_28[0] | 0xC0), (unsigned)PUBLIC_KEY_28[1],
                (unsigned)PUBLIC_KEY_28[2], (unsigned)PUBLIC_KEY_28[3],
                (unsigned)PUBLIC_KEY_28[4], (unsigned)PUBLIC_KEY_28[5]);

  vTaskDelete(NULL);
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

    // GERÇEK HİBRİT EKO-UYKU DÖNGÜSÜ (~80 ms esp_light_sleep_start + BLE Burst)
    if (!isSystemAwake) {
      if (adxl345CheckEcoWake()) {
        wakeUpFromEcoSleep(true);
      } else {
        if (bleReady && !bleSuspendedForLightSleep) {
          suspendBleForLightSleep();
        }
        ecoSleepSliceCounter++;
        // Her 45 dilimde bir (~3.6 sn) 75 ms boyunca Apple Find My BLE paketi yayınla
        if (ecoSleepSliceCounter >= 45) {
          ecoSleepSliceCounter = 0;
          resumeBleFromLightSleep(true);
          vTaskDelay(pdMS_TO_TICKS(75));
          suspendBleForLightSleep();
          continue;
        }

        if (Serial.available() > 0) {
          continue;
        }

        Serial.flush();
        uart_set_wakeup_threshold(UART_NUM_0, 3);
        esp_sleep_enable_uart_wakeup(UART_NUM_0);
        esp_sleep_enable_timer_wakeup(80000ULL); // 80 ms donanımsal light-sleep
        if (esp_light_sleep_start() != ESP_OK) {
          vTaskDelay(pdMS_TO_TICKS(80));
        }
        continue;
      }
    }

    uint32_t now = millis();

    // Oyun modunda cihaz eğilip oynandıkça uyanıklık süresini canlı tut
    if (currentMode == APP_MODE_GAME && adxlJerkMagnitude > 0.035f) {
      lastWakeTimeMs = now;
    }

    uint32_t timeoutLimit = (currentMode == APP_MODE_FLUID)
                                ? WAKE_TIMEOUT_FLUID_MS
                                : WAKE_TIMEOUT_SPECIAL_MS;
    uint32_t elapsed = now - lastWakeTimeMs;
    if (elapsed >= timeoutLimit) {
      Serial.println("[EKO-UYKU] Hareketsizlik suresi doldu, ekran ve ses kapatildi.");
      Serial.flush();
      enterEcoSleepMode();
      continue;
    }

    uint32_t frameStart = millis();

    // 1. Sensör ve Sert Sallama Algılayıcıyı Güncelle
    adxl345ReadGravity();
    updateHardShakeSelector(now);

    // 2. Aktif Bölümü Çalıştır ve Çiz
    if (currentMode == APP_MODE_FLUID) {
      stepFluidPhysics(now);
      renderUltraFluid(frameCounter, now);

      float kinetic = 0.0f;
      for (int i = 0; i < NUM_PARTICLES; i += 6) {
        float vx = particles[i].x - particles[i].px;
        float vy = particles[i].y - particles[i].py;
        kinetic += vx * vx + vy * vy;
      }
      smoothedKinetic = (smoothedKinetic * 0.85f) + (kinetic * 0.15f);
    } else if (currentMode == APP_MODE_GAME) {
      stepAndRenderFlyingHeartGame(frameCounter, now);
    } else {
      stepAndRenderLoveMarquee(frameCounter, now);
    }

    // 3. Pürüzsüz Müzik Kutusu & Celesta Ses Motorunu Güncelle
    updateSoundEngine(now);

    frameCounter++;

    if (now - lastFpsTime >= 1000) {
      liveFps = (frameCounter - lastFpsCount) * 1000 / (now - lastFpsTime);
      lastFpsCount = frameCounter;
      lastFpsTime = now;
    }

    if (now - lastTelemetryMs >= 2000) {
      lastTelemetryMs = now;
      uint32_t remSec = (timeoutLimit - elapsed) / 1000;
      Serial.printf("[MOD %u] Fx:%+.2f Fy:%+.2f | Sallama:%u | FPS:%lu | "
                    "Kalan:%lu sn\n",
                    (unsigned)currentMode, effGx, effGy,
                    (unsigned)pendingShakeCount, (unsigned long)liveFps,
                    (unsigned long)remSec);
    }

    // ADAPTİF PROMOTION KARE HIZI (80 MHz'de 110 FPS Akıcılık & Pil Tasarrufu)
    uint32_t targetFrameTimeMs = 8; // ~110-120 FPS
    if (currentMode == APP_MODE_FLUID && fluidStage == FLUID_STAGE_WATER &&
        smoothedKinetic <= 0.05f && shakeMagnitude <= 0.04f &&
        pendingShakeCount == 0) {
      targetFrameTimeMs = 28; // Su durulunca ~35 FPS eko-bekleme
    }

    uint32_t workTime = millis() - frameStart;
    if (workTime < targetFrameTimeMs) {
      vTaskDelay(pdMS_TO_TICKS(targetFrameTimeMs - workTime));
    } else {
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }
}

void setup() {
  buzzerInit();
  buzzerSilence();

  setCpuFrequencyMhz(80);

  Serial.begin(115200);
  delay(300);

  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN, 400000UL);
  currentI2cClockHz = 400000UL;
  Wire.setTimeOut(50);
  Wire.setBufferSize(640);

  runHardwareDiagnostics();
  adxl345ConfigureMode(false);
  initHTFormation();
  oledInit128x32();
  setI2cClockHz(800000UL);
  renderUltraFluid(0, millis());

  buzzerPlayWakeSound();

  fluidStageStartMs = millis();
  lastWakeTimeMs = millis();

  xTaskCreatePinnedToCore(bleAirTagTask, "BLE_Haystack", 4096, NULL, 2, NULL,
                          0);
  xTaskCreatePinnedToCore(waterSimTask, "PBD_UltraFluid", 8192, NULL, 5, NULL,
                          1);
}

void loop() { vTaskDelete(NULL); }
