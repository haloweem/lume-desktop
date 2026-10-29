#include "platform/windows/mpv_process_controller.h"
#include "vendor/nlohmann/json.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {
using Json = nlohmann::json;
using Controller = lume::platform::windows::MpvProcessController;
using lume::wallpaper::MpvCommand;
int failures = 0;
void Expect(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

struct TemporaryMedia {
    std::filesystem::path root = std::filesystem::temp_directory_path() /
        (L"lume-mpv-controller-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
         std::to_wstring(GetTickCount64()));
    std::filesystem::path executable = root / L"not-a-player.exe";
    std::filesystem::path media = root / L"wallpaper.mp4";

    TemporaryMedia() {
        std::filesystem::create_directories(root);
        std::ofstream(executable) << "An existing file is not a working mpv player.";
        std::ofstream(media) << "Test media.";
    }
    ~TemporaryMedia() {
        std::error_code error;
        // This directory contains only this test's flat, generated fixtures.
        for (const auto& entry : std::filesystem::directory_iterator(root, error)) {
            if (entry.is_regular_file(error)) std::filesystem::remove(entry.path(), error);
        }
        std::filesystem::remove(root, error);
    }
};

std::uint64_t Display() {
    const auto displays = lume::platform::windows::EnumerateMpvDisplays();
    Expect(!displays.empty(), "the controlled Win32 tests require one enumerated display");
    return displays.empty() ? 0 : displays.front().id;
}

struct VerificationWindow {
    HWND handle = CreateWindowExW(0, L"STATIC", L"Lume mpv controller verification",
                                  WS_POPUP, -30000, -30000, 320, 180, nullptr, nullptr,
                                  GetModuleHandleW(nullptr), nullptr);
    VerificationWindow() { Expect(handle != nullptr, "verification window created"); }
    ~VerificationWindow() { if (handle) DestroyWindow(handle); }
};

std::wstring ThisExecutable() {
    std::array<wchar_t, 32768> path{};
    const auto count = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    return std::wstring(path.data(), count);
}

std::vector<DWORD> ChildProcesses(HWND parent) {
    std::vector<DWORD> processes;
    EnumChildWindows(parent, [](HWND child, LPARAM context) -> BOOL {
        DWORD processId = 0;
        GetWindowThreadProcessId(child, &processId);
        auto& result = *reinterpret_cast<std::vector<DWORD>*>(context);
        if (processId != GetCurrentProcessId() &&
            std::find(result.begin(), result.end(), processId) == result.end()) result.push_back(processId);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&processes));
    return processes;
}

bool PipeWrite(HANDLE pipe, const Json& message) {
    const auto text = message.dump() + "\n";
    DWORD written = 0;
    return WriteFile(pipe, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) &&
        written == text.size();
}

int RunControlledPlayer(const std::vector<std::string>& arguments) {
    std::string pipeName;
    HWND parent = nullptr;
    for (const auto& argument : arguments) {
        if (argument.find("--input-ipc-server=") == 0) pipeName = argument.substr(19);
        if (argument.find("--wid=") == 0)
            parent = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(std::stoull(argument.substr(6))));
    }
    if (pipeName.empty() || !IsWindow(parent)) return 2;
    if (const auto* output = std::getenv("LUME_TEST_MPV_ARGUMENTS"); output && *output)
        std::ofstream(std::filesystem::u8path(output)) << Json(arguments).dump();
    const auto child = CreateWindowExW(0, L"STATIC", L"Controlled mpv",
                                      WS_CHILD | WS_VISIBLE, 0, 0, 320, 180,
                                      parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!child) return 3;
    const auto pipe = CreateNamedPipeA(pipeName.c_str(), PIPE_ACCESS_DUPLEX,
                                      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                      1, 65536, 65536, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) return 4;
    if (!ConnectNamedPipe(pipe, nullptr) && GetLastError() != ERROR_PIPE_CONNECTED) {
        CloseHandle(pipe);
        return 5;
    }
    std::string input;
    std::string media;
    std::string pendingMedia;
    bool pause = false;
    bool mismatch = false;
    Json filters = Json::array({{{"name", "hflip"}, {"label", "fixture-other"}}});
    // Match the packaged MPV defaults so missing launch options remain visible.
    Json properties = {{"scale", "lanczos"}, {"cscale", ""},
        {"dscale", "hermite"}, {"tscale", "oversample"}, {"volume", 0},
        {"panscan", 1.0}, {"mute", true}, {"aid", "no"}};
    for (const auto& argument : arguments) {
        if (argument == "--vf-add=@lume-balanced:fps=30")
            filters.push_back({{"name", "fps"}, {"label", "lume-balanced"}, {"params", {{"fps", "30"}}}});
        for (const auto* property : {"scale", "cscale", "dscale", "tscale"}) {
            const auto prefix = std::string("--") + property + "=";
            if (argument.find(prefix) == 0) properties[property] = argument.substr(prefix.size());
        }
    }
    for (;;) {
        std::array<char, 4096> buffer{};
        DWORD read = 0;
        if (!ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) || !read) break;
        input.append(buffer.data(), read);
        while (input.find('\n') != std::string::npos) {
            const auto newline = input.find('\n');
            const auto line = input.substr(0, newline);
            input.erase(0, newline + 1);
            const auto request = Json::parse(line, nullptr, false);
            if (!request.is_object()) continue;
            const auto command = request.at("command");
            auto id = request.at("request_id").get<std::uint64_t>();
            if (mismatch) ++id;
            Json response{{"request_id", id}, {"error", "success"}};
            const auto name = command.at(0).get<std::string>();
            bool quit = false;
            bool reject = false;
            bool finishLoad = false;
            if (!media.empty() && media.find("http://") != 0 && (name == "vf" || name == "set")) {
                const auto root = std::filesystem::u8path(media).parent_path();
                const auto failure = std::filesystem::u8path(media + ".reject-command.json");
                if (std::filesystem::is_regular_file(failure)) {
                    std::ifstream source(failure);
                    auto expected = Json::parse(source, nullptr, false);
                    const bool series = expected.is_array() && !expected.empty() && expected[0].is_array();
                    reject = (series ? expected[0] : expected) == command;
                    source.close();
                    if (reject) {
                        if (series && expected.size() > 1) {
                            expected.erase(expected.begin());
                            std::ofstream(failure) << expected.dump();
                        } else std::filesystem::remove(failure);
                    }
                }
                const auto delay = root / "delay-balanced";
                if (std::filesystem::is_regular_file(delay)) {
                    std::ifstream source(delay);
                    unsigned millis = 0; source >> millis; source.close();
                    std::filesystem::remove(delay);
                    std::ofstream(root / "balanced-entered") << "entered";
                    Sleep((std::min)(millis, 3000u));
                }
            }
            if (reject) {
                response["error"] = "controlled balanced failure";
            } else if (name == "loadfile") {
                const bool firstLoad = media.empty();
                const auto previousMedia = media;
                media = command.at(1).get<std::string>();
                const auto file = std::filesystem::u8path(media);
                if (firstLoad && std::filesystem::is_regular_file(file.parent_path() / "sampling.json")) {
                    std::ifstream source(file.parent_path() / "sampling.json");
                    properties.update(Json::parse(source));
                }
                if (file.filename() == "slow-load.mp4") Sleep(1000);
                if (file.filename() == "reject.mp4") {
                    response["error"] = "loading failed";
                } else if (file.filename() == "mismatch.mp4") {
                    mismatch = true;
                    ++id;
                    response["request_id"] = id;
                } else {
                    if (media.find("http://") != 0)
                        std::ofstream(file.parent_path() / "arguments.json") << Json(arguments).dump();
                    PipeWrite(pipe, {{"event", "start-file"}});
                    const auto deferred = std::filesystem::path(file.wstring() + L".defer-load");
                    if (!previousMedia.empty() && std::filesystem::is_regular_file(deferred)) {
                        std::filesystem::remove(deferred);
                        pendingMedia = media;
                        media = previousMedia;
                    } else PipeWrite(pipe, {{"event", "file-loaded"}});
                }
            } else if (name == "get_property") {
                const auto property = command.at(1).get<std::string>();
                if (property == "path") {
                    response["data"] = media;
                    finishLoad = !pendingMedia.empty();
                }
                else if (property == "pause") response["data"] = pause;
                else if (property == "vf") response["data"] = filters;
                else if (properties.contains(property)) response["data"] = properties[property];
                else if (property == "video-frame-info") response["data"] = Json{{"picture-type", "I"}};
                else response["error"] = "property not found";
            } else if (name == "set_property") {
                if (command.at(1) == "pause") {
                    if (std::filesystem::u8path(media).filename() == "slow-command.mp4" &&
                        command.at(2).get<bool>()) Sleep(1000);
                    pause = command.at(2).get<bool>();
                } else if (properties.contains(command.at(1).get<std::string>()))
                    properties[command.at(1).get<std::string>()] = command.at(2);
                else response["error"] = "property not found";
            } else if (name == "set" && properties.contains(command.at(1).get<std::string>())) {
                properties[command.at(1).get<std::string>()] = command.at(2);
            } else if (name == "vf") {
                const auto operation = command.at(1).get<std::string>();
                const auto value = command.at(2).get<std::string>();
                if (operation == "remove" && value == "@lume-balanced") {
                    const auto found = std::find_if(filters.begin(), filters.end(), [](const auto& filter) {
                        return filter.value("label", "") == "lume-balanced";
                    });
                    if (found == filters.end()) response["error"] = "filter not found";
                    else filters.erase(found);
                } else if (operation == "add" && value == "@lume-balanced:fps=30") {
                    filters.push_back({{"name", "fps"}, {"label", "lume-balanced"}, {"params", {{"fps", "30"}}}});
                } else response["error"] = "unsupported filter command";
            } else if (name == "quit") {
                quit = true;
            } else response["error"] = "unsupported command";
            if (!media.empty() && media.find("http://") != 0) {
                const auto file = std::filesystem::u8path(media);
                std::ofstream(file.parent_path() / "commands.jsonl", std::ios::app) << command.dump() << '\n';
                const auto statePath = file.wstring() + L".state.json";
                {
                    std::ofstream output(std::filesystem::path(statePath + L".tmp"));
                    output << Json{{"media", media}, {"running", !quit}, {"vf", filters},
                                   {"properties", properties}, {"pause", pause}}.dump();
                }
                MoveFileExW((statePath + L".tmp").c_str(), statePath.c_str(), MOVEFILE_REPLACE_EXISTING);
                std::ofstream runtime(file.parent_path() / "runtime-state.json.tmp");
                runtime << Json{{"media", media}, {"running", !quit}, {"vf", filters},
                                {"properties", properties}, {"pause", pause}}.dump();
                runtime.close();
                MoveFileExW((file.parent_path() / "runtime-state.json.tmp").c_str(),
                            (file.parent_path() / "runtime-state.json").c_str(),
                            MOVEFILE_REPLACE_EXISTING);
            }
            if (!PipeWrite(pipe, response) || quit) {
                FlushFileBuffers(pipe);
                CloseHandle(pipe);
                DestroyWindow(child);
                return 0;
            }
            if (finishLoad) {
                media = pendingMedia;
                pendingMedia.clear();
                PipeWrite(pipe, {{"event", "file-loaded"}});
            }
        }
    }
    CloseHandle(pipe);
    DestroyWindow(child);
    return 0;
}

