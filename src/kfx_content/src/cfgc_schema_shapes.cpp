/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file cfgc_schema_shapes.cpp
 *     See cfgc_schema_shapes.h.
 */
#include "pre_inc.h"
#include "cfgc_schema_shapes.h"

#include <cstdlib>
#include <sstream>
#include "post_inc.h"

/******************************************************************************/
// Builds the parts of a key from its shape text.
void cfgc_apply_shape(CfgFieldSpec &f, const std::string &shape, const CfgTableNames &table_names)
{
    f.parts.clear();
    f.whole_string = false;
    f.repeat_last = false;
    if (shape == "text")
    {
        f.whole_string = true; // free text: the loader takes the line as it is
        CfgValueSpec p;
        p.kind = CfgKind_Custom;
        f.parts.push_back(p);
        return;
    }
    std::istringstream in(shape);
    std::string tok;
    while (in >> tok)
    {
        if (tok == "...")
        {
            f.repeat_last = true;
            continue;
        }
        size_t times = 1;
        const size_t star = tok.rfind('*');
        if (star != std::string::npos)
        {
            times = (size_t)std::atoi(tok.c_str() + star + 1);
            tok = tok.substr(0, star);
        }
        CfgValueSpec p;
        if (tok == "N")
            p.kind = CfgKind_Number;
        else if (tok == "S")
            p.kind = CfgKind_Custom;
        else if (tok == "I")
            p.kind = CfgKind_Icon;
        else if (tok.compare(0, 2, "E:") == 0)
        {
            p.kind = CfgKind_Enum;
            std::string rest = tok.substr(2);
            const size_t bar = rest.find('|');
            p.enum_registry = rest.substr(0, bar);
            while (bar != std::string::npos)
            {
                // literal extras after the registry name: E:creature|NULL
                size_t start = bar + 1, end;
                do
                {
                    end = rest.find('|', start);
                    p.enum_names.push_back(rest.substr(start, end == std::string::npos ? std::string::npos : end - start));
                    start = end + 1;
                } while (end != std::string::npos);
                break;
            }
        }
        else if (tok.compare(0, 2, "F:") == 0)
        {
            p.kind = CfgKind_Flags;
            const std::string what = tok.substr(2);
            const std::vector<std::string> listed = table_names(what);
            if (!listed.empty())
                p.enum_names = listed;
            else
            {
                const size_t bar = what.find('|');
                p.enum_registry = what.substr(0, bar);
                if (bar != std::string::npos)
                    p.enum_names.push_back(what.substr(bar + 1)); // extra literal (NULL)
            }
        }
        else if (tok.compare(0, 2, "T:") == 0)
        {
            p.kind = CfgKind_Enum;
            for (const std::string &n : table_names(tok.substr(2)))
                p.enum_names.push_back(n);
        }
        else if (tok.compare(0, 2, "R:") == 0)
        {
            // A number that may also be written as a name from a static table (RangeMin = MIN).
            p.kind = CfgKind_Number;
            for (const std::string &n : table_names(tok.substr(2)))
                p.enum_names.push_back(n);
        }
        for (size_t i = 0; i < times; i++)
            f.parts.push_back(p);
    }
}
