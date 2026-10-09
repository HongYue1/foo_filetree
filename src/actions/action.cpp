#include "action.h"

#include <algorithm>

namespace filetree::actions {
namespace {

constexpr std::uint8_t magic0 = 'F';
constexpr std::uint8_t magic1 = 'T';
constexpr std::uint8_t version = 1;

constexpr Action send(Target target, Mode mode, bool play) {
    return {Kind::send, target, mode, play, Recursion::by_default};
}

void put(std::uint8_t*& out, const Action& action) noexcept {
    *out++ = static_cast<std::uint8_t>(action.kind);
    *out++ = static_cast<std::uint8_t>(action.target);
    *out++ = static_cast<std::uint8_t>(action.mode);
    *out++ = action.play ? 1 : 0;
    *out++ = static_cast<std::uint8_t>(action.recursion);
}

Action get(const std::uint8_t* in, const Action& fallback) noexcept {
    if (in[0] > static_cast<std::uint8_t>(Kind::send) ||
        in[1] > static_cast<std::uint8_t>(Target::queue) ||
        in[2] > static_cast<std::uint8_t>(Mode::add) || in[3] > 1 ||
        in[4] > static_cast<std::uint8_t>(Recursion::never)) {
        return fallback;
    }
    return {static_cast<Kind>(in[0]), static_cast<Target>(in[1]), static_cast<Mode>(in[2]),
            in[3] == 1, static_cast<Recursion>(in[4])};
}

} // namespace

Bindings Bindings::defaults() noexcept {
    Bindings out;
    const Action toggle{Kind::toggle};
    const Action play_temp = send(Target::temp, Mode::replace, true);
    const Action add_active = send(Target::active, Mode::add, false);
    out.gestures[static_cast<std::size_t>(Gesture::single_click)] = {};
    out.gestures[static_cast<std::size_t>(Gesture::double_click)] = {toggle, play_temp};
    out.gestures[static_cast<std::size_t>(Gesture::middle_click)] = {add_active, add_active};
    out.gestures[static_cast<std::size_t>(Gesture::enter)] = {toggle, play_temp};
    return out;
}

std::size_t encode(const Bindings& bindings, std::uint8_t (&out)[encoded_bindings_size]) noexcept {
    std::uint8_t* cursor = out;
    *cursor++ = magic0;
    *cursor++ = magic1;
    *cursor++ = version;
    *cursor++ = static_cast<std::uint8_t>(gesture_count);
    for (const Binding& binding : bindings.gestures) {
        put(cursor, binding.folder);
        put(cursor, binding.file);
    }
    return static_cast<std::size_t>(cursor - out);
}

Bindings decode(const void* data, std::size_t size) noexcept {
    Bindings out = Bindings::defaults();
    const auto* in = static_cast<const std::uint8_t*>(data);
    if (in == nullptr || size < 4 || in[0] != magic0 || in[1] != magic1 || in[2] < 1) return out;

    const std::size_t stored = in[3];
    const std::size_t available = (size - 4) / 10;
    const std::size_t count = std::min({stored, available, gesture_count});
    in += 4;
    for (std::size_t i = 0; i < count; ++i, in += 10) {
        Binding& binding = out.gestures[i];
        binding.folder = get(in, binding.folder);
        binding.file = get(in + 5, binding.file);
    }
    return out;
}

} // namespace filetree::actions
