// kfx_platform: LbLogSanitizeAddresses() (upstream #5386, "hide IPs"): every log line goes out with its
// IPv4/IPv6 addresses replaced by a number that is the same for the same address all run long.
#include <catch2/catch_test_macros.hpp>

#include "bflib_basics.h"

#include <string>

namespace {
std::string sanitized(const char *message) {
    char out[512];
    LbLogSanitizeAddresses(message, out, sizeof(out));
    return out;
}
}

TEST_CASE("log lines keep no IP address, and the same address keeps its number", "[kfx_platform][log_sanitize]") {
    const std::string a = sanitized("Join: connected to 192.168.1.20:5556 via LAN");
    CHECK(a.find("192.168") == std::string::npos);
    CHECK(a.find("IPv4#") != std::string::npos);
    CHECK(a.rfind("Join: connected to IPv4#", 0) == 0);
    // the same address, another line: the same number
    CHECK(a.find(":5556") != std::string::npos); // the port stays
    const std::string number = a.substr(a.find("IPv4#"), a.find(':', a.find("IPv4#")) - a.find("IPv4#"));
    CHECK(sanitized("peer 192.168.1.20 dropped") == "peer " + number + " dropped");

    const std::string v6 = sanitized("punch received at 2001:db8::1 and ::ffff:10.0.0.1.");
    CHECK(v6.find("2001") == std::string::npos);
    CHECK(v6.find("10.0.0.1") == std::string::npos);
    CHECK(v6.find("IPv6#") != std::string::npos);
    CHECK(v6.find("IPv4#") != std::string::npos); // an IPv4-mapped address counts as IPv4
}

TEST_CASE("log lines without addresses are left alone", "[kfx_platform][log_sanitize]") {
    for (const char *line : {"GameTurn 1234 at 12:34:56", "Loaded map00001.slb (3.5 KB)", "ratio 1.5, scale 0.25",
                             "version 1.0.0", "dead:beef is not an address", "Error: 256.1.1.1 is out of range"}) {
        INFO(line);
        CHECK(sanitized(line) == line);
    }
}
