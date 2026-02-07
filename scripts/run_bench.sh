#!/usr/bin/env bash

set -euo pipefail



# =========================================================

# Usage: ./scripts/run_bench.sh <scenario>

#   scenario: s1|s1_baseline, s2|s2_scale, s3|s3_handoff

# =========================================================



SCENARIO="${1:-}"

if [[ -z "${SCENARIO}" ]]; then

  echo "Usage: $0 <scenario>"

  exit 1

fi



case "${SCENARIO}" in

  s1|s1_baseline)  SCEN="s1" ;;

  s2|s2_scale)     SCEN="s2" ;;

  s3|s3_handoff)   SCEN="s3" ;;

  *) echo "Unknown scenario: ${SCENARIO}"; exit 1 ;;

esac



ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

BIN_DIR="${ROOT_DIR}/build/bin"

CFG_DIR="${ROOT_DIR}/config/${SCEN}"



MOCK_BIN="${BIN_DIR}/mock_exchange"

FEP_BIN="${BIN_DIR}/fep_gateway"

LOADGEN_BIN="${BIN_DIR}/loadgen"



# ---------------------------------------------------------

# [Core Pinning Strategy] - Ryzen 8C/16T (SMT ON)

# ---------------------------------------------------------

# lscpu -e=CPU,CORE 결과 기준 sibling:

#   core0: (0,8), core1: (1,9), core2: (2,10), core3: (3,11),

#   core4: (4,12), core5: (5,13), core6: (6,14), core7: (7,15)

#

# 정책:

# - OS 전용: 물리코어 0 -> (0,8) (건드리지 않음)

# - 프로세스당 물리코어 2개(= SMT쌍 2개, 논리CPU 4개) 할당

# - 내부 thread affinity는 끔 (OS가 같은 CPU set 안에서 스케줄)

# ---------------------------------------------------------



# 환경변수 초기화

unset CORE_MOCK CORE_FEP CORE_LOADGEN



CORE_MOCK="1,9,2,10"       

CORE_FEP="3,11,4,12"       

CORE_LOADGEN="5,13,6,14"   



# # 물리코어 1+2, 3+4, 5+6을 각각 프로세스에 할당 (sibling 포함)

# CORE_MOCK="1,2"        # Physical cores 1+2 (with SMT siblings)

# CORE_FEP="3,4"        # Physical cores 3+4

# CORE_LOADGEN="5,6"    # Physical cores 5+6



# ---------------------------------------------------------

# Helpers

# ---------------------------------------------------------

require_cmd() {

  command -v "$1" >/dev/null 2>&1 || { echo "Error: required command not found: $1"; exit 1; }

}



validate_cpu_list() {

  local list="$1"

  local IFS=',' cpu

  for cpu in $list; do

    [[ -d "/sys/devices/system/cpu/cpu${cpu}" ]] || { echo "Error: CPU ${cpu} not found on this machine"; exit 1; }

  done

}



port_is_listening() {

  local port="$1"

  # ss filter syntax: ( sport = :PORT )

  ss -ltn "( sport = :${port} )" 2>/dev/null | awk 'NR>1{found=1} END{exit found?0:1}'

}



wait_port() {

  local port="$1"

  local name="$2"

  local tries="${3:-80}"   # 80 * 0.1s = 8s

  local delay="${4:-0.1}"



  for ((i=1; i<=tries; i++)); do

    if port_is_listening "${port}"; then

      return 0

    fi

    sleep "${delay}"

  done

  echo "Error: ${name} port ${port} not ready after $((tries)) tries"

  exit 1

}



any_ports_in_use() {

  # 리슨 중인 포트만 체크 (프로세스 식별은 안 하지만, 벤치 충돌 방지에는 충분)

  local ports=("$@")

  local p

  for p in "${ports[@]}"; do

    if port_is_listening "${p}"; then

      echo "Port ${p} is already listening"

      return 0

    fi

  done

  return 1

}



# ---------------------------------------------------------

# [Validation & Safety]

# ---------------------------------------------------------

require_cmd ss

require_cmd taskset

require_cmd stdbuf

require_cmd awk

require_cmd date

require_cmd uname



if [[ ! -d "${CFG_DIR}" ]]; then

  echo "Error: Config dir not found: ${CFG_DIR}"

  exit 1

fi



[[ -x "${MOCK_BIN}" ]] || { echo "Error: not executable: ${MOCK_BIN}"; exit 1; }

[[ -x "${FEP_BIN}"  ]] || { echo "Error: not executable: ${FEP_BIN}";  exit 1; }

[[ -x "${LOADGEN_BIN}" ]] || { echo "Error: not executable: ${LOADGEN_BIN}"; exit 1; }



validate_cpu_list "${CORE_MOCK}"

validate_cpu_list "${CORE_FEP}"

validate_cpu_list "${CORE_LOADGEN}"



# 파일 디스크립터 상향(미래 connection_count 증가 대비)

ulimit -n 1048576 >/dev/null 2>&1 || true



# 포트 점유 체크

PORTS_TO_CHECK=(9000 10000 9100 9101 9102)

