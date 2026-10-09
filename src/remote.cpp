#include "remote.h"

#include <algorithm>
#include <regex>

namespace ec {

static const double HELLO_TTL = 30, SESSION_TTL = 14 * 86400, POLL_S = 20;
static const size_t MAX_SESSIONS = 30, MAX_BODY = 600000, LOG_KEEP = 300;

// ---------------------------------------------------------------- hub
void RemoteHub::set_list(const string& name, const json& rows) {
    std::lock_guard<std::mutex> g(m_);
    auto it = lists_.find(name);
    if (it != lists_.end() && it->second.second == rows) return;
    long long v = it == lists_.end() ? 1 : it->second.first + 1;
    lists_[name] = {v, rows};
    bump();
}
void RemoteHub::set_status(const json& status) {
    std::lock_guard<std::mutex> g(m_);
    if (status != status_) {
        status_ = status;
        bump();
    }
}
void RemoteHub::add_log(const string& line) {
    std::lock_guard<std::mutex> g(m_);
    log_.push_back({++log_n_, line});
    while (log_.size() > LOG_KEEP) log_.pop_front();
    bump();
}
void RemoteHub::wake() {
    std::lock_guard<std::mutex> g(m_);
    bump();
}
json RemoteHub::state(const json& known0, double wait_s) {
    json known = known0.is_object() ? known0 : json::object();
    std::unique_lock<std::mutex> g(m_);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds((long long)(std::max(0.0, wait_s) * 1000));
    auto known_ver = [&]() -> long long {
        return known.contains("ver") && known["ver"].is_number_integer() ? known["ver"].get<long long>() : -1;
    };
    while (ver_ == known_ver() && std::chrono::steady_clock::now() < deadline) cv_.wait_until(g, deadline);
    json kl = known.contains("lists") && known["lists"].is_object() ? known["lists"] : json::object();
    long long after = 0;
    if (known.contains("log") && known["log"].is_number_integer()) after = known["log"].get<long long>();
    if (after > log_n_) after = 0;                         // the program was restarted: send the log again
    json lists = json::object();
    for (auto& kv : lists_) {
        bool same = kl.contains(kv.first) && kl[kv.first].is_number_integer() && kl[kv.first].get<long long>() == kv.second.first;
        if (!same) lists[kv.first] = {{"ver", kv.second.first}, {"rows", kv.second.second}};
    }
    json log = json::array();
    for (auto& l : log_)
        if (l.first > after) log.push_back(json::array({l.first, l.second}));
    return {{"ver", ver_}, {"now", now()}, {"status", status_}, {"lists", lists}, {"log", log}, {"log_n", log_n_}};
}
json RemoteHub::command(const json& req, double timeout_s) {
    auto c = std::make_shared<Command>();
    c->req = req;
    std::unique_lock<std::mutex> g(m_);
    commands_.push_back(c);
    bool ok = cv_.wait_for(g, std::chrono::milliseconds((long long)(timeout_s * 1000)), [&] { return c->done; });
    if (!ok) return {{"ok", false}, {"err", "busy"}};
    return c->result.is_null() ? json{{"ok", true}} : c->result;
}
bool RemoteHub::next_command(std::shared_ptr<Command>& cmd) {
    std::lock_guard<std::mutex> g(m_);
    if (commands_.empty()) return false;
    cmd = commands_.front();
    commands_.pop_front();
    return true;
}
void RemoteHub::finish(const std::shared_ptr<Command>& cmd, const json& result) {
    std::lock_guard<std::mutex> g(m_);
    cmd->result = result;
    cmd->done = true;
    cv_.notify_all();
}

// ---------------------------------------------------------------- guard
long long RemoteGuard::wait_for(const string& ip, double t) {
    if (t < 0) t = now();
    std::lock_guard<std::mutex> g(m_);
    double w = all_until_ - t;
    auto it = ips_.find(ip);
    if (it != ips_.end()) w = std::max(w, it->second.until - t);
    w = std::max(0.0, w);
    return (long long)(w + 0.999);
}
string RemoteGuard::failed(const string& ip, double t) {
    if (t < 0) t = now();
    std::lock_guard<std::mutex> g(m_);
    Rec& r = ips_[ip];
    r.fails++;
    if (r.fails >= 5) {
        r.fails = 0;
        r.until = t + std::min(3600.0, 60.0 * (double)(1LL << std::min(r.times, 20)));
        r.times++;
    }
    vector<double> keep;
    for (double f : fails_) if (t - f < 3600) keep.push_back(f);
    keep.push_back(t);
    fails_ = keep;
    if (fails_.size() >= 20) {
        fails_.clear();
        all_until_ = t + 3600;
        return "all";
    }
    return r.until > t ? "ip" : "";
}
void RemoteGuard::ok(const string& ip) {
    std::lock_guard<std::mutex> g(m_);
    ips_.erase(ip);
}

// ---------------------------------------------------------------- server
RemoteServer::RemoteServer(RemoteHub& hub, const string& pin, int port, EventFn on_event)
    : hub_(hub), pin_(pin), port_(port), on_event_(std::move(on_event)), guard_(new RemoteGuard()) {}

RemoteServer::~RemoteServer() { stop(); }

void RemoteServer::start() {
    string err;
    listener_ = tcp_listen(port_, err);
    if (listener_ == BAD_SOCK) throw Error(err);
    closed_ = false;
    accept_thread_ = std::thread([this] {
        while (!closed_) {
            string ip;
            sock_t c = tcp_accept(listener_, 300, ip);
            if (c == BAD_SOCK) continue;
            if (closed_) {
                sock_close(c);
                break;
            }
            {
                std::lock_guard<std::mutex> g(clients_m_);
                clients_.insert(c);
            }
            active_++;
            std::thread([this, c, ip] {
                serve_client(c, ip);
                {
                    std::lock_guard<std::mutex> g(clients_m_);
                    clients_.erase(c);
                }
                sock_close(c);
                active_--;
            }).detach();
        }
    });
}

void RemoteServer::stop() {
    bool was = closed_.exchange(true);
    {
        std::lock_guard<std::mutex> g(m_);
        sessions_.clear();
        hellos_.clear();
    }
    if (accept_thread_.joinable()) accept_thread_.join();
    if (listener_ != BAD_SOCK) {
        sock_close(listener_);
        listener_ = BAD_SOCK;
    }
    {
        std::lock_guard<std::mutex> g(clients_m_);
        for (auto c : clients_) sock_shutdown(c);
    }
    hub_.wake();                                           // phones that are waiting for news are let go
    for (int i = 0; i < 300 && active_ > 0; i++) sleep_ms(50);
    (void)was;
}

void RemoteServer::set_pin(const string& pin) {
    {
        std::lock_guard<std::mutex> g(m_);
        pin_ = pin;
        sessions_.clear();
        hellos_.clear();
    }
    hub_.wake();
}

int RemoteServer::phones(double within) {
    std::lock_guard<std::mutex> g(m_);
    double t = now();
    int n = 0;
    for (auto& s : sessions_) if (t - s.second->last < within) n++;
    return n;
}

static string http_response(int code, const string& body, const string& ctype, bool close) {
    string reason = code == 200 ? "OK" : code == 204 ? "No Content" : code == 400 ? "Bad Request"
                  : code == 413 ? "Payload Too Large" : code == 503 ? "Service Unavailable" : "Error";
    string h = "HTTP/1.1 " + std::to_string(code) + " " + reason + "\r\n";
    if (code == 204) {
        h += "Access-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: POST, GET, OPTIONS\r\n"
             "Access-Control-Allow-Headers: Content-Type\r\nAccess-Control-Allow-Private-Network: true\r\n"
             "Access-Control-Max-Age: 86400\r\n";
    } else {
        h += "Content-Type: " + ctype + "\r\nAccess-Control-Allow-Origin: *\r\nCache-Control: no-store\r\n";
    }
    h += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    h += close ? "Connection: close\r\n" : "Connection: keep-alive\r\n";
    return h + "\r\n" + body;
}

void RemoteServer::serve_client(sock_t c, const string& ip) {
    string buf;
    char tmp[16384];
    while (!closed_) {
        size_t he;
        while ((he = buf.find("\r\n\r\n")) == string::npos) {
            if (buf.size() > 16384) return;
            int n = sock_recv(c, tmp, sizeof tmp, 40000);
            if (n <= 0 || closed_) return;                 // idle for 40 s, closed, or switched off
            buf.append(tmp, (size_t)n);
        }
        auto lines = split(buf.substr(0, he), '\n');
        auto first = split(trim(lines.empty() ? string() : lines[0]), ' ');
        string method = first.empty() ? "" : first[0];
        bool http10 = first.size() > 2 && trim(first[2]) == "HTTP/1.0";
        std::map<string, string> hd;
        for (size_t i = 1; i < lines.size(); i++) {
            size_t col = lines[i].find(':');
            if (col != string::npos) hd[lower_ascii(trim(lines[i].substr(0, col)))] = trim(lines[i].substr(col + 1));
        }
        long long clen = hd.count("content-length") ? to_ll(hd["content-length"], -1) : 0;
        bool close = http10 || lower_ascii(hd["connection"]) == "close";
        buf.erase(0, he + 4);
        if (method == "OPTIONS") {
            if (!sock_send_all(c, http_response(204, "", "", close)) || close) return;
            continue;
        }
        if (method == "GET") {
            if (!sock_send_all(c, http_response(200, "EasyCentral Link Bot - phone server. Use the Android app to connect.",
                                                "text/plain; charset=utf-8", close)) || close)
                return;
            continue;
        }
        if (method != "POST" || clen <= 0 || clen > (long long)MAX_BODY) {
            sock_send_all(c, http_response(clen > (long long)MAX_BODY ? 413 : 400, R"({"err": "size"})",
                                           "application/json", true));
            return;
        }
        while ((long long)buf.size() < clen) {
            int n = sock_recv(c, tmp, sizeof tmp, 30000);
            if (n <= 0) return;
            buf.append(tmp, (size_t)n);
        }
        string body = buf.substr(0, (size_t)clen);
        buf.erase(0, (size_t)clen);
        json out;
        int code = 200;
        try {
            json msg = json::parse(body);
            if (!msg.is_object()) throw Error("not an object");
            try {
                out = handle(msg, ip);
            } catch (...) {
                out = {{"err", "bad"}};
            }
        } catch (...) {
            out = {{"err", "bad"}};
            code = 400;
        }
        if (closed_) {                                      // switched off meanwhile: say nothing, hang up
            sock_send_all(c, http_response(503, R"({"err": "off"})", "application/json", true));
            return;
        }
        if (!sock_send_all(c, http_response(code, out.dump(-1, ' ', false, json::error_handler_t::replace),
                                            "application/json", close)) || close)
            return;
    }
}

json RemoteServer::handle(const json& msg, const string& ip) {
    if (closed_) return {{"err", "off"}};
    string t = msg.value("t", "");
    if (t == "hello") return hello(msg, ip);
    if (t == "login") return login(msg, ip);
    if (t == "call") return call(msg, ip);
    return {{"err", "bad"}};
}

json RemoteServer::hello(const json& msg, const string& ip) {
    long long wait = guard_->wait_for(ip);
    if (wait) return {{"err", "locked"}, {"wait", wait}};
    BigInt A = BigInt::from_hex(msg.value("A", ""));
    if (A.mod(SRP_N()).is_zero()) return {{"err", "bad"}};
    double t = now();
    string pin;
    {
        std::lock_guard<std::mutex> g(m_);
        pin = pin_;
        for (auto it = hellos_.begin(); it != hellos_.end();) it = t - it->second.t > HELLO_TTL ? hellos_.erase(it) : std::next(it);
        if (hellos_.size() > 200) return {{"err", "locked"}, {"wait", 30}};
    }
    SrpHello h = srp_server_hello(pin);
    string id = to_hex(random_bytes(12));
    {
        std::lock_guard<std::mutex> g(m_);
        hellos_[id] = Hello{t, A, h.b, h.B, h.v, ip};
    }
    return {{"sid", id}, {"salt", to_hex(h.salt)}, {"B", h.B.to_hex()}};
}

json RemoteServer::login(const json& msg, const string& ip) {
    long long wait = guard_->wait_for(ip);
    if (wait) return {{"err", "locked"}, {"wait", wait}};
    Hello h;
    {
        std::lock_guard<std::mutex> g(m_);
        auto it = hellos_.find(msg.value("sid", ""));
        if (it == hellos_.end()) return {{"err", "session"}};
        h = it->second;
        hellos_.erase(it);
    }
    if (now() - h.t > HELLO_TTL) return {{"err", "session"}};
    string K = srp_server_key(h.A, h.b, h.B, h.v);
    auto proofs = srp_proofs(K, h.A, h.B);
    if (!same_text(to_hex(proofs.first), msg.value("M1", ""))) {
        string what = guard_->failed(ip);
        on_event_(what.empty() ? "wrong" : "lock_" + what, ip);
        return {{"err", "pin"}, {"wait", guard_->wait_for(ip)}};
    }
    guard_->ok(ip);
    string sid = to_hex(random_bytes(12));
    auto s = std::make_shared<Sess>();
    s->keys = Keys{hmac_sha256(K, "enc"), hmac_sha256(K, "mac")};
    s->last = now();
    s->ip = ip;
    {
        std::lock_guard<std::mutex> g(m_);
        double t = now();
        for (auto it = sessions_.begin(); it != sessions_.end();)
            it = t - it->second->last > SESSION_TTL ? sessions_.erase(it) : std::next(it);
        while (sessions_.size() >= MAX_SESSIONS) {
            auto oldest = std::min_element(sessions_.begin(), sessions_.end(),
                                           [](auto& a, auto& b) { return a.second->last < b.second->last; });
            sessions_.erase(oldest);
        }
        sessions_[sid] = s;
    }
    on_event_("login", ip);
    return {{"sid", sid}, {"M2", to_hex(proofs.second)}};
}

json RemoteServer::call(const json& msg, const string& ip) {
    string sid = msg.value("sid", "");
    std::shared_ptr<Sess> s;
    {
        std::lock_guard<std::mutex> g(m_);
        auto it = sessions_.find(sid);
        if (it == sessions_.end()) return {{"err", "session"}};
        s = it->second;
    }
    long long n;
    json req;
    try {
        if (!msg.contains("n") || !msg["n"].is_number_integer()) throw Error("number");
        n = msg["n"].get<long long>();
        if (n <= 0 || n >= (1LL << 62)) throw Error("number");
        req = unseal(s->keys, "C", (uint64_t)n, msg);
    } catch (...) {
        return {{"err", "bad"}};
    }
    {
        std::lock_guard<std::mutex> g(m_);                 // every message counts only once
        if (s->seen.count(n) || n <= s->top - 500) return {{"err", "replay"}};
        s->seen.insert(n);
        s->top = std::max(s->top, n);
        if (s->seen.size() > 1000)
            for (auto it = s->seen.begin(); it != s->seen.end();) it = *it <= s->top - 500 ? s->seen.erase(it) : std::next(it);
        s->last = now();
        s->ip = ip;
    }
    string op = req.is_object() ? req.value("op", "") : "";
    json out;
    if (op == "state") {
        bool wait = req.contains("wait") && req["wait"].is_boolean() && req["wait"].get<bool>();
        out = hub_.state(req.contains("known") ? req["known"] : json(), wait ? POLL_S : 0);
        std::lock_guard<std::mutex> g(m_);
        auto it = sessions_.find(sid);
        if (it == sessions_.end() || it->second != s) return {{"err", "session"}};   // logged out while waiting
    } else if (!op.empty()) {
        out = hub_.command(req);
    } else {
        out = {{"ok", false}, {"err", "bad"}};
    }
    return seal(s->keys, "S", (uint64_t)n, out);
}

// ---------------------------------------------------------------- helpers for the Phone tab
vector<string> local_addresses() { return local_ipv4s(); }

bool is_private_ip(const string& ip) {
    static const std::regex re(R"(^(\d+)\.(\d+)\.(\d+)\.(\d+)$)");
    std::smatch m;
    if (!std::regex_match(ip, m, re)) return false;
    long long a = to_ll(m[1].str()), b = to_ll(m[2].str());
    return a == 10 || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168) || (a == 100 && b >= 64 && b <= 127);
}

