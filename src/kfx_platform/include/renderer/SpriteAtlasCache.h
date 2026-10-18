#ifndef RENDERER_SPRITEATLASCACHE_H
#define RENDERER_SPRITEATLASCACHE_H

#include <algorithm>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

// The rectangle bookkeeping of a sprite atlas (docs/plans/sprite-atlas-thrash.md, Phase 3): an array texture's layers,
// each cut into square pages, rectangles shelf-packed inside a page; a rectangle bigger than a page takes a layer of
// its own. When it is full it first evicts a page unused for SetStaleAge() frames, else grows (more layers, up to a
// cap), and past the cap it evicts the page (or a big rectangle's layer) least recently used -- every rectangle in it
// -- rather than clearing everything; so the atlas grows with what recent frames draw, not with all ever drawn. A page used by
// the current frame is never evicted, so what a frame placed stays put until it is drawn.
// No GPU here: GpuSpriteRenderer owns the textures (grow() makes a bigger one) and the uploads.
template <typename Key, typename KeyHash = std::hash<Key>>
class SpriteAtlasCache {
public:
    struct Entry {
        int32_t x = 0, y = 0, layer = 0; // the rectangle's top-left texel
        int32_t w = 0, h = 0;
        uint64_t hash = 0;               // the owner's (a content check)
        uint64_t stamp = 0;              // the owner's (the frame it was last checked)
        uint32_t page = 0;
    };
    // Makes the atlas `layers` layers; false if it can't (it then stays as it is).
    using GrowFn = std::function<bool(int32_t layers)>;

    // An empty atlas of `layers` layers of layer_size squared, pages of page_size (a divisor of layer_size).
    void Reset(int32_t layer_size, int32_t page_size, int32_t layers)
    {
        m_layer_size = layer_size;
        m_page_size = page_size;
        m_pages_per_row = (page_size > 0) ? layer_size / page_size : 0;
        m_entries.clear();
        m_pages.clear();
        m_free.clear();
        m_current = -1;
        m_layers = 0;
        add_layers(layers);
    }
    // Drops every rectangle, keeping the layers.
    void Clear()
    {
        const int32_t layers = m_layers;
        Reset(m_layer_size, m_page_size, layers);
    }
    // The most layers growing may reach (at least 1). Lowering it below Layers() doesn't shrink the atlas.
    void SetCap(int32_t layers) { m_cap = (layers < 1) ? 1 : layers; }
    int32_t Cap() const { return m_cap; }
    // Frames a page may go unused before a full atlas evicts it rather than growing (0: grow first, up to the cap).
    void SetStaleAge(uint64_t frames) { m_stale_age = frames; }
    int32_t Layers() const { return m_layers; }
    int32_t LayerSize() const { return m_layer_size; }
    int32_t PageSize() const { return m_page_size; }
    size_t EntryCount() const { return m_entries.size(); }
    uint64_t Evictions() const { return m_evictions; }

    // The rectangle of `key`, marking its page used by `frame`; nullptr if it has none.
    Entry* Find(const Key& key, uint64_t frame)
    {
        auto it = m_entries.find(key);
        if (it == m_entries.end())
            return nullptr;
        m_pages[it->second.page].last_used = frame;
        return &it->second;
    }

    // A w x h rectangle for `key` (which has none), used by `frame`: in the page being filled, else a free page, else
    // a new one (growing through grow()), else the least recently used page not used by `frame` emptied for it; one
    // bigger than a page takes a whole layer the same way. nullptr when it is bigger than a layer, or there is no room
    // this frame. Entries returned this frame stay valid through later calls in the same frame.
    Entry* Insert(const Key& key, int32_t w, int32_t h, uint64_t frame, const GrowFn& grow)
    {
        if (w <= 0 || h <= 0 || w > m_layer_size || h > m_layer_size)
            return nullptr;
        if (w > m_page_size || h > m_page_size)
            return insert_big(key, w, h, frame, grow);
        for (;;)
        {
            int32_t px, py;
            if (m_current >= 0 && place(m_pages[m_current], w, h, px, py))
            {
                Page& page = m_pages[m_current];
                page.last_used = frame;
                page.keys.push_back(key);
                Entry e;
                e.layer = m_current / pages_per_layer();
                const int32_t in_layer = m_current % pages_per_layer();
                e.x = (in_layer % m_pages_per_row) * m_page_size + px;
                e.y = (in_layer / m_pages_per_row) * m_page_size + py;
                e.w = w;
                e.h = h;
                e.page = static_cast<uint32_t>(m_current);
                return &(m_entries[key] = e);
            }
            const int32_t next = take_page(frame, grow);
            if (next < 0)
                return nullptr;
            m_current = next;
        }
    }

private:
    struct Page {
        int32_t cx = 0, cy = 0, shelf = 0; // shelf packing: the cursor and the current shelf's height
        uint64_t last_used = 0;
        int32_t owner = -1;                // a big rectangle's layer: its first page (which holds the key and the use)
        std::vector<Key> keys;
    };

    int32_t pages_per_layer() const { return m_pages_per_row * m_pages_per_row; }
    uint64_t last_used(int32_t page) const
    {
        const Page& p = m_pages[page];
        return (p.owner >= 0) ? m_pages[p.owner].last_used : p.last_used;
    }

    void add_layers(int32_t layers)
    {
        const int32_t first = static_cast<int32_t>(m_pages.size());
        m_pages.resize(static_cast<size_t>(layers) * pages_per_layer());
        // Popped from the back: the lowest page first.
        for (int32_t i = static_cast<int32_t>(m_pages.size()) - 1; i >= first; --i)
            m_free.push_back(i);
        m_layers = layers;
    }

