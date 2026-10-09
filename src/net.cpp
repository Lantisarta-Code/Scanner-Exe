#include "net.h"

#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace ec {

#ifdef _WIN32
const sock_t BAD_SOCK = (sock_t)INVALID_SOCKET;
#else
const sock_t BAD_SOCK = -1;
#endif

void net_init() {
#ifdef _WIN32
    static bool done = false;
    if (!done) {
        WSADATA w;
        WSAStartup(MAKEWORD(2, 2), &w);
        done = true;
    }
#else
    signal(SIGPIPE, SIG_IGN);
#endif
}

void sock_close(sock_t s) {
    if (s == BAD_SOCK) return;
#ifdef _WIN32
    closesocket((SOCKET)s);
#else
    close(s);
#endif
}
void sock_shutdown(sock_t s) {
#ifdef _WIN32
    shutdown((SOCKET)s, SD_BOTH);
#else
    shutdown(s, SHUT_RDWR);
#endif
}

static void set_nonblock(sock_t s, bool on) {
#ifdef _WIN32
    u_long v = on ? 1 : 0;
    ioctlsocket((SOCKET)s, FIONBIO, &v);
#else
    int fl = fcntl(s, F_GETFL, 0);
    fcntl(s, F_SETFL, on ? (fl | O_NONBLOCK) : (fl & ~O_NONBLOCK));
#endif
}

// 1 = ready, 0 = timeout, -1 = error
static int wait_sock(sock_t s, bool write, int timeout_ms) {
    fd_set fs;
    FD_ZERO(&fs);
#ifdef _WIN32
    FD_SET((SOCKET)s, &fs);
#else
    FD_SET(s, &fs);
#endif
    timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    int r = select((int)(s + 1), write ? nullptr : &fs, write ? &fs : nullptr, nullptr, &tv);
    return r > 0 ? 1 : (r == 0 ? 0 : -1);
}

int sock_recv(sock_t s, char* buf, int n, int timeout_ms) {
    int w = wait_sock(s, false, timeout_ms);
    if (w == 0) return -2;
    if (w < 0) return -1;
    int r = (int)recv(s, buf, n, 0);
    return r < 0 ? -1 : r;
}

bool sock_send_all(sock_t s, const string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        if (wait_sock(s, true, 30000) <= 0) return false;
#ifdef _WIN32
        int r = send((SOCKET)s, data.data() + sent, (int)(data.size() - sent), 0);
#else
        int r = (int)send(s, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
#endif
        if (r <= 0) return false;
        sent += (size_t)r;
    }
    return true;
}

static sock_t tcp_connect(const string& host, int port, int timeout_ms, string& err) {
    net_init();
    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res) {
        err = "cannot resolve " + host;
        return BAD_SOCK;
    }
    sock_t s = (sock_t)socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s == BAD_SOCK) {
        freeaddrinfo(res);
        err = "no socket";
        return BAD_SOCK;
    }
    set_nonblock(s, true);
    int r = connect(s, res->ai_addr, (int)res->ai_addrlen);
    freeaddrinfo(res);
    if (r != 0) {
        if (wait_sock(s, true, timeout_ms) <= 0) {
            sock_close(s);
            err = "connection timed out (" + host + ":" + std::to_string(port) + ")";
            return BAD_SOCK;
        }
        int so = 0;
        socklen_t len = sizeof so;
        getsockopt(s, SOL_SOCKET, SO_ERROR, (char*)&so, &len);
        if (so != 0) {
            sock_close(s);
            err = "connection refused (" + host + ":" + std::to_string(port) + ")";
            return BAD_SOCK;
        }
    }
    set_nonblock(s, false);
    return s;
}

sock_t tcp_listen(int port, string& err) {
    net_init();
    sock_t s = (sock_t)socket(AF_INET, SOCK_STREAM, 0);
    if (s == BAD_SOCK) {
        err = "no socket";
        return BAD_SOCK;
    }
#ifdef _WIN32
    BOOL ex = TRUE;                                           // nobody else may take the port meanwhile
    setsockopt((SOCKET)s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&ex, sizeof ex);
#else
    int one = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
#endif
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons((unsigned short)port);
    if (bind(s, (sockaddr*)&a, sizeof a) != 0 || listen(s, 64) != 0) {
#ifdef _WIN32
        int code = WSAGetLastError();
        err = code == WSAEADDRINUSE ? "the port is already in use" : "error " + std::to_string(code);
#else
        err = "the port is already in use";
#endif
        sock_close(s);
        return BAD_SOCK;
    }
    return s;
}

