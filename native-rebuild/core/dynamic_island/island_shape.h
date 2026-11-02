#pragma once
#include <algorithm>
#include <cmath>

namespace lume::dynamic_island {
// The attached island has concave top shoulders and convex lower corners.
// Negative is inside. Coordinates and radii use the same units (DIP or pixels).
inline float AttachedShapeDistance(float x,float y,float width,float height,float shoulder,float radius) {
    const float bounds=std::max({-x,x-width,-y,y-height});
    if(y<shoulder) {
        if(x<shoulder)return std::max(bounds,shoulder-std::hypot(x,y-shoulder));
        if(x>width-shoulder)return std::max(bounds,shoulder-std::hypot(x-width,y-shoulder));
        return bounds;
    }
    if(y>height-radius) {
        if(x<shoulder+radius)return std::max(bounds,std::hypot(x-shoulder-radius,y-height+radius)-radius);
        if(x>width-shoulder-radius)return std::max(bounds,std::hypot(x-width+shoulder+radius,y-height+radius)-radius);
    }
    return std::max({bounds,shoulder-x,x-width+shoulder});
}
inline float IslandShoulder(float height) {return std::min(10.f,height*.25f);}
inline float IslandLowerRadius(float height) {return std::min(28.f,height*.25f);}
inline float MorphShapeDistance(float x,float y,float width,float height,float capsuleRadius,float mix,float scale=1.f) {
    const float qx=std::abs(x-width*.5f)-width*.5f+capsuleRadius;
    const float qy=std::abs(y-height*.5f)-height*.5f+capsuleRadius;
    const float capsule=std::min(std::max(qx,qy),0.f)+std::hypot(std::max(qx,0.f),std::max(qy,0.f))-capsuleRadius;
    const float attached=AttachedShapeDistance(x,y,width,height,IslandShoulder(height/scale)*scale,IslandLowerRadius(height/scale)*scale);
    return capsule+(attached-capsule)*std::clamp(mix,0.f,1.f);
}
}
