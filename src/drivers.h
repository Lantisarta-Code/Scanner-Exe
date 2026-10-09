// Downloads msedgedriver.exe / chromedriver.exe that matches the installed browser.
// Edge 109 (the last Edge for Windows 7 / 8 / 8.1) has no driver at Microsoft any more, so the NuGet package
// "Selenium.WebDriver.MSEdgeDriver" (it contains Microsoft's driver) is the fallback source.
#pragma once
#include "common.h"

namespace ec {

struct DriverError : Error {
    explicit DriverError(const string& m) : Error(m) {}
};
struct DriverLocked : DriverError {
    explicit DriverLocked(const string& m) : DriverError(m) {}
};

extern vector<string> EDGE_DRIVER_HOSTS;
extern string NUGET_EDGE, NUGET_EDGE_V2, CHROME_STORAGE, CHROME_TESTING;

vector<int> version_tuple(const string& v);
string installed_browser_version(const string& kind);
string driver_arch();
string extract_driver(const string& archive, const string& name, const string& dest_dir);
string install_driver(const string& kind, const string& dest_dir, const string& version, const string& file,
                      const std::function<void(const string&)>& log);
int driver_cli(const vector<string>& args, const std::function<void(const string&)>& say);

// Excel: every cell text / hyperlink that starts with http, in sheet order
vector<string> xlsx_links(const string& path);

}  // namespace ec
