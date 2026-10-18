// kfx_platform: RendererGpu3D::device_is_cpu_rasteriser() -- a Vulkan device that runs on the CPU is refused (the
// software renderer is faster), so the name match must catch Mesa's and Google's CPU drivers and nothing else.
#include <catch2/catch_test_macros.hpp>

#include "renderer/RendererGpu3D.h"

TEST_CASE("CPU Vulkan devices are recognised by device or driver name", "[kfx_platform][RendererGpu3D]") {
    CHECK(RendererGpu3D::device_is_cpu_rasteriser("llvmpipe (LLVM 21.1.8, 256 bits)", "llvmpipe"));
    CHECK(RendererGpu3D::device_is_cpu_rasteriser("?", "llvmpipe"));
    CHECK(RendererGpu3D::device_is_cpu_rasteriser("Lavapipe", ""));
    CHECK(RendererGpu3D::device_is_cpu_rasteriser("SwiftShader Device (Subzero)", "SwiftShader driver"));
    CHECK(RendererGpu3D::device_is_cpu_rasteriser(nullptr, "softpipe"));
}

TEST_CASE("Real GPUs are not taken for CPU rasterisers", "[kfx_platform][RendererGpu3D]") {
    CHECK_FALSE(RendererGpu3D::device_is_cpu_rasteriser("AMD Radeon RX 7900 GRE (RADV NAVI31)", "radv"));
    CHECK_FALSE(RendererGpu3D::device_is_cpu_rasteriser("NVIDIA GeForce RTX 4070", "NVIDIA"));
    CHECK_FALSE(RendererGpu3D::device_is_cpu_rasteriser("Intel(R) Arc(tm) Graphics (MTL)", "Intel open-source Mesa driver"));
    CHECK_FALSE(RendererGpu3D::device_is_cpu_rasteriser("?", ""));
    CHECK_FALSE(RendererGpu3D::device_is_cpu_rasteriser(nullptr, nullptr));
}
