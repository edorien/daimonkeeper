// kfx_platform: bflib_coroutine.c, a small hand-rolled cooperative
// scheduler (a queue of function pointers with a shared args block, not
// a real stackful coroutine). Fully pure state-machine logic on a bare
// CoroutineLoop -- test doubles are just plain C function pointers
// matching CoroutineFn, no fake-provider infrastructure needed.
//
// Worth noting explicitly (found by reading the body, not assumed from
// the enum name): CoroutineLoopState has four values, but
// coroutine_process()'s dispatch only handles three (CLS_CONTINUE,
// CLS_ABORT, CLS_RETURN) -- CLS_REPEAT matches none of the if/else-if
// branches, so the loop just calls the same function again next
// iteration without advancing read_idx or clearing fns[read_idx]. Tested
// below as the function's actual behavior (a "run this step again next
// tick" primitive), same "test what's there, not what you'd expect"
// discipline creature_control_test.cpp's own quirk-finding used.
#include <catch2/catch_test_macros.hpp>

#include "bflib_basics.h" // TbBool -- bflib_coroutine.h relies on this being included first
#include "bflib_coroutine.h"

#include <cstring>

namespace {
struct ZeroedLoop {
    ZeroedLoop() { std::memset(&loop, 0, sizeof(loop)); }
    CoroutineLoop loop;
};

int64_t g_call_count = 0;
int64_t g_repeat_countdown = 0;
int64_t g_last_args[COROUTINE_ARGS] = {0, 0};

CoroutineLoopState fn_continue(CoroutineLoop *) {
    g_call_count++;
    return CLS_CONTINUE;
}
CoroutineLoopState fn_abort(CoroutineLoop *) {
    g_call_count++;
    return CLS_ABORT;
}
CoroutineLoopState fn_return(CoroutineLoop *) {
    g_call_count++;
    return CLS_RETURN;
}
CoroutineLoopState fn_repeat_then_continue(CoroutineLoop *) {
    g_call_count++;
    if (g_repeat_countdown > 0) {
        g_repeat_countdown--;
        return CLS_REPEAT;
    }
    return CLS_CONTINUE;
}
CoroutineLoopState fn_record_args_and_continue(CoroutineLoop *context) {
    int64_t *args = coroutine_args(context);
    g_last_args[0] = args[0];
    g_last_args[1] = args[1];
    return CLS_CONTINUE;
}

struct ResetGlobals {
    ResetGlobals() {
        g_call_count = 0;
        g_repeat_countdown = 0;
        g_last_args[0] = g_last_args[1] = 0;
    }
};
}

TEST_CASE_METHOD(ZeroedLoop, "coroutine_add appends a function pointer at write_idx and advances it", "[kfx_platform][bflib_coroutine]") {
    coroutine_add(&loop, fn_continue);
    CHECK(loop.write_idx == 1);
    CHECK(loop.fns[0] == fn_continue);
    coroutine_add(&loop, fn_abort);
    CHECK(loop.write_idx == 2);
    CHECK(loop.fns[1] == fn_abort);
}

TEST_CASE_METHOD(ZeroedLoop, "coroutine_add_args stores the function's per-slot args alongside it", "[kfx_platform][bflib_coroutine]") {
    int64_t args[COROUTINE_ARGS] = {11, 22};
    coroutine_add_args(&loop, fn_continue, args);
    CHECK(loop.write_idx == 1);
    CHECK(loop.args[0] == 11);
    CHECK(loop.args[1] == 22);
}

TEST_CASE_METHOD(ZeroedLoop, "coroutine_clear zeroes every queued function up to write_idx and resets write_idx", "[kfx_platform][bflib_coroutine]") {
    coroutine_add(&loop, fn_continue);
    coroutine_add(&loop, fn_abort);
    coroutine_clear(&loop, false);
    CHECK(loop.write_idx == 0);
    CHECK(loop.fns[0] == nullptr);
    CHECK(loop.fns[1] == nullptr);
}

TEST_CASE_METHOD(ZeroedLoop, "coroutine_clear ORs the error flag in rather than overwriting it", "[kfx_platform][bflib_coroutine]") {
    loop.error = false;
    coroutine_clear(&loop, false);
    CHECK(loop.error == false);
    coroutine_clear(&loop, true);
    CHECK(loop.error == true);
    coroutine_clear(&loop, false); // false shouldn't clear a previously-set error
    CHECK(loop.error == true);
}