sock_t tcp_accept(sock_t l, int timeout_ms, string& peer_ip) {
    if (wait_sock(l, false, timeout_ms) <= 0) return BAD_SOCK;
    sockaddr_in a{};
    socklen_t len = sizeof a;
    sock_t c = (sock_t)accept(l, (sockaddr*)&a, &len);
    if (c == BAD_SOCK) return BAD_SOCK;
    char buf[64] = {0};
    inet_ntop(AF_INET, &a.sin_addr, buf, sizeof buf);
    peer_ip = buf;
    return c;
}

int free_local_port() {
    net_init();
    sock_t s = (sock_t)socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    bind(s, (sockaddr*)&a, sizeof a);
    socklen_t len = sizeof a;
    getsockname(s, (sockaddr*)&a, &len);
    int port = ntohs(a.sin_port);
    sock_close(s);
    return port;
}

vector<string> local_ipv4s() {
    net_init();
    vector<string> found, out;
    sock_t s = (sock_t)socket(AF_INET, SOCK_DGRAM, 0);
    if (s != BAD_SOCK) {
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(1);
        inet_pton(AF_INET, "10.255.255.255", &a.sin_addr);      // nothing is sent: it only picks the card
        if (connect(s, (sockaddr*)&a, sizeof a) == 0) {
            sockaddr_in me{};
            socklen_t len = sizeof me;
            if (getsockname(s, (sockaddr*)&me, &len) == 0) {
                char buf[64] = {0};
                inet_ntop(AF_INET, &me.sin_addr, buf, sizeof buf);
                found.push_back(buf);
            }
        }
        sock_close(s);
    }
    char host[256] = {0};
    if (gethostname(host, sizeof host - 1) == 0) {
        addrinfo hints{}, *res = nullptr;
        hints.ai_family = AF_INET;
        if (getaddrinfo(host, nullptr, &hints, &res) == 0) {
            for (auto* p = res; p; p = p->ai_next) {
                char buf[64] = {0};
                inet_ntop(AF_INET, &((sockaddr_in*)p->ai_addr)->sin_addr, buf, sizeof buf);
                found.push_back(buf);
            }
            freeaddrinfo(res);
        }
    }
    for (auto& ip : found) {
        if (ip.empty() || starts_with(ip, "127.") || ip == "0.0.0.0") continue;
        bool dup = false;
        for (auto& o : out) dup = dup || o == ip;
        if (!dup) out.push_back(ip);
    }
    return out;
}

string ssdp_search(const string& msg, int timeout_ms) {
    net_init();
    sock_t s = (sock_t)socket(AF_INET, SOCK_DGRAM, 0);
    if (s == BAD_SOCK) return "";
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(1900);
    inet_pton(AF_INET, "239.255.255.250", &a.sin_addr);
    sendto(s, msg.data(), (int)msg.size(), 0, (sockaddr*)&a, sizeof a);
    double end = now() + timeout_ms / 1000.0;
    string out;
    while (now() < end) {
        int left = (int)((end - now()) * 1000);
        if (left <= 0 || wait_sock(s, false, left) <= 0) break;
        char buf[4096];
        int n = (int)recvfrom(s, buf, sizeof buf, 0, nullptr, nullptr);
        if (n > 0) {
            string d(buf, (size_t)n);
            if (contains(lower_ascii(d), "location:")) {
                out = d;
                break;
            }
        }
    }
    sock_close(s);
    return out;
}

