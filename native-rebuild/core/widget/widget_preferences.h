#pragma once
#include <string>
namespace lume::widget {
// Only geometry and common appearance; each feature owns its own file/lifecycle.
struct Preferences {
    bool enabled=false,locked=false,frosted=true,placed=false;
    bool solid=false;  // "普通": an opaque panel without glass.
    int panelColor=0x2c2c2e;  // RGB of the solid panel.
    int x=0,y=0,width=320,height=152;
};
// fitToScreen: the first-run default size shrinks on small displays
// (window::FitScale); sizes the user has set are kept.
struct Limits {int defaultWidth=320,defaultHeight=152,minWidth=320,minHeight=152,maxWidth=720,maxHeight=720;bool fitToScreen=false;};
Preferences Defaults(Limits);
bool Valid(const Preferences&,Limits);
// A live window measured on a display with another DPI can come out a pixel
// outside the limits; clamp it before saving so enabling cannot fail.
Preferences Clamped(Preferences,Limits);
bool Decode(const std::string&,Preferences&,Limits);
bool Encode(const Preferences&,std::string&,Limits);
}
