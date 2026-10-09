// The window (Windows 7 - 11): the same tabs, lists and buttons as the Python version, in plain Win32.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>

#include <cstdio>
#include <memory>

#include "core.h"
#include "drivers.h"
#include "scanner.h"

using namespace ec;

namespace {

const wchar_t* MAIN_CLASS = L"EasyCentralBotMain";
const wchar_t* PANEL_CLASS = L"EasyCentralBotPanel";
const wchar_t* INPUT_CLASS = L"EasyCentralBotInput";
const UINT_PTR TIMER_TICK = 1, TIMER_PUMP = 2;
const char* BROWSERS[] = {"Chrome", "Opera", "Edge"};

std::wstring W(const string& s) { return widen(s); }
string U(const std::wstring& w) { return narrow(w); }
string get_text(HWND h) {
    int n = GetWindowTextLengthW(h);
    std::wstring w(n + 1, L'\0');
    GetWindowTextW(h, &w[0], n + 1);
    w.resize(n);
    return U(w);
}
void set_text(HWND h, const string& s) {
    if (get_text(h) != s) SetWindowTextW(h, W(s).c_str());
}

// ---------------------------------------------------------------- a small modal text box
struct InputState {
    HWND edit = nullptr;
    bool done = false, ok = false;
    string result;
};

LRESULT CALLBACK InputProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto* st = (InputState*)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (m) {
    case WM_COMMAND:
        if (LOWORD(w) == IDOK || LOWORD(w) == IDCANCEL) {
            if (LOWORD(w) == IDOK) {
                st->ok = true;
                st->result = get_text(st->edit);
            }
            st->done = true;
            return 0;
        }
        break;
    case WM_CLOSE:
        st->done = true;
        return 0;
    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)w, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    }
    return DefWindowProcW(h, m, w, l);
}

}  // namespace

class Gui {
public:
    explicit Gui(HINSTANCE inst) : inst_(inst) {}
    int run();

private:
    struct ListUI {
        HWND panel = nullptr, lv = nullptr;
        string attr, kind;
        vector<vector<std::wstring>> rows;
        vector<HWND> buttons;
        HWND header = nullptr;                             // the Repeat tab's top row
    };

    // building
    void build_ui();
    HWND mk(const wchar_t* cls, const string& text, DWORD style, HWND parent, std::function<void(int)> on = nullptr,
            DWORD ex = 0);
    HWND label(HWND parent, const string& text, bool gray = false);
    HWND button(HWND parent, const string& text, std::function<void()> on);
    HWND edit(HWND parent, const string& text, std::function<void(const string&)> on_change, DWORD extra = 0);
    HWND check(HWND parent, const string& text, bool on, std::function<void(bool)> changed);
    HWND combo(HWND parent, const vector<string>& items, const string& sel, std::function<void(const string&)> changed);
    HWND panel(HWND parent);
    ListUI* make_list(HWND tabs, const string& attr, const string& kind,
                      const vector<std::pair<string, std::function<void(ListUI&)>>>& buttons);
    void browser_rows(HWND parent, bool scanner, vector<HWND>& row1, vector<vector<HWND>>& drivers, HWND& hint);

    // layout
    void layout();
    void layout_scanner();
    void layout_splitter();
    void layout_phone();
    void layout_list(ListUI& l, RECT r);
    void show_tab(HWND tabs, const vector<HWND>& pages);
    RECT page_rect(HWND tabs);
    int S(int v) const { return MulDiv(v, dpi_, 96); }

    // behaviour
    void refresh_lists();
    void refresh_list(ListUI& l);
    void update_status();
    vector<int> selected(ListUI& l);
    void select_rows(ListUI& l, const vector<int>& idx);
    void move_sel(ListUI& l, const string& where);
    void info(const string& title, const string& text, UINT icon = MB_ICONINFORMATION);
    bool ask(const string& title, const string& text);
    bool input(const string& title, const string& prompt, const string& initial, bool multiline, string& out);
    string open_file(const string& title, const wchar_t* filter);
    void copy_text(const string& text);
    void append_log(const string& line);
    void browse_driver(const string& kind);
    void import_excel();
    LRESULT handle(HWND h, UINT m, WPARAM w, LPARAM l);
    LRESULT notify(NMHDR* n);
    static LRESULT CALLBACK MainProc(HWND h, UINT m, WPARAM w, LPARAM l);
    static LRESULT CALLBACK PanelProc(HWND h, UINT m, WPARAM w, LPARAM l);

    HINSTANCE inst_;
    HWND main_ = nullptr;
    HFONT font_ = nullptr, bold_ = nullptr;
    int dpi_ = 96, line_h_ = 16;
    std::unique_ptr<Core> core_;
    std::map<int, std::function<void(int)>> handlers_;
    int next_id_ = 1000;
    std::set<HWND> gray_;
    HBRUSH panel_brush_ = nullptr;

    // controls (rebuilt with build_ui)
    HWND env_lbl_ = nullptr, lang_lbl_ = nullptr, lang_combo_ = nullptr, tabs_ = nullptr, log_ = nullptr;
    HWND scan_page_ = nullptr, split_page_ = nullptr, phone_page_ = nullptr;
    // scanner
    HWND start_btn_ = nullptr, now_btn_ = nullptr, login_scan_btn_ = nullptr, status_lbl_ = nullptr;
    vector<HWND> scan_row2_, scan_row3_, scan_browser_row_, fill_row_;
    vector<vector<HWND>> scan_driver_rows_;
    HWND scan_hint_ = nullptr, scan_tabs_ = nullptr;
    // splitter
    HWND split_tabs_ = nullptr, split_run_ = nullptr;
    HWND split_link_lbl_ = nullptr, split_url_ = nullptr, split_status_ = nullptr, rules_lbl_ = nullptr;
    vector<HWND> split_opts_, split_prices_, split_browser_row_, split_btns_;
    vector<vector<HWND>> split_driver_rows_;
    HWND split_hint_ = nullptr;
    // phone
    HWND remote_chk_ = nullptr, remote_info_ = nullptr, remote_help_ = nullptr, remote_port_ = nullptr, remote_pin_ = nullptr;
    vector<HWND> phone_row1_, phone_row2_;
    // driver path boxes (both tabs show them)
    vector<HWND> chrome_edits_, edge_edits_, opera_edits_;
    // lists
    ListUI* lists_[4] = {nullptr, nullptr, nullptr, nullptr};   // pending, done, repeat, saved
    vector<std::unique_ptr<ListUI>> list_store_;
    vector<HWND> scan_list_pages_, split_pages_;
    int tab_sel_ = 0, scan_tab_sel_ = 0, split_tab_sel_ = 0;
    int ticks_ = 0;
    bool building_ = false;
};

static Gui* g_gui = nullptr;

