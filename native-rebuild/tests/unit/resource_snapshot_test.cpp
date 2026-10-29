#include "apps/shell/resource_snapshot.h"

#include <iostream>
#include <string>
#include <vector>

namespace {
using lume::shell::resources::Aggregate;
using lume::shell::resources::ClassifyProcess;
using lume::shell::resources::Coverage;
using lume::shell::resources::ProcessSample;
using lume::shell::resources::ResourceRole;

bool passed = true;

void Check(bool condition, const char* message) {
    if (!condition) {
        passed = false;
        std::cerr << "FAIL: " << message << '\n';
    }
}

ProcessSample Process(unsigned long pid, unsigned long parentPid, const wchar_t* name,
                      const wchar_t* commandLine, unsigned long long workingSet) {
    ProcessSample sample;
    sample.pid = pid;
    sample.parentPid = parentPid;
    sample.name = name;
    sample.commandLine = commandLine;
    sample.workingSetBytes = workingSet;
    return sample;
}
}

int main() {
    Check(ClassifyProcess(L"msedgewebview2.exe", L"--type=gpu-process") ==
              ResourceRole::WebViewGpu,
          "WebView2 GPU role");
    Check(ClassifyProcess(L"msedgewebview2.exe", L"--type=renderer") ==
              ResourceRole::WebViewRenderer,
          "WebView2 renderer role");
    Check(ClassifyProcess(L"LumeMusicHost.exe", L"") == ResourceRole::MusicHost,
          "music host role");
    Check(ClassifyProcess(L"mpv.exe", L"") == ResourceRole::Wallpaper,
          "wallpaper role");

    const std::vector<ProcessSample> samples{
        Process(100, 0, L"LumeShell.exe", L"", 10),
        Process(101, 100, L"msedgewebview2.exe", L"--type=browser", 20),
        Process(102, 101, L"msedgewebview2.exe", L"--type=renderer", 30),
        Process(103, 101, L"msedgewebview2.exe", L"--type=gpu-process", 40),
        Process(104, 100, L"LumeMusicHost.exe", L"", 50),
        Process(105, 100, L"mpv.exe", L"", 60),
    };
    const auto snapshot = Aggregate(samples);
    Check(snapshot.nonWallpaper.workingSetBytes == 150,
          "non-wallpaper working set excludes mpv");
    Check(snapshot.wallpaper.workingSetBytes == 60,
          "wallpaper working set is separate");
    Check(snapshot.webview.processCount == 3, "all WebView2 helpers are grouped");
    Check(snapshot.music.processCount == 1, "music process is grouped");

    auto missing = samples;
    missing[2].workingSetBytes.reset();
    const auto partial = Aggregate(missing);
    Check(partial.nonWallpaper.coverage == Coverage::Partial,
          "missing process counters are partial");
    Check(!partial.nonWallpaper.workingSetBytes.has_value(),
          "partial aggregate does not replace missing bytes with zero");

    return passed ? 0 : 1;
}
