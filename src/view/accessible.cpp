// No SDK headers: the offline tests build this file on its own.
#include "accessible.h"

#include <climits>

#include <algorithm>

#pragma comment(lib, "oleacc.lib")

namespace filetree::view {
namespace {

BSTR make_bstr(const std::wstring& text) noexcept {
    return SysAllocStringLen(text.data(), static_cast<UINT>(text.size()));
}

void set_child(VARIANT* out, long id) noexcept {
    VariantInit(out);
    out->vt = VT_I4;
    out->lVal = id;
}

//! IEnumVARIANT over child ids, for a selection of several rows.
class ChildEnum final : public IEnumVARIANT {
public:
    explicit ChildEnum(std::vector<long> ids) : ids_(std::move(ids)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) noexcept override {
        if (out == nullptr) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IEnumVARIANT) {
            *out = static_cast<IEnumVARIANT*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() noexcept override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() noexcept override {
        const ULONG left = --refs_;
        if (left == 0) delete this;
        return left;
    }
    HRESULT STDMETHODCALLTYPE Next(ULONG count, VARIANT* out, ULONG* fetched) noexcept override {
        if (out == nullptr) return E_POINTER;
        ULONG done = 0;
        while (done < count && at_ < ids_.size()) set_child(&out[done++], ids_[at_++]);
        if (fetched != nullptr) *fetched = done;
        return done == count ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Skip(ULONG count) noexcept override {
        at_ = std::min(ids_.size(), at_ + count);
        return at_ < ids_.size() ? S_OK : S_FALSE;
    }
    HRESULT STDMETHODCALLTYPE Reset() noexcept override {
        at_ = 0;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Clone(IEnumVARIANT** out) noexcept override {
        if (out == nullptr) return E_POINTER;
        try {
            auto* copy = new ChildEnum(ids_);
            copy->at_ = at_;
            *out = copy;
            return S_OK;
        } catch (...) {
            *out = nullptr;
            return E_OUTOFMEMORY;
        }
    }

private:
    std::vector<long> ids_;
    std::size_t at_{0};
    ULONG refs_{1};
};

} // namespace

TreeAccessible* TreeAccessible::create(HWND wnd, AccessSource& source) noexcept {
    try {
        return new TreeAccessible(wnd, source);
    } catch (...) {
        return nullptr;
    }
}

void TreeAccessible::detach() noexcept {
    source_ = nullptr;
    wnd_ = nullptr;
    Release();
}

LRESULT TreeAccessible::answer(WPARAM wp) noexcept {
    return LresultFromObject(IID_IAccessible, wp, static_cast<IAccessible*>(this));
}

HRESULT TreeAccessible::QueryInterface(REFIID riid, void** out) noexcept {
    if (out == nullptr) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IDispatch || riid == IID_IAccessible) {
        *out = static_cast<IAccessible*>(this);
        AddRef();
        return S_OK;
    }
    *out = nullptr;
    return E_NOINTERFACE;
}

ULONG TreeAccessible::AddRef() noexcept { return static_cast<ULONG>(InterlockedIncrement(&refs_)); }

ULONG TreeAccessible::Release() noexcept {
    const LONG left = InterlockedDecrement(&refs_);
    if (left == 0) delete this;
    return static_cast<ULONG>(left);
}

HRESULT TreeAccessible::GetTypeInfoCount(UINT* count) noexcept {
    if (count != nullptr) *count = 0;
    return S_OK;
}
HRESULT TreeAccessible::GetTypeInfo(UINT, LCID, ITypeInfo** info) noexcept {
    if (info != nullptr) *info = nullptr;
    return E_NOTIMPL;
}
HRESULT TreeAccessible::GetIDsOfNames(REFIID, LPOLESTR*, UINT, LCID, DISPID*) noexcept {
    return E_NOTIMPL;
}
HRESULT TreeAccessible::Invoke(DISPID, REFIID, LCID, WORD, DISPPARAMS*, VARIANT*, EXCEPINFO*,
                               UINT*) noexcept {
    return E_NOTIMPL;
}

long TreeAccessible::check_child(const VARIANT& child) const noexcept {
    if (child.vt != VT_I4) return -1;
    if (child.lVal == CHILDID_SELF) return 0;
    if (child.lVal < 1 || static_cast<std::size_t>(child.lVal) > source_->acc_row_count()) return -1;
    return child.lVal;
}

#define FT_ACC_ALIVE() \
    if (source_ == nullptr || wnd_ == nullptr) return RPC_E_DISCONNECTED

HRESULT TreeAccessible::get_accParent(IDispatch** parent) noexcept {
    if (parent == nullptr) return E_POINTER;
    *parent = nullptr;
    FT_ACC_ALIVE();
    // The window object (frame, scroll bar) the standard proxy builds for us.
    return AccessibleObjectFromWindow(wnd_, static_cast<DWORD>(OBJID_WINDOW), IID_IDispatch,
                                      reinterpret_cast<void**>(parent));
}

HRESULT TreeAccessible::get_accChildCount(long* count) noexcept {
    if (count == nullptr) return E_POINTER;
    *count = 0;
    FT_ACC_ALIVE();
    *count = static_cast<long>(std::min<std::size_t>(source_->acc_row_count(), LONG_MAX));
    return S_OK;
}

HRESULT TreeAccessible::get_accChild(VARIANT child, IDispatch** out) noexcept {
    if (out == nullptr) return E_POINTER;
    *out = nullptr;
    FT_ACC_ALIVE();
    return check_child(child) < 0 ? E_INVALIDARG : S_FALSE; // rows are simple elements
}

HRESULT TreeAccessible::get_accName(VARIANT child, BSTR* name) noexcept {
    if (name == nullptr) return E_POINTER;
    *name = nullptr;
    FT_ACC_ALIVE();
    const long id = check_child(child);
    if (id < 0) return E_INVALIDARG;
    if (id == 0) {
        *name = SysAllocString(L"Folder Tree");
        return S_OK;
    }
    try {
        AccessRow row;
        if (!source_->acc_row(static_cast<std::size_t>(id - 1), row)) return E_INVALIDARG;
        *name = make_bstr(row.name);
        return S_OK;
    } catch (...) {
        return E_OUTOFMEMORY;
    }
}

HRESULT TreeAccessible::get_accValue(VARIANT child, BSTR* value) noexcept {
    if (value == nullptr) return E_POINTER;
    *value = nullptr;
    FT_ACC_ALIVE();
    const long id = check_child(child);
    if (id < 0) return E_INVALIDARG;
    if (id == 0) return S_FALSE;
    try {
        // Like SysTreeView32: an item's value is its level, which screen readers announce.
        AccessRow row;
        if (!source_->acc_row(static_cast<std::size_t>(id - 1), row)) return E_INVALIDARG;
        *value = make_bstr(std::to_wstring(row.level));
        return S_OK;
    } catch (...) {
        return E_OUTOFMEMORY;
    }
}

HRESULT TreeAccessible::get_accDescription(VARIANT, BSTR* text) noexcept {
    if (text != nullptr) *text = nullptr;
    return S_FALSE;
}

HRESULT TreeAccessible::get_accRole(VARIANT child, VARIANT* role) noexcept {
    if (role == nullptr) return E_POINTER;
    VariantInit(role);
    FT_ACC_ALIVE();
    const long id = check_child(child);
    if (id < 0) return E_INVALIDARG;
    role->vt = VT_I4;
    role->lVal = id == 0 ? ROLE_SYSTEM_OUTLINE : ROLE_SYSTEM_OUTLINEITEM;
    return S_OK;
}

HRESULT TreeAccessible::get_accState(VARIANT child, VARIANT* state) noexcept {
    if (state == nullptr) return E_POINTER;
    VariantInit(state);
    FT_ACC_ALIVE();
    const long id = check_child(child);
    if (id < 0) return E_INVALIDARG;
    state->vt = VT_I4;
    if (id == 0) {
        long bits = STATE_SYSTEM_FOCUSABLE | STATE_SYSTEM_MULTISELECTABLE |
                    STATE_SYSTEM_EXTSELECTABLE;
        if (source_->acc_has_focus()) bits |= STATE_SYSTEM_FOCUSED;
        if (!IsWindowVisible(wnd_)) bits |= STATE_SYSTEM_INVISIBLE;
        state->lVal = bits;
        return S_OK;
    }
    try {
        const auto index = static_cast<std::size_t>(id - 1);
        AccessRow row;
        if (!source_->acc_row(index, row)) return E_INVALIDARG;
        long bits = STATE_SYSTEM_SELECTABLE | STATE_SYSTEM_FOCUSABLE;
        if (row.selected) bits |= STATE_SYSTEM_SELECTED;
        if (row.container) bits |= row.expanded ? STATE_SYSTEM_EXPANDED : STATE_SYSTEM_COLLAPSED;
        if (source_->acc_focus_row() == static_cast<std::ptrdiff_t>(index) &&
            source_->acc_has_focus()) {
            bits |= STATE_SYSTEM_FOCUSED;
        }
        RECT rect{};
        if (!source_->acc_row_rect(index, rect)) bits |= STATE_SYSTEM_OFFSCREEN | STATE_SYSTEM_INVISIBLE;
        state->lVal = bits;
        return S_OK;
    } catch (...) {
        return E_OUTOFMEMORY;
    }
}

HRESULT TreeAccessible::get_accHelp(VARIANT, BSTR* help) noexcept {
    if (help != nullptr) *help = nullptr;
    return S_FALSE;
}

HRESULT TreeAccessible::get_accHelpTopic(BSTR* file, VARIANT, long* topic) noexcept {
    if (file != nullptr) *file = nullptr;
    if (topic != nullptr) *topic = 0;
    return S_FALSE;
}

HRESULT TreeAccessible::get_accKeyboardShortcut(VARIANT, BSTR* keys) noexcept {
    if (keys != nullptr) *keys = nullptr;
    return S_FALSE;
}

HRESULT TreeAccessible::get_accFocus(VARIANT* child) noexcept {
    if (child == nullptr) return E_POINTER;
    VariantInit(child);
    FT_ACC_ALIVE();
    if (!source_->acc_has_focus()) return S_FALSE;
    const std::ptrdiff_t row = source_->acc_focus_row();
    set_child(child, row >= 0 ? static_cast<long>(row + 1) : CHILDID_SELF);
    return S_OK;
}

HRESULT TreeAccessible::get_accSelection(VARIANT* children) noexcept {
    if (children == nullptr) return E_POINTER;
    VariantInit(children);
    FT_ACC_ALIVE();
    try {
        std::vector<std::size_t> rows;
        source_->acc_selected_rows(rows);
        if (rows.empty()) return S_FALSE;
        if (rows.size() == 1) {
            set_child(children, static_cast<long>(rows.front() + 1));
            return S_OK;
        }
        std::vector<long> ids;
        ids.reserve(rows.size());
        for (const std::size_t row : rows) ids.push_back(static_cast<long>(row + 1));
        children->vt = VT_UNKNOWN;
        children->punkVal = new ChildEnum(std::move(ids));
        return S_OK;
    } catch (...) {
        return E_OUTOFMEMORY;
    }
}

HRESULT TreeAccessible::get_accDefaultAction(VARIANT child, BSTR* action) noexcept {
    if (action == nullptr) return E_POINTER;
    *action = nullptr;
    FT_ACC_ALIVE();
    const long id = check_child(child);
    if (id < 0) return E_INVALIDARG;
    if (id == 0) return S_FALSE;
    *action = SysAllocString(L"Open");
    return S_OK;
}

HRESULT TreeAccessible::accSelect(long flags, VARIANT child) noexcept {
    FT_ACC_ALIVE();
    const long id = check_child(child);
    if (id <= 0) return E_INVALIDARG;
    source_->acc_select(static_cast<std::size_t>(id - 1), flags);
    return S_OK;
}

HRESULT TreeAccessible::accLocation(long* left, long* top, long* width, long* height,
                                    VARIANT child) noexcept {
    if (left == nullptr || top == nullptr || width == nullptr || height == nullptr) return E_POINTER;
    *left = *top = *width = *height = 0;
    FT_ACC_ALIVE();
    const long id = check_child(child);
    if (id < 0) return E_INVALIDARG;
    RECT rect{};
    if (id == 0) {
        GetClientRect(wnd_, &rect);
    } else if (!source_->acc_row_rect(static_cast<std::size_t>(id - 1), rect)) {
        return S_FALSE;
    }
    MapWindowPoints(wnd_, nullptr, reinterpret_cast<POINT*>(&rect), 2);
    *left = rect.left;
    *top = rect.top;
    *width = rect.right - rect.left;
    *height = rect.bottom - rect.top;
    return S_OK;
}

HRESULT TreeAccessible::accNavigate(long direction, VARIANT start, VARIANT* end) noexcept {
    if (end == nullptr) return E_POINTER;
    VariantInit(end);
    FT_ACC_ALIVE();
    const long id = check_child(start);
    if (id < 0) return E_INVALIDARG;
    const auto count = static_cast<long>(std::min<std::size_t>(source_->acc_row_count(), LONG_MAX));
    long to = 0;
    switch (direction) {
    case NAVDIR_FIRSTCHILD:
        if (id != 0) return E_INVALIDARG;
        to = count > 0 ? 1 : 0;
        break;
    case NAVDIR_LASTCHILD:
        if (id != 0) return E_INVALIDARG;
        to = count;
        break;
    case NAVDIR_NEXT:
    case NAVDIR_DOWN:
        if (id == 0) return S_FALSE; // the window's siblings belong to the proxy
        to = id < count ? id + 1 : 0;
        break;
    case NAVDIR_PREVIOUS:
    case NAVDIR_UP:
        if (id == 0) return S_FALSE;
        to = id > 1 ? id - 1 : 0;
        break;
    default:
        return S_FALSE;
    }
    if (to <= 0) return S_FALSE;
    set_child(end, to);
    return S_OK;
}

HRESULT TreeAccessible::accHitTest(long x, long y, VARIANT* child) noexcept {
    if (child == nullptr) return E_POINTER;
    VariantInit(child);
    FT_ACC_ALIVE();
    POINT point{x, y};
    ScreenToClient(wnd_, &point);
    RECT client{};
    GetClientRect(wnd_, &client);
    if (!PtInRect(&client, point)) return S_FALSE;
    const std::ptrdiff_t row = source_->acc_row_at(point);
    set_child(child, row >= 0 ? static_cast<long>(row + 1) : CHILDID_SELF);
    return S_OK;
}

HRESULT TreeAccessible::accDoDefaultAction(VARIANT child) noexcept {
    FT_ACC_ALIVE();
    const long id = check_child(child);
    if (id <= 0) return E_INVALIDARG;
    source_->acc_default_action(static_cast<std::size_t>(id - 1));
    return S_OK;
}

HRESULT TreeAccessible::put_accName(VARIANT, BSTR) noexcept { return E_NOTIMPL; }
HRESULT TreeAccessible::put_accValue(VARIANT, BSTR) noexcept { return E_NOTIMPL; }

#undef FT_ACC_ALIVE

} // namespace filetree::view