// ---------------------------------------------------------------- control helpers
HWND Gui::mk(const wchar_t* cls, const string& text, DWORD style, HWND parent, std::function<void(int)> on, DWORD ex) {
    int id = next_id_++;
    HWND h = CreateWindowExW(ex, cls, W(text).c_str(), style | WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, parent,
                             (HMENU)(INT_PTR)id, inst_, nullptr);
    SendMessageW(h, WM_SETFONT, (WPARAM)font_, TRUE);
    if (on) handlers_[id] = on;
    return h;
}
HWND Gui::label(HWND parent, const string& text, bool gray) {
    HWND h = mk(L"STATIC", text, SS_LEFT | SS_NOPREFIX, parent);
    if (gray) gray_.insert(h);
    return h;
}
HWND Gui::button(HWND parent, const string& text, std::function<void()> on) {
    return mk(L"BUTTON", text, BS_PUSHBUTTON | WS_TABSTOP, parent, [on](int code) {
        if (code == BN_CLICKED) on();
    });
}
HWND Gui::edit(HWND parent, const string& text, std::function<void(const string&)> on_change, DWORD extra) {
    HWND h = mk(L"EDIT", text, ES_AUTOHSCROLL | WS_TABSTOP | extra, parent, nullptr, WS_EX_CLIENTEDGE);
    int id = GetDlgCtrlID(h);
    if (on_change)
        handlers_[id] = [this, h, on_change](int code) {
            if (code == EN_CHANGE && !building_) on_change(get_text(h));
        };
    return h;
}
HWND Gui::check(HWND parent, const string& text, bool on, std::function<void(bool)> changed) {
    HWND h = mk(L"BUTTON", text, BS_AUTOCHECKBOX | WS_TABSTOP, parent);
    SendMessageW(h, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
    handlers_[GetDlgCtrlID(h)] = [h, changed](int code) {
        if (code == BN_CLICKED) changed(SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED);
    };
    return h;
}
HWND Gui::combo(HWND parent, const vector<string>& items, const string& sel, std::function<void(const string&)> changed) {
    HWND h = mk(L"COMBOBOX", "", CBS_DROPDOWNLIST | WS_TABSTOP | WS_VSCROLL, parent);
    int pick = 0;
    for (size_t i = 0; i < items.size(); i++) {
        SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)W(items[i]).c_str());
        if (items[i] == sel) pick = (int)i;
    }
    SendMessageW(h, CB_SETCURSEL, pick, 0);
    handlers_[GetDlgCtrlID(h)] = [h, items, changed](int code) {
        if (code != CBN_SELCHANGE) return;
        int i = (int)SendMessageW(h, CB_GETCURSEL, 0, 0);
        if (i >= 0 && i < (int)items.size()) changed(items[i]);
    };
    return h;
}
HWND Gui::panel(HWND parent) {
    HWND h = CreateWindowExW(WS_EX_CONTROLPARENT, PANEL_CLASS, L"", WS_CHILD | WS_CLIPCHILDREN, 0, 0, 10, 10, parent,
                             nullptr, inst_, nullptr);
    return h;
}