Json PlayerState(const std::filesystem::path& media) {
    std::ifstream source(std::filesystem::path(media.wstring() + L".state.json"));
    return Json::parse(source, nullptr, false);
}

bool HasBalanced(const Json& state) {
    if (!state.is_object() || !state.contains("vf")) return false;
    return std::count_if(state["vf"].begin(), state["vf"].end(), [](const auto& filter) {
        return filter.value("label", "") == "lume-balanced";
    }) == 1;
}

bool HasOriginalSampling(const Json& state) {
    if (!state.is_object() || !state.contains("properties") || !state["properties"].is_object()) return false;
    const auto& properties = state["properties"];
    return properties.value("scale", "") == "bilinear" &&
        properties.value("cscale", "") == "bilinear" &&
        properties.value("dscale", "") == "mitchell" &&
        properties.value("tscale", "") == "oversample";
}

bool HasUnbalancedLaunchOptions(const TemporaryMedia& files) {
    std::ifstream source(files.root / "arguments.json");
    const auto arguments = Json::parse(source, nullptr, false);
    if (!arguments.is_array()) return false;
    for (const auto* option : {"--scale=bilinear", "--cscale=bilinear",
                              "--dscale=mitchell", "--tscale=oversample"}) {
        if (std::count(arguments.begin(), arguments.end(), Json(option)) != 1) return false;
    }
    return std::none_of(arguments.begin(), arguments.end(), [](const auto& argument) {
        return argument.template get<std::string>().find("fps=") != std::string::npos;
    });
}

void TestUnbalancedStartup() {
    TemporaryMedia files;
    VerificationWindow window;
    Controller controller(ThisExecutable(), window.handle);
    const auto display = Display();
    Expect(controller.SetBalancedMode(false), "initial disabled mode is accepted without a player");
    Expect(controller.Start(display, files.media.u8string()), "initial unbalanced player starts");
    const auto state = PlayerState(files.media);
    Expect(HasOriginalSampling(state), "initial unbalanced player applies original sampling instead of MPV defaults");
    Expect(!HasBalanced(state), "initial unbalanced player has no balanced filter");
    Expect(HasUnbalancedLaunchOptions(files), "initial unbalanced launch explicitly sets all four samplers without a frame cap");
}

