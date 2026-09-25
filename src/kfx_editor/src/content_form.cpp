/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file content_form.cpp
 *     See content_form.h.
 */
#include "pre_inc.h"
#include "content_form.h"
#include "content_names.h"
#include "cfgc_help.h"
#include "frontgui_widgets.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <fstream>
#include <sstream>
#include "post_inc.h"

/******************************************************************************/
namespace {

const ImVec4 kIgnoredColour(0.74f, 0.56f, 0.88f, 1.0f); // keys the loader does not read
const ImVec4 kEditedColour(1.0f, 0.55f, 0.35f, 1.0f);
const ImVec4 kProblemColour(1.0f, 0.45f, 0.40f, 1.0f);

void draw_badge(const FieldView &v)
{
    if (v.pending)
        ImGui::TextColored(kEditedColour, "edited");
    else if (!v.is_set)
        ImGui::TextDisabled("default");
    else
        ImGui::TextColored(form_layer_colour(v.source), "%s", form_layer_name(v.source));
    if (v.overridden_here && v.has_beneath)
    {
        ImGui::SameLine();
        if (v.same_as_beneath)
            ImGui::TextDisabled("(same as before)");
        else
            ImGui::TextDisabled("(was %s)", v.beneath.c_str());
    }
}

// Flag list: a button with the current text opens a list of the names to tick. Returns true when changed.
bool flags_popup(const char *id, std::string &value, const std::vector<std::string> &names, float width)
{
    bool changed = false;
    const std::vector<std::string> current = form_split_words(value);
    auto has = [&](const std::string &n) {
        for (const std::string &c : current)
            if (form_lower(c) == form_lower(n))
                return true;
        return false;
    };
    // The button's own label is only the value (FeButton draws its label as given); the id comes from PushID.
    ImGui::PushID(id);
    const bool pressed = FeButton(value.empty() ? "(none)" : value.c_str(), ImVec2(width, 0));
    ImGui::PopID();
    if (pressed)
        ImGui::OpenPopup(id);
    if (ImGui::BeginPopup(id))
    {
        std::vector<std::string> picked;
        for (const std::string &n : names)
        {
            bool on = has(n);
            if (FeCheckbox(n.c_str(), &on))
                changed = true;
            if (on)
                picked.push_back(n);
        }
        if (changed)
            value = form_join_words(picked); // an empty list is an empty value
        ImGui::EndPopup();
    }
    return changed;
}

} // namespace

/******************************************************************************/
float form_col(float em)
{
    return ImGui::GetFontSize() * em;
}

ImVec4 form_layer_colour(CfgLayer l)
{
    switch (l)
    {
    case CfgLayer_Base: return ImVec4(0.62f, 0.62f, 0.66f, 1.0f);
    case CfgLayer_Campaign: return ImVec4(0.95f, 0.75f, 0.30f, 1.0f);
    default: return ImVec4(0.45f, 0.85f, 0.50f, 1.0f);
    }
}

const char *form_layer_name(CfgLayer l)
{
    return l == CfgLayer_Base ? "Base" : l == CfgLayer_Campaign ? "Campaign" : "Level";
}

std::string form_lower(std::string s)
{
    for (char &c : s)
        c = (char)std::tolower((unsigned char)c);
    return s;
}

bool form_parse_int(const std::string &s, int64_t &out)
{
    if (s.empty())
        return false;
    size_t i = (s[0] == '-') ? 1 : 0;
    if (i >= s.size() || s.size() - i > 18)
        return false;
    for (size_t k = i; k < s.size(); k++)
        if (!std::isdigit((unsigned char)s[k]))
            return false;
    out = std::atoll(s.c_str());
    return true;
}

std::vector<std::string> form_split_words(const std::string &text)
{
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string w;
    while (in >> w)
        out.push_back(w);
    return out;
}

