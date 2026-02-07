#include <hypernet/net/Acceptor.hpp>
#include <hypernet/net/Socket.hpp>

#include <chrono>
#include <future>
#include <iostream>
#include <tuple>

using hypernet::net::Acceptor;
using hypernet::net::Socket;

namespace {

bool test_accept_one_loopback() {
    using namespace std::chrono_literals;

    // port=0: 커널이 임의 포트를 할당(테스트에서 포트 충돌 회피)
    Acceptor acceptor("127.0.0.1", 0);
    const auto port = acceptor.listenPort();
    if (port == 0) {
        std::cerr << "[acceptor] listenPort() == 0, getsockname failed?\n";
        return false;
    }

    // accept는 블로킹이므로 async로 돌리고, 클라이언트 connect로 깨운다.
    auto fut = std::async(std::launch::async, [&]() {
        Acceptor::PeerEndpoint peer{};
        Socket s = acceptor.acceptOne(&peer);
        return std::make_tuple(std::move(s), peer.ip, peer.port);
    });

    // 클라이언트 연결
    Socket client = Socket::createTcpIPv4();
    if (!client.isValid()) {
        std::cerr << "[acceptor] client socket invalid\n";
        return false;
    }

    if (!client.connect("127.0.0.1", port)) {
        std::cerr << "[acceptor] client connect failed\n";
        return false;
    }

    if (fut.wait_for(2s) != std::future_status::ready) {
        std::cerr << "[acceptor] acceptOne timeout\n";
        return false;
    }

    auto [accepted, peerIp, peerPort] = fut.get();
    if (!accepted.isValid()) {
        std::cerr << "[acceptor] accepted socket invalid\n";
        return false;
    }

    // peer 정보는 환경에 따라 달라질 수 있어 "비어 있지 않음" 정도만 확인
    if (peerIp.empty() || peerPort == 0) {
        std::cerr << "[acceptor] peer endpoint not filled: ip='" << peerIp << "' port=" << peerPort
                  << "\n";
        return false;
    }

    return true;
}

} // namespace

int main() {
    bool ok = true;

    ok = ok && test_accept_one_loopback();

    if (!ok) {
        std::cerr << "Acceptor tests FAILED\n";
        return 1;
    }

    std::cout << "Acceptor tests PASSED\n";
    return 0;
}
