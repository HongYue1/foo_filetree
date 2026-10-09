#pragma once

// The status bar's text (M8b): what the selection, or the focused folder, holds. Pure, so it is
// tested offline; the view fills a Summary from the tree and formats it here.

#include <cstdint>
#include <string>

namespace filetree::model {

class Tree;

struct Summary {
    enum class Kind : std::uint8_t {
        none,       //!< nothing focused
        items,      //!< several selected items: files + folders, bytes of the files
        folder,     //!< a listed folder: its direct children
        listing,    //!< a folder whose listing is on its way
        failed,     //!< a folder that could not be listed
        not_listed, //!< a closed folder never listed
        file,       //!< one file: bytes
    };
    Kind kind{Kind::none};
    std::uint64_t files{};
    std::uint64_t folders{};
    std::uint64_t bytes{};

    friend bool operator==(const Summary&, const Summary&) = default;
};

//! Several selected visible rows sum them (Kind::items); otherwise the focus node describes
//! itself. `focus` may be no_node. No allocation: walks the rows or the node's children.
[[nodiscard]] Summary summarize(const Tree& tree, std::uint32_t focus) noexcept;

//! Explorer-like sizes: "0 bytes", "1 byte", "912 bytes", "1.00 KB", "12.4 MB", "980 GB".
[[nodiscard]] std::wstring format_size(std::uint64_t bytes);

//! "3 items selected · 2 files, 1 folder · 12.4 MB", "12 folders, 340 files · 2.10 GB",
//! "Empty folder", "Listing…", ...
[[nodiscard]] std::wstring format_summary(const Summary& summary);

} // namespace filetree::model
