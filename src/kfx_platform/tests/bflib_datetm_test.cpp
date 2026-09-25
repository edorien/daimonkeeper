// kfx_platform: bflib_datetm.cpp. LbDateTimeDecode() is pure given an
// explicit time_t (unlike LbTime/LbDate/LbDateTime, which always read
// the real "now" and aren't attempted here) -- but it goes through
// localtime(), so the fixture pins TZ=UTC for the duration of each test
// to keep the decoded fields reproducible across machines/CI runners.
// get_trigger_time_measurement_fps() is pure given a hand-built
// TriggerTimeMeasurement (no chrono/SDL involved) -- trigger_time_
// measurement_capture() itself, which actually populates that struct
// from the real wall clock, is not attempted here since its one bit of
// real logic (the MAX_TRIGGER_TIME_CNT ring-buffer compaction) can't be
// exercised in a unit test without literally calling it 2000 times.
// framerate_measurement_capture()'s bounds-check early return is testable
// without the real clock too. Every other function in this file (LbTime*/
// LbDate*, LbSleep*, LbTimerInit, frametime_*) needs real wall-clock time
// or SDL_Delay and isn't attempted here -- see
// docs/Architecture/testing-harness.md §10.
#include <catch2/catch_test_macros.hpp>

#include "bflib_datetm.h"

#include <cstdlib>
#include <cstring>
#include <ctime>

namespace {
struct UtcTimezone {
    UtcTimezone() {
        setenv("TZ", "UTC", 1);
        tzset();
    }
    ~UtcTimezone() {
        unsetenv("TZ");
        tzset();
    }
};
}

TEST_CASE_METHOD(UtcTimezone, "LbDateTimeDecode decodes a known epoch timestamp into date and time fields", "[kfx_platform][bflib_datetm]") {
    time_t t = 946684800; // 2000-01-01T00:00:00Z, a Saturday
    struct TbDate date{};
    struct TbTime time_out{};
    CHECK(LbDateTimeDecode(&t, &date, &time_out) == Lb_SUCCESS);
    CHECK(date.Day == 1);
    CHECK(date.Month == 1);
    CHECK(date.Year == 2000);
    CHECK(date.DayOfWeek == 6); // Sunday=0 .. Saturday=6
    CHECK(time_out.Hour == 0);
    CHECK(time_out.Minute == 0);
    CHECK(time_out.Second == 0);
}

TEST_CASE_METHOD(UtcTimezone, "LbDateTimeDecode decodes the time-of-day portion of a non-midnight timestamp", "[kfx_platform][bflib_datetm]") {
    time_t t = 946684800 + (13 * 3600) + (45 * 60) + 30; // 13:45:30Z same day
    struct TbTime time_out{};
    CHECK(LbDateTimeDecode(&t, nullptr, &time_out) == Lb_SUCCESS);
    CHECK(time_out.Hour == 13);
    CHECK(time_out.Minute == 45);
    CHECK(time_out.Second == 30);
}

TEST_CASE_METHOD(UtcTimezone, "LbDateTimeDecode with a NULL curr_date only fills curr_time", "[kfx_platform][bflib_datetm]") {
    time_t t = 946684800;
    struct TbTime time_out{};
    time_out.Hour = 99; // sentinel, should be overwritten
    CHECK(LbDateTimeDecode(&t, nullptr, &time_out) == Lb_SUCCESS);
    CHECK(time_out.Hour == 0);
}

TEST_CASE_METHOD(UtcTimezone, "LbDateTimeDecode with a NULL curr_time only fills curr_date", "[kfx_platform][bflib_datetm]") {
    time_t t = 946684800;
    struct TbDate date{};
    CHECK(LbDateTimeDecode(&t, &date, nullptr) == Lb_SUCCESS);
    CHECK(date.Year == 2000);
}

TEST_CASE_METHOD(UtcTimezone, "LbDateTimeDecode with both outputs NULL still succeeds", "[kfx_platform][bflib_datetm]") {
    time_t t = 946684800;
    CHECK(LbDateTimeDecode(&t, nullptr, nullptr) == Lb_SUCCESS);
}

namespace {
struct ZeroedTrigger {
    ZeroedTrigger() { std::memset(&trigger, 0, sizeof(trigger)); }
    struct TriggerTimeMeasurement trigger;
};
}

TEST_CASE_METHOD(ZeroedTrigger, "get_trigger_time_measurement_fps is 0 when trigger_cnt is 0", "[kfx_platform][bflib_datetm]") {
    trigger.trigger_cnt = 0;
    CHECK(get_trigger_time_measurement_fps(&trigger) == 0);
}

TEST_CASE_METHOD(ZeroedTrigger, "get_trigger_time_measurement_fps is 1 for a single recorded trigger", "[kfx_platform][bflib_datetm]") {
    trigger.trigger_cnt = 1;
    trigger.trigger_time[0] = 500.0;
    CHECK(get_trigger_time_measurement_fps(&trigger) == 1);
}

TEST_CASE_METHOD(ZeroedTrigger, "get_trigger_time_measurement_fps counts every trigger within the last 1000ms window", "[kfx_platform][bflib_datetm]") {
    trigger.trigger_cnt = 3;
    trigger.trigger_time[0] = 0.0;
    trigger.trigger_time[1] = 500.0;
    trigger.trigger_time[2] = 999.0;
    CHECK(get_trigger_time_measurement_fps(&trigger) == 3);
}

TEST_CASE_METHOD(ZeroedTrigger, "get_trigger_time_measurement_fps stops counting once the gap from the last trigger reaches 1000ms", "[kfx_platform][bflib_datetm]") {
    trigger.trigger_cnt = 3;
    trigger.trigger_time[0] = 0.0;    // exactly 1000ms before the last -- excluded (>= threshold)
    trigger.trigger_time[1] = 500.0;  // 500ms before the last -- included
    trigger.trigger_time[2] = 1000.0; // the last trigger itself
    CHECK(get_trigger_time_measurement_fps(&trigger) == 2);
}

TEST_CASE("framerate_measurement_capture is a no-op for a negative framerate_kind", "[kfx_platform][bflib_datetm]") {
    framerate_measurement_capture(-1); // must not touch frametime_measurements or crash
    SUCCEED();
}

TEST_CASE("framerate_measurement_capture is a no-op for a framerate_kind at or past TOTAL_FRAMERATE_KINDS", "[kfx_platform][bflib_datetm]") {
    framerate_measurement_capture(TOTAL_FRAMERATE_KINDS); // one past the last valid index
    SUCCEED();
}
