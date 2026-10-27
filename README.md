# 🔮 Cyberpunk Smart Badge / Keychain (Fun Find)

ESP32 tabanlı, çift çekirdekli (Dual-Core), **Apple Find My (Macless-Haystack)** BLE yayını yapan ve dahili **0.91" 128x32 OLED** üzerinde ivmeölçer duyarlı **60 FPS Ultra-Gerçekçi Akışkan Fiziği (Position Based Dynamics - PBD)** simülasyonu çalıştıran akıllı rozet & anahtarlık projesi.

---

## ⚡ Donanım Bileşenleri

| Bileşen | Model / Tip | Protokol / Pin | Açıklama |
| :--- | :--- | :--- | :--- |
| **Mikrodenetleyici** | ESP32-WROOM-32D (DevKit v1) | USB-UART (COM) | 240 MHz Çift Çekirdek, BLE + Wi-Fi |
| **Ekran** | DSTK22666 (SSD1306) | I2C (0x3C) | 0.91" 128x32 Piksel OLED |
| **İvmeölçer** | GY-291 (ADXL345) | I2C (0x53) | 3 Eksen Sayısal İvme Sensörü (±16g) |

### 🔌 I2C Bağlantı Şeması

```text
ESP32 GPIO 21 (SDA)  --->  OLED SDA & ADXL345 SDA
ESP32 GPIO 22 (SCL)  --->  OLED SCL & ADXL345 SCL
ESP32 3V3            --->  OLED VCC & ADXL345 VCC & ADXL345 CS
ESP32 GND            --->  OLED GND & ADXL345 GND & ADXL345 SDO
```

---

## 🚀 Özellikler

### 1. Çift Çekirdekli (FreeRTOS) Mimari
- **Core 0 (Arka Plan BLE Görevi):** Apple Find My (Macless-Haystack) protokolüyle ham 31-byte Bluetooth Low Energy reklam paketleri yayınlar. Apple cihazlar tarafından çevre etiket olarak algılanır.
- **Core 1 (Fizik & Grafik Görevi):** ADXL345 verilerini 400 kHz I2C hızında okuyup, 240 parçacıklı PBD akışkan simülasyonunu ve 60 FPS ekran yenilemesini yürütür.

### 2. Ultra-Gerçekçi PBD Akışkan Motoru
- **Sıkıştırılamaz Sıvı Dinamiği:** Position Based Dynamics (PBD) kısıt çözücüsü ile parçacıklar gerçek bir sıvı gibi birbirini iterek hacim korur.
- **Yüzey Gerilimi & Kohezyon:** Sıvı damlacıkları birbirini çekerek menisküs ve organik dalgalanmalar oluşturur.
- **Eylemsizlik (Inertia Boost):** Elinizi hızlıca salladığınızda veya rozeti durdurduğunuzda sıvı eylemsizlik etkisiyle duvara tokat gibi çarpar.
- **Bayer 4x4 Matris Dithering:** 1-bit monokrom OLED ekranda sıvı derinliğini ve damlacık ışık parlamalarını organik şekilde hissettirir.

### 3. Akıllı Güç Yönetimi & Shake-to-Wake
- 60 saniye boyunca hareketsiz kaldığında ekran ve çevre birimleri otomatik uyku moduna geçer.
- Sert sallandığında (`> 2.0g`) ADXL345 donanımsal aktivite kesmesi ile anında uyanır ve ekrana kinetik şok dalgası (splash) gönderir.

---

## 🛠️ Kurulum & Yükleme

### Yöntem A: PlatformIO (Önerilen)
```bash
# Projeyi derle
pio run

# ESP32'ye flaşla
pio run -t upload

# Canlı telemetriyi izle (115200 Baud)
pio device monitor
```

### Yöntem B: Arduino IDE
1. Arduino IDE ile `CyberpunkBadge/CyberpunkBadge.ino` dosyasını açın.
2. Kart olarak **ESP32 Dev Module** seçin.
3. Upload Speed: `460800`, CPU Frequency: `240MHz` olarak ayarlayıp yükleyin.

---

## 🔑 Apple Find My Anahtar Üretimi
Kendi özel OpenHaystack / Macless-Haystack anahtarınızı üretmek için:
```bash
python key_uretici.py
```
Üretilen 28-byte public key değerini koddaki `PUBLIC_KEY_28` dizisine yapıştırabilirsiniz.
