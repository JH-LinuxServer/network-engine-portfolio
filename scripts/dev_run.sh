#!/usr/bin/env bash
set -e

# 사용법: ./scripts/dev_run.sh <target> [scenario]
TARGET=$1
SCENARIO="${2:-s1}"

# [핵심 변경] 기본적으로 RelWithDebInfo 폴더를 바라봄
BUILD_TYPE="relwithdebinfo"
BIN_DIR="build_${BUILD_TYPE}/bin"
CONFIG_DIR="config/${SCENARIO}"

if [[ -z "$TARGET" ]]; then
  echo "Usage: $0 <mock|gateway|client> [s1|s2|s3]"
  echo "Example: $0 gateway s1"
  exit 1
fi

# 폴더가 없으면 혹시 Debug로 빌드했는지 체크 (유연성 확보)
if [[ ! -d "$BIN_DIR" ]]; then
  echo "Warning: ${BIN_DIR} not found. Trying debug build..."
  BIN_DIR="build_debug/bin"
fi

if [[ ! -x "${BIN_DIR}/fep_gateway" ]]; then
  echo "Error: Binary not found in ${BIN_DIR}. Run ./scripts/dev_build.sh first."
  exit 1
fi

echo "=== Running ${TARGET} in ${SCENARIO} (from ${BIN_DIR}) ==="

case "$TARGET" in
mock)
  exec "${BIN_DIR}/mock_exchange" --config "${CONFIG_DIR}/exchange.toml"
  ;;
gateway)
  exec "${BIN_DIR}/fep_gateway" --config "${CONFIG_DIR}/fep.toml"
  ;;
client)
  exec "${BIN_DIR}/loadgen" --config "${CONFIG_DIR}/client.toml"
  ;;
*)
  echo "Error: Unknown target '${TARGET}'"
  exit 1
  ;;
esac
