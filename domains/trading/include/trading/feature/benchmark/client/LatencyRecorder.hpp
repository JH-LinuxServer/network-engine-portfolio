#pragma once

#include <vector>
#include <algorithm>
#include <iostream>
#include <iomanip>
#include <string>
#include <cstdint>
#include <numeric>

namespace trading::feature::benchmark::client
{

class LatencyRecorder
{
  public:
    // RTT 및 구간별 지연시간 저장소
    std::vector<uint64_t> rtt_;
    std::vector<uint64_t> hop1_; // Client -> FEP
    std::vector<uint64_t> hop2_; // FEP -> Mock
    std::vector<uint64_t> hop3_; // Mock -> FEP
    std::vector<uint64_t> hop4_; // FEP -> Client

    // 생성자: 메모리 미리 확보 (재할당 오버헤드 방지)
    LatencyRecorder(size_t capacity = 100000)
    {
        rtt_.reserve(capacity);
        hop1_.reserve(capacity);
        hop2_.reserve(capacity);
        hop3_.reserve(capacity);
        hop4_.reserve(capacity);
    }

    // [Hot Path] 기록
    void record(uint64_t total, uint64_t h1, uint64_t h2, uint64_t h3, uint64_t h4)
    {
        rtt_.push_back(total);
        hop1_.push_back(h1);
        hop2_.push_back(h2);
        hop3_.push_back(h3);
        hop4_.push_back(h4);
    }

    // [Cold Path] 결과 리포트 출력
    void printReport()
    {
        std::cout << "\n=========================================================================================\n";
        std::cout << "                               BENCHMARK RESULT REPORT                                   \n";
        std::cout << "=========================================================================================\n";
        std::cout << std::left << std::setw(15) << "Metric"
                  << "| " << std::setw(10) << "Min(ns)"
                  << "| " << std::setw(10) << "Avg(ns)"
                  << "| " << std::setw(10) << "Max(ns)"
                  << "| " << std::setw(10) << "p50"
                  << "| " << std::setw(10) << "p99"
                  << "| " << std::setw(10) << "p99.9" << " |\n";
        std::cout << "-----------------------------------------------------------------------------------------\n";

        printRow("Total RTT", rtt_);
        printRow("Hop1(C->F)", hop1_);
        printRow("Hop2(F->M)", hop2_);
        printRow("Hop3(M->F)", hop3_);
        printRow("Hop4(F->C)", hop4_);

        std::cout << "=========================================================================================\n";
    }

  private:
    void printRow(const std::string &name, std::vector<uint64_t> &data)
    {
        if (data.empty())
            return;

        // 통계 계산을 위해 정렬
        std::sort(data.begin(), data.end());

        uint64_t min_v = data.front();
        uint64_t max_v = data.back();

        double sum = std::accumulate(data.begin(), data.end(), 0.0);
        double avg = sum / data.size();

        uint64_t p50 = data[data.size() * 0.50];
        uint64_t p99 = data[data.size() * 0.99];
        uint64_t p999 = data[data.size() * 0.999];

        std::cout << std::left << std::setw(15) << name << "| " << std::setw(10) << min_v << "| " << std::setw(10) << std::fixed << std::setprecision(0) << avg << "| " << std::setw(10) << max_v
                  << "| " << std::setw(10) << p50 << "| " << std::setw(10) << p99 << "| " << std::setw(10) << p999 << " |\n";
    }
};

} // namespace trading::feature::benchmark::client
