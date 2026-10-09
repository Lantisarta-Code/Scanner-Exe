#include "drivers.h"

#include <algorithm>
#include <regex>

#include "miniz.h"
#include "net.h"
#include "webdriver.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace ec {

vector<string> EDGE_DRIVER_HOSTS = {"https://msedgedriver.microsoft.com", "https://msedgedriver.azureedge.net"};
string NUGET_EDGE = "https://api.nuget.org/v3-flatcontainer/selenium.webdriver.msedgedriver";
string NUGET_EDGE_V2 = "https://www.nuget.org/api/v2/package/Selenium.WebDriver.MSEdgeDriver";
string CHROME_STORAGE = "https://chromedriver.storage.googleapis.com";     // Chrome 114 and older
string CHROME_TESTING =
    "https://googlechromelabs.github.io/chrome-for-testing/latest-versions-per-milestone-with-downloads.json";
static const std::map<int, string> NUGET_KNOWN = {{109, "109.0.1518.61"}};   // when the NuGet list cannot be read

vector<int> version_tuple(const string& v) {
    vector<int> out;
    static const std::regex re(R"(\d+)");
    for (auto it = std::sregex_iterator(v.begin(), v.end(), re); it != std::sregex_iterator() && out.size() < 4; ++it)
        out.push_back((int)to_ll(it->str(), 0));
    return out;
}

static bool full_version(const string& v) {
    static const std::regex re(R"(^\d+\.\d+\.\d+\.\d+$)");
    return std::regex_match(v, re);
}

#ifdef _WIN32
static string reg_value(HKEY root, const string& path, const string& name, REGSAM extra) {
    HKEY k;
    if (RegOpenKeyExW(root, widen(path).c_str(), 0, KEY_READ | extra, &k) != ERROR_SUCCESS) return "";
    wchar_t buf[256];
    DWORD type = 0, size = sizeof buf;
    string out;
    if (RegQueryValueExW(k, widen(name).c_str(), nullptr, &type, (BYTE*)buf, &size) == ERROR_SUCCESS && type == REG_SZ)
        out = narrow(std::wstring(buf, wcsnlen(buf, size / sizeof(wchar_t))));
    RegCloseKey(k);
    return trim(out);
}
#endif

