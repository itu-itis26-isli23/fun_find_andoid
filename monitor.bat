@echo off
title Cyberpunk Rozet Seri Port Ekrani (115200 Baud)
echo =======================================================
echo    CYBERPUNK ROZET SERI PORT EKRANI (115200 BAUD)
echo =======================================================
echo Komutlar:
echo   's' -> Suya Sok Dalgasi Vur (veya Uykudaysa Uyandir)
echo   'u' -> Hemen Uyku Moduna Gecir
echo   'r' -> Su Havuzunu Sifirla
echo.
echo Cikmak icin Ctrl+C tusuna basabilirsiniz.
echo -------------------------------------------------------
pio device monitor --port COM3 --baud 115200
pause
