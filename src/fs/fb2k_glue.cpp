#include <helpers/foobar2000+atl.h>

#include "fb2k_glue.h"

#include <atomic>

namespace filetree::fs {
namespace {

// Two threads: enough to keep one slow (network) folder from stalling a local one, without
// thrashing a spinning disk with parallel seeks.
constexpr unsigned worker_threads = 2;

// After on_quit, a worker that finishes late must not queue anything on a main thread that is
// going away.
std::atomic<bool> g_quitting{false};

std::unique_ptr<EnumerationService> g_service;
std::shared_ptr<const model::ExtensionSet> g_playable;

void post_to_main(std::function<void()> work) {
    if (g_quitting.load(std::memory_order_acquire)) return;
    fb2k::inMainThread(std::move(work));
}

void add_utf8(model::ExtensionSet& set, const char* extension) {
    if (extension == nullptr || *extension == '\0') return;
    const pfc::stringcvt::string_wide_from_utf8 wide(extension);
    set.add(std::wstring_view(wide.get_ptr()));
}

} // namespace

EnumerationService& enumeration() {
    if (!g_service) g_service = std::make_unique<EnumerationService>(post_to_main, worker_threads);
    return *g_service;
}

std::shared_ptr<const model::ExtensionSet> playable_extensions() {
    if (g_playable) return g_playable;

    auto set = std::make_shared<model::ExtensionSet>();
    try {
        input_file_type::for_each_media_ext([&](const char* ext) { add_utf8(*set, ext); });
        for (auto loader : playlist_loader::enumerate()) add_utf8(*set, loader->get_extension());
    } catch (const std::exception& error) {
        FB2K_console_formatter() << "Folder Tree: could not read file types: " << error.what();
    }
    // Archives (zip, 7z, rar via archive services) are not included yet; see QUEUE/M3.
    g_playable = std::move(set);
    return g_playable;
}

void invalidate_playable_extensions() noexcept { g_playable.reset(); }

namespace {

class lifecycle : public initquit {
public:
    void on_init() override {}

    void on_quit() override {
        g_quitting.store(true, std::memory_order_release);
        if (g_service) g_service->shutdown(1500);
    }
};

FB2K_SERVICE_FACTORY(lifecycle);

} // namespace

} // namespace filetree::fs
