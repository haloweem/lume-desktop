#pragma once
#include "core/settings/file_store.h"
#include "core/todo/todo_types.h"
#include <functional>
#include <memory>

namespace lume::todo {
class TodoRepository {
  public:
    using IdFactory = std::function<std::string()>;
    // The caller creates baseDirectory/accountKey. Core never accesses platform paths.
    TodoRepository(settings::IFileStore &files, const ILocalTimePort &time, std::string baseDirectory,
                   IdFactory ids, bool enqueueSync = true);
    ~TodoRepository();
    TodoRepository(const TodoRepository &) = delete;
    TodoRepository &operator=(const TodoRepository &) = delete;
    static bool ValidAccountKey(const std::string &key) noexcept;
    LoadResult SwitchAccount(const std::string &key);
    RepositorySnapshot Read() const;
    CommandResult Dispatch(const Command &command, Millis now);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace lume::todo
