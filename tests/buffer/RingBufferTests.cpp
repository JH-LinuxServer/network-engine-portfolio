#include <hypernet/buffer/RingBuffer.hpp>

#include <cassert>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <string>

using hypernet::buffer::RingBuffer;

namespace {

bool test_simple_write_read() {
    RingBuffer rb(16);

    const char *msg = "hello";
    const std::size_t len = std::strlen(msg);

    const std::size_t written = rb.write(reinterpret_cast<const std::byte *>(msg), len);
    if (written != len) {
        std::cerr << "[simple] written=" << written << " expected=" << len << "\n";
        return false;
    }

    char out[16] = {};
    const std::size_t read = rb.read(reinterpret_cast<std::byte *>(out), len);
    if (read != len) {
        std::cerr << "[simple] read=" << read << " expected=" << len << "\n";
        return false;
    }

    if (std::string(out, read) != std::string(msg, len)) {
        std::cerr << "[simple] data mismatch\n";
        return false;
    }

    if (!rb.empty()) {
        std::cerr << "[simple] buffer not empty after full read\n";
        return false;
    }

    return true;
}

bool test_wrap_around() {
    RingBuffer rb(8);

    // 1) 5바이트 쓰기
    const char *first = "ABCDE";
    const std::size_t firstLen = 5;
    auto written = rb.write(reinterpret_cast<const std::byte *>(first), firstLen);
    if (written != firstLen) {
        std::cerr << "[wrap] first write failed\n";
        return false;
    }

    // 2) 앞에서 3바이트 읽어서 head를 앞으로 이동시켜 wrap-around를 위한 공간 확보
    char tmp[8] = {};
    auto read = rb.read(reinterpret_cast<std::byte *>(tmp), 3);
    if (read != 3) {
        std::cerr << "[wrap] first read failed\n";
        return false;
    }

    // 현재 버퍼 안에는 'D', 'E' 두 글자가 남아 있음.

    // 3) 6바이트를 추가로 써서 tail이 끝을 넘어가도록 wrap-around 시킨다.
    const char *second = "123456";
    const std::size_t secondLen = 6;
    written = rb.write(reinterpret_cast<const std::byte *>(second), secondLen);

    // 총 capacity는 8이므로, 현재 남은 freeSpace는 8 - 2 = 6, 전부 써져야 함.
    if (written != secondLen) {
        std::cerr << "[wrap] second write failed, written=" << written << "\n";
        return false;
    }

    // 4) 버퍼에 남아 있는 모든 데이터를 읽어서 순서가 "DE123456" 인지 확인
    char out[16] = {};
    read = rb.read(reinterpret_cast<std::byte *>(out), sizeof(out));

    const std::string expected = "DE123456";
    if (read != expected.size()) {
        std::cerr << "[wrap] total read=" << read << " expected=" << expected.size() << "\n";
        return false;
    }

    if (std::string(out, read) != expected) {
        std::cerr << "[wrap] data mismatch: got='" << std::string(out, read) << "' expected='"
                  << expected << "'\n";
        return false;
    }

    if (!rb.empty()) {
        std::cerr << "[wrap] buffer not empty after full read\n";
        return false;
    }

    return true;
}

bool test_partial_write_and_read() {
    RingBuffer rb(4);

    const char *payload = "ABCDEFG"; // 7바이트
    const std::size_t len = std::strlen(payload);

    const std::size_t written = rb.write(reinterpret_cast<const std::byte *>(payload), len);
    if (written != rb.capacity()) {
        std::cerr << "[partial] expected full capacity write, written=" << written << "\n";
        return false;
    }

    if (!rb.full()) {
        std::cerr << "[partial] buffer should be full\n";
        return false;
    }

    // 2바이트만 읽음
    char out[8] = {};
    const std::size_t read = rb.read(reinterpret_cast<std::byte *>(out), 2);
    if (read != 2) {
        std::cerr << "[partial] read 2 failed\n";
        return false;
    }

    // 남은 2바이트를 모두 읽음
    const std::size_t read2 = rb.read(reinterpret_cast<std::byte *>(out + 2), 8);
    if (read2 != 2) {
        std::cerr << "[partial] read remaining failed, read2=" << read2 << "\n";
        return false;
    }

    // capacity=4이므로 실제로 기록된 첫 4바이트 "ABCD"만 검증한다.
    const std::string expected(payload, 4);
    if (std::string(out, 4) != expected) {
        std::cerr << "[partial] data mismatch: got='" << std::string(out, 4) << "' expected='"
                  << expected << "'\n";
        return false;
    }

    if (!rb.empty()) {
        std::cerr << "[partial] buffer not empty after reads\n";
        return false;
    }

    return true;
}

bool test_peek() {
    RingBuffer rb(8);
    const char *msg = "XYZ";
    const std::size_t len = std::strlen(msg);

    rb.write(reinterpret_cast<const std::byte *>(msg), len);

    char peekBuf[8] = {};
    const std::size_t peeked = rb.peek(reinterpret_cast<std::byte *>(peekBuf), sizeof(peekBuf));
    if (peeked != len) {
        std::cerr << "[peek] peeked=" << peeked << " expected=" << len << "\n";
        return false;
    }

    if (std::string(peekBuf, peeked) != std::string(msg, len)) {
        std::cerr << "[peek] peek data mismatch\n";
        return false;
    }

    // peek 이후에도 여전히 데이터를 읽을 수 있어야 한다.
    char out[8] = {};
    const std::size_t read = rb.read(reinterpret_cast<std::byte *>(out), sizeof(out));
    if (read != len || std::string(out, read) != std::string(msg, len)) {
        std::cerr << "[peek] read after peek mismatch\n";
        return false;
    }

    return true;
}

/// BufferView 기반의 peekView() 기본 동작 테스트
bool test_peek_view_basic() {
    RingBuffer rb(16);

    const char *msg = "HELLO";
    const std::size_t len = std::strlen(msg);
    rb.write(reinterpret_cast<const std::byte *>(msg), len);

    auto view = rb.peekView(16);
    if (view.size() != len) {
        std::cerr << "[peek_view] size=" << view.size() << " expected=" << len << "\n";
        return false;
    }

    std::string s(reinterpret_cast<const char *>(view.data()), view.size());
    if (s != std::string(msg, len)) {
        std::cerr << "[peek_view] data mismatch: got='" << s << "' expected='" << msg << "'\n";
        return false;
    }

    if (rb.available() != len) {
        std::cerr << "[peek_view] buffer should not be consumed, available=" << rb.available()
                  << " expected=" << len << "\n";
        return false;
    }

    return true;
}

/// BufferView 기반의 readView() 가 데이터를 소비하는지 테스트
bool test_read_view_consumes() {
    RingBuffer rb(16);

    const char *msg = "WORLD";
    const std::size_t len = std::strlen(msg);
    rb.write(reinterpret_cast<const std::byte *>(msg), len);

    auto view = rb.readView(16);
    if (view.size() != len) {
        std::cerr << "[read_view] size=" << view.size() << " expected=" << len << "\n";
        return false;
    }

    std::string s(reinterpret_cast<const char *>(view.data()), view.size());
    if (s != std::string(msg, len)) {
        std::cerr << "[read_view] data mismatch: got='" << s << "' expected='" << msg << "'\n";
        return false;
    }

    if (!rb.empty()) {
        std::cerr << "[read_view] buffer should be empty after readView\n";
        return false;
    }

    return true;
}

/// 랩어라운드된 경우, peekView()/readView() 가 연속 구간만을 반환하는지 테스트
bool test_view_wrap_around() {
    RingBuffer rb(8);

    // 1) 5바이트 쓰기
    const char *first = "ABCDE";
    const std::size_t firstLen = 5;
    auto written = rb.write(reinterpret_cast<const std::byte *>(first), firstLen);
    if (written != firstLen) {
        std::cerr << "[view_wrap] first write failed\n";
        return false;
    }

    // 2) 3바이트 읽어서 head 를 3으로 이동
    char tmp[8] = {};
    auto read = rb.read(reinterpret_cast<std::byte *>(tmp), 3);
    if (read != 3) {
        std::cerr << "[view_wrap] first read failed\n";
        return false;
    }

    // 3) 6바이트를 추가로 써서 랩어라운드 발생 (논리 데이터: "DE123456")
    const char *second = "123456";
    const std::size_t secondLen = 6;
    written = rb.write(reinterpret_cast<const std::byte *>(second), secondLen);
    if (written != secondLen) {
        std::cerr << "[view_wrap] second write failed\n";
        return false;
    }

    if (rb.available() != 8) {
        std::cerr << "[view_wrap] available=" << rb.available() << " expected=8\n";
        return false;
    }

    // head 에서 끝까지의 연속 구간은 "DE123" (5바이트) 이어야 한다.
    auto view1 = rb.peekView(16);
    const std::string expected1 = "DE123";
    std::string s1(reinterpret_cast<const char *>(view1.data()), view1.size());

    if (s1 != expected1) {
        std::cerr << "[view_wrap] peekView mismatch: got='" << s1 << "' expected='" << expected1
                  << "'\n";
        return false;
    }

    // readView 도 동일한 연속 구간을 소비해야 한다.
    auto view2 = rb.readView(16);
    std::string s2(reinterpret_cast<const char *>(view2.data()), view2.size());
    if (s2 != expected1) {
        std::cerr << "[view_wrap] readView mismatch: got='" << s2 << "' expected='" << expected1
                  << "'\n";
        return false;
    }

    // 나머지 랩어라운드된 "456" 을 다시 readView 로 읽을 수 있어야 한다.
    auto view3 = rb.readView(16);
    const std::string expected2 = "456";
    std::string s3(reinterpret_cast<const char *>(view3.data()), view3.size());
    if (s3 != expected2) {
        std::cerr << "[view_wrap] second readView mismatch: got='" << s3 << "' expected='"
                  << expected2 << "'\n";
        return false;
    }

    if (!rb.empty()) {
        std::cerr << "[view_wrap] buffer not empty after consuming all views\n";
        return false;
    }

    return true;
}

} // namespace

int main() {
    bool ok = true;

    ok = ok && test_simple_write_read();
    ok = ok && test_wrap_around();
    ok = ok && test_partial_write_and_read();
    ok = ok && test_peek();
    ok = ok && test_peek_view_basic();
    ok = ok && test_read_view_consumes();
    ok = ok && test_view_wrap_around();

    if (!ok) {
        std::cerr << "RingBuffer tests FAILED\n";
        return 1;
    }

    std::cout << "RingBuffer tests PASSED\n";
    return 0;
}
