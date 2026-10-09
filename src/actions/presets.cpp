#include "presets.h"

namespace filetree::actions {
namespace {

constexpr Action send(Target target, Mode mode, bool play) {
    return {Kind::send, target, mode, play, Recursion::by_default};
}

#define FILETREE_SEND_PRESETS                                                                 \
    {L"Play in temp playlist", send(Target::temp, Mode::replace, true)},                      \
        {L"Send to temp playlist", send(Target::temp, Mode::replace, false)},                 \
        {L"Add to temp playlist", send(Target::temp, Mode::add, false)},                      \
        {L"Add to temp and play", send(Target::temp, Mode::add, true)},                       \
        {L"Play in active playlist", send(Target::active, Mode::replace, true)},              \
        {L"Send to active playlist", send(Target::active, Mode::replace, false)},             \
        {L"Add to active playlist", send(Target::active, Mode::add, false)},                  \
        {L"Add to active and play", send(Target::active, Mode::add, true)},                   \
        {L"Play in new playlist", send(Target::new_playlist, Mode::replace, true)},           \
        {L"Send to new playlist", send(Target::new_playlist, Mode::replace, false)},          \
        {L"Add to playback queue", send(Target::queue, Mode::add, false)}

constexpr Preset folder_presets[] = {
    {L"None", Action{}},
    {L"Expand / collapse", Action{Kind::toggle}},
    FILETREE_SEND_PRESETS,
};

constexpr Preset file_presets[] = {
    {L"None", Action{}},
    FILETREE_SEND_PRESETS,
};

#undef FILETREE_SEND_PRESETS

bool same_ignoring_recursion(const Action& a, const Action& b) noexcept {
    if (a.kind != b.kind) return false;
    if (a.kind != Kind::send) return true;
    if (a.target == Target::queue || b.target == Target::queue) return a.target == b.target;
    // Adding to a new playlist is the same as sending to it.
    const Mode mode_a = a.target == Target::new_playlist ? Mode::replace : a.mode;
    const Mode mode_b = b.target == Target::new_playlist ? Mode::replace : b.mode;
    return a.target == b.target && mode_a == mode_b && a.play == b.play;
}

} // namespace

std::span<const Preset> presets(bool folder) noexcept {
    if (folder) return folder_presets;
    return file_presets;
}

std::size_t preset_index(const Action& action, bool folder) noexcept {
    const auto list = presets(folder);
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (same_ignoring_recursion(list[i].action, action)) return i;
    }
    return 0;
}

Action from_preset(std::size_t index, bool folder, const Action& previous) noexcept {
    const auto list = presets(folder);
    Action out = index < list.size() ? list[index].action : Action{};
    if (out.kind == Kind::send) out.recursion = previous.recursion;
    return out;
}

} // namespace filetree::actions
