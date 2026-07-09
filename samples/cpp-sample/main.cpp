#include <xray/inspector.hpp>
#include <thread>
#include <chrono>

// ── Game state exposed to Lua REPL ──
int player_hp = 100;
float player_pos[3] = {0.0f, 0.0f, 0.0f};
float bullet_speed = 50.0f;
bool god_mode = false;

void simulate_frame()
{
    // Move player
    player_pos[0] += 1.0f;
    player_pos[1] += 0.5f;

    // Players lose HP over time
    if (!god_mode && player_hp > 0) {
        player_hp -= 1;
    }

    // Log something every frame
    xb::Inspector::log_info("GAME", "Frame: hp=%d pos=(%.1f,%.1f)",
        player_hp, player_pos[0], player_pos[1]);
}

int main()
{
    // 1. Start inspector (spawns network thread, opens port 9000)
    xb::Inspector::start("XraySample");

    // 2. Bind variables — Vault can read/write these in real time
    xb::Inspector::bind("player_hp", &player_hp);
    xb::Inspector::bind("bullet_speed", &bullet_speed);
    xb::Inspector::bind("god_mode", &god_mode);
    xb::Inspector::bind_array("player_pos", player_pos, 3);

    // 3. Game loop
    for (int frame = 0; frame < 500; ++frame) {
        // IMPORTANT: consume REPL commands at frame start
        xb::Inspector::update();

        simulate_frame();

        // Wait a bit so Vault can observe
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // 4. Cleanup
    xb::Inspector::stop();
    return 0;
}