string public_ip(int timeout_ms) {
    static const std::regex re(R"(^\d+\.\d+\.\d+\.\d+$)");
    for (const char* url : {"http://api.ipify.org", "http://checkip.amazonaws.com", "http://ifconfig.me/ip"}) {
        try {
            string ip = trim(http_text(url, timeout_ms));
            if (std::regex_match(ip, re)) return ip;
        } catch (...) {
        }
    }
    return "";
}

static string join_url(const string& base, const string& rel) {
    if (contains(rel, "://")) return rel;
    Url b = parse_url(base);
    string root = b.scheme + "://" + b.host + ":" + std::to_string(b.port);
    if (starts_with(rel, "/")) return root + rel;
    return root + b.path.substr(0, b.path.rfind('/') + 1) + rel;
}

static string upnp_call(const string& control, const string& st, const string& action,
                        const vector<std::pair<string, string>>& args) {
    string inner;
    for (auto& a : args) inner += "<" + a.first + ">" + a.second + "</" + a.first + ">";
    string body = "<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
                  "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:" + action + " xmlns:u=\"" +
                  st + "\">" + inner + "</u:" + action + "></s:Body></s:Envelope>";
    HttpResponse r = http_request("POST", control, body,
                                  {{"Content-Type", "text/xml; charset=\"utf-8\""}, {"SOAPAction", "\"" + st + "#" + action + "\""}},
                                  8000);
    if (r.status >= 400) {
        static const std::regex re(R"(<errorDescription>([^<]*)</errorDescription>)");
        std::smatch m;
        string d = std::regex_search(r.body, m, re) ? m[1].str() : "";
        throw UpnpError(d.empty() ? "HTTP " + std::to_string(r.status) : d);
    }
    return r.body;
}

