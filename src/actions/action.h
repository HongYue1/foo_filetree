#pragma once

// What a click or key does. Pure data + encoding (no fb2k), so it is unit-tested offline.

#include <array>
#include <cstddef>
#include <cstdint>

namespace filetree::actions {

enum class Kind : std::uint8_t { none, toggle, send };
enum class Target : std::uint8_t { temp, active, new_playlist };
enum class Mode : std::uint8_t { replace, add };
enum class Recursion : std::uint8_t { by_default, always, never };

struct Action {
    Kind kind{Kind::none};
    Target target{Target::temp};
    Mode mode{Mode::replace};
    bool play{false};
    Recursion recursion{Recursion::by_default};

    friend bool operator==(const Action&, const Action&) = default;
};

enum class Gesture : std::uint8_t { single_click, double_click, middle_click, enter };
inline constexpr std::size_t gesture_count = 4;

struct Binding {
    Action folder;
    Action file;

    friend bool operator==(const Binding&, const Binding&) = default;
};

struct Bindings {
    std::array<Binding, gesture_count> gestures{};

    [[nodiscard]] static Bindings defaults() noexcept;

    [[nodiscard]] const Action& lookup(Gesture gesture, bool folder) const noexcept {
        const Binding& binding = gestures[static_cast<std::size_t>(gesture)];
        return folder ? binding.folder : binding.file;
    }

    friend bool operator==(const Bindings&, const Bindings&) = default;
};

//! Encoded size: 4-byte header + 5 bytes per action.
inline constexpr std::size_t encoded_bindings_size = 4 + gesture_count * 2 * 5;

//! Writes the versioned encoding into `out`. Returns the bytes written.
std::size_t encode(const Bindings& bindings, std::uint8_t (&out)[encoded_bindings_size]) noexcept;

//! Reads an encoding. Never fails: an empty, foreign or truncated blob gives the defaults, and an
//! action with out-of-range bytes falls back to that action's default. Extra trailing data (a
//! newer version with more gestures) is ignored.
[[nodiscard]] Bindings decode(const void* data, std::size_t size) noexcept;

} // namespace filetree::actions
