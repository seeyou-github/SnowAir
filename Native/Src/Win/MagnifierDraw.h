#pragma once
// 鼠标单击效果（波纹 / 放大光标）的录制侧补画：单击层被排除在桌面捕获之外，
// 采集线程用这里的状态在最终帧里补回来。

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
#include <windows.h>
#include "MouseFxParams.h"

namespace MagnifierDraw
{
	inline BYTE* pixelAt(std::vector<BYTE>& dst, int w, int h, int x, int y, bool bottomUp)
	{
		const int row = bottomUp ? h - 1 - y : y;
		return dst.data() + ((size_t)row * w + x) * 4;
	}

	// 源图统一按 top-down 采样；浮点坐标落在四个相邻像素之间时做双线性插值。
	inline void sampleBgra(const BYTE* src, int w, int h, float x, float y, BYTE out[4])
	{
		const int x0 = (int)std::floor(x), y0 = (int)std::floor(y);
		const int x1 = (std::min)(x0 + 1, w - 1), y1 = (std::min)(y0 + 1, h - 1);
		const int cx0 = std::clamp(x0, 0, w - 1), cy0 = std::clamp(y0, 0, h - 1);
		const float tx = x - x0, ty = y - y0;
		const BYTE* p00 = src + ((size_t)cy0 * w + cx0) * 4;
		const BYTE* p01 = src + ((size_t)cy0 * w + x1) * 4;
		const BYTE* p10 = src + ((size_t)y1 * w + cx0) * 4;
		const BYTE* p11 = src + ((size_t)y1 * w + x1) * 4;
		for (int c = 0; c < 4; ++c) {
			const float a = p00[c] * (1.f - tx) + p01[c] * tx;
			const float b = p10[c] * (1.f - tx) + p11[c] * tx;
			out[c] = (BYTE)std::lround(a * (1.f - ty) + b * ty);
		}
	}

	inline void blendPixel(BYTE* d, const BYTE src[4], float opacity = 1.f)
	{
		const float a = (src[3] / 255.f) * opacity;
		if (a <= 0.f) return;
		d[0] = (BYTE)std::lround(src[0] * a + d[0] * (1.f - a));
		d[1] = (BYTE)std::lround(src[1] * a + d[1] * (1.f - a));
		d[2] = (BYTE)std::lround(src[2] * a + d[2] * (1.f - a));
		d[3] = 255;
	}

	// ---------------- 单击层（波纹 / 放大光标） ----------------
	struct ClickFx
	{
		POINT screenPt{};
		ULONGLONG t{ 0 };
	};
	// WinCap 的单击层被排除在桌面捕获外；采集侧用这份状态在最终帧里补画。
	struct ClickOverlayFrame
	{
		bool enabled{ false };
		bool ripple{ false };
		bool zoom{ false };
		UINT32 color{ 0 };
		float dpi{ 1.f };
		std::vector<ClickFx> clicks;
		bool cursorValid{ false };
		POINT cursorPos{};
		POINT cursorHotspot{};
		int cursorW{ 0 }, cursorH{ 0 };
		std::vector<BYTE> cursorPix;   // top-down BGRA，straight alpha
	};

	inline void drawClickOverlay(std::vector<BYTE>& dst, int w, int h,
		int originX, int originY, const ClickOverlayFrame& state, bool bottomUp = true)
	{
		if (!state.enabled || w <= 0 || h <= 0 || dst.size() < (size_t)w * h * 4) return;
		const ULONGLONG now = GetTickCount64();
		if (state.ripple) {
			const BYTE b = (BYTE)((state.color >> 8) & 0xFFu);
			const BYTE g = (BYTE)((state.color >> 16) & 0xFFu);
			const BYTE r = (BYTE)((state.color >> 24) & 0xFFu);
			for (const auto& fx : state.clicks) {
				if (now < fx.t) continue;
				const ULONGLONG age = now - fx.t;
				if (age >= MouseFx::rippleLifeMs) continue;
				const float k = (float)age / (float)MouseFx::rippleLifeMs;
				const float radius = MouseFx::rippleRadius(k) * state.dpi;
				const float thickness = MouseFx::rippleWidth(k) * state.dpi;
				const float cx = (float)(fx.screenPt.x - originX);
				const float cy = (float)(fx.screenPt.y - originY);
				const float extent = radius + thickness * 0.5f + 1.f;
				const int left = (std::max)(0, (int)std::floor(cx - extent));
				const int right = (std::min)(w - 1, (int)std::ceil(cx + extent));
				const int top = (std::max)(0, (int)std::floor(cy - extent));
				const int bottom = (std::min)(h - 1, (int)std::ceil(cy + extent));
				const float opacity = MouseFx::rippleAlpha(k);
				for (int y = top; y <= bottom; ++y) {
					for (int x = left; x <= right; ++x) {
						const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
						const float dist = std::sqrt(dx * dx + dy * dy);
						const float coverage = std::clamp(thickness * 0.5f + 0.5f - std::fabs(dist - radius), 0.f, 1.f);
						const BYTE s[4]{ b, g, r, 255 };
						blendPixel(pixelAt(dst, w, h, x, y, bottomUp), s, opacity * coverage);
					}
				}
			}
		}

		if (!state.zoom || !state.cursorValid || state.clicks.empty()
			|| state.cursorW <= 0 || state.cursorH <= 0
			|| state.cursorPix.size() < (size_t)state.cursorW * state.cursorH * 4) return;
		if (now < state.clicks.back().t) return;
		const ULONGLONG age = now - state.clicks.back().t;
		if (age >= 320) return;
		const float scale = 1.f + 0.8f * std::sin((float)age / 320.f * 3.14159265f);
		const float left = state.cursorPos.x - originX - state.cursorHotspot.x * scale;
		const float top = state.cursorPos.y - originY - state.cursorHotspot.y * scale;
		const int right = (int)std::ceil(left + state.cursorW * scale);
		const int bottom = (int)std::ceil(top + state.cursorH * scale);
		for (int y = (std::max)(0, (int)std::floor(top)); y < (std::min)(h, bottom); ++y) {
			for (int x = (std::max)(0, (int)std::floor(left)); x < (std::min)(w, right); ++x) {
				const float sx = (x + 0.5f - left) / scale - 0.5f;
				const float sy = (y + 0.5f - top) / scale - 0.5f;
				if (sx < 0.f || sy < 0.f || sx > state.cursorW - 1.f || sy > state.cursorH - 1.f) continue;
				BYTE s[4]{};
				sampleBgra(state.cursorPix.data(), state.cursorW, state.cursorH, sx, sy, s);
				blendPixel(pixelAt(dst, w, h, x, y, bottomUp), s);
			}
		}
	}
}
