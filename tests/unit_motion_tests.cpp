#include "ribterra/unit_motion.hpp"
#include "ribterra/unit_animation.hpp"
#include "ribterra/resident_art.hpp"
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>

using namespace ribterra;
void check(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}

void playback(int speed, bool remote, bool jitter) {
    MotionClock clock;
    UnitMotion track;
    UnitAnimation walk;
    std::uint64_t received = 0;
    const int cadence = remote ? 4 : 1;
    track.add({0, 0}, 0);
    float previous = 0;
    int moved_without_packet = 0, advancing_frames = 0;
    std::set<int> phases;
    for (int frame = 0; frame <= 600; ++frame) {
        const double now = frame / 60.0;
        // Alternate 30ms delivery jitter, preserving packet order.
        const auto due = [&](std::uint64_t tick) {
            return double(tick) * .05 / speed + (jitter && (tick / (cadence * speed)) % 2 ? .03 : 0);
        };
        bool packet = false;
        while (due(received + cadence * speed) <= now) {
            received += cadence * speed;
            check(!track.add({float(received) * .125f, 0}, received), "Normal network walking classified as teleport");
            packet = true;
        }
        clock.update(received, now, speed, false, remote);
        const auto p = track.sample(clock.tick());
        check(p.x >= previous - .00001f, "Presentation moved backwards");
        check(p.x - previous <= float(speed) * .05f, "Packet arrival caused a visible position jump");
        if (frame > 120 && p.x - previous > .00001f) {
            ++advancing_frames;
            if (!packet) ++moved_without_packet;
        }
        walk.update(p.x, p.y, clock.tick());
        if (frame > 120) phases.insert(walk.frame());
        previous = p.x;
    }
    check(moved_without_packet > 150, "Movement did not fill frames between snapshots");
    check(advancing_frames > 400, "Buffered motion repeatedly stopped");
    check(phases.size() == 4, "Network motion skipped the walking cycle");
    const auto frozen = track.sample(clock.tick());
    const auto phase = walk.frame();
    for (int i = 1; i <= 120; ++i) {
        clock.update(received, 10 + i / 60.0, speed, true, remote);
        const auto p = track.sample(clock.tick());
        walk.update(p.x, p.y, clock.tick());
        check(p.x == frozen.x && walk.frame() == phase, "Paused motion or animation advanced");
    }
    // With no new packets, displayed units stop at the newest authority position.
    for (int i = 1; i <= 120; ++i) clock.update(received, 12 + i / 60.0, speed, false, remote);
    check(track.sample(clock.tick()).x <= float(received) * .125f, "Starved client extrapolated beyond authority");
}

int main() {
    try {
        const float vectors[8][2]{{1,-1},{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1},{0,-1}};
        for (int direction=0; direction<8; ++direction) {
            UnitAnimation animation;
            animation.update(10,10,1);
            animation.update(10+vectors[direction][0]*.1f,10+vectors[direction][1]*.1f,2);
            check(animation.facing==direction,"Isometric walk direction mapped to wrong atlas column");
            animation.update(animation.x,animation.y,3);
            check(animation.facing==direction && !animation.moving,"Idle resident lost its facing");
            animation.update(99,99,3);
            check(animation.facing==direction && animation.x<11,"Paused animation changed position or facing");
        }
        for (int item=0; item<15; ++item) {
            check(resident_load(static_cast<ItemKind>(item),0)==ResidentLoad::Empty,"Empty carrier still shows cargo");
            check(resident_load(static_cast<ItemKind>(item),1)!=ResidentLoad::Empty,"Actual cargo invisible");
        }
        check(resident_load(ItemKind::Wood,10)==ResidentLoad::Logs,"Wood must be carried in arms");
        check(resident_load(ItemKind::Grain,10)==ResidentLoad::Sack,"Grain must use a sack");
        check(resident_load(ItemKind::Stone,10)==ResidentLoad::Cart,"Heavy material must use a cart");
        check(resident_load(ItemKind::Stone,2)==ResidentLoad::Sack,"Small load must not create a full cart");
        check(resident_sprite_width(ResidentLoad::Empty)>=39.f,"V03 resident scale is not readable");
        check(resident_contact_shadow_half_width(ResidentLoad::Cart)>
                  resident_contact_shadow_half_width(ResidentLoad::Empty),
              "Cart contact shadow does not cover its wider ground contact");
        check(resident_contact_shadow_half_height>0,"Resident contact shadow has no height");
        for (int speed : {1, 2, 4}) {
            playback(speed, false, false);
            playback(speed, true, false);
            playback(speed, true, true);
        }
        UnitMotion track;
        track.add({0, 0}, 0);
        check(!track.add({2.7f, 0}, 16), "Fast road motion considered a teleport");
        check(track.sample(8).x > 1 && track.sample(8).x < 2, "Road motion not interpolated");
        check(track.add({100, 100}, 17), "Teleport interpolated across world");
        check(track.sample(8).x == 100, "Old teleport history survived");
        check(track.add({2, 3, WorldLayer::Mine}, 18), "Layer transition interpolated");
        check(track.sample(17).layer == WorldLayer::Mine, "Old layer survived transition");
        check(track.add({5, 6}, 1), "World time rollback did not reset history");
        MotionClock clock;
        clock.update(0, 0, 1, false, true);
        check(clock.update(500, .01, 1, false, true) && clock.tick() == 500,
              "Client replayed old lobby/background history instead of rebasing");
        clock.update(100, 10, 1, false, true);
        check(clock.update(1, 11, 1, false, true) && clock.tick() == 1, "Clock rollback not reset");
        check(clock.update(200, 0, 1, false, true) && clock.tick() == 200, "New session time not reset");
        std::cout << "Smooth offline/online motion at 1x/2x/4x, jitter, pause, starvation and transitions passed.\n";
    } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; }
}
