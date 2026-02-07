# FEP Benchmark Project (hypernet / hyperapp)

이 저장소는 C++ 기반 이벤트 루프/세션 엔진(`hypernet`)과 애플리케이션 런타임(`hyperapp`) 위에서,
단순한 FEP Gateway 경로의 레이턴시/처리량을 재현 가능한 방식으로 측정하기 위한 벤치마크 환경을 포함합니다.

구성 요소:
- `mock_exchange`: 단순 Exchange 역할 (Ping 수신 -> Pong 반환)
- `fep_gateway`: Client <-> Exchange 중계 및 라우팅(handoff) 실험
- `loadgen`: 지정한 세션 수로 접속 후 Ping/Pong 왕복 레이턴시 측정

---

## Build

```bash
mkdir -p build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
````

빌드 결과 바이너리:

* `build/bin/mock_exchange`
* `build/bin/fep_gateway`
* `build/bin/loadgen`

---

## Benchmark Run

시나리오 실행:

```bash
./scripts/run_bench.sh s1
./scripts/run_bench.sh s2
./scripts/run_bench.sh s3
```

반복 실행(예: 각 시나리오 5회):

```bash
./scripts/bench_sweep.sh 5
```

최근 결과 요약:

```bash
./scripts/summarize_bench.py --last 5
```

---

## Scenarios

* `s1` (baseline): worker_threads=1 기반 단일 워커 경로 기준선
* `s2` (scale/local): worker_threads=2 + cross-worker 최소화 라우팅
* `s3` (handoff): worker_threads=2 + session-id 기반 cross-worker handoff 비용 측정

각 시나리오는 `config/s1|s2|s3/*.toml`에 정의되어 있습니다.

---

## What this benchmark measures (Important)

현재 loadgen은 **in-flight Ping을 1개만 유지**합니다.
즉, 세션이 N개여도 “동시에 N개 요청”이 아니라 “한 번에 1개 왕복”을 세션만 바꿔가며 수행합니다.

* 장점: 레이턴시(p99/p99.9) 회귀 체크에 유리
* 한계: 혼잡/최대 처리량/대규모 동시 outstanding 검증에는 별도 부하모델이 필요

---

## Results

실행 결과는 다음 위치에 저장됩니다:

`results/<scenario>/<timestamp>/`

주요 파일:

* `report.txt`: BENCHMARK RESULT REPORT 섹션
* `loadgen.log`, `fep.log`, `mock.log`
* `exchange.toml`, `fep.toml`, `client.toml` (실행 시점 설정 스냅샷)
* (환경 스냅샷이 있으면) `uname.txt`, `lscpu.txt`, `pinning.txt` 등

---

## Project Layout (high level)

* `engine/` : 네트워크 엔진 (epoll, session, router, scheduler 등)
* `runtime/` : 앱 런타임(상태 머신, session service 등)
* `domains/trading/` : 도메인 로직(Handshake, Benchmark 기능)
* `apps/` : 실행 바이너리(mock_exchange, fep_gateway, loadgen)
* `scripts/` : 벤치 실행/요약 스크립트