void TestUnbalancedRecoveryAndRestart() {
    TemporaryMedia files;
    VerificationWindow window;
    Controller controller(ThisExecutable(), window.handle);
    const auto display = Display();
    Expect(controller.SetBalancedMode(true) && controller.Start(display, files.media.u8string()),
        "balanced player starts for disabled-mode recovery");
    Expect(controller.SetBalancedMode(false), "disable mode before recovery");
    const auto disabled = PlayerState(files.media);
    Expect(HasOriginalSampling(disabled) && !HasBalanced(disabled), "disabled player confirms original sampling");
    Expect(controller.Recover(display), "recover the disabled player");
    const auto recovered = PlayerState(files.media);
    Expect(HasOriginalSampling(recovered), "disabled recovery retains all four confirmed samplers");
    Expect(!HasBalanced(recovered), "disabled recovery does not restore a frame cap");
    Expect(HasUnbalancedLaunchOptions(files), "disabled recovery explicitly sets sampling before the first frame");
    controller.Stop(display);
    Expect(controller.Start(display, files.media.u8string()), "start a new player after disabling and stopping");
    const auto restarted = PlayerState(files.media);
    Expect(HasOriginalSampling(restarted) && !HasBalanced(restarted),
        "later unbalanced starts retain confirmed sampling without a frame cap");
    Expect(HasUnbalancedLaunchOptions(files), "later unbalanced starts explicitly set all four samplers");
}

void TestBalancedMode() {
    TemporaryMedia files;
    VerificationWindow window;
    const auto display = Display();
    Controller controller(ThisExecutable(), window.handle);
    Expect(controller.Start(display, files.media.u8string()), "balanced fixture starts");
    if (!controller.IsRunning(display)) return;
    Expect(controller.SetVolume(display, 27) && controller.SetFit(display, false) &&
        controller.Command(display, MpvCommand::Pause), "seed independent playback options");
    Expect(controller.SetBalancedMode(true), "enable balanced runtime API");
    auto state = PlayerState(files.media);
    Expect(HasBalanced(state), "balanced adds exactly one labelled fps filter");
    if (state.is_object()) {
        Expect(state["vf"][0]["label"] == "fixture-other", "balanced preserves unrelated filters");
        Expect(state["properties"]["scale"] == "bilinear" && state["properties"]["cscale"] == "bilinear" &&
            state["properties"]["dscale"] == "bilinear" && state["properties"]["tscale"] == "linear",
            "balanced uses the original four sampling settings");
        Expect(state["pause"] == true && state["properties"]["volume"] == 27 &&
            state["properties"]["panscan"] == 0.0, "balanced retains pause, volume and fit");
    }
    const auto log = files.root / "commands.jsonl";
    const auto length = std::filesystem::file_size(log);
    Expect(controller.SetBalancedMode(true) && std::filesystem::file_size(log) == length,
        "repeated enable is an IPC no-op");
    Expect(controller.Recover(display) && HasBalanced(PlayerState(files.media)),
        "recover applies balanced mode before the first frame");
    std::ifstream source(files.root / "arguments.json");
    const auto arguments = Json::parse(source, nullptr, false);
    Expect(arguments.is_array() &&
        std::find(arguments.begin(), arguments.end(), "--vf-add=@lume-balanced:fps=30") != arguments.end(),
        "balanced filter is present in actual recovery launch arguments");
    Expect(controller.SetBalancedMode(false), "disable balanced runtime API");
    state = PlayerState(files.media);
    Expect(!HasBalanced(state) && state["vf"].size() == 1 &&
        state["vf"][0]["label"] == "fixture-other", "disable removes only the owned filter");
    Expect(HasOriginalSampling(state),
        "disable restores original sampling mode");
    const auto disabledLength = std::filesystem::file_size(log);
    Expect(controller.SetBalancedMode(false) && std::filesystem::file_size(log) == disabledLength,
        "repeated disable is an IPC no-op");
    bool wrongThread = true;
    std::thread other([&] { wrongThread = controller.SetBalancedMode(true); });
    other.join();
    Expect(!wrongThread && !HasBalanced(PlayerState(files.media)), "wrong-thread mode update cannot touch MPV");
    controller.Stop(display);
    Expect(controller.SetBalancedMode(true) && controller.Start(display, files.media.u8string()) &&
        HasBalanced(PlayerState(files.media)), "mode set while idle applies to later starts");
}

void TestBalancedRollback() {
    TemporaryMedia files;
    VerificationWindow window;
    const auto displays = lume::platform::windows::EnumerateMpvDisplays();
    Controller controller(ThisExecutable(), window.handle);
    const Json sampling = {{"scale", "lanczos"}, {"cscale", "spline36"},
        {"dscale", "catmull_rom"}, {"tscale", "oversample"}};
    std::ofstream(files.root / "sampling.json") << sampling.dump();
    std::vector<std::pair<std::uint64_t, std::filesystem::path>> started;
    for (const auto& display : displays) {
        const auto media = files.root / (std::to_string(display.id) + ".mp4");
        std::ofstream(media) << "Rollback fixture";
        Expect(controller.Start(display.id, media.u8string()), "rollback display starts");
        started.push_back({display.id, media});
    }
    if (started.empty()) return;
    std::sort(started.begin(), started.end());
    const auto failure = std::filesystem::path(started.back().second.wstring() + L".reject-command.json");
    std::ofstream(failure) << Json::array({"set", "tscale", "linear"}).dump();
    Expect(!controller.SetBalancedMode(true), "last display's rejected sampler fails the mode transaction");
    for (const auto& item : started) {
        const auto state = PlayerState(item.second);
        Expect(controller.IsRunning(item.first) && !HasBalanced(state) &&
            state["vf"].size() == 1 && state["properties"]["scale"] == sampling["scale"] &&
            state["properties"]["cscale"] == sampling["cscale"] &&
            state["properties"]["dscale"] == sampling["dscale"] &&
            state["properties"]["tscale"] == sampling["tscale"],
            "failed enable restores exact prior samplers on every changed display");
    }
    std::filesystem::remove(files.root / "sampling.json");
    Expect(controller.SetBalancedMode(true), "mode can be retried after rollback");
    std::ofstream(failure) << Json::array({"set", "tscale", "oversample"}).dump();
    Expect(!controller.SetBalancedMode(false), "rejected disable fails without committing its boolean");
    for (const auto& item : started)
        Expect(controller.IsRunning(item.first) && HasBalanced(PlayerState(item.second)),
            "failed disable restores the previously enabled filter");
    for (const auto& item : started)
        Expect(controller.Recover(item.first) && HasBalanced(PlayerState(item.second)),
            "recover retains the last successfully committed mode");
    std::cout << "PASS: balanced rollback across " << started.size() << " display(s)\n";
}

