// EasyCentral Scanner: submits one Amazon link to EasyCentral (steps 1-5) and picks the saved setting.
#pragma once
#include "webdriver.h"

namespace ec {

extern string TARGET_URL;                                  // https://app.easycentral.com/product-search/new
extern const char* FILTER_PLACEHOLDER;
extern const char* FILTER_HEADING;
extern const char* DEFAULT_SAVED_SETTING;

struct FilterError : Error {
    explicit FilterError(const string& m) : Error(m) {}
};

void select_saved_setting(Page& page, const string& option, long long wait_ms = 3000, double timeout_s = 45);
void scan_link(const string& url, const string& browser, const string& opera_path, int step_wait,
               const string& saved_setting);
void open_login_window(const string& feature, const string& browser, const string& opera_path, const string& url);

}  // namespace ec