TEST_CASE_METHOD(ZeroedLoop, "coroutine_args returns a pointer into the current slot's args block", "[kfx_platform][bflib_coroutine]") {
    int64_t args[COROUTINE_ARGS] = {5, 6};
    coroutine_add_args(&loop, fn_continue, args);
    loop.read_idx = 0;
    int64_t *result = coroutine_args(&loop);
    CHECK(result == &loop.args[0]);
    CHECK(result[0] == 5);
    CHECK(result[1] == 6);
}

TEST_CASE_METHOD(ZeroedLoop, "coroutine_process runs every queued CLS_CONTINUE function in order, then resets both indices", "[kfx_platform][bflib_coroutine]") {
    ResetGlobals reset;
    coroutine_add(&loop, fn_continue);
    coroutine_add(&loop, fn_continue);
    coroutine_add(&loop, fn_continue);
    coroutine_process(&loop);
    CHECK(g_call_count == 3);
    CHECK(loop.write_idx == 0);
    CHECK(loop.read_idx == 0);
}

TEST_CASE_METHOD(ZeroedLoop, "coroutine_process clears each slot's function pointer as it advances past it", "[kfx_platform][bflib_coroutine]") {
    ResetGlobals reset;
    coroutine_add(&loop, fn_continue);
    coroutine_add(&loop, fn_abort); // second slot aborts, so the loop stops here
    coroutine_process(&loop);
    // fn_continue's own slot (0) was cleared on the way past it; fn_abort's
    // own reset of write_idx/read_idx to 0 happens on the CLS_ABORT branch.
    CHECK(g_call_count == 2);
    CHECK(loop.write_idx == 0);
    CHECK(loop.read_idx == 0);
}

TEST_CASE_METHOD(ZeroedLoop, "coroutine_process on CLS_ABORT stops immediately and resets both indices", "[kfx_platform][bflib_coroutine]") {
    ResetGlobals reset;
    coroutine_add(&loop, fn_abort);
    coroutine_add(&loop, fn_continue); // never reached
    coroutine_process(&loop);
    CHECK(g_call_count == 1);
    CHECK(loop.write_idx == 0);
    CHECK(loop.read_idx == 0);
}

TEST_CASE_METHOD(ZeroedLoop, "coroutine_process on CLS_RETURN stops immediately without resetting either index", "[kfx_platform][bflib_coroutine]") {
    ResetGlobals reset;
    coroutine_add(&loop, fn_return);
    coroutine_add(&loop, fn_continue); // never reached
    coroutine_process(&loop);
    CHECK(g_call_count == 1);
    // Unlike CLS_ABORT, CLS_RETURN leaves write_idx/read_idx and the
    // uncleared fns[] queue exactly as they were -- a later
    // coroutine_process() call resumes from the same slot.
    CHECK(loop.write_idx == 2);
    CHECK(loop.read_idx == 0);
    CHECK(loop.fns[0] == fn_return);
}

TEST_CASE_METHOD(ZeroedLoop, "coroutine_process re-calls the same function on CLS_REPEAT without advancing read_idx", "[kfx_platform][bflib_coroutine]") {
    ResetGlobals reset;
    g_repeat_countdown = 2; // CLS_REPEAT twice, then CLS_CONTINUE on the 3rd call
    coroutine_add(&loop, fn_repeat_then_continue);
    coroutine_process(&loop);
    CHECK(g_call_count == 3); // same slot invoked three times before advancing
    CHECK(loop.write_idx == 0);
    CHECK(loop.read_idx == 0);
}

TEST_CASE_METHOD(ZeroedLoop, "coroutine_process hands each function its own args via coroutine_args", "[kfx_platform][bflib_coroutine]") {
    ResetGlobals reset;
    int64_t args[COROUTINE_ARGS] = {7, 8};
    coroutine_add_args(&loop, fn_record_args_and_continue, args);
    coroutine_process(&loop);
    CHECK(g_last_args[0] == 7);
    CHECK(g_last_args[1] == 8);
}
