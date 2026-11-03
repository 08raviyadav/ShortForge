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
    const double z = std::max(1.0, z0 + (z1 - z0) * e);

    // Largest aspect-correct rectangle inscribed in the base image at zoom 1,
    // then divided by the zoom factor. Comparing the *unzoomed* rectangle
    // against the base keeps the crop continuous in z — comparing the zoomed
    // size instead causes a visible jump cut mid-scene on wide/square images.
    double fw = base_w;
    double fh = fw * out_h / out_w;
    if (fh > base_h) {
        fh = base_h;
        fw = fh * out_w / out_h;
    }
    const double cw = fw / z;
    const double ch = fh / z;

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
