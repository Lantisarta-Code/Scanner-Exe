// Price Splitter: walks an Amazon search link down its price range so every saved link has <= limit results.
#pragma once
#include <optional>

#include "webdriver.h"

namespace ec {

using Emit = std::function<void(const json&)>;             // events for the window (same as the Python queue)

struct NoPriceError : Error {
    NoPriceError() : Error("no price") {}
};
struct MinPriceError : Error {
    long long price;
    explicit MinPriceError(long long p) : Error("min price"), price(p) {}
};

struct UrlParts {
    string scheme, netloc, path, query, fragment;
};
UrlParts urlsplit(const string& url);
vector<std::pair<string, string>> parse_qsl(const string& query);
string quote_all(const string& s);                         // urllib.parse.quote(s, safe="")
string unquote_plus(const string& s);

std::pair<std::optional<long long>, std::optional<long long>> parse_price_bounds(const string& url);
string build_url(const string& base_url, long long low, long long high);
bool parse_result_count(const string& text, long long& n, bool& over);
string range_from_url(const string& url);

void fill_info(const vector<string>& urls, int wait_s, const string& browser, const string& opera_path,
               const Emit& emit, const std::atomic<bool>& stop);
void run_splitter(const string& start_url, long long limit, int wait_s, const string& browser,
                  const string& opera_path, std::optional<long long> start_price, const Emit& emit,
                  const std::atomic<bool>& stop, std::optional<long long> min_price);

// for the tests: the page the splitter reads (Page by default)
struct CountReader {
    virtual ~CountReader() = default;
    virtual std::pair<long long, bool> read_count(const string& url) = 0;
    virtual string page_name() = 0;
};
void run_splitter_with(CountReader& reader, const string& start_url, long long limit,
                       std::optional<long long> start_price, const Emit& emit, const std::atomic<bool>& stop,
                       std::optional<long long> min_price);

}  // namespace ec
