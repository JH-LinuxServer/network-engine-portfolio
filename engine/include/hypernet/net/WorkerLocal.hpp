#pragma once

namespace hypernet::net
{
class SessionManager;
class ConnectorManager; // [추가] 전방 선언
} // namespace hypernet::net

namespace hypernet::net
{

class WorkerLocal
{
  public:
    // ---------------------------------------------------------------------
    // SessionManager (기존)
    // ---------------------------------------------------------------------
    static void set(SessionManager *sm) noexcept { sm_() = sm; }
    static SessionManager *sessionManager() noexcept { return sm_(); }

    // ---------------------------------------------------------------------
    // ConnectorManager (누락된 부분 추가)
    // ---------------------------------------------------------------------
    static void set(ConnectorManager *cm) noexcept { cm_() = cm; }
    static ConnectorManager *connectorManager() noexcept { return cm_(); }

  private:
    // SessionManager 저장소
    static SessionManager *&sm_() noexcept
    {
        thread_local SessionManager *p = nullptr;
        return p;
    }

    // [추가] ConnectorManager 저장소
    static ConnectorManager *&cm_() noexcept
    {
        thread_local ConnectorManager *p = nullptr;
        return p;
    }
};

} // namespace hypernet::net