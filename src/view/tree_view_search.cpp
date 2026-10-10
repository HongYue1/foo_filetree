// TreeView: searching every root by name (Ctrl+Shift+F, settings disk_search). While a search
// is shown the tree holds only what was found and the folders above it, all open; found folders
// list their contents when opened, as usual. end_search() brings the normal tree back as it was.
//
// The worker (fs/disk_search.h) sends batches; the tree is rebuilt from all hits so far at most
// every search_rebuild_ms, keeping what the user closed, opened, selected and scrolled to.

#include <helpers/foobar2000+atl.h>

#include "tree_view.h"

#include <algorithm>
#include <unordered_map>

#include "../fs/fb2k_glue.h"

namespace filetree::view {
namespace {

constexpr std::size_t hit_limit = 10000;

//! `path` is `root` or below it (case-insensitive).
bool inside(std::wstring_view path, std::wstring_view root) noexcept {
    if (root.empty() || path.size() < root.size()) return false;
    if (CompareStringOrdinal(path.data(), static_cast<int>(root.size()), root.data(),
                             static_cast<int>(root.size()), TRUE) != CSTR_EQUAL) {
        return false;
    }
    return path.size() == root.size() || root.back() == L'\\' || path[root.size()] == L'\\';
}

std::wstring upper(std::wstring text) {
    if (!text.empty()) CharUpperBuffW(text.data(), static_cast<DWORD>(text.size()));
    return text;
}

} // namespace

bool TreeView::start_search(std::wstring_view text) noexcept {
    try {
        bool glob = false;
        if (fs::search_pattern(text, glob).empty()) {
            end_search();
            return false;
        }
        stop_search_worker();
        if (!search_mode_) {
            capture_state(search_saved_);
            search_roots_.clear();
            std::wstring path;
            for (std::uint32_t node = 0;
                 node < tree_.node_count() && tree_.node(node).has(model::node_root); ++node) {
                const model::Node& n = tree_.node(node);
                if (n.has(model::node_virtual)) continue;
                tree_.build_path(node, path);
                search_roots_.push_back(
                    {path, static_cast<std::uint16_t>(n.flags & (model::node_favourite |
                                                                 model::node_library))});
            }
            // A root inside another one (a favourite on a shown drive) is searched once.
            std::vector<SearchRoot> outer;
            for (const SearchRoot& root : search_roots_) {
                const bool nested = std::any_of(
                    search_roots_.begin(), search_roots_.end(), [&](const SearchRoot& other) {
                        return &other != &root && other.path.size() < root.path.size() &&
                               inside(root.path, other.path);
                    });
                if (!nested) outer.push_back(root);
            }
            search_roots_ = std::move(outer);
            search_mode_ = true;
            unwatch_all();
        }
        search_text_ = std::wstring(text);
        search_hits_.clear();
        search_folders_ = 0;
        search_limited_ = false;
        search_running_ = true;
        rebuild_search_tree(); // empty: "Searching..."

        auto cancel = std::make_shared<std::atomic<bool>>(false);
        search_cancel_ = cancel;
        std::vector<std::wstring> roots;
        for (const SearchRoot& root : search_roots_) roots.push_back(root.path);
        fs::EnumOptions options = options_;
        if (options.files == fs::FileMode::playable && options.playable == nullptr) {
            options.playable = fs::playable_extensions();
        }
        std::weak_ptr<TreeView*> weak = alive_;
        fs::search_worker().submit([roots = std::move(roots), query = search_text_,
                                    options = std::move(options), cancel, weak] {
            fs::search_folders(roots, query, options, hit_limit, *cancel,
                               [&](fs::SearchBatch& batch) {
                                   auto copy = std::make_shared<fs::SearchBatch>();
                                   copy->hits = std::move(batch.hits);
                                   copy->folders = batch.folders;
                                   copy->done = batch.done;
                                   copy->limited = batch.limited;
                                   fs::post_to_main([copy, cancel, weak] {
                                       if (cancel->load(std::memory_order_relaxed)) return;
                                       if (const auto alive = weak.lock()) {
                                           (*alive)->on_search_batch(*copy);
                                       }
                                   });
                               });
        });
        notify_search();
        return true;
    } catch (...) {
        search_running_ = false;
        notify_search();
        return false;
    }
}

void TreeView::stop_search_worker() noexcept {
    if (search_cancel_) search_cancel_->store(true, std::memory_order_relaxed);
    search_cancel_.reset();
    search_running_ = false;
}

void TreeView::cancel_search() noexcept {
    if (!search_running_) return;
    stop_search_worker();
    if (search_dirty_) rebuild_search_tree(); // show what has arrived
    notify_search();
}

void TreeView::end_search() noexcept {
    if (!search_mode_) return;
    stop_search_worker();
    search_mode_ = false;
    search_dirty_ = false;
    if (wnd_ != nullptr) KillTimer(wnd_, timer_search);
    try {
        search_hits_.clear();
        search_hits_.shrink_to_fit();
        search_roots_.clear();
        search_text_.clear();
        end_rename(false);
        populate_roots();
        update_scrollbar();
        if (wnd_ != nullptr) InvalidateRect(wnd_, nullptr, FALSE);
        restore_state(search_saved_);
    } catch (...) {
    }
    schedule_watch_sync();
    notify_search();
}

void TreeView::notify_search() noexcept {
    try {
        if (search_listener_) search_listener_();
    } catch (...) {
    }
}

std::wstring TreeView::search_status() const {
    const std::wstring count = std::to_wstring(search_hits_.size());
    if (search_running_) {
        return search_hits_.empty() ? L"Searching\u2026" : L"Searching\u2026 " + count + L" found";
    }
    if (search_hits_.empty()) return L"Nothing found";
    return count + (search_limited_ ? L"+ found" : L" found");
}

void TreeView::on_search_batch(fs::SearchBatch& batch) noexcept {
    if (!search_mode_) return;
    try {
        search_hits_.insert(search_hits_.end(), std::make_move_iterator(batch.hits.begin()),
                            std::make_move_iterator(batch.hits.end()));
        search_folders_ = batch.folders;
        search_dirty_ |= !batch.hits.empty();
    } catch (...) {
    }
    if (batch.done) {
        search_running_ = false;
        search_limited_ = batch.limited;
        search_cancel_.reset();
        if (wnd_ != nullptr) KillTimer(wnd_, timer_search);
        rebuild_search_tree(); // also when nothing came: "Nothing found"
    } else if (search_dirty_ && wnd_ != nullptr) {
        // The first hits at once; then at most every search_rebuild_ms.
        if (tree_.node_count() == 0) {
            rebuild_search_tree();
        } else {
            SetTimer(wnd_, timer_search, search_rebuild_ms, nullptr); // resets: fine, batches are slower
        }
    }
    notify_search();
}

void TreeView::on_search_timer() noexcept {
    if (wnd_ != nullptr) KillTimer(wnd_, timer_search);
    if (search_mode_ && search_dirty_) rebuild_search_tree();
}

void TreeView::rebuild_search_tree() noexcept {
    search_dirty_ = false;
    try {
        end_rename(false);
        // What the user did with the results so far: closed folders stay closed, opened ones
        // (a found folder opened to look inside) open again, the selection and scroll stay.
        std::unordered_set<std::wstring> closed;
        restore_expand_.clear();
        std::wstring path;
        for (const std::uint32_t node : tree_.rows()) {
            const model::Node& n = tree_.node(node);
            if (!n.has(model::node_container) || !n.has(model::node_loaded)) continue;
            (n.has(model::node_expanded) ? restore_expand_ : closed).insert(upper_path(node));
        }
        std::unordered_set<std::wstring> selected;
        if (tree_.selection_hint() > 1) {
            for (const std::uint32_t node : tree_.rows()) {
                if (tree_.is_selected(node)) selected.insert(upper_path(node));
            }
        }
        restore_select_.clear();
        if (selected_row_ >= 0 && static_cast<std::size_t>(selected_row_) < tree_.row_count()) {
            restore_select_ = upper_path(tree_.node_at_row(static_cast<std::size_t>(selected_row_)));
        }
        restore_top_.clear();
        if (top_row_ > 0 && top_row_ < tree_.row_count()) {
            restore_top_ = upper_path(tree_.node_at_row(top_row_));
        }

        // A fresh, empty tree (as populate_roots, without the roots).
        for (const PendingListing& pending : pending_) pending.ticket.cancel();
        pending_.clear();
        ++generation_;
        set_cut({});
        tree_.clear();
        pending_select_ = {};
        boundary_node_ = model::no_node;
        boundary_row_cache_ = 0;
        selected_row_ = hover_row_ = -1;
        top_row_ = 0;
        filter_hidden_selection_ = model::no_node;
        restore_top_node_ = model::no_node;

        // The hits as a tree below their roots. Names point into search_roots_ / search_hits_.
        struct Entry {
            std::wstring_view name;
            std::uint32_t attributes{FILE_ATTRIBUTE_DIRECTORY};
            std::uint64_t size{};
            std::int64_t modified{};
            std::vector<std::uint32_t> children;
        };
        std::vector<Entry> entries(search_roots_.size());
        std::unordered_map<std::wstring, std::uint32_t> index; // "parent|NAME" -> entry
        std::wstring key;
        for (const fs::SearchHit& hit : search_hits_) {
            std::size_t root = 0;
            while (root < search_roots_.size() && !inside(hit.path, search_roots_[root].path)) ++root;
            if (root == search_roots_.size()) continue;
            std::uint32_t parent = static_cast<std::uint32_t>(root);
            std::size_t at = search_roots_[root].path.size();
            while (at < hit.path.size()) {
                if (hit.path[at] == L'\\') ++at;
                const std::size_t end = std::min(hit.path.find(L'\\', at), hit.path.size());
                if (end == at) break;
                const std::wstring_view name(hit.path.data() + at, end - at);
                key = std::to_wstring(parent);
                key.push_back(L'|');
                key.append(name);
                key = upper(std::move(key));
                auto found = index.find(key);
                if (found == index.end()) {
                    entries.push_back({name});
                    const auto added = static_cast<std::uint32_t>(entries.size() - 1);
                    entries[parent].children.push_back(added);
                    found = index.emplace(std::move(key), added).first;
                }
                if (end == hit.path.size()) {
                    Entry& entry = entries[found->second];
                    entry.attributes = hit.attributes;
                    entry.size = hit.size;
                    entry.modified = hit.modified;
                }
                parent = found->second;
                at = end;
            }
        }

        std::vector<model::ChildRecord> records;
        const auto build = [&](auto& self, std::uint32_t node, std::uint32_t entry) -> void {
            std::vector<std::uint32_t> kids = entries[entry].children;
            const auto item = [&](std::uint32_t e) {
                return model::SortItem{entries[e].name, entries[e].attributes, entries[e].size,
                                       entries[e].modified};
            };
            std::sort(kids.begin(), kids.end(), [&](std::uint32_t a, std::uint32_t b) {
                return model::sort_less(item(a), item(b), options_.sort);
            });
            records.clear();
            for (const std::uint32_t e : kids) {
                records.push_back({entries[e].name, entries[e].attributes, entries[e].size,
                                   entries[e].modified});
            }
            if (tree_.expand(node) != model::Tree::ExpandResult::needs_load) return;
            tree_.apply_children(node, records);
            const std::uint32_t first = tree_.node(node).first_child;
            for (std::size_t i = 0; i < kids.size(); ++i) {
                if (!entries[kids[i]].children.empty()) {
                    self(self, first + static_cast<std::uint32_t>(i), kids[i]);
                }
            }
            if (closed.contains(upper_path(node))) tree_.collapse(node);
        };
        for (std::size_t r = 0; r < search_roots_.size(); ++r) {
            if (entries[r].children.empty()) continue;
            const std::uint32_t node = tree_.add_root(search_roots_[r].path, FILE_ATTRIBUTE_DIRECTORY,
                                                      search_roots_[r].flags);
            build(build, node, static_cast<std::uint32_t>(r));
        }
        if (tree_.filtered()) tree_.rebuild_rows();

        for (std::uint32_t node = 0;
             node < tree_.node_count() && tree_.node(node).has(model::node_root); ++node) {
            try_restore(node);
        }
        if (!selected.empty()) {
            for (const std::uint32_t node : tree_.rows()) {
                if (selected.contains(upper_path(node))) tree_.set_selected(node, true);
            }
        }
        update_scrollbar();
        apply_restore_top();
        resolve_playing();
        notify_selection();
        acc_event(EVENT_OBJECT_REORDER, -1);
        if (wnd_ != nullptr) InvalidateRect(wnd_, nullptr, FALSE);
    } catch (...) {
        restore_expand_.clear();
        restore_select_.clear();
        restore_top_.clear();
    }
}

} // namespace filetree::view