    bool place(Page& p, int32_t w, int32_t h, int32_t& x, int32_t& y)
    {
        if (p.cx + w > m_page_size)
        {
            p.cy += p.shelf;
            p.cx = 0;
            p.shelf = 0;
        }
        if (p.cy + h > m_page_size)
            return false;
        x = p.cx;
        y = p.cy;
        p.cx += w;
        if (h > p.shelf)
            p.shelf = h;
        return true;
    }

    // More layers (doubling, up to the cap) when every page is taken; a refused grow caps the atlas where it is.
    void try_grow(const GrowFn& grow)
    {
        if (m_layers >= m_cap)
            return;
        int32_t layers = m_layers * 2;
        if (layers > m_cap)
            layers = m_cap;
        if (grow && grow(layers))
            add_layers(layers);
        else
            m_cap = m_layers;
    }

    // Empties a page (a big rectangle's: its whole layer), its pages back on the free list.
    void evict(int32_t page)
    {
        const int32_t owner = m_pages[page].owner;
        if (owner >= 0)
        {
            for (const Key& k : m_pages[owner].keys)
                m_entries.erase(k);
            for (int32_t i = owner; i < owner + pages_per_layer(); ++i)
            {
                m_pages[i] = Page();
                m_free.push_back(i);
            }
        }
        else
        {
            for (const Key& k : m_pages[page].keys)
                m_entries.erase(k);
            m_pages[page] = Page();
            m_free.push_back(page);
            if (m_current == page)
                m_current = -1;
        }
        ++m_evictions;
    }

    // The least recently used page not used by `frame`, or -1.
    int32_t lru_page(uint64_t frame) const
    {
        int32_t lru = -1;
        for (int32_t i = 0; i < static_cast<int32_t>(m_pages.size()); ++i)
            if (last_used(i) < frame && (lru < 0 || last_used(i) < last_used(lru)))
                lru = i;
        return lru;
    }

    bool stale(int32_t page, uint64_t frame) const
    {
        return m_stale_age > 0 && page >= 0 && last_used(page) + m_stale_age < frame;
    }

    int32_t take_page(uint64_t frame, const GrowFn& grow)
    {
        if (m_free.empty())
        {
            const int32_t lru = lru_page(frame);
            if (stale(lru, frame))
                evict(lru);
            else
                try_grow(grow);
        }
        if (m_free.empty())
        {
            const int32_t lru = lru_page(frame);
            if (lru < 0)
                return -1;
            evict(lru);
        }
        const int32_t page = m_free.back();
        m_free.pop_back();
        return page;
    }

    // A layer whose pages are all free, taken off the free list; -1 if none.
    int32_t take_free_layer()
    {
        const int32_t n = pages_per_layer();
        std::vector<int32_t> free_count(static_cast<size_t>(m_layers), 0);
        for (int32_t p : m_free)
            free_count[static_cast<size_t>(p / n)]++;
        for (int32_t l = 0; l < m_layers; ++l)
            if (free_count[static_cast<size_t>(l)] == n)
            {
                m_free.erase(std::remove_if(m_free.begin(), m_free.end(), [&](int32_t p) { return p / n == l; }), m_free.end());
                return l;
            }
        return -1;
    }

    Entry* insert_big(const Key& key, int32_t w, int32_t h, uint64_t frame, const GrowFn& grow)
    {
        const int32_t n = pages_per_layer();
        // The layer least recently used as a whole (its most recent page), none of it used by this frame.
        auto lru_layer = [&](uint64_t& lru_used) {
            int32_t lru = -1;
            for (int32_t l = 0; l < m_layers; ++l)
            {
                uint64_t used = 0;
                for (int32_t i = l * n; i < (l + 1) * n; ++i)
                    used = std::max(used, last_used(i));
                if (used < frame && (lru < 0 || used < lru_used))
                {
                    lru = l;
                    lru_used = used;
                }
            }
            return lru;
        };
        auto empty_layer = [&](int32_t l) {
            for (int32_t i = l * n; i < (l + 1) * n; ++i)
                if (!m_pages[i].keys.empty() || m_pages[i].owner == i)
                    evict(i);
        };
        int32_t layer = take_free_layer();
        if (layer < 0)
        {
            uint64_t used = 0;
            const int32_t lru = lru_layer(used);
            if (lru >= 0 && m_stale_age > 0 && used + m_stale_age < frame)
                empty_layer(lru);
            else
                try_grow(grow);
            layer = take_free_layer();
        }
        if (layer < 0)
        {
            uint64_t used = 0;
            const int32_t lru = lru_layer(used);
            if (lru < 0)
                return nullptr;
            empty_layer(lru);
            layer = take_free_layer();
            if (layer < 0)
                return nullptr;
        }
        const int32_t first = layer * n;
        for (int32_t i = first; i < first + n; ++i)
        {
            m_pages[i] = Page();
            m_pages[i].owner = first;
            if (m_current == i)
                m_current = -1;
        }
        m_pages[first].last_used = frame;
        m_pages[first].keys.push_back(key);
        Entry e;
        e.layer = layer;
        e.w = w;
        e.h = h;
        e.page = static_cast<uint32_t>(first);
        return &(m_entries[key] = e);
    }

    int32_t m_layer_size = 0, m_page_size = 0, m_pages_per_row = 0;
    int32_t m_layers = 0;
    int32_t m_cap = 1;
    uint64_t m_stale_age = 0;
    int32_t m_current = -1;
    uint64_t m_evictions = 0;
    std::vector<Page> m_pages;
    std::vector<int32_t> m_free;
    std::unordered_map<Key, Entry, KeyHash> m_entries;
};

#endif // RENDERER_SPRITEATLASCACHE_H
