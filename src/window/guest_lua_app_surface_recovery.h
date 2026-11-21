#ifndef MOCKTAIL_WINDOW_GUEST_LUA_APP_SURFACE_RECOVERY_H_
#define MOCKTAIL_WINDOW_GUEST_LUA_APP_SURFACE_RECOVERY_H_

#include <cstdint>

#include "mocktail/status.h"
#include "runtime/game_session_coordinator.h"
#include "window/window_game_surface_bridge.h"

namespace mocktail {
namespace window {

// Restarts the LuaApp on a recreated surface. It must hand the engine a new
// Surface object: Roblox only rebuilds its framebuffer on a new identity.
using GuestLuaAppRestartFn = Status (*)(void* context,
                                        const runtime::GameSurface& surface);

// Surface recovery for the LuaApp before it has an authenticated session.
// RobloxExperienceComposition does this for authenticated sessions; without
// it the login screen keeps presenting into an obsolete swapchain until the
// window is resized.
//
// A new surface generation restarts the LuaApp once, on the Changed event that
// carries the final extent, but only when the extent is unchanged and has been
// stable for kStableExtentMs: the host invalidated the swapchain without a
// size change. A generation recreated while the window is being resized is
// adopted without a restart, since the restart would lay the UI out at an
// extent that is already stale. The engine follows resizes through the surface
// capabilities, so same-generation Changed events only update the tracked
// extent.
class GuestLuaAppSurfaceRecovery final {
 public:
  using Clock = uint64_t (*)();

  static constexpr uint64_t kStableExtentMs = 500;

  // The default clock for `now_ms` and for the `extent_changed_ms` seed.
  static uint64_t SteadyClockMs();

  // `engine_surface` and `extent_changed_ms` describe the surface the engine
  // was started with and when its extent was last observed to change.
  GuestLuaAppSurfaceRecovery(void* restart_context,
                             GuestLuaAppRestartFn restart,
                             const runtime::GameSurface& engine_surface,
                             uint64_t extent_changed_ms,
                             Clock now_ms = nullptr);

  GuestLuaAppSurfaceRecovery(const GuestLuaAppSurfaceRecovery&) = delete;
  GuestLuaAppSurfaceRecovery& operator=(const GuestLuaAppSurfaceRecovery&) =
      delete;

  runtime::GameSessionUpdateResult Created(uint64_t generation);
  runtime::GameSessionUpdateResult Changed(runtime::GameSurface surface);
  runtime::GameSessionUpdateResult Destroyed(uint64_t generation);

  // The recovery must outlive the returned consumer.
  WindowGameSurfaceConsumer Consumer();

  uint64_t engine_surface_generation() const { return engine_generation_; }
  bool restart_pending() const { return pending_; }
  uint64_t restarts() const { return restarts_; }
  uint64_t failed_restarts() const { return failed_restarts_; }

 private:
  uint64_t NowMs() const;

  void* restart_context_;
  GuestLuaAppRestartFn restart_;
  Clock now_ms_;
  uint64_t engine_generation_;
  uint32_t extent_width_;
  uint32_t extent_height_;
  uint64_t extent_changed_ms_;
  bool pending_ = false;
  uint64_t restarts_ = 0;
  uint64_t failed_restarts_ = 0;
};

}  // namespace window
}  // namespace mocktail

#endif  // MOCKTAIL_WINDOW_GUEST_LUA_APP_SURFACE_RECOVERY_H_
