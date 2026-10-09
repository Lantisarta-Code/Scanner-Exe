// Drives Chrome / Opera / Edge through chromedriver / msedgedriver (W3C WebDriver over HTTP).
// This is the engine the Python version used on Windows 7; here it is used on every Windows version.
#pragma once
#include <memory>

#include "common.h"
#include "net.h"

namespace ec {

struct Runtime {
    string chromedriver, edgedriver;                       // paths chosen in the window ('' = look for it)
    bool headless = false;                                 // tests only
    vector<string> extra_args;                             // tests only
};
extern Runtime g_rt;

struct WebDriverError : Error {
    explicit WebDriverError(const string& m) : Error(m) {}
};

// ---- Windows version / browsers / drivers ----
struct WinVer {
    int major = 0, minor = 0, build = 0;
    bool known = false;
};
WinVer windows_version();
string windows_name(const WinVer& v);
string profile_dir(const string& feature, const string& browser);
string find_opera();
string driver_file_name(const string& kind);              // kind: "edge" or "chrome"
string find_driver_quiet(const string& kind);
bool is_driver_file(const string& path);
string find_driver(const string& kind);                    // throws Error with a helpful text
string driver_version(const string& path);                 // "109.0.1518.61" or ""
void kill_live_drivers();

class Session;

class Page {
public:
    explicit Page(Session& s) : s_(s) {}
    void go(const string& url);
    void wait(long long ms);
    void wait_for_selector(const string& css, int timeout_ms);
    string text(const string& css, int timeout_ms = 5000);
    string body_text();
    string value(const string& css, int timeout_ms = 2000);
    string selected_text(const string& css);
    void click_text(const string& text, int timeout_ms);
    void fill_placeholder(const string& placeholder, const string& value, int timeout_ms);
    void click_button(const string& name, int timeout_ms);
    void click_css(const string& css, int timeout_ms);
    json run_js(const string& body, const json& arg = nullptr);
    void mouse_away();
    void close();

private:
    Session& s_;
};

class Session {
public:
    Session(const string& feature, const string& browser, const string& opera_path);
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    json cmd(const string& method, const string& path, const json& body = json::object(), int timeout_ms = 120000);
    vector<string> find(const string& how, const string& what);
    bool displayed(const string& id);
    string first_displayed(const string& locator, int timeout_ms, const string& what, const string& how = "xpath");
    void click_first(const string& locator, int timeout_ms, const string& what, const string& how = "xpath");
    template <class F>
    auto wait_for(F fn, int timeout_ms, const string& what) -> decltype(fn());
    Page& page() { return page_; }
    void wait_closed();
    void close();

private:
    json request(const string& method, const string& path, const json& body, int timeout_ms);
    void start_driver(const string& driver);
    string exit_info(const string& driver, const string& log_file);

    std::unique_ptr<Process> proc_;
    string base_, sid_;
    Page page_;
};

template <class F>
auto Session::wait_for(F fn, int timeout_ms, const string& what) -> decltype(fn()) {
    double end = now() + timeout_ms / 1000.0;
    while (true) {
        decltype(fn()) result{};
        try {
            result = fn();
        } catch (const WebDriverError&) {                    // the page is changing - try again
            result = decltype(fn()){};
        }
        if (!result.empty()) return result;
        if (now() >= end) throw TimeoutError("Timeout " + std::to_string(timeout_ms) + "ms waiting for: " + what);
        sleep_ms(250);
    }
}

}  // namespace ec
