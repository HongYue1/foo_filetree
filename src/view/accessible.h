#pragma once

// Accessibility (M8c): an MSAA IAccessible for the tree window, answered from WM_GETOBJECT.
// The tree is an outline; each visible row is a simple child element (child id = row + 1) with
// its name, level (value), selected / focused / expanded state and screen location. Windows
// exposes it to UI Automation clients (Narrator) through its MSAA proxy, and NVDA / JAWS read
// MSAA directly. Events come from the view (NotifyWinEvent, see AccessSource users).

#include <windows.h>

#include <oleacc.h>

#include <cstddef>
#include <string>
#include <vector>

namespace filetree::view {

struct AccessRow {
    std::wstring name;
    int level{};
    bool container{};
    bool expanded{};
    bool selected{};
};

class AccessSource {
public:
    [[nodiscard]] virtual std::size_t acc_row_count() const noexcept = 0;
    virtual bool acc_row(std::size_t row, AccessRow& out) const = 0;
    //! Client rectangle of the row; false when it is scrolled out of view.
    virtual bool acc_row_rect(std::size_t row, RECT& out) const noexcept = 0;
    [[nodiscard]] virtual std::ptrdiff_t acc_focus_row() const noexcept = 0;
    [[nodiscard]] virtual bool acc_has_focus() const noexcept = 0;
    [[nodiscard]] virtual std::ptrdiff_t acc_row_at(POINT client) const noexcept = 0;
    virtual void acc_selected_rows(std::vector<std::size_t>& out) const = 0;
    //! SELFLAG_* from IAccessible::accSelect.
    virtual void acc_select(std::size_t row, long flags) noexcept = 0;
    //! What Enter does on the row.
    virtual void acc_default_action(std::size_t row) noexcept = 0;

protected:
    ~AccessSource() = default;
};

class TreeAccessible final : public IAccessible {
public:
    //! A new object with one reference, for the view to hand out and detach on destroy.
    static TreeAccessible* create(HWND wnd, AccessSource& source) noexcept;
    //! The view is going away: every later call fails with RPC_E_DISCONNECTED. Releases the
    //! view's reference.
    void detach() noexcept;
    //! WM_GETOBJECT for OBJID_CLIENT.
    LRESULT answer(WPARAM wp) noexcept;

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) noexcept override;
    ULONG STDMETHODCALLTYPE AddRef() noexcept override;
    ULONG STDMETHODCALLTYPE Release() noexcept override;
    // IDispatch (not used by MSAA clients)
    HRESULT STDMETHODCALLTYPE GetTypeInfoCount(UINT* count) noexcept override;
    HRESULT STDMETHODCALLTYPE GetTypeInfo(UINT, LCID, ITypeInfo** info) noexcept override;
    HRESULT STDMETHODCALLTYPE GetIDsOfNames(REFIID, LPOLESTR*, UINT, LCID, DISPID*) noexcept override;
    HRESULT STDMETHODCALLTYPE Invoke(DISPID, REFIID, LCID, WORD, DISPPARAMS*, VARIANT*,
                                     EXCEPINFO*, UINT*) noexcept override;
    // IAccessible
    HRESULT STDMETHODCALLTYPE get_accParent(IDispatch** parent) noexcept override;
    HRESULT STDMETHODCALLTYPE get_accChildCount(long* count) noexcept override;
    HRESULT STDMETHODCALLTYPE get_accChild(VARIANT child, IDispatch** out) noexcept override;
    HRESULT STDMETHODCALLTYPE get_accName(VARIANT child, BSTR* name) noexcept override;
    HRESULT STDMETHODCALLTYPE get_accValue(VARIANT child, BSTR* value) noexcept override;
    HRESULT STDMETHODCALLTYPE get_accDescription(VARIANT child, BSTR* text) noexcept override;
    HRESULT STDMETHODCALLTYPE get_accRole(VARIANT child, VARIANT* role) noexcept override;
    HRESULT STDMETHODCALLTYPE get_accState(VARIANT child, VARIANT* state) noexcept override;
    HRESULT STDMETHODCALLTYPE get_accHelp(VARIANT child, BSTR* help) noexcept override;
    HRESULT STDMETHODCALLTYPE get_accHelpTopic(BSTR* file, VARIANT child, long* topic) noexcept override;
    HRESULT STDMETHODCALLTYPE get_accKeyboardShortcut(VARIANT child, BSTR* keys) noexcept override;
    HRESULT STDMETHODCALLTYPE get_accFocus(VARIANT* child) noexcept override;
    HRESULT STDMETHODCALLTYPE get_accSelection(VARIANT* children) noexcept override;
    HRESULT STDMETHODCALLTYPE get_accDefaultAction(VARIANT child, BSTR* action) noexcept override;
    HRESULT STDMETHODCALLTYPE accSelect(long flags, VARIANT child) noexcept override;
    HRESULT STDMETHODCALLTYPE accLocation(long* left, long* top, long* width, long* height,
                                          VARIANT child) noexcept override;
    HRESULT STDMETHODCALLTYPE accNavigate(long direction, VARIANT start, VARIANT* end) noexcept override;
    HRESULT STDMETHODCALLTYPE accHitTest(long x, long y, VARIANT* child) noexcept override;
    HRESULT STDMETHODCALLTYPE accDoDefaultAction(VARIANT child) noexcept override;
    HRESULT STDMETHODCALLTYPE put_accName(VARIANT, BSTR) noexcept override;
    HRESULT STDMETHODCALLTYPE put_accValue(VARIANT, BSTR) noexcept override;

private:
    TreeAccessible(HWND wnd, AccessSource& source) noexcept : wnd_(wnd), source_(&source) {}
    ~TreeAccessible() = default;
    //! 0 for the tree itself, row + 1 for a row, -1 for an invalid id.
    [[nodiscard]] long check_child(const VARIANT& child) const noexcept;

    LONG refs_{1};
    HWND wnd_{};
    AccessSource* source_{};
};

} // namespace filetree::view
