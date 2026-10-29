#pragma once
#include "core/todo/todo_repository.h"
#include "core/todo/todo_time.h"
#include <cstdlib>
#include <iostream>
#include <map>
#include <vector>

namespace todo_test {
inline void Expect(bool value, const char *message) {
    if (!value) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
class Utc final : public lume::todo::ILocalTimePort {
  public:
    std::optional<lume::todo::Millis> FromLocal(const lume::todo::LocalDateTime &x) const override {
        if (!lume::todo::ValidLocal(x))
            return {};
        const auto y = static_cast<lume::todo::Millis>(x.date.year - 1);
        lume::todo::Millis days =
            365 * y + y / 4 - y / 100 + y / 400 + lume::calendar::DayOfYear(x.date) - 1 - 719162;
        return days * 86400000 + x.hour * 3600000 + x.minute * 60000 + x.second * 1000 + x.millisecond;
    }
    std::optional<lume::todo::LocalDateTime> ToLocal(lume::todo::Millis x) const override {
        auto days = x / 86400000;
        auto rest = x % 86400000;
        if (rest < 0) {
            --days;
            rest += 86400000;
        }
        if (days < -719162 || days > 2932896)
            return {};
        auto date = lume::calendar::AddDays({1970, 1, 1}, static_cast<int>(days));
        if (!date)
            return {};
        return lume::todo::LocalDateTime{*date, static_cast<int>(rest / 3600000),
                                         static_cast<int>(rest / 60000 % 60),
                                         static_cast<int>(rest / 1000 % 60), static_cast<int>(rest % 1000)};
    }
};
inline lume::todo::Millis At(int year, int month, int day, int hour = 0, int minute = 0, int second = 0,
                             int ms = 0) {
    Utc utc;
    return *utc.FromLocal({{year, month, day}, hour, minute, second, ms});
}
class Files final : public lume::settings::IFileStore {
  public:
    std::map<std::string, std::string> data;
    mutable std::vector<std::string> calls;
    std::string fail;
    std::string failAlso;
    bool Read(const std::string &p, std::string &out) const override {
        calls.push_back("read:" + p);
        if (fail == calls.back() || failAlso == calls.back())
            return false;
        auto it = data.find(p);
        if (it == data.end())
            return false;
        out = it->second;
        return true;
    }
    bool Exists(const std::string &p) const override {
        calls.push_back("exists:" + p);
        return data.count(p) != 0;
    }
    bool Write(const std::string &p, const std::string &x) override {
        calls.push_back("write:" + p);
        if (fail == calls.back() || failAlso == calls.back())
            return false;
        data[p] = x;
        return true;
    }
    bool Rename(const std::string &a, const std::string &b) override {
        calls.push_back("rename:" + a + ":" + b);
        if (fail == calls.back() || failAlso == calls.back() || !data.count(a))
            return false;
        data[b] = data.at(a);
        data.erase(a);
        return true;
    }
    bool Remove(const std::string &p) override {
        calls.push_back("remove:" + p);
        if (fail == calls.back() || failAlso == calls.back())
            return false;
        data.erase(p);
        return true;
    }
};
inline lume::todo::Command Add(lume::todo::Scope scope) {
    lume::todo::Command c;
    c.scope = std::move(scope);
    c.patch.title = "  task  ";
    c.patch.schedule = lume::todo::Schedule{At(2026, 9, 25, 9), At(2026, 9, 25, 10)};
    return c;
}
} // namespace todo_test