// ---------------------------------------------------------------- URLs / HTTP
Url parse_url(const string& url) {
    Url u;
    size_t p = url.find("://");
    if (p == string::npos) throw Error("bad address: " + url);
    u.scheme = lower_ascii(url.substr(0, p));
    string rest = url.substr(p + 3);
    size_t slash = rest.find('/');
    string hostport = slash == string::npos ? rest : rest.substr(0, slash);
    u.path = slash == string::npos ? "/" : rest.substr(slash);
    size_t q = hostport.find('?');
    if (q != string::npos) {                                     // "http://host?x" (no path)
        u.path = "/" + hostport.substr(q);
        hostport = hostport.substr(0, q);
    }
    u.port = u.scheme == "https" ? 443 : 80;
    size_t colon = hostport.rfind(':');
    if (colon != string::npos && hostport.find(']') == string::npos) {
        u.port = (int)to_ll(hostport.substr(colon + 1), u.port);
        hostport = hostport.substr(0, colon);
    }
    u.host = hostport;
    return u;
}

static string resolve_location(const string& base, const string& loc) {
    if (contains(loc, "://")) return loc;
    Url b = parse_url(base);
    string root = b.scheme + "://" + b.host + ((b.port == 80 || b.port == 443) ? "" : ":" + std::to_string(b.port));
    if (starts_with(loc, "/")) return root + loc;
    string dir = b.path.substr(0, b.path.rfind('/') + 1);
    return root + dir + loc;
}

static HttpResponse http_plain(const string& method, const Url& u, const string& body, const Headers& headers,
                               int timeout_ms, const Progress& progress) {
    string err;
    sock_t s = tcp_connect(u.host, u.port, std::min(timeout_ms, 15000), err);
    if (s == BAD_SOCK) throw Error(err);
    string req = method + " " + u.path + " HTTP/1.1\r\nHost: " + u.host +
                 (u.port != 80 ? ":" + std::to_string(u.port) : "") +
                 "\r\nConnection: close\r\nAccept-Encoding: identity\r\nUser-Agent: Mozilla/5.0 (Windows NT 6.1) EasyCentralBot\r\n";
    bool has_ct = false;
    for (auto& h : headers) {
        req += h.first + ": " + h.second + "\r\n";
        has_ct = has_ct || lower_ascii(h.first) == "content-type";
    }
    if (method == "POST" || method == "PUT" || !body.empty()) {
        if (!has_ct) req += "Content-Type: application/json; charset=utf-8\r\n";
        req += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    }
    req += "\r\n" + body;
    if (!sock_send_all(s, req)) {
        sock_close(s);
        throw Error("could not send to " + u.host);
    }
    string raw;
    HttpResponse r;
    size_t head_end = string::npos;
    long long clen = -1;
    bool chunked = false;
    char buf[65536];
    double deadline = now() + timeout_ms / 1000.0;
    while (true) {
        int left = (int)((deadline - now()) * 1000);
        if (left <= 0) {
            sock_close(s);
            throw TimeoutError("timed out waiting for " + u.host);
        }
        int n = sock_recv(s, buf, sizeof buf, left);
        if (n == -2) continue;
        if (n <= 0) break;
        raw.append(buf, (size_t)n);
        if (head_end == string::npos) {
            head_end = raw.find("\r\n\r\n");
            if (head_end != string::npos) {
                auto lines = split(raw.substr(0, head_end), '\n');
                if (!lines.empty()) {
                    auto parts = split(trim(lines[0]), ' ');
                    if (parts.size() >= 2) r.status = (int)to_ll(parts[1], 0);
                }
                for (size_t i = 1; i < lines.size(); i++) {
                    size_t c = lines[i].find(':');
                    if (c == string::npos) continue;
                    r.headers[lower_ascii(trim(lines[i].substr(0, c)))] = trim(lines[i].substr(c + 1));
                }
                if (r.headers.count("content-length")) clen = to_ll(r.headers["content-length"], -1);
                chunked = contains(lower_ascii(r.headers["transfer-encoding"]), "chunked");
            }
        }
        if (head_end != string::npos) {
            unsigned long long got = raw.size() - head_end - 4;
            if (progress && clen > 0) progress(got, (unsigned long long)clen);
            if (clen >= 0 && (long long)got >= clen) break;
            if (chunked && ends_with(raw, "0\r\n\r\n")) break;
        }
    }
    sock_close(s);
    if (head_end == string::npos) throw Error("no answer from " + u.host);
    string data = raw.substr(head_end + 4);
    if (chunked) {
        string out;
        size_t p = 0;
        while (p < data.size()) {
            size_t e = data.find("\r\n", p);
            if (e == string::npos) break;
            long long len = std::strtoll(data.substr(p, e - p).c_str(), nullptr, 16);
            if (len <= 0) break;
            out += data.substr(e + 2, (size_t)len);
            p = e + 2 + (size_t)len + 2;
        }
        data = out;
    } else if (clen >= 0 && (long long)data.size() > clen) {
        data.resize((size_t)clen);
    }
    r.body = data;
    return r;
}

