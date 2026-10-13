/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file safe_path.c
 *     Which file names from content may be opened. See safe_path.h.
 */
/******************************************************************************/
#include "pre_inc.h"
#include <string.h>
#include <strings.h>
#include "safe_path.h"
#include "post_inc.h"

static int is_separator(char c)
{
    return (c == '/') || (c == '\\');
}

/** CON, PRN, AUX, NUL, COM1-9, LPT1-9, with or without an extension: names Windows treats as devices. */
static int component_is_device(const char *c, size_t len)
{
    size_t stem = 0;
    while ((stem < len) && (c[stem] != '.'))
        stem++;
    while ((stem > 0) && (c[stem - 1] == ' '))
        stem--;
    if (stem == 3)
        return (strncasecmp(c, "con", 3) == 0) || (strncasecmp(c, "prn", 3) == 0)
            || (strncasecmp(c, "aux", 3) == 0) || (strncasecmp(c, "nul", 3) == 0);
    if (stem == 4)
        return ((strncasecmp(c, "com", 3) == 0) || (strncasecmp(c, "lpt", 3) == 0)) && (c[3] >= '1') && (c[3] <= '9');
    return 0;
}

int path_is_plain_relative(const char *path)
{
    if ((path == NULL) || (path[0] == '\0'))
        return 0;
    const size_t len = strlen(path);
    if (len >= 1024)
        return 0;
    if (is_separator(path[0]))
        return 0;
    size_t start = 0;
    for (size_t i = 0; i <= len; i++)
    {
        const unsigned char c = (unsigned char)path[i];
        if (i < len)
        {
            if ((c < 0x20) || (c == 0x7F) || (c == ':') || (c == '*') || (c == '?') || (c == '"') || (c == '<') || (c == '>') || (c == '|'))
                return 0;
        }
        if ((i == len) || is_separator((char)c))
        {
            const size_t clen = i - start;
            if (clen == 0)
                return 0; // "a//b", or a trailing separator
            if ((clen == 2) && (path[start] == '.') && (path[start + 1] == '.'))
                return 0;
            // Windows drops trailing dots and spaces from a name, so "..." and ".. " mean ".."
            size_t trimmed = clen;
            while ((trimmed > 0) && ((path[start + trimmed - 1] == '.') || (path[start + trimmed - 1] == ' ')))
                trimmed--;
            if ((trimmed == 0) && (clen > 1))
                return 0;
            if (component_is_device(path + start, clen))
                return 0;
            start = i + 1;
        }
    }
    return 1;
}

int path_cannot_escape(const char *path)
{
    if (path == NULL)
        return 1;
    const size_t len = strlen(path);
    if (len >= 1024)
        return 0;
    if ((len > 0) && is_separator(path[0]))
        return 0;
    size_t start = 0;
    for (size_t i = 0; i <= len; i++)
    {
        const unsigned char c = (unsigned char)path[i];
        if ((i < len) && ((c < 0x20) || (c == 0x7F) || (c == ':')))
            return 0;
        if ((i == len) || is_separator((char)c))
        {
            const size_t clen = i - start;
            if (clen > 1)
            {
                size_t trimmed = clen;
                while ((trimmed > 0) && ((path[start + trimmed - 1] == '.') || (path[start + trimmed - 1] == ' ')))
                    trimmed--;
                if (trimmed == 0)
                    return 0; // "..", "...", ". ."
                if (component_is_device(path + start, clen))
                    return 0;
            }
            else if ((clen == 1) && (path[start] == ' '))
                return 0;
            start = i + 1;
        }
    }
    return 1;
}
