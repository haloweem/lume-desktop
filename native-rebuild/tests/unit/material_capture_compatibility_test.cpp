#include "platform/windows/material/capture_compatibility.h"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void Expect(bool value, const char* text) {
    if (!value) { std::cerr << "FAIL: " << text << '\n'; std::exit(1); }
}

struct Session {
    bool setterFails=false, getterFails=false, ignored=false, startFails=false;
    bool borderRequired=true;
    unsigned starts=0;
    std::string calls;
    void IsBorderRequired(bool value) {
        calls+='S';
        if (setterFails) throw std::runtime_error("unsupported or denied");
        if (!ignored) borderRequired=value;
    }
    bool IsBorderRequired() {
        calls+='G';
        if (getterFails) throw std::runtime_error("readback failed");
        return borderRequired;
    }
    void StartCapture() {
        calls+='C';++starts;
        if (startFails) throw std::runtime_error("source unavailable");
    }
};

template<class Action>
void ExpectCompatibilityFailure(Action&& action, HRESULT error, const char* text) {
    bool rejected=false;
    try { action(); }
    catch (const lume_glass::CaptureCompatibilityFailure& failure) {
        rejected=true;Expect(failure.error==error,"original compatibility HRESULT preserved");
    }
    Expect(rejected,text);
}
}

int main() {
    unsigned resources=0;
    volatile bool available=false, queryFailure=true;
    ExpectCompatibilityFailure([&] {
        lume_glass::RequireCaptureBorderControl([&] { return available; }, [] { return E_FAIL; });
        ++resources;
    },HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED),"missing border API blocks material initialization");
    Expect(resources==0,"unsupported platform creates no material/capture resources");

    ExpectCompatibilityFailure([&] {
        lume_glass::RequireCaptureBorderControl([&] { if (queryFailure) throw std::runtime_error("metadata unavailable"); return true; },
                                               [] { return E_NOINTERFACE; });
        ++resources;
    },E_NOINTERFACE,"failed capability query blocks initialization");
    Expect(resources==0,"failed capability query creates no resources");

    lume_glass::RequireCaptureBorderControl([] { return true; }, [] { return E_FAIL; });
    Session healthy;
    lume_glass::StartMaterialCapture(healthy,[] { return 4; },[] { return E_FAIL; });
    Expect(healthy.starts==1&&healthy.calls=="SGC"&&!healthy.borderRequired,
           "supported session verifies border configuration before starting once");

    for(const HRESULT error : {E_NOINTERFACE,E_ACCESSDENIED}) {
        Session denied;denied.setterFails=true;
        ExpectCompatibilityFailure([&] { lume_glass::StartMaterialCapture(denied,[] { return 4; },[&] { return error; }); },
                                   error,"failed border configuration aborts capture");
        Expect(denied.starts==0&&denied.calls=="S","configuration failure never starts capture");
    }
    Session ignored;ignored.ignored=true;
    ExpectCompatibilityFailure([&] { lume_glass::StartMaterialCapture(ignored,[] { return 4; },[] { return E_FAIL; }); },
                               E_ACCESSDENIED,"system retaining its border rejects decorative capture");
    Expect(ignored.starts==0&&ignored.calls=="SG","ignored configuration never starts capture");

    Session unreadable;unreadable.getterFails=true;
    ExpectCompatibilityFailure([&] { lume_glass::StartMaterialCapture(unreadable,[] { return 4; },[] { return E_UNEXPECTED; }); },
                               E_UNEXPECTED,"failed border readback aborts capture");
    Expect(unreadable.starts==0,"unverified border state never starts capture");

    Session unavailable;unavailable.startFails=true;
    bool ordinaryFailure=false;
    try { lume_glass::StartMaterialCapture(unavailable,[] { return 4; },[] { return E_FAIL; }); }
    catch (const std::runtime_error&) { ordinaryFailure=true; }
    Expect(ordinaryFailure&&unavailable.starts==1,
           "source start failure retains ordinary error handling rather than terminal compatibility state");
    for (const int status : {0, 1, 2, 3, 5, -1}) {
        Session deceptive;deceptive.borderRequired=false;deceptive.ignored=true;
        ExpectCompatibilityFailure([&] {
            lume_glass::StartMaterialCapture(deceptive,[status] { return status; },[] { return E_FAIL; });
        },E_ACCESSDENIED,"only an explicit Allowed permission can start a capture");
        Expect(deceptive.starts==0&&deceptive.calls=="SG",
               "successful setter and false getter cannot bypass denied/prompt/missing permission");
    }
    Session permissionError;
    volatile bool permissionQueryFails=true;
    ExpectCompatibilityFailure([&] {
        lume_glass::StartMaterialCapture(permissionError,[&]() -> int { if (permissionQueryFails) throw std::runtime_error("permission unavailable"); return 4; },
                                       [] { return E_NOINTERFACE; });
    },E_NOINTERFACE,"permission query error fails closed");
    Expect(permissionError.starts==0,"permission query exception never starts a capture");
    unsigned permissionResources=0;
    volatile int startupPermission=3;
    ExpectCompatibilityFailure([&] {
        lume_glass::RequireCapturePermission([&] { return startupPermission; },[] { return E_FAIL; });
        ++permissionResources;
    },E_ACCESSDENIED,"startup prompt-required gate fails before creating graphics resources");
    Expect(permissionResources==0,"ungranted permission creates no capture resources");
    int livePermission=4;
    lume_glass::RequireCapturePermission([&] { return livePermission; },[] { return E_FAIL; });
    livePermission=2;
    ExpectCompatibilityFailure([&] {
        lume_glass::RequireCapturePermission([&] { return livePermission; },[] { return E_FAIL; });
    },E_ACCESSDENIED,"permission recheck detects a grant revoked after initialization");
    std::cout << "PASS material capture capability, permission and start ordering\n";
}
