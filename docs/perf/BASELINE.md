# Performance Baseline Report

본 문서는 `loadgen`을 이용하여 측정한 FEP Gateway의 시나리오별 Latency 및 Throughput 결과를 기술한다.

## 1. Test Environment

### 1.1. Hardware Specifications
- **Model:** AMD Ryzen 7 7840U w/ Radeon 780M Graphics
- **Architecture:** Zen 4 (x86_64)
- **Cores:** 8 Cores / 16 Threads
- **Clock:** 3.79 GHz (Base), Time per Cycle ≈ 0.26 ns
- **Cache:** L1 32KiB/core, L2 1MiB/core, L3 16MiB (Shared)

### 1.2. Software & Configuration
- **OS:** Fedora Server (Linux Standard Kernel)
- **Kernel Tuning:** None (`isolcpus` 미적용)
- **Network Topology:** Loopback (Loadgen ↔ Gateway ↔ Exchange)
- **Key Settings:**
  - `SO_REUSEPORT`: Enabled
  - `TCP_NODELAY`: Enabled
  - **Process Pinning:** `taskset`을 사용하여 Mock, FEP, Loadgen 프로세스 그룹 분리.
  - **Thread Pinning:** **미적용** (OS 스케줄러(CFS)에 의존).
  - **Memory Pool:** **미적용** (System Allocator `new`/`delete` 직접 사용).

## 2. Methodology

### 2.1. Scenarios
| ID | Client Config | Gateway/Exchange Config | Description |
| :--- | :--- | :--- | :--- |
| **S1** | Thread: 1<br>Sessions: 10 | Worker Threads: **1** | **[Baseline]** 단일 워커 기준선.<br>스레드 간 통신 비용 없음. |
| **S2** | Thread: 1<br>Sessions: 10 | Worker Threads: **2** | **[Local Scale]** 멀티 워커 확장성 테스트.<br>라우팅 최적화를 통해 세션이 할당된 워커 내에서만 처리됨. |
| **S3** | Thread: 1<br>Sessions: 10 | Worker Threads: **2** | **[Handoff Cost]** 워커 간 통신 비용 테스트.<br>강제적인 워커 간 패킷 이관 (TaskQueue + eventfd) 발생. |

### 2.2. Measurement Conditions
- **Traffic Volume:** 총 200,000 패킷 (Ping/Pong)
- **Traffic Model:** 세션당 In-flight Ping 1개 유지 (Request-Response 왕복 후 다음 요청).
- **Metric:** Application Level RTT (Round Trip Time).

## 3. Results Summary

각 시나리오별 2회 수행 결과의 중앙값(Median) 기준.
(단위: µs, 1,000ns = 1µs)

| Metric | S1 (Baseline) | S2 (Local/Scale) | S3 (Handoff) |
| :--- | :---: | :---: | :---: |
| **RTT P50 (Median)** | **32.0** | **32.1** | **41.1** |
| **RTT P99** | 40.6 | 39.9 | 46.6 |
| **RTT P99.9** | 65.8 | 57.9 | 63.3 |
| **Throughput (ops/sec)** | ~30,700 | ~30,500 | ~26,600 |

<참고사항>
1. 물리적인 레이턴시 측정x
2. 현재 하드웨어에 대한 지식이 부족한 상태로 측정을 하였으며, 일부 결과가 튀는 현상에 대해 OS,CPU ,전력모드 등 더 알아보고 테스트 환경을 구성해야함을 인지하고 있습니다.
3. 최대한 결과를 일관성있게 측정하기 위해 시스템 설정 성능모드, 프로세스별 CPU 배치까지는 하였으나 정말 이것이 제대로 적용된 것인지에 대한 검증도 아직 필요한 상태입니다.
4. 본 측정에서 얻을 수 있었던 결과로는 일단 추측이지만 S1, S2 의 경우 대부분  평균 32~35us 로 측정되며, s3의 경우 38~41us 가 나오며 이는 TaskQueue 에 작업을 넣는 과정과 epoll_wait 에서 대기중인
    ownerThread 를 깨우는 과정에서 발생하는 지연이라고 생각합니다.
5. rdtsc 를 사용하는 방법에 대한 내용도 숙지 하겠습니다.


## 4. Analysis

### 4.1. Thread-per-Core Efficiency (S1 vs. S2)
- **결과:** 단일 워커(S1, 32.0µs)와 2개 워커(S2, 32.1µs) 간의 P50 Latency 차이는 0.1µs로, 오차 범위 내에서 동일하게 측정됨.
- **분석:**
  - 워커 스레드 증가 시에도 Local Path 처리 성능이 유지됨을 확인.
  - 현재 구조(Shared-Nothing) 상에서 로컬 세션 처리에 대한 Lock Contention이 발생하지 않음이 수치로 나타남.

### 4.2. Cost of Thread Handoff (S2 vs. S3)
- **결과:** S3 시나리오(Cross-worker) 수행 시 S2 대비 약 **+9.0µs** (32.1µs → 41.1µs)의 Latency 지연이 발생함.
- **분석:**
  - 이 9µs의 지연은 `TaskQueue` 진입을 위한 Mutex 대기, `eventfd` 시그널링, 그리고 스레드 Wake-up 및 Context Switch 비용의 총합임.
  - 현재 `TaskQueue` 구현체는 `std::mutex`를 사용하고 있어, 스레드 경합 시 Context Switch가 빈번하게 발생할 가능성이 있음.
- **개선 여지 (Future Work):**
  - 향후 `std::mutex` 대신 **Spinlock (Busy-wait)**을 적용하여 Sleep 상태 진입을 방지할 경우, Wake-up Latency를 제거하여 Handoff 비용을 단축할 수 있을 것으로 판단됨.

