#include "scanner.h"

#include "gen_strings.h"

namespace ec {

string TARGET_URL = "https://app.easycentral.com/product-search/new";
const char* FILTER_PLACEHOLDER = u8"Lütfen Filtre Seç";
const char* FILTER_HEADING = u8"Kayıtlı Tarama Ayarlarınız";
const char* DEFAULT_SAVED_SETTING = "Otomasyon Filtresi";
static const json FILTER_AVOID = json::array({u8"taramayı başlat", "amazon url"});
static const json FILTER_AVOID_EXACT = json::array({"sil", u8"kaldır", "temizle", "kaydet", "delete", "remove", "clear", "save"});

static bool truthy(const json& j, const char* k) {
    if (!j.is_object() || !j.contains(k)) return false;
    const json& v = j[k];
    if (v.is_boolean()) return v.get<bool>();
    if (v.is_number()) return v.get<double>() != 0;
    if (v.is_string()) return !v.get<string>().empty();
    return !v.is_null();
}

// Saves what the page looks like around the box. Returns {path, short text}.
static std::pair<string, string> filter_dump(Page& page, const string& heading, const string& placeholder) {
    try {
        json d = page.run_js(FILTER_JS, {{"op", "dump"}, {"heading", heading}, {"placeholder", placeholder}, {"option", ""}});
        if (!d.is_object()) d = json::object();
        string path = path_join(app_dir(), "filter_debug.txt");
        string text = "placeholder found: " + string(truthy(d, "placeholder") ? "True" : "False") +
                      "\nheading found: " + string(truthy(d, "heading") ? "True" : "False") + "\n\n--- visible text ---\n" +
                      d.value("text", "") + "\n\n--- html ---\n" + d.value("html", "") + "\n";
        write_file(path, text);
        return {path, d.value("seen", "")};
    } catch (...) {
        return {"", ""};
    }
}

void select_saved_setting(Page& page, const string& option, long long wait_ms, double timeout_s) {
    const string heading = FILTER_HEADING, placeholder = FILTER_PLACEHOLDER;
    json base = {{"heading", heading}, {"placeholder", placeholder}, {"option", option},
                 {"avoid", FILTER_AVOID}, {"avoid_exact", FILTER_AVOID_EXACT}};
    const string OPT = "[data-ecbot='opt']", CTRL = "[data-ecbot='ctrl']";
    long long pause = std::max<long long>(wait_ms, 800);   // let the page update before looking again
    vector<string> problems;                               // what went wrong on the way (for the message)

    auto js = [&](const string& op, json extra = json::object()) {
        json d = base;
        d["op"] = op;
        for (auto it = extra.begin(); it != extra.end(); ++it) d[it.key()] = it.value();
        json r = page.run_js(FILTER_JS, d);
        return r.is_object() ? r : json::object();
    };
    auto click = [&](const string& css, bool real) {       // real mouse click when visible, else inside the page
        if (real) {
            try {
                page.click_css(css, 3000);
                return;
            } catch (const std::exception& e) {
                problems.push_back(utf8_truncate(collapse_ws(e.what()), 110));
            }
        }
        page.run_js(CLICK_JS, {{"sel", css}, {"item", false}});
    };

    double deadline = now() + timeout_s;
    int attempts = 0, ctl_index = 0;
    bool done = false;
    json st = json::object();
    string why = "box";
    while (now() < deadline && !done) {
        st = js("probe");
        if (!(truthy(st, "placeholder") || truthy(st, "heading") || truthy(st, "native") || truthy(st, "found"))) {
            page.wait(500);                                // nothing there yet: the page is still loading
            continue;
        }
        why = "option";
        if (truthy(st, "native")) {                        // a normal <select>
            if (truthy(st, "selected")) {
                done = true;
            } else {
                js("set_native");
                page.wait(300);
            }
        } else if (truthy(st, "found")) {                  // the setting is in the page: selected or not?
            bool sel;
            if (!st.contains("selected") || st["selected"].is_null())
                sel = attempts >= 1 && !truthy(st, "placeholder");   // no sign to read: trust our own click once
            else
                sel = truthy(st, "selected");
            if (sel) {
                done = true;
            } else if (attempts >= 3) {
                why = "click";
                break;
            } else {
                if (attempts == 0) {                       // 1st: the entry itself
                    click(OPT, truthy(st, "visible"));
                } else if (attempts == 1) {                // 2nd: the list entry around it, clicked in the page
                    page.run_js(CLICK_JS, {{"sel", OPT}, {"item", true}});
                } else if (truthy(js("tag_control", {{"skip", 0}}), "control")) {   // 3rd: open the box first
                    click(CTRL, true);
                    page.wait(700);
                    if (truthy(js("probe"), "visible")) click(OPT, true);
                    else page.run_js(CLICK_JS, {{"sel", OPT}, {"item", false}});
                }
                attempts++;
                page.wait(pause);
            }
        } else if (ctl_index < 3) {                        // not in the page yet: open the box
            if (!truthy(js("tag_control", {{"skip", ctl_index}}), "control")) {
                page.wait(700);                            // the box may still be loading
                continue;
            }
            click(CTRL, true);
            bool shown = false;
            for (int i = 0; i < 6 && !shown; i++) {
                page.wait(500);
                shown = truthy(js("probe"), "found");
            }
            if (!shown) ctl_index++;                       // that did not open it: try the next element
        } else {
            break;
        }
    }
    if (!done) {
        auto dump = filter_dump(page, heading, placeholder);
        if (!(truthy(st, "placeholder") || truthy(st, "heading") || truthy(st, "found") || truthy(st, "native")))
            why = "box";
        string reason = T("filter_why_" + why) + (problems.empty() ? "" : " [" + problems.back() + "]");
        throw FilterError(T("err_filter", {{"option", option}, {"why", reason},
                                           {"seen", dump.second.empty() ? "-" : dump.second},
                                           {"path", dump.first.empty() ? "-" : dump.first}}));
    }
    page.mouse_away();                                     // a list that opens on hover must not stay open
    if (truthy(st, "menu")) {                              // close an open list so it does not cover the page
        js("close");
        page.wait(300);
        if (truthy(js("probe"), "menu") && !heading.empty()) {
            try {
                page.click_css("[data-ecbot='head']", 3000);
            } catch (...) {
            }
        }
    }
    page.wait(300);
}

// What the page shows right now: {short text with the boxes and buttons, path of a detail file}
static std::pair<string, string> page_report(Page& page) {
    try {
        json d = page.run_js(REPORT_JS);
        if (!d.is_object()) return {"", ""};
        auto lines = [&](const char* k) {
            vector<string> v;
            if (d.contains(k) && d[k].is_array())
                for (auto& x : d[k]) v.push_back(x.is_string() ? x.get<string>() : x.dump());
            return v;
        };
        auto pylist = [](const vector<string>& v) {       // the same look as the Python version: ['a', 'b']
            vector<string> q;
            for (auto& x : v) q.push_back("'" + x + "'");
            return "[" + join(q, ", ") + "]";
        };
        string path = path_join(app_dir(), "page_debug.txt");
        write_file(path, "page: " + d.value("url", "") + "\n\n--- boxes ---\n" + join(lines("boxes"), "\n") +
                             "\n\n--- buttons ---\n" + join(lines("buttons"), "\n") + "\n\n--- visible text ---\n" +
                             d.value("text", "") + "\n\n--- html ---\n" + d.value("html", "") + "\n");
        string shrt = "page " + d.value("url", "?") + " | boxes " + pylist(lines("boxes")) + " | buttons " +
                      pylist(lines("buttons"));
        return {shrt, path};
    } catch (...) {
        return {"", ""};
    }
}

// Runs one page step; when it fails, the error also says what the page shows.
template <class F>
static void run_step(Page& page, const string& step, F fn) {
    try {
        fn();
    } catch (const FilterError&) {
        throw;
    } catch (const std::exception& e) {
        auto rep = page_report(page);
        if (rep.first.empty()) throw;
        throw Error(T("err_step", {{"step", step}, {"e", utf8_truncate(short_err(e.what()), 160)},
                                   {"seen", utf8_truncate(rep.first, 300)},
                                   {"path", rep.second.empty() ? "-" : rep.second}}));
    }
}

void scan_link(const string& url, const string& browser, const string& opera_path, int step_wait,
               const string& saved_setting) {
    long long pause = (long long)std::max(0, step_wait) * 1000;
    sleep_ms(pause);                                       // wait when the process starts
    Session sess("scanner", browser, opera_path);
    Page& page = sess.page();
    page.wait(pause);
    page.go(TARGET_URL);                                                              // step 1
    page.wait(pause);
    run_step(page, "URL ile Tara", [&] { page.click_text("URL ile Tara", 30000); });  // step 2
    page.wait(pause);
    string setting = trim(saved_setting);
    if (!setting.empty()) {                                                           // step 2b
        run_step(page, setting, [&] { select_saved_setting(page, setting, pause); });
        page.wait(pause);
    }
    run_step(page, "Amazon url adresini ekle",
             [&] { page.fill_placeholder("Amazon url adresini ekle", url, 30000); });  // step 3
    page.wait(pause);
    run_step(page, u8"Taramayı Başlat", [&] { page.click_button(u8"Taramayı Başlat", 30000); });   // step 4
    page.wait(std::max<long long>(pause, 5000));           // let the scan be submitted
    page.close();                                                                     // step 5
    sess.close();
}

void open_login_window(const string& feature, const string& browser, const string& opera_path, const string& url) {
    Session sess(feature, browser, opera_path);
    sess.page().go(url);
    sess.wait_closed();
}

}  // namespace ec