#ifdef _WIN32
static HttpResponse http_winhttp(const string& method, const string& url, const string& body,
                                 const Headers& headers, int timeout_ms, const Progress& progress) {
    std::wstring wurl = widen(url);
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof uc;
    wchar_t host[512], path[8192];
    uc.lpszHostName = host;
    uc.dwHostNameLength = 512;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 8192;
    wchar_t extra[8192];
    uc.lpszExtraInfo = extra;
    uc.dwExtraInfoLength = 8192;
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) throw Error("bad address: " + url);
    std::wstring full_path = std::wstring(path, uc.dwUrlPathLength) + std::wstring(extra, uc.dwExtraInfoLength);
    HINTERNET ses = WinHttpOpen(L"Mozilla/5.0 (Windows NT 6.1) EasyCentralBot", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) throw Error("WinHTTP could not start");
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1 |
                      WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;      // Windows 7 needs update KB3140245 for TLS 1.2
    if (!WinHttpSetOption(ses, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols)) {
        protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1;
        WinHttpSetOption(ses, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols);
    }
    WinHttpSetTimeouts(ses, 15000, 15000, timeout_ms, timeout_ms);
    HINTERNET con = WinHttpConnect(ses, std::wstring(host, uc.dwHostNameLength).c_str(), uc.nPort, 0);
    HINTERNET req = con ? WinHttpOpenRequest(con, widen(method).c_str(), full_path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                             WINHTTP_DEFAULT_ACCEPT_TYPES,
                                             uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                        : nullptr;
    auto cleanup = [&] {
        if (req) WinHttpCloseHandle(req);
        if (con) WinHttpCloseHandle(con);
        WinHttpCloseHandle(ses);
    };
    if (!req) {
        cleanup();
        throw Error("could not connect to " + narrow(std::wstring(host, uc.dwHostNameLength)));
    }
    std::wstring hdrs;
    for (auto& h : headers) hdrs += widen(h.first + ": " + h.second + "\r\n");
    BOOL ok = WinHttpSendRequest(req, hdrs.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : hdrs.c_str(), (DWORD)-1L,
                                 body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data(), (DWORD)body.size(),
                                 (DWORD)body.size(), 0) &&
              WinHttpReceiveResponse(req, nullptr);
    if (!ok) {
        DWORD e = GetLastError();
        cleanup();
        if (e == 12175 /* ERROR_WINHTTP_SECURE_FAILURE */ || e == 12157 /* SECURE_CHANNEL_ERROR */)
            throw Error(T("drv_ssl") + " (WinHTTP " + std::to_string(e) + ")");
        if (e == ERROR_WINHTTP_TIMEOUT) throw TimeoutError("timed out: " + url);
        throw Error("could not download (WinHTTP error " + std::to_string(e) + ")");
    }
    HttpResponse r;
    DWORD code = 0, sz = sizeof code;
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code,
                        &sz, WINHTTP_NO_HEADER_INDEX);
    r.status = (int)code;
    unsigned long long total = 0;
    wchar_t lenbuf[64];
    DWORD lensz = sizeof lenbuf;
    if (WinHttpQueryHeaders(req, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, lenbuf, &lensz,
                            WINHTTP_NO_HEADER_INDEX))
        total = (unsigned long long)_wtoi64(lenbuf);
    while (true) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(req, &avail)) {
            cleanup();
            throw Error("the download was cut off");
        }
        if (!avail) break;
        string chunk(avail, '\0');
        DWORD got = 0;
        if (!WinHttpReadData(req, &chunk[0], avail, &got)) {
            cleanup();
            throw Error("the download was cut off");
        }
        r.body.append(chunk.data(), got);
        if (progress && total) progress(r.body.size(), total);
    }
    cleanup();
    return r;
}
#endif

