// Test program (Linux): runs the parts of the bot without the window, so they can be checked automatically.
#include <cstdio>
#include <iostream>
#include <thread>

#include "core.h"
#include "drivers.h"
#include "scanner.h"

using namespace ec;

static void setup_runtime() {
    g_rt.headless = env("ECBOT_HEADLESS") != "0";
    g_rt.extra_args = {"--no-sandbox", "--disable-gpu", "--disable-dev-shm-usage"};
    if (!env("ECBOT_CHROMEDRIVER").empty()) g_rt.chromedriver = env("ECBOT_CHROMEDRIVER");
}

int main(int argc, char** argv) {
    net_init();
    vector<string> a(argv + 1, argv + argc);
    if (a.empty()) {
        std::puts("modes: big | srp | scan | split | serve | get-driver | xlsx | url");
        return 2;
    }
    g_lang = env("ECBOT_LANG").empty() ? "en" : env("ECBOT_LANG");
    string mode = a[0];
    try {
        if (mode == "big") {                               // big <a> <b> <m>: a*b, a mod m, a^b mod m, a+b, a-b
            BigInt x = BigInt::from_hex(a[1]), y = BigInt::from_hex(a[2]), m = BigInt::from_hex(a[3]);
            std::printf("%s\n%s\n%s\n%s\n%s\n", x.mul(y).to_hex().c_str(), x.mod(m).to_hex().c_str(),
                        BigInt::modpow(x, y, m).to_hex().c_str(), x.add(y).to_hex().c_str(),
                        (x.cmp(y) >= 0 ? x.sub(y) : y.sub(x)).to_hex().c_str());
            return 0;
        }
        if (mode == "sha") {                               // sha <hexdata> <hexkey>
            std::printf("%s\n%s\n", to_hex(sha256(from_hex(a[1]))).c_str(),
                        to_hex(hmac_sha256(from_hex(a[2]), from_hex(a[1]))).c_str());
            return 0;
        }
        if (mode == "url") {                               // url <link> <low> <high>
            auto b = parse_price_bounds(a[1]);
            std::printf("%s\n%s\n%s\n%s\n", b.first ? std::to_string(*b.first).c_str() : "None",
                        b.second ? std::to_string(*b.second).c_str() : "None",
                        build_url(a[1], to_ll(a[2]), to_ll(a[3])).c_str(), range_from_url(a[1]).c_str());
            return 0;
        }
        if (mode == "count") {                             // count <text>
            long long n;
            bool over;
            if (parse_result_count(a[1], n, over)) std::printf("%lld %d\n", n, over ? 1 : 0);
            else std::puts("None");
            return 0;
        }
        if (mode == "reorder") {                           // reorder <letters> <where> <i,j,...>
            vector<json> items;
            for (char c : a[1]) items.push_back(string(1, c));
            vector<int> idx;
            for (auto& s : split(a[3], ',')) if (!s.empty()) idx.push_back((int)to_ll(s));
            auto r = reorder(items, idx, a[2]);
            string out;
            for (auto& j : items) out += j.get<string>();
            vector<string> rs;
            for (int i : r) rs.push_back(std::to_string(i));
            std::printf("%s %s\n", out.c_str(), join(rs, ",").c_str());
            return 0;
        }
        if (mode == "scan") {                              // scan <target> <link> <setting> <browser binary>
            setup_runtime();
            TARGET_URL = a[1];
            scan_link(a[2], "Opera", a.size() > 4 ? a[4] : "", (int)to_ll(env("ECBOT_STEP_WAIT"), 0), a.size() > 3 ? a[3] : "");
            std::puts("OK");
            return 0;
        }
        if (mode == "split") {                             // split <link> <limit> <start|-> <min|-> <browser binary>
            setup_runtime();
            std::atomic<bool> stop(false);
            std::optional<long long> sp, mp;
            if (a[3] != "-") sp = to_ll(a[3]);
            if (a[4] != "-") mp = to_ll(a[4]);
            Emit emit = [](const json& ev) { std::printf("%s\n", ev.dump().c_str()); std::fflush(stdout); };
            try {
                run_splitter(a[1], to_ll(a[2]), 0, "Opera", a[5], sp, emit, stop, mp);
                std::puts("{\"k\":\"end\"}");
            } catch (const NoPriceError&) {
                std::puts("{\"k\":\"no_price\"}");
            } catch (const MinPriceError& e) {
                std::printf("{\"k\":\"min_price\",\"p\":%lld}\n", e.price);
            }
            return 0;
        }
        if (mode == "get-driver") {                        // the download addresses can be pointed at a test server
            if (!env("ECBOT_EDGE_HOSTS").empty()) EDGE_DRIVER_HOSTS = split(env("ECBOT_EDGE_HOSTS"), ',');
            if (!env("ECBOT_NUGET").empty()) NUGET_EDGE = env("ECBOT_NUGET");
            if (!env("ECBOT_NUGET_V2").empty()) NUGET_EDGE_V2 = env("ECBOT_NUGET_V2");
            if (!env("ECBOT_CHROME_STORAGE").empty()) CHROME_STORAGE = env("ECBOT_CHROME_STORAGE");
            if (!env("ECBOT_CHROME_TESTING").empty()) CHROME_TESTING = env("ECBOT_CHROME_TESTING");
            vector<string> rest(a.begin() + 1, a.end());
            return driver_cli(rest, [](const string& s) { std::printf("%s\n", s.c_str()); std::fflush(stdout); });
        }
        if (mode == "xlsx") {
            std::printf("%s\n", json(xlsx_links(a[1])).dump().c_str());
            return 0;
        }
        if (mode == "serve") {                             // the program without its window; commands on stdin
            setup_runtime();
            Core core;
            vector<json> scanned;
            std::mutex sm;
            core.scan_fn = [&](const string& url, const string&, const string&, int, const string& setting) {
                {
                    std::lock_guard<std::mutex> g(sm);
                    scanned.push_back({url, setting});
                }
                sleep_ms(1200);
            };
            core.split_fn = [&](const string& url, long long limit, int wait, const string&, const string&,
                                std::optional<long long> sp, const Emit&, const std::atomic<bool>& stop,
                                std::optional<long long> mp) {
                {
                    std::lock_guard<std::mutex> g(sm);
                    scanned.push_back({"split", url, limit, wait, sp ? json(*sp) : json(nullptr), mp ? json(*mp) : json(nullptr)});
                }
                for (int i = 0; i < 100; i++) {
                    if (stop) throw Interrupted();
                    sleep_ms(100);
                }
            };
            core.on_log = [](const string& l) { std::fprintf(stderr, "%s\n", l.c_str()); };
            core.startup();
            std::mutex cm;
            vector<string> cmds;
            std::thread([&] {
                string line;
                while (std::getline(std::cin, line)) {
                    std::lock_guard<std::mutex> g(cm);
                    cmds.push_back(line);
                }
                std::lock_guard<std::mutex> g(cm);
                cmds.push_back("{\"cmd\":\"quit\"}");
            }).detach();
            int n = 0;
            while (true) {
                core.remote_pump();
                if (++n % 4 == 0) core.tick();
                vector<string> todo;
                {
                    std::lock_guard<std::mutex> g(cm);
                    todo.swap(cmds);
                }
                for (auto& l : todo) {
                    json c = json::parse(l);
                    string cmd = c.value("cmd", "");
                    json out = {{"ok", true}};
                    if (cmd == "quit") {
                        core.remote_stop(true);
                        std::printf("{\"bye\":true}\n");
                        std::fflush(stdout);
                        std::_Exit(0);
                    } else if (cmd == "add_desktop") {
                        core.pending.push_back(norm_entry(c["url"].get<string>()));
                        if (c.value("first", false)) std::rotate(core.pending.rbegin(), core.pending.rbegin() + 1, core.pending.rend());
                        core.refresh();
                    } else if (cmd == "set_pending") {
                        core.pending.clear();
                        for (auto& u : c["urls"]) core.pending.push_back(norm_entry(u.get<string>()));
                        core.refresh();
                    } else if (cmd == "remote_stop") {
                        core.remote_toggle(false);
                    } else if (cmd == "remote_start") {
                        core.remote_toggle(true);
                    } else if (cmd == "pin") {
                        out["err"] = core.remote_apply(c["pin"].get<string>(), std::to_string(core.remote_port));
                    } else if (cmd == "interval") {
                        core.apply_interval(c["m"].get<int>());
                    } else if (cmd == "reset_guard") {
                        if (core.remote) core.remote->reset_guard();
                    } else if (cmd == "move") {
                        vector<int> idx;
                        for (auto& i : c["idx"]) idx.push_back(i.get<int>());
                        out["idx"] = core.move_rows(c["list"].get<string>(), idx, c["where"].get<string>());
                    }
                    if (cmd == "dump" || true) {
                        std::lock_guard<std::mutex> g(sm);
                        auto urls = [](const vector<json>& v) {
                            json o = json::array();
                            for (auto& e : v) o.push_back(e);
                            return o;
                        };
                        out["pending"] = urls(core.pending);
                        out["done"] = urls(core.done);
                        out["repeat"] = urls(core.repeat);
                        out["saved"] = urls(core.saved);
                        out["running"] = core.running;
                        out["busy"] = core.busy;
                        out["splitting"] = core.splitting;
                        out["interval_s"] = core.interval_s;
                        out["filter"] = core.scan_filter;
                        out["pin"] = core.remote_pin;
                        out["port"] = core.remote_port;
                        out["remote"] = (bool)core.remote;
                        out["phones"] = core.remote ? core.remote->phones() : 0;
                        out["scanned"] = scanned;
                        out["log"] = core.log_lines;
                        out["info"] = core.remote_info_text();
                    }
                    std::printf("%s\n", out.dump().c_str());
                    std::fflush(stdout);
                }
                sleep_ms(30);
            }
        }
    } catch (const std::exception& e) {
        std::printf("ERROR: %s\n", e.what());
        return 1;
    }
    std::puts("unknown mode");
    return 2;
}
