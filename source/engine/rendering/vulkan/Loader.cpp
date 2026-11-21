#include "engine/rendering/vulkan/vk.h"

#define NFS_VK_DEFINE_FUNCTION(name) PFN_##name name;
NFS_VK_GLOBAL_FUNCTIONS(NFS_VK_DEFINE_FUNCTION)
NFS_VK_INSTANCE_FUNCTIONS(NFS_VK_DEFINE_FUNCTION)
NFS_VK_DEVICE_FUNCTIONS(NFS_VK_DEFINE_FUNCTION)
#undef NFS_VK_DEFINE_FUNCTION

namespace nfs::vk
{
namespace
{
PFN_vkGetInstanceProcAddr g_getInstanceProcAddr;
}

bool LoadLoader()
{
    if (g_getInstanceProcAddr)
        return true;
    HMODULE library = LoadLibraryA("vulkan-1.dll");
    if (!library)
        return false;
    g_getInstanceProcAddr =
        reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(library, "vkGetInstanceProcAddr"));
    if (!g_getInstanceProcAddr)
        return false;
#define NFS_VK_LOAD_GLOBAL(name) name = reinterpret_cast<PFN_##name>(g_getInstanceProcAddr(nullptr, #name));
    NFS_VK_GLOBAL_FUNCTIONS(NFS_VK_LOAD_GLOBAL)
#undef NFS_VK_LOAD_GLOBAL
    return vkCreateInstance != nullptr;
}

void LoadInstanceFunctions(VkInstance instance)
{
#define NFS_VK_LOAD_INSTANCE(name) name = reinterpret_cast<PFN_##name>(g_getInstanceProcAddr(instance, #name));
    NFS_VK_INSTANCE_FUNCTIONS(NFS_VK_LOAD_INSTANCE)
#undef NFS_VK_LOAD_INSTANCE
}

void LoadDeviceFunctions(VkDevice device)
{
#define NFS_VK_LOAD_DEVICE(name) name = reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(device, #name));
    NFS_VK_DEVICE_FUNCTIONS(NFS_VK_LOAD_DEVICE)
#undef NFS_VK_LOAD_DEVICE
}
} // namespace nfs::vk
