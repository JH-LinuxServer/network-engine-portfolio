#include <hypernet/net/Socket.hpp>

#include <cassert>
#include <cerrno>
#include <cstring>
#include <iostream>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

using hypernet::net::Socket;

namespace {

/// Socket 생성/close 에서 fd 가 닫히는지 기본 동작을 확인하는 테스트입니다.
bool test_create_and_close_raii() {
    Socket sock = Socket::createTcpIPv4();
    if (!sock.isValid()) {
        std::cerr << "[raii] createTcpIPv4 returned invalid socket\n";
        return false;
    }

    const int fd = sock.nativeHandle();
    if (fd < 0) {
        std::cerr << "[raii] nativeHandle < 0\n";
        return false;
    }

    // close 전에는 F_GETFD 가 성공해야 한다.
    if (::fcntl(fd, F_GETFD) == -1) {
        std::cerr << "[raii] F_GETFD failed before close, errno=" << errno << "\n";
        return false;
    }

    sock.close();

    if (sock.isValid()) {
        std::cerr << "[raii] socket should be invalid after close()\n";
        return false;
    }

    // close 이후에는 F_GETFD 가 실패해야 한다(일반적으로 EBADF).
    if (::fcntl(fd, F_GETFD) != -1) {
        std::cerr << "[raii] F_GETFD still succeeds after close(), fd may be leaked\n";
        return false;
    }

    return true;
}

/// move 연산자/생성자가 fd 소유권을 정확히 옮기는지 확인합니다.
bool test_move_semantics() {
    Socket a = Socket::createTcpIPv4();
    if (!a.isValid()) {
        std::cerr << "[move] createTcpIPv4 returned invalid socket\n";
        return false;
    }

    const int fd = a.nativeHandle();

    Socket b = std::move(a);

    if (a.isValid()) {
        std::cerr << "[move] moved-from socket should be invalid\n";
        return false;
    }

    if (!b.isValid()) {
        std::cerr << "[move] destination socket should be valid\n";
        return false;
    }

    if (b.nativeHandle() != fd) {
        std::cerr << "[move] nativeHandle mismatch after move\n";
        return false;
    }

    // move 대입도 한 번 더 확인
    Socket c;
    c = std::move(b);

    if (b.isValid()) {
        std::cerr << "[move] moved-from socket (assignment) should be invalid\n";
        return false;
    }

    if (!c.isValid() || c.nativeHandle() != fd) {
        std::cerr << "[move] destination socket (assignment) invalid or fd mismatch\n";
        return false;
    }

    return true;
}

/// loopback (127.0.0.1) 에서 bind/listen/connect/accept/send/recv 를 단순 검증합니다.
bool test_loopback_echo() {
    // 서버 소켓 생성 및 옵션 설정
    Socket server = Socket::createTcpIPv4();
    if (!server.isValid()) {
        std::cerr << "[loopback] server socket invalid\n";
        return false;
    }

    if (!server.setReuseAddr(true)) {
        std::cerr << "[loopback] setReuseAddr failed, errno=" << errno << "\n";
        return false;
    }

    // port=0 으로 bind 하면 커널이 사용 가능한 포트를 자동 선택합니다.
    ::sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(0);

    if (!server.bind(reinterpret_cast<::sockaddr *>(&addr), sizeof(addr))) {
        std::cerr << "[loopback] bind failed, errno=" << errno << "\n";
        return false;
    }

    if (!server.listen(1)) {
        std::cerr << "[loopback] listen failed, errno=" << errno << "\n";
        return false;
    }

    // 실제 바인딩된 포트를 얻어온다.
    ::socklen_t len = sizeof(addr);
    if (::getsockname(server.nativeHandle(), reinterpret_cast<::sockaddr *>(&addr), &len) == -1) {
        std::cerr << "[loopback] getsockname failed, errno=" << errno << "\n";
        return false;
    }
    const std::uint16_t port = ntohs(addr.sin_port);

    // 클라이언트 소켓 생성 후 loopback 으로 connect
    Socket client = Socket::createTcpIPv4();
    if (!client.isValid()) {
        std::cerr << "[loopback] client socket invalid\n";
        return false;
    }

    if (!client.setNoDelay(true)) {
        std::cerr << "[loopback] setNoDelay failed, errno=" << errno << "\n";
        return false;
    }

    if (!client.connect("127.0.0.1", port)) {
        std::cerr << "[loopback] client connect failed, errno=" << errno << "\n";
        return false;
    }

    // 서버에서 accept
    ::sockaddr_in peer{};
    ::socklen_t peerLen = sizeof(peer);
    Socket accepted = server.accept(reinterpret_cast<::sockaddr *>(&peer), &peerLen);
    if (!accepted.isValid()) {
        std::cerr << "[loopback] accept failed, errno=" << errno << "\n";
        return false;
    }

    // 클라이언트에서 메시지 전송
    const char msg[] = "ping";
    const ::ssize_t sent = client.send(msg, sizeof(msg), 0);
    if (sent != static_cast<::ssize_t>(sizeof(msg))) {
        std::cerr << "[loopback] send failed or partial, sent=" << sent
                  << " expected=" << sizeof(msg) << " errno=" << errno << "\n";
        return false;
    }

    // 서버에서 수신
    char buf[16] = {};
    const ::ssize_t recvd = accepted.recv(buf, sizeof(buf), 0);
    if (recvd != sent) {
        std::cerr << "[loopback] recv size mismatch, recvd=" << recvd << " sent=" << sent
                  << " errno=" << errno << "\n";
        return false;
    }

    if (std::memcmp(msg, buf, static_cast<std::size_t>(recvd)) != 0) {
        std::cerr << "[loopback] data mismatch\n";
        return false;
    }

    return true;
}

} // namespace

int main() {
    bool ok = true;

    ok = ok && test_create_and_close_raii();
    ok = ok && test_move_semantics();
    ok = ok && test_loopback_echo();

    if (!ok) {
        std::cerr << "Socket tests FAILED\n";
        return 1;
    }

    std::cout << "Socket tests PASSED\n";
    return 0;
}
