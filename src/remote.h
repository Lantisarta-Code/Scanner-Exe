// Phone access: this program is the server and the phone app connects straight to this computer.
// Same protocol as the Python version (SRP-6a log-in with the 6 digit code, encrypted and signed
// messages, each usable once, wrong codes locked out).
#pragma once
#include <condition_variable>
#include <deque>
#include <memory>
#include <set>
#include <thread>

#include "crypto.h"
#include "net.h"

namespace ec {

const int REMOTE_PORT = 8765;

// What the phones see (lists, log, status) and what they ask for (commands). Thread safe.
class RemoteHub {
public:
    struct Command {
        json req, result;
        bool done = false;
    };
    void set_list(const string& name, const json& rows);
    void set_status(const json& status);
    void add_log(const string& line);
    void wake();
    json state(const json& known, double wait_s);
    json command(const json& req, double timeout_s = 15);
    bool next_command(std::shared_ptr<Command>& cmd);     // for the window: one waiting request
    void finish(const std::shared_ptr<Command>& cmd, const json& result);

private:
    void bump() {
        ver_++;
        cv_.notify_all();
    }
    std::mutex m_;
    std::condition_variable cv_;
    long long ver_ = 1, log_n_ = 0;
    std::map<string, std::pair<long long, json>> lists_;
    json status_ = json::object();
    std::deque<std::pair<long long, string>> log_;
    std::deque<std::shared_ptr<Command>> commands_;
};

// Counts wrong codes: an address is locked after 5 (1 minute, twice as long each time, at most 1 hour);
// after 20 in an hour nobody can log in for an hour. Connected phones stay connected.
class RemoteGuard {
public:
    long long wait_for(const string& ip, double t = -1);
    string failed(const string& ip, double t = -1);       // "", "ip" or "all"
    void ok(const string& ip);

private:
    std::mutex m_;
    struct Rec {
        int fails = 0;
        double until = 0;
        int times = 0;
    };
    std::map<string, Rec> ips_;
    vector<double> fails_;
    double all_until_ = 0;
};

class RemoteServer {
public:
    using EventFn = std::function<void(const string& kind, const string& ip)>;
    RemoteServer(RemoteHub& hub, const string& pin, int port, EventFn on_event);
    ~RemoteServer();
    void start();                                          // throws Error when the port cannot be used
    void stop();
    void set_pin(const string& pin);
    int phones(double within = 60);
    int port() const { return port_; }
    json handle(const json& msg, const string& ip);        // one message (public for the tests)
    RemoteGuard& guard() { return *guard_; }
    void reset_guard() { guard_.reset(new RemoteGuard()); }

private:
    struct Hello {
        double t;
        BigInt A, b, B, v;
        string ip;
    };
    struct Sess {
        Keys keys;
        std::set<long long> seen;
        long long top = 0;
        double last = 0;
        string ip;
    };
    json hello(const json& msg, const string& ip);
    json login(const json& msg, const string& ip);
    json call(const json& msg, const string& ip);
    void serve_client(sock_t c, const string& ip);

    RemoteHub& hub_;
    string pin_;
    int port_;
    EventFn on_event_;
    std::unique_ptr<RemoteGuard> guard_;
    std::mutex m_;
    std::map<string, Hello> hellos_;
    std::map<string, std::shared_ptr<Sess>> sessions_;
    sock_t listener_ = BAD_SOCK;
    std::atomic<bool> closed_{false};
    std::thread accept_thread_;
    std::mutex clients_m_;
    std::set<sock_t> clients_;
    std::atomic<int> active_{0};
};

// ---- network helpers for the Phone tab ----
vector<string> local_addresses();
bool is_private_ip(const string& ip);
string public_ip(int timeout_ms = 8000);
struct UpnpError : Error {
    explicit UpnpError(const string& m) : Error(m) {}
};
string upnp_open_port(int port, const string& local_ip, const string& location = "");   // returns WAN address

}  // namespace ec