// ---------------------------------------------------------------- building the window
Gui::ListUI* Gui::make_list(HWND tabs, const string& attr, const string& kind,
                            const vector<std::pair<string, std::function<void(ListUI&)>>>& buttons) {
    list_store_.emplace_back(new ListUI());
    ListUI* l = list_store_.back().get();
    l->attr = attr;
    l->kind = kind;
    l->panel = panel(GetParent(tabs));
    l->lv = mk(WC_LISTVIEWW, "", LVS_REPORT | LVS_SHOWSELALWAYS | LVS_OWNERDATA | WS_TABSTOP, l->panel, nullptr,
               WS_EX_CLIENTEDGE);
    ListView_SetExtendedListViewStyle(l->lv, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
    struct Col {
        const char* key;
        int w;
    };
    vector<Col> cols = kind == "list" ? vector<Col>{{"col_num", 45}, {"col_name", 150}, {"col_range", 115},
                                                    {"col_results", 95}, {"col_repeat", 65}, {"col_link", 300}}
                     : kind == "repeat" ? vector<Col>{{"col_num", 45}, {"col_name", 140}, {"col_range", 110},
                                                      {"col_results", 90}, {"col_repeat", 65}, {"col_next", 105},
                                                      {"col_link", 230}}
                                        : vector<Col>{{"col_num", 45}, {"col_name", 150}, {"col_range", 115},
                                                      {"col_results", 95}, {"col_date", 85}, {"col_link", 260}};
    for (size_t i = 0; i < cols.size(); i++) {
        std::wstring t = W(T(cols[i].key));
        LVCOLUMNW c{};
        c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
        c.fmt = (i == 0 || string(cols[i].key) == "col_results" || string(cols[i].key) == "col_repeat" ||
                 string(cols[i].key) == "col_date" || string(cols[i].key) == "col_next")
                    ? LVCFMT_CENTER
                    : LVCFMT_LEFT;
        if (i == 0) c.fmt = LVCFMT_LEFT;                   // the first column cannot be centred
        c.cx = S(cols[i].w);
        c.pszText = &t[0];
        ListView_InsertColumn(l->lv, (int)i, &c);
    }
    for (auto& b : buttons) {
        auto fn = b.second;
        l->buttons.push_back(button(l->panel, T(b.first), [l, fn] { fn(*l); }));
    }
    l->buttons.push_back(nullptr);                         // a gap
    for (auto where : {"up", "down", "top", "bottom"}) {
        string w = where;
        l->buttons.push_back(button(l->panel, T("move_" + w), [this, l, w] { move_sel(*l, w); }));
    }
    return l;
}

void Gui::browser_rows(HWND parent, bool scanner, vector<HWND>& row1, vector<vector<HWND>>& drivers, HWND& hint) {
    vector<string> names(std::begin(BROWSERS), std::end(BROWSERS));
    row1.push_back(label(parent, T("browser")));
    row1.push_back(combo(parent, names, scanner ? core_->scan_browser : core_->split_browser, [this, scanner](const string& b) {
        (scanner ? core_->scan_browser : core_->split_browser) = b;
        core_->settings_changed();
        update_status();
    }));
    row1.push_back(label(parent, T("opera_loc")));
    HWND oe = edit(parent, core_->opera_path, [this](const string& v) {
        core_->opera_path = v;
        for (HWND e : opera_edits_) set_text(e, v);
        core_->settings_changed();
    });
    opera_edits_.push_back(oe);
    row1.push_back(oe);
    row1.push_back(button(parent, T("browse"), [this] {
        string p = open_file(T("select_opera"), L"Opera (opera.exe, launcher.exe)\0opera.exe;launcher.exe\0All files\0*.*\0");
        if (!p.empty()) {
            core_->opera_path = p;
            for (HWND e : opera_edits_) set_text(e, p);
            core_->settings_changed();
        }
    }));
    for (const char* kind0 : {"chrome", "edge"}) {
        string kind = kind0;
        vector<HWND> row;
        row.push_back(label(parent, T(kind == "edge" ? "edgedriver_loc" : "chromedriver_loc")));
        HWND e = edit(parent, kind == "edge" ? core_->edgedriver : core_->chromedriver, [this, kind](const string& v) {
            (kind == "edge" ? core_->edgedriver : core_->chromedriver) = v;
            for (HWND x : (kind == "edge" ? edge_edits_ : chrome_edits_)) set_text(x, v);
            core_->settings_changed();
        });
        (kind == "edge" ? edge_edits_ : chrome_edits_).push_back(e);
        row.push_back(e);
        row.push_back(button(parent, T("browse"), [this, kind] { browse_driver(kind); }));
        row.push_back(button(parent, T("drv_btn"), [this, kind] { core_->get_driver(kind); }));
        drivers.push_back(row);
    }
    hint = label(parent, T("driver_hint"), true);
}

void Gui::build_ui() {
    building_ = true;
    // remember the open tabs, then start again
    if (tabs_) tab_sel_ = TabCtrl_GetCurSel(tabs_);
    if (scan_tabs_) scan_tab_sel_ = TabCtrl_GetCurSel(scan_tabs_);
    if (split_tabs_) split_tab_sel_ = TabCtrl_GetCurSel(split_tabs_);
    for (HWND c = GetWindow(main_, GW_CHILD); c; c = GetWindow(main_, GW_CHILD)) DestroyWindow(c);
    handlers_.clear();
    gray_.clear();
    list_store_.clear();
    scan_row2_.clear(); scan_row3_.clear(); scan_browser_row_.clear(); fill_row_.clear(); scan_driver_rows_.clear();
    split_opts_.clear(); split_prices_.clear(); split_browser_row_.clear(); split_btns_.clear(); split_driver_rows_.clear();
    phone_row1_.clear(); phone_row2_.clear(); chrome_edits_.clear(); edge_edits_.clear(); opera_edits_.clear();
    scan_list_pages_.clear(); split_pages_.clear();
    SetWindowTextW(main_, L"EasyCentral Link Bot");

    env_lbl_ = label(main_, core_->env_text(), true);
    lang_lbl_ = label(main_, T("language"));
    lang_combo_ = combo(main_, {"English", u8"Türkçe"}, core_->lang == "tr" ? u8"Türkçe" : "English", [this](const string& v) {
        string code = v == "English" ? "en" : "tr";
        if (code != core_->lang) {
            core_->lang = code;
            core_->settings_changed();
            PostMessageW(main_, WM_APP + 1, 0, 0);       // rebuild after this message
        }
    });
    tabs_ = mk(WC_TABCONTROLW, "", WS_CLIPSIBLINGS | WS_TABSTOP, main_);
    for (auto key : {"tab_scan", "tab_split", "tab_phone"}) {
        std::wstring t = W(T(key));
        TCITEMW it{};
        it.mask = TCIF_TEXT;
        it.pszText = &t[0];
        TabCtrl_InsertItem(tabs_, TabCtrl_GetItemCount(tabs_), &it);
    }
    handlers_[GetDlgCtrlID(tabs_)] = nullptr;
    log_ = mk(L"EDIT", "", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL, main_, nullptr, WS_EX_CLIENTEDGE);
    SendMessageW(log_, EM_SETLIMITTEXT, 4 * 1024 * 1024, 0);

    // ---- Scanner ----
    scan_page_ = panel(main_);
    start_btn_ = button(scan_page_, T(core_->running ? "stop" : "start"), [this] {
        core_->toggle();
        update_status();
    });
    now_btn_ = button(scan_page_, T("run_now"), [this] { core_->start_job(); });
    login_scan_btn_ = button(scan_page_, T("login_btn", {{"browser", core_->scan_browser}}), [this] { core_->login_scanner(); });
    status_lbl_ = label(scan_page_, core_->scan_status_text());
    scan_row2_.push_back(label(scan_page_, T("interval_lbl")));
    HWND interval = edit(scan_page_, std::to_string(core_->interval_min), nullptr, ES_NUMBER);
    scan_row2_.push_back(interval);
    scan_row2_.push_back(button(scan_page_, T("apply"), [this, interval] {
        int m = (int)std::max(1LL, to_ll(get_text(interval), 30));
        set_text(interval, std::to_string(m));
        core_->apply_interval(m);
    }));
    scan_row2_.push_back(label(scan_page_, T("step_wait_lbl")));
    scan_row2_.push_back(edit(scan_page_, std::to_string(core_->step_wait), [this](const string& v) {
        core_->step_wait = (int)to_ll(v, 3);
        core_->settings_changed();
    }, ES_NUMBER));
    scan_row3_.push_back(label(scan_page_, T("filter_lbl")));
    scan_row3_.push_back(edit(scan_page_, core_->scan_filter, [this](const string& v) {
        core_->scan_filter = v;
        core_->settings_changed();
    }));
    browser_rows(scan_page_, true, scan_browser_row_, scan_driver_rows_, scan_hint_);
    fill_row_.push_back(button(scan_page_, T("fill_btn"), [this] { core_->start_fill(false); }));
    fill_row_.push_back(check(scan_page_, T("fill_chk"), core_->fill_on_start, [this](bool on) {
        core_->fill_on_start = on;
        core_->settings_changed();
    }));
    scan_tabs_ = mk(WC_TABCONTROLW, "", WS_CLIPSIBLINGS | WS_TABSTOP, scan_page_);
    for (auto key : {"tab_pending", "tab_done", "tab_repeat"}) {
        std::wstring t = W(T(key));
        TCITEMW it{};
        it.mask = TCIF_TEXT;
        it.pszText = &t[0];
        TabCtrl_InsertItem(scan_tabs_, TabCtrl_GetItemCount(scan_tabs_), &it);
    }
    lists_[0] = make_list(scan_tabs_, "pending", "list",
                          {{"add", [this](ListUI&) {
                                string text;
                                if (input(T("dlg_add_title"), T("dlg_add_prompt"), "", true, text) && !trim(text).empty())
                                    core_->add_text(text);
                            }},
                           {"import_excel", [this](ListUI&) { import_excel(); }},
                           {"revise", [this](ListUI& l) {
                                auto sel = selected(l);
                                if (sel.empty()) return info(T("revise"), T("select_first"));
                                string url = core_->pending[sel[0]].value("url", ""), out;
                                if (input(T("dlg_rev_title"), T("dlg_rev_prompt"), url, false, out)) core_->revise(sel[0], out);
                            }},
                           {"remove", [this](ListUI& l) { core_->remove_indices("pending", selected(l)); }},
                           {"clear_all", [this](ListUI&) {
                                if (!core_->pending.empty() && ask(T("clear_all"), T("clear_confirm"))) core_->clear("pending");
                            }}});
    lists_[1] = make_list(scan_tabs_, "done", "list",
                          {{"move_back", [this](ListUI& l) { core_->move_done_back(selected(l)); }},
                           {"to_repeat_sel", [this](ListUI& l) {
                                auto sel = selected(l);
                                if (sel.empty()) return info(T("to_repeat_sel"), T("select_first"));
                                core_->done_to_repeat(sel, false);
                            }},
                           {"to_repeat_all", [this](ListUI&) { core_->done_to_repeat({}, true); }},
                           {"remove", [this](ListUI& l) { core_->remove_indices("done", selected(l)); }},
                           {"clear_all", [this](ListUI&) {
                                if (!core_->done.empty() && ask(T("clear_all"), T("clear_confirm"))) core_->clear("done");
                            }}});
    lists_[2] = make_list(scan_tabs_, "repeat", "repeat",
                          {{"to_pending", [this](ListUI& l) {
                                auto sel = selected(l);
                                if (sel.empty()) return info(T("to_pending"), T("select_first"));
                                core_->move_repeat_to_pending(sel);
                            }},
                           {"remove", [this](ListUI& l) { core_->remove_indices("repeat", selected(l)); }},
                           {"clear_all", [this](ListUI&) {
                                if (!core_->repeat.empty() && ask(T("clear_all"), T("clear_confirm"))) core_->clear("repeat");
                            }}});
    {
        ListUI* r = lists_[2];                             // the Repeat tab's top row
        r->header = check(r->panel, T("repeat_chk"), core_->repeat_on, [this](bool on) {
            core_->repeat_on = on;
            core_->auto_transfer();
            core_->settings_changed();
            core_->refresh();
        });
        HWND days = edit(r->panel, std::to_string(core_->repeat_days), [this](const string& v) {
            long long d = to_ll(v, 0);
            if (d >= 1) {
                core_->repeat_days = (int)d;
                core_->settings_changed();
                core_->refresh();
            }
        }, ES_NUMBER);
        HWND lbl = label(r->panel, T("days"));
        SetPropW(r->header, L"days_edit", days);
        SetPropW(r->header, L"days_lbl", lbl);
    }
    scan_list_pages_ = {lists_[0]->panel, lists_[1]->panel, lists_[2]->panel};

    // ---- Price Splitter ----
    split_page_ = panel(main_);
    split_tabs_ = mk(WC_TABCONTROLW, "", WS_CLIPSIBLINGS | WS_TABSTOP, split_page_);
    for (auto key : {"tab_split_run", "tab_saved"}) {
        std::wstring t = W(T(key));
        TCITEMW it{};
        it.mask = TCIF_TEXT;
        it.pszText = &t[0];
        TabCtrl_InsertItem(split_tabs_, TabCtrl_GetItemCount(split_tabs_), &it);
    }
    split_run_ = panel(split_page_);
    split_link_lbl_ = label(split_run_, T("split_link"));
    split_url_ = edit(split_run_, core_->split_url, [this](const string& v) { core_->split_url = v; });
    split_opts_.push_back(label(split_run_, T("target")));
    split_opts_.push_back(edit(split_run_, std::to_string(core_->limit), [this](const string& v) { core_->limit = (int)to_ll(v, 9000); }, ES_NUMBER));
    split_opts_.push_back(label(split_run_, T("wait_lbl")));
    split_opts_.push_back(edit(split_run_, std::to_string(core_->wait_s), [this](const string& v) { core_->wait_s = (int)to_ll(v, 10); }, ES_NUMBER));
    split_prices_.push_back(label(split_run_, T("start_price")));
    split_prices_.push_back(edit(split_run_, core_->start_price, [this](const string& v) { core_->start_price = v; }));
    split_prices_.push_back(label(split_run_, T("min_price")));
    split_prices_.push_back(edit(split_run_, core_->min_price, [this](const string& v) {
        core_->min_price = v;
        core_->settings_changed();
    }));
    browser_rows(split_run_, false, split_browser_row_, split_driver_rows_, split_hint_);
    split_btns_.push_back(button(split_run_, T("start_split"), [this] {
        string err = core_->start_split();
        if (!err.empty()) info("Price Splitter", T(err));
    }));
    split_btns_.push_back(button(split_run_, T("stop"), [this] { core_->stop_split(); }));
    split_btns_.push_back(button(split_run_, T("login_btn", {{"browser", core_->split_browser}}), [this] {
        string err = core_->login_splitter();
        if (!err.empty()) info("Opera", T(err));
    }));
    split_status_ = label(split_run_, T(core_->splitting ? "st_working" : "st_idle"));
    rules_lbl_ = label(split_run_, T("rules"), true);
    lists_[3] = make_list(split_tabs_, "saved", "saved",
                          {{"copy_sel", [this](ListUI& l) {
                                string t = core_->copy_urls(selected(l), false);
                                if (t.empty()) return info(T("copy_sel"), T("nothing_copy"));
                                copy_text(t);
                            }},
                           {"copy_all", [this](ListUI&) {
                                string t = core_->copy_urls({}, true);
                                if (t.empty()) return info(T("copy_sel"), T("nothing_copy"));
                                copy_text(t);
                            }},
                           {"to_pending", [this](ListUI& l) { core_->move_saved_to_pending(selected(l)); }},
                           {"remove", [this](ListUI& l) { core_->remove_indices("saved", selected(l)); }},
                           {"clear_all", [this](ListUI&) {
                                if (!core_->saved.empty() && ask(T("clear_all"), T("clear_confirm"))) core_->clear("saved");
                            }}});
    SetParent(lists_[3]->panel, split_page_);
    split_pages_ = {split_run_, lists_[3]->panel};

    // ---- Phone ----
    phone_page_ = panel(main_);
    remote_chk_ = check(phone_page_, T("remote_chk"), core_->remote_on, [this](bool on) {
        core_->remote_toggle(on);
        SendMessageW(remote_chk_, BM_SETCHECK, core_->remote_on ? BST_CHECKED : BST_UNCHECKED, 0);
        set_text(remote_info_, core_->remote_info_text());
    });
    phone_row1_.push_back(label(phone_page_, T("remote_port")));
    remote_port_ = edit(phone_page_, std::to_string(core_->remote_port), nullptr, ES_NUMBER);
    phone_row1_.push_back(remote_port_);
    phone_row1_.push_back(label(phone_page_, T("remote_pin")));
    remote_pin_ = edit(phone_page_, core_->remote_pin, nullptr, ES_NUMBER);
    SendMessageW(remote_pin_, EM_SETLIMITTEXT, 6, 0);
    phone_row1_.push_back(remote_pin_);
    auto apply = [this] {
        string err = core_->remote_apply(get_text(remote_pin_), get_text(remote_port_));
        if (!err.empty()) {
            set_text(remote_pin_, core_->remote_pin);
            set_text(remote_port_, std::to_string(core_->remote_port));
            info(T("tab_phone"), T(err));
        }
        set_text(remote_info_, core_->remote_info_text());
    };
    phone_row1_.push_back(button(phone_page_, T("remote_apply"), apply));
    phone_row1_.push_back(button(phone_page_, T("remote_newpin"), [this, apply] {
        set_text(remote_pin_, new_pin());
        apply();
    }));
    phone_row2_.push_back(button(phone_page_, T("remote_ext_btn"), [this] { core_->remote_find_ext(); }));
    phone_row2_.push_back(button(phone_page_, T("remote_upnp_btn"), [this] { core_->remote_upnp(); }));
    remote_info_ = label(phone_page_, core_->remote_info_text());
    SendMessageW(remote_info_, WM_SETFONT, (WPARAM)bold_, TRUE);
    remote_help_ = label(phone_page_, T("remote_help"), true);

    TabCtrl_SetCurSel(tabs_, tab_sel_);
    TabCtrl_SetCurSel(scan_tabs_, scan_tab_sel_);
    TabCtrl_SetCurSel(split_tabs_, split_tab_sel_);
    for (auto& line : core_->log_lines) append_log(line);
    building_ = false;
    layout();
    refresh_lists();
    update_status();
}

// ---------------------------------------------------------------- layout
RECT Gui::page_rect(HWND tabs) {
    RECT r;
    GetClientRect(tabs, &r);
    TabCtrl_AdjustRect(tabs, FALSE, &r);
    MapWindowPoints(tabs, GetParent(tabs), (POINT*)&r, 2);
    return r;
}
void Gui::show_tab(HWND tabs, const vector<HWND>& pages) {
    int sel = TabCtrl_GetCurSel(tabs);
    for (int i = 0; i < (int)pages.size(); i++) ShowWindow(pages[i], i == sel ? SW_SHOW : SW_HIDE);
    for (int i = 0; i < (int)pages.size(); i++)
        if (i == sel) BringWindowToTop(pages[i]);
}

static int text_width(HWND h, HFONT f, const string& s) {
    HDC dc = GetDC(h);
    HGDIOBJ old = SelectObject(dc, f);
    SIZE sz{0, 0};
    std::wstring w = widen(s);
    GetTextExtentPoint32W(dc, w.c_str(), (int)w.size(), &sz);
    SelectObject(dc, old);
    ReleaseDC(h, dc);
    return sz.cx;
}

void Gui::layout() {
    if (!main_ || !tabs_) return;
    RECT rc;
    GetClientRect(main_, &rc);
    int W8 = S(8), rowh = line_h_ + S(10);
    int lw = text_width(main_, font_, T("language")) + S(6);
    MoveWindow(lang_combo_, rc.right - W8 - S(110), S(6), S(110), S(200), TRUE);
    MoveWindow(lang_lbl_, rc.right - W8 - S(110) - lw - S(4), S(10), lw, line_h_, TRUE);
    MoveWindow(env_lbl_, W8, S(10), std::max(10, (int)(rc.right - W8 * 3 - S(110) - lw)), line_h_, TRUE);
    int log_h = line_h_ * 8 + S(8);
    int top = S(6) + rowh;
    MoveWindow(tabs_, W8, top, rc.right - 2 * W8, rc.bottom - top - log_h - 2 * W8, TRUE);
    MoveWindow(log_, W8, rc.bottom - log_h - W8, rc.right - 2 * W8, log_h, TRUE);
    RECT pr = page_rect(tabs_);
    for (HWND p : {scan_page_, split_page_, phone_page_})
        MoveWindow(p, pr.left, pr.top, pr.right - pr.left, pr.bottom - pr.top, TRUE);
    show_tab(tabs_, {scan_page_, split_page_, phone_page_});
    layout_scanner();
    layout_splitter();
    layout_phone();
}

void Gui::layout_scanner() {
    RECT rc;
    GetClientRect(scan_page_, &rc);
    int pad = S(8), x, y = pad, h = line_h_ + S(10), gap = S(6);
    auto tw = [&](HWND hw) { return text_width(hw, font_, get_text(hw)) + S(24); };
    auto place = [&](HWND hw, int w, int hh = 0) {
        MoveWindow(hw, x, y + (hh ? 0 : 0), w, hh ? hh : h, TRUE);
        x += w + gap;
    };
    auto lbl = [&](HWND hw) {
        int w = text_width(hw, font_, get_text(hw)) + S(4);
        MoveWindow(hw, x, y + (h - line_h_) / 2, w, line_h_, TRUE);
        x += w + gap;
    };
    x = pad;
    place(start_btn_, tw(start_btn_));
    place(now_btn_, tw(now_btn_));
    place(login_scan_btn_, tw(login_scan_btn_));
    MoveWindow(status_lbl_, rc.right - pad - S(220), y + (h - line_h_) / 2, S(220), line_h_, TRUE);
    y += h + S(6);
    x = pad;
    lbl(scan_row2_[0]);
    place(scan_row2_[1], S(60));
    place(scan_row2_[2], tw(scan_row2_[2]));
    x += S(10);
    lbl(scan_row2_[3]);
    place(scan_row2_[4], S(50));
    y += h + S(4);
    x = pad;
    lbl(scan_row3_[0]);
    place(scan_row3_[1], S(220));
    y += h + S(4);
    // browser + Opera
    x = pad;
    lbl(scan_browser_row_[0]);
    place(scan_browser_row_[1], S(95), S(200));
    lbl(scan_browser_row_[2]);
    int bw = tw(scan_browser_row_[4]);
    place(scan_browser_row_[3], std::max(S(80), (int)rc.right - x - pad - bw - gap));
    place(scan_browser_row_[4], bw);
    y += h + S(4);
    for (auto& row : scan_driver_rows_) {
        x = pad;
        lbl(row[0]);
        int b1 = tw(row[2]), b2 = tw(row[3]);
        place(row[1], std::max(S(80), (int)rc.right - x - pad - b1 - b2 - 2 * gap));
        place(row[2], b1);
        place(row[3], b2);
        y += h + S(2);
    }
    MoveWindow(scan_hint_, pad, y, rc.right - 2 * pad, line_h_ * 2 + S(2), TRUE);
    y += line_h_ * 2 + S(6);
    x = pad;
    place(fill_row_[0], tw(fill_row_[0]));
    place(fill_row_[1], tw(fill_row_[1]) + S(10));
    y += h + S(4);
    MoveWindow(scan_tabs_, pad, y, rc.right - 2 * pad, std::max(S(60), (int)rc.bottom - y - pad), TRUE);
    RECT pr = page_rect(scan_tabs_);
    for (int i = 0; i < 3; i++) layout_list(*lists_[i], pr);
    show_tab(scan_tabs_, scan_list_pages_);
}

void Gui::layout_list(ListUI& l, RECT r) {
    MoveWindow(l.panel, r.left, r.top, r.right - r.left, r.bottom - r.top, TRUE);
    RECT rc;
    GetClientRect(l.panel, &rc);
    int pad = S(6), h = line_h_ + S(10), y = pad;
    if (l.header) {                                        // Repeat: [x] move ... after [ 7 ] days
        HWND days = (HWND)GetPropW(l.header, L"days_edit"), lbl = (HWND)GetPropW(l.header, L"days_lbl");
        int cw = text_width(l.header, font_, get_text(l.header)) + S(26);
        MoveWindow(l.header, pad, y, cw, h, TRUE);
        MoveWindow(days, pad + cw + S(6), y, S(55), h, TRUE);
        MoveWindow(lbl, pad + cw + S(67), y + (h - line_h_) / 2, S(80), line_h_, TRUE);
        y += h + S(6);
    }
    int bw = S(170);
    for (HWND b : l.buttons) if (b) bw = std::max(bw, text_width(b, font_, get_text(b)) + S(24));
    MoveWindow(l.lv, pad, y, std::max(S(50), (int)rc.right - 3 * pad - bw), rc.bottom - y - pad, TRUE);
    int by = y, bx = rc.right - pad - bw;
    for (HWND b : l.buttons) {
        if (!b) {
            by += S(10);
            continue;
        }
        MoveWindow(b, bx, by, bw, h, TRUE);
        by += h + S(4);
    }
}

void Gui::layout_splitter() {
    RECT rc;
    GetClientRect(split_page_, &rc);
    int pad = S(8);
    MoveWindow(split_tabs_, pad, pad, rc.right - 2 * pad, rc.bottom - 2 * pad, TRUE);
    RECT pr = page_rect(split_tabs_);
    MoveWindow(split_run_, pr.left, pr.top, pr.right - pr.left, pr.bottom - pr.top, TRUE);
    layout_list(*lists_[3], pr);
    show_tab(split_tabs_, split_pages_);
    GetClientRect(split_run_, &rc);
    pad = S(12);
    int x, y = pad, h = line_h_ + S(10), gap = S(6);
    auto tw = [&](HWND hw) { return text_width(hw, font_, get_text(hw)) + S(24); };
    auto place = [&](HWND hw, int w, int hh = 0) {
        MoveWindow(hw, x, y, w, hh ? hh : h, TRUE);
        x += w + gap;
    };
    auto lbl = [&](HWND hw) {
        int w = text_width(hw, font_, get_text(hw)) + S(4);
        MoveWindow(hw, x, y + (h - line_h_) / 2, w, line_h_, TRUE);
        x += w + gap;
    };
    MoveWindow(split_link_lbl_, pad, y, rc.right - 2 * pad, line_h_, TRUE);
    y += line_h_ + S(4);
    MoveWindow(split_url_, pad, y, rc.right - 2 * pad, h, TRUE);
    y += h + S(6);
    x = pad;
    lbl(split_opts_[0]);
    place(split_opts_[1], S(80));
    x += S(10);
    lbl(split_opts_[2]);
    place(split_opts_[3], S(50));
    y += h + S(6);
    x = pad;
    lbl(split_prices_[0]);
    place(split_prices_[1], S(80));
    x += S(10);
    lbl(split_prices_[2]);
    place(split_prices_[3], S(80));
    y += h + S(6);
    x = pad;
    lbl(split_browser_row_[0]);
    place(split_browser_row_[1], S(95), S(200));
    lbl(split_browser_row_[2]);
    int bw = tw(split_browser_row_[4]);
    place(split_browser_row_[3], std::max(S(80), (int)rc.right - x - pad - bw - gap));
    place(split_browser_row_[4], bw);
    y += h + S(4);
    for (auto& row : split_driver_rows_) {
        x = pad;
        lbl(row[0]);
        int b1 = tw(row[2]), b2 = tw(row[3]);
        place(row[1], std::max(S(80), (int)rc.right - x - pad - b1 - b2 - 2 * gap));
        place(row[2], b1);
        place(row[3], b2);
        y += h + S(2);
    }
    MoveWindow(split_hint_, pad, y, rc.right - 2 * pad, line_h_ * 2 + S(2), TRUE);
    y += line_h_ * 2 + S(8);
    x = pad;
    for (HWND b : split_btns_) place(b, tw(b));
    MoveWindow(split_status_, rc.right - pad - S(200), y + (h - line_h_) / 2, S(200), line_h_, TRUE);
    y += h + S(10);
    MoveWindow(rules_lbl_, pad, y, rc.right - 2 * pad, std::max(S(20), (int)rc.bottom - y - pad), TRUE);
}

void Gui::layout_phone() {
    RECT rc;
    GetClientRect(phone_page_, &rc);
    int pad = S(12), x = pad, y = pad, h = line_h_ + S(10), gap = S(6);
    auto tw = [&](HWND hw) { return text_width(hw, font_, get_text(hw)) + S(24); };
    MoveWindow(remote_chk_, pad, y, tw(remote_chk_) + S(10), h, TRUE);
    y += h + S(8);
    for (size_t i = 0; i < phone_row1_.size(); i++) {
        HWND hw = phone_row1_[i];
        wchar_t cls[32];
        GetClassNameW(hw, cls, 32);
        int w = lstrcmpiW(cls, L"Edit") == 0 ? S(70) : (lstrcmpiW(cls, L"Static") == 0 ? text_width(hw, font_, get_text(hw)) + S(4) : tw(hw));
        int yy = lstrcmpiW(cls, L"Static") == 0 ? y + (h - line_h_) / 2 : y;
        MoveWindow(hw, x, yy, w, lstrcmpiW(cls, L"Static") == 0 ? line_h_ : h, TRUE);
        x += w + gap + (lstrcmpiW(cls, L"Edit") == 0 ? S(8) : 0);
    }
    y += h + S(8);
    x = pad;
    for (HWND b : phone_row2_) {
        MoveWindow(b, x, y, tw(b), h, TRUE);
        x += tw(b) + gap;
    }
    y += h + S(12);
    MoveWindow(remote_info_, pad, y, rc.right - 2 * pad, line_h_ * 3 + S(12), TRUE);
    y += line_h_ * 3 + S(20);
    MoveWindow(remote_help_, pad, y, rc.right - 2 * pad, std::max(S(20), (int)rc.bottom - y - pad), TRUE);
}

// ---------------------------------------------------------------- lists
void Gui::refresh_list(ListUI& l) {
    auto* items = core_->list(l.attr);
    std::set<std::wstring> chosen;                        // keep the selection (by link) when the list changes
    for (int i : selected(l))
        if (i < (int)l.rows.size() && !l.rows[i].empty()) chosen.insert(l.rows[i].back());
    l.rows.clear();
    for (int i = 0; i < (int)items->size(); i++) {
        vector<std::wstring> r;
        for (auto& v : core_->row_values(i + 1, (*items)[i], l.kind)) r.push_back(W(v));
        l.rows.push_back(r);
    }
    ListView_SetItemCountEx(l.lv, (int)l.rows.size(), LVSICF_NOSCROLL | LVSICF_NOINVALIDATEALL);
    ListView_SetItemState(l.lv, -1, 0, LVIS_SELECTED);
    for (int i = 0; i < (int)l.rows.size() && !chosen.empty(); i++)
        if (chosen.count(l.rows[i].back())) ListView_SetItemState(l.lv, i, LVIS_SELECTED, LVIS_SELECTED);
    InvalidateRect(l.lv, nullptr, FALSE);
}
void Gui::refresh_lists() {
    for (auto* l : lists_) if (l) refresh_list(*l);
}
vector<int> Gui::selected(ListUI& l) {
    vector<int> out;
    int i = -1;
    while ((i = ListView_GetNextItem(l.lv, i, LVNI_SELECTED)) >= 0) out.push_back(i);
    return out;
}
void Gui::select_rows(ListUI& l, const vector<int>& idx) {
    ListView_SetItemState(l.lv, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    for (int i : idx) ListView_SetItemState(l.lv, i, LVIS_SELECTED, LVIS_SELECTED);
    if (!idx.empty()) ListView_SetItemState(l.lv, idx[0], LVIS_FOCUSED, LVIS_FOCUSED);
}
void Gui::move_sel(ListUI& l, const string& where) {
    auto idx = selected(l);
    if (idx.empty()) return info(T("move_" + where), T("select_first"));
    auto out = core_->move_rows(l.attr, idx, where);
    if (out == idx) return;                                // already at the top / bottom
    select_rows(l, out);
    ListView_EnsureVisible(l.lv, (where == "up" || where == "top") ? out.front() : out.back(), FALSE);
    SetFocus(l.lv);
}

void Gui::update_status() {
    if (!start_btn_) return;
    set_text(start_btn_, T(core_->running ? "stop" : "start"));
    set_text(status_lbl_, core_->scan_status_text());
    set_text(login_scan_btn_, T("login_btn", {{"browser", core_->scan_browser}}));
    if (split_btns_.size() > 2) set_text(split_btns_[2], T("login_btn", {{"browser", core_->split_browser}}));
    set_text(split_status_, T(core_->splitting ? "st_working" : "st_idle"));
    SendMessageW(remote_chk_, BM_SETCHECK, core_->remote_on ? BST_CHECKED : BST_UNCHECKED, 0);
}

// ---------------------------------------------------------------- small dialogs
void Gui::info(const string& title, const string& text, UINT icon) {
    MessageBoxW(main_, W(text).c_str(), W(title).c_str(), MB_OK | icon);
}
bool Gui::ask(const string& title, const string& text) {
    return MessageBoxW(main_, W(text).c_str(), W(title).c_str(), MB_YESNO | MB_ICONQUESTION) == IDYES;
}
bool Gui::input(const string& title, const string& prompt, const string& initial, bool multiline, string& out) {
    InputState st;
    int w = S(560), h = multiline ? S(360) : S(170);
    RECT pr;
    GetWindowRect(main_, &pr);
    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, INPUT_CLASS, W(title).c_str(),
                               WS_POPUP | WS_CAPTION | WS_SYSMENU, pr.left + (pr.right - pr.left - w) / 2,
                               pr.top + (pr.bottom - pr.top - h) / 2, w, h, main_, nullptr, inst_, nullptr);
    SetWindowLongPtrW(dlg, GWLP_USERDATA, (LONG_PTR)&st);
    RECT rc;
    GetClientRect(dlg, &rc);
    int pad = S(10), bh = line_h_ + S(12);
    auto child = [&](const wchar_t* cls, const string& text, DWORD style, int x, int y, int cw, int ch, int id, DWORD ex = 0) {
        HWND c = CreateWindowExW(ex, cls, W(text).c_str(), style | WS_CHILD | WS_VISIBLE, x, y, cw, ch, dlg,
                                 (HMENU)(INT_PTR)id, inst_, nullptr);
        SendMessageW(c, WM_SETFONT, (WPARAM)font_, TRUE);
        return c;
    };
    child(L"STATIC", prompt, SS_LEFT, pad, pad, rc.right - 2 * pad, line_h_ + S(4), -1);
    int ey = pad + line_h_ + S(8);
    int eh = rc.bottom - ey - bh - 2 * pad;
    DWORD es = WS_TABSTOP | (multiline ? (ES_MULTILINE | ES_AUTOVSCROLL | WS_VSCROLL | ES_WANTRETURN) : ES_AUTOHSCROLL);
    st.edit = child(L"EDIT", initial, es, pad, ey, rc.right - 2 * pad, multiline ? eh : bh, 100, WS_EX_CLIENTEDGE);
    child(L"BUTTON", "OK", BS_DEFPUSHBUTTON | WS_TABSTOP, rc.right - pad - S(190), rc.bottom - pad - bh, S(90), bh, IDOK);
    child(L"BUTTON", g_lang == "tr" ? u8"İptal" : "Cancel", BS_PUSHBUTTON | WS_TABSTOP, rc.right - pad - S(90),
          rc.bottom - pad - bh, S(90), bh, IDCANCEL);
    EnableWindow(main_, FALSE);
    ShowWindow(dlg, SW_SHOW);
    SetFocus(st.edit);
    SendMessageW(st.edit, EM_SETSEL, 0, -1);
    MSG msg;
    while (!st.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            st.done = true;
            continue;
        }
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    EnableWindow(main_, TRUE);
    DestroyWindow(dlg);
    SetForegroundWindow(main_);
    if (!st.ok) return false;
    out = st.result;
    return true;
}
string Gui::open_file(const string& title, const wchar_t* filter) {
    wchar_t buf[4096] = L"";
    std::wstring t = W(title);
    OPENFILENAMEW of{};
    of.lStructSize = sizeof of;
    of.hwndOwner = main_;
    of.lpstrFilter = filter;
    of.lpstrFile = buf;
    of.nMaxFile = 4096;
    of.lpstrTitle = t.c_str();
    of.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    return GetOpenFileNameW(&of) ? U(buf) : string();
}
void Gui::copy_text(const string& text) {
    std::wstring w = W(text);
    if (!OpenClipboard(main_)) return;
    EmptyClipboard();
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t));
    if (mem) {
        memcpy(GlobalLock(mem), w.c_str(), (w.size() + 1) * sizeof(wchar_t));
        GlobalUnlock(mem);
        SetClipboardData(CF_UNICODETEXT, mem);
    }
    CloseClipboard();
}
void Gui::append_log(const string& line) {
    if (!log_) return;
    int len = GetWindowTextLengthW(log_);
    if (len > 3 * 1024 * 1024) {                           // keep the box from growing for ever
        SendMessageW(log_, EM_SETSEL, 0, len / 2);
        SendMessageW(log_, EM_REPLACESEL, FALSE, (LPARAM)L"");
        len = GetWindowTextLengthW(log_);
    }
    std::wstring w = W((len ? "\r\n" : "") + line);
    SendMessageW(log_, EM_SETSEL, len, len);
    SendMessageW(log_, EM_REPLACESEL, FALSE, (LPARAM)w.c_str());
    SendMessageW(log_, EM_SCROLLCARET, 0, 0);
}
void Gui::browse_driver(const string& kind) {
    string path = open_file(T("select_driver"), L"Driver / zip / nupkg\0*.exe;*.zip;*.nupkg\0All files\0*.*\0");
    if (path.empty()) return;
    string low = lower_ascii(path);
    if (ends_with(low, ".zip") || ends_with(low, ".nupkg")) {   // a downloaded package: take the driver out
        core_->get_driver(kind, path);
    } else if (!is_driver_file(path)) {                    // for example msedge.exe: not a driver
        info(T("select_driver"), T("err_not_driver", {{"path", path}}), MB_ICONWARNING);
    } else {
        (kind == "edge" ? core_->edgedriver : core_->chromedriver) = path;
        for (HWND e : (kind == "edge" ? edge_edits_ : chrome_edits_)) set_text(e, path);
        core_->settings_changed();
    }
}
void Gui::import_excel() {
    string path = open_file(T("choose_excel"), L"Excel\0*.xlsx;*.xlsm\0All files\0*.*\0");
    if (path.empty()) return;
    vector<string> found;
    try {
        found = xlsx_links(path);
    } catch (const std::exception& e) {
        return info("Error", T("err_open_file", {{"e", e.what()}}), MB_ICONERROR);
    }
    auto r = core_->import_urls(found);
    info(T("import_title"), T("import_done", {{"added", std::to_string(r.first)}, {"skipped", std::to_string(r.second)}}));
}

