// Status bar text. See status_text.h.

#include "status_text.h"

#include <cwchar>

#include "tree.h"

namespace filetree::model {
namespace {

void add_node(const Node& node, Summary& out) noexcept {
    if (node.has(node_container)) {
        ++out.folders;
    } else {
        ++out.files;
        out.bytes += node.size;
    }
}

void append_count(std::wstring& out, std::uint64_t count, const wchar_t* one, const wchar_t* many) {
    out += std::to_wstring(count);
    out += L' ';
    out += count == 1 ? one : many;
}

} // namespace

Summary summarize(const Tree& tree, std::uint32_t focus) noexcept {
    Summary out;
    if (tree.selection_hint() > 1) {
        for (const std::uint32_t index : tree.rows()) {
            const Node& node = tree.node(index);
            if (node.has(node_selected)) add_node(node, out);
        }
        if (out.files + out.folders > 1) {
            out.kind = Summary::Kind::items;
            return out;
        }
        out = Summary{};
    }
    if (focus == no_node || focus >= tree.node_count()) return out;
    const Node& node = tree.node(focus);
    if (!node.has(node_container)) {
        out.kind = Summary::Kind::file;
        out.files = 1;
        out.bytes = node.size;
        return out;
    }
    if (node.has(node_loading)) {
        out.kind = Summary::Kind::listing;
    } else if (node.has(node_load_failed)) {
        out.kind = Summary::Kind::failed;
    } else if (!node.has(node_loaded)) {
        out.kind = Summary::Kind::not_listed;
    } else {
        out.kind = Summary::Kind::folder;
        if (node.first_child != no_node) {
            for (std::uint32_t i = 0; i < node.child_count; ++i) {
                add_node(tree.node(node.first_child + i), out);
            }
        }
    }
    return out;
}

std::wstring format_size(std::uint64_t bytes) {
    if (bytes < 1024) {
        std::wstring out = std::to_wstring(bytes);
        out += bytes == 1 ? L" byte" : L" bytes";
        return out;
    }
    static constexpr const wchar_t* units[] = {L"KB", L"MB", L"GB", L"TB", L"PB", L"EB"};
    double value = static_cast<double>(bytes) / 1024.0;
    std::size_t unit = 0;
    while (value >= 1000.0 && unit + 1 < std::size(units)) {
        value /= 1024.0;
        ++unit;
    }
    // Three significant digits, truncated like Explorer (1023 bytes never reads "1.00 KB").
    wchar_t text[32];
    const auto trunc = [](double v, double scale) {
        return static_cast<double>(static_cast<std::uint64_t>(v * scale)) / scale;
    };
    if (value < 10.0) {
        std::swprintf(text, std::size(text), L"%.2f %ls", trunc(value, 100.0), units[unit]);
    } else if (value < 100.0) {
        std::swprintf(text, std::size(text), L"%.1f %ls", trunc(value, 10.0), units[unit]);
    } else {
        std::swprintf(text, std::size(text), L"%.0f %ls", trunc(value, 1.0), units[unit]);
    }
    return text;
}

std::wstring format_summary(const Summary& summary) {
    std::wstring out;
    switch (summary.kind) {
    case Summary::Kind::none: break;
    case Summary::Kind::items:
        append_count(out, summary.files + summary.folders, L"item selected", L"items selected");
        if (summary.files > 0 && summary.folders > 0) {
            out += L" \x00b7 ";
            append_count(out, summary.files, L"file", L"files");
            out += L", ";
            append_count(out, summary.folders, L"folder", L"folders");
        }
        if (summary.files > 0) {
            out += L" \x00b7 ";
            out += format_size(summary.bytes);
        }
        break;
    case Summary::Kind::folder:
        if (summary.files + summary.folders == 0) {
            out = L"Empty folder";
            break;
        }
        if (summary.folders > 0) append_count(out, summary.folders, L"folder", L"folders");
        if (summary.files > 0) {
            if (!out.empty()) out += L", ";
            append_count(out, summary.files, L"file", L"files");
            out += L" \x00b7 ";
            out += format_size(summary.bytes);
        }
        break;
    case Summary::Kind::listing: out = L"Listing\x2026"; break;
    case Summary::Kind::failed: out = L"This folder could not be listed"; break;
    case Summary::Kind::not_listed: out = L"Open the folder to count its contents"; break;
    case Summary::Kind::file: out = format_size(summary.bytes); break;
    }
    return out;
}

} // namespace filetree::model
