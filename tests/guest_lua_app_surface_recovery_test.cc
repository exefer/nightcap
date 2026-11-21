#include "window/guest_lua_app_surface_recovery.h"

#include <gtest/gtest.h>

#include <vector>

#include "window/window_game_surface_bridge.h"
#include "window/window_surface_lifecycle.h"

namespace mocktail {
namespace window {
namespace {

constexpr uintptr_t kNativeWindow = 0x1000;
constexpr uint64_t kStableMs = GuestLuaAppSurfaceRecovery::kStableExtentMs;

uint64_t g_now_ms = 0;

uint64_t Now() { return g_now_ms; }

struct RestartProbe {
  std::vector<runtime::GameSurface> restarts;
  Status result = Status::Ok();
};

Status Restart(void* context, const runtime::GameSurface& surface) {
  auto* probe = static_cast<RestartProbe*>(context);
  probe->restarts.push_back(surface);
  return probe->result;
}

runtime::GameSurface Surface(uint64_t generation, uint32_t width = 948,
                             uint32_t height = 1034) {
  return {generation, kNativeWindow, width, height, 1.0f};
}

// The engine starts on generation 1 at 948x1034, with the extent last changed
// at t=0, and the window has been stable since.
class GuestLuaAppSurfaceRecoveryTest : public testing::Test {
 protected:
  void SetUp() override { g_now_ms = 2 * kStableMs; }

  GuestLuaAppSurfaceRecovery MakeRecovery(uint64_t generation = 1) {
    return GuestLuaAppSurfaceRecovery(&probe_, &Restart, Surface(generation), 0,
                                      &Now);
  }

