#include "harness_common.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "pre_inc.h"
#include "config_keeperfx.h"
#include "config_rules.h"
#include "bflib_basics.h"
#include "bflib_sprfnt.h"
#include "frontend.h"
#include "kfx_sim_state.h"
#include "map_events.h"
#include "ports/ui_port.h"
#include "file_path_port_impl.h"
#include "sound_host_port_impl.h"
#include "input_focus_port_impl.h"
#include "display_host_port_impl.h"
#include "sim_port_impl.h"
#include "pathfinding_world_port_impl.h"
#include "ai_port_impl.h"
#include "render_port_impl.h"
#include "net_port_impl.h"
#include "game_port_impl.h"
#include "audio_port_impl.h"
#include "ui_port_impl.h"
#include "script_port_impl.h"
#include "session_loop_port_impl.h"
#include "editor_port_impl.h"
#include "post_inc.h"

static TbBool never_multiplayer(void) { return false; }

/** The one answer the user-interface port has to give for the simulation to go on: how an event kind behaves (the
 *  table is the simulation's own). Every other call of the port stays its default, a no-op. */
static const struct EventTypeInfo *fuzz_event_button_info(EventKind evkind)
{
    return (evkind < EVENT_KIND_COUNT) ? &event_button_info[evkind] : &event_button_info[0];
}
static struct UiPort fuzz_ui_port;

/** What main.cpp's wire_ports() does: the harness reaches the real implementations, never the unwired defaults. */
void fuzz_wire_ports(void)
{
    static bool done = false;
    if (done)
        return;
    done = true;
    set_file_path_port(&kfx_config_file_path_port);
    set_sound_host_port(&kfx_game_sound_host_port);
    // (the ports into the user interface, the audio feedback, the display, the session loop and the editor stay at their
    // unwired defaults: no-ops. Their real implementations draw, play and open windows, which a harness has no state for)
    set_sim_port(&kfx_sim_port);
    set_pathfinding_world_port(&kfx_sim_pathfinding_world_port);
    set_ai_port(&kfx_ai_port);
    set_render_port(&kfx_render_port);
    set_net_port(&kfx_net_port);
    set_game_port(&kfx_game_port);
    set_script_port(&kfx_script_port);
    fuzz_ui_port = *ui_port;
    fuzz_ui_port.get_event_button_info = fuzz_event_button_info;
    set_ui_port(&fuzz_ui_port);
    set_emulate_integer_overflow_provider(&emulate_integer_overflow);
    set_gameturn_source(&kfx_sim_state.play_gameturn);
    set_rid_random_seed_source(&kfx_sim_state.action_random_seed);
    set_config_level_sources(&kfx_sim_state.selected_level_number, &kfx_sim_state.loaded_level_number);
    bf_sprfnt_set_font_role_resolver(resolve_font_role);
    set_config_network_is_active_check(never_multiplayer);
}

#ifndef KFX_FUZZ_REPO_ROOT
#define KFX_FUZZ_REPO_ROOT "."
#endif

static char runtime[512];

static void link_in(const char *dir, const char *name)
{
    const char *data = getenv("KFX_FUZZ_DATA");
    char from[1024], to[1024];
    snprintf(from, sizeof(from), "%s/%s", data ? data : KFX_FUZZ_REPO_ROOT "/config", name);
    snprintf(to, sizeof(to), "%s/%s", dir, name);
    struct stat st;
    if (stat(from, &st) != 0 || lstat(to, &st) == 0)
        return;
    if (symlink(from, to) != 0 && errno != EEXIST)
        perror("symlink");
}

const char *fuzz_runtime_dir(void)
{
    if (runtime[0] != '\0')
        return runtime;
    // no real sound or video device: a config can name a sound, which starts the audio system
    setenv("SDL_AUDIODRIVER", "dummy", 1);
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    fuzz_wire_ports();
    snprintf(runtime, sizeof(runtime), "/dev/shm/kfx_fuzz_rt");
    mkdir(runtime, 0755);
    static const char *const names[] = { "fxdata", "creatrs", "data", "ldata", "sound", "mods", "multiplayer", NULL };
    char own[600];
    snprintf(own, sizeof(own), "%s/campgns", runtime);
    mkdir(own, 0755);
    snprintf(own, sizeof(own), "%s/levels", runtime);
    mkdir(own, 0755);
    for (int i = 0; names[i] != NULL; i++)
        link_in(runtime, names[i]);
    snprintf(keeper_runtime_directory, sizeof(keeper_runtime_directory), "%s", runtime);
    return runtime;
}

const char *fuzz_scratch_dir(char *rel, size_t rel_size, const char *name)
{
    snprintf(rel, rel_size, "%s-%d", name, (int)getpid());
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", fuzz_runtime_dir(), rel);
    mkdir(path, 0755);
    return rel;
}

int fuzz_write_file(const char *rel, const char *file, const uint8_t *data, size_t size)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s/%s", fuzz_runtime_dir(), rel, file);
    FILE *f = fopen(path, "wb");
    if (f == NULL)
        return -1;
    if (size > 0)
        fwrite(data, 1, size, f);
    fclose(f);
    return 0;
}

void fuzz_scratch_clean(const char *rel)
{
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", fuzz_runtime_dir(), rel);
    DIR *d = opendir(path);
    if (d != NULL)
    {
        struct dirent *e;
        while ((e = readdir(d)) != NULL)
        {
            if (e->d_name[0] == '.')
                continue;
            char f[1400];
            snprintf(f, sizeof(f), "%s/%s", path, e->d_name);
            unlink(f);
        }
        closedir(d);
    }
    rmdir(path);
}