void TestBalancedCancellation() {
    TemporaryMedia files;
    VerificationWindow window;
    Controller controller(ThisExecutable(), window.handle);
    const auto display = Display();
    std::atomic<bool> cancelled{false};
    controller.SetCancellation(&cancelled);
    Expect(controller.Start(display, files.media.u8string()), "balanced cancellation fixture starts");
    std::ofstream(files.root / "delay-balanced") << 200;
    std::thread trigger([&] {
        const auto until = GetTickCount64() + 2000;
        while (!std::filesystem::is_regular_file(files.root / "balanced-entered") && GetTickCount64() < until)
            Sleep(5);
        cancelled = true;
    });
    const auto before = GetTickCount64();
    const auto result = controller.SetBalancedMode(true);
    trigger.join();
    Expect(!result && GetTickCount64() - before < 3000, "in-flight mode cancellation has a bounded rollback");
    Expect(!controller.IsRunning(display) || !HasBalanced(PlayerState(files.media)),
        "cancellation restores the old mode or stops the unresponsive owned player");
    cancelled = false;
    Expect(controller.Recover(display) && !HasBalanced(PlayerState(files.media)),
        "cancelled mode cannot leak into recovery");
}

void TestBalancedTimeoutAndFailedRollback() {
    TemporaryMedia files;
    VerificationWindow window;
    Controller controller(ThisExecutable(), window.handle);
    const auto display = Display();
    Expect(controller.Start(display, files.media.u8string()), "balanced timeout fixture starts");
    std::ofstream(files.root / "delay-balanced") << 1500;
    const auto before = GetTickCount64();
    Expect(!controller.SetBalancedMode(true) && GetTickCount64() - before < 4000,
        "timed-out filter command completes a bounded rollback before returning false");
    Expect(!HasBalanced(PlayerState(files.media)), "late filter acknowledgement is compensated before failure returns");
    Expect(controller.Recover(display) && !HasBalanced(PlayerState(files.media)),
        "IPC timeout never changes the mode inherited by recovery");
    const auto failure = std::filesystem::path(files.media.wstring() + L".reject-command.json");
    std::ofstream(failure) << Json::array({
        Json::array({"set", "tscale", "linear"}),
        Json::array({"vf", "remove", "@lume-balanced"})
    }).dump();
    Expect(!controller.SetBalancedMode(true), "rollback rejection still returns a failed mode transaction");
    Expect(!controller.IsRunning(display) && controller.ActiveDisplays().empty() &&
        GetWindow(window.handle, GW_CHILD) == nullptr,
        "a player refusing rollback is stopped instead of playing an inconsistent mode");
    Expect(controller.Recover(display) && !HasBalanced(PlayerState(files.media)),
        "failed rollback preserves remembered media and the previous committed mode");
}

void TestUnavailableMpv() {
    lume::platform::windows::MpvProcessController controller(L"missing-mpv.exe");
    Expect(!controller.Start(1, "wallpaper.mp4"), "missing mpv rejected");
    Expect(!controller.Command(1, lume::wallpaper::MpvCommand::Pause), "missing command rejected");
}

void TestExistingFileIsNotAPlayer() {
    TemporaryMedia files;
    VerificationWindow window;
    Controller controller(files.executable.wstring(), window.handle);
    const auto display = Display();
    Expect(!controller.Start(display, files.media.u8string()),
           "an existing non-executable file must not report successful playback");
    Expect(controller.LastError() == "MPV_PROCESS_START_FAILED", "invalid executable fails at process creation");
    Expect(!controller.Command(display, MpvCommand::Pause),
           "a failed startup must not leave a commandable player");
    controller.Stop(display);
    controller.Stop(display);
    Expect(!controller.Command(display, MpvCommand::Resume),
           "stopping twice must leave the player stopped");
    Expect(controller.ActiveDisplays().empty(), "failed startup owns no live display");
    Expect(GetWindow(window.handle, GW_CHILD) == nullptr, "failed startup destroys its native container");
}

void TestCommandRequiresStartedPlayer() {
    TemporaryMedia files;
    lume::platform::windows::MpvProcessController controller(files.executable.wstring());
    Expect(!controller.Command(1, lume::wallpaper::MpvCommand::Pause),
           "commands must fail before any player has started");
    Expect(!controller.Recover(1),
           "recovery must fail when there is no owned display or remembered media");
}

void TestControlledPlayerLifecycle() {
    TemporaryMedia files;
    VerificationWindow window;
    const auto display = Display();
    Controller controller(ThisExecutable(), window.handle);
    Expect(controller.Start(display, files.media.u8string()), "controlled player reports a loaded frame");
    if (!controller.IsRunning(display)) {
        std::cerr << "player startup detail: " << controller.LastError() << '\n';
        return;
    }
    const auto processes = ChildProcesses(window.handle);
    Expect(processes.size() == 1, "player is embedded only in the owned verification window");
    const auto process = processes.empty() ? nullptr : OpenProcess(SYNCHRONIZE, FALSE, processes.front());
    Expect(controller.EnsureAttached(display), "healthy player attachment is verified");
    Expect(controller.Command(display, MpvCommand::Pause), "pause response and actual pause state verified");
    Expect(controller.SetVolume(display, 30), "explicit positive volume enables an audio track");
    Expect(controller.SetVolume(display, 0), "zero volume disables audio decoding");
    Expect(controller.SetFit(display, false), "contain fit is commandable");
    const auto second = files.root / L"second.mp4";
    std::ofstream(second) << "Another controlled media fixture.";
    Expect(controller.Start(display, second.u8string()), "same display switches the loaded media");
    Expect(ChildProcesses(window.handle) == processes, "switching media reuses the owned player process");
    Expect(controller.Command(display, MpvCommand::Resume), "resume response and actual pause state verified");
    Expect(!controller.SetVolume(display, 101), "invalid volume is rejected");
    Expect(!controller.Start(0, files.media.u8string()), "unknown displays are rejected");
    Expect(controller.IsRunning(display), "invalid requests do not stop the existing display");
    std::ifstream recorded(files.root / L"arguments.json");
    const auto arguments = Json::parse(recorded, nullptr, false);
    Expect(arguments.is_array(), "controlled process records its real launch arguments");
    if (arguments.is_array()) {
        const auto contains = [&](const char* value) {
            return std::find(arguments.begin(), arguments.end(), Json(value)) != arguments.end();
        };
        Expect(contains("--audio=no") && contains("--hwdec=auto-safe"), "original silent hardware-decoded defaults retained");
        Expect(contains("--d3d11va-zero-copy=yes"), "ordinary local video retains zero-copy");
        Expect(contains("--access-references=no") && contains("--load-scripts=no"), "external references and scripts disabled");
        for (const auto& argument : arguments) {
            Expect(argument.get<std::string>().find("fps=") == std::string::npos, "no implicit frame-rate downgrade");
        }
    }
    controller.Stop(display);
    controller.Stop(display);
    Expect(!controller.IsRunning(display) && controller.ActiveDisplays().empty(), "stop reclaims the owned player");
    if (process) {
        Expect(WaitForSingleObject(process, 1000) == WAIT_OBJECT_0, "stopped process has really exited");
        CloseHandle(process);
    }
    Expect(GetWindow(window.handle, GW_CHILD) == nullptr, "stop destroys the verification container");
}

