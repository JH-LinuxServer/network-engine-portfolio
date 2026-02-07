#!/usr/bin/env bash
set -e

# 기본값: RelWithDebInfo
BUILD_TYPE="${1:-RelWithDebInfo}"
DIR_NAME="build_$(echo "$BUILD_TYPE" | tr '[:upper:]' '[:lower:]')"

echo "=== Building FEP Project [Type: ${BUILD_TYPE}] ==="
echo "=== Directory: ${DIR_NAME} ==="

# 1. 설정 (Configure)
cmake -S . -B "${DIR_NAME}" -G Ninja \
  -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# 2. 컴파일 (Build)
cmake --build "${DIR_NAME}" -j "$(nproc)"

# 3. LSP 연동 (compile_commands.json)
ln -sf "${DIR_NAME}/compile_commands.json" .

# [추가된 부분] 4. 'build' 폴더를 현재 빌드 폴더로 연결 (심볼릭 링크)
# -s: 심볼릭 링크 생성
# -f: 기존 파일이 있으면 덮어쓰기
# -n: 링크가 폴더 안으로 들어가는 것 방지 (no-dereference)
ln -sfn "${DIR_NAME}" build

echo "=== Build Complete! ==="
echo "1. Binary: ${DIR_NAME}/bin"
echo "2. Shortcut: build -> ${DIR_NAME}"