std::string form_join_words(const std::vector<std::string> &v)
{
    std::string s;
    for (size_t i = 0; i < v.size(); i++)
        s += (i ? " " : "") + v[i];
    return s;
}

bool form_pick_name(const char *id, std::string &value, const std::vector<std::string> &options, float width)
{
    ImGui::SetNextItemWidth(width);
    if (options.empty())
    {
        char buf[128];
        snprintf(buf, sizeof(buf), "%s", value.c_str());
        if (FeTextInput(id, buf, sizeof(buf)))
        {
            value = buf;
            return true;
        }
        return false;
    }
    std::vector<std::string> items;
    int64_t cur = -1;
    bool unknown = true;
    for (const std::string &o : options)
        if (form_lower(o) == form_lower(value))
            unknown = false;
    if (unknown)
    {
        items.push_back(value.empty() ? std::string("(choose)") : value + " (unknown)");
        cur = 0;
    }
    const size_t offset = items.size();
    for (size_t i = 0; i < options.size(); i++)
    {
        items.push_back(options[i]);
        if (!unknown && form_lower(options[i]) == form_lower(value))
            cur = (int64_t)(offset + i);
    }
    std::vector<const char *> ptrs;
    for (const std::string &t : items)
        ptrs.push_back(t.c_str());
    int64_t sel = cur;
    if (FeCombo(id, &sel, ptrs.data(), (int64_t)ptrs.size()) && sel != cur && (size_t)sel >= offset)
    {
        value = options[(size_t)sel - offset];
        return true;
    }
    return false;
}

/******************************************************************************/
void FormContext::read_reference(const std::string &base_file_path)
{
    help.clear();
    spelling.clear();
    std::ifstream f(base_file_path, std::ios::binary);
    if (!f)
        return;
    std::stringstream ss;
    ss << f.rdbuf();
    const ConfigDocument doc = ConfigDocument::parse(ss.str());
    help = cfgc_extract_help(doc);
    for (const CfgLine &l : doc.lines())
        if (l.kind == CfgLine_Key && spelling.find(form_lower(l.name)) == spelling.end())
            spelling[form_lower(l.name)] = l.name;
}

void FormContext::refresh_problems()
{
    bad.clear();
    if (session == nullptr)
        return;
    for (const CfgDiagnostic &d : session->diagnostics())
        if (d.severity >= CfgSev_Warning)
            bad.insert(form_lower(d.section) + "/" + form_lower(d.key));
}

std::string FormContext::display_key(const CfgFieldSpec &f) const
{
    const auto it = spelling.find(form_lower(f.key));
    return it != spelling.end() ? it->second : f.key;
}

std::string FormContext::help_for(const std::string &section, const std::string &key) const
{
    const auto it = help.find(cfgc_help_key(section, key));
    return it != help.end() ? it->second : std::string();
}

void FormContext::draw_info_row(const std::string &label, const std::string &value, const std::string &note)
{
    ImGui::TextDisabled("%s", label.c_str());
    ImGui::SameLine(form_col(24));
    ImGui::TextUnformatted(value.c_str());
    if (!note.empty())
    {
        ImGui::SameLine(form_col(43));
        ImGui::TextDisabled("%s", note.c_str());
    }
}

