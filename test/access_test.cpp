// Offline tests for the MSAA object (view/accessible.cpp) with a fake row source, through a real
// window and oleacc's WM_GETOBJECT round trip.

#include <windows.h>

#include <oleacc.h>

#include <string>
#include <vector>

#include "check.h"
#include "../src/view/accessible.h"

using namespace filetree::view;

namespace {

struct FakeSource final : AccessSource {
    std::vector<AccessRow> rows;
    std::ptrdiff_t focus{-1};
    long last_flags{-1};
    std::size_t last_row{999};
    int defaults{0};
    std::size_t acc_row_count() const noexcept override { return rows.size(); }
    bool acc_row(std::size_t row, AccessRow& out) const override {
        if (row >= rows.size()) return false;
        out = rows[row];
        return true;
    }
    bool acc_row_rect(std::size_t row, RECT& out) const noexcept override {
        if (row >= 2) return false; // only two rows fit
        out = RECT{0, static_cast<LONG>(row) * 20, 100, static_cast<LONG>(row) * 20 + 20};
        return true;
    }
    std::ptrdiff_t acc_focus_row() const noexcept override { return focus; }
    bool acc_has_focus() const noexcept override { return true; }
    std::ptrdiff_t acc_row_at(POINT p) const noexcept override { return p.y / 20 < 2 ? p.y / 20 : -1; }
    void acc_selected_rows(std::vector<std::size_t>& out) const override {
        out.clear();
        for (std::size_t i = 0; i < rows.size(); ++i) {
            if (rows[i].selected) out.push_back(i);
        }
    }
    void acc_select(std::size_t row, long flags) noexcept override {
        last_row = row;
        last_flags = flags;
    }
    void acc_default_action(std::size_t) noexcept override { ++defaults; }
};

TreeAccessible* g_acc = nullptr;

LRESULT CALLBACK test_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_GETOBJECT && static_cast<DWORD>(lp) == static_cast<DWORD>(OBJID_CLIENT) &&
        g_acc != nullptr) {
        return g_acc->answer(wp);
    }
    return DefWindowProcW(wnd, msg, wp, lp);
}

VARIANT child(long id) {
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_I4;
    v.lVal = id;
    return v;
}

} // namespace

void test_accessible() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    WNDCLASSW wc{};
    wc.lpfnWndProc = test_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"ft_access_test";
    RegisterClassW(&wc);
    HWND wnd = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 100, 100, 200, 200,
                               nullptr, nullptr, wc.hInstance, nullptr);
    CHECK(wnd != nullptr);

    FakeSource source;
    source.rows = {{L"C:", 0, true, true, false}, {L"Music", 1, true, false, true},
                   {L"a.mp3", 1, false, false, true}};
    source.focus = 1;
    g_acc = TreeAccessible::create(wnd, source);
    CHECK(g_acc != nullptr);

    IAccessible* acc = nullptr;
    const HRESULT hr = AccessibleObjectFromWindow(wnd, static_cast<DWORD>(OBJID_CLIENT),
                                                  IID_IAccessible, reinterpret_cast<void**>(&acc));
    CHECK(SUCCEEDED(hr) && acc != nullptr);
    if (acc == nullptr) return;

    long count = 0;
    CHECK(acc->get_accChildCount(&count) == S_OK && count == 3);
    VARIANT role;
    CHECK(acc->get_accRole(child(CHILDID_SELF), &role) == S_OK && role.lVal == ROLE_SYSTEM_OUTLINE);
    CHECK(acc->get_accRole(child(2), &role) == S_OK && role.lVal == ROLE_SYSTEM_OUTLINEITEM);
    BSTR text = nullptr;
    CHECK(acc->get_accName(child(2), &text) == S_OK && text != nullptr && std::wstring(text) == L"Music");
    SysFreeString(text);
    CHECK(acc->get_accValue(child(2), &text) == S_OK && text != nullptr && std::wstring(text) == L"1");
    SysFreeString(text);
    CHECK(acc->get_accName(child(4), &text) == E_INVALIDARG);

    VARIANT state;
    CHECK(acc->get_accState(child(1), &state) == S_OK);
    CHECK((state.lVal & STATE_SYSTEM_EXPANDED) != 0 && (state.lVal & STATE_SYSTEM_SELECTED) == 0);
    CHECK(acc->get_accState(child(2), &state) == S_OK);
    CHECK((state.lVal & STATE_SYSTEM_COLLAPSED) != 0 && (state.lVal & STATE_SYSTEM_SELECTED) != 0 &&
          (state.lVal & STATE_SYSTEM_FOCUSED) != 0);
    CHECK(acc->get_accState(child(3), &state) == S_OK);
    CHECK((state.lVal & (STATE_SYSTEM_EXPANDED | STATE_SYSTEM_COLLAPSED)) == 0 &&
          (state.lVal & STATE_SYSTEM_OFFSCREEN) != 0);

    VARIANT focus;
    CHECK(acc->get_accFocus(&focus) == S_OK && focus.vt == VT_I4 && focus.lVal == 2);
    VARIANT selection;
    CHECK(acc->get_accSelection(&selection) == S_OK && selection.vt == VT_UNKNOWN);
    if (selection.vt == VT_UNKNOWN) {
        IEnumVARIANT* items = nullptr;
        selection.punkVal->QueryInterface(IID_IEnumVARIANT, reinterpret_cast<void**>(&items));
        CHECK(items != nullptr);
        if (items != nullptr) {
            VARIANT got[3];
            ULONG fetched = 0;
            CHECK(items->Next(3, got, &fetched) == S_FALSE && fetched == 2);
            CHECK(got[0].lVal == 2 && got[1].lVal == 3);
            items->Release();
        }
        VariantClear(&selection);
    }

    long x = 0, y = 0, w = 0, h = 0;
    CHECK(acc->accLocation(&x, &y, &w, &h, child(2)) == S_OK && w == 100 && h == 20);
    POINT origin{0, 0};
    ClientToScreen(wnd, &origin);
    CHECK(x == origin.x && y == origin.y + 20);
    VARIANT hit;
    CHECK(acc->accHitTest(origin.x + 5, origin.y + 25, &hit) == S_OK && hit.lVal == 2);
    VARIANT next;
    CHECK(acc->accNavigate(NAVDIR_NEXT, child(2), &next) == S_OK && next.lVal == 3);
    CHECK(acc->accNavigate(NAVDIR_NEXT, child(3), &next) == S_FALSE);
    CHECK(acc->accNavigate(NAVDIR_FIRSTCHILD, child(CHILDID_SELF), &next) == S_OK && next.lVal == 1);
    CHECK(acc->accSelect(SELFLAG_TAKEFOCUS | SELFLAG_TAKESELECTION, child(3)) == S_OK);
    CHECK(source.last_row == 2 && source.last_flags == (SELFLAG_TAKEFOCUS | SELFLAG_TAKESELECTION));
    CHECK(acc->accDoDefaultAction(child(1)) == S_OK && source.defaults == 1);

    // The view goes away while a client still holds the object.
    g_acc->detach();
    g_acc = nullptr;
    CHECK(acc->get_accChildCount(&count) == RPC_E_DISCONNECTED);
    acc->Release();
    DestroyWindow(wnd);
    CoUninitialize();
}
