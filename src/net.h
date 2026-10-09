// Network and process helpers (Windows: Winsock + WinHTTP + CreateProcess; Linux: for the tests).
#pragma once
#include "common.h"

namespace ec {

void net_init();

struct Url {
    string scheme, host, path = "/";                       // path includes the query
    int port = 80;
};
Url parse_url(const string& url);

struct HttpResponse {
    int status = 0;
    string body;
    std::map<string, string> headers;                     // names in lower case
};
struct HttpError : Error {
    int status;
    HttpError(int s, const string& m) : Error(m), status(s) {}
};

using Headers = vector<std::pair<string, string>>;
using Progress = std::function<void(unsigned long long done, unsigned long long total)>;

// One request. http:// goes over a plain socket (also used for the browser driver on this computer);
// https:// uses WinHTTP on Windows. Redirects are followed. Throws Error when nothing could be read.
HttpResponse http_request(const string& method, const string& url, const string& body = "",
                          const Headers& headers = {}, int timeout_ms = 60000, Progress progress = nullptr);
string http_text(const string& url, int timeout_ms = 60000);   // GET; throws HttpError on 4xx / 5xx
void http_download(const string& url, const string& dest, Progress progress = nullptr);

// ---- sockets for the phone server ----
#ifdef _WIN32
using sock_t = uintptr_t;
#else
using sock_t = int;
#endif
extern const sock_t BAD_SOCK;
void sock_close(sock_t s);
sock_t tcp_listen(int port, string& err);                  // all network cards
sock_t tcp_accept(sock_t listener, int timeout_ms, string& peer_ip);   // BAD_SOCK on timeout
bool sock_send_all(sock_t s, const string& data);
int sock_recv(sock_t s, char* buf, int n, int timeout_ms); // >0 bytes, 0 closed, -1 error, -2 timeout
void sock_shutdown(sock_t s);
int free_local_port();
vector<string> local_ipv4s();
string ssdp_search(const string& msg, int timeout_ms);     // first answer, or ""

// ---- programs ----
class Process {
public:
    Process() = default;
    ~Process();
    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;
    bool start(const vector<string>& argv, string& err);    // no window, no input / output
    bool running(int* exit_code = nullptr);
    void kill();
    bool started() const { return started_; }

private:
    bool started_ = false;
#ifdef _WIN32
    void* handle_ = nullptr;
#else
    int pid_ = -1;
    int code_ = 0;
    bool done_ = false;
#endif
};
// Runs a program and returns what it printed (stdout + stderr). Throws Error when it cannot run.
string run_capture(const vector<string>& argv, int timeout_ms, int* exit_code = nullptr);

}  // namespace ec
