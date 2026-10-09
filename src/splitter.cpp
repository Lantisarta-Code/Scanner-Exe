#include "splitter.h"

#include <cctype>
#include <cmath>
#include <regex>

namespace ec {

static const char* INFO_BAR = "[data-component-type=\"s-result-info-bar\"]";

// ---------------------------------------------------------------- links
UrlParts urlsplit(const string& url) {
    UrlParts p;
    string rest = url;
    size_t hash = rest.find('#');
    if (hash != string::npos) {
        p.fragment = rest.substr(hash + 1);
        rest = rest.substr(0, hash);
    }
    size_t q = rest.find('?');
    if (q != string::npos) {
        p.query = rest.substr(q + 1);
        rest = rest.substr(0, q);
    }
    size_t s = rest.find("://");
    if (s != string::npos) {
        p.scheme = rest.substr(0, s);
        rest = rest.substr(s + 3);
        size_t slash = rest.find('/');
        p.netloc = slash == string::npos ? rest : rest.substr(0, slash);
        p.path = slash == string::npos ? "" : rest.substr(slash);
    } else {
        p.path = rest;
    }
    return p;
}

string unquote_plus(const string& s) {
    string out;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if (c == '+') {
            out += ' ';
        } else if (c == '%' && i + 2 < s.size() && std::isxdigit((unsigned char)s[i + 1]) &&
                   std::isxdigit((unsigned char)s[i + 2])) {
            out += (char)std::stoi(s.substr(i + 1, 2), nullptr, 16);
            i += 2;
        } else {
            out += c;
        }
    }
    return out;
}

vector<std::pair<string, string>> parse_qsl(const string& query) {
    vector<std::pair<string, string>> out;
    for (auto& part : split(query, '&')) {
        if (part.empty()) continue;
        size_t eq = part.find('=');
        if (eq == string::npos) out.push_back({unquote_plus(part), ""});
        else out.push_back({unquote_plus(part.substr(0, eq)), unquote_plus(part.substr(eq + 1))});
    }
    return out;
}

