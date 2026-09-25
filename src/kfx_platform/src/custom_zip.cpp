/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file custom_zip.cpp
 *     Shared helpers for reading named entries out of a level's mapNNNNN.zip
 *     bundle (custom sprites/icons/lenses, and custom sounds/speech).
 * @par Purpose:
 *     Factored out of custom_sprites.c so non-sprite consumers (sound_manager,
 *     gui_soundmsgs) don't need to depend on the sprite/icon/lens loading code
 *     just to read a single zip entry.
 * @par Comment:
 *     None.
 * @author   KeeperFX Team
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "custom_zip.h"
#include "bflib_basics.h"
#include "bflib_fileio.h"
#include <json.h>
#include <json-dom.h>
#include "post_inc.h"

static char *noop_prepare_map_zip_path(LevelNumber lvnum, const char *fname) { return NULL; }
static const struct MapZipCallbacks default_map_zip_callbacks = {
    &noop_prepare_map_zip_path,
};
const struct MapZipCallbacks *map_zip_callbacks = &default_map_zip_callbacks;

void set_map_zip_callbacks(const struct MapZipCallbacks *callbacks)
{
    map_zip_callbacks = callbacks ? callbacks : &default_map_zip_callbacks;
}

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/*
 * Speedup zip stuff
 * We postulate only one zip file loaded at once
 */
static VALUE zip_cache_v;
static VALUE *zip_cache = &zip_cache_v;

int64_t fastUnzLocateFile(unzFile zip, const char *szFileName, int64_t iCaseSensitivity)
{
    //return unzLocateFile(file, szFileName, iCaseSensitivity);
    char seek_for[PATH_MAX];
    strncpy(seek_for, szFileName, PATH_MAX - 1);
    seek_for[PATH_MAX - 1] = '\0';
    make_lowercase(seek_for);
    VALUE *rec = value_dict_get(zip_cache, seek_for);
    if (rec == NULL)
        return UNZ_END_OF_LIST_OF_FILE;
    unz64_file_pos file_pos = {
            .pos_in_zip_directory = static_cast<ZPOS64_T>(value_int64(value_array_get(rec, 0))),
            .num_of_file = static_cast<ZPOS64_T>(value_int64(value_array_get(rec, 1)))
    };
    return unzGoToFilePos64(zip, &file_pos);
}

/*
 * Construct a cache for files.
 * Also if there is no indexFile just return instead
 * */
int64_t fastUnzConstructCache(unzFile zip)
{
    char szCurrentFileName[PATH_MAX];
    if (value_type(zip_cache) != VALUE_NULL)
    {
        ERRORLOG("Zip cache is not clear!");
    }
    value_init_dict(zip_cache);

    for (int64_t err = unzGoToFirstFile(zip);
         err == UNZ_OK;
         err = unzGoToNextFile(zip))
    {
        if (UNZ_OK != unzGetCurrentFileInfo64(zip, NULL,
                                              szCurrentFileName, sizeof(szCurrentFileName) - 1,
                                              NULL, 0, NULL, 0)
                )
        {
            continue;
        }
        make_lowercase(szCurrentFileName);

        unz64_file_pos file_pos;
        unzGetFilePos64(zip, &file_pos);

        VALUE *rec = value_dict_add(zip_cache, szCurrentFileName);
        value_init_array(rec);
        value_init_int64(value_array_append(rec), file_pos.pos_in_zip_directory);
        value_init_int64(value_array_append(rec), file_pos.num_of_file);
    }
    return UNZ_OK;
}

int64_t fastUnzClearCache()
{
    value_fini(zip_cache);
    return 0;
}

/* end of zip stuff */

namespace {

// RAII wrapper around an open minizip archive handle.
class UnzFileGuard {
public:
    explicit UnzFileGuard(const char *path) : zip_(unzOpen(path)) {}
    ~UnzFileGuard() { if (zip_ != NULL) unzClose(zip_); }
    UnzFileGuard(const UnzFileGuard &) = delete;
    UnzFileGuard &operator=(const UnzFileGuard &) = delete;
    bool is_open() const { return zip_ != NULL; }
    unzFile get() const { return zip_; }
private:
    unzFile zip_;
};

// Pairs fastUnzConstructCache()/fastUnzClearCache(): the cache must be
// cleared on every exit path once built, or the next caller to build one
// trips the "Zip cache is not clear!" guard in fastUnzConstructCache().
class ZipCacheGuard {
public:
    explicit ZipCacheGuard(unzFile zip) : built_(fastUnzConstructCache(zip) == UNZ_OK) {}
    ~ZipCacheGuard() { if (built_) fastUnzClearCache(); }
    ZipCacheGuard(const ZipCacheGuard &) = delete;
    ZipCacheGuard &operator=(const ZipCacheGuard &) = delete;
    bool built() const { return built_; }
private:
    bool built_;
};

// RAII wrapper around a malloc'd buffer, freed unless released. Stays
// malloc/free (not new[]/unique_ptr) because read_map_zip_entry hands the
// buffer to callers that free() it themselves.
class MallocBuffer {
public:
    explicit MallocBuffer(size_t size) : ptr_(static_cast<unsigned char *>(malloc(size))) {}
    ~MallocBuffer() { free(ptr_); }
    MallocBuffer(const MallocBuffer &) = delete;
    MallocBuffer &operator=(const MallocBuffer &) = delete;
    unsigned char *get() const { return ptr_; }
    unsigned char *release() { unsigned char *p = ptr_; ptr_ = NULL; return p; }
private:
    unsigned char *ptr_;
};

} // namespace

TbBool read_map_zip_entry(LevelNumber lvnum, const char *entry_name, unsigned char **out_data, size_t *out_size)
{
    if ((out_data == NULL) || (out_size == NULL) || (entry_name == NULL))
    {
        return false;
    }
    *out_data = NULL;
    *out_size = 0;

    char zipname[32];
    snprintf(zipname, sizeof(zipname), "map%05" PRId64 ".zip", (int64_t)(lvnum));
    char *fname = map_zip_callbacks->prepare_map_zip_path(lvnum, zipname);
    if ((fname == NULL) || !LbFileExists(fname))
    {
        return false;
    }

    UnzFileGuard zip(fname);
    if (!zip.is_open())
    {
        return false;
    }

    ZipCacheGuard cache(zip.get());
    if (!cache.built())
    {
        return false;
    }

    if (UNZ_OK != fastUnzLocateFile(zip.get(), entry_name, 0))
    {
        return false;
    }

    unz_file_info64 zip_info{};
    if (UNZ_OK != unzGetCurrentFileInfo64(zip.get(), &zip_info, NULL, 0, NULL, 0, NULL, 0))
    {
        return false;
    }
    if (zip_info.uncompressed_size > 32 * 1024 * 1024)
    {
        WARNLOG("Zip entry too large: '%s' in '%s'", entry_name, fname);
        return false;
    }

    MallocBuffer data((size_t)zip_info.uncompressed_size);
    if (data.get() == NULL)
    {
        return false;
    }
    if (UNZ_OK != unzOpenCurrentFile(zip.get()))
    {
        return false;
    }
    int64_t bytes_read = unzReadCurrentFile(zip.get(), data.get(), zip_info.uncompressed_size);
    unzCloseCurrentFile(zip.get());
    if (bytes_read != (int64_t)zip_info.uncompressed_size)
    {
        WARNLOG("Failed to read '%s' from '%s'", entry_name, fname);
        return false;
    }

    *out_size = (size_t)zip_info.uncompressed_size;
    *out_data = data.release();
    return true;
}
