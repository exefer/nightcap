#ifndef MOCKTAIL_WINDOW_GUEST_LUA_APP_SURFACE_RECOVERY_H_
#define MOCKTAIL_WINDOW_GUEST_LUA_APP_SURFACE_RECOVERY_H_

#include <cstdint>

#include "mocktail/status.h"
#include "runtime/game_session_coordinator.h"
#include "window/window_game_surface_bridge.h"

namespace mocktail {
namespace window {

// Re-enters the LuaApp with a recreated surface. Implemented by the runtime
// that owns the engine entry points; it must hand the engine a new Surface
// object, because Roblox only rebuilds its framebuffer when the surface
// identity changes.
using GuestLuaAppRestartFn = Status (*)(void* context,
                                        const runtime::GameSurface& surface);

// Surface recovery for the LuaApp before it has an authenticated session.
//
// The host reports VK_ERROR_OUT_OF_DATE_KHR and queues a recreated surface
// (Destroyed -> Created -> Changed with a new generation). For an
// authenticated session RobloxExperienceComposition restarts the LuaApp onto
// the new surface. The welcome/login screen has no such session, so nothing
// consumed those events: the engine kept presenting into an obsolete swapchain
// and the window stayed on its first frame until its extent happened to change.
//
// This consumer applies the same rule to that stage: a new surface generation
// restarts the LuaApp once, on the Changed event that carries the final
// extent. Same-generation Changed events are plain resizes, which the engine
// already follows through the surface capabilities, so they are ignored.
class GuestLuaAppSurfaceRecovery final {
 public:
  // `engine_surface_generation` is the generation of the surface the engine
  // was started with.
  GuestLuaAppSurfaceRecovery(void* restart_context, GuestLuaAppRestartFn restart,
                             uint64_t engine_surface_generation);

  GuestLuaAppSurfaceRecovery(const GuestLuaAppSurfaceRecovery&) = delete;
  GuestLuaAppSurfaceRecovery& operator=(const GuestLuaAppSurfaceRecovery&) =
      delete;

  runtime::GameSessionUpdateResult Created(uint64_t generation);
  runtime::GameSessionUpdateResult Changed(runtime::GameSurface surface);
  runtime::GameSessionUpdateResult Destroyed(uint64_t generation);

  // Consumer for WindowGameSurfaceBridge. The recovery must outlive it.
  WindowGameSurfaceConsumer Consumer();

  uint64_t engine_surface_generation() const { return engine_generation_; }
  bool restart_pending() const { return pending_; }
  uint64_t restarts() const { return restarts_; }
  uint64_t failed_restarts() const { return failed_restarts_; }

 private:
  void* restart_context_;
  GuestLuaAppRestartFn restart_;
  uint64_t engine_generation_;
  bool pending_ = false;
  uint64_t restarts_ = 0;
  uint64_t failed_restarts_ = 0;
};

}  // namespace window
}  // namespace mocktail

#endif  // MOCKTAIL_WINDOW_GUEST_LUA_APP_SURFACE_RECOVERY_H_