string quote_all(const string& s) {
    static const char* hex = "0123456789ABCDEF";
    string out;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '_' || c == '.' || c == '-' || c == '~') {
            out += char(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

static long long float_to_ll(const string& s) {            // int(float("12.5")) -> 12
    string t = trim(s);
    char* end = nullptr;
    double d = std::strtod(t.c_str(), &end);
    if (t.empty() || !end || *end) throw Error("not a number: " + s);
    return (long long)std::trunc(d);
}

std::pair<std::optional<long long>, std::optional<long long>> parse_price_bounds(const string& url) {
    std::map<string, string> q;
    for (auto& kv : parse_qsl(urlsplit(url).query)) q[kv.first] = kv.second;
    std::optional<long long> low, high;
    static const std::regex re(R"(p_36:(\d*)-(\d*))");
    std::smatch m;
    string rh = q.count("rh") ? q["rh"] : "";
    if (std::regex_search(rh, m, re)) {
        if (m[1].length()) low = to_ll(m[1].str()) / 100;
        if (m[2].length()) high = to_ll(m[2].str()) / 100;
    }
    if (q.count("low-price") && !q["low-price"].empty()) low = float_to_ll(q["low-price"]);
    if (q.count("high-price") && !q["high-price"].empty()) high = float_to_ll(q["high-price"]);
    return {low, high};
}

string build_url(const string& base_url, long long low, long long high) {
    UrlParts parts = urlsplit(base_url);
    auto params = parse_qsl(parts.query);
    string path = parts.path, node;
    for (auto& kv : params)
        if (kv.first == "node") {
            node = kv.second;
            break;
        }
    bool converted = !node.empty() && !starts_with(path, "/s");    // category (/b) link -> search link
    vector<string> other_rh;
    if (converted) {
        path = "/s";
        other_rh.push_back("n:" + node);
    }
    vector<std::pair<string, string>> items;
    for (auto& kv : params) {
        const string& k = kv.first;
        if (k == "low-price" || k == "high-price" || k == "qid" || (converted && (k == "node" || k == "ie"))) continue;
        if (k == "rh") {
            for (auto& x : split(kv.second, ','))
                if (!x.empty() && !starts_with(x, "p_36:")) other_rh.push_back(x);
            continue;
        }
        items.push_back(kv);
    }
    string price = low > 0 ? "p_36:" + std::to_string(low * 100) + "-" + std::to_string(high * 100)
                           : "p_36:-" + std::to_string(high * 100);
    long long idx = (long long)items.size() - 1;
    for (size_t i = 0; i < items.size(); i++)
        if (items[i].first == "i") {
            idx = (long long)i;
            break;
        }
    other_rh.push_back(price);
    items.insert(items.begin() + (idx + 1), {"rh", join(other_rh, ",")});
    vector<string> enc;
    for (auto& kv : items) enc.push_back(quote_all(kv.first) + "=" + quote_all(kv.second));
    string out = parts.scheme.empty() ? string() : parts.scheme + "://";
    out += parts.netloc + path;
    string query = join(enc, "&");
    if (!query.empty()) out += "?" + query;
    return out;
}

bool parse_result_count(const string& text, long long& n, bool& over) {
    string t = collapse_ws(text);
    static const std::regex none(R"(\bno results\b)", std::regex::icase);
    if (std::regex_search(t, none)) {
        n = 0;
        over = false;
        return true;
    }
    static const std::regex re(R"((?:of\s+)?(over\s+)?([\d.,]+)\s+results?\b)", std::regex::icase);
    std::smatch m;
    if (!std::regex_search(t, m, re)) return false;
    string digits;
    for (char c : m[2].str()) if (c != '.' && c != ',') digits += c;
    if (digits.empty()) return false;
    n = to_ll(digits, 0);
    over = m[1].matched;
    return true;
}

string range_from_url(const string& url) {
    try {
        auto b = parse_price_bounds(url);
        if (!b.first && !b.second) return "";
        if (!b.second) return "$" + std::to_string(*b.first) + "+";
        return "$" + std::to_string(b.first ? *b.first : 0) + " - $" + std::to_string(*b.second);
    } catch (...) {
        return "";
    }
}

// ---------------------------------------------------------------- reading Amazon pages
static string clean_name(const string& s0) {
    string s = trim(s0);
    static const vector<string> quotes = {"\"", u8"“", u8"”"};
    bool changed = true;
    while (changed && !s.empty()) {
        changed = false;
        for (auto& q : quotes) {
            if (starts_with(s, q)) {
                s = s.substr(q.size());
                changed = true;
            }
            if (ends_with(s, q) && s.size() >= q.size()) {
                s = s.substr(0, s.size() - q.size());
                changed = true;
            }
        }
    }
    return collapse_ws(s);
}

class PageReader : public CountReader {
public:
    PageReader(Page& p, int wait_s, const Emit& emit) : page_(p), wait_s_(wait_s), emit_(emit) {}

    std::pair<long long, bool> read_count(const string& url) override {
        for (int attempt = 0; attempt < 3; attempt++) {
            page_.go(url);
            try {
                page_.wait_for_selector(INFO_BAR, 20000);
            } catch (const std::exception&) {
                string body;
                try {
                    body = lower_ascii(page_.body_text());
                } catch (...) {
                }
                if (contains(body, "captcha") || contains(body, "enter the characters") || contains(body, "robot")) {
                    emit_({{"k", "msg"}, {"key", "log_captcha"}, {"kw", json::object()}});
                    try {
                        page_.wait_for_selector(INFO_BAR, 300000);
                    } catch (...) {
                    }
                }
            }
            page_.wait((long long)wait_s_ * 1000);
            string text;
            try {
                text = page_.text(INFO_BAR, 5000);
            } catch (...) {
                try {
                    text = page_.body_text();
                } catch (...) {
                }
            }
            long long n;
            bool over;
            if (parse_result_count(text, n, over)) return {n, over};
        }
        throw Error("Could not read the result count from the Amazon page.");
    }

    string page_name() override {
        vector<std::function<string()>> tries = {
            [&] { return page_.text(string(INFO_BAR) + " span.a-color-state", 2000); },
            [&] {
                static const std::regex re(R"(results?\s+for\s+([^\n]+))", std::regex::icase);
                std::smatch m;
                string t = page_.text(INFO_BAR, 2000);
                return std::regex_search(t, m, re) ? m[1].str() : string();
            },
            [&] { return page_.value("#twotabsearchtextbox", 2000); },
            [&] { return page_.text("h1", 2000); },
            [&] { return page_.selected_text("#searchDropdownBox"); },
        };
        for (auto& f : tries) {
            try {
                string n = clean_name(f());
                if (!n.empty()) return n;
            } catch (...) {
            }
        }
        return "";
    }

private:
    Page& page_;
    int wait_s_;
    const Emit& emit_;
};

static json msg(const string& key, json kw = json::object()) { return {{"k", "msg"}, {"key", key}, {"kw", kw}}; }

void fill_info(const vector<string>& urls, int wait_s, const string& browser, const string& opera_path,
               const Emit& emit, const std::atomic<bool>& stop) {
    Session sess("splitter", browser, opera_path);
    PageReader reader(sess.page(), wait_s, emit);
    int total = (int)urls.size();
    for (int i = 0; i < total; i++) {
        if (stop) throw Interrupted();
        try {
            auto r = reader.read_count(urls[i]);
            emit({{"k", "info"}, {"url", urls[i]}, {"name", reader.page_name()}, {"n", r.first}, {"over", r.second},
                  {"i", i + 1}, {"total", total}});
        } catch (const Interrupted&) {
            throw;
        } catch (...) {
            emit({{"k", "info_fail"}, {"url", urls[i]}, {"i", i + 1}, {"total", total}});
        }
    }
    sess.close();
}

static std::pair<long long, std::optional<long long>> start_bounds(const string& start_url,
                                                                   std::optional<long long> start_price,
                                                                   std::optional<long long> min_price,
                                                                   long long& floor) {
    auto b = parse_price_bounds(start_url);
    std::optional<long long> low0 = b.first, high0 = b.second;
    if (!low0) {
        if (!start_price || *start_price == 0) throw NoPriceError();
        low0 = start_price;
        high0.reset();
    }
    floor = std::max<long long>(0, min_price ? *min_price : 0);   // the search never goes below this price
    if (floor && floor >= *low0) throw MinPriceError(*low0);
    return {*low0, high0};
}

void run_splitter_with(CountReader& reader, const string& start_url, long long limit,
                       std::optional<long long> start_price, const Emit& emit, const std::atomic<bool>& stop,
                       std::optional<long long> min_price) {
    long long floor = 0;
    auto sb = start_bounds(start_url, start_price, min_price, floor);
    long long low0 = sb.first;
    std::optional<long long> high0 = sb.second;
    int step = 0;
    string name;

    auto measure = [&](const string& url, long long lo, long long hi) {
        if (stop) throw Interrupted();
        step++;
        auto r = reader.read_count(url);
        if (name.empty()) {
            name = reader.page_name();
            if (!name.empty()) emit(msg("log_name", {{"name", name}}));
        }
        emit(msg("log_search", {{"step", std::to_string(step)}, {"lo", std::to_string(lo)}, {"hi", std::to_string(hi)},
                                {"res", fmt_res(r.first, r.second)}}));
        return r;
    };
    auto save = [&](const string& url, long long lo, long long hi, long long n, bool over, const string& note) {
        emit({{"k", "saved"}, {"url", url}, {"lo", lo}, {"hi", hi}, {"note", note}, {"n", n}, {"over", over},
              {"name", name}});
    };
    struct Prev {
        long long lo, hi, n;
        bool over;
        string url;
    };
    std::optional<Prev> prev;                              // last search of the current range below the limit
    long long high = low0, low = low0 - 1;

    if (high0) {                                           // search 1 = the link you put in
        auto r = measure(start_url, low0, *high0);
        if (r.first == limit) {
            save(start_url, low0, *high0, r.first, r.second, "");
        } else if (r.first < limit) {
            prev = Prev{low0, *high0, r.first, r.second, start_url};
            high = *high0;
        } else {
            emit(msg("log_start_over"));
        }
    }
    while (high > floor) {
        string url = build_url(start_url, low, high);
        auto r = measure(url, low, high);
        long long n = r.first;
        bool over = r.second;
        if (n == limit) {
            save(url, low, high, n, over, "");
            prev.reset();
            high = low;
            low = low - 1;
        } else if (n > limit) {
            if (prev) {
                save(prev->url, prev->lo, prev->hi, prev->n, prev->over, "");
                high = prev->lo;
            } else {
                save(url, low, high, n, over, "over_limit");
                high = low;
            }
            prev.reset();
            low = high - 1;
        } else {
            prev = Prev{low, high, n, over, url};
            if (low <= floor) {                            // reached the minimum price: save and stop
                save(url, low, high, n, over, floor ? "min_price" : "last_range");
                break;
            }
            low -= 1;
        }
    }
    if (floor) emit(msg("log_min_reached", {{"p", std::to_string(floor)}}));
}

void run_splitter(const string& start_url, long long limit, int wait_s, const string& browser,
                  const string& opera_path, std::optional<long long> start_price, const Emit& emit,
                  const std::atomic<bool>& stop, std::optional<long long> min_price) {
    long long floor = 0;
    start_bounds(start_url, start_price, min_price, floor);   // the checks come before a browser is opened
    Session sess("splitter", browser, opera_path);
    PageReader reader(sess.page(), wait_s, emit);
    run_splitter_with(reader, start_url, limit, start_price, emit, stop, min_price);
    sess.close();
}

}  // namespace ec