HttpResponse http_request(const string& method, const string& url, const string& body, const Headers& headers,
                          int timeout_ms, Progress progress) {
    string cur = url;
    for (int hop = 0; hop < 6; hop++) {
        Url u = parse_url(cur);
        HttpResponse r;
        if (u.scheme == "https") {
#ifdef _WIN32
            return http_winhttp(method, cur, body, headers, timeout_ms, progress);   // follows redirects itself
#else
            throw Error("https is not available in this test build: " + cur);
#endif
        }
        r = http_plain(method, u, body, headers, timeout_ms, progress);
        if ((r.status == 301 || r.status == 302 || r.status == 303 || r.status == 307 || r.status == 308) &&
            r.headers.count("location") && method == "GET") {
            cur = resolve_location(cur, r.headers["location"]);
            continue;
        }
        return r;
    }
    throw Error("too many redirects: " + url);
}

string http_text(const string& url, int timeout_ms) {
    HttpResponse r = http_request("GET", url, "", {}, timeout_ms);
    if (r.status >= 400) throw HttpError(r.status, "HTTP " + std::to_string(r.status));
    return r.body;
}

void http_download(const string& url, const string& dest, Progress progress) {
    HttpResponse r = http_request("GET", url, "", {}, 300000, progress);
    if (r.status >= 400) throw HttpError(r.status, "HTTP " + std::to_string(r.status));
    if (!write_file(dest, r.body)) throw Error("cannot write " + dest);
}

