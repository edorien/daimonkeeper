# kfx_model -- header-only type layouts: struct Thing (thing_types.h),
# struct Map (map_types.h) and struct SlabMap (slab_types.h), plus the pure
# static inline helpers over them. Refactor pass 2, S08
# (docs/refactor-pass2/stage-08-kfx-model-headers.md): ranked below
# kfx_pathfinding so Ariadne can read these fields directly instead of
# through PathfindingWorldPort. The storage (things_data[], map[],
# slabmap[]) stays in kfx_sim.
#
# An INTERFACE target: no sources, no symbols. check_layering.py --strict
# rejects any non-inline function prototype or extern data in these headers,
# so kfx_model can never reference a higher library.
add_library(kfx_model INTERFACE)
target_include_directories(kfx_model INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
