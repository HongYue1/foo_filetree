// Offline tests for the tree selection (model/tree_selection.cpp).

#include <windows.h>

#include <string>
#include <vector>

#include "check.h"
#include "../src/model/tree.h"

using namespace filetree;

namespace {

std::vector<model::ChildRecord> children(std::initializer_list<const wchar_t*> folders,
                                         std::initializer_list<const wchar_t*> files) {
    std::vector<model::ChildRecord> out;
    for (const wchar_t* name : folders) out.push_back({name, FILE_ATTRIBUTE_DIRECTORY, 0, 0});
    for (const wchar_t* name : files) out.push_back({name, FILE_ATTRIBUTE_ARCHIVE, 0, 0});
    return out;
}

std::vector<std::uint32_t> selection(const model::Tree& tree) {
    std::vector<std::uint32_t> out;
    tree.selected_nodes(out);
    return out;
}

} // namespace

void test_selection() {
    using model::Tree;
    Tree tree;
    const auto c = tree.add_root(L"C:\\");
    const auto d = tree.add_root(L"D:\\");
    tree.expand(c);
    tree.apply_children(c, children({L"Music", L"Users"}, {L"a.mp3", L"b.mp3"}));
    const auto music = tree.find_child(c, L"Music");
    tree.expand(music);
    tree.apply_children(music, children({L"Album"}, {L"x.flac"}));
    // C, Music, Album, x.flac, Users, a.mp3, b.mp3, D
    CHECK(tree.row_count() == 8);
    CHECK(tree.selection_hint() == 0 && tree.count_selected_rows() == 0);

    // Toggle.
    tree.set_selected(c, true);
    tree.set_selected(c, true); // no duplicate
    CHECK(tree.is_selected(c) && tree.selection_hint() == 1);
    tree.set_selected(c, false);
    CHECK(!tree.is_selected(c) && tree.count_selected_rows() == 0);

    // Range in either order, added to the selection; row order out.
    tree.set_selected(d, true);
    tree.select_rows(5, 3);
    auto sel = selection(tree);
    CHECK(sel.size() == 4 && sel[0] == tree.node_at_row(3) && sel[3] == d);
    CHECK(tree.count_selected_rows(2) == 2);
    CHECK(tree.clear_selection() == 4 && tree.count_selected_rows() == 0);
    tree.select_rows(6, 99); // clamped to the last row
    CHECK(tree.count_selected_rows() == 2);
    tree.clear_selection();

    // Merge: selection and anchor follow moved nodes; gone nodes drop out.
    const auto users = tree.find_child(c, L"Users");
    const auto a = tree.find_child(c, L"a.mp3");
    tree.set_selected(music, true);
    tree.set_selected(users, true);
    tree.set_selected(a, true);
    tree.set_anchor(users);
    auto result = tree.merge_children(c, children({L"Music", L"Users"}, {L"b.mp3"}));
    CHECK(tree.anchor() == result.map(users) && tree.anchor() != users);
    sel = selection(tree);
    CHECK(sel.size() == 2 && sel[0] == result.map(music) && sel[1] == result.map(users));
    tree.set_anchor(tree.find_child(c, L"b.mp3"));
    tree.merge_children(c, children({L"Music", L"Users"}, {}));
    CHECK(tree.anchor() == model::no_node);

    // Collapse deselects what it hides, not the folder itself.
    const auto music2 = tree.find_child(c, L"Music");
    const auto album = tree.find_child(music2, L"Album");
    tree.set_selected(album, true);
    tree.set_selected(music2, true);
    tree.collapse(music2);
    CHECK(!tree.is_selected(album) && tree.is_selected(music2));
    tree.expand(music2);
    CHECK(!tree.is_selected(album));

    // A filter hides selected rows but keeps them selected.
    tree.clear_selection();
    const auto flac = tree.find_child(music2, L"x.flac");
    tree.set_selected(flac, true);
    tree.set_selected(tree.find_child(c, L"Users"), true);
    tree.set_filter(L"flac");
    CHECK(tree.count_selected_rows() == 1 && selection(tree)[0] == flac);
    tree.set_filter(L"");
    CHECK(tree.count_selected_rows() == 2);

    // Collapse all keeps selected roots only; clear() drops everything.
    tree.set_selected(d, true);
    tree.collapse_all();
    CHECK(tree.count_selected_rows() == 1 && tree.is_selected(d) && !tree.is_selected(flac));
    tree.clear();
    CHECK(tree.selection_hint() == 0 && tree.anchor() == model::no_node);
}