// ---------------------------------------------------------------- programs
#ifdef _WIN32
static std::wstring quote_arg(const std::wstring& a) {
    if (!a.empty() && a.find_first_of(L" \t\"") == std::wstring::npos) return a;
    std::wstring out = L"\"";
    size_t bs = 0;
    for (wchar_t c : a) {
        if (c == L'\\') {
            bs++;
        } else if (c == L'"') {
            out.append(bs * 2 + 1, L'\\');
            out += c;
            bs = 0;
        } else {
            out.append(bs, L'\\');
            out += c;
            bs = 0;
        }
    }
    out.append(bs * 2, L'\\');
    return out + L"\"";
}
static std::wstring command_line(const vector<string>& argv) {
    std::wstring cl;
    for (size_t i = 0; i < argv.size(); i++) {
        if (i) cl += L" ";
        cl += quote_arg(widen(argv[i]));
    }
    return cl;
}
Process::~Process() {
    if (handle_) CloseHandle((HANDLE)handle_);
}
bool Process::start(const vector<string>& argv, string& err) {
    std::wstring cl = command_line(argv);
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(widen(argv[0]).c_str(), &cl[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                        &si, &pi)) {
        err = "cannot start " + argv[0] + " (error " + std::to_string(GetLastError()) + ")";
        return false;
    }
    CloseHandle(pi.hThread);
    handle_ = pi.hProcess;
    started_ = true;
    return true;
}
bool Process::running(int* exit_code) {
    if (!handle_) return false;
    DWORD code = 0;
    if (WaitForSingleObject((HANDLE)handle_, 0) == WAIT_TIMEOUT) return true;
    GetExitCodeProcess((HANDLE)handle_, &code);
    if (exit_code) *exit_code = (int)code;
    return false;
}
void Process::kill() {
    if (handle_ && running()) {
        TerminateProcess((HANDLE)handle_, 1);
        WaitForSingleObject((HANDLE)handle_, 5000);
    }
}
string run_capture(const vector<string>& argv, int timeout_ms, int* exit_code) {
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) throw Error("no pipe");
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wr;
    si.hStdError = wr;
    si.hStdInput = nullptr;
    PROCESS_INFORMATION pi{};
    std::wstring cl = command_line(argv);
    if (!CreateProcessW(widen(argv[0]).c_str(), &cl[0], nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si,
                        &pi)) {
        DWORD e = GetLastError();
        CloseHandle(rd);
        CloseHandle(wr);
        throw Error("cannot run " + argv[0] + " (error " + std::to_string(e) + ")");
    }
    CloseHandle(wr);
    CloseHandle(pi.hThread);
    string out;
    double end = now() + timeout_ms / 1000.0;
    while (now() < end) {
        DWORD avail = 0;
        if (!PeekNamedPipe(rd, nullptr, 0, nullptr, &avail, nullptr)) break;   // the program has finished
        if (avail) {
            char buf[4096];
            DWORD got = 0;
            if (!ReadFile(rd, buf, sizeof buf, &got, nullptr) || !got) break;
            out.append(buf, got);
        } else if (WaitForSingleObject(pi.hProcess, 50) == WAIT_OBJECT_0) {
            DWORD a2 = 0;
            if (!PeekNamedPipe(rd, nullptr, 0, nullptr, &a2, nullptr) || !a2) break;
        }
    }
    if (WaitForSingleObject(pi.hProcess, 0) == WAIT_TIMEOUT) TerminateProcess(pi.hProcess, 1);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    if (exit_code) *exit_code = (int)code;
    CloseHandle(pi.hProcess);
    CloseHandle(rd);
    return out;
}
#else
Process::~Process() {}
bool Process::start(const vector<string>& argv, string& err) {
    pid_t pid = fork();
    if (pid < 0) {
        err = "cannot start " + argv[0];
        return false;
    }
    if (pid == 0) {
        int nul = open("/dev/null", O_RDWR);
        dup2(nul, 0);
        dup2(nul, 1);
        dup2(nul, 2);
        setpgid(0, 0);
        vector<char*> a;
        for (auto& s : argv) a.push_back(const_cast<char*>(s.c_str()));
        a.push_back(nullptr);
        execv(argv[0].c_str(), a.data());
        _exit(127);
    }
    pid_ = pid;
    started_ = true;
    return true;
}
bool Process::running(int* exit_code) {
    if (pid_ <= 0) return false;
    if (!done_) {
        int st = 0;
        pid_t r = waitpid(pid_, &st, WNOHANG);
        if (r == 0) return true;
        done_ = true;
        code_ = WIFEXITED(st) ? WEXITSTATUS(st) : 1;
    }
    if (exit_code) *exit_code = code_;
    return false;
}
void Process::kill() {
    if (pid_ > 0 && running()) {
        ::kill(-pid_, SIGTERM);
        for (int i = 0; i < 50 && running(); i++) sleep_ms(100);
        if (running()) ::kill(-pid_, SIGKILL);
        running();
    }
}
string run_capture(const vector<string>& argv, int timeout_ms, int* exit_code) {
    int fds[2];
    if (pipe(fds) != 0) throw Error("no pipe");
    pid_t pid = fork();
    if (pid == 0) {
        dup2(fds[1], 1);
        dup2(fds[1], 2);
        close(fds[0]);
        vector<char*> a;
        for (auto& s : argv) a.push_back(const_cast<char*>(s.c_str()));
        a.push_back(nullptr);
        execv(argv[0].c_str(), a.data());
        _exit(127);
    }
    close(fds[1]);
    string out;
    double end = now() + timeout_ms / 1000.0;
    char buf[4096];
    while (now() < end) {
        fd_set fs;
        FD_ZERO(&fs);
        FD_SET(fds[0], &fs);
        timeval tv{0, 100000};
        if (select(fds[0] + 1, &fs, nullptr, nullptr, &tv) > 0) {
            ssize_t n = read(fds[0], buf, sizeof buf);
            if (n <= 0) break;
            out.append(buf, (size_t)n);
        }
    }
    close(fds[0]);
    int st = 0;
    if (waitpid(pid, &st, WNOHANG) == 0) {
        ::kill(pid, SIGKILL);
        waitpid(pid, &st, 0);
    }
    if (exit_code) *exit_code = WIFEXITED(st) ? WEXITSTATUS(st) : 1;
    if (WIFEXITED(st) && WEXITSTATUS(st) == 127 && out.empty()) throw Error("cannot run " + argv[0]);
    return out;
}
#endif

}  // namespace ec
