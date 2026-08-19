#pragma once

/******************************************************************************
view_utils - resolution independence helpers.

Pure math (no SFML/graphics dependency) so it can be unit tested. The game
renders against a fixed "virtual" resolution and the result is scaled to the
actual window through an SFML view whose viewport is computed here. This keeps
the aspect ratio constant (letterbox/pillarbox bars) instead of stretching.
******************************************************************************/

// A viewport rectangle in normalized [0,1] coordinates (SFML view viewport).
struct ViewportRect {
	float x;
	float y;
	float w;
	float h;
};

// Compute the letterbox/pillarbox viewport that fits a virt_w x virt_h design
// area into a win_w x win_h window while preserving aspect ratio.
ViewportRect letterbox_viewport(unsigned int win_w, unsigned int win_h,
                                float virt_w, float virt_h);
