/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file cfgc_strings.cpp
 *     See cfgc_strings.h.
 */
#include "pre_inc.h"
#include "cfgc_strings.h"

#include <algorithm>
#include "bflib_text.h"
#include "post_inc.h"

/******************************************************************************/
bool cfgc_lang_is_legacy_codepage(const std::string &lang_code)
{
    return lang_code != "chi" && lang_code != "cht" && lang_code != "jpn" && lang_code != "kor";
}

StringsFile StringsFile::parse(const std::string &bytes)
{
    StringsFile f;
    size_t start = 0;
    while (start < bytes.size())
    {
        const size_t nul = bytes.find('\0', start);
        if (nul == std::string::npos)
        {
            f.entries_.push_back(bytes.substr(start));
            f.unterminated_tail_ = true;
            break;
        }
        f.entries_.push_back(bytes.substr(start, nul - start));
        start = nul + 1;
    }
    return f;
}

std::string StringsFile::serialize() const
{
    std::string out;
    for (size_t i = 0; i < entries_.size(); i++)
    {
        out += entries_[i];
        if (!(unterminated_tail_ && i + 1 == entries_.size()))
            out += '\0';
    }
    return out;
}

std::string StringsFile::serialize_for_write() const
{
    StringsFile copy = *this;
    copy.unterminated_tail_ = false; // a file we write always ends in a NUL
    std::string out = copy.serialize();
    if (out.size() < 16)
        out.append(16 - out.size(), '\0'); // the loader refuses anything shorter
    return out;
}

const std::string &StringsFile::raw(size_t id) const
{
    static const std::string none;
    return id < entries_.size() ? entries_[id] : none;
}

void StringsFile::set_raw(size_t id, const std::string &raw)
{
    if (id >= entries_.size())
    {
        // A tail without its NUL becomes an ordinary entry when something is added after it.
        unterminated_tail_ = false;
        entries_.resize(id + 1);
    }
    entries_[id] = raw;
    if (unterminated_tail_ && id + 1 != entries_.size())
        unterminated_tail_ = false;
}

void StringsFile::trim_trailing_empty()
{
    while (!entries_.empty() && entries_.back().empty())
        entries_.pop_back();
    if (entries_.empty())
        unterminated_tail_ = false;
}

std::string cfgc_strings_decode(const std::string &raw)
{
    std::string out;
    for (unsigned char b : raw)
    {
        uint64_t cp = codepage_byte_to_unicode(b);
        if (cp == 0)
            cp = '?';
        char buf[8];
        const size_t n = encode_utf8_codepoint(cp, buf, sizeof(buf));
        out.append(buf, n);
    }
    return out;
}

StringsEncodeResult cfgc_strings_encode(const std::string &utf8)
{
    StringsEncodeResult r;
    size_t i = 0;
    while (i < utf8.size())
    {
        const unsigned char c = (unsigned char)utf8[i];
        uint64_t cp;
        size_t len;
        if (c < 0x80) { cp = c; len = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 4; }
        else { cp = 0xFFFD; len = 1; }
        if (len > 1)
        {
            if (i + len > utf8.size())
            {
                cp = 0xFFFD;
                len = utf8.size() - i;
            }
            else
                for (size_t k = 1; k < len; k++)
                    cp = (cp << 6) | ((unsigned char)utf8[i + k] & 0x3F);
        }
        i += len;
        if (cp == '\r' || cp == '\n' || (cp >= 0x20 && cp < 0x7F))
        {
            r.bytes += (char)cp;
            continue;
        }
        const int64_t b = codepage_unicode_to_byte(cp);
        if (b > 0)
            r.bytes += (char)b;
        else
        {
            r.ok = false;
            if (std::find(r.unrepresentable.begin(), r.unrepresentable.end(), cp) == r.unrepresentable.end())
                r.unrepresentable.push_back(cp);
        }
    }
    return r;
}

StringsStack::Effective StringsStack::effective(size_t id) const
{
    Effective e;
    for (int l = StringLayer_Level; l >= 0; l--)
    {
        if (!present[l] || layers[l].is_empty(id))
            continue;
        if (!e.found)
        {
            e.found = true;
            e.source = (StringLayer)l;
            e.raw = layers[l].raw(id);
        }
        else
        {
            e.has_beneath = true;
            e.beneath_source = (StringLayer)l;
            e.beneath_raw = layers[l].raw(id);
            break;
        }
    }
    return e;
}

size_t StringsStack::id_count() const
{
    size_t n = 0;
    for (int l = 0; l < StringLayer_Count; l++)
        if (present[l])
            n = std::max(n, layers[l].size());
    return n;
}
