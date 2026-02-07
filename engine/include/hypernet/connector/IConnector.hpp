#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace hypernet::connector
{

// [수정됨] ConnectorManager와 호환되는 옵션 구조체
struct SendOptions
{
    // 기존 std::chrono::milliseconds timeout 대신 정수형 사용 (직관성 통일)
    // 기존 코드와 호환성을 위해 timeoutMs로 변경
    std::uint32_t timeoutMs{3000};

    // 최대 1회 재시도 여부
    bool retryOnce{false};
};

struct Response
{
    bool ok{false};
    std::vector<std::uint8_t> payload;

    // [추가됨] 오류 응답을 쉽게 만들기 위한 헬퍼 (로그에 나온 'fail' 에러 해결)
    static Response fail(std::string /*reason*/)
    {
        Response r;
        r.ok = false;
        return r;
    }
};

// [추가됨] 완료 콜백 타입 (RequestId 포함)
using ConnectorCallback = std::function<void(std::uint64_t reqId, int attempt, Response r)>;

class IConnector
{
  public:
    virtual ~IConnector() = default;

    virtual std::string_view name() const noexcept = 0;

    // [추가됨] ConnectorManager가 콜백을 등록하기 위해 호출 (로그에 나온 'setCompletionHook' 에러 해결)
    void setCompletionHook(ConnectorCallback hook) { hook_ = std::move(hook); }

    // [수정됨] 기존 sendAsync 대신, 상태(ID, attempt)를 포함한 전송 인터페이스
    // (로그에 나온 'send' 에러 해결)
    virtual void send(std::uint64_t reqId, int attempt, const SendOptions &opt, const std::vector<std::uint8_t> &data) = 0;

  protected:
    // 구현체(예: RedisConnector)가 작업 완료 시 호출할 훅
    ConnectorCallback hook_;
};

// [호환성 유지용] 기존 콜백 타입 (혹시 다른 곳에서 쓸까봐 남겨둠)
using Callback = std::function<void(Response &&)>;

} // namespace hypernet::connector