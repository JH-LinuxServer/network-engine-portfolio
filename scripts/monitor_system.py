import time
import requests
import re
import sys
from datetime import datetime

# 설정: 각 프로세스의 모니터링 주소
TARGETS = {
    "GATEWAY ": "http://127.0.0.1:9100/metrics",
    "EXCHANGE": "http://127.0.0.1:9102/metrics"
}

def get_metrics(url):
    try:
        res = requests.get(url, timeout=0.5)
        text = res.text
        # Prometheus 포맷 파싱 (rx_messages_total 12345)
        rx = int(re.search(r"rx_messages_total (\d+)", text).group(1))
        tx = int(re.search(r"tx_messages_total (\d+)", text).group(1))
        return rx, tx
    except:
        return 0, 0

print(f"Monitoring System Started... (Targets: {len(TARGETS)})")
print("=" * 75)
print(f"{'Time':<10} | {'Node':<10} | {'Total RX':<12} | {'RX TPS':<10} | {'Total TX':<12} | {'TX TPS':<10}")
print("=" * 75)

prev_data = {name: (0, 0) for name in TARGETS}
first_run = True

try:
    while True:
        time.sleep(1)
        now_str = datetime.now().strftime("%H:%M:%S")
        
        # UI 갱신을 위해 커서 이동 (선택 사항, 여기선 단순 출력)
        if not first_run:
            print("-" * 75)

        for name, url in TARGETS.items():
            curr_rx, curr_tx = get_metrics(url)
            prev_rx, prev_tx = prev_data[name]

            # TPS 계산
            rx_tps = curr_rx - prev_rx
            tx_tps = curr_tx - prev_tx
            
            # 첫 실행때는 TPS 0으로 표시
            if first_run:
                rx_tps = 0
                tx_tps = 0

            print(f"{now_str:<10} | {name:<10} | {curr_rx:<12,} | {rx_tps:<10,} | {curr_tx:<12,} | {tx_tps:<10,}")
            
            # 이전 값 갱신
            prev_data[name] = (curr_rx, curr_tx)
            
        first_run = False

except KeyboardInterrupt:
    print("\nStopped.")  