string upnp_open_port(int port, const string& local_ip, const string& location0) {
    string location = location0;
    if (location.empty()) {
        string ans = ssdp_search("M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 2\r\n"
                                 "ST: urn:schemas-upnp-org:device:InternetGatewayDevice:1\r\n\r\n", 3000);
        static const std::regex re(R"((?:^|\n)location:\s*(\S+))", std::regex::icase);
        std::smatch m;
        if (std::regex_search(ans, m, re)) location = m[1].str();
    }
    if (location.empty()) throw UpnpError("no modem answered");
    string xml = http_text(location, 8000);
    string control, st;
    for (const char* kind : {"WANIPConnection:2", "WANIPConnection:1", "WANPPPConnection:1"}) {
        string want = string("urn:schemas-upnp-org:service:") + kind;
        size_t i = xml.find(want);
        if (i == string::npos) continue;
        size_t end = xml.find("</service>", i);
        string part = xml.substr(i, end == string::npos ? string::npos : end - i);
        static const std::regex re(R"(<controlURL>\s*([^<\s]+)\s*</controlURL>)");
        std::smatch m;
        if (std::regex_search(part, m, re)) {
            control = join_url(location, m[1].str());
            st = want;
            break;
        }
    }
    if (control.empty()) throw UpnpError("no WAN service");
    string p = std::to_string(port);
    upnp_call(control, st, "AddPortMapping",
              {{"NewRemoteHost", ""}, {"NewExternalPort", p}, {"NewProtocol", "TCP"}, {"NewInternalPort", p},
               {"NewInternalClient", local_ip}, {"NewEnabled", "1"}, {"NewPortMappingDescription", "EasyCentral Bot"},
               {"NewLeaseDuration", "0"}});
    try {
        string r = upnp_call(control, st, "GetExternalIPAddress", {});
        static const std::regex re(R"(<NewExternalIPAddress>\s*([^<\s]*)\s*</NewExternalIPAddress>)");
        std::smatch m;
        return std::regex_search(r, m, re) ? m[1].str() : string();
    } catch (...) {
        return "";
    }
}

}  // namespace ec
