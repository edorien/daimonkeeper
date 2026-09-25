#ifndef STEAM_API_H
#define STEAM_API_H

#ifdef __cplusplus
extern "C"
{
#endif

    int64_t steam_api_init();
    void steam_api_shutdown();

    extern unsigned char is_running_under_wine;

#ifdef __cplusplus
}
#endif

#endif