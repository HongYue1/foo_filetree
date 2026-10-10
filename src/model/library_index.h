#pragma once

// What Folder Tree knows about the Media Library. The SDK has no call that lists the library
// folders, so they are derived from the tracks: each track's path minus its path relative to
// the library folder that holds it (library_manager::get_relative_path). Also kept: every folder
// that holds a library track somewhere below it, as proof that a folder is not empty.
// Built on a worker, then shared read-only. Pure std + Win32, tested offline.

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace filetree::model {

class LibraryIndex {
public:
    //! `path` is a track's file path ("C:\Music\A\x.flac"), `relative` its path inside the
    //! library folder ("A\x.flac"). False when they do not fit together (then nothing is kept).
    bool add(std::wstring_view path, std::wstring_view relative);
    //! A root remembered from the last session (no tracks known under it). Duplicates are
    //! ignored.
    void add_root(std::wstring_view root);
    //! Sorts the roots (case-insensitive). Call once, after the last add().
    void finish();

    //! The library folders, as written in the tracks' paths ("C:\Music"; a drive as "D:\").
    [[nodiscard]] const std::vector<std::wstring>& roots() const noexcept { return roots_; }
    //! `upper_path`: a folder path, upper-cased with to_upper(), without a trailing backslash.
    [[nodiscard]] bool holds_tracks(std::wstring_view upper_path) const noexcept;
    [[nodiscard]] std::size_t folder_count() const noexcept { return folders_.size(); }

    [[nodiscard]] bool same_roots(const LibraryIndex& other) const noexcept {
        return root_keys_ == other.root_keys_;
    }

private:
    struct Hash {
        using is_transparent = void;
        std::size_t operator()(std::wstring_view text) const noexcept {
            return std::hash<std::wstring_view>{}(text);
        }
    };

    std::vector<std::wstring> roots_;
    std::vector<std::wstring> root_keys_; //!< upper-cased, same order as roots_
    std::unordered_set<std::wstring, Hash, std::equal_to<>> folders_;
    std::wstring last_folder_; //!< tracks come folder by folder: skip the repeats cheaply
    std::wstring scratch_;
};

} // namespace filetree::model
