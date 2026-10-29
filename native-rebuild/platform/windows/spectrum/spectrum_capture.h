#pragma once
#include <functional>
#include <memory>
#include <vector>

namespace lume::platform::windows {
// System loopback analysis of whatever the default output device plays, so
// the spectrum follows any player. UI-thread-owned lifecycle; the worker only
// publishes magnitudes and a notification (post a message, touch no UI).
// Stop joins the worker.
class SpectrumCapture final {
public:
    explicit SpectrumCapture(std::function<void()> notify);
    ~SpectrumCapture();
    SpectrumCapture(const SpectrumCapture&)=delete;
    SpectrumCapture& operator=(const SpectrumCapture&)=delete;
    bool Start();
    void Stop();
    bool Running()const;
    struct Frame { std::vector<float> left,right; unsigned sampleRate=0; };
    // Latest FFT magnitudes of the left and right channel; empty while
    // nothing has been heard yet.
    Frame Latest()const;
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