void FormContext::draw_row(const std::string &section, const CfgFieldSpec &spec)
{
    const FieldView v = session->value_of(section, spec.key);
    const bool writable = session->writable();
    ImGui::PushID((section + "/" + spec.key).c_str());

    const bool problem = bad.count(form_lower(section) + "/" + form_lower(spec.key)) > 0;
    const bool ignored = spec.state == CfgState_Ignored;
    const bool locked = ignored || read_only.count(form_lower(spec.key)) > 0;
    const std::string label = display_key(spec);
    if (ignored)
        ImGui::TextColored(kIgnoredColour, "%s", label.c_str());
    else if (problem)
        ImGui::TextColored(kProblemColour, "%s", label.c_str());
    else if (v.overridden_here)
        ImGui::TextUnformatted(label.c_str());
    else
        ImGui::TextDisabled("%s", label.c_str());
    {
        const std::string h = help_for(section, spec.key);
        if ((!h.empty() || ignored) && ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
            if (!h.empty())
                ImGui::TextUnformatted(h.c_str());
            if (ignored)
                ImGui::TextColored(kIgnoredColour, "No effect in this build%s%s.", spec.note.empty() ? "" : ": ", spec.note.c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }
    ImGui::SameLine(form_col(24));

    std::string edited = v.text;
    bool changed = false;
    const std::vector<std::string> words = form_split_words(v.text);
    const CfgValueSpec *part = spec.parts.size() == 1 ? &spec.parts[0] : nullptr;
    int64_t n = 0;
    ImGui::BeginDisabled(!writable || locked);

    // The names a part offers: its static names (and literal extras such as NULL), then its registry's.
    auto options_of = [&](const CfgValueSpec &p) {
        std::vector<std::string> out = p.enum_names;
        if (!p.enum_registry.empty())
            for (const std::string &name : content_registry_list(session->names(), p.enum_registry))
                if (std::find(out.begin(), out.end(), name) == out.end())
                    out.push_back(name);
        return out;
    };
    bool all_named = spec.parts.size() > 1 && spec.parts.size() <= 3 && !spec.whole_string;
    for (const CfgValueSpec &p : spec.parts)
        all_named = all_named && p.kind == CfgKind_Enum && (!p.enum_names.empty() || !p.enum_registry.empty());

    // A per-level array ("Cost = 150 150 ..."): every part a number, three or more.
    bool all_numbers = spec.parts.size() >= 3 && spec.parts.size() <= 16 && !spec.whole_string;
    for (const CfgValueSpec &p : spec.parts)
        all_numbers = all_numbers && (p.kind == CfgKind_Number || p.kind == CfgKind_Coord);

    if (all_numbers)
    {
        ImGui::TextDisabled("(values below)");
    }
    else if (part != nullptr && part->kind == CfgKind_Number && part->min == 0 && part->max == 1 && form_parse_int(v.text, n))
    {
        bool on = n != 0;
        if (FeCheckbox("##v", &on))
        {
            edited = on ? "1" : "0";
            changed = true;
        }
    }
    else if (part != nullptr && (part->kind == CfgKind_Number || part->kind == CfgKind_Unspecified || part->kind == CfgKind_Custom)
        && !all_numbers && form_parse_int(v.text, n))
    {
        ImGui::SetNextItemWidth(form_col(11));
        int64_t val = n;
        if (FeInputInt("##v", &val, 1, 10, std::max<int64_t>(part->min, INT32_MIN), std::min<int64_t>(part->max, UINT32_MAX)) && val != n)
        {
            edited = std::to_string((long long)val);
            changed = true;
        }
    }
    else if (part != nullptr && spec.repeat_last && part->kind == CfgKind_Enum)
    {
        // A list of names on one line (HostileTowards = a list of creatures): tick them.
        changed = flags_popup("##list", edited, options_of(*part), form_col(16));
    }
    else if (part != nullptr && part->kind == CfgKind_Flags && (!part->enum_names.empty() || !part->enum_registry.empty()))
    {
        changed = flags_popup("##flags", edited, options_of(*part), form_col(16));
    }
    else if (part != nullptr && part->kind == CfgKind_Enum && (!part->enum_names.empty() || !part->enum_registry.empty())
        && (form_parse_int(v.text, n) == false))
    {
        std::string val = v.text;
        if (form_pick_name("##v", val, options_of(*part), form_col(16)))
        {
            edited = val;
            changed = true;
        }
    }
    else if (all_named)
    {
        // Several names on one line ("SlabKind = DOOR_WOODEN DOOR_WOODEN2"): one drop-down per word.
        std::vector<std::string> w = words;
        w.resize(spec.parts.size());
        for (size_t i = 0; i < spec.parts.size(); i++)
        {
            if (i > 0)
                ImGui::SameLine();
            ImGui::PushID((int)i);
            if (form_pick_name("##part", w[i], options_of(spec.parts[i]), form_col(14)))
                changed = true;
            ImGui::PopID();
        }
        if (changed)
            edited = form_join_words(w);
    }
    else
    {
        char buf[256];
        snprintf(buf, sizeof(buf), "%s", v.text.c_str());
        ImGui::SetNextItemWidth(form_col(16));
        if (FeTextInput("##v", buf, sizeof(buf)))
        {
            edited = buf;
            changed = true;
        }
    }
    ImGui::EndDisabled();
    if (changed)
        session->set(section, spec.key, edited);

    ImGui::SameLine(form_col(43) + (all_named ? form_col(14) : 0.0f));
    draw_badge(v);
    if (ignored)
    {
        ImGui::SameLine();
        ImGui::TextColored(kIgnoredColour, "no effect");
    }
    else if (locked)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("read only");
    }
    if (writable && v.overridden_here)
    {
        ImGui::SameLine(form_col(64) + (all_named ? form_col(14) : 0.0f));
        if (FeButton("Reset", ImVec2(form_col(5), 0)))
            session->reset(section, spec.key);
    }
    if (all_numbers)
    {
        // Under the key, indented. Arrays with a curve (power cost and strength) are a wrapped row of small boxes; the
        // others are a column, one labelled box per value, so nothing runs past the edge of the window.
        std::vector<std::string> w = words;
        w.resize(spec.parts.size(), "0");
        std::vector<float> curve;
        bool array_changed = false;
        const bool as_row = plot_keys.count(form_lower(spec.key)) > 0;
        const bool per_level = form_lower(spec.key) == "levelstrainvalues"; // the experience needed to reach level 2, 3, ...
        ImGui::Indent(form_col(2));
        ImGui::BeginDisabled(!writable || locked);
        const float cell_w = form_col(6.5f);
        for (size_t i = 0; i < w.size(); i++)
        {
            ImGui::PushID((int)i);
            if (as_row)
            {
                // Wrap to the next line when the next box would not fit: the right edge of the content is the cursor's
                // x plus what is available, measured at the start of the line the cursor is on.
                const float right_edge = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
                const float next_x = ImGui::GetItemRectMax().x + ImGui::GetStyle().ItemSpacing.x;
                if (i > 0 && next_x + cell_w <= right_edge)
                    ImGui::SameLine();
            }
            else
            {
                if (per_level)
                    ImGui::TextDisabled("Level %zu", i + 2);
                else
                    ImGui::TextDisabled("%zu", i + 1);
                ImGui::SameLine(form_col(9));
            }
            int64_t val = 0;
            form_parse_int(w[i], val);
            ImGui::SetNextItemWidth(as_row ? cell_w : form_col(11));
            if (ImGui::InputScalar("##cell", ImGuiDataType_S64, &val, nullptr, nullptr, "%lld"))
            {
                const CfgValueSpec &p = spec.parts[i];
                val = std::max<int64_t>(std::max<int64_t>(p.min, INT32_MIN), std::min<int64_t>(std::min<int64_t>(p.max, UINT32_MAX), val));
                w[i] = std::to_string((long long)val);
                array_changed = true;
            }
            ImGui::PopID();
            curve.push_back((float)val);
        }
        ImGui::EndDisabled();
        if (curve.size() >= 2 && plot_keys.count(form_lower(spec.key)))
            ImGui::PlotLines("##curve", curve.data(), (int)curve.size(), 0, nullptr, FLT_MAX, FLT_MAX, ImVec2(form_col(30), form_col(3)));
        ImGui::Unindent(form_col(2));
        if (array_changed)
            session->set(section, spec.key, form_join_words(w));
    }
    ImGui::PopID();
}
