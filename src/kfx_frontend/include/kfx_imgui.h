// ImGui edits `int`/`float` in place through pointers, but every integer in the game is int64_t and every
// float a double. These wrappers copy to a temporary of ImGui's type and back; same-type calls work too.
#pragma once
#include <cstdint>
#include "imgui.h"
namespace kfximgui {
template<class I> inline bool InputInt(const char *label, I *v, int step = 1, int step_fast = 100, ImGuiInputTextFlags flags = 0)
{ int t = static_cast<int>(*v); const bool c = ImGui::InputInt(label, &t, step, step_fast, flags); if (c) *v = static_cast<I>(t); return c; }
template<class F> inline bool InputFloat(const char *label, F *v, double step = 0.0, double step_fast = 0.0, const char *fmt = "%.3f", ImGuiInputTextFlags flags = 0)
{ float t = static_cast<float>(*v); const bool c = ImGui::InputFloat(label, &t, static_cast<float>(step), static_cast<float>(step_fast), fmt, flags); if (c) *v = static_cast<F>(t); return c; }
template<class F> inline bool SliderFloat(const char *label, F *v, double vmin, double vmax, const char *fmt = "%.3f", ImGuiSliderFlags flags = 0)
{ float t = static_cast<float>(*v); const bool c = ImGui::SliderFloat(label, &t, static_cast<float>(vmin), static_cast<float>(vmax), fmt, flags); if (c) *v = static_cast<F>(t); return c; }
template<class I> inline bool SliderInt(const char *label, I *v, int64_t vmin, int64_t vmax, const char *fmt = "%d", ImGuiSliderFlags flags = 0)
{ int t = static_cast<int>(*v); const bool c = ImGui::SliderInt(label, &t, static_cast<int>(vmin), static_cast<int>(vmax), fmt, flags); if (c) *v = static_cast<I>(t); return c; }
template<class I> inline bool DragInt(const char *label, I *v, double speed = 1.0, int64_t vmin = 0, int64_t vmax = 0, const char *fmt = "%d", ImGuiSliderFlags flags = 0)
{ int t = static_cast<int>(*v); const bool c = ImGui::DragInt(label, &t, static_cast<float>(speed), static_cast<int>(vmin), static_cast<int>(vmax), fmt, flags); if (c) *v = static_cast<I>(t); return c; }
template<class F> inline bool DragFloat(const char *label, F *v, double speed = 1.0, double vmin = 0.0, double vmax = 0.0, const char *fmt = "%.3f", ImGuiSliderFlags flags = 0)
{ float t = static_cast<float>(*v); const bool c = ImGui::DragFloat(label, &t, static_cast<float>(speed), static_cast<float>(vmin), static_cast<float>(vmax), fmt, flags); if (c) *v = static_cast<F>(t); return c; }
template<class I> inline bool Combo(const char *label, I *current, const char *const items[], int64_t count, int64_t height = -1)
{ int t = static_cast<int>(*current); const bool c = ImGui::Combo(label, &t, items, static_cast<int>(count), static_cast<int>(height)); if (c) *current = static_cast<I>(t); return c; }
template<class I> inline bool Combo(const char *label, I *current, const char *items_separated_by_zeros, int64_t height = -1)
{ int t = static_cast<int>(*current); const bool c = ImGui::Combo(label, &t, items_separated_by_zeros, static_cast<int>(height)); if (c) *current = static_cast<I>(t); return c; }
} // namespace kfximgui
