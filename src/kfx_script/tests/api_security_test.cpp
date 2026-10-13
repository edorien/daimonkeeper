// Security pass S02 (docs/security-pass/README.md): the API server on a real loopback socket. The server is
// started the way the game starts it (api_init_server(), driven by api_update_server()) and a plain TCP client
// plays the part of a stray local program and of a web page that got the browser to send an HTTP request.
#ifndef _WIN32

#include <catch2/catch_test_macros.hpp>

#include "api.h"
#include "config_keeperfx.h"
#include "kfx_sim_state.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

int free_port()
{
    int s = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    REQUIRE(bind(s, (sockaddr *)&a, sizeof(a)) == 0);
    socklen_t len = sizeof(a);
    REQUIRE(getsockname(s, (sockaddr *)&a, &len) == 0);
    const int port = ntohs(a.sin_port);
    close(s);
    return port;
}

/** The API server, started on a free port for the length of one test (with a token when `token` is given). */
struct RunningApi {
    int port = 0;
    explicit RunningApi(const char *token = nullptr)
    {
        std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
        kfx_sim_state.game_kind = GKind_LocalGame;
        if (token != nullptr)
            setenv("DAIMON_API_TOKEN", token, 1);
        else
            unsetenv("DAIMON_API_TOKEN");
        port = free_port();
        api_enabled = true;
        api_port = port;
        REQUIRE(api_init_server() == 0);
    }
    ~RunningApi()
    {
        api_close_server();
        api_enabled = false;
        unsetenv("DAIMON_API_TOKEN");
    }
};

struct Client {
    int fd = -1;
    bool peer_closed = false;
    std::string received;

    explicit Client(int port)
    {
        fd = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = htons((uint16_t)port);
        REQUIRE(connect(fd, (sockaddr *)&a, sizeof(a)) == 0);
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    }
    ~Client() { close(fd); }

    void send_text(const std::string &text) { REQUIRE(send(fd, text.data(), text.size(), MSG_NOSIGNAL) == (ssize_t)text.size()); }

    /** Lets the server run a while and collects what it sent; notes whether it closed the connection. */
    void pump()
    {
        for (int i = 0; i < 40; i++) {
            api_update_server();
            usleep(5000);
            char buf[4096];
            for (;;) {
                const ssize_t n = recv(fd, buf, sizeof(buf), 0);
                if (n > 0) {
                    received.append(buf, (size_t)n);
                    continue;
                }
                if (n == 0)
                    peer_closed = true;
                break;
            }
        }
    }
};

} // namespace

TEST_CASE("api server: an HTTP request is dropped without an answer", "[kfx_script][api][security]") {
    RunningApi api;
    Client page(api.port);
    // what a web page can make a browser send to http://127.0.0.1:<port>/ , body included
    page.send_text("POST / HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Type: text/plain\r\nContent-Length: 55\r\n\r\n"
                   "{\"action\":\"console_command\",\"command\":\"lua print(1)\"}");
    page.pump();
    CHECK(page.peer_closed);
    CHECK(page.received.empty()); // no reply of any kind

    // the slot is free again for a real client
    Client real(api.port);
    real.send_text("{\"action\":\"hello\",\"ack\":1}\n");
    real.pump();
    CHECK(real.received.find("\"success\":true") != std::string::npos);
    CHECK_FALSE(real.peer_closed);
}

TEST_CASE("api server: lua cannot be run through console_command", "[kfx_script][api][security]") {
    RunningApi api;
    Client c(api.port);
    c.send_text("{\"action\":\"console_command\",\"command\":\"lua SECURITY_TEST_RAN = true\",\"ack\":7}\n"
                "{\"action\":\"console_command\",\"command\":\"!LUA SECURITY_TEST_RAN = true\",\"ack\":8}\n");
    c.pump();
    CHECK(c.received.find("COMMAND_NOT_ALLOWED") != std::string::npos);
    CHECK(c.received.find("\"ack\":7") != std::string::npos);
    CHECK(c.received.find("\"ack\":8") != std::string::npos);
    CHECK(c.received.find("\"success\":true") == std::string::npos);
}

TEST_CASE("api server: with a token set nothing runs before hello presents it", "[kfx_script][api][security]") {
    RunningApi api("test-token-1234");
    {
        Client c(api.port);
        c.send_text("{\"action\":\"console_command\",\"command\":\"ver\",\"ack\":1}\n");
        c.pump();
        CHECK(c.received.find("UNAUTHORIZED") != std::string::npos);
        CHECK(c.peer_closed);
    }
    {
        Client c(api.port);
        c.send_text("{\"action\":\"hello\",\"token\":\"wrong\",\"ack\":1}\n");
        c.pump();
        CHECK(c.received.find("UNAUTHORIZED") != std::string::npos);
        CHECK(c.peer_closed);
    }
    {
        Client c(api.port);
        c.send_text("{\"action\":\"hello\",\"ack\":1}\n"); // no token at all
        c.pump();
        CHECK(c.received.find("UNAUTHORIZED") != std::string::npos);
        CHECK(c.peer_closed);
    }
    {
        Client c(api.port);
        c.send_text("{\"action\":\"hello\",\"token\":\"test-token-1234\",\"ack\":1}\n"
                    "{\"action\":\"no_such_action\",\"ack\":2}\n");
        c.pump();
        CHECK(c.received.find("\"ack\":1") != std::string::npos);
        CHECK(c.received.find("UNAUTHORIZED") == std::string::npos);
        CHECK(c.received.find("UNKNOWN_ACTION") != std::string::npos); // reached the dispatcher
        CHECK_FALSE(c.peer_closed);
    }
}

TEST_CASE("api server: a token-less setup behaves as before", "[kfx_script][api][security]") {
    RunningApi api;
    Client c(api.port);
    c.send_text("  \n{\"action\":\"no_such_action\",\"ack\":3}\n");
    c.pump();
    CHECK(c.received.find("UNKNOWN_ACTION") != std::string::npos);
    CHECK_FALSE(c.peer_closed);
}


// Security pass S08: an empty message used to read the byte before its buffer (found by the API fuzzer).
TEST_CASE("an empty API message is answered with NO_JSON", "[kfx_script][api][security]") {
    std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
    kfx_sim_state.game_kind = GKind_LocalGame;
    int sv[2];
    REQUIRE(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    api_process_message_for_test("", 0, sv[0]);
    char reply[256] = {0};
    const ssize_t got = recv(sv[1], reply, sizeof(reply) - 1, MSG_DONTWAIT);
    CHECK(got > 0);
    CHECK(std::string(reply).find("NO_JSON") != std::string::npos);
    close(sv[0]);
    close(sv[1]);
}

#endif
