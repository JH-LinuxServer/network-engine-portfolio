#include <trading/controllers/CompositeController.hpp>

namespace trading::controllers
{
void CompositeController::add(std::shared_ptr<trading::IController> child)
{
    if (child)
        children_.push_back(std::move(child));
}

void CompositeController::install(hypernet::protocol::Dispatcher &dispatcher,
                                  hyperapp::AppRuntime &runtime)
{
    for (auto &c : children_)
        c->install(dispatcher, runtime);
}

void CompositeController::onServerStart()
{
    for (auto &c : children_)
        c->onServerStart();
}

void CompositeController::onServerStop()
{
    for (auto &c : children_)
        c->onServerStop();
}

void CompositeController::onSessionStart(hypernet::SessionHandle session)
{
    for (auto &c : children_)
        c->onSessionStart(session);
}

void CompositeController::onSessionEnd(hypernet::SessionHandle session)
{
    for (auto &c : children_)
        c->onSessionEnd(session);
}
} // namespace trading::controllers
