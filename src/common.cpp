#include "common.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <thread>

#include "gen_strings.h"

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace ec {

string g_lang = "en";

// ---------------------------------------------------------------- texts
static const std::map<string, const TrEntry*>& tr_index() {
    static std::map<string, const TrEntry*> idx = [] {
        std::map<string, const TrEntry*> m;
        for (const auto& e : TR_TABLE) m[e.key] = &e;
        return m;
    }();
    return idx;
}

bool has_text(const string& key) { return tr_index().count(key) > 0; }

string fmt(string s, const KW& kw) {
    for (const auto& p : kw) s = replace_all(s, "{" + p.first + "}", p.second);
    return s;
}

string T(const string& key, const KW& kw) {
    auto it = tr_index().find(key);
    if (it == tr_index().end()) return key;
    const char* s = (g_lang == "tr" && it->second->tr && *it->second->tr) ? it->second->tr : it->second->en;
    return fmt(s, kw);
}

// ---------------------------------------------------------------- strings
string trim(const string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) a++;
    while (b > a && std::isspace((unsigned char)s[b - 1])) b--;
    return s.substr(a, b - a);
}
string lower_ascii(string s) {
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return s;
}
string upper_ascii(string s) {
    for (auto& c : s) if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
    return s;
}
bool starts_with(const string& s, const string& p) { return s.size() >= p.size() && s.compare(0, p.size(), p) == 0; }
bool ends_with(const string& s, const string& p) {
    return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0;
}
bool contains(const string& s, const string& p) { return s.find(p) != string::npos; }
string replace_all(string s, const string& a, const string& b) {
    if (a.empty()) return s;
    size_t pos = 0;
    while ((pos = s.find(a, pos)) != string::npos) {
        s.replace(pos, a.size(), b);
        pos += b.size();
    }
    return s;
}
string join(const vector<string>& v, const string& sep) {
    string out;
    for (size_t i = 0; i < v.size(); i++) {
        if (i) out += sep;
        out += v[i];
    }
    return out;
}
vector<string> split(const string& s, char sep) {
    vector<string> out;
    string cur;
    for (char c : s) {
        if (c == sep) {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    out.push_back(cur);
    return out;
}
vector<string> split_lines(const string& s) {
    vector<string> out;
    string cur;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c == '\r') {
            out.push_back(cur);
            cur.clear();
            if (i + 1 < s.size() && s[i + 1] == '\n') i++;
        } else if (c == '\n') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}
string collapse_ws(const string& s) {
    string out;
    bool sp = false;
    for (unsigned char c : s) {
        if (std::isspace(c)) {
            sp = !out.empty();
        } else {
            if (sp) out += ' ';
            sp = false;
            out += char(c);
        }
    }
    return out;
}
string short_err(const string& msg) {
    vector<string> lines;
    for (auto& l : split_lines(msg)) {
        string t = trim(l);
        if (!t.empty()) lines.push_back(t);
        if (lines.size() == 2) break;
    }
    string out = utf8_truncate(join(lines, " | "), 600);
    return out.empty() ? string("error") : out;
}
string num(long long n) {
    bool neg = n < 0;
    string d = std::to_string(neg ? -n : n), out;
    int c = 0;
    for (size_t i = d.size(); i-- > 0;) {
        out.insert(out.begin(), d[i]);
        if (++c % 3 == 0 && i > 0) out.insert(out.begin(), ',');
    }
    return neg ? "-" + out : out;
}
string fmt_res(long long n, bool over) { return (over ? "over " : "") + num(n); }
string fmt_time(double sec) {
    long long s = (long long)sec, h = s / 3600, m = (s % 3600) / 60, x = s % 60;
    char buf[40];
    if (h) std::snprintf(buf, sizeof buf, "%lld:%02lld:%02lld", h, m, x);
    else std::snprintf(buf, sizeof buf, "%02lld:%02lld", m, x);
    return buf;
}
bool is_digits(const string& s) {
    if (s.empty()) return false;
    for (char c : s) if (c < '0' || c > '9') return false;
    return true;
}
long long to_ll(const string& s, long long def) {
    string t = trim(s);
    if (t.empty()) return def;
    char* end = nullptr;
    long long v = std::strtoll(t.c_str(), &end, 10);
    if (!end || *end) return def;
    return v;
}
string utf8_truncate(const string& s, size_t max_bytes) {
    if (s.size() <= max_bytes) return s;
    size_t n = max_bytes;
    while (n > 0 && (((unsigned char)s[n]) & 0xC0) == 0x80) n--;
    return s.substr(0, n);
}
string to_hex(const string& bytes) {
    static const char* d = "0123456789abcdef";
    string out;
    out.reserve(bytes.size() * 2);
    for (unsigned char c : bytes) {
        out += d[c >> 4];
        out += d[c & 15];
    }
    return out;
}
static int hexval(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
string from_hex(const string& hex0) {
    string hex = hex0.size() % 2 ? "0" + hex0 : hex0;
    string out;
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        int a = hexval(hex[i]), b = hexval(hex[i + 1]);
        if (a < 0 || b < 0) throw Error("bad hex");
        out += char(a * 16 + b);
    }
    return out;
}
static const char* B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
string base64_encode(const string& in) {
    string out;
    size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        unsigned v = ((unsigned char)in[i] << 16) | ((unsigned char)in[i + 1] << 8) | (unsigned char)in[i + 2];
        out += B64[v >> 18];
        out += B64[(v >> 12) & 63];
        out += B64[(v >> 6) & 63];
        out += B64[v & 63];
    }
    if (i < in.size()) {
        unsigned v = (unsigned char)in[i] << 16;
        if (i + 1 < in.size()) v |= (unsigned char)in[i + 1] << 8;
        out += B64[v >> 18];
        out += B64[(v >> 12) & 63];
        out += (i + 1 < in.size()) ? B64[(v >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}
string base64_decode(const string& in) {
    string out;
    unsigned v = 0;
    int bits = 0;
    for (char c : in) {
        if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
        const char* p = std::strchr(B64, c);
        if (!p || !c) throw Error("bad base64");
        v = (v << 6) | unsigned(p - B64);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += char((v >> bits) & 0xFF);
        }
    }
    return out;
}

// ---------------------------------------------------------------- time
double now() {
    using namespace std::chrono;
    return duration_cast<duration<double>>(system_clock::now().time_since_epoch()).count();
}
void sleep_ms(long long ms) {
    if (ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}
string clock_hms() {
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char buf[16];
    std::strftime(buf, sizeof buf, "%H:%M:%S", &tmv);
    return buf;
}
string date_ymd(double epoch) {
    std::time_t t = (std::time_t)epoch;
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char buf[16];
    std::strftime(buf, sizeof buf, "%Y-%m-%d", &tmv);
    return buf;
}

// ---------------------------------------------------------------- files
#ifdef _WIN32
std::wstring widen(const string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}
string narrow(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}
string env(const string& name) {
    wchar_t buf[32768];
    DWORD n = GetEnvironmentVariableW(widen(name).c_str(), buf, 32768);
    return (n && n < 32768) ? narrow(std::wstring(buf, n)) : string();
}
#else
string env(const string& name) {
    const char* v = std::getenv(name.c_str());
    return v ? string(v) : string();
}
#endif

static string g_app_dir;
string app_dir() {
    if (g_app_dir.empty()) {
        string over = env("ECBOT_HOME");
        if (!over.empty()) return over;
#ifdef _WIN32
        string home = env("USERPROFILE");
        if (home.empty()) home = env("HOMEDRIVE") + env("HOMEPATH");
#else
        string home = env("HOME");
#endif
        g_app_dir = path_join(home, ".easycentral_bot");
    }
    return g_app_dir;
}
void set_app_dir(const string& p) { g_app_dir = p; }

string exe_dir() {
#ifdef _WIN32
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH * 4);
    return dir_name(narrow(std::wstring(buf, n)));
#else
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    return n > 0 ? dir_name(string(buf, (size_t)n)) : string(".");
#endif
}

string path_join(const string& a, const string& b) {
    if (a.empty()) return b;
#ifdef _WIN32
    const char sep = '\\';
#else
    const char sep = '/';
#endif
    if (a.back() == '/' || a.back() == '\\') return a + b;
    return a + sep + b;
}
string base_name(const string& p) {
    size_t i = p.find_last_of("/\\");
    return i == string::npos ? p : p.substr(i + 1);
}
string dir_name(const string& p) {
    size_t i = p.find_last_of("/\\");
    return i == string::npos ? string(".") : p.substr(0, i);
}

#ifdef _WIN32
bool file_exists(const string& p) {
    DWORD a = GetFileAttributesW(widen(p).c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
bool dir_exists(const string& p) {
    DWORD a = GetFileAttributesW(widen(p).c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
bool make_dirs(const string& p) {
    if (p.empty() || dir_exists(p)) return true;
    string parent = dir_name(p);
    if (parent != p && !parent.empty() && parent != "." && !(parent.size() == 2 && parent[1] == ':')) make_dirs(parent);
    return CreateDirectoryW(widen(p).c_str(), nullptr) || dir_exists(p);
}
static FILE* open_file(const string& p, const char* mode) {
    return _wfopen(widen(p).c_str(), widen(mode).c_str());
}
bool remove_file(const string& p) { return DeleteFileW(widen(p).c_str()) != 0; }
bool replace_file(const string& from, const string& to) {
    return MoveFileExW(widen(from).c_str(), widen(to).c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}
vector<string> list_dir(const string& p) {
    vector<string> out;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(widen(path_join(p, "*")).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        string n = narrow(fd.cFileName);
        if (n != "." && n != "..") out.push_back(n);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return out;
}
bool remove_tree(const string& p) {
    for (auto& n : list_dir(p)) {
        string q = path_join(p, n);
        if (dir_exists(q)) remove_tree(q);
        else remove_file(q);
    }
    return RemoveDirectoryW(widen(p).c_str()) != 0;
}
string temp_dir() {
    wchar_t buf[MAX_PATH + 2];
    DWORD n = GetTempPathW(MAX_PATH + 1, buf);
    string base = narrow(std::wstring(buf, n));
    string d = path_join(base, "ecbot_" + to_hex(random_bytes(6)));
    make_dirs(d);
    return d;
}
string random_bytes(size_t n) {
    string out(n, '\0');
    HCRYPTPROV prov = 0;
    if (CryptAcquireContextW(&prov, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT | CRYPT_SILENT)) {
        BOOL ok = CryptGenRandom(prov, (DWORD)n, (BYTE*)&out[0]);
        CryptReleaseContext(prov, 0);
        if (ok) return out;
    }
    throw Error("no random numbers available");
}
#else
bool file_exists(const string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}
bool dir_exists(const string& p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}
bool make_dirs(const string& p) {
    if (p.empty() || dir_exists(p)) return true;
    string parent = dir_name(p);
    if (parent != p && !parent.empty() && parent != ".") make_dirs(parent);
    return mkdir(p.c_str(), 0755) == 0 || dir_exists(p);
}
static FILE* open_file(const string& p, const char* mode) { return std::fopen(p.c_str(), mode); }
bool remove_file(const string& p) { return unlink(p.c_str()) == 0; }
bool replace_file(const string& from, const string& to) { return std::rename(from.c_str(), to.c_str()) == 0; }
vector<string> list_dir(const string& p) {
    vector<string> out;
    DIR* d = opendir(p.c_str());
    if (!d) return out;
    while (auto* e = readdir(d)) {
        string n = e->d_name;
        if (n != "." && n != "..") out.push_back(n);
    }
    closedir(d);
    return out;
}
bool remove_tree(const string& p) {
    for (auto& n : list_dir(p)) {
        string q = path_join(p, n);
        if (dir_exists(q)) remove_tree(q);
        else remove_file(q);
    }
    return rmdir(p.c_str()) == 0;
}
string temp_dir() {
    string base = env("TMPDIR").empty() ? string("/tmp") : env("TMPDIR");
    string d = path_join(base, "ecbot_" + to_hex(random_bytes(6)));
    make_dirs(d);
    return d;
}
string random_bytes(size_t n) {
    string out(n, '\0');
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) throw Error("no random numbers available");
    size_t got = 0;
    while (got < n) {
        ssize_t r = read(fd, &out[got], n - got);
        if (r <= 0) break;
        got += (size_t)r;
    }
    close(fd);
    if (got != n) throw Error("no random numbers available");
    return out;
}
#endif

bool read_file(const string& p, string& out) {
    FILE* f = open_file(p, "rb");
    if (!f) return false;
    out.clear();
    char buf[65536];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    std::fclose(f);
    return true;
}
bool write_file(const string& p, const string& data) {
    FILE* f = open_file(p, "wb");
    if (!f) return false;
    bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    ok = (std::fclose(f) == 0) && ok;
    return ok;
}

}  // namespace ec
