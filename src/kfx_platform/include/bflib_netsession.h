/******************************************************************************/
// Bullfrog Engine Emulation Library - for use to remake classic games like
// Syndicate Wars, Magic Carpet or Dungeon Keeper.
/******************************************************************************/
/** @file bflib_netsession.h
 *     Header file for bflib_netsession.c.
 * @par Purpose:
 *     Algorithms and data structures for network sessions.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   KeeperFX Team
 * @date     09 Oct 2010 - 12 Oct 2014
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef BFLIB_NETSESSION_H
#define BFLIB_NETSESSION_H


#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#define NETSP_PLAYERS_COUNT 32
#define SESSION_ENTRIES_COUNT 32
#define SESSION_NAME_MAX_LEN     128
#define SESSION_LOBBY_ID_MAX_LEN  64
#define NETSP_PLAYER_NAME_MAX_LEN  32

enum NetMsgType
{
    NETMSGTYPE_MULTIPLAYER      = 0,
    NETMSGTYPE_ADD              = 1,
    NETMSGTYPE_DELETE           = 2,
    NETMSGTYPE_PROBABLYHOST     = 3, //might be incorrect
    NETMSGTYPE_SYSUSER          = 4,
    NETMSGTYPE_MPREQEXDATA      = 5,
    NETMSGTYPE_MPREQCOMPEXDATA  = 6,
    NETMSGTYPE_UNIDIRECTIONAL   = 7,
    NETMSGTYPE_UNKNOWN          = 8
};

struct TbNetworkSessionNameEntry {
    unsigned char joinable;
    uint64_t id;
    uint64_t in_use;
    char text[SESSION_NAME_MAX_LEN];
    char join_address[SESSION_LOBBY_ID_MAX_LEN];
    char lobby_id[SESSION_LOBBY_ID_MAX_LEN];
};

struct TbNetworkPlayerEntry {
  unsigned char reserved_flags;
  uint64_t id;
  uint64_t reserved_data;
  uint64_t is_active;
  char name[32];
};

struct TbNetworkCallbackData {
  char svc_name[12];
  char plyr_name[20];
  char session_data[32];
};

struct ReceiveCallbacks {
  void (*addMsg)(uint64_t, char *, void *);
  void (*deleteMsg)(uint64_t, void *);
  void (*hostMsg)(uint64_t, void *);
  void (*sysMsg)(void *);
  void *(*multiPlayer)(uint64_t, uint64_t, uint64_t, void *);
  void (*mpReqExDataMsg)(uint64_t, uint64_t, void *);
  void (*mpReqCompsExDataMsg)(uint64_t, uint64_t, void *);
  void *(*unidirectionalMsg)(uint64_t, uint64_t, void *);
  void (*systemUserMsg)(uint64_t, void *, uint64_t, void *);
  void *(*unhandledMessageTypeCallback)(uint64_t, void *);
};
/******************************************************************************/
void net_copy_name_string(char *dst,const char *src,int64_t max_len);
/******************************************************************************/
#ifdef __cplusplus
};
#endif

#endif //BFLIB_NETSESSION_H