string installed_browser_version(const string& kind) {
#ifdef _WIN32
    string guid = kind == "edge" ? "{56EB18F8-B008-4CBD-B6D2-8C97FE7E9062}" : "{8A69D345-D564-463c-AFF1-A69D9E530F96}";
    struct K {
        HKEY root;
        string path, value;
    };
    vector<K> keys = kind == "edge"
                         ? vector<K>{{HKEY_CURRENT_USER, "Software\\Microsoft\\Edge\\BLBeacon", "version"},
                                     {HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\EdgeUpdate\\Clients\\" + guid, "pv"}}
                         : vector<K>{{HKEY_CURRENT_USER, "Software\\Google\\Chrome\\BLBeacon", "version"},
                                     {HKEY_LOCAL_MACHINE, "SOFTWARE\\Google\\Update\\Clients\\" + guid, "pv"}};
    for (auto& k : keys)
        for (REGSAM flag : {(REGSAM)0, (REGSAM)KEY_WOW64_32KEY, (REGSAM)KEY_WOW64_64KEY}) {
            string v = reg_value(k.root, k.path, k.value, flag);
            if (full_version(v)) return v;
        }
#endif
    // fallback: the program folder contains a sub-folder named after the version
    string sub = kind == "edge" ? path_join(path_join("Microsoft", "Edge"), "Application")
                                : path_join(path_join("Google", "Chrome"), "Application");
    string best;
    for (const char* e : {"ProgramFiles(x86)", "ProgramFiles", "LocalAppData"}) {
        string base = env(e);
        if (base.empty()) continue;
        for (auto& n : list_dir(path_join(base, sub)))
            if (full_version(n) && (best.empty() || version_tuple(n) > version_tuple(best))) best = n;
    }
    return best;
}

string driver_arch() {
#ifdef _WIN32
    string m = upper_ascii(env("PROCESSOR_ARCHITEW6432").empty() ? env("PROCESSOR_ARCHITECTURE")
                                                                 : env("PROCESSOR_ARCHITEW6432"));
    return contains(m, "64") ? "win64" : "win32";
#else
    return "win64";
#endif
}

static string why_text(const std::exception& e) {
    if (auto* h = dynamic_cast<const HttpError*>(&e)) return "HTTP " + std::to_string(h->status);
    string s = e.what();
    if (contains(s, "CERTIFICATE") || contains(s, T("drv_ssl"))) return T("drv_ssl");
    return short_err(s);
}

static string read_zip_file(mz_zip_archive& z, mz_uint index) {
    size_t size = 0;
    void* p = mz_zip_reader_extract_to_heap(&z, index, &size, 0);
    if (!p) throw Error("cannot unpack the archive");
    string out((const char*)p, size);
    mz_free(p);
    return out;
}

string extract_driver(const string& archive, const string& name, const string& dest_dir) {
    string data;
    if (!read_file(archive, data)) throw DriverError(T("drv_not_in_zip", {{"name", name}}));
    mz_zip_archive z{};
    if (!mz_zip_reader_init_mem(&z, data.data(), data.size(), 0)) throw DriverError(T("drv_not_in_zip", {{"name", name}}));
    string arch = driver_arch();
    vector<std::pair<int, mz_uint>> hits;                  // (preference, index)
    mz_uint count = mz_zip_reader_get_num_files(&z);
    for (mz_uint i = 0; i < count; i++) {
        mz_zip_archive_file_stat st;
        if (!mz_zip_reader_file_stat(&z, i, &st) || mz_zip_reader_is_file_a_directory(&z, i)) continue;
        string fn = replace_all(st.m_filename, "\\", "/");
        if (lower_ascii(split(fn, '/').back()) != lower_ascii(name)) continue;
        hits.push_back({contains("/" + lower_ascii(fn), "/" + arch + "/") ? 0 : 1, i});
    }
    if (hits.empty()) {
        mz_zip_reader_end(&z);
        throw DriverError(T("drv_not_in_zip", {{"name", name}}));
    }
    std::stable_sort(hits.begin(), hits.end(), [](auto& a, auto& b) { return a.first < b.first; });
    string bytes = read_zip_file(z, hits[0].second);
    mz_zip_reader_end(&z);
    make_dirs(dest_dir);
    string target = path_join(dest_dir, name), part = target + ".part";
    if (!write_file(part, bytes)) throw DriverError("cannot write " + part);
    if (!replace_file(part, target)) {                     // fails while the old driver is running
        remove_file(part);
        throw DriverLocked(T("drv_locked"));
    }
#ifndef _WIN32
    chmod(target.c_str(), 0755);
#endif
    return target;
}

// Microsoft's own downloads first; the NuGet list is only read when those did not work.
static vector<string> edge_ms_sources(const string& version) {
    vector<string> out;
    if (version_tuple(version).size() == 4)
        for (auto& h : EDGE_DRIVER_HOSTS) out.push_back(h + "/" + version + "/edgedriver_" + driver_arch() + ".zip");
    return out;
}
static vector<string> edge_nuget_sources(const string& version, const std::function<void(const string&)>& log) {
    vector<int> want = version_tuple(version);
    vector<int> padded = want;
    while (padded.size() < 4) padded.push_back(1000000000);
    vector<string> versions;
    try {
        json data = json::parse(http_text(NUGET_EDGE + "/index.json"));
        for (auto& v : data.value("versions", json::array())) {
            string s = v.get<string>();
            if (!contains(s, "-") && !want.empty() && !version_tuple(s).empty() && version_tuple(s)[0] == want[0])
                versions.push_back(s);
        }
    } catch (const std::exception& e) {
        log(T("drv_nuget_fail", {{"why", why_text(e)}}));
    }
    if (versions.empty() && !want.empty() && NUGET_KNOWN.count(want[0])) versions.push_back(NUGET_KNOWN.at(want[0]));
    std::sort(versions.begin(), versions.end(),
              [](const string& a, const string& b) { return version_tuple(a) < version_tuple(b); });
    vector<string> below, above;
    for (auto& v : versions) (version_tuple(v) <= padded ? below : above).push_back(v);
    std::reverse(below.begin(), below.end());
    below.insert(below.end(), above.begin(), above.end());
    vector<string> out;
    for (size_t i = 0; i < below.size() && i < 2; i++) {    // closest version first
        string lv = lower_ascii(below[i]);
        out.push_back(NUGET_EDGE + "/" + lv + "/selenium.webdriver.msedgedriver." + lv + ".nupkg");
        out.push_back(NUGET_EDGE_V2 + "/" + below[i]);
    }
    return out;
}
static vector<string> chrome_sources(const string& version) {
    int major = version_tuple(version).empty() ? 0 : version_tuple(version)[0];
    if (major <= 114) {
        string full = trim(http_text(CHROME_STORAGE + "/LATEST_RELEASE_" + std::to_string(major)));
        return {CHROME_STORAGE + "/" + full + "/chromedriver_win32.zip"};
    }
    json items = json::parse(http_text(CHROME_TESTING))["milestones"][std::to_string(major)]["downloads"]["chromedriver"];
    vector<string> urls, fallback;
    for (auto& i : items) {
        if (i.value("platform", "") == driver_arch()) urls.push_back(i.value("url", ""));
        if (i.value("platform", "") == "win32") fallback.push_back(i.value("url", ""));
    }
    return urls.empty() ? fallback : urls;
}

string install_driver(const string& kind, const string& dest_dir, const string& version0, const string& file,
                      const std::function<void(const string&)>& log) {
    string name = driver_file_name(kind);
    string label = kind == "edge" ? "Edge" : "Chrome";
    string dest = dest_dir.empty() ? path_join(app_dir(), "drivers") : dest_dir;
    string version = trim(version0);
    static const std::regex vre(R"(^\d+(\.\d+){0,3}$)");
    if (!version.empty() && !std::regex_match(version, vre)) throw DriverError(T("drv_bad_version", {{"v", version}}));
    if (version.empty()) {
        version = installed_browser_version(kind);
        if (!version.empty()) log(T("drv_found", {{"browser", label}, {"v", version}}));
        else if (file.empty()) throw DriverError(T("drv_no_browser", {{"browser", label}}));
    }
    string tmp = temp_dir();
    string target;
    try {
        if (!file.empty()) {
            log(T("drv_from_file", {{"path", file}}));
            target = extract_driver(file, name, dest);
        } else {
            vector<string> tried;
            int stage = 0;
            vector<string> sources;
            size_t next = 0;
            while (target.empty()) {
                if (next >= sources.size()) {                 // the next group of addresses
                    try {
                        if (kind == "edge" && stage == 0) sources = edge_ms_sources(version);
                        else if (kind == "edge" && stage == 1) sources = edge_nuget_sources(version, log);
                        else if (kind != "edge" && stage == 0) sources = chrome_sources(version);
                        else break;
                    } catch (const std::exception& e) {      // the address list itself could not be read
                        throw DriverError(T("drv_error", {{"e", why_text(e)}}));
                    }
                    stage++;
                    next = 0;
                    continue;
                }
                string url = sources[next++];
                log(T("drv_try", {{"url", url}}));
                tried.push_back(url);
                int last = -1;
                try {
                    string archive = path_join(tmp, "driver.zip");
                    http_download(url, archive, [&](unsigned long long done, unsigned long long total) {
                        int pct = (int)(done * 100 / total);
                        if (pct / 25 != last) {
                            last = pct / 25;
                            log(T("drv_progress", {{"pct", std::to_string(pct)}}));
                        }
                    });
                    target = extract_driver(archive, name, dest);
                } catch (const DriverLocked&) {
                    throw;
                } catch (const std::exception& e) {
                    log(T("drv_not_available", {{"why", why_text(e)}}));
                }
            }
            if (target.empty()) throw DriverError(T("drv_all_failed", {{"links", join(tried, "\n")}}));
        }
    } catch (...) {
        remove_tree(tmp);
        throw;
    }
    remove_tree(tmp);
    string dv;
    try {
        dv = driver_version(target);
    } catch (const std::exception& e) {
        log(T("drv_run_fail", {{"e", short_err(e.what())}}));
        return target;
    }
    log(T("drv_saved", {{"path", target}, {"v", dv.empty() ? "?" : dv}}));
    if (version.empty()) {
        log(T("drv_unknown_version", {{"browser", label}}));
    } else if (!dv.empty() && version_tuple(dv)[0] != version_tuple(version)[0]) {
        log(T("drv_mismatch", {{"dv", dv}, {"browser", label}, {"bv", version}}));
    } else {
        log(T("drv_match", {{"browser", label}, {"bv", version}}));
    }
    return target;
}

int driver_cli(const vector<string>& args, const std::function<void(const string&)>& say) {
    string kind, version, file, dest;
    for (size_t i = 0; i < args.size(); i++) {
        const string& a = args[i];
        auto val = [&](string& out) {
            if (i + 1 < args.size()) out = args[++i];
        };
        if (a == "--get-driver") {
            kind = "edge";
            if (i + 1 < args.size() && !starts_with(args[i + 1], "--")) kind = args[++i];
        } else if (a == "--version") {
            val(version);
        } else if (a == "--file") {
            val(file);
        } else if (a == "--dest") {
            val(dest);
        }
    }
    if (kind != "edge" && kind != "chrome") {
        say("EasyCentralBot.exe: error: argument --get-driver: invalid choice: '" + kind +
            "' (choose from 'edge', 'chrome')");
        return 2;
    }
    try {
        install_driver(kind, dest, version, file, say);
        return 0;
    } catch (const DriverError& e) {
        say(e.what());
    } catch (const std::exception& e) {
        say(T("drv_error", {{"e", short_err(e.what())}}));
    }
    return 1;
}

// ---------------------------------------------------------------- Excel (.xlsx) links
static string xml_unescape(const string& s) {
    string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] != '&') {
            out += s[i];
            continue;
        }
        size_t semi = s.find(';', i);
        if (semi == string::npos) {
            out += s[i];
            continue;
        }
        string ent = s.substr(i + 1, semi - i - 1);
        long cp = -1;
        if (ent == "amp") out += '&';
        else if (ent == "lt") out += '<';
        else if (ent == "gt") out += '>';
        else if (ent == "quot") out += '"';
        else if (ent == "apos") out += '\'';
        else if (!ent.empty() && ent[0] == '#')
            cp = ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X') ? std::strtol(ent.c_str() + 2, nullptr, 16)
                                                                     : std::strtol(ent.c_str() + 1, nullptr, 10);
        else {
            out += s.substr(i, semi - i + 1);
            i = semi;
            continue;
        }
        if (cp >= 0) {                                     // the character as UTF-8
            if (cp < 0x80) out += char(cp);
            else if (cp < 0x800) {
                out += char(0xC0 | (cp >> 6));
                out += char(0x80 | (cp & 63));
            } else if (cp < 0x10000) {
                out += char(0xE0 | (cp >> 12));
                out += char(0x80 | ((cp >> 6) & 63));
                out += char(0x80 | (cp & 63));
            } else {
                out += char(0xF0 | (cp >> 18));
                out += char(0x80 | ((cp >> 12) & 63));
                out += char(0x80 | ((cp >> 6) & 63));
                out += char(0x80 | (cp & 63));
            }
        }
        i = semi;
    }
    return out;
}
static string attr(const string& tag, const string& name) {
    static const std::regex ws(R"(\s)");
    for (size_t p = 0; (p = tag.find(name + "=", p)) != string::npos; p++) {
        if (p > 0 && !std::isspace((unsigned char)tag[p - 1])) continue;
        char q = tag[p + name.size() + 1];
        if (q != '"' && q != '\'') continue;
        size_t e = tag.find(q, p + name.size() + 2);
        if (e == string::npos) return "";
        return xml_unescape(tag.substr(p + name.size() + 2, e - p - name.size() - 2));
    }
    return "";
}
// every <name ...>inner</name> (or <name .../>): (tag text, inner)
static vector<std::pair<string, string>> elements(const string& xml, const string& name) {
    vector<std::pair<string, string>> out;
    size_t p = 0;
    while ((p = xml.find("<" + name, p)) != string::npos) {
        char c = xml[p + name.size() + 1];
        if (c != ' ' && c != '>' && c != '/' && c != '\t' && c != '\r' && c != '\n') {
            p++;
            continue;
        }
        size_t gt = xml.find('>', p);
        if (gt == string::npos) break;
        string tag = xml.substr(p, gt - p + 1);
        if (xml[gt - 1] == '/') {
            out.push_back({tag, ""});
            p = gt + 1;
            continue;
        }
        size_t end = xml.find("</" + name + ">", gt);
        if (end == string::npos) break;
        out.push_back({tag, xml.substr(gt + 1, end - gt - 1)});
        p = end + name.size() + 3;
    }
    return out;
}
static string texts_of(const string& inner) {              // all <t> texts inside
    string out;
    for (auto& t : elements(inner, "t")) out += xml_unescape(t.second);
    return out;
}

