# Hardware Specification Report

## 1. CPU Basic Info
- **Model:** AMD Ryzen 7 7840U w/ Radeon 780M Graphics
- **Architecture:** Zen 4 (x86_64)
- **Cores/Threads:** 8 Cores / 16 Threads
- **TSC Frequency (Base):** 3792.842 MHz (Refined TSC calibration)
- **Time per Cycle:** 0.2637 ns (1 / 3.792842 GHz)

## 2. Cache Info
- **L1 Cache Line Size:** 64 bytes (데이터 정렬 기준)
- **L1 Cache:** 32 KiB per core (Instruction & Data 분리)
- **L2 Cache:** 1 MiB per core (Private)
- **L3 Cache:** 16 MiB (Shared across all cores)

## 3. Note
- 내 시스템의 1 CPU Cycle은 약 **0.26 ns**이다. 
- 예를 들어, 메모리 접근(약 100ns) 한 번은 CPU가 약 **380 사이클** 동안 아무것도 못 하고 기다리게 만든다는 뜻이다.
- 따라서  객체 설계 시 **64바이트(Cache Line)** 정렬을 최대한  지켜 불필요한 메모리 로딩을 막아야 한다.
- 노트북 환경 특성상 전원 관리로 인한 클럭 변동이 있을 수 있으므로, 벤치마크 수행 시에는  전원을 성능 모드로 해야함
