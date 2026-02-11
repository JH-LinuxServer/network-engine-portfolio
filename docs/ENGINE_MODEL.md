# Engine Model & Benchmark Design

이 문서는 `hypernet` 엔진 / `hyperapp` 런타임 기반 FEP 벤치마크의
스레딩 모델, 세션 라우팅, 프로토콜 흐름, 그리고 s1/s2/s3 시나리오의 의도를 설명합니다.

---

## Components

### Engine (`engine/`)
- epoll 기반 Reactor/EventLoop
- Session/SessionManager
- ConnectorManager (Outbound connections)
- WorkerScheduler (멀티 워커 스레드)
- Dispatcher/Codec/Framer (Opcode 기반 디스패치)

### Runtime (`runtime/`)
- AppRuntime / SessionService
- ConnState(StateMachine): Connected / Handshaked 등
- UpstreamGateway: 워커별 upstream 세션 매핑 저장소

### Domain (`domains/trading/`)
- Handshake Feature:
  - RoleHello(Client/Exchange role 식별 및 Handshaked 전이)
- Benchmark Feature:
  - PerfPing(20050) / PerfPong(20051)
  - hop timestamp를 기록하여 RTT 및 구간별 지연시간 산출

### Apps (`apps/`)
- `loadgen`: Client 역할
- `fep_gateway`: Gateway 역할 (Client<->Exchange 중계)
- `mock_exchange`: Exchange 역할 (Ping 수신 -> Pong 응답)

---

## Protocol Flow

### Handshake
1. 세션 연결 시 각 Role 컨트롤러가 `RoleHelloReq` 전송
2. Gateway가 role을 확인하고 `RoleHelloAck` 응답
3. ack ok이면 `ConnState::Handshaked`로 전이

### Benchmark Ping/Pong and timestamps

PerfPing fields (client->gateway->exchange):
- `seq`: 요청 순번
- `t1`: client send timestamp
- `t2`: gateway forward timestamp (gateway에서 기록)
- exchange에서 도착 시 `t3` 기록 후 PerfPong 생성/전송

PerfPong fields (exchange->gateway->client):
- `t4`: gateway가 client로 forward 직전 기록
- client 수신 시 `t5`(local)로 마무리하여 hop 계산

계산:
- Total RTT = t5 - t1
- Hop1 (Client->FEP) = t2 - t1
- Hop2 (FEP->Mock)   = t3 - t2
- Hop3 (Mock->FEP)   = t4 - t3
- Hop4 (FEP->Client) = t5 - t4

Warmup/Measure:
- warmup N회 수행 후, measure M회 수집
- 측정 결과는 p50/p99/p99.9 등 tail latency 중심으로 리포트

---

## Load Model (Important)

현재 loadgen은 **in-flight 요청을 1개만 유지**합니다.

- 세션이 `connection_count = 10`이어도
- 동시에 10개 outstanding이 아니라,
- `PerfPong`이 돌아온 뒤에야 다음 `PerfPing`을 전송합니다.
- 대상 세션만 round-robin으로 교체합니다.

의미:
- 레이턴시 회귀(특히 p99/p99.9) 확인에 유리
- 고혼잡/최대 처리량 검증에는 별도 파이프라이닝 옵션이 필요

---

## Scenarios

### s1: baseline
- worker_threads = 1
- handoff_mode = false
- 단일 워커 기준선 레이턴시

### s2: scale (local routing)
- worker_threads = 2
- handoff_mode = false
- gateway upstream 선택: `idx = wid() % workerCount`
- cross-worker 이동을 최소화하여 “멀티워커 확장”의 기본 비용을 확인

### s3: handoff (cross-worker routing)
- worker_threads = 2
- handoff_mode = true
- gateway upstream 선택: `idx = session.id() % workerCount`
- 현재 워커와 다른 워커 upstream으로 보내는 케이스가 발생할 수 있어,
  cross-worker handoff/라우팅 비용이 레이턴시에 반영됨

---

## Benchmark Execution & Result Storage

- `./scripts/run_bench.sh <scenario>`가 mock/fep/loadgen을 순서대로 실행
- 결과는 `results/<scenario>/<timestamp>/`에 저장
- `bench_sweep.sh`로 반복 실행 후 `summarize_bench.py`로 중앙값 요약








# ENGINE_MODEL

이 문서는 **“현재 프로젝트가 어떻게 동작하는지” 설명** 합니다.  

Thread-per-core + SO_REUSEPORT: 워커 IO 스레드마다(보통 코어마다) 리슨 소켓을 따로 두고 커널이 accept 분산.

메인 스레드 역할: 워커들이 공통으로 쓸 “객체/리소스(설정, 로거, 풀, 매니저 등)”를 생성.

워커 IO 스레드 역할: 각자 초기화(자기 이벤트루프/리액터/리슨 소켓/상태 준비)를 끝낸 뒤 독립적으로 동작.

세션 소유권(affinity): “각 워커가 자기 세션을 소유”하고, 해당 세션의 IO/상태 처리는 원칙적으로 소유 워커에서만 수행.

크로스-워커 동작(자기 소유가 아닌 세션 send, broadcast, routing 등)은 전부 TaskQueue로 넘겨서 소유 워커에서 처리.


