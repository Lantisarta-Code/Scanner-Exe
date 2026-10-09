# EasyCentral Link Bot - C++ version

The same program as `easycentral_bot.py`, written in C++ as a single `EasyCentralBot.exe`:
no Python, no Playwright, no packages to install. Runs on Windows 7, 8, 8.1, 10 and 11.

* EasyCentral Scanner (Pending / Done / Repeat, timer, "Otomasyon Filtresi" step, Excel import, move up / down)
* Price Splitter (with minimum price and name / result count filling)
* Phone tab: the Android app connects to this program exactly as it did to the Python version
* English / Turkish
* Uses the same data file as the Python version (`%USERPROFILE%\.easycentral_bot\links.json`), so your lists,
  settings, browser logins and the phone code carry over.

The browser is driven through `chromedriver.exe` (Chrome and Opera) or `msedgedriver.exe` (Edge) on every
Windows version - the "Download automatically" button gets the right one.
On Windows 7 the automatic download needs TLS 1.2 (Windows update KB3140245). If it fails, download the
driver with the browser and choose it with "Browse..." (a .zip or .nupkg file is fine).

## Getting the .exe (GitHub builds it, nothing to install)

1. Create a new repository on github.com (for example `EasyCentral-Desktop`).
2. "uploading an existing file": drag in everything from this folder (with or without the sub-folders) and
   "Commit changes".
3. Create the build instructions: open
   `https://github.com/<you>/<repository>/new/main?filename=.github/workflows/build.yml`,
   paste the content of `.github/workflows/build.yml` from this folder, "Commit changes".
4. "Actions" tab: "Build EasyCentralBot.exe" runs for 2-4 minutes.
5. "Releases" on the repository page: `EasyCentralBot.exe` (64-bit Windows) and `EasyCentralBot-32bit.exe`.

Command line (like the Python version):

    EasyCentralBot.exe --get-driver edge
    EasyCentralBot.exe --get-driver chrome --version 109
    EasyCentralBot.exe --get-driver edge --file C:\Users\me\Downloads\x.nupkg

## Files

* `src/` - the program (`gui_win.cpp` is the window, `core.cpp` the logic, `scanner.cpp`, `splitter.cpp`,
  `webdriver.cpp`, `drivers.cpp`, `remote.cpp` + `crypto.cpp` the phone server, `gen_strings.h` the texts)
* `third_party/` - nlohmann/json and miniz (both MIT licensed)
* `tools/gen_strings.py` - makes `gen_strings.h` from the Python program's texts