void TestFailuresAndRecovery() {
    TemporaryMedia files;
    VerificationWindow window;
    const auto display = Display();
    const auto rejected = files.root / L"reject.mp4";
    const auto mismatch = files.root / L"mismatch.mp4";
    std::ofstream(rejected) << "Rejected fixture.";
    std::ofstream(mismatch) << "Wrong request id fixture.";
    Controller controller(ThisExecutable(), window.handle);
    Expect(!controller.Start(display, rejected.u8string()), "negative load response cannot mean success");
    Expect(controller.ActiveDisplays().empty(), "negative response reclaims the player");
    const auto started = GetTickCount64();
    Expect(!controller.Start(display, mismatch.u8string()), "wrong response request id cannot satisfy a command");
    Expect(GetTickCount64() - started < 5000, "mismatched request has a bounded timeout");
    Expect(controller.ActiveDisplays().empty(), "timed out startup reclaims the player");
    Expect(controller.Start(display, files.media.u8string()), "valid startup still works after failures");
    const auto children = ChildProcesses(window.handle);
    if (!children.empty()) {
        const auto child = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, children.front());
        Expect(child != nullptr, "test obtains only its own controlled child's process");
        if (child) {
            TerminateProcess(child, 9);
            WaitForSingleObject(child, 1000);
            CloseHandle(child);
            Expect(!controller.IsRunning(display), "child exit is observed");
            Expect(controller.Recover(display), "recovery relaunches only remembered owned media");
            Expect(controller.EnsureAttached(display), "recovered attachment is healthy");
        }
    }
    controller.Stop(display);
    Expect(!controller.Recover(display), "explicit stop forgets the display");
}

void TestUrlCapabilityAndDestructor() {
    TemporaryMedia files;
    VerificationWindow window;
    const auto display = Display();
    HANDLE process = nullptr;
    {
        Controller controller(ThisExecutable(), window.handle);
        Expect(!controller.AllowMediaUrlPrefix("http://localhost:1234/abcdefghijklmnop/"), "DNS loopback aliases are not accepted");
        Expect(!controller.AllowMediaUrlPrefix("http://127.0.0.1:1234/"), "non-capability root prefixes are not accepted");
        Expect(controller.AllowMediaUrlPrefix("http://127.0.0.1:1234/abcdefghijklmnop/"), "opaque loopback capability is accepted");
        Expect(controller.AllowMediaUrlPrefix("http://127.0.0.1:1234/qrstuvwxyz012345/"), "second opaque media capability is accepted");
        Expect(!controller.Start(display, "http://127.0.0.1:1234/other-token/movie.mp4"), "URLs outside the registered capability rejected");
        Expect(!controller.Start(display, "http://127.0.0.1:1234/abcdefghijklmnop/../movie.mp4"), "URL traversal rejected");
        Expect(!controller.Start(display, "http://127.0.0.1:1234/abcdefghijklmnop/movie.mp4?next=x"), "URL query redirects rejected");
        Expect(!controller.Start(display, "\\\\server\\share\\movie.mp4"), "UNC media cannot trigger network access");
        Expect(controller.Start(display, "http://127.0.0.1:1234/abcdefghijklmnop/media.mp4"),
               "earlier registered media capability remains allowed");
        Expect(controller.Start(display, "http://127.0.0.1:1234/qrstuvwxyz012345/media.mp4"),
               "different registered media capability can reuse the player");
        controller.Stop(display);
        controller.RevokeMediaUrlPrefix("http://127.0.0.1:1234/abcdefghijklmnop/");
        Expect(!controller.Start(display,"http://127.0.0.1:1234/abcdefghijklmnop/media.mp4"),
               "released media capability cannot be reused for a later launch");
        bool recycled=true;
        for(unsigned cycle=0;cycle<4200&&recycled;++cycle){
            const auto prefix="http://127.0.0.1:1234/abcdefghijklmnop"+std::to_string(cycle)+"/";
            recycled=controller.AllowMediaUrlPrefix(prefix);
            controller.RevokeMediaUrlPrefix(prefix);
        }
        Expect(recycled,"released capabilities do not accumulate toward a session lifetime limit");
        Expect(controller.Start(display, files.media.u8string()), "player starts for destructor cleanup");
        const auto children = ChildProcesses(window.handle);
        if (!children.empty()) process = OpenProcess(SYNCHRONIZE, FALSE, children.front());
    }
    if (process) {
        Expect(WaitForSingleObject(process, 1000) == WAIT_OBJECT_0, "destructor terminates its job-owned child");
        CloseHandle(process);
    }
    Expect(GetWindow(window.handle, GW_CHILD) == nullptr, "destructor destroys the owned container");
}

void TestProtectedVideoUsesVideoPolicy() {
    TemporaryMedia files;
    VerificationWindow window;
    Controller controller(ThisExecutable(), window.handle);
    const auto display = Display();
    const auto launchArguments = files.root / "protected-video-arguments.json";
    const auto prefix = "http://127.0.0.1:1234/abcdefghijklmnop/";
    const auto url = std::string(prefix) + "media.mp4";
    _putenv_s("LUME_TEST_MPV_ARGUMENTS", launchArguments.u8string().c_str());
    Expect(controller.AllowMediaUrlPrefix(prefix), "protected video capability is accepted");
    Expect(controller.SetBalancedMode(true), "balanced policy is enabled before protected video starts");
    Expect(controller.Start(display, url), "protected video starts through the controlled player");
    std::ifstream launchSource(launchArguments);
    const auto arguments = Json::parse(launchSource, nullptr, false);
    Expect(arguments.is_array() &&
        std::find(arguments.begin(), arguments.end(), "--vf-add=@lume-balanced:fps=30") != arguments.end(),
        "protected video receives the balanced frame-rate filter");
    Expect(arguments.is_array() &&
        std::find(arguments.begin(), arguments.end(), "--d3d11va-zero-copy=yes") == arguments.end(),
        "protected video does not inherit the local-file zero-copy option");
    controller.Stop(display);
    _putenv_s("LUME_TEST_MPV_ARGUMENTS", "");
}

void TestFailedMediaSwitchKeepsPreviousPlayer() {
    TemporaryMedia files;
    VerificationWindow window;
    Controller controller(ThisExecutable(), window.handle);
    const auto display = Display();
    const auto previous = files.root / "previous.webp";
    const auto next = files.root / "next.mp4";
    std::ofstream(previous) << "previous static wallpaper";
    std::ofstream(next) << "next video wallpaper";
    std::ofstream(std::filesystem::path(next.wstring() + L".reject-command.json"))
        << Json::array({"vf", "add", "@lume-balanced:fps=30"}).dump();
    Expect(controller.SetBalancedMode(true), "balanced policy is enabled before the switch");
    Expect(controller.Start(display, previous.u8string()), "previous static wallpaper starts");
    Expect(!controller.Start(display, next.u8string()), "a failed post-load filter command rejects the switch");
    std::ifstream runtimeSource(files.root / "runtime-state.json");
    const auto runtime = Json::parse(runtimeSource, nullptr, false);
    Expect(controller.IsRunning(display), "failed media switch keeps the previous player alive");
    Expect(controller.LastError() == "MPV_COMMAND_REJECTED",
        "failed media switch keeps the original switch error");
    Expect(runtime.is_object() && runtime.value("running", false) &&
        runtime.value("media", std::string{}) == std::filesystem::absolute(previous).lexically_normal().u8string(),
        "failed media switch restores the previous wallpaper");
    controller.Stop(display);
}

