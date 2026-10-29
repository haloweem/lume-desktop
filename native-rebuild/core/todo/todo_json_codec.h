#pragma once
#include "core/todo/todo_state.h"
#include <cstddef>

namespace lume::todo {
class TodoJsonCodec {
  public:
    static constexpr std::size_t MaxBytes = 8 * 1024 * 1024;
    static constexpr int MaxDepth = 48;
    Error Decode(const std::string &text, TodoState &output, const ILocalTimePort &port) const;
    Error Encode(const TodoState &value, std::string &output) const;
    Error DecodeSidecar(const std::string &text, std::vector<std::string> &output,
                        std::vector<std::string> &ids) const;
    Error Operation(const TaskRecord &task, const std::string &id, bool deleted, Millis now,
                    std::string &output) const;
};
} // namespace lume::todo
