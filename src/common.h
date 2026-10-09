// EasyCentral Link Bot - C++ version. Shared helpers: texts, strings, time, files.
#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "json.hpp"

using json = nlohmann::json;
using std::string;
using std::vector;
using KW = std::map<string, string>;

namespace ec {

// ---- errors ----
struct Error : std::runtime_error {
    explicit Error(const string& m) : std::runtime_error(m) {}
};
struct TimeoutError : Error {
    explicit TimeoutError(const string& m) : Error(m) {}
};
struct Interrupted : Error {
    Interrupted() : Error("interrupted") {}
};

// ---- texts (English / Turkish) ----
extern string g_lang;                                     // "en" or "tr"
string T(const string& key, const KW& kw = {});
string fmt(string s, const KW& kw);
bool has_text(const string& key);

// ---- strings ----
string trim(const string& s);
string lower_ascii(string s);
string upper_ascii(string s);
bool starts_with(const string& s, const string& p);
bool ends_with(const string& s, const string& p);
bool contains(const string& s, const string& p);
string replace_all(string s, const string& a, const string& b);
string join(const vector<string>& v, const string& sep);
vector<string> split(const string& s, char sep);
vector<string> split_lines(const string& s);
string collapse_ws(const string& s);                      // " ".join(s.split())
string short_err(const string& msg);                      // first two lines, max 600 characters
string num(long long n);                                  // 9000 -> "9,000"
string fmt_res(long long n, bool over);
string fmt_time(double sec);                              // 75 -> "01:15", 3700 -> "1:01:40"
bool is_digits(const string& s);
long long to_ll(const string& s, long long def = 0);
string utf8_truncate(const string& s, size_t max_bytes);  // never cuts a character in half
string to_hex(const string& bytes);
string from_hex(const string& hex);
string base64_encode(const string& bytes);
string base64_decode(const string& text);

// ---- time ----
double now();                                             // seconds since 1970
void sleep_ms(long long ms);
string clock_hms();                                       // "14:05:09"
string date_ymd(double epoch);                            // "2026-10-09"

// ---- files (UTF-8 paths, also on Windows) ----
string app_dir();                                         // <home>/.easycentral_bot
void set_app_dir(const string& p);
string exe_dir();
string path_join(const string& a, const string& b);
string base_name(const string& p);
string dir_name(const string& p);
bool file_exists(const string& p);
bool dir_exists(const string& p);
bool make_dirs(const string& p);
bool read_file(const string& p, string& out);
bool write_file(const string& p, const string& data);
bool remove_file(const string& p);
bool replace_file(const string& from, const string& to);  // false while the target is in use
bool remove_tree(const string& p);
vector<string> list_dir(const string& p);                 // names
string temp_dir();
string env(const string& name);
string random_bytes(size_t n);

#ifdef _WIN32
std::wstring widen(const string& s);
string narrow(const std::wstring& w);
#endif

// ---- a simple thread-safe queue of events ----
class EventQueue {
public:
    void put(json ev) {
        std::lock_guard<std::mutex> g(m_);
        q_.push_back(std::move(ev));
    }
    bool get(json& ev) {
        std::lock_guard<std::mutex> g(m_);
        if (q_.empty()) return false;
        ev = std::move(q_.front());
        q_.erase(q_.begin());
        return true;
    }

private:
    std::mutex m_;
    vector<json> q_;
};

}  // namespace ec