// ---------------------------------------------------------------- messages
LRESULT Gui::notify(NMHDR* n) {
    if (n->code == TCN_SELCHANGE) {
        if (n->hwndFrom == tabs_) show_tab(tabs_, {scan_page_, split_page_, phone_page_});
        else if (n->hwndFrom == scan_tabs_) show_tab(scan_tabs_, scan_list_pages_);
        else if (n->hwndFrom == split_tabs_) show_tab(split_tabs_, split_pages_);
        return 0;
    }
    if (n->code == LVN_GETDISPINFOW) {
        auto* di = (NMLVDISPINFOW*)n;
        for (auto* l : lists_) {
            if (!l || l->lv != n->hwndFrom) continue;
            int row = di->item.iItem, col = di->item.iSubItem;
            if ((di->item.mask & LVIF_TEXT) && row >= 0 && row < (int)l->rows.size() && col < (int)l->rows[row].size())
                lstrcpynW(di->item.pszText, l->rows[row][col].c_str(), di->item.cchTextMax);
        }
        return 0;
    }
    return 0;
}

LRESULT Gui::handle(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_COMMAND: {
        auto it = handlers_.find(LOWORD(w));
        if (it != handlers_.end() && it->second) it->second(HIWORD(w));
        return 0;
    }
    case WM_NOTIFY:
        return notify((NMHDR*)l);
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)w;
        if ((HWND)l == log_) {                             // the read-only log box: a normal white box
            SetBkColor(dc, GetSysColor(COLOR_WINDOW));
            return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
        }
        SetBkMode(dc, TRANSPARENT);
        if (gray_.count((HWND)l)) SetTextColor(dc, RGB(0x55, 0x55, 0x55));
        if ((HWND)l == env_lbl_ || (HWND)l == lang_lbl_) return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
        return (LRESULT)panel_brush_;
    }
    }
    return DefWindowProcW(h, m, w, l);
}

