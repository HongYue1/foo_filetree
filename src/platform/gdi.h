#pragma once

// Small RAII wrappers for GDI objects. Header-only; no allocations beyond the GDI object itself.

#include <windows.h>

#include <utility>

namespace filetree::gdi {

//! A solid brush that remembers its colour, so set() is free when nothing changed.
//! Main thread only, like every GDI object owned by a window.
class SolidBrush {
public:
    SolidBrush() noexcept = default;
    ~SolidBrush() { reset(); }

    SolidBrush(const SolidBrush&) = delete;
    SolidBrush& operator=(const SolidBrush&) = delete;

    SolidBrush(SolidBrush&& other) noexcept
        : brush_(std::exchange(other.brush_, nullptr)), colour_(other.colour_) {}

    SolidBrush& operator=(SolidBrush&& other) noexcept {
        if (this != &other) {
            reset();
            brush_ = std::exchange(other.brush_, nullptr);
            colour_ = other.colour_;
        }
        return *this;
    }

    //! Returns true when the colour changed (the caller should invalidate).
    bool set(COLORREF colour) noexcept {
        if (brush_ != nullptr && colour == colour_) return false;
        reset();
        brush_ = CreateSolidBrush(colour);
        colour_ = colour;
        return true;
    }

    [[nodiscard]] HBRUSH get() const noexcept { return brush_; }

    void reset() noexcept {
        if (brush_ != nullptr) {
            DeleteObject(brush_);
            brush_ = nullptr;
        }
    }

private:
    HBRUSH brush_{};
    COLORREF colour_{};
};

//! Fills only the invalid rectangle. Falls back to the system window colour if no brush exists.
inline void fill_paint_rect(HDC dc, const RECT& rect, HBRUSH brush) noexcept {
    FillRect(dc, &rect, brush != nullptr ? brush : GetSysColorBrush(COLOR_WINDOW));
}

} // namespace filetree::gdi