  RestartProbe probe_;
};

TEST_F(GuestLuaAppSurfaceRecoveryTest,
       ResizeWithinTheEngineGenerationIsIgnored) {
  GuestLuaAppSurfaceRecovery recovery = MakeRecovery();

  const auto result = recovery.Changed(Surface(1, 1904, 1034));

  EXPECT_TRUE(result.ok());
  EXPECT_FALSE(result.changed());
  EXPECT_TRUE(probe_.restarts.empty());
}

TEST_F(GuestLuaAppSurfaceRecoveryTest,
       RecreatedSurfaceAtAStableExtentRestartsOnChangedOnly) {
  GuestLuaAppSurfaceRecovery recovery = MakeRecovery();

  EXPECT_TRUE(recovery.Destroyed(1).ok());
  EXPECT_TRUE(recovery.Created(2).ok());
  EXPECT_TRUE(probe_.restarts.empty());
  EXPECT_TRUE(recovery.restart_pending());

  const auto result = recovery.Changed(Surface(2));

  EXPECT_TRUE(result.changed());
  ASSERT_EQ(probe_.restarts.size(), 1u);
  EXPECT_EQ(probe_.restarts[0].generation, 2u);
  EXPECT_EQ(probe_.restarts[0].width, 948u);
  EXPECT_EQ(recovery.engine_surface_generation(), 2u);
  EXPECT_FALSE(recovery.restart_pending());
}

TEST_F(GuestLuaAppSurfaceRecoveryTest,
       NewGenerationWithoutCreatedStillRestarts) {
  GuestLuaAppSurfaceRecovery recovery = MakeRecovery();

  EXPECT_TRUE(recovery.Changed(Surface(3)).changed());
  EXPECT_EQ(probe_.restarts.size(), 1u);
}

TEST_F(GuestLuaAppSurfaceRecoveryTest, StaleDestroyedDoesNotArmARestart) {
  GuestLuaAppSurfaceRecovery recovery = MakeRecovery(2);

  EXPECT_FALSE(recovery.Destroyed(1).changed());
  EXPECT_FALSE(recovery.restart_pending());
  EXPECT_FALSE(recovery.Changed(Surface(2)).changed());
  EXPECT_TRUE(probe_.restarts.empty());
}

TEST_F(GuestLuaAppSurfaceRecoveryTest, FailedRestartIsNotFatalAndIsRetried) {
  probe_.result = Status::Error(StatusCode::kUnavailable, "engine busy");
  GuestLuaAppSurfaceRecovery recovery = MakeRecovery();

  recovery.Created(2);
  const auto failed = recovery.Changed(Surface(2));

  // A rejected update would make the bridge terminal and exit the process.
  EXPECT_TRUE(failed.ok());
  EXPECT_EQ(recovery.failed_restarts(), 1u);
  EXPECT_TRUE(recovery.restart_pending());
  EXPECT_EQ(recovery.engine_surface_generation(), 1u);

  probe_.result = Status::Ok();
  EXPECT_TRUE(recovery.Changed(Surface(2)).changed());
  EXPECT_EQ(recovery.restarts(), 1u);
  EXPECT_FALSE(recovery.restart_pending());
}

TEST_F(GuestLuaAppSurfaceRecoveryTest, MissingRestartHookIsNotFatal) {
  GuestLuaAppSurfaceRecovery recovery(nullptr, nullptr, Surface(1), 0, &Now);

  recovery.Created(2);

  EXPECT_TRUE(recovery.Changed(Surface(2)).ok());
  EXPECT_EQ(recovery.failed_restarts(), 1u);
}

TEST_F(GuestLuaAppSurfaceRecoveryTest,
       NewGenerationAtADifferentExtentAdoptsWithoutRestarting) {
  GuestLuaAppSurfaceRecovery recovery = MakeRecovery();

  recovery.Created(2);
  const auto result = recovery.Changed(Surface(2, 1471, 828));

  EXPECT_TRUE(result.ok());
  EXPECT_FALSE(result.changed());
  EXPECT_TRUE(probe_.restarts.empty());
  EXPECT_EQ(recovery.engine_surface_generation(), 2u);
  EXPECT_FALSE(recovery.restart_pending());
}

TEST_F(GuestLuaAppSurfaceRecoveryTest,
       NewGenerationSoonAfterASizeChangeAdoptsWithoutRestarting) {
  GuestLuaAppSurfaceRecovery recovery = MakeRecovery();

  EXPECT_FALSE(recovery.Changed(Surface(1, 1471, 828)).changed());
  g_now_ms += kStableMs - 1;
  recovery.Created(2);
  EXPECT_FALSE(recovery.Changed(Surface(2, 1471, 828)).changed());

  EXPECT_TRUE(probe_.restarts.empty());
  EXPECT_EQ(recovery.engine_surface_generation(), 2u);
  EXPECT_FALSE(recovery.restart_pending());
}

TEST_F(GuestLuaAppSurfaceRecoveryTest,
       NewGenerationOnceTheExtentHasBeenStableForTheWindowRestarts) {
  GuestLuaAppSurfaceRecovery recovery = MakeRecovery();

  EXPECT_FALSE(recovery.Changed(Surface(1, 1471, 828)).changed());
  g_now_ms += kStableMs;
  recovery.Created(2);
  EXPECT_TRUE(recovery.Changed(Surface(2, 1471, 828)).changed());

  ASSERT_EQ(probe_.restarts.size(), 1u);
  EXPECT_EQ(probe_.restarts[0].width, 1471u);
  EXPECT_EQ(probe_.restarts[0].height, 828u);
}

TEST_F(GuestLuaAppSurfaceRecoveryTest,
       ResizeEndingAfterAnAdoptedRecreationDoesNotRestart) {
  GuestLuaAppSurfaceRecovery recovery = MakeRecovery();

  recovery.Created(2);
  recovery.Changed(Surface(2, 1471, 828));
  g_now_ms += 40;

  // The drag ends at a different size within the adopted generation.
  EXPECT_FALSE(recovery.Changed(Surface(2, 1335, 752)).changed());
  EXPECT_TRUE(probe_.restarts.empty());
}

bool PollLifecycle(void* context, WindowSurfaceEvent* event) {
  return static_cast<WindowSurfaceLifecycle*>(context)->Poll(event);
}

class GuestLuaAppSurfaceRecoveryBridgeTest
    : public GuestLuaAppSurfaceRecoveryTest {
 protected:
  void SetUp() override {
    GuestLuaAppSurfaceRecoveryTest::SetUp();
    ASSERT_TRUE(lifecycle_.Activate(kNativeWindow, 948, 1034, 1.0f).ok());
  }

  void Resize(uint32_t width, uint32_t height) {
    g_now_ms += 16;
    ASSERT_TRUE(lifecycle_.Observe(kNativeWindow, width, height, 1.0f).ok());
    ASSERT_TRUE(bridge_.Drain().ok());
  }

  void RecreateSurface() {
    g_now_ms += 16;
    ASSERT_TRUE(lifecycle_.Recreate().ok());
    ASSERT_TRUE(bridge_.Drain().ok());
  }

  WindowSurfaceLifecycle lifecycle_;
  GuestLuaAppSurfaceRecovery recovery_ = MakeRecovery();
  WindowGameSurfaceBridge bridge_{{&lifecycle_, &PollLifecycle},
                                  recovery_.Consumer()};
};

// The host reports the swapchain out of date although the extent never
// changes, and every recovery recreates the surface at the same size. Each
// recreation must reach the engine exactly once.
TEST_F(GuestLuaAppSurfaceRecoveryBridgeTest,
       RepeatedSameSizeRecoveriesEachRestartTheEngineOnce) {
  EXPECT_TRUE(bridge_.Drain().ok());
  EXPECT_TRUE(probe_.restarts.empty());

  for (uint64_t generation = 2; generation <= 4; ++generation) {
    g_now_ms += 16;
    ASSERT_TRUE(lifecycle_.Recreate().ok());
    const WindowGameSurfaceDrainResult drained = bridge_.Drain();
    ASSERT_TRUE(drained.ok());
    EXPECT_EQ(drained.drained_events, 3u);  // Destroyed, Created, Changed.
    ASSERT_EQ(probe_.restarts.size(), generation - 1);
    EXPECT_EQ(probe_.restarts.back().generation, generation);
    EXPECT_EQ(probe_.restarts.back().width, 948u);
    EXPECT_EQ(probe_.restarts.back().height, 1034u);
  }
}

TEST_F(GuestLuaAppSurfaceRecoveryBridgeTest,
       PlainResizeThroughTheBridgeDoesNotRestart) {
  Resize(1904, 1034);

  EXPECT_TRUE(probe_.restarts.empty());
}

TEST_F(GuestLuaAppSurfaceRecoveryBridgeTest,
       ObservingTheLiveExtentBeforeRecreatingDoesNotRestart) {
  ASSERT_TRUE(lifecycle_.Observe(kNativeWindow, 1798, 1034, 1.0f).ok());
  RecreateSurface();

  EXPECT_TRUE(probe_.restarts.empty());
  EXPECT_EQ(recovery_.engine_surface_generation(), 2u);
}

// A drag-resize with the host reporting the swapchain out of date partway
// through. The restart would lay the UI out at a size the window has already
// left, so it must not happen; once the window settles, a later recreation at
// the settled size still restarts.
TEST_F(GuestLuaAppSurfaceRecoveryBridgeTest,
       RecreateDuringAResizeSequenceDoesNotRestart) {
  Resize(1604, 1034);
  Resize(1485, 1034);
  Resize(1471, 828);
  RecreateSurface();
  Resize(1400, 800);
  Resize(1335, 752);

  EXPECT_TRUE(probe_.restarts.empty());
  EXPECT_EQ(recovery_.engine_surface_generation(), 2u);

  g_now_ms += kStableMs;
  RecreateSurface();

  ASSERT_EQ(probe_.restarts.size(), 1u);
  EXPECT_EQ(probe_.restarts[0].generation, 3u);
  EXPECT_EQ(probe_.restarts[0].width, 1335u);
  EXPECT_EQ(probe_.restarts[0].height, 752u);
}

}  // namespace
}  // namespace window
}  // namespace mocktail
