// Multiplayer and replay files exchange these structs as raw bytes (memcpy in packets_misc.c,
// net_exchange_*.c, net_lobby.c, packet-save files). A 32-bit Windows peer and a 64-bit Linux peer must
// agree on every byte, so none of them may contain `long`, `unsigned long` or a pointer. The sizes below
// were measured on the 32-bit build (`gcc -m32`); see docs/refactor/defects/00-lp64-layout-audit.md, which
// also lists the structs that are NOT portable yet (the whole-state blobs used by resync/savegames).
#include <catch2/catch_test_macros.hpp>

#include "packets.h"
#include "packet_data.h"
#include "net_main.h"
#include "save_catalogue.h"
#include "kfx_net_state.h"

#include <cstddef>
#include <cstdint>

TEST_CASE("struct Packet has one fixed byte layout on every ABI", "[kfx_net][lp64][wire]") {
    CHECK(sizeof(struct Packet) == 39);
    CHECK(offsetof(struct Packet, turn) == 0);
    CHECK(offsetof(struct Packet, checksum) == 8);
    CHECK(offsetof(struct Packet, action) == 13);
    CHECK(offsetof(struct Packet, actn_par1) == 14);
    CHECK(offsetof(struct Packet, actn_par2) == 18);
    CHECK(offsetof(struct Packet, pos_x) == 22);
    CHECK(offsetof(struct Packet, pos_y) == 26);
    CHECK(offsetof(struct Packet, control_flags) == 30);
    CHECK(offsetof(struct Packet, actn_par3) == 35);
    CHECK(offsetof(struct Packet, actn_par4) == 37);
    CHECK(sizeof(struct PacketEx) == 39 + 4 * CKS_MAX);
}

TEST_CASE("Replay header, save catalogue entry and lobby version keep their on-disk/wire size", "[kfx_net][lp64][wire]") {
    CHECK(sizeof(struct PacketSaveHead) == 180);
    CHECK(sizeof(struct CatalogueEntry) == 446);
    CHECK(sizeof(struct GameVersionPacket) == 16);
    CHECK(sizeof(struct ScreenPacket) == 9);
    CHECK(sizeof(struct DesyncChecksums) == 64);
    CHECK(sizeof(TbBigChecksum) == 4);
    CHECK(sizeof(GameTurn) == 8);
}
