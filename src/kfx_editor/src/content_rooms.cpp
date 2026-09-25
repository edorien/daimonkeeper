/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file content_rooms.cpp
 *     The Room editor: a configuration of the shared entity window. See content_rooms.h.
 */
#include "pre_inc.h"
#include "content_rooms.h"
#include "content_entity.h"
#include "content_form.h"

#include <map>
#include <set>
#include <string>
#include <vector>
#include "post_inc.h"

/******************************************************************************/
std::vector<std::string> rooms_groups(const std::string &basename)
{
    if (basename == "room")
        return {"Build", "Capacity", "Roles", "Look and sound", "Advanced"};
    if (basename == "slab")
        return {"Digging", "Ownership", "Block flags", "Look and type"};
    if (basename == "block_health")
        return {"Health"};
    return {};
}

std::string rooms_group_of(const std::string &basename, const std::string &key)
{
    const std::string k = form_lower(key);
    static const std::map<std::string, std::map<std::string, std::set<std::string>>> table = {
        {"room", {
            {"Build", {"cost", "health", "slabassign", "properties", "paneltabindex"}},
            {"Capacity", {"totalcapacity", "usedcapacity", "storageheight", "slabsynergy"}},
            {"Roles", {"roles"}},
            {"Look and sound", {"nametextid", "tooltiptextid", "symbolsprites", "pointersprites", "ambientsndsample", "messages"}},
        }},
        {"slab", {
            {"Digging", {"isdiggable", "blockhealthindex", "indestructible", "goldheld"}},
            {"Ownership", {"isownable", "issafeland"}},
            {"Block flags", {"blockflags", "noblockflags", "blockflagsheight"}},
            {"Look and type", {"tooltiptextid", "fillstyle", "category", "slbid", "wibble", "animated", "wlbtype"}},
        }},
    };
    const auto kind = table.find(basename);
    if (kind != table.end())
        for (const auto &g : kind->second)
            if (g.second.count(k))
                return g.first;
    const std::vector<std::string> groups = rooms_groups(basename);
    return groups.empty() ? std::string() : groups.back(); // room: Advanced; slab: Look and type; health table: Health
}

/******************************************************************************/
namespace {

EntityEditor &editor()
{
    static EntityEditor e = [] {
        EntityConfig c;
        c.title = "Room Editor";
        c.imgui_id = "##ContentRooms";
        c.kind = "terrain";
        c.file_name = "terrain.cfg";
        c.empty_text = "Pick a campaign or map pack to edit its rooms and terrain.";

        // A room is built on a slab kind: show what that slab is.
        EntityLink slab;
        slab.mode_basename = "room";
        slab.key = "SlabAssign";
        slab.group = "Build";
        slab.kind = "terrain";
        slab.file_name = "terrain.cfg";
        slab.target_basename = "slab";
        slab.title = "Built on";
        slab.show = {"BlockFlags", "IsDiggable", "IsOwnable", "BlockHealthIndex"};
        slab.note = "The slab's own numbers are on the Terrain tab.";
        c.links.push_back(slab);

        auto mode = [&](const char *label, const char *basename) {
            EntityMode m;
            m.label = label;
            m.basename = basename;
            m.groups = rooms_groups(basename);
            const std::string base = basename;
            m.group_of = [base](const std::string &key) { return rooms_group_of(base, key); };
            return m;
        };
        EntityMode rooms = mode("Rooms", "room");
        rooms.compare_keys = {"Cost", "Health", "StorageHeight", "TotalCapacity", "PanelTabIndex"};
        rooms.unique_keys = {"PanelTabIndex", "SlabAssign"};
        rooms.unique_group = "Build";
        rooms.group_notes["Roles"] = "Roles decide what creatures and the computer players use a room for. Change them only if you know what depends on them.";
        c.modes.push_back(rooms);

        EntityMode slabs = mode("Terrain", "slab");
        slabs.compare_keys = {"GoldHeld", "IsDiggable", "BlockHealthIndex", "IsOwnable", "Indestructible"};
        slabs.group_notes["Digging"] = "This changes every slab of this kind on the map. BlockHealthIndex points at a row of the Health table tab.";
        slabs.group_notes["Block flags"] = "Block flags decide what can walk, dig or build on a slab; a wrong change can break pathing and digging.";
        c.modes.push_back(slabs);

        EntityMode health = mode("Health table", "block_health");
        health.single = true;
        health.group_notes["Health"] = "How many hits each kind of block takes to dig or break; a slab's BlockHealthIndex is the position in this list (1 is the first).";
        c.modes.push_back(health);
        return EntityEditor(c);
    }();
    return e;
}

} // namespace

void content_rooms_open(bool map_host)
{
    editor().open(map_host);
}

void content_rooms_frame(void)
{
    editor().frame();
}

bool content_rooms_is_open(void)
{
    return editor().is_open();
}

void content_rooms_show(int mode, const char *group)
{
    editor().show((size_t)mode, group != nullptr ? group : "");
}