if any_ports_in_use "${PORTS_TO_CHECK[@]}"; then

  echo "Error: Ports are in use. Try:"

  echo "  pkill -INT loadgen fep_gateway mock_exchange || true"

  echo "  pkill -KILL loadgen fep_gateway mock_exchange || true"

  exit 1

fi



# ---------------------------------------------------------

# [Execution]

# ---------------------------------------------------------

TS="$(date +%Y%m%d_%H%M%S)"

OUT_DIR="${ROOT_DIR}/results/${SCEN}/${TS}"

mkdir -p "${OUT_DIR}"



cp -f "${CFG_DIR}/exchange.toml" "${OUT_DIR}/exchange.toml"

cp -f "${CFG_DIR}/fep.toml"      "${OUT_DIR}/fep.toml"

cp -f "${CFG_DIR}/client.toml"   "${OUT_DIR}/client.toml"



# 환경 스냅샷 (재현성/비교용)

( uname -a > "${OUT_DIR}/uname.txt" ) || true

( lscpu > "${OUT_DIR}/lscpu.txt" ) || true

( lscpu -e=CPU,CORE > "${OUT_DIR}/lscpu_cpu_core.txt" ) || true

( cat /sys/kernel/mm/transparent_hugepage/enabled > "${OUT_DIR}/thp.txt" 2>/dev/null ) || true

( cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor > "${OUT_DIR}/governor.txt" 2>/dev/null ) || true

( git -C "${ROOT_DIR}" rev-parse HEAD > "${OUT_DIR}/git_rev.txt" 2>/dev/null ) || true

( git -C "${ROOT_DIR}" status --porcelain > "${OUT_DIR}/git_status.txt" 2>/dev/null ) || true

( printf "MOCK=%s\nFEP=%s\nLOADGEN=%s\n" "${CORE_MOCK}" "${CORE_FEP}" "${CORE_LOADGEN}" > "${OUT_DIR}/pinning.txt" ) || true



MOCK_PID=""

FEP_PID=""



cleanup() {

  set +e

  echo " Stopping background processes..."

  if [[ -n "${FEP_PID}" ]]; then kill -INT "${FEP_PID}" 2>/dev/null || true; fi

  if [[ -n "${MOCK_PID}" ]]; then kill -INT "${MOCK_PID}" 2>/dev/null || true; fi



  # 약간의 정상 종료 시간 부여

  sleep 0.5



  if [[ -n "${FEP_PID}" ]] && kill -0 "${FEP_PID}" 2>/dev/null; then kill -KILL "${FEP_PID}" 2>/dev/null || true; fi

  if [[ -n "${MOCK_PID}" ]] && kill -0 "${MOCK_PID}" 2>/dev/null; then kill -KILL "${MOCK_PID}" 2>/dev/null || true; fi



  wait 2>/dev/null || true

  echo " Cleanup done."

}

trap cleanup EXIT INT TERM



echo "=================================================="

echo " Scenario : ${SCEN}"

echo " Strategy : Process Pinning Only (No Internal Thread Affinity)"

echo " Pinning  : Mock(${CORE_MOCK}) | FEP(${CORE_FEP}) | Loadgen(${CORE_LOADGEN})"

echo " Output   : ${OUT_DIR}"

echo "=================================================="



# Governor Performance 설정 (가능하면). sudo 비번 요구로 멈추는 것 방지 위해 -n 사용.

if command -v cpupower &>/dev/null; then

  sudo -n cpupower frequency-set -g performance >/dev/null 2>&1 || true

fi



echo "[1/3] Mock Exchange..."

taskset -c "${CORE_MOCK}" stdbuf -oL -eL "${MOCK_BIN}" --config "${CFG_DIR}/exchange.toml" > "${OUT_DIR}/mock.log" 2>&1 &

MOCK_PID=$!

wait_port 10000 "Mock Exchange"



echo "[2/3] FEP Gateway..."

taskset -c "${CORE_FEP}" stdbuf -oL -eL "${FEP_BIN}" --config "${CFG_DIR}/fep.toml" > "${OUT_DIR}/fep.log" 2>&1 &

FEP_PID=$!

wait_port 9000 "FEP Gateway"



# 어떤 CPU set으로 실행됐는지 기록

( taskset -cp "${MOCK_PID}" > "${OUT_DIR}/mock_taskset.txt" 2>/dev/null ) || true

( taskset -cp "${FEP_PID}"  > "${OUT_DIR}/fep_taskset.txt"  2>/dev/null ) || true



echo "[3/3] Loadgen..."

set +e

taskset -c "${CORE_LOADGEN}" stdbuf -oL -eL "${LOADGEN_BIN}" --config "${CFG_DIR}/client.toml" > "${OUT_DIR}/loadgen.log" 2>&1

LOADGEN_RC=$?

set -e



echo "${LOADGEN_RC}" > "${OUT_DIR}/exit_code.txt"



# Report section만 추출

awk '/BENCHMARK RESULT REPORT/{flag=1} flag' "${OUT_DIR}/loadgen.log" > "${OUT_DIR}/report.txt" || true



echo "Done. ExitCode: ${LOADGEN_RC}"

if [[ -s "${OUT_DIR}/report.txt" ]]; then

  cat "${OUT_DIR}/report.txt"

else

  echo "Warning: No report"

fi



exit "${LOADGEN_RC}"