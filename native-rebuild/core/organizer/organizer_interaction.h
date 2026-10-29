#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <optional>

namespace lume::organizer {

class Interaction final {
public:
    const std::vector<std::string>& Selected() const { return selected_; }
    bool IsSelected(std::string_view id) const;

    void Select(const std::vector<std::string>& visible,std::string_view id,
        bool control,bool shift,bool preserveGroup=false);
    void SelectAll(const std::vector<std::string>& visible);
    void Clear();
    void Reconcile(const std::vector<std::string>& visible);

    bool BeginDrag(const std::vector<std::string>& order,std::string_view grabbed);
    bool Dragging() const { return dragging_; }
    const std::vector<std::string>& Dragged() const { return dragged_; }
    const std::vector<std::string>& Preview() const { return preview_; }
    void MoveBefore(std::optional<std::string> before);
    void EndDrag();

private:
    std::vector<std::string> OrderedSelection(const std::vector<std::string>& order) const;
    std::vector<std::string> selected_;
    std::string anchor_;
    std::vector<std::string> dragged_;
    std::vector<std::string> preview_;
    std::vector<std::string> dragOrigin_;
    bool dragging_=false;
};
}
