#include "view_utils.h"

ViewportRect letterbox_viewport(unsigned int win_w, unsigned int win_h,
                                float virt_w, float virt_h)
{
	ViewportRect vp{ 0.f, 0.f, 1.f, 1.f };

	if (win_w == 0 || win_h == 0 || virt_w <= 0.f || virt_h <= 0.f) {
		return vp;
	}

	const float window_ratio = static_cast<float>(win_w) / static_cast<float>(win_h);
	const float view_ratio   = virt_w / virt_h;

	if (window_ratio >= view_ratio) {
		// Window is wider than the content -> bars on the left/right (pillarbox).
		vp.w = view_ratio / window_ratio;
		vp.x = (1.f - vp.w) / 2.f;
	} else {
		// Window is taller than the content -> bars on top/bottom (letterbox).
		vp.h = window_ratio / view_ratio;
		vp.y = (1.f - vp.h) / 2.f;
	}

	return vp;
}
