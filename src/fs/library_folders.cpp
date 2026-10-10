#include <helpers/foobar2000+atl.h>

#include "library_folders.h"

#include "../platform/worker_pool.h"
#include "fb2k_glue.h"

namespace filetree::fs {
namespace {

// Library changes come in bursts (a rescan adds thousands of tracks in batches).
constexpr double debounce_seconds = 2.0;

// The library folders found last time, one per line: shown at once on the next start while
// the library loads and the index is built.
constexpr GUID guid_last_roots = {
    0x679d9424, 0x897e, 0x4d96, {0xb2, 0x4c, 0xb0, 0xb4, 0xe4, 0xf3, 0xb2, 0x0e}};
cfg_var_modern::cfg_string cfg_last_roots(guid_last_roots, "");

std::shared_ptr<const model::LibraryIndex> g_index;
void (*g_listener)() = nullptr;
std::unique_ptr<platform::WorkerPool> g_worker;
bool g_started = false;
bool g_scheduled = false; //!< a rebuild is waiting for its debounce
bool g_building = false;  //!< a rebuild runs on the worker
bool g_again = false;     //!< the library changed while building
bool g_quit = false;

void schedule(double delay);

void remember(const model::LibraryIndex& index) {
    pfc::string8 text;
    for (const std::wstring& root : index.roots()) {
        if (!text.is_empty()) text += "\n";
        text += pfc::stringcvt::string_utf8_from_wide(root.c_str()).get_ptr();
    }
    if (strcmp(text.c_str(), cfg_last_roots.get().c_str()) != 0) cfg_last_roots.set(text);
}

//! The roots from last time, or null when there were none.
std::shared_ptr<const model::LibraryIndex> remembered() {
    const pfc::string8 text = cfg_last_roots.get();
    if (text.is_empty()) return nullptr;
    auto index = std::make_shared<model::LibraryIndex>();
    const char* line = text.c_str();
    while (*line != '\0') {
        const char* end = strchr(line, '\n');
        const std::size_t length =
            end != nullptr ? static_cast<std::size_t>(end - line) : strlen(line);
        const pfc::stringcvt::string_wide_from_utf8 wide(line, length);
        index->add_root(wide.get_ptr());
        line += length;
        if (*line == '\n') ++line;
    }
    index->finish();
    return index;
}

//! Main thread: takes the item list, derives on the worker (get_relative_path is legal from any
//! thread since foobar2000 v2.0), posts the result back.
void rebuild() {
    if (g_quit) return;
    if (g_building) {
        g_again = true;
        return;
    }
    auto items = std::make_shared<metadb_handle_list>();
    library_manager::ptr manager;
    try {
        manager = library_manager::get();
        if (!manager->is_library_enabled()) {
            g_index = std::make_shared<model::LibraryIndex>();
            remember(*g_index);
            if (g_listener != nullptr) g_listener();
            return;
        }
        manager->get_all_items(*items);
    } catch (...) {
        return;
    }
    g_building = true;
    if (!g_worker) g_worker = std::make_unique<platform::WorkerPool>(1);
    g_worker->submit([manager, items] {
        auto index = std::make_shared<model::LibraryIndex>();
        try {
            pfc::string8 relative;
            for (const metadb_handle_ptr& item : *items) {
                const char* path = item->get_path();
                if (strncmp(path, "file://", 7) != 0) continue; // archives, streams
                if (!manager->get_relative_path(item, relative)) continue;
                const pfc::stringcvt::string_wide_from_utf8 wide_path(path + 7);
                const pfc::stringcvt::string_wide_from_utf8 wide_relative(relative.c_str());
                index->add(wide_path.get_ptr(), wide_relative.get_ptr());
            }
            index->finish();
        } catch (...) {
        }
        post_to_main([index] {
            g_building = false;
            g_index = index;
            remember(*index);
            if (g_listener != nullptr) g_listener();
            if (g_again) {
                g_again = false;
                schedule(debounce_seconds);
            }
        });
    });
}

void schedule(double delay) {
    if (g_scheduled) return;
    g_scheduled = true;
    try {
        fb2k::callLater(delay, [] {
            g_scheduled = false;
            rebuild();
        });
    } catch (...) {
        g_scheduled = false;
    }
}

//! Folder changes only: tag edits (on_items_modified) do not move files.
class LibraryWatch : public library_callback_v2_dynamic_impl_base {
public:
    void on_items_added(metadb_handle_list_cref) override { schedule(debounce_seconds); }
    void on_items_removed(metadb_handle_list_cref) override { schedule(debounce_seconds); }
    void on_items_modified_v2(metadb_handle_list_cref, metadb_io_callback_v2_data&) override {}
    void on_library_initialized() override { schedule(0); }
};

std::unique_ptr<LibraryWatch> g_watch;

class lifecycle : public initquit {
public:
    void on_init() override {}
    void on_quit() override {
        g_quit = true;
        g_watch.reset();
        if (g_worker) g_worker->shutdown(500);
        g_listener = nullptr;
    }
};

FB2K_SERVICE_FACTORY(lifecycle);

} // namespace

std::shared_ptr<const model::LibraryIndex> library_index() {
    if (!g_started) {
        g_started = true;
        try {
            g_index = remembered();
            g_watch = std::make_unique<LibraryWatch>();
            // Before the library has loaded, on_library_initialized starts the first build.
            if (library_manager_v4::get()->is_initialized()) schedule(0);
        } catch (...) {
        }
    }
    return g_index;
}

void set_library_listener(void (*listener)()) { g_listener = listener; }

} // namespace filetree::fs
