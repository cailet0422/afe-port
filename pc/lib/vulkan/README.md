Vulkan headers 1.3.275 (Khronos Vulkan-Headers, Apache-2.0, LICENSE.txt): `vulkan_core.h`, `vk_platform.h`
and the `vk_video` headers it includes. The Vulkan backend (`pc/src/gfx/pc_gfx_vk.c`) loads every function
at run time through SDL (`VK_NO_PROTOTYPES`), so no Vulkan library is linked.
