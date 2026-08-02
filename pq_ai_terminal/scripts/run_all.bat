@echo off
chcp 65001 >nul
echo ============================================================
echo   PQ AI Terminal - Batch Scenario Simulation
echo   Platform: T536 + HT7627S (Windows Simulation)
echo ============================================================
echo.

set EXE=pq_sim.exe
set CYCLES=100

if not exist %EXE% (
    echo ERROR: %EXE% not found.
    echo Please build the project first:
    echo   cmake -S . -B build -G "Ninja"
    echo   ninja -C build
    echo   copy build\sim\pq_sim.exe .
    exit /b 1
)

echo [1/5] Running S1 - Baseline Load (%CYCLES% cycles)...
%EXE% --scenario S1 --cycles %CYCLES% > report_S1.txt 2>&1
echo   Saved to report_S1.txt

echo [2/5] Running S2 - EV Charging (%CYCLES% cycles)...
%EXE% --scenario S2 --cycles %CYCLES% > report_S2.txt 2>&1
echo   Saved to report_S2.txt

echo [3/5] Running S3 - Distributed PV (%CYCLES% cycles)...
%EXE% --scenario S3 --cycles %CYCLES% > report_S3.txt 2>&1
echo   Saved to report_S3.txt

echo [4/5] Running S4 - EV+PV Coupled (%CYCLES% cycles)...
%EXE% --scenario S4 --cycles %CYCLES% > report_S4.txt 2>&1
echo   Saved to report_S4.txt

echo [5/5] Running S5 - Extreme Scenario (%CYCLES% cycles)...
%EXE% --scenario S5 --cycles %CYCLES% > report_S5.txt 2>&1
echo   Saved to report_S5.txt

echo.
echo ============================================================
echo   All scenarios completed. Reports saved:
echo     report_S1.txt ~ report_S5.txt
echo     pq_metrics.csv (last run)
echo     pq_events.csv  (last run)
echo ============================================================
