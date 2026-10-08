#pragma once

// The fb2k-facing side of fs/: the shared enumeration service and the playable-extension set.
// Everything here is main-thread only. Kept apart from enumerate/enumeration_service so those
// build without the SDK in the offline tests.

#include <functional>
#include <memory>

#include "../model/extension_set.h"
#include "enumeration_service.h"

namespace filetree::fs {

//! The component-wide service, created on first use (never in on_init). Shut down in on_quit.
EnumerationService& enumeration();

//! A single worker for shell operations (open, recycle, rename). Separate from the enumeration
//! pool so a slow delete waiting on a confirmation dialog never stalls folder listings. Each task
//! must initialise COM itself (STA). Created on first use, shut down in on_quit.
platform::WorkerPool& shell_worker();

//! Runs `work` on the main thread later; dropped once foobar2000 is quitting.
void post_to_main(std::function<void()> work);

//! Extensions fb2k can play or load as a playlist, built on first use from the registered input
//! and playlist types (no disk access). Cached; shared read-only with workers.
std::shared_ptr<const model::ExtensionSet> playable_extensions();

//! Drops the cached set so the next call rebuilds it (Preferences change, M5).
void invalidate_playable_extensions() noexcept;

} // namespace filetree::fs
