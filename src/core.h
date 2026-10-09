// Everything the program does, without the window: the link lists, the scanner timer, the splitter,
// the drivers and the phone server. The window (gui_win.cpp) and the tests (cli_main.cpp) sit on top.
// All methods are called from ONE thread (the window's); background work reports back through events.
#pragma once
#include <memory>
#include <optional>

#include "remote.h"
#include "splitter.h"

namespace ec {

json norm_entry(const json& x, bool saved = false);
vector<int> reorder(vector<json>& items, vector<int> idx, const string& where);

class Core {
public:
    Core();
    ~Core();
    void startup();                                        // after the window has set its hooks

    // ---- lists ----
    vector<json> pending, done, saved, repeat;
    vector<json>* list(const string& name);
    vector<string> row_values(int i, const json& e, const string& kind) const;

    // ---- settings (change them, then call settings_changed()) ----
    string lang = "en", scan_browser = "Chrome", split_browser = "Opera", opera_path, scan_filter, min_price,
           chromedriver, edgedriver;
    int interval_min = 30, repeat_days = 7, step_wait = 3;
    bool repeat_on = true, fill_on_start = true;
    string split_url, start_price;
    int limit = 9000, wait_s = 10;
    void settings_changed();

    // ---- state ----
    bool running = false, busy = false, splitting = false, drv_busy = false, net_busy = false;
    double next_run = 0;
    int interval_s = 1800;
    vector<string> log_lines;
    string env_text() const;
    string scan_status_text() const;

    // ---- hooks for the window ----
    std::function<void(const string&)> on_log;              // a new line in the log
    std::function<void()> on_lists;                        // the lists changed
    std::function<void()> on_status;                       // running / busy / splitting / settings changed
    std::function<void(const string& kind, const string& path)> on_driver;   // a driver was installed

    // ---- list actions (the same as the window's buttons) ----
    void log(const string& msg);
    void refresh();
    void save();
    void remove_indices(const string& name, vector<int> idx);
    void clear(const string& name);
    string copy_urls(const vector<int>& idx, bool everything);   // the text to put on the clipboard
    void move_saved_to_pending(const vector<int>& idx);          // empty = all
    void auto_transfer();
    void move_repeat_to_pending(const vector<int>& idx);
    void move_repeat_entries(vector<json*> entries);
    void done_to_repeat(const vector<int>& idx, bool everything);
    void move_done_back(const vector<int>& idx);
    int add_text(const string& text);
    std::pair<int, int> import_urls(const vector<string>& urls);   // added, skipped
    void revise(int i, const string& url);
    vector<int> move_rows(const string& name, const vector<int>& idx, const string& where);

    // ---- scanner ----
    void apply_interval(int minutes);
    void toggle();
    void login_scanner();
    void start_job();

    // ---- splitter ----
    string start_split();                                  // "" or the key of an error text
    string login_splitter();
    void start_fill(bool automatic);
    void stop_split();
    vector<string> missing_info(bool only_untried);

    // ---- drivers ----
    void get_driver(const string& kind, const string& file = "");

    // ---- phone ----
    bool remote_on = false;
    int remote_port = REMOTE_PORT;
    string remote_pin, remote_ext;
    RemoteHub hub;
    std::unique_ptr<RemoteServer> remote;
    bool remote_start();
    void remote_stop(bool quiet = false);
    void remote_toggle(bool on);
    string remote_apply(const string& pin, const string& port);   // "" or the key of an error text
    string remote_info_text();
    void remote_find_ext();
    void remote_upnp();
    void remote_pump();                                    // carries out what the phones asked for
    json remote_exec(const json& c);

    void tick();                                           // every 0.5 s: events, timer, phone status

    // for the tests: replace the browser work
    std::function<void(const string&, const string&, const string&, int, const string&)> scan_fn;
    std::function<void(const string&, long long, int, const string&, const string&, std::optional<long long>,
                       const Emit&, const std::atomic<bool>&, std::optional<long long>)>
        split_fn;

private:
    void sync_runtime();
    void handle_event(const json& ev);
    void remote_publish();
    json remote_status() const;
    void move_done_to_repeat(vector<json*> entries);

    json doc_;                                             // links.json as read (unknown keys are kept)
    vector<string> cleared_drivers_;
    EventQueue q_;
    std::shared_ptr<std::atomic<bool>> stop_ = std::make_shared<std::atomic<bool>>(false);
    double fill_at_ = 0, last_transfer_ = 0;
    bool started_ = false;
};

}  // namespace ec
