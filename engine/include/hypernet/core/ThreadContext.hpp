#pragma once

#include <array>
#include <cstdio>
#include <string_view>

#if defined(__linux__)
#include <sys/syscall.h> // SYS_gettid
#include <unistd.h>      // syscall
#endif

namespace hypernet::core
{

class ThreadContext
{
  public:
    static constexpr int kNonWorker = -1;

    // 워커 스레드 시작점에서 1회 호출 (이미 하고 있는 패턴)
    static void setCurrentWorkerId(int workerId) noexcept
    {
        currentWorkerId_() = workerId;
        updateTag_(workerId);
        (void)currentTid();
    }

    [[nodiscard]] static int currentWorkerId() noexcept { return currentWorkerId_(); }

    [[nodiscard]] static bool isWorkerThread() noexcept { return currentWorkerId() >= 0; }

    // syscall 매번 호출하지 않도록 thread_local 캐시
    [[nodiscard]] static long currentTid() noexcept { return cachedTid_(); }

    // 로그/디버깅용: "main" 또는 "w0" 등
    [[nodiscard]] static std::string_view currentThreadTag() noexcept
    {
        auto &buf = tagBuf_();
        if (buf[0] == '\0')
        {
            // 혹시 초기화가 안된 경우에도 안전하게
            updateTag_(currentWorkerId_());
        }
        return std::string_view{buf.data()};
    }

  private:
    static int &currentWorkerId_() noexcept
    {
        thread_local int id = kNonWorker;
        return id;
    }

    static long computeTid_() noexcept
    {
#if defined(__linux__)
        return static_cast<long>(::syscall(SYS_gettid));
#else
        return 0;
#endif
    }

    static long &cachedTid_() noexcept
    {
        thread_local long tid = computeTid_(); // 스레드당 1회만 syscall
        return tid;
    }

    static std::array<char, 16> &tagBuf_() noexcept
    {
        thread_local std::array<char, 16> buf{}; // 0으로 초기화
        return buf;
    }

    static void updateTag_(int wid) noexcept
    {
        auto &buf = tagBuf_();
        if (wid < 0)
        {
            std::snprintf(buf.data(), buf.size(), "main");
        }
        else
        {
            std::snprintf(buf.data(), buf.size(), "w%d", wid);
        }
    }
};
[[nodiscard]] inline int wid() noexcept
{
    return ThreadContext::currentWorkerId();
}
[[nodiscard]] inline long tid() noexcept
{
    return ThreadContext::currentTid();
}
[[nodiscard]] inline std::string_view ttag() noexcept
{
    return ThreadContext::currentThreadTag();
}
} // namespace hypernet::core
