#include "kenburns.hpp"

#include <algorithm>
#include <cmath>

namespace sf {

double kb_ease(double t) {
    t = std::min(1.0, std::max(0.0, t));
    return t * t * (3.0 - 2.0 * t);  // smoothstep
}

CropRect kenburns_crop(double t, double base_w, double base_h, double out_w, double out_h,
                       double z0, double z1, double px0, double py0, double px1, double py1) {
    const double e = kb_ease(t);
    const double z = z0 + (z1 - z0) * e;
    const double zs = std::max(1.0, z);

    // Aspect-correct crop: fit the limiting dimension, keep output aspect.
    double cw = base_w / zs;
    double ch = cw * out_h / out_w;
    if (ch > base_h) {
        ch = base_h / zs;
        cw = ch * out_w / out_h;
    }
    cw = std::min(cw, base_w);
    ch = std::min(ch, base_h);

    const double px = px0 + (px1 - px0) * e;
    const double py = py0 + (py1 - py0) * e;
    const double pcx = std::min(1.0, std::max(0.0, px));
    const double pcy = std::min(1.0, std::max(0.0, py));

    CropRect r;
    r.w = cw;
    r.h = ch;
    r.x = (base_w - cw) * pcx;
    r.y = (base_h - ch) * pcy;
    // Clamp for floating point safety.
    r.x = std::min(std::max(0.0, r.x), base_w - cw);
    r.y = std::min(std::max(0.0, r.y), base_h - ch);
    return r;
}

}  // namespace sf
