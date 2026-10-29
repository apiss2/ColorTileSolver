#include "live_playback.h"
#include <cstdlib>
#include <iostream>

namespace {
void check(bool ok, const char* label) {
    if (!ok) { std::cerr << "FAIL: " << label << '\n'; std::exit(1); }
}
using A = ct::PlaybackAction;
struct Fixture {
    ct::Board first{5, 3, {0,8,0,2,0, 8,0,0,0,2, 0,8,0,2,0}};
    ct::Board second = first, final;
    std::vector<ct::Move> moves;
    Fixture() {
        ct::Move a, b;
        check(ct::apply_move(second, 6, &a), "first legal move");
        final = second; check(ct::apply_move(final, 8, &b), "second legal move");
        moves = {a, b};
    }
};
}
int main() {
    Fixture f; ct::VerifiedPlayback p; std::string error;
    check(!p.reset(f.first, {f.moves[0]}, true, 100, &error), "reject partial route");
    auto broken = f.moves; broken[0].removed.clear();
    check(!p.reset(f.first, broken, true, 100), "verify removal list");
    check(!p.reset(f.first, f.moves, true, 150), "reject sub-tenth intervals");
    check(p.reset(f.first, f.moves, true, 600), "valid route");
    check(!p.clicked(0), "cannot acknowledge unsolicited click");
    check(p.observe(&f.first, true, 1000) == A::Wait, "initial stability");
    check(p.observe(&f.first, true, 1060) == A::Click, "first click ready");
    check(p.clicked(1060), "first click acknowledged");
    check(!p.clicked(1061), "no double click");
    check(p.observe(&f.first, true, 1600) == A::Wait, "do not retry unconfirmed click");
    check(p.observe(nullptr, false, 1650) == A::Wait, "transient uncertainty");
    check(p.observe(&f.second, true, 1700) == A::Wait, "confirm next state");
    check(p.observe(&f.second, true, 1760) == A::Advanced && p.step() == 1, "advance only after real change");
    check(p.observe(&f.second, true, 1770) == A::Wait, "next stability");
    check(p.observe(&f.second, true, 1830) == A::Click, "next click ready");
    check(p.clicked(1830), "second acknowledged");
    check(p.observe(&f.final, true, 1900) == A::Wait, "final frame confirmation");
    check(p.observe(&f.final, true, 1960) == A::Complete && p.step() == 2, "full clear confirmed");
    check(p.observe(&f.first, true, 2000) == A::Complete, "terminal latch");

    check(p.reset(f.first, f.moves, true, 100), "reset timeout fixture");
    p.observe(&f.first, true, 100); p.observe(&f.first, true, 160); p.clicked(160);
    check(p.observe(&f.first, true, 2159) == A::Wait, "wait before deadline");
    check(p.observe(&f.first, true, 2160) == A::NoResponse, "timeout does not retry");
    check(!p.clicked(2300), "timeout cannot click again");

    check(p.reset(f.first, f.moves, false, 100), "hint fixture");
    check(p.observe(&f.first, true, 0) == A::Wait, "hint idle");
    check(p.observe(&f.first, true, 5000) == A::Wait && !p.clicked(5000), "hint never requests input");
    p.observe(&f.second, true, 5100);
    check(p.observe(&f.second, true, 5160) == A::Advanced, "hint follows actual user click");
    ct::Board changed = f.second; changed.cells[0] = 4;
    check(p.observe(&changed, true, 5200) == A::Wait, "changed board grace");
    check(p.observe(&changed, true, 5700) == A::BoardChanged, "unplanned change stops");

    check(p.reset(f.first, f.moves, true, 2000), "interval fixture");
    p.observe(&f.first, true, 100); p.observe(&f.first, true, 160); p.clicked(160);
    p.observe(&f.second, true, 200); p.observe(&f.second, true, 260);
    p.observe(&f.second, true, 300);
    check(p.observe(&f.second, true, 400) == A::Wait, "respect interval after ack");
    p.set_interval(100);
    check(p.observe(&f.second, true, 500) == A::Click, "changed interval applies");
    check(p.reset(f.first, f.moves, true, 100), "uncertain fixture");
    p.observe(&f.first, false, 1000);
    check(p.observe(&f.first, false, 1500) == A::BoardChanged, "unreliable frames cannot authorize clicks");
    std::cout << "PASS: verified screen playback state, interval, no retries, manual hints, mismatch and timeout\n";
}