---

## 1) 시스템 개요

이 프로젝트는 **3개의 독립 프로세스**가 로컬 TCP로 연결되어 동작하는 벤치마크 구조입니다.

- **mock_exchange**
  - 역할: 거래소 서버
  - 동작: TCP listen 후 요청/프로토콜 처리(현재는 벤치 Ping/Pong 응답이 핵심)
- **fep_gateway**
  - 역할: 게이트웨이(프록시/브릿지)
  - 동작: 클라이언트(loadgen)로부터 연결을 받고, 동시에 upstream(mock_exchange)으로 연결을 생성하여 중계/처리
- **loadgen**
  - 역할: 부하 생성 클라이언트
  - 동작: gateway로 접속하여 handshake 및 ping/pong 벤치 수행, 지표 기록/리포트

기본 데이터 흐름:
(loadgen) --> (fep_gateway) --> (mock_exchange)
client        proxy/bridge      server

---

## 2) 네트워크 토폴로지 / 포트(기본)

기본 포트는 다음과 같습니다.

- Gateway listen: `127.0.0.1:9000`
- Exchange listen: `127.0.0.1:10000`
- Gateway upstream connect: `127.0.0.1:10000`
- Loadgen connect target: `127.0.0.1:9000`

토폴로지:
(loadgen) ----connect----> (fep_gateway) ----connect----> (mock_exchange)
           127.0.0.1:9000                127.0.0.1:10000

---

## 3) 코드/역할 분리(엔진 vs 도메인)

### 3.1 엔진 레이어: `hypernet/`
- 런타임, 이벤트 루프, 세션 관리, 디스패처 등 네트워크 엔진의 공통 기반을 제공합니다.
- 앱(프로세스)은 엔진을 초기화하고 “도메인 컨트롤러”를 주입하여 동작합니다.

### 3.2 도메인 레이어: `domains/trading/`
- 프로토콜(opcode/packet), 컨트롤러(핸들러), role별 조립(Install)을 제공합니다.
- “무엇을 처리하는가(비즈니스/벤치 로직)”는 trading 도메인이 담당합니다.

---

## 4) 앱 공통 베이스와 워커/컨트롤러 모델

### 4.1 앱 공통 베이스
`apps/common`의 공통 베이스는 앱이 반복적으로 해야 하는 다음 작업을 담당합니다.

- 워커 수만큼 컨트롤러 슬롯 준비
- 세션 시작/종료 등 라이프사이클 이벤트를
  - 런타임 공통 처리 후
  - 해당 워커의 컨트롤러에 전달
- 워커별 컨트롤러 저장/조회(“워커 단위 상태/로직” 지원)

즉, **엔진 이벤트를 도메인 컨트롤러로 전달하는 접착층**입니다.

### 4.2 워커(Thread) 전제
- 프로젝트는 워커(worker) 단위로 동작합니다.
- “워커 수”와 “워커별 컨트롤러/연결”이 성능/동작에 직접 영향을 줍니다.
- 측정/벤치 환경에서는 워커/프로세스 간 간섭을 최소화하도록 코어 핀닝을 “규약”으로 둡니다.

---

## 5) Gateway의 upstream 연결 모델(중요 규약)

Gateway는 “서버 + 클라이언트” 역할을 동시에 수행합니다.

- 서버로서: `127.0.0.1:9000` listen, loadgen의 접속을 받음
- 클라이언트로서: `127.0.0.1:10000`으로 upstream(mock_exchange) 연결을 생성

현재 모델의 핵심 규약:

- **워커마다 upstream 연결을 별도로 생성하는 구조**
  - 보통 “워커 수 = upstream 연결 수” 형태로 동작합니다.
- upstream 연결(세션)은 워커별로 식별/보관되어, 해당 워커의 gateway 컨트롤러에서 사용됩니다.

이 모델은 “워커 단위로 독립적인 경로/부하”를 만들기 좋지만, 워커 수가 늘면 upstream 연결 수도 늘어난다는 점을 전제로 합니다.

---

## 6) Trading Domain의 규약(프로토콜/컨트롤러/설치)

### 6.1 Controller 규약
- 기능은 `IController` 규약으로 구현됩니다.
- `install(dispatcher, runtime)`에서 패킷 핸들러를 등록합니다.
- 필요 시 서버 시작/세션 이벤트 훅을 통해 상태/자원을 관리합니다.

### 6.2 Install(조립) 패턴
- role별로 필요한 컨트롤러를 모아(Composite) 한 번에 install 합니다.
- 현재 role 구성(개념):
  - client(loadgen): hello + benchmark
  - gateway(fep_gateway): gateway hello + gateway benchmark(+ upstream 연동)
  - exchange(mock_exchange): exchange hello + exchange benchmark

### 6.3 Opcode/Packet Bind 정책
- trading 도메인은 엔진 예약 영역과 충돌하지 않도록 opcode 범위/정책을 유지합니다.
- 패킷 핸들러는 **ConnState(연결 상태) 허용 조건**을 명시하며,
  상태가 맞지 않으면 패킷이 와도 핸들러가 실행되지 않을 수 있습니다.
