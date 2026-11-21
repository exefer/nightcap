#include "window/guest_lua_app_surface_recovery.h"

#include <chrono>
#include <utility>

namespace mocktail {
namespace window {
namespace {

runtime::GameSessionUpdateResult Applied(const char* message) {
  return {runtime::GameSessionUpdateStatus::kApplied,
          runtime::GameSessionState::kCreated, Status::Ok(), message};
}

runtime::GameSessionUpdateResult Duplicate(const char* message) {
  return {runtime::GameSessionUpdateStatus::kDuplicate,
          runtime::GameSessionState::kCreated, Status::Ok(), message};
}

runtime::GameSessionUpdateResult DispatchCreated(void* context,
                                                 uint64_t generation) {
  return static_cast<GuestLuaAppSurfaceRecovery*>(context)->Created(generation);
}

runtime::GameSessionUpdateResult DispatchChanged(void* context,
                                                 runtime::GameSurface surface) {
  return static_cast<GuestLuaAppSurfaceRecovery*>(context)->Changed(surface);
}

runtime::GameSessionUpdateResult DispatchDestroyed(void* context,
                                                   uint64_t generation) {
  return static_cast<GuestLuaAppSurfaceRecovery*>(context)->Destroyed(
      generation);
}

}  // namespace

uint64_t GuestLuaAppSurfaceRecovery::SteadyClockMs() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
}

GuestLuaAppSurfaceRecovery::GuestLuaAppSurfaceRecovery(
    void* restart_context, GuestLuaAppRestartFn restart,
    const runtime::GameSurface& engine_surface, uint64_t extent_changed_ms,
    Clock now_ms)
    : restart_context_(restart_context),
      restart_(restart),
      now_ms_(now_ms != nullptr ? now_ms : &SteadyClockMs),
      engine_generation_(engine_surface.generation),
      extent_width_(engine_surface.width),
      extent_height_(engine_surface.height),
      extent_changed_ms_(extent_changed_ms) {}

uint64_t GuestLuaAppSurfaceRecovery::NowMs() const { return now_ms_(); }

runtime::GameSessionUpdateResult GuestLuaAppSurfaceRecovery::Created(
    uint64_t generation) {
  if (generation == engine_generation_) {
    return Duplicate("guest LuaApp surface generation already current");
  }
  pending_ = true;
  return Applied("guest LuaApp surface recreation pending");
}

runtime::GameSessionUpdateResult GuestLuaAppSurfaceRecovery::Destroyed(
    uint64_t generation) {
  if (generation != engine_generation_) {
    return Duplicate("guest LuaApp ignored a stale surface destruction");
  }
  pending_ = true;
  return Applied("guest LuaApp surface destruction retained");
}

runtime::GameSessionUpdateResult GuestLuaAppSurfaceRecovery::Changed(
    runtime::GameSurface surface) {
  const uint64_t now = NowMs();
  const bool extent_changed =
      surface.width != extent_width_ || surface.height != extent_height_;
  if (extent_changed) {
    extent_width_ = surface.width;
    extent_height_ = surface.height;
    extent_changed_ms_ = now;
  }
  // A new generation is a recreated surface even without a Created event.
  if (surface.generation != engine_generation_) {
    pending_ = true;
  }
  if (!pending_) {
    return Duplicate("guest LuaApp surface resize follows the extent query");
  }
  const uint64_t stable_ms = now > extent_changed_ms_ ? now - extent_changed_ms_
                                                       : 0;
  if (extent_changed || stable_ms < kStableExtentMs) {
    engine_generation_ = surface.generation;
    pending_ = false;
    return Duplicate("guest LuaApp adopted a surface recreated in a resize");
  }
  if (restart_ == nullptr) {
    ++failed_restarts_;
    return Duplicate("guest LuaApp surface restart is unavailable");
  }
  // Not fatal: stay pending and retry on the next surface event.
  const Status status = restart_(restart_context_, surface);
  if (!status.ok()) {
    ++failed_restarts_;
    return Duplicate("guest LuaApp surface restart failed");
  }
  engine_generation_ = surface.generation;
  pending_ = false;
  ++restarts_;
  return Applied("guest LuaApp restarted on recreated surface");
}

WindowGameSurfaceConsumer GuestLuaAppSurfaceRecovery::Consumer() {
  return {this, &DispatchCreated, &DispatchChanged, &DispatchDestroyed};
}

}  // namespace window
}  // namespace mocktail