void TestMediaKindSwitchesAndRecovery() {
    TemporaryMedia files;
    VerificationWindow window;
    Controller controller(ThisExecutable(), window.handle);
    const auto display = Display();
    const auto image = files.root / "static.WEBP";
    std::ofstream(image) << "static fixture";
    Expect(controller.Start(display, image.u8string()), "static policy fixture starts");
    Expect(controller.SetVolume(display, 23) && controller.SetFit(display, false) &&
        controller.Command(display, MpvCommand::Pause), "seed media switch playback options");
    const auto processes = ChildProcesses(window.handle);
    for (const bool enabled : {true, false, true}) {
        Expect(controller.SetBalancedMode(enabled), "static media accepts either sampling policy");
        Expect(!HasBalanced(PlayerState(image)), "static media never receives a frame-rate filter");
        Expect(controller.Start(display, files.media.u8string()), "static media switches to video");
        Expect(HasBalanced(PlayerState(files.media)) == enabled, "video receives the committed frame-rate policy");
        Expect(controller.Start(display, image.u8string()), "video switches back to a static image");
        const auto state = PlayerState(image);
        Expect(!HasBalanced(state) && state["vf"].size() == 1 &&
            state["pause"] == true && state["properties"]["volume"] == 23 &&
            state["properties"]["panscan"] == 0.0, "switches retain independent player options");
        Expect(ChildProcesses(window.handle) == processes, "media kind changes reuse the existing player");
    }
    Expect(controller.Recover(display) && !HasBalanced(PlayerState(image)),
        "balanced static recovery does not reintroduce the video filter");
}

void TestMediaSwitchTimeoutAndFailedRestore() {
    for (const bool rejectRestore : {false, true}) {
        TemporaryMedia files;
        VerificationWindow window;
        Controller controller(ThisExecutable(), window.handle);
        const auto display = Display();
        const auto image = files.root / "previous.webp";
        std::ofstream(image) << "previous static fixture";
        Expect(controller.SetBalancedMode(true) && controller.Start(display, image.u8string()),
            "static timeout fixture starts");
        std::ofstream(files.root / "delay-balanced") << 1500;
        if (rejectRestore)
            std::ofstream(std::filesystem::path(files.media.wstring() + L".reject-command.json"))
                << Json::array({"vf", "remove", "@lume-balanced"}).dump();
        const auto before = GetTickCount64();
        Expect(!controller.Start(display, files.media.u8string()) &&
            GetTickCount64() - before < 4000, "late filter application has a bounded failed switch");
        Expect(controller.LastError() == "MPV_IPC_TIMEOUT", "restore preserves the original filter timeout");
        std::ifstream runtimeSource(files.root / "runtime-state.json");
        const auto state = Json::parse(runtimeSource, nullptr, false);
        if (rejectRestore) {
            Expect(!controller.IsRunning(display) && GetWindow(window.handle, GW_CHILD) == nullptr,
                "rejected compensating removal stops the inconsistent player");
        } else {
            Expect(controller.IsRunning(display) && !HasBalanced(state) &&
                state.value("media", std::string{}) == image.u8string(),
                "late video filter is removed before restoring the previous static image");
        }
        Expect(controller.Recover(display) && !HasBalanced(PlayerState(image)),
            "recovery remembers the old image even if a post-load switch rollback failed");
    }
}

void TestMediaSwitchRemovalAndCancellation() {
    TemporaryMedia files;
    VerificationWindow window;
    Controller controller(ThisExecutable(), window.handle);
    const auto display = Display();
    const auto image = files.root / "next.webp";
    std::ofstream(image) << "next static fixture";
    Expect(controller.SetBalancedMode(true) && controller.Start(display, files.media.u8string()),
        "video removal fixture starts");
    std::ofstream(std::filesystem::path(files.media.wstring() + L".reject-command.json"))
        << Json::array({"vf", "remove", "@lume-balanced"}).dump();
    Expect(!controller.Start(display, image.u8string()) &&
        controller.LastError() == "MPV_COMMAND_REJECTED", "rejected pre-load removal preserves its failure");
    Expect(controller.IsRunning(display) && HasBalanced(PlayerState(files.media)),
        "failed pre-load removal reconfirms the previous video policy");
    std::atomic<bool> cancelled{false};
    controller.SetCancellation(&cancelled);
    std::ofstream(files.root / "delay-balanced") << 1500;
    std::thread trigger([&] {
        const auto until = GetTickCount64() + 3000;
        while (!std::filesystem::is_regular_file(files.root / "balanced-entered") &&
               GetTickCount64() < until) Sleep(5);
        cancelled = true;
    });
    const auto before = GetTickCount64();
    const auto switched = controller.Start(display, image.u8string());
    trigger.join();
    Expect(!switched && controller.LastError() == "MPV_CANCELLED" &&
        GetTickCount64() - before < 1200, "cancelled switch stops promptly without starting a long restoration");
    Expect(!controller.IsRunning(display), "cancelled switch leaves no uncertain active media");
    cancelled = false;
    Expect(controller.Recover(display) && HasBalanced(PlayerState(files.media)),
        "cancelled switch retains the previously confirmed video for recovery");
    controller.SetCancellation(nullptr);
}

void TestDelayedRestoreAfterFilterTimeout() {
    TemporaryMedia files;
    VerificationWindow window;
    Controller controller(ThisExecutable(), window.handle);
    const auto display = Display();
    const auto image = files.root / "previous.webp";
    const auto deferred = std::filesystem::path(image.wstring() + L".defer-load");
    std::ofstream(image) << "previous static fixture";
    Expect(controller.SetBalancedMode(true) && controller.Start(display, image.u8string()),
        "delayed restoration starts with the previous static image");
    Expect(controller.SetVolume(display, 23) && controller.SetFit(display, false) &&
        controller.Command(display, MpvCommand::Pause), "seed delayed restoration playback options");
    const auto processes = ChildProcesses(window.handle);
    std::ofstream(deferred) << "acknowledge before publishing the restored path";
    std::ofstream(files.root / "delay-balanced") << 1500;
    Expect(!controller.Start(display, files.media.u8string()) &&
        controller.LastError() == "MPV_IPC_TIMEOUT", "delayed restoration retains the original switch timeout");
    Expect(!std::filesystem::exists(deferred), "restoration exercised the deferred load response");
    Expect(controller.IsRunning(display) && ChildProcesses(window.handle) == processes,
        "a pending restored path must not reuse the old timeout and stop the player");
    std::ifstream runtimeSource(files.root / "runtime-state.json");
    const auto state = Json::parse(runtimeSource, nullptr, false);
    Expect(state.is_object() && state.value("running", false) && !HasBalanced(state) &&
        state.value("media", std::string{}) == image.u8string() && state["pause"] == true &&
        state["properties"]["volume"] == 23 && state["properties"]["panscan"] == 0.0,
        "deferred restoration preserves the old image and independent playback options");
}