LRESULT CALLBACK Gui::PanelProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (g_gui && (m == WM_COMMAND || m == WM_NOTIFY || m == WM_CTLCOLORSTATIC)) return g_gui->handle(h, m, w, l);
    if (m == WM_ERASEBKGND && g_gui) {
        RECT rc;
        GetClientRect(h, &rc);
        FillRect((HDC)w, &rc, g_gui->panel_brush_);
        return 1;
    }
    return DefWindowProcW(h, m, w, l);
}

LRESULT CALLBACK Gui::MainProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    Gui* g = g_gui;
    if (!g) return DefWindowProcW(h, m, w, l);
    switch (m) {
    case WM_SIZE:
        g->layout();
        return 0;
    case WM_GETMINMAXINFO: {
        auto* mm = (MINMAXINFO*)l;
        mm->ptMinTrackSize.x = g->S(760);
        mm->ptMinTrackSize.y = g->S(600);
        return 0;
    }
    case WM_TIMER:
        if (w == TIMER_PUMP) {
            g->core_->remote_pump();
        } else if (w == TIMER_TICK) {
            g->core_->tick();
            g->update_status();
            if (++g->ticks_ % 6 == 0 && g->remote_info_) set_text(g->remote_info_, g->core_->remote_info_text());
        }
        return 0;
    case WM_APP + 1:                                       // the language changed
        g->build_ui();
        return 0;
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        KillTimer(h, TIMER_TICK);
        KillTimer(h, TIMER_PUMP);
        PostQuitMessage(0);
        return 0;
    case WM_COMMAND:
    case WM_NOTIFY:
    case WM_CTLCOLORSTATIC:
        return g->handle(h, m, w, l);
    }
    return DefWindowProcW(h, m, w, l);
}

