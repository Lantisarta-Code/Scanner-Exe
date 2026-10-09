#include "core.h"

#include <algorithm>
#include <regex>
#include <set>
#include <thread>

#include "drivers.h"
#include "scanner.h"

#ifndef _WIN32
#include <unistd.h>
#endif

namespace ec {

static string data_file() { return path_join(app_dir(), "links.json"); }

static long long jint(const json& d, const char* k, long long def) {
    if (!d.is_object() || !d.contains(k)) return def;
    const json& v = d[k];
    if (v.is_number()) return (long long)v.get<double>();
    if (v.is_string()) return to_ll(v.get<string>(), def);
    return def;
}
static bool jbool(const json& d, const char* k, bool def) {
    if (!d.is_object() || !d.contains(k)) return def;
    const json& v = d[k];
    if (v.is_boolean()) return v.get<bool>();
    if (v.is_number()) return v.get<double>() != 0;
    return def;
}
static string jstr(const json& d, const char* k, const string& def = "") {
    if (!d.is_object() || !d.contains(k) || !d[k].is_string()) return def;
    return d[k].get<string>();
}
static string url_of(const json& e) { return jstr(e, "url"); }

json norm_entry(const json& x, bool saved) {
    json e = x.is_string() ? json{{"url", x.get<string>()}} : (x.is_object() ? x : json{{"url", ""}});
    if (!e.contains("url") || !e["url"].is_string()) e["url"] = "";
    if (!e.contains("name") || e["name"].is_null()) e["name"] = "";
    if (!e.contains("range") || e["range"].is_null()) e["range"] = "";
    if (!e.contains("count")) e["count"] = nullptr;
    if (!e.contains("over")) e["over"] = false;
    if (!e.contains("from_repeat")) e["from_repeat"] = false;
    if (!e.contains("repeat_count")) e["repeat_count"] = 0;
    if (jstr(e, "range").empty()) e["range"] = range_from_url(url_of(e));   // the price range is in the link
    if (saved && (!e.contains("saved_at") || e["saved_at"].is_null() || jint(e, "saved_at", 0) == 0)) e["saved_at"] = now();
    return e;
}

vector<int> reorder(vector<json>& items, vector<int> idx, const string& where) {
    std::sort(idx.begin(), idx.end());
    idx.erase(std::unique(idx.begin(), idx.end()), idx.end());
    std::set<int> sel(idx.begin(), idx.end());
    if (where == "top" || where == "bottom") {
        vector<json> chosen, rest;
        for (int i = 0; i < (int)items.size(); i++) (sel.count(i) ? chosen : rest).push_back(items[i]);
        items.clear();
        if (where == "top") {
            items.insert(items.end(), chosen.begin(), chosen.end());
            items.insert(items.end(), rest.begin(), rest.end());
        } else {
            items.insert(items.end(), rest.begin(), rest.end());
            items.insert(items.end(), chosen.begin(), chosen.end());
        }
        int start = where == "top" ? 0 : (int)rest.size();
        vector<int> out;
        for (int i = 0; i < (int)chosen.size(); i++) out.push_back(start + i);
        return out;
    }
    int step = where == "up" ? -1 : 1;
    vector<int> order = idx;
    if (step > 0) std::reverse(order.begin(), order.end());
    for (int i : order) {
        int j = i + step;
        if (j >= 0 && j < (int)items.size() && !sel.count(j)) {   // a block at the edge stays where it is
            std::swap(items[i], items[j]);
            sel.erase(i);
            sel.insert(j);
        }
    }
    return vector<int>(sel.begin(), sel.end());
}

static json to_list(const json& d, const char* k, bool saved = false) {
    json out = json::array();
    if (d.is_object() && d.contains(k) && d[k].is_array())
        for (auto& x : d[k]) out.push_back(norm_entry(x, saved));
    return out;
}

// ---------------------------------------------------------------- set-up
Core::Core() {
    string text;
    json d = json::object();
    if (read_file(data_file(), text)) {
        try {
            d = json::parse(text);
        } catch (...) {
            d = json::object();
        }
    }
    if (!d.is_object()) d = json::object();
    doc_ = d;
    for (auto& e : to_list(d, "pending")) pending.push_back(e);
    for (auto& e : to_list(d, "done")) done.push_back(e);
    for (auto& e : to_list(d, "saved", true)) saved.push_back(e);
    for (auto& e : to_list(d, "repeat")) {
        if (!e.contains("repeat_at") || e["repeat_at"].is_null()) e["repeat_at"] = now();
        repeat.push_back(e);
    }
    lang = jstr(d, "lang", "en");
    if (lang != "tr") lang = "en";
    interval_min = (int)jint(d, "interval_min", 30);
    scan_browser = jstr(d, "scan_browser", "Chrome");
    split_browser = jstr(d, "split_browser", "Opera");
    opera_path = jstr(d, "opera_path");
    if (opera_path.empty()) opera_path = find_opera();
    repeat_on = jbool(d, "repeat_on", true);
    repeat_days = (int)jint(d, "repeat_days", jint(d, "auto_days", 7));
    step_wait = (int)jint(d, "step_wait", 3);
    scan_filter = d.contains("scan_filter") ? jstr(d, "scan_filter") : string(DEFAULT_SAVED_SETTING);
    min_price = d.contains("min_price") && d["min_price"].is_number() ? std::to_string(jint(d, "min_price", 0))
                                                                       : jstr(d, "min_price");
    fill_on_start = jbool(d, "fill_on_start", true);
    auto driver_setting = [&](const char* key, const string& kind) {
        string p = trim(jstr(d, key));
        if (!p.empty() && !is_driver_file(p)) {
            cleared_drivers_.push_back(p);
            p.clear();
        }
        return p.empty() ? find_driver_quiet(kind) : p;
    };
    chromedriver = driver_setting("chromedriver_path", "chrome");
    edgedriver = driver_setting("edgedriver_path", "edge");
    string pin = jstr(d, "remote_pin");
    remote_pin = valid_pin(pin) ? pin : new_pin();
    remote_port = (int)jint(d, "remote_port", REMOTE_PORT);
    remote_on = jbool(d, "remote_on", false);
    remote_ext = jstr(d, "remote_ext");
    interval_s = std::max(1, interval_min) * 60;
    sync_runtime();
    scan_fn = [](const string& url, const string& b, const string& o, int w, const string& s) { scan_link(url, b, o, w, s); };
    split_fn = [](const string& url, long long limit, int wait, const string& b, const string& o,
                  std::optional<long long> sp, const Emit& emit, const std::atomic<bool>& stop,
                  std::optional<long long> mp) { run_splitter(url, limit, wait, b, o, sp, emit, stop, mp); };
}

Core::~Core() {
    if (remote) remote->stop();
}

void Core::startup() {
    auto_transfer();
    refresh();                                             // shows the price ranges filled from the links
    log(env_text());
    for (auto& p : cleared_drivers_) log(T("drv_cleared", {{"path", p}}));
    hub.set_status(remote_status());
    if (remote_on) remote_start();
    if (fill_on_start) fill_at_ = now() + 1.5;
    last_transfer_ = now();
    started_ = true;
}

void Core::sync_runtime() {
    g_lang = lang;
    g_rt.chromedriver = trim(chromedriver);
    g_rt.edgedriver = trim(edgedriver);
}

void Core::settings_changed() {
    sync_runtime();
    save();
    if (on_status) on_status();
}

string Core::env_text() const { return T("env_info", {{"win", windows_name(windows_version())}}); }

string Core::scan_status_text() const {
    if (!running) return T("st_stopped");
    if (busy) return T("st_working");
    return T("st_next", {{"t", fmt_time(std::max(0.0, next_run - now()))}});
}

vector<json>* Core::list(const string& name) {
    if (name == "pending") return &pending;
    if (name == "done") return &done;
    if (name == "saved") return &saved;
    if (name == "repeat") return &repeat;
    return nullptr;
}

vector<string> Core::row_values(int i, const json& e, const string& kind) const {
    string res = e.contains("count") && !e["count"].is_null() ? fmt_res(jint(e, "count", 0), jbool(e, "over", false)) : "-";
    long long rc = jint(e, "repeat_count", 0);
    string tick = rc == 0 ? "" : (rc == 1 ? u8"✓" : u8"✓ ×" + std::to_string(rc));
    vector<string> v = {std::to_string(i), jstr(e, "name"), jstr(e, "range"), res};
    if (kind == "saved") {
        double sa = (double)jint(e, "saved_at", 0);
        v.push_back(sa ? date_ymd(sa) : "");
    } else if (kind == "repeat") {
        double ra = e.contains("repeat_at") && e["repeat_at"].is_number() ? e["repeat_at"].get<double>() : 0;
        v.push_back(tick);
        v.push_back(ra && repeat_on ? date_ymd(ra + repeat_days * 86400.0) : "-");
    } else {
        v.push_back(tick);
    }
    v.push_back(url_of(e));
    return v;
}

// ---------------------------------------------------------------- log / save
void Core::log(const string& msg) {
    string line = "[" + clock_hms() + "] " + msg;
    log_lines.push_back(line);
    if (log_lines.size() > 500) log_lines.erase(log_lines.begin(), log_lines.end() - 500);
    hub.add_log(line);
    if (on_log) on_log(line);
}

void Core::refresh() {
    remote_publish();
    save();
    if (on_lists) on_lists();
}

void Core::save() {
    json d = doc_;
    auto arr = [](const vector<json>& v) { return json(v); };
    d["pending"] = arr(pending);
    d["done"] = arr(done);
    d["saved"] = arr(saved);
    d["repeat"] = arr(repeat);
    d["lang"] = lang;
    d["interval_min"] = interval_min;
    d["scan_browser"] = scan_browser;
    d["split_browser"] = split_browser;
    d["opera_path"] = trim(opera_path);
    d["repeat_on"] = repeat_on;
    d["repeat_days"] = repeat_days;
    d["step_wait"] = step_wait;
    d["scan_filter"] = trim(scan_filter);
    d["min_price"] = trim(min_price);
    d["fill_on_start"] = fill_on_start;
    d["chromedriver_path"] = trim(chromedriver);
    d["edgedriver_path"] = trim(edgedriver);
    d["remote_on"] = remote_on;
    d["remote_port"] = remote_port;
    d["remote_pin"] = remote_pin;
    d["remote_ext"] = remote_ext;
    doc_ = d;
    make_dirs(app_dir());
    string tmp = data_file() + ".tmp";
    if (write_file(tmp, d.dump(2, ' ', false, json::error_handler_t::replace)) && !replace_file(tmp, data_file()))
        write_file(data_file(), d.dump(2, ' ', false, json::error_handler_t::replace));
}

// ---------------------------------------------------------------- list actions
void Core::remove_indices(const string& name, vector<int> idx) {
    auto* items = list(name);
    if (!items) return;
    std::sort(idx.begin(), idx.end());
    for (auto it = idx.rbegin(); it != idx.rend(); ++it)
        if (*it >= 0 && *it < (int)items->size()) items->erase(items->begin() + *it);
    refresh();
}

void Core::clear(const string& name) {
    auto* items = list(name);
    if (items && !items->empty()) {
        items->clear();
        refresh();
    }
}

string Core::copy_urls(const vector<int>& idx, bool everything) {
    vector<string> urls;
    if (everything) {
        for (auto& e : saved) urls.push_back(url_of(e));
    } else {
        for (int i : idx) if (i >= 0 && i < (int)saved.size()) urls.push_back(url_of(saved[i]));
    }
    if (urls.empty()) return "";
    log(T("log_copied", {{"n", std::to_string(urls.size())}}));
    return join(urls, "\r\n");
}

void Core::move_saved_to_pending(const vector<int>& idx0) {
    vector<int> idx = idx0;
    if (idx.empty()) for (int i = 0; i < (int)saved.size(); i++) idx.push_back(i);
    std::set<string> known;
    for (auto& e : pending) known.insert(url_of(e));
    std::set<int> chosen(idx.begin(), idx.end());
    vector<json> keep;
    int n = 0;
    for (int i = 0; i < (int)saved.size(); i++) {
        if (!chosen.count(i)) {
            keep.push_back(saved[i]);
            continue;
        }
        n++;
        if (!known.count(url_of(saved[i]))) {
            json e = saved[i];
            e.erase("saved_at");
            pending.push_back(e);
            known.insert(url_of(e));
        }
    }
    saved = keep;
    refresh();
    log(T("log_moved", {{"n", std::to_string(n)}}));
}

void Core::auto_transfer() {                               // Repeat -> Pending after the chosen number of days
    if (!repeat_on || repeat_days <= 0) return;
    double t = now();
    vector<json*> due;
    for (auto& r : repeat) {
        double ra = r.contains("repeat_at") && r["repeat_at"].is_number() ? r["repeat_at"].get<double>() : t;
        if (t - ra >= repeat_days * 86400.0) due.push_back(&r);
    }
    if (!due.empty()) move_repeat_entries(due);
}

void Core::move_repeat_entries(vector<json*> entries) {
    std::set<string> known;
    for (auto& e : pending) known.insert(url_of(e));
    std::set<const json*> moved;
    for (auto* e : entries) {
        if (known.count(url_of(*e))) continue;
        json e2 = *e;
        e2.erase("repeat_at");
        e2["from_repeat"] = true;                          // returns to Repeat after EasyCentral
        e2["repeat_count"] = jint(*e, "repeat_count", 0) + 1;
        pending.push_back(e2);
        known.insert(url_of(*e));
        moved.insert(e);
    }
    if (!moved.empty()) {
        vector<json> keep;
        for (auto& r : repeat) if (!moved.count(&r)) keep.push_back(r);
        repeat = keep;
        refresh();
        log(T("log_moved_repeat", {{"n", std::to_string(moved.size())}}));
    }
}

void Core::move_repeat_to_pending(const vector<int>& idx) {
    vector<json*> entries;
    for (int i : idx) if (i >= 0 && i < (int)repeat.size()) entries.push_back(&repeat[i]);
    move_repeat_entries(entries);
}

void Core::move_done_to_repeat(vector<json*> entries) {
    std::set<string> known;
    for (auto& r : repeat) known.insert(url_of(r));
    for (auto* e : entries) {
        if (known.count(url_of(*e))) continue;
        json e2 = *e;
        e2["from_repeat"] = false;
        e2["repeat_at"] = now();
        repeat.push_back(e2);
        known.insert(url_of(*e));
    }
    std::set<const json*> ids(entries.begin(), entries.end());
    vector<json> keep;
    for (auto& d : done) if (!ids.count(&d)) keep.push_back(d);
    size_t n = entries.size();
    done = keep;
    refresh();
    log(T("log_to_repeat", {{"n", std::to_string(n)}}));
}

void Core::done_to_repeat(const vector<int>& idx, bool everything) {
    vector<json*> entries;
    if (everything) {
        for (auto& d : done) entries.push_back(&d);
    } else {
        for (int i : idx) if (i >= 0 && i < (int)done.size()) entries.push_back(&done[i]);
    }
    if (entries.empty()) return;
    move_done_to_repeat(entries);
}

void Core::move_done_back(const vector<int>& idx0) {
    vector<int> idx = idx0;
    std::sort(idx.begin(), idx.end());
    for (auto it = idx.rbegin(); it != idx.rend(); ++it) {
        if (*it < 0 || *it >= (int)done.size()) continue;
        pending.push_back(done[*it]);
        done.erase(done.begin() + *it);
    }
    refresh();
}

int Core::add_text(const string& text) {
    std::set<string> known;
    for (auto& e : pending) known.insert(url_of(e));
    int added = 0;
    for (auto& line0 : split_lines(text)) {
        string line = trim(line0);
        if (!line.empty() && !known.count(line)) {
            pending.push_back(norm_entry(line));
            known.insert(line);
            added++;
        }
    }
    refresh();
    return added;
}

std::pair<int, int> Core::import_urls(const vector<string>& urls) {
    std::set<string> known;
    for (auto* l : {&pending, &done, &repeat}) for (auto& e : *l) known.insert(url_of(e));
    int added = 0, skipped = 0;
    for (auto& u : urls) {
        if (known.count(u)) {
            skipped++;
        } else {
            pending.push_back(norm_entry(u));
            known.insert(u);
            added++;
        }
    }
    refresh();
    log(T("log_excel", {{"added", std::to_string(added)}, {"skipped", std::to_string(skipped)}}));
    return {added, skipped};
}

void Core::revise(int i, const string& url0) {
    string url = trim(url0);
    if (i < 0 || i >= (int)pending.size() || url.empty() || url == url_of(pending[i])) return;
    json& e = pending[i];
    e["url"] = url;
    e["range"] = range_from_url(url);
    e["name"] = "";
    e["count"] = nullptr;
    e["over"] = false;
    e.erase("info_tried");
    refresh();
}

vector<int> Core::move_rows(const string& name, const vector<int>& idx, const string& where) {
    auto* items = list(name);
    if (!items || idx.empty()) return idx;
    vector<int> sorted = idx;
    std::sort(sorted.begin(), sorted.end());
    vector<int> out = reorder(*items, sorted, where);
    if (out != sorted) refresh();
    return out;
}

// ---------------------------------------------------------------- scanner
void Core::apply_interval(int minutes) {
    int m = std::max(1, minutes);
    interval_min = m;
    interval_s = m * 60;
    if (running) next_run = now() + interval_s;
    log(T("log_interval", {{"m", std::to_string(m)}}));
    save();
}

void Core::toggle() {
    running = !running;
    if (running) {
        apply_interval(interval_min);
        next_run = now();
        log(T("log_scan_start", {{"m", std::to_string(interval_s / 60)}}));
    } else {
        log(T("log_scan_stop"));
    }
    if (on_status) on_status();
}

void Core::login_scanner() {
    if (busy) return;
    busy = true;
    string browser = scan_browser, opera = trim(opera_path);
    log(T("log_login_open", {{"browser", browser}}));
    EventQueue* q = &q_;
    std::thread([=] {
        try {
            open_login_window("scanner", browser, opera, TARGET_URL);
            q->put({{"k", "msg"}, {"key", "log_login_closed"}, {"kw", json::object()}});
        } catch (const std::exception& e) {
            q->put({{"k", "msg"}, {"key", "log_login_err"}, {"kw", {{"e", short_err(e.what())}}}});
        }
        q->put({{"k", "free"}});
    }).detach();
    if (on_status) on_status();
}

void Core::start_job() {
    if (busy) return log(T("log_job_running"));
    if (pending.empty()) return log(T("log_pending_empty"));
    string url = url_of(pending[0]);
    string browser = scan_browser, opera = trim(opera_path), setting = trim(scan_filter);
    int sw = std::max(0, step_wait);
    busy = true;
    log(T("log_processing", {{"url", url}}));
    EventQueue* q = &q_;
    auto fn = scan_fn;
    std::thread([=] {
        try {
            fn(url, browser, opera, sw, setting);
            q->put({{"k", "done"}, {"url", url}});
        } catch (const std::exception& e) {
            q->put({{"k", "fail"}, {"url", url}, {"e", short_err(e.what())}});
        }
        q->put({{"k", "free"}});
    }).detach();
    if (on_status) on_status();
}

// ---------------------------------------------------------------- splitter
static bool opera_ok(const string& browser, const string& opera) {
    return lower_ascii(browser) != "opera" || (!trim(opera).empty() && file_exists(trim(opera)));
}

string Core::login_splitter() {
    if (splitting) return "";
    string browser = split_browser, opera = trim(opera_path);
    if (!opera_ok(browser, opera)) return "opera_missing";
    splitting = true;
    log(T("log_login_open", {{"browser", browser}}));
    EventQueue* q = &q_;
    std::thread([=] {
        try {
            open_login_window("splitter", browser, opera, "https://www.amazon.com");
            q->put({{"k", "msg"}, {"key", "log_login_closed"}, {"kw", json::object()}});
        } catch (const std::exception& e) {
            q->put({{"k", "msg"}, {"key", "log_login_err"}, {"kw", {{"e", short_err(e.what())}}}});
        }
        q->put({{"k", "split_free"}});
    }).detach();
    if (on_status) on_status();
    return "";
}

string Core::start_split() {
    if (splitting) {
        log(T("log_split_running"));
        return "";
    }
    string url = trim(split_url);
    if (!starts_with(lower_ascii(url), "http")) return "paste_link";
    string browser = split_browser, opera = trim(opera_path);
    if (!opera_ok(browser, opera)) return "opera_missing";
    long long lim = limit;
    int ws = std::max(0, wait_s);
    std::optional<long long> sp, mp;
    try {
        if (!trim(start_price).empty()) sp = (long long)std::stod(trim(start_price));
    } catch (...) {
        sp.reset();
    }
    if (!trim(min_price).empty()) {
        try {
            size_t used = 0;
            string m = replace_all(trim(min_price), ",", ".");
            double v = std::stod(m, &used);
            if (used != m.size()) throw Error("x");
            mp = (long long)v;
        } catch (...) {
            return "err_min_invalid";
        }
    }
    splitting = true;
    stop_->store(false);
    log(T("log_split_start", {{"browser", browser}, {"limit", num(lim)}, {"wait", std::to_string(ws)}}));
    EventQueue* q = &q_;
    auto stop = stop_;
    auto fn = split_fn;
    std::thread([=] {
        Emit emit = [q](const json& ev) { q->put(ev); };
        try {
            fn(url, lim, ws, browser, opera, sp, emit, *stop, mp);
            q->put({{"k", "msg"}, {"key", "log_split_finished"}, {"kw", json::object()}});
        } catch (const NoPriceError&) {
            q->put({{"k", "msg"}, {"key", "err_no_price"}, {"kw", json::object()}});
        } catch (const MinPriceError& e) {
            q->put({{"k", "msg"}, {"key", "err_min_price"}, {"kw", {{"p", std::to_string(e.price)}}}});
        } catch (const Interrupted&) {
            q->put({{"k", "msg"}, {"key", "log_split_stopped"}, {"kw", json::object()}});
        } catch (const std::exception& e) {
            q->put({{"k", "msg"}, {"key", "log_split_err"}, {"kw", {{"e", short_err(e.what())}}}});
        }
        q->put({{"k", "split_free"}});
    }).detach();
    if (on_status) on_status();
    return "";
}

vector<string> Core::missing_info(bool only_untried) {
    std::set<string> seen;
    vector<string> urls;
    for (auto* l : {&pending, &repeat, &done, &saved})
        for (auto& e : *l) {
            string u = url_of(e);
            if (seen.count(u) || !starts_with(lower_ascii(u), "http")) continue;
            bool missing = jstr(e, "name").empty() || !e.contains("count") || e["count"].is_null();
            if (missing && !(only_untried && jbool(e, "info_tried", false))) {
                seen.insert(u);
                urls.push_back(u);
            }
        }
    return urls;
}

void Core::start_fill(bool automatic) {
    if (splitting) {
        if (!automatic) log(T("log_split_running"));
        return;
    }
    vector<string> urls = missing_info(automatic);
    if (urls.empty()) {
        if (!automatic) log(T("log_fill_none"));
        return;
    }
    string browser = split_browser, opera = trim(opera_path);
    if (lower_ascii(browser) == "opera" && (opera.empty() || !file_exists(opera)))
        return log(T("log_fill_skip", {{"browser", browser}}));
    int ws = std::max(0, wait_s);
    splitting = true;                                      // the browser profile is shared with the splitter
    stop_->store(false);
    log(T("log_fill_start", {{"n", std::to_string(urls.size())}, {"browser", browser}}));
    EventQueue* q = &q_;
    auto stop = stop_;
    std::thread([=] {
        Emit emit = [q](const json& ev) { q->put(ev); };
        try {
            fill_info(urls, ws, browser, opera, emit, *stop);
            q->put({{"k", "msg"}, {"key", "log_fill_done"}, {"kw", json::object()}});
        } catch (const Interrupted&) {
            q->put({{"k", "msg"}, {"key", "log_split_stopped"}, {"kw", json::object()}});
        } catch (const std::exception& e) {
            q->put({{"k", "msg"}, {"key", "log_split_err"}, {"kw", {{"e", short_err(e.what())}}}});
        }
        q->put({{"k", "split_free"}});
    }).detach();
    if (on_status) on_status();
}

void Core::stop_split() {
    if (splitting) {
        stop_->store(true);
        log(T("log_split_stopping"));
    }
}

// ---------------------------------------------------------------- drivers
static string opera_chromium_major(const string& opera) {
    static const std::regex re(R"([\\/](\d+)\.\d+\.\d+\.\d+[\\/])");
    std::smatch m;
    return std::regex_search(opera, m, re) ? std::to_string(to_ll(m[1].str()) + 14) : string();
}

void Core::get_driver(const string& kind, const string& file) {
    if (drv_busy) return;
    drv_busy = true;
    string opera = trim(opera_path);
    EventQueue* q = &q_;
    std::thread([=] {
        try {
            string version;
            if (kind == "chrome" && file.empty() && installed_browser_version("chrome").empty()) {
                version = opera_chromium_major(opera);     // Opera runs on Chromium N+14
                if (!version.empty()) q->put({{"k", "msg"}, {"key", "drv_opera_est"}, {"kw", {{"v", version}}}});
            }
            string path = install_driver(kind, "", version, file, [q](const string& s) { q->put({{"k", "raw"}, {"text", s}}); });
            q->put({{"k", "driver"}, {"kind", kind}, {"path", path}});
        } catch (const DriverError& e) {
            q->put({{"k", "raw"}, {"text", e.what()}});
        } catch (const std::exception& e) {
            q->put({{"k", "raw"}, {"text", T("drv_error", {{"e", short_err(e.what())}})}});
        }
        q->put({{"k", "drv_free"}});
    }).detach();
}

// ---------------------------------------------------------------- events and the timer
void Core::handle_event(const json& ev) {
    string k = jstr(ev, "k");
    if (k == "done") {
        string url = jstr(ev, "url");
        bool back = false;
        for (size_t i = 0; i < pending.size(); i++) {
            if (url_of(pending[i]) != url) continue;
            json e = pending[i];
            pending.erase(pending.begin() + (long)i);
            if (jbool(e, "from_repeat", false)) {          // repeat links return to Repeat
                e["from_repeat"] = false;
                e["repeat_at"] = now();                    // the day counter starts again
                bool there = false;
                for (auto& r : repeat) there = there || url_of(r) == url;
                if (!there) repeat.push_back(e);
                back = true;
            } else {
                done.push_back(e);
            }
            break;
        }
        refresh();
        log(T(back ? "log_done_repeat" : "log_done", {{"url", url}}));
    } else if (k == "fail") {
        log(T("log_fail", {{"url", jstr(ev, "url")}, {"e", jstr(ev, "e")}}));
    } else if (k == "saved") {
        string url = jstr(ev, "url"), note = jstr(ev, "note"), name = jstr(ev, "name");
        long long n = jint(ev, "n", 0);
        bool over = jbool(ev, "over", false);
        string rng = "$" + std::to_string(jint(ev, "lo", 0)) + " - $" + std::to_string(jint(ev, "hi", 0)) +
                     (note.empty() ? "" : "  " + T("note_" + note));
        bool there = false;
        for (auto& s : saved) there = there || url_of(s) == url;
        if (!there) {
            saved.push_back({{"url", url}, {"name", name}, {"range", rng}, {"count", n}, {"over", over}, {"saved_at", now()}});
            refresh();
        }
        log(T("log_saved", {{"n", std::to_string(saved.size())}, {"name", name}, {"rng", rng}, {"res", fmt_res(n, over)}}));
    } else if (k == "info") {
        string url = jstr(ev, "url"), name = jstr(ev, "name");
        long long n = jint(ev, "n", 0);
        bool over = jbool(ev, "over", false);
        for (auto* l : {&pending, &repeat, &done, &saved})
            for (auto& e : *l)
                if (url_of(e) == url) {
                    if (!name.empty() && jstr(e, "name").empty()) e["name"] = name;
                    if (!e.contains("count") || e["count"].is_null()) {
                        e["count"] = n;
                        e["over"] = over;
                    }
                    e["info_tried"] = true;
                }
        refresh();
        log(T("log_fill_item", {{"i", std::to_string(jint(ev, "i", 0))}, {"total", std::to_string(jint(ev, "total", 0))},
                                {"name", name.empty() ? "-" : name}, {"res", fmt_res(n, over)}}));
    } else if (k == "info_fail") {
        string url = jstr(ev, "url");
        for (auto* l : {&pending, &repeat, &done, &saved})
            for (auto& e : *l) if (url_of(e) == url) e["info_tried"] = true;
        refresh();
        log(T("log_fill_fail", {{"i", std::to_string(jint(ev, "i", 0))}, {"total", std::to_string(jint(ev, "total", 0))},
                                {"url", url}}));
    } else if (k == "msg") {
        KW kw;
        if (ev.contains("kw") && ev["kw"].is_object())
            for (auto it = ev["kw"].begin(); it != ev["kw"].end(); ++it)
                kw[it.key()] = it.value().is_string() ? it.value().get<string>() : it.value().dump();
        log(T(jstr(ev, "key"), kw));
    } else if (k == "raw") {
        log(jstr(ev, "text"));
    } else if (k == "driver") {                            // a driver was installed: use it
        string kind = jstr(ev, "kind"), path = jstr(ev, "path");
        (kind == "edge" ? edgedriver : chromedriver) = path;
        settings_changed();
        if (on_driver) on_driver(kind, path);
    } else if (k == "drv_free") {
        drv_busy = false;
    } else if (k == "ext") {
        remote_ext = jstr(ev, "ip");
        save();
    } else if (k == "net_free") {
        net_busy = false;
    } else if (k == "free") {
        busy = false;
        if (on_status) on_status();
    } else if (k == "split_free") {
        splitting = false;
        if (on_status) on_status();
    }
}

void Core::tick() {
    json ev;
    while (q_.get(ev)) handle_event(ev);
    if (running) {
        double remaining = next_run - now();
        if (remaining <= 0 && !busy) {
            next_run = now() + interval_s;
            start_job();
        }
    }
    if (now() - last_transfer_ >= 60) {                    // about once a minute
        last_transfer_ = now();
        auto_transfer();
    }
    if (fill_at_ && now() >= fill_at_) {
        fill_at_ = 0;
        start_fill(true);
    }
    hub.set_status(remote_status());
}

// ---------------------------------------------------------------- phone
bool Core::remote_start() {
    remote_stop(true);
    EventQueue* q = &q_;
    auto ev = [q](const string& kind, const string& ip) {          // called from a server thread
        q->put({{"k", "msg"}, {"key", "log_remote_" + kind}, {"kw", {{"ip", ip}}}});
    };
    std::unique_ptr<RemoteServer> srv(new RemoteServer(hub, remote_pin, remote_port, ev));
    try {
        srv->start();
    } catch (const std::exception& e) {
        log(T("err_remote_port", {{"port", std::to_string(remote_port)}, {"e", short_err(e.what())}}));
        remote_on = false;
        if (on_status) on_status();
        return false;
    }
    remote = std::move(srv);
    log(T("log_remote_started", {{"port", std::to_string(remote_port)}}));
    return true;
}

void Core::remote_stop(bool quiet) {
    if (remote) {
        remote->stop();
        remote.reset();
        if (!quiet) log(T("log_remote_stopped"));
    }
}

void Core::remote_toggle(bool on) {
    remote_on = on;
    if (on) remote_start();
    else remote_stop();
    save();
}

string Core::remote_apply(const string& pin0, const string& port0) {
    string pin = trim(pin0);
    if (!valid_pin(pin)) return "err_remote_pin";
    long long port = to_ll(trim(port0), -1);
    if (port < 1024 || port > 65535) return "err_remote_portnum";
    bool pin_changed = pin != remote_pin, port_changed = port != remote_port;
    remote_pin = pin;
    remote_port = (int)port;
    if (pin_changed) {
        if (remote) remote->set_pin(pin);
        log(T("log_remote_pin"));
    }
    if (port_changed && remote) remote_start();
    save();
    return "";
}

string Core::remote_info_text() {
    if (!remote) return T("remote_off");
    vector<string> ips = local_addresses(), addrs;
    for (auto& ip : ips) addrs.push_back(ip + ":" + std::to_string(remote_port));
    vector<string> lines = {
        ips.empty() ? T("remote_home_none") : T("remote_home", {{"addr", join(addrs, "   /   ")}}),
        remote_ext.empty() ? T("remote_out_unknown")
                           : T("remote_out", {{"addr", remote_ext + ":" + std::to_string(remote_port)}}),
        T("remote_phones", {{"n", std::to_string(remote->phones())}})};
    return join(lines, "\n");
}

void Core::remote_find_ext() {
    if (net_busy) return;
    net_busy = true;
    int port = remote_port;
    log(T("log_ext_looking"));
    EventQueue* q = &q_;
    std::thread([=] {
        string ip = public_ip();
        if (!ip.empty()) {
            q->put({{"k", "ext"}, {"ip", ip}});
            q->put({{"k", "msg"}, {"key", "log_ext_ok"}, {"kw", {{"ip", ip}, {"port", std::to_string(port)}}}});
        } else {
            q->put({{"k", "msg"}, {"key", "log_ext_fail"}, {"kw", json::object()}});
        }
        q->put({{"k", "net_free"}});
    }).detach();
}

void Core::remote_upnp() {
    if (net_busy) return;
    net_busy = true;
    int port = remote_port;
    auto ips = local_addresses();
    string local = ips.empty() ? "" : ips[0];
    log(T("log_upnp_trying", {{"port", std::to_string(port)}}));
    EventQueue* q = &q_;
    std::thread([=] {
        try {
            string wan = upnp_open_port(port, local);
            q->put({{"k", "msg"}, {"key", "log_upnp_ok"}, {"kw", {{"port", std::to_string(port)}, {"ip", local}}}});
            string pub = public_ip();
            if (!pub.empty()) {
                q->put({{"k", "ext"}, {"ip", pub}});
                q->put({{"k", "msg"}, {"key", "log_ext_ok"}, {"kw", {{"ip", pub}, {"port", std::to_string(port)}}}});
            }
            if (!wan.empty() && (is_private_ip(wan) || (!pub.empty() && wan != pub)))
                q->put({{"k", "msg"}, {"key", "log_cgnat"}, {"kw", {{"wan", wan}, {"pub", pub.empty() ? "?" : pub}}}});
        } catch (const std::exception& e) {
            q->put({{"k", "msg"}, {"key", "log_upnp_fail"},
                    {"kw", {{"e", short_err(e.what())}, {"port", std::to_string(port)}, {"ip", local.empty() ? "?" : local}}}});
        }
        q->put({{"k", "net_free"}});
    }).detach();
}

void Core::remote_publish() {
    const std::pair<const char*, const char*> kinds[] = {
        {"list", "pending"}, {"list", "done"}, {"repeat", "repeat"}, {"saved", "saved"}};
    for (auto& kv : kinds) {
        json rows = json::array();
        auto* items = list(kv.second);
        for (int i = 0; i < (int)items->size(); i++) {
            auto v = row_values(i + 1, (*items)[i], kv.first);
            vector<string> extra;
            for (size_t j = 4; j + 1 < v.size(); j++) if (!v[j].empty()) extra.push_back(v[j]);
            rows.push_back({{"u", v.back()}, {"n", v[1]}, {"r", v[2]}, {"c", v[3]}, {"x", join(extra, "  ")}});
        }
        hub.set_list(kv.second, rows);
    }
}

json Core::remote_status() const {
#ifdef _WIN32
    string pc = env("COMPUTERNAME");
#else
    char host[256] = "computer";
    string pc = gethostname(host, sizeof host - 1) == 0 ? string(host) : string("computer");
#endif
    return {{"lang", lang}, {"running", running}, {"busy", busy}, {"splitting", splitting},
            {"next_run", running ? next_run : 0.0}, {"interval", interval_min}, {"step_wait", step_wait},
            {"filter", scan_filter}, {"repeat_on", repeat_on}, {"repeat_days", repeat_days}, {"limit", limit},
            {"wait", wait_s}, {"min_price", trim(min_price)}, {"pc", pc}};
}

void Core::remote_pump() {
    std::shared_ptr<RemoteHub::Command> c;
    while (hub.next_command(c)) {
        json r;
        try {
            r = remote_exec(c->req);
        } catch (const std::exception& e) {
            r = {{"ok", false}, {"err", "error"}, {"msg", utf8_truncate(short_err(e.what()), 200)}};
        }
        hub.finish(c, r);
    }
}

json Core::remote_exec(const json& c) {
    string op = jstr(c, "op"), name = jstr(c, "list");
    std::set<string> urls;
    if (c.contains("urls") && c["urls"].is_array())
        for (auto& u : c["urls"]) if (u.is_string()) urls.insert(u.get<string>());
    auto bad = [](const string& err) { return json{{"ok", false}, {"err", err}}; };
    static const std::map<string, string> names = {{"pending", "Pending"}, {"done", "Done"}, {"repeat", "Repeat"}, {"saved", "Saved"}};

    if (op == "add") {
        std::set<string> known;
        for (auto& e : pending) known.insert(url_of(e));
        int added = 0, skipped = 0, count = 0;
        for (auto& line0 : split_lines(jstr(c, "text"))) {
            if (++count > 5000) break;
            string line = trim(line0);
            if (line.empty()) continue;
            if (known.count(line)) {
                skipped++;
            } else {
                pending.push_back(norm_entry(line));
                known.insert(line);
                added++;
            }
        }
        if (added) {
            refresh();
            log(T("log_remote_add", {{"n", std::to_string(added)}}));
        }
        return {{"ok", true}, {"added", added}, {"skipped", skipped}};
    }
    if (op == "remove") {
        auto* items = list(name);
        if (!items) return bad("bad");
        size_t before = items->size();
        vector<json> keep;
        for (auto& e : *items) if (!urls.count(url_of(e))) keep.push_back(e);
        *items = keep;
        size_t n = before - items->size();
        if (n) {
            refresh();
            log(T("log_remote_remove", {{"n", std::to_string(n)}, {"name", names.at(name)}}));
        }
        return {{"ok", true}, {"n", n}};
    }
    if (op == "clear") {
        auto* items = list(name);
        if (!items) return bad("bad");
        if (!items->empty()) {
            items->clear();
            refresh();
            log(T("log_remote_clear", {{"name", names.at(name)}}));
        }
        return {{"ok", true}};
    }
    if (op == "revise") {
        string old = jstr(c, "old"), nu = trim(jstr(c, "new"));
        if (nu.empty()) return bad("bad_link");
        for (int i = 0; i < (int)pending.size(); i++)
            if (url_of(pending[i]) == old) {
                if (nu != old) {
                    revise(i, nu);
                    log(T("log_remote_revise"));
                }
                return {{"ok", true}};
            }
        return bad("gone");
    }
    if (op == "move") {
        string src = jstr(c, "from"), dst = jstr(c, "to");
        auto* items = list(src);
        if (!items) return bad("bad");
        vector<int> idx;
        for (int i = 0; i < (int)items->size(); i++)
            if (urls.empty() ? jbool(c, "all", false) : urls.count(url_of((*items)[i]))) idx.push_back(i);
        if (idx.empty()) return bad("gone");
        if (src == "done" && dst == "pending") move_done_back(idx);
        else if (src == "done" && dst == "repeat") done_to_repeat(idx, false);
        else if (src == "repeat" && dst == "pending") move_repeat_to_pending(idx);
        else if (src == "saved" && dst == "pending") move_saved_to_pending(idx);
        else return bad("bad");
        return {{"ok", true}, {"n", idx.size()}};
    }
    if (op == "reorder") {                                 // move links up / down / to the top / bottom
        auto* items = list(name);
        string where = jstr(c, "where");
        if (!items || (where != "up" && where != "down" && where != "top" && where != "bottom")) return bad("bad");
        vector<int> idx;
        for (int i = 0; i < (int)items->size(); i++) if (urls.count(url_of((*items)[i]))) idx.push_back(i);
        if (idx.empty()) return bad("gone");
        move_rows(name, idx, where);
        return {{"ok", true}};
    }
    if (op == "scan") {
        if (jbool(c, "on", false) != running) toggle();
        return {{"ok", true}};
    }
    if (op == "run_now") {
        if (busy) return bad("busy");
        if (pending.empty()) return bad("empty");
        start_job();
        return {{"ok", true}};
    }
    if (op == "set") {
        auto number = [&](const char* k, long long lo, long long hi, long long& out) {
            const json& v = c[k];
            if (v.is_number()) out = (long long)v.get<double>();
            else if (v.is_string() && is_digits(trim(v.get<string>()))) out = to_ll(v.get<string>());
            else return false;
            out = std::max(lo, std::min(hi, out));
            return true;
        };
        long long v;
        if (c.contains("interval")) {
            if (!number("interval", 1, 10080, v)) return bad("number");
            apply_interval((int)v);
        }
        if (c.contains("step_wait")) {
            if (!number("step_wait", 0, 60, v)) return bad("number");
            step_wait = (int)v;
        }
        if (c.contains("repeat_days")) {
            if (!number("repeat_days", 1, 3650, v)) return bad("number");
            repeat_days = (int)v;
            refresh();
        }
        if (c.contains("filter")) scan_filter = trim(utf8_truncate(jstr(c, "filter"), 100));
        if (c.contains("repeat_on")) {
            repeat_on = jbool(c, "repeat_on", true);
            auto_transfer();
            refresh();
        }
        settings_changed();
        return {{"ok", true}};
    }
    if (op == "split_start") {
        if (splitting) return bad("splitting");
        string url = trim(jstr(c, "url"));
        if (!starts_with(lower_ascii(url), "http")) return bad("bad_link");
        if (!opera_ok(split_browser, opera_path)) return bad("opera");
        auto field = [&](const char* k) {
            if (!c.contains(k) || c[k].is_null()) return string();
            return c[k].is_string() ? trim(c[k].get<string>()) : c[k].dump();
        };
        long long lim = 9000, ws = 10;
        string sp = field("start_price"), mp = field("min_price");
        try {
            if (!field("limit").empty()) lim = (long long)std::stod(field("limit"));
            if (!field("wait").empty()) ws = (long long)std::stod(field("wait"));
            if (!sp.empty()) std::stod(sp);
            if (!mp.empty()) std::stod(replace_all(mp, ",", "."));
        } catch (...) {
            return bad("number");
        }
        split_url = url;
        limit = (int)std::max(1000LL, std::min(100000LL, lim));
        wait_s = (int)std::max(0LL, std::min(120LL, ws));
        start_price = sp;
        min_price = mp;
        string err = start_split();
        settings_changed();
        return splitting && err.empty() ? json{{"ok", true}} : bad("error");
    }
    if (op == "split_stop") {
        stop_split();
        return {{"ok", true}};
    }
    if (op == "fill") {
        if (splitting) return bad("splitting");
        start_fill(false);
        return {{"ok", true}};
    }
    return bad("bad");
}

}  // namespace ec