void TestCancellation() {
    TemporaryMedia files;
    VerificationWindow window;
    const auto display = Display();
    const auto slowLoad = files.root / L"slow-load.mp4";
    const auto slowCommand = files.root / L"slow-command.mp4";
    std::ofstream(slowLoad) << "Controlled slow first-load fixture.";
    std::ofstream(slowCommand) << "Controlled slow command fixture.";
    std::atomic<bool> cancelled{false};
    Controller controller(ThisExecutable(), window.handle);
    controller.SetCancellation(&cancelled);
    auto trigger = std::thread([&] { Sleep(80); cancelled = true; });
    auto before = GetTickCount64();
    Expect(!controller.Start(display, slowLoad.u8string()), "cancellation interrupts startup");
    Expect(GetTickCount64() - before < 600, "cancelled startup returns without waiting for the IPC timeout");
    Expect(controller.LastError() == "MPV_CANCELLED", "startup cancellation has a distinct error");
    trigger.join();
    controller.Stop(display);
    Expect(controller.ActiveDisplays().empty() && GetWindow(window.handle, GW_CHILD) == nullptr,
           "cancelled startup leaves no process or window");
    cancelled = false;
    Expect(controller.Start(display, slowCommand.u8string()), "uncancelled controlled player still starts");
    trigger = std::thread([&] { Sleep(80); cancelled = true; });
    before = GetTickCount64();
    Expect(!controller.Command(display, MpvCommand::Pause), "cancellation interrupts an in-flight IPC command");
    Expect(GetTickCount64() - before < 500, "cancelled IPC returns promptly");
    Expect(controller.LastError() == "MPV_CANCELLED", "IPC cancellation has a distinct error");
    trigger.join();
    before = GetTickCount64();
    controller.Stop(display);
    Expect(GetTickCount64() - before < 1000, "Stop still performs bounded cleanup with cancellation set");
    Expect(controller.ActiveDisplays().empty() && GetWindow(window.handle, GW_CHILD) == nullptr,
           "Stop after cancellation reclaims process and native container");
}

constexpr std::uint32_t kRed = 0xff0000u, kBlue = 0x0000ffu;

// 4x2 24-bit BMP: left half red, right half blue.
std::string SplitBmp() {
    const std::uint32_t width = 4, height = 2, stride = 12, pixels = stride * height;
    std::string data(54 + pixels, '\0');
    const auto put = [&](std::size_t offset, std::uint32_t value, int bytes) {
        for (int index = 0; index < bytes; ++index)
            data[offset + index] = static_cast<char>((value >> (8 * index)) & 0xffu);
    };
    data[0] = 'B'; data[1] = 'M';
    put(2, 54 + pixels, 4); put(10, 54, 4); put(14, 40, 4);
    put(18, width, 4); put(22, height, 4); put(26, 1, 2); put(28, 24, 2); put(34, pixels, 4);
    for (std::uint32_t y = 0; y < height; ++y) {
        for (std::uint32_t x = 0; x < width; ++x)
            put(54 + y * stride + x * 3, x < width / 2 ? kRed : kBlue, 3);
    }
    return data;
}

std::filesystem::path WriteSplitBmp(const std::filesystem::path& path) {
    std::ofstream(path, std::ios::binary) << SplitBmp();
    return path;
}

HWND OwnHost(HWND parent) {
    const auto host = GetWindow(parent, GW_CHILD);
    DWORD process = 0;
    if (host) GetWindowThreadProcessId(host, &process);
    return process == GetCurrentProcessId() ? host : nullptr;
}

// Reads one painted RGB pixel of the native host via WM_PRINTCLIENT.
std::uint32_t HostPixel(HWND parent, int x, int y) {
    const auto host = OwnHost(parent);
    RECT bounds{};
    if (!host || !GetClientRect(host, &bounds) || x >= bounds.right || y >= bounds.bottom) return 0x1000000u;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = bounds.right;
    info.bmiHeader.biHeight = -bounds.bottom;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    const auto bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    const auto dc = CreateCompatibleDC(nullptr);
    std::uint32_t result = 0x1000000u;
    if (bitmap && dc && bits) {
        const auto old = SelectObject(dc, bitmap);
        std::fill_n(static_cast<std::uint32_t*>(bits),
                    static_cast<std::size_t>(bounds.right) * bounds.bottom, 0x00ff00ffu);
        SendMessageW(host, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT);
        GdiFlush();
        result = static_cast<const std::uint32_t*>(bits)[y * bounds.right + x] & 0xffffffu;
        SelectObject(dc, old);
    }
    if (dc) DeleteDC(dc);
    if (bitmap) DeleteObject(bitmap);
    return result;
}

bool CoverPixels(HWND parent) {
    return HostPixel(parent, 10, 2) == kRed && HostPixel(parent, 310, 177) == kBlue;
}

bool ContainPixels(HWND parent) {
    return HostPixel(parent, 10, 90) == kRed && HostPixel(parent, 310, 90) == kBlue &&
        HostPixel(parent, 160, 2) == 0 && HostPixel(parent, 160, 177) == 0;
}

void TestNativeStaticWithoutPlayer() {
    TemporaryMedia files;
    VerificationWindow window;
    Controller controller((files.root / L"missing-mpv.exe").wstring(), window.handle);
    const auto display = Display();
    const auto image = WriteSplitBmp(files.root / L"split.bmp");
    Expect(controller.Start(display, image.u8string()), "a decodable BMP starts without any mpv executable");
    Expect(controller.IsRunning(display) && controller.ActiveDisplays().size() == 1,
        "the native static host counts as an active display");
    Expect(OwnHost(window.handle) && ChildProcesses(window.handle).empty(),
        "native static wallpaper uses an in-process host and no player process");
    Expect(CoverPixels(window.handle), "native host paints the cover-fitted image");
    Expect(controller.EnsureAttached(display) && CoverPixels(window.handle),
        "native attachment check does not need IPC");
    Expect(controller.Command(display, MpvCommand::Pause) && controller.Command(display, MpvCommand::Resume),
        "pause and resume are accepted as remembered state");
    Expect(controller.SetVolume(display, 30) && !controller.SetVolume(display, 101),
        "native volume keeps range validation");
    Expect(controller.SetFit(display, false) && ContainPixels(window.handle), "contain re-fits the bitmap");
    Expect(controller.SetBalancedMode(true), "balanced mode with a native image needs no IPC");
    Expect(controller.Recover(display) && ContainPixels(window.handle) && ChildProcesses(window.handle).empty(),
        "recovery restores the native image with the remembered fit");
    const auto corrupt = files.root / L"corrupt.png";
    std::ofstream(corrupt) << "not a png";
    Expect(!controller.Start(display, corrupt.u8string()) &&
        controller.LastError() == "STATIC_IMAGE_DECODE_FAILED", "corrupt PNG fails as corrupt, not as missing mpv");
    Expect(controller.IsRunning(display) && ContainPixels(window.handle), "failed image switch keeps the old image");
    Expect(!controller.Start(display, files.media.u8string()) &&
        controller.LastError() == "MPV_EXECUTABLE_NOT_FOUND", "video still requires the player");
    Expect(controller.IsRunning(display) && ContainPixels(window.handle), "failed video switch keeps the old image");
    controller.Stop(display);
    Expect(!controller.IsRunning(display) && controller.ActiveDisplays().empty() &&
        GetWindow(window.handle, GW_CHILD) == nullptr, "stop destroys the native host");
}

