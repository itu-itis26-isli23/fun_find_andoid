@echo off
title Cyberpunk Rozet - Kodu ESP32'ye Yukle
echo =======================================================
echo    ESP32'YE YUKLEME BASLATILIYOR (COM3)
echo =======================================================
pio run -t upload
if %ERRORLEVEL% equ 0 (
    echo.
    echo [BASARILI] Kod ESP32'ye yuklendi!
    echo Seri Port Ekranini acmak icin monitor.bat dosyasini calistirabilirsiniz.
) else (
    echo.
    echo [HATA] Yukleme basarisiz oldu. Kabloyu ve COM portunu kontrol edin.
)
pause
