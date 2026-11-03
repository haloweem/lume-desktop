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
    lume_glass::StartMaterialCapture(healthy,[] { return E_FAIL; });
    Expect(healthy.starts==1&&healthy.calls=="SGC"&&!healthy.borderRequired,
           "supported session verifies border configuration before starting once");

    for(const HRESULT error : {E_NOINTERFACE,E_ACCESSDENIED}) {
        Session denied;denied.setterFails=true;
        ExpectCompatibilityFailure([&] { lume_glass::StartMaterialCapture(denied,[&] { return error; }); },
                                   error,"failed border configuration aborts capture");
        Expect(denied.starts==0&&denied.calls=="S","configuration failure never starts capture");
    }
    Session ignored;ignored.ignored=true;
    ExpectCompatibilityFailure([&] { lume_glass::StartMaterialCapture(ignored,[] { return E_FAIL; }); },
                               E_ACCESSDENIED,"system retaining its border rejects decorative capture");
    Expect(ignored.starts==0&&ignored.calls=="SG","ignored configuration never starts capture");

    Session unreadable;unreadable.getterFails=true;
    ExpectCompatibilityFailure([&] { lume_glass::StartMaterialCapture(unreadable,[] { return E_UNEXPECTED; }); },
                               E_UNEXPECTED,"failed border readback aborts capture");
    Expect(unreadable.starts==0,"unverified border state never starts capture");

    Session unavailable;unavailable.startFails=true;
    bool ordinaryFailure=false;
    try { lume_glass::StartMaterialCapture(unavailable,[] { return E_FAIL; }); }
    catch (const std::runtime_error&) { ordinaryFailure=true; }
    Expect(ordinaryFailure&&unavailable.starts==1,
           "source start failure retains ordinary error handling rather than terminal compatibility state");
    std::cout << "PASS material capture capability, permission and start ordering\n";
}
