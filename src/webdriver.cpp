#include "webdriver.h"

#include <algorithm>
#include <regex>

#ifdef _WIN32
#include <windows.h>
#endif

namespace ec {

Runtime g_rt;
static const char* ELEMENT_KEY = "element-6066-11e4-a52e-4f735466cecf";

// ---------------------------------------------------------------- Windows / browsers / drivers
WinVer windows_version() {
    WinVer v;
#ifdef _WIN32
    typedef LONG(WINAPI * RtlGetVersionFn)(OSVERSIONINFOEXW*);
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    auto fn = nt ? (RtlGetVersionFn)(void*)GetProcAddress(nt, "RtlGetVersion") : nullptr;
    OSVERSIONINFOEXW info{};
    info.dwOSVersionInfoSize = sizeof info;
    if (fn && fn(&info) == 0) {                            // not fooled by compatibility modes
        v.major = (int)info.dwMajorVersion;
        v.minor = (int)info.dwMinorVersion;
        v.build = (int)info.dwBuildNumber;
        v.known = true;
    }
#endif
    return v;
}

string windows_name(const WinVer& v) {
    if (!v.known) {
#ifdef _WIN32
        return "Windows";
#else
        return "Linux";
#endif
    }
    if (v.major >= 10) return v.build >= 22000 ? "Windows 11" : "Windows 10";
    if (v.major == 6 && v.minor == 1) return "Windows 7";
    if (v.major == 6 && v.minor == 2) return "Windows 8";
    if (v.major == 6 && v.minor == 3) return "Windows 8.1";
    if (v.major == 6 && v.minor == 0) return "Windows Vista";
    return "Windows " + std::to_string(v.major) + "." + std::to_string(v.minor);
}

string profile_dir(const string& feature, const string& browser) {
    string b = lower_ascii(browser);
    if (feature == "scanner" && b == "chrome") return path_join(app_dir(), "chrome_profile");   // earlier login
    if (feature == "splitter" && b == "opera") return path_join(app_dir(), "opera_profile");
    return path_join(app_dir(), feature + "_" + b + "_profile");
}

static vector<long long> num_parts(const string& s) {
    vector<long long> out;
    string cur;
    for (char c : s) {
        if (c >= '0' && c <= '9') {
            cur += c;
        } else {
            out.push_back(cur.empty() ? 0 : to_ll(cur, 0));
            cur.clear();
        }
    }
    out.push_back(cur.empty() ? 0 : to_ll(cur, 0));
    return out;
}

string find_opera() {
    const char* envs[][2] = {{"LOCALAPPDATA", "Programs"}, {"ProgramFiles", ""}, {"ProgramFiles(x86)", ""}};
    for (auto& e : envs) {
        string root = env(e[0]);
        if (root.empty()) continue;
        for (const char* name : {"Opera", "Opera GX"}) {
            string base = path_join(e[1][0] ? path_join(root, e[1]) : root, name);
            if (!dir_exists(base)) continue;
            vector<string> versions;
            for (auto& n : list_dir(base))
                if (file_exists(path_join(path_join(base, n), "opera.exe"))) versions.push_back(n);
            std::sort(versions.begin(), versions.end(),
                      [](const string& a, const string& b) { return num_parts(a) > num_parts(b); });
            if (!versions.empty()) return path_join(path_join(base, versions[0]), "opera.exe");
            for (const char* exe : {"opera.exe", "launcher.exe"})
                if (file_exists(path_join(base, exe))) return path_join(base, exe);
        }
    }
    return "";
}

string driver_file_name(const string& kind) {
#ifdef _WIN32
    return string(kind == "edge" ? "msedgedriver" : "chromedriver") + ".exe";
#else
    return kind == "edge" ? "msedgedriver" : "chromedriver";
#endif
}

string find_driver_quiet(const string& kind) {
    string name = driver_file_name(kind);
    vector<string> dirs = {path_join(app_dir(), "drivers"), exe_dir(), path_join(exe_dir(), "drivers")};
#ifdef _WIN32
    wchar_t cwd[MAX_PATH * 2];
    DWORD n = GetCurrentDirectoryW(MAX_PATH * 2, cwd);
    if (n) dirs.push_back(narrow(std::wstring(cwd, n)));
    for (auto& d : split(env("PATH"), ';')) if (!trim(d).empty()) dirs.push_back(trim(d));
#else
    dirs.push_back(".");
    for (auto& d : split(env("PATH"), ':')) if (!d.empty()) dirs.push_back(d);
#endif
    for (auto& d : dirs) {
        string p = path_join(d, name);
        if (file_exists(p)) return p;
    }
    return "";
}

bool is_driver_file(const string& path) {
    string p = trim(path);
    if (p.size() >= 2 && p.front() == '"' && p.back() == '"') p = p.substr(1, p.size() - 2);
    return contains(lower_ascii(base_name(p)), "driver");
}

string find_driver(const string& kind) {
    string cfg = trim(kind == "edge" ? g_rt.edgedriver : g_rt.chromedriver);
    if (cfg.size() >= 2 && cfg.front() == '"' && cfg.back() == '"') cfg = cfg.substr(1, cfg.size() - 2);
    if (!cfg.empty()) {
        if (file_exists(cfg)) {
            if (!is_driver_file(cfg)) throw Error(T("err_not_driver", {{"path", cfg}}));
            return cfg;
        }
        throw Error(T("err_driver_path", {{"path", cfg}}));
    }
    string found = find_driver_quiet(kind);
    if (!found.empty()) return found;
    throw Error(T("err_driver_missing", {{"name", driver_file_name(kind)}, {"kind", kind},
                                         {"folder", path_join(app_dir(), "drivers")}}));
}

string driver_version(const string& path) {
    string out = run_capture({path, "--version"}, 30000);
    std::smatch m;
    static const std::regex re(R"((\d+\.\d+\.\d+\.\d+))");
    return std::regex_search(out, m, re) ? m[1].str() : string();
}

static std::mutex g_live_m;
static vector<Process*> g_live;
void kill_live_drivers() {
    std::lock_guard<std::mutex> g(g_live_m);
    for (auto* p : g_live) p->kill();
}

// ---------------------------------------------------------------- XPath helpers
static string xpath_literal(const string& s) {
    if (!contains(s, "\"")) return "\"" + s + "\"";
    if (!contains(s, "'")) return "'" + s + "'";
    vector<string> parts;
    for (auto& p : split(s, '"')) parts.push_back("\"" + p + "\"");
    return "concat(" + join(parts, ", '\"', ") + ")";
}

static const char* FOLD_FROM = u8"ABCDEFGHIJKLMNOPQRSTUVWXYZİıÇĞÖŞÜ";
static const char* FOLD_TO = u8"abcdefghijklmnopqrstuvwxyziiçğöşü";

static string fold(const string& s) {          // lower case; the Turkish dotted / dotless i both count as i
    static const vector<std::pair<string, string>> pairs = {
        {u8"İ", "i"}, {u8"ı", "i"}, {u8"Ç", u8"ç"}, {u8"Ğ", u8"ğ"}, {u8"Ö", u8"ö"}, {u8"Ş", u8"ş"}, {u8"Ü", u8"ü"}};
    string out = lower_ascii(s);
    for (auto& p : pairs) out = replace_all(out, p.first, p.second);
    return out;
}

static string xp_has(const string& expr, const string& needle) {
    return "contains(translate(" + expr + ", " + xpath_literal(FOLD_FROM) + ", " + xpath_literal(FOLD_TO) + "), " +
           xpath_literal(fold(needle)) + ")";
}

// ---------------------------------------------------------------- Session
Session::Session(const string& feature, const string& browser, const string& opera_path) : page_(*this) {
    string b = lower_ascii(browser);
    string binary;
    if (b == "opera") {
        if (opera_path.empty() || !file_exists(opera_path)) throw Error(T("err_opera_missing"));
        binary = opera_path;
    }
    string kind = b == "edge" ? "edge" : "chrome";          // Opera is Chromium: chromedriver drives it
    string driver = find_driver(kind);
    string profile = profile_dir(feature, browser);
    make_dirs(profile);
    json args = {"--user-data-dir=" + profile, "--no-first-run", "--no-default-browser-check", "--start-maximized",
                 "--disable-blink-features=AutomationControlled"};
    if (g_rt.headless) args.push_back("--headless=new");
    for (auto& a : g_rt.extra_args) args.push_back(a);
    json opts = {{"args", args}, {"excludeSwitches", json::array({"enable-automation"})}};
    if (!binary.empty()) opts["binary"] = binary;
    json caps = {{"browserName", kind == "edge" ? "MicrosoftEdge" : "chrome"}, {"pageLoadStrategy", "eager"}};
    caps[kind == "edge" ? "ms:edgeOptions" : "goog:chromeOptions"] = opts;
    try {
        start_driver(driver);
        json value = request("POST", "/session", {{"capabilities", {{"alwaysMatch", caps}}}}, 180000);
        sid_ = value.value("sessionId", "");
        if (sid_.empty()) throw WebDriverError("the driver did not open a browser");
        cmd("POST", "/timeouts", {{"pageLoad", 60000}, {"script", 30000}, {"implicit", 0}});
    } catch (const std::exception& e) {
        close();
        string m = e.what();
        if (contains(lower_ascii(m), "only supports")) throw WebDriverError(m + " | " + T("hint_driver_match"));
        throw;
    }
}

Session::~Session() { close(); }

void Session::start_driver(const string& driver) {
    int port = free_local_port();
    base_ = "http://127.0.0.1:" + std::to_string(port);
    make_dirs(app_dir());
    string log_file = path_join(app_dir(), "driver.log");
    remove_file(log_file);
    proc_.reset(new Process());
    string err;
    if (!proc_->start({driver, "--port=" + std::to_string(port), "--log-path=" + log_file}, err)) {
        proc_.reset();
        throw WebDriverError(err);
    }
    {
        std::lock_guard<std::mutex> g(g_live_m);
        g_live.push_back(proc_.get());
    }
    double deadline = now() + 25;
    while (now() < deadline) {
        int code = 0;
        if (!proc_->running(&code))
            throw WebDriverError(T("err_driver_exit", {{"code", std::to_string(code)}, {"path", driver},
                                                       {"info", exit_info(driver, log_file)}}));
        try {
            request("GET", "/status", json(), 2000);
            return;
        } catch (const std::exception&) {
            sleep_ms(300);
        }
    }
    throw WebDriverError(T("err_driver_start"));
}

string Session::exit_info(const string& driver, const string& log_file) {
    string info;
    if (is_driver_file(driver)) {                          // never run a browser with --version
        try {
            string v = driver_version(driver);
            if (!v.empty()) info += T("drv_exit_version", {{"v", v}}) + " ";
        } catch (...) {
        }
    }
    string text;
    if (read_file(log_file, text)) {
        vector<string> lines;
        for (auto& l : split_lines(text)) if (!trim(l).empty()) lines.push_back(trim(l));
        if (!lines.empty()) {
            vector<string> last(lines.end() - std::min<size_t>(3, lines.size()), lines.end());
            info += T("drv_exit_log", {{"log", utf8_truncate(join(last, " | "), 300)}}) + " ";
        }
    }
    return info;
}

json Session::request(const string& method, const string& path, const json& body, int timeout_ms) {
    HttpResponse r;
    try {
        r = http_request(method, base_ + path, method == "POST" ? (body.is_null() ? string("{}") : body.dump()) : string(),
                         {}, timeout_ms);
    } catch (const std::exception& e) {
        throw WebDriverError(string("Cannot reach the driver: ") + e.what());
    }
    json payload;
    if (!r.body.empty()) {
        try {
            payload = json::parse(r.body);
        } catch (...) {
            throw WebDriverError("Bad answer from the driver (HTTP " + std::to_string(r.status) + ")");
        }
    }
    json value = payload.is_object() && payload.contains("value") ? payload["value"] : payload;
    if (value.is_object() && value.contains("error") && (value.contains("message") || value.contains("stacktrace"))) {
        string msg = value.value("message", "");
        throw WebDriverError(msg.empty() ? value["error"].get<string>() : msg);
    }
    if (r.status >= 400) throw WebDriverError("HTTP " + std::to_string(r.status) + " from the driver");
    return value;
}

json Session::cmd(const string& method, const string& path, const json& body, int timeout_ms) {
    return request(method, "/session/" + sid_ + path, body, timeout_ms);
}

vector<string> Session::find(const string& how, const string& what) {
    json res = cmd("POST", "/elements", {{"using", how}, {"value", what}});
    vector<string> out;
    if (res.is_array())
        for (auto& r : res)
            if (r.is_object() && !r.empty()) out.push_back(r.begin().value().get<string>());
    return out;
}

bool Session::displayed(const string& id) {
    try {
        json v = cmd("GET", "/element/" + id + "/displayed");
        return v.is_boolean() && v.get<bool>();
    } catch (const WebDriverError&) {
        return false;
    }
}

string Session::first_displayed(const string& locator, int timeout_ms, const string& what, const string& how) {
    return wait_for([&]() -> string {
        for (auto& id : find(how, locator)) if (displayed(id)) return id;
        return "";
    }, timeout_ms, what);
}

void Session::click_first(const string& locator, int timeout_ms, const string& what, const string& how) {
    string id = first_displayed(locator, timeout_ms, what, how);
    try {
        cmd("POST", "/element/" + id + "/click", json::object());
    } catch (const WebDriverError&) {                       // covered by something else: click with script
        cmd("POST", "/execute/sync", {{"script", "arguments[0].click();"}, {"args", json::array({json{{ELEMENT_KEY, id}}})}});
    }
}

void Session::wait_closed() {
    while (true) {
        sleep_ms(1000);
        if (!proc_ || !proc_->running() || sid_.empty()) return;
        try {
            json h = cmd("GET", "/window/handles", json(), 10000);
            if (!h.is_array() || h.empty()) return;
        } catch (...) {
            return;
        }
    }
}

void Session::close() {
    if (!sid_.empty()) {
        string sid = sid_;
        sid_.clear();
        try {
            request("DELETE", "/session/" + sid, json(), 15000);
        } catch (...) {
        }
    }
    if (proc_) {
        proc_->kill();
        std::lock_guard<std::mutex> g(g_live_m);
        g_live.erase(std::remove(g_live.begin(), g_live.end(), proc_.get()), g_live.end());
        proc_.reset();
    }
}

// ---------------------------------------------------------------- Page
void Page::go(const string& url) { s_.cmd("POST", "/url", {{"url", url}}, 150000); }
void Page::wait(long long ms) { sleep_ms(ms); }
void Page::wait_for_selector(const string& css, int timeout_ms) {
    s_.wait_for([&] { return s_.find("css selector", css); }, timeout_ms, css);
}
string Page::text(const string& css, int timeout_ms) {
    auto ids = s_.wait_for([&] { return s_.find("css selector", css); }, timeout_ms, css);
    json t = s_.cmd("GET", "/element/" + ids[0] + "/text");
    return t.is_string() ? t.get<string>() : string();
}
string Page::body_text() { return text("body", 5000); }
string Page::value(const string& css, int timeout_ms) {
    auto ids = s_.wait_for([&] { return s_.find("css selector", css); }, timeout_ms, css);
    json t = s_.cmd("GET", "/element/" + ids[0] + "/property/value");
    return t.is_string() ? t.get<string>() : string();
}
string Page::selected_text(const string& css) {
    json t = s_.cmd("POST", "/execute/sync",
                    {{"script", "var e=document.querySelector(arguments[0]);"
                                "return (e && e.options && e.selectedIndex>=0) ? e.options[e.selectedIndex].text : '';"},
                     {"args", json::array({css})}});
    return t.is_string() ? t.get<string>() : string();
}
void Page::click_text(const string& text, int timeout_ms) {
    string has = xp_has("normalize-space(.)", text);
    s_.click_first("//*[text()[" + has + "]] | //*[not(*)][" + has + "]", timeout_ms, text);
}
void Page::fill_placeholder(const string& placeholder, const string& value, int timeout_ms) {
    string xp = "//*[(self::input or self::textarea) and " + xp_has("@placeholder", placeholder) + "]";
    string id = s_.first_displayed(xp, timeout_ms, placeholder);
    try {
        s_.cmd("POST", "/element/" + id + "/clear", json::object());
    } catch (const WebDriverError&) {
    }
    s_.cmd("POST", "/element/" + id + "/value", {{"text", value}});
    json now_value = s_.cmd("GET", "/element/" + id + "/property/value");
    if (!now_value.is_string() || now_value.get<string>() != value) {     // React-style inputs
        s_.cmd("POST", "/execute/sync",
               {{"script", "var e=arguments[0],v=arguments[1];"
                           "var set=Object.getOwnPropertyDescriptor(Object.getPrototypeOf(e),'value').set;"
                           "set.call(e,v);e.dispatchEvent(new Event('input',{bubbles:true}));"
                           "e.dispatchEvent(new Event('change',{bubbles:true}));"},
                {"args", json::array({json{{ELEMENT_KEY, id}}, value})}});
    }
}
void Page::click_button(const string& name, int timeout_ms) {
    string has = xp_has("normalize-space(.)", name);
    string xp = "//button[" + has + "] | //*[@role='button'][" + has + "] | //input[(@type='button' or @type='submit') and " +
                xp_has("@value", name) + "]";
    s_.click_first(xp, timeout_ms, name);
}
void Page::click_css(const string& css, int timeout_ms) { s_.click_first(css, timeout_ms, css, "css selector"); }
json Page::run_js(const string& body, const json& arg) {
    return s_.cmd("POST", "/execute/sync", {{"script", body}, {"args", json::array({arg})}});
}
void Page::mouse_away() {
    json move = {{"type", "pointerMove"}, {"duration", 0}, {"origin", "viewport"}, {"x", 1}, {"y", 1}};
    json src = {{"type", "pointer"}, {"id", "mouse"}, {"parameters", {{"pointerType", "mouse"}}},
                {"actions", json::array({move})}};
    try {
        s_.cmd("POST", "/actions", {{"actions", json::array({src})}});
    } catch (...) {
    }
}
void Page::close() {
    try {
        s_.cmd("DELETE", "/window", json(), 15000);
    } catch (...) {
    }
}

}  // namespace ec