int Gui::run() {
    typedef BOOL(WINAPI * DpiFn)();
    if (auto fn = (DpiFn)(void*)GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDPIAware")) fn();
    INITCOMMONCONTROLSEX icc{sizeof icc, ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    net_init();
    HDC dc = GetDC(nullptr);
    dpi_ = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(nullptr, dc);
    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof ncm;
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    font_ = CreateFontIndirectW(&ncm.lfMessageFont);
    LOGFONTW bl = ncm.lfMessageFont;
    bl.lfWeight = FW_BOLD;
    bold_ = CreateFontIndirectW(&bl);
    {
        HDC d = GetDC(nullptr);
        HGDIOBJ old = SelectObject(d, font_);
        TEXTMETRICW tm;
        GetTextMetricsW(d, &tm);
        line_h_ = tm.tmHeight + tm.tmExternalLeading;
        SelectObject(d, old);
        ReleaseDC(nullptr, d);
    }
    panel_brush_ = GetSysColorBrush(COLOR_WINDOW);
    HICON icon = LoadIconW(inst_, MAKEINTRESOURCEW(1));
    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = MainProc;
    wc.hInstance = inst_;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    wc.lpszClassName = MAIN_CLASS;
    wc.hIcon = icon;
    wc.hIconSm = icon;
    RegisterClassExW(&wc);
    WNDCLASSEXW pc{sizeof pc};
    pc.lpfnWndProc = PanelProc;
    pc.hInstance = inst_;
    pc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    pc.lpszClassName = PANEL_CLASS;
    RegisterClassExW(&pc);
    WNDCLASSEXW ic{sizeof ic};
    ic.lpfnWndProc = InputProc;
    ic.hInstance = inst_;
    ic.hCursor = LoadCursor(nullptr, IDC_ARROW);
    ic.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    ic.lpszClassName = INPUT_CLASS;
    ic.hIcon = icon;
    RegisterClassExW(&ic);

    core_.reset(new Core());
    g_lang = core_->lang;
    g_gui = this;
    main_ = CreateWindowExW(WS_EX_CONTROLPARENT, MAIN_CLASS, L"EasyCentral Link Bot", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                            CW_USEDEFAULT, CW_USEDEFAULT, S(1000), S(800), nullptr, nullptr, inst_, nullptr);
    core_->on_log = [this](const string& line) { append_log(line); };
    core_->on_lists = [this] { refresh_lists(); };
    core_->on_status = [this] { update_status(); };
    core_->on_driver = [this](const string& kind, const string& path) {
        for (HWND e : (kind == "edge" ? edge_edits_ : chrome_edits_)) set_text(e, path);
    };
    build_ui();
    ShowWindow(main_, SW_SHOWNORMAL);
    UpdateWindow(main_);
    core_->startup();
    update_status();
    SetTimer(main_, TIMER_TICK, 500, nullptr);
    SetTimer(main_, TIMER_PUMP, 150, nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_SYSKEYDOWN && (msg.wParam == VK_UP || msg.wParam == VK_DOWN)) {   // Alt+Up / Alt+Down
            bool done = false;
            for (auto* l : lists_)
                if (l && msg.hwnd == l->lv) {
                    move_sel(*l, msg.wParam == VK_UP ? "up" : "down");
                    done = true;
                }
            if (done) continue;
        }
        if (msg.message == WM_KEYDOWN && msg.wParam == 'A' && (GetKeyState(VK_CONTROL) & 0x8000)) {   // Ctrl+A
            bool done = false;
            for (auto* l : lists_)
                if (l && msg.hwnd == l->lv) {
                    ListView_SetItemState(l->lv, -1, LVIS_SELECTED, LVIS_SELECTED);
                    done = true;
                }
            if (done) continue;
        }
        if (!IsDialogMessageW(main_, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    core_->save();
    core_->remote_stop(true);                              // stops the phone server
    kill_live_drivers();
    ExitProcess(0);                                        // background work (a scan, a download) just ends
    return 0;
}

// ---------------------------------------------------------------- the console command (--get-driver)
static void console_say(const string& s) {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    std::wstring w = widen(s + "\n");
    DWORD n = 0;
    if (!WriteConsoleW(out, w.c_str(), (DWORD)w.size(), &n, nullptr)) {   // redirected to a file
        string u = s + "\r\n";
        WriteFile(out, u.data(), (DWORD)u.size(), &n, nullptr);
    }
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    int argc = 0;
    LPWSTR* argvw = CommandLineToArgvW(GetCommandLineW(), &argc);
    vector<string> args;
    for (int i = 1; i < argc; i++) args.push_back(narrow(argvw[i]));
    LocalFree(argvw);
    bool cli = false;
    for (auto& a : args) cli = cli || a == "--get-driver";
    if (cli) {                                             // EasyCentralBot.exe --get-driver edge
        if (!AttachConsole(ATTACH_PARENT_PROCESS)) AllocConsole();
        net_init();
        string text, lang;
        if (read_file(path_join(app_dir(), "links.json"), text)) {
            try {
                lang = json::parse(text).value("lang", "");
            } catch (...) {
            }
        }
        if (lang.empty()) lang = PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_TURKISH ? "tr" : "en";
        g_lang = lang == "tr" ? "tr" : "en";
        return driver_cli(args, console_say);
    }
    Gui gui(inst);
    return gui.run();
}