void TestNativeStaticCallbackSource() {
    TemporaryMedia files;
    VerificationWindow window;
    Controller controller((files.root / L"missing-mpv.exe").wstring(), window.handle);
    const auto display = Display();
    const auto bytes = std::make_shared<std::string>(SplitBmp());
    const auto outOfRange = std::make_shared<bool>(false);
    lume::platform::windows::wallpaper::StaticImageSource source;
    source.bytes = bytes->size();
    source.read = [bytes, outOfRange](std::uint64_t offset, char* output, std::size_t size, std::size_t& read) {
        if (offset > bytes->size() || size > bytes->size() - offset) *outOfRange = true;
        if (offset >= bytes->size()) { read = 0; return true; }
        read = (std::min)(size, static_cast<std::size_t>(bytes->size() - offset));
        std::copy_n(bytes->data() + offset, read, output);
        return true;
    };
    const std::string prefix = "http://127.0.0.1:1234/abcdefghijklmnop/";
    const auto url = prefix + "image.bmp";
    Expect(!controller.AllowStaticImageSource(url, source), "a reader needs a registered capability");
    Expect(controller.AllowMediaUrlPrefix(prefix), "static image capability accepted");
    Expect(!controller.AllowStaticImageSource(prefix + "video.mp4", source), "video URLs cannot register readers");
    Expect(!controller.AllowStaticImageSource(prefix + "../image.bmp", source), "traversal URLs cannot register readers");
    Expect(!controller.AllowStaticImageSource(url, {}), "an empty reader is rejected");
    Expect(controller.AllowStaticImageSource(url, source), "registered static image reader accepted");
    Expect(controller.Start(display, url) && OwnHost(window.handle) && ChildProcesses(window.handle).empty(),
        "registered encrypted image starts natively without mpv");
    Expect(CoverPixels(window.handle) && !*outOfRange, "callback decode stays inside the declared bytes");
    controller.RevokeMediaUrlPrefix(prefix);
    Expect(controller.SetFit(display, false) && ContainPixels(window.handle),
        "a running native host keeps its reader for re-fitting after revocation");
    Expect(!controller.Start(display, url) && controller.IsRunning(display),
        "revoked capability rejects new starts and keeps the current image");
    Expect(controller.AllowMediaUrlPrefix(prefix) && !controller.Start(display, url) &&
        controller.LastError() == "MPV_EXECUTABLE_NOT_FOUND",
        "revocation removed the reader, so the URL falls back to the missing player");
    controller.Stop(display);
}

void TestNativeAndPlayerSwitches() {
    TemporaryMedia files;
    VerificationWindow window;
    Controller controller(ThisExecutable(), window.handle);
    const auto display = Display();
    const auto image = WriteSplitBmp(files.root / L"split.bmp");
    Expect(controller.SetBalancedMode(true) && controller.Start(display, image.u8string()) &&
        ChildProcesses(window.handle).empty(), "native image starts with a player available");
    Expect(controller.SetVolume(display, 23) && controller.SetFit(display, false) &&
        controller.Command(display, MpvCommand::Pause), "seed options on the native image");
    Expect(controller.Start(display, files.media.u8string()), "native image switches to video");
    const auto processes = ChildProcesses(window.handle);
    const auto state = PlayerState(files.media);
    Expect(processes.size() == 1 && state.is_object() && state["pause"] == true &&
        state["properties"]["volume"] == 23 && state["properties"]["panscan"] == 0.0 && HasBalanced(state),
        "video inherits options remembered by the native image");
    const auto process = processes.empty() ? nullptr : OpenProcess(SYNCHRONIZE, FALSE, processes.front());
    Expect(controller.Start(display, image.u8string()) && ContainPixels(window.handle),
        "video switches back to a native image with the remembered fit");
    Expect(ChildProcesses(window.handle).empty(), "native switch stops the player");
    if (process) {
        Expect(WaitForSingleObject(process, 1000) == WAIT_OBJECT_0, "previous player process exits");
        CloseHandle(process);
    }
    const auto compat = files.root / L"compat.webp";
    std::ofstream(compat) << "no system decoder recognizes this";
    Expect(controller.Start(display, compat.u8string()) && ChildProcesses(window.handle).size() == 1,
        "images without a usable decoder keep the player compatibility path");
    const auto compatState = PlayerState(compat);
    Expect(compatState.is_object() && !HasBalanced(compatState) && compatState["pause"] == true,
        "compatibility image keeps static policy and remembered pause");
    controller.Stop(display);
    Expect(controller.ActiveDisplays().empty() && GetWindow(window.handle, GW_CHILD) == nullptr,
        "stop reclaims the compatibility player");
}
}
int main(int argc, char** argv) {
    std::vector<std::string> arguments;
    for (auto index = 1; index < argc; ++index) arguments.emplace_back(argv[index]);
    if (std::any_of(arguments.begin(), arguments.end(), [](const auto& argument) {
        return argument.find("--input-ipc-server=") == 0;
    })) return RunControlledPlayer(arguments);
    TestUnavailableMpv();
    TestExistingFileIsNotAPlayer();
    TestCommandRequiresStartedPlayer();
    TestControlledPlayerLifecycle();
    TestFailuresAndRecovery();
    TestUrlCapabilityAndDestructor();
    TestProtectedVideoUsesVideoPolicy();
    TestFailedMediaSwitchKeepsPreviousPlayer();
    TestMediaKindSwitchesAndRecovery();
    TestMediaSwitchTimeoutAndFailedRestore();
    TestMediaSwitchRemovalAndCancellation();
    TestDelayedRestoreAfterFilterTimeout();
    TestCancellation();
    TestNativeStaticWithoutPlayer();
    TestNativeStaticCallbackSource();
    TestNativeAndPlayerSwitches();
    TestUnbalancedStartup();
    TestUnbalancedRecoveryAndRestart();
    TestBalancedMode();
    TestBalancedRollback();
    TestBalancedCancellation();
    TestBalancedTimeoutAndFailedRollback();
    std::cout << "mpv process controller tests: " << failures << " failure(s)\n";
    return failures == 0 ? 0 : 1;
}
