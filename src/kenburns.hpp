#pragma once
// Ken Burns effect: smooth zoom + pan crop window over a cover-scaled image.
// Pure math, no dependencies — unit tested.

namespace sf {

struct CropRect {
    double x, y;  // top-left in source pixels
    double w, h;  // size in source pixels (aspect matches output)
};

// t: progress 0..1 across the scene.
// base_w/base_h: cover-scaled source image size (both >= out dims).
// out_w/out_h: output frame size.
// z0 -> z1: zoom factors (>= 1). 1.0 = whole image visible.
// (px0,py0) -> (px1,py1): pan position 0..1 inside the travel range.
CropRect kenburns_crop(double t, double base_w, double base_h, double out_w, double out_h,
                       double z0, double z1, double px0, double py0, double px1, double py1);

// Smoothstep easing.
double kb_ease(double t);

}  // namespace sf