vector<string> xlsx_links(const string& path) {
    string data;
    if (!read_file(path, data)) throw Error(T("err_open_file", {{"e", path}}));
    mz_zip_archive z{};
    if (!mz_zip_reader_init_mem(&z, data.data(), data.size(), 0)) throw Error(T("err_open_file", {{"e", path}}));
    auto get = [&](const string& name) -> string {
        int i = mz_zip_reader_locate_file(&z, name.c_str(), nullptr, 0);
        return i < 0 ? string() : read_zip_file(z, (mz_uint)i);
    };
    auto rels_of = [&](const string& rels_path) {
        std::map<string, string> m;
        for (auto& r : elements(get(rels_path), "Relationship")) m[attr(r.first, "Id")] = attr(r.first, "Target");
        return m;
    };
    vector<string> shared;
    for (auto& si : elements(get("xl/sharedStrings.xml"), "si")) shared.push_back(texts_of(si.second));
    auto wb_rels = rels_of("xl/_rels/workbook.xml.rels");
    vector<string> sheets;
    for (auto& s : elements(get("xl/workbook.xml"), "sheet")) {
        string target = wb_rels[attr(s.first, "r:id")];
        if (target.empty()) continue;
        if (starts_with(target, "/")) target = target.substr(1);
        else target = "xl/" + target;
        sheets.push_back(target);
    }
    vector<string> found;
    auto is_link = [](const string& v) { return starts_with(lower_ascii(trim(v)), "http"); };
    for (auto& sheet : sheets) {
        string xml = get(sheet);
        string rels_path = dir_name(sheet) + "/_rels/" + base_name(sheet) + ".rels";
        auto rels = rels_of(rels_path);
        std::map<string, string> links;                    // cell -> hyperlink target
        for (auto& h : elements(xml, "hyperlink")) {
            string target = rels[attr(h.first, "r:id")];
            if (!target.empty()) links[attr(h.first, "ref")] = target;
        }
        std::map<string, bool> seen;
        for (auto& c : elements(xml, "c")) {
            string ref = attr(c.first, "r"), type = attr(c.first, "t"), value;
            seen[ref] = true;
            if (type == "inlineStr") {
                value = texts_of(c.second);
            } else {
                auto v = elements(c.second, "v");
                string raw = v.empty() ? string() : xml_unescape(v[0].second);
                if (type == "s") {
                    long long idx = to_ll(raw, -1);
                    value = idx >= 0 && idx < (long long)shared.size() ? shared[(size_t)idx] : string();
                } else {
                    value = raw;
                }
            }
            if (is_link(value)) found.push_back(trim(value));
            else if (links.count(ref) && is_link(links[ref])) found.push_back(trim(links[ref]));
        }
        for (auto& l : links)                              // a link on a cell without a value
            if (!seen.count(l.first) && is_link(l.second)) found.push_back(trim(l.second));
    }
    mz_zip_reader_end(&z);
    return found;
}

}  // namespace ec
