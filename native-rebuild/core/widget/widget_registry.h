#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace lume::widget {

class IWidget {
public:
    virtual ~IWidget() = default;
    virtual bool Start() = 0;
    virtual void Stop() = 0;
    virtual void Suspend() = 0;
    virtual void Resume() = 0;
    virtual void Dispose() = 0;
};

using WidgetFactory = std::function<std::unique_ptr<IWidget>()>;

class WidgetRegistry final {
public:
    bool Register(std::string id, WidgetFactory factory);
    const WidgetFactory* Find(const std::string& id) const;

private:
    std::unordered_map<std::string, WidgetFactory> factories_;
};

}  // namespace lume::widget
