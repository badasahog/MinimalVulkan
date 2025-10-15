/*
* (C) 2025 badasahog. All Rights Reserved
*
* The above copyright notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#define WIN32_LEAN_AND_MEAN
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#undef _CRT_SECURE_NO_WARNINGS

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include <cglm/cglm.h>
#include <cglm/struct.h>
#include <cglm/call.h>
#include <cglm/cam.h>
#include <cglm/clipspace/persp_rh_zo.h>

#include <stdint.h>
#include <stdbool.h>
#include <stdalign.h>

__declspec(dllexport) DWORD NvOptimusEnablement = 1;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;

HANDLE ConsoleHandle;

inline void THROW_ON_FAIL_IMPL(HRESULT hr, int line)
{
	if (FAILED(hr))
	{
		LPWSTR messageBuffer;
		DWORD formattedErrorLength = FormatMessageW(
			FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL,
			hr,
			MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
			(LPWSTR)&messageBuffer,
			0,
			NULL
		);

		if (formattedErrorLength == 0)
			WriteConsoleA(ConsoleHandle, "an error occured, unable to retrieve error message\n", 51, NULL, NULL);
		else
		{
			WriteConsoleA(ConsoleHandle, "an error occured: ", 18, NULL, NULL);
			WriteConsoleW(ConsoleHandle, messageBuffer, formattedErrorLength, NULL, NULL);
			WriteConsoleA(ConsoleHandle, "\n", 1, NULL, NULL);
			LocalFree(messageBuffer);
		}

		char buffer[50];
		int stringlength = _snprintf_s(buffer, 50, _TRUNCATE, "error code: 0x%X\nlocation:line %i\n", hr, line);
		WriteConsoleA(ConsoleHandle, buffer, stringlength, NULL, NULL);

		RaiseException(0, EXCEPTION_NONCONTINUABLE, 0, NULL);
	}
}

void THROW_ON_FAIL_VK_IMPL(VkResult Result, int line)
{
	if (Result < VK_SUCCESS)
	{
		char buffer[50];
		int stringlength = _snprintf_s(buffer, 50, _TRUNCATE, "Vulkan Error: %i\nlocation:line %i\n", Result, line);
		WriteConsoleA(ConsoleHandle, buffer, stringlength, NULL, NULL);

		RaiseException(0, EXCEPTION_NONCONTINUABLE, 0, NULL);
	}
}

#define THROW_ON_FAIL(x) THROW_ON_FAIL_IMPL(x, __LINE__)

#define THROW_ON_FAIL_VK(x) THROW_ON_FAIL_VK_IMPL(x, __LINE__)

#define THROW_ON_FALSE(x) if((x) == FALSE) THROW_ON_FAIL(HRESULT_FROM_WIN32(GetLastError()))

#define VALIDATE_HANDLE(x) if((x) == NULL || (x) == INVALID_HANDLE_VALUE) THROW_ON_FAIL(HRESULT_FROM_WIN32(GetLastError()))

void FailFastWithMessage(const char* Message)
{
	WriteConsoleA(ConsoleHandle, Message, strlen(Message), NULL, NULL);
	RaiseException(0, EXCEPTION_NONCONTINUABLE, 0, NULL);
}

LRESULT CALLBACK PreInitProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK IdleProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static const UINT TEXTURE_WIDTH = 64;
static const UINT TEXTURE_HEIGHT = 64;
static const UINT BYTES_PER_TEXEL = 2;
static const VkFormat IMAGE_FORMAT = VK_FORMAT_B5G6R5_UNORM_PACK16;

static const LPCTSTR WindowClassName = L"MinimalVulkan";

//note: these are presumptive. Not within spec (as far as I know)
#define MAX_FRAMES_IN_FLIGHT 3
#define MAX_SURFACE_FORMATS 32
#define MAX_PRESENT_MODES 8
#define SWAP_CHAIN_MAX_IMAGE_COUNT 8
#define MAX_DEVICE_COUNT 16
#define MAX_QUEUE_FAMILY_COUNT 16

#define WM_INIT (WM_USER + 1)

static const char* const VALIDATION_LAYERS[] = {
	"VK_LAYER_KHRONOS_validation"
};

static const char* const DEVICE_EXTENSIONS[] = {
	VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

#ifdef _DEBUG
static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT MessageSeverity, VkDebugUtilsMessageTypeFlagsEXT MessageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData)
{
	char buffer[512];
	int stringlength = _snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "validation layer: %s\n", pCallbackData->pMessage);
	WriteConsoleA(ConsoleHandle, buffer, stringlength, NULL, NULL);
	return VK_FALSE;
}

void DestroyDebugUtilsMessengerEXT(VkInstance VulkanInstance, VkDebugUtilsMessengerEXT DebugMessenger, const VkAllocationCallbacks* pAllocator)
{
	PFN_vkDestroyDebugUtilsMessengerEXT Func = vkGetInstanceProcAddr(VulkanInstance, "vkDestroyDebugUtilsMessengerEXT");
	if (Func)
	{
		Func(VulkanInstance, DebugMessenger, pAllocator);
	}
}
#endif

struct QueueFamilyIndices
{
	uint32_t GraphicsFamily;
	uint32_t PresentFamily;
};

struct Vertex
{
	vec3 Pos;
	vec3 Color;
	vec2 TexCoord;
};

struct UniformBufferObject
{
	alignas(16) mat4 Model;
	alignas(16) mat4 View;
	alignas(16) mat4 Proj;
};

static const struct Vertex Vertices[] = {
	{{-0.5f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
	{{0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
	{{0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
	{{-0.5f, 0.5f, 0.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}},

	{{-0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},
	{{0.5f, -0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
	{{0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
	{{-0.5f, 0.5f, -0.5f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f}}
};

static const uint16_t Indices[] = {
	0, 1, 2, 2, 3, 0,
	4, 5, 6, 6, 7, 4
};

struct VulkanObjects
{
#ifdef _DEBUG
	VkDebugUtilsMessengerEXT DebugMessenger;
#endif
	VkSurfaceKHR Surface;

	VkFormat DepthFormat;

	VkPhysicalDevice PhysicalDevice;
	VkDevice Device;

	struct QueueFamilyIndices QueueFamilyIndices;


	VkQueue PresentQueue;
	VkQueue GraphicsQueue;

	uint32_t SwapChainImageCount;

	VkSurfaceCapabilitiesKHR SurfaceCapabilities;
	VkSwapchainKHR SwapChain;
	VkImage SwapChainImages[SWAP_CHAIN_MAX_IMAGE_COUNT];
	VkSurfaceFormatKHR SwapChainImageFormat;
	VkPresentModeKHR SwapChainPresentMode;
	VkExtent2D SwapChainExtent;
	VkImageView SwapChainImageViews[SWAP_CHAIN_MAX_IMAGE_COUNT];
	VkFramebuffer SwapChainFramebuffers[SWAP_CHAIN_MAX_IMAGE_COUNT];

	VkRenderPass RenderPass;
	VkPipelineLayout PipelineLayout;
	VkPipeline GraphicsPipeline;

	VkImage DepthImage;
	VkDeviceMemory DepthImageMemory;
	VkImageView DepthImageView;

	VkBuffer VertexBuffer;
	VkDeviceMemory VertexBufferMemory;
	VkBuffer IndexBuffer;
	VkDeviceMemory IndexBufferMemory;

	void* UniformBuffersMapped[MAX_FRAMES_IN_FLIGHT];

	VkDescriptorSet DescriptorSets[MAX_FRAMES_IN_FLIGHT];

	VkCommandBuffer CommandBuffers[MAX_FRAMES_IN_FLIGHT];

	VkSemaphore ImageAvailableSemaphores[MAX_FRAMES_IN_FLIGHT];
	VkSemaphore RenderFinishedSemaphores[MAX_FRAMES_IN_FLIGHT];
	VkFence InFlightFences[MAX_FRAMES_IN_FLIGHT];
};

uint32_t ClampU32(uint32_t value, uint32_t min, uint32_t max)
{
	if (value < min)
	{
		return min;
	}
	else if (value > max)
	{
		return max;
	}
	return value;
}

VkCommandBuffer BeginSingleTimeCommands(VkDevice Device, VkCommandPool CommandPool)
{
	VkCommandBuffer CommandBuffer;

	{
		VkCommandBufferAllocateInfo AllocInfo = { 0 };
		AllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		AllocInfo.commandPool = CommandPool;
		AllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		AllocInfo.commandBufferCount = 1;
		vkAllocateCommandBuffers(Device, &AllocInfo, &CommandBuffer);
	}

	{
		VkCommandBufferBeginInfo BeginInfo = { 0 };
		BeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
		BeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		vkBeginCommandBuffer(CommandBuffer, &BeginInfo);
	}

	return CommandBuffer;
}

void EndSingleTimeCommands(VkDevice Device, VkCommandPool CommandPool, VkQueue GraphicsQueue, VkCommandBuffer CommandBuffer)
{
	vkEndCommandBuffer(CommandBuffer);

	{
		VkSubmitInfo SubmitInfo = { 0 };
		SubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
		SubmitInfo.commandBufferCount = 1;
		SubmitInfo.pCommandBuffers = &CommandBuffer;
		vkQueueSubmit(GraphicsQueue, 1, &SubmitInfo, VK_NULL_HANDLE);
	}

	vkQueueWaitIdle(GraphicsQueue);

	vkFreeCommandBuffers(Device, CommandPool, 1, &CommandBuffer);
}

uint32_t FindMemoryType(VkPhysicalDevice PhysicalDevice, uint32_t TypeFilter, VkMemoryPropertyFlags Properties)
{
	VkPhysicalDeviceMemoryProperties MemProperties;
	vkGetPhysicalDeviceMemoryProperties(PhysicalDevice, &MemProperties);

	for (int i = 0; i < MemProperties.memoryTypeCount; i++) {
		if ((TypeFilter & (1 << i)) && (MemProperties.memoryTypes[i].propertyFlags & Properties) == Properties) {
			return i;
		}
	}

	FailFastWithMessage("failed to find suitable memory type!");
}

void CreateBuffer(VkPhysicalDevice PhysicalDevice, VkDevice Device, VkDeviceSize Size, VkBufferUsageFlags Usage, VkMemoryPropertyFlags Properties, VkBuffer* Buffer, VkDeviceMemory* BufferMemory)
{
	{
		VkBufferCreateInfo BufferInfo = { 0 };
		BufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		BufferInfo.size = Size;
		BufferInfo.usage = Usage;
		BufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		THROW_ON_FAIL_VK(vkCreateBuffer(Device, &BufferInfo, NULL, Buffer));
	}

	{
		VkMemoryRequirements MemRequirements;
		vkGetBufferMemoryRequirements(Device, *Buffer, &MemRequirements);

		VkMemoryAllocateInfo AllocInfo = { 0 };
		AllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		AllocInfo.allocationSize = MemRequirements.size;
		AllocInfo.memoryTypeIndex = FindMemoryType(PhysicalDevice, MemRequirements.memoryTypeBits, Properties);
		THROW_ON_FAIL_VK(vkAllocateMemory(Device, &AllocInfo, NULL, BufferMemory));
	}

	vkBindBufferMemory(Device, *Buffer, *BufferMemory, 0);
}

int main()
{
	ConsoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);

	HINSTANCE Instance = GetModuleHandleW(NULL);

	HICON Icon = LoadIconW(NULL, IDI_APPLICATION);
	HCURSOR Cursor = LoadCursorW(NULL, IDC_ARROW);

	WNDCLASSEXW WindowClass = { 0 };
	WindowClass.cbSize = sizeof(WNDCLASSEXW);
	WindowClass.style = CS_HREDRAW | CS_VREDRAW;
	WindowClass.lpfnWndProc = PreInitProc;
	WindowClass.cbClsExtra = 0;
	WindowClass.cbWndExtra = 0;
	WindowClass.hInstance = Instance;
	WindowClass.hIcon = Icon;
	WindowClass.hCursor = Cursor;
	WindowClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 2);
	WindowClass.lpszMenuName = NULL;
	WindowClass.lpszClassName = WindowClassName;
	WindowClass.hIconSm = Icon;

	ATOM WindowClassAtom = RegisterClassExW(&WindowClass);
	if (WindowClassAtom == 0)
		THROW_ON_FAIL(HRESULT_FROM_WIN32(GetLastError()));

	RECT WindowRect = { 0 };
	WindowRect.left = 0;
	WindowRect.top = 0;
	WindowRect.right = 800;
	WindowRect.bottom = 600;

	THROW_ON_FALSE(AdjustWindowRect(&WindowRect, WS_OVERLAPPEDWINDOW, FALSE));

	HWND Window = CreateWindowExW(
		0,
		WindowClassName,
		L"Minimal Vulkan",
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		WindowRect.right - WindowRect.left,
		WindowRect.bottom - WindowRect.top,
		NULL,
		NULL,
		Instance,
		NULL);

	VALIDATE_HANDLE(Window);

	THROW_ON_FALSE(ShowWindow(Window, SW_SHOW));

	VkInstance VulkanInstance;

	{
		VkApplicationInfo AppInfo = { 0 };
		AppInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
		AppInfo.pApplicationName = "Hello Triangle";
		AppInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
		AppInfo.pEngineName = "No Engine";
		AppInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
		AppInfo.apiVersion = VK_API_VERSION_1_0;

		static const char* const Extensions[] =
		{
			"VK_KHR_surface",
			"VK_KHR_win32_surface",
#ifdef _DEBUG
			VK_EXT_DEBUG_UTILS_EXTENSION_NAME
#endif
		};

#ifdef _DEBUG
		VkDebugUtilsMessengerCreateInfoEXT DebugCreateInfo = { 0 };
		DebugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		DebugCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		DebugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		DebugCreateInfo.pfnUserCallback = DebugCallback;
#endif

		VkInstanceCreateInfo CreateInfo = { 0 };
		CreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
		CreateInfo.pApplicationInfo = &AppInfo;
		CreateInfo.enabledExtensionCount = ARRAYSIZE(Extensions);
		CreateInfo.ppEnabledExtensionNames = Extensions;

#ifdef _DEBUG
		CreateInfo.pNext = &DebugCreateInfo;
		CreateInfo.enabledLayerCount = ARRAYSIZE(VALIDATION_LAYERS);
		CreateInfo.ppEnabledLayerNames = VALIDATION_LAYERS;
#else
		CreateInfo.pNext = NULL;
		CreateInfo.enabledLayerCount = 0;
#endif

		THROW_ON_FAIL_VK(vkCreateInstance(&CreateInfo, NULL, &VulkanInstance));
	}

	struct VulkanObjects VulkanObjects = { 0 };

#ifdef _DEBUG
	PFN_vkCreateDebugUtilsMessengerEXT CallbackFunction = vkGetInstanceProcAddr(VulkanInstance, "vkCreateDebugUtilsMessengerEXT");
	if (CallbackFunction)
	{
		VkDebugUtilsMessengerCreateInfoEXT CreateInfo = { 0 };
		CreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
		CreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
		CreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
		CreateInfo.pfnUserCallback = DebugCallback;
		THROW_ON_FAIL_VK(CallbackFunction(VulkanInstance, &CreateInfo, NULL, &VulkanObjects.DebugMessenger));
	}
	else
	{
		THROW_ON_FAIL_VK(VK_ERROR_EXTENSION_NOT_PRESENT);
	}
#endif

	{
		VkWin32SurfaceCreateInfoKHR CreateInfo = { 0 };
		CreateInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
		CreateInfo.hinstance = Instance;
		CreateInfo.hwnd = Window;
		THROW_ON_FAIL_VK(vkCreateWin32SurfaceKHR(VulkanInstance, &CreateInfo, NULL, &VulkanObjects.Surface));
	}

	{
		uint32_t DeviceCount = 0;
		vkEnumeratePhysicalDevices(VulkanInstance, &DeviceCount, NULL);
		VkPhysicalDevice Devices[MAX_DEVICE_COUNT];

		vkEnumeratePhysicalDevices(VulkanInstance, &DeviceCount, Devices);

		// are devices organized in any kind of order? just pick the first one
		VulkanObjects.PhysicalDevice = Devices[0];
	}

	{
		uint32_t QueueFamilyCount = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(VulkanObjects.PhysicalDevice, &QueueFamilyCount, NULL);

		VkQueueFamilyProperties QueueFamilies[MAX_QUEUE_FAMILY_COUNT];
		vkGetPhysicalDeviceQueueFamilyProperties(VulkanObjects.PhysicalDevice, &QueueFamilyCount, QueueFamilies);

		bool HasGraphics = false;
		bool HasPresent = false;

		for (int i = 0; i < QueueFamilyCount; i++)
		{
			if (QueueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
			{
				VulkanObjects.QueueFamilyIndices.GraphicsFamily = i;
				HasGraphics = true;
			}

			VkBool32 PresentSupport = false;
			vkGetPhysicalDeviceSurfaceSupportKHR(VulkanObjects.PhysicalDevice, i, VulkanObjects.Surface, &PresentSupport);

			if (PresentSupport)
			{
				VulkanObjects.QueueFamilyIndices.PresentFamily = i;
				HasPresent = true;
			}

			if (HasGraphics && HasPresent)
			{
				break;
			}
		}
	}

	{
		float QueuePriority = 1.0f;

		uint32_t UniqueQueueFamilies[2] = { 0 };

		UniqueQueueFamilies[0] = VulkanObjects.QueueFamilyIndices.GraphicsFamily;

		if (VulkanObjects.QueueFamilyIndices.PresentFamily != VulkanObjects.QueueFamilyIndices.GraphicsFamily)
		{
			UniqueQueueFamilies[1] = VulkanObjects.QueueFamilyIndices.PresentFamily;
		}

		const int UniqueQueueFamilyCount = (VulkanObjects.QueueFamilyIndices.PresentFamily == VulkanObjects.QueueFamilyIndices.GraphicsFamily) ? 1 : 2;

		VkDeviceQueueCreateInfo QueueCreateInfos[2] = { 0 };
	
		for (int i = 0; i < UniqueQueueFamilyCount; i++)
		{
			QueueCreateInfos[i].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
			QueueCreateInfos[i].queueFamilyIndex = UniqueQueueFamilies[i];
			QueueCreateInfos[i].queueCount = 1;
			QueueCreateInfos[i].pQueuePriorities = &QueuePriority;
		}

		VkPhysicalDeviceFeatures DeviceFeatures = { 0 };
		DeviceFeatures.samplerAnisotropy = VK_TRUE;

		VkDeviceCreateInfo DeviceCreationInfo = { 0 };
		DeviceCreationInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
		DeviceCreationInfo.queueCreateInfoCount = UniqueQueueFamilyCount;
		DeviceCreationInfo.pQueueCreateInfos = QueueCreateInfos;


#ifdef _DEBUG
		DeviceCreationInfo.enabledLayerCount = ARRAYSIZE(VALIDATION_LAYERS);
		DeviceCreationInfo.ppEnabledLayerNames = VALIDATION_LAYERS;
#else
		DeviceCreationInfo.enabledLayerCount = 0;
		DeviceCreationInfo.ppEnabledLayerNames = NULL;
#endif

		DeviceCreationInfo.enabledExtensionCount = ARRAYSIZE(DEVICE_EXTENSIONS);
		DeviceCreationInfo.ppEnabledExtensionNames = DEVICE_EXTENSIONS;
		DeviceCreationInfo.pEnabledFeatures = &DeviceFeatures;

		THROW_ON_FAIL_VK(vkCreateDevice(VulkanObjects.PhysicalDevice, &DeviceCreationInfo, NULL, &VulkanObjects.Device));

		vkGetDeviceQueue(VulkanObjects.Device, VulkanObjects.QueueFamilyIndices.GraphicsFamily, 0, &VulkanObjects.GraphicsQueue);
		vkGetDeviceQueue(VulkanObjects.Device, VulkanObjects.QueueFamilyIndices.PresentFamily, 0, &VulkanObjects.PresentQueue);
	}

	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(VulkanObjects.PhysicalDevice, VulkanObjects.Surface, &VulkanObjects.SurfaceCapabilities);

	{
		uint32_t SurfaceFormatCount;
		VkSurfaceFormatKHR SurfaceFormats[MAX_SURFACE_FORMATS];
		vkGetPhysicalDeviceSurfaceFormatsKHR(VulkanObjects.PhysicalDevice, VulkanObjects.Surface, &SurfaceFormatCount, NULL);
		vkGetPhysicalDeviceSurfaceFormatsKHR(VulkanObjects.PhysicalDevice, VulkanObjects.Surface, &SurfaceFormatCount, SurfaceFormats);

		VulkanObjects.SwapChainImageFormat = SurfaceFormats[0];

		for (int i = 0; i < SurfaceFormatCount; i++)
		{
			if (SurfaceFormats[i].format == VK_FORMAT_B8G8R8A8_SRGB &&
				SurfaceFormats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
			{
				VulkanObjects.SwapChainImageFormat = SurfaceFormats[i];
				break;
			}
		}
	}
	
	{
		uint32_t PresentModeCount;
		VkPresentModeKHR PresentModes[MAX_PRESENT_MODES];
		vkGetPhysicalDeviceSurfacePresentModesKHR(VulkanObjects.PhysicalDevice, VulkanObjects.Surface, &PresentModeCount, NULL);
		vkGetPhysicalDeviceSurfacePresentModesKHR(VulkanObjects.PhysicalDevice, VulkanObjects.Surface, &PresentModeCount, PresentModes);

		VulkanObjects.SwapChainPresentMode = VK_PRESENT_MODE_FIFO_KHR;

		for (int i = 0; i < PresentModeCount; i++)
		{
			if (PresentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
			{
				VulkanObjects.SwapChainPresentMode = VK_PRESENT_MODE_MAILBOX_KHR;
			}
		}
	}

	VulkanObjects.SwapChainImageCount = VulkanObjects.SurfaceCapabilities.minImageCount + 1;

	if (VulkanObjects.SurfaceCapabilities.maxImageCount > 0 && VulkanObjects.SwapChainImageCount > VulkanObjects.SurfaceCapabilities.maxImageCount)
	{
		VulkanObjects.SwapChainImageCount = VulkanObjects.SurfaceCapabilities.maxImageCount;
	}

	{
		VkFormat Formats[] = {
			VK_FORMAT_D32_SFLOAT,
			VK_FORMAT_D32_SFLOAT_S8_UINT,
			VK_FORMAT_D24_UNORM_S8_UINT
		};

		VulkanObjects.DepthFormat == VK_FORMAT_UNDEFINED;

		for (int i = 0; i < ARRAYSIZE(Formats); i++) {
			VkFormatProperties Props;
			vkGetPhysicalDeviceFormatProperties(VulkanObjects.PhysicalDevice, Formats[i], &Props);

			if ((Props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) == VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
			{
				VulkanObjects.DepthFormat = Formats[i];
				break;
			}
		}

		if (VulkanObjects.DepthFormat == VK_FORMAT_UNDEFINED)
			FailFastWithMessage("failed to find supported format!");
	}

	{
		VkAttachmentDescription Attachments[2] = { 0 };
		Attachments[0].format = VulkanObjects.SwapChainImageFormat.format;
		Attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
		Attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		Attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		Attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		Attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		Attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		Attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		Attachments[1].format = VulkanObjects.DepthFormat;
		Attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
		Attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		Attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		Attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		Attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		Attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		Attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkAttachmentReference ColorAttachmentRef = { 0 };
		ColorAttachmentRef.attachment = 0;
		ColorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkAttachmentReference DepthAttachmentRef = { 0 };
		DepthAttachmentRef.attachment = 1;
		DepthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkSubpassDescription Subpass = { 0 };
		Subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		Subpass.colorAttachmentCount = 1;
		Subpass.pColorAttachments = &ColorAttachmentRef;
		Subpass.pDepthStencilAttachment = &DepthAttachmentRef;

		VkSubpassDependency Dependency = { 0 };
		Dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
		Dependency.dstSubpass = 0;
		Dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		Dependency.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		Dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		Dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

		VkRenderPassCreateInfo RenderPassInfo = { 0 };
		RenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		RenderPassInfo.attachmentCount = ARRAYSIZE(Attachments);
		RenderPassInfo.pAttachments = Attachments;
		RenderPassInfo.subpassCount = 1;
		RenderPassInfo.pSubpasses = &Subpass;
		RenderPassInfo.dependencyCount = 1;
		RenderPassInfo.pDependencies = &Dependency;

		THROW_ON_FAIL_VK(vkCreateRenderPass(VulkanObjects.Device, &RenderPassInfo, NULL, &VulkanObjects.RenderPass));
	}

	VkDescriptorSetLayout DescriptorSetLayout;

	{
		VkDescriptorSetLayoutBinding Bindings[2] = { 0 };
		Bindings[0].binding = 0;
		Bindings[0].descriptorCount = 1;
		Bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		Bindings[0].pImmutableSamplers = NULL;
		Bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

		Bindings[1].binding = 1;
		Bindings[1].descriptorCount = 1;
		Bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		Bindings[1].pImmutableSamplers = NULL;
		Bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

		VkDescriptorSetLayoutCreateInfo LayoutInfo = { 0 };
		LayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		LayoutInfo.bindingCount = ARRAYSIZE(Bindings);
		LayoutInfo.pBindings = Bindings;

		THROW_ON_FAIL_VK(vkCreateDescriptorSetLayout(VulkanObjects.Device, &LayoutInfo, NULL, &DescriptorSetLayout));
	}

	{
		VkShaderModule VertexShaderModule;
		{
			HANDLE VertexShaderFile = CreateFileW(L"vert.spv", GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			VALIDATE_HANDLE(VertexShaderFile);

			SIZE_T VertexShaderSize;
			THROW_ON_FALSE(GetFileSizeEx(VertexShaderFile, &VertexShaderSize));

			HANDLE VertexShaderFileMap = CreateFileMappingW(VertexShaderFile, NULL, PAGE_READONLY, 0, 0, NULL);
			VALIDATE_HANDLE(VertexShaderFileMap);

			const void* VertexShaderBytecode = MapViewOfFile(VertexShaderFileMap, FILE_MAP_READ, 0, 0, 0);

			VkShaderModuleCreateInfo CreateInfo = { 0 };
			CreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			CreateInfo.codeSize = VertexShaderSize;
			CreateInfo.pCode = VertexShaderBytecode;

			THROW_ON_FAIL_VK(vkCreateShaderModule(VulkanObjects.Device, &CreateInfo, NULL, &VertexShaderModule));

			THROW_ON_FALSE(UnmapViewOfFile(VertexShaderBytecode));
			THROW_ON_FALSE(CloseHandle(VertexShaderFileMap));
			THROW_ON_FALSE(CloseHandle(VertexShaderFile));
		}

		VkShaderModule FragmentShaderModule;
		{
			HANDLE FragmentShaderFile = CreateFileW(L"frag.spv", GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
			VALIDATE_HANDLE(FragmentShaderFile);

			SIZE_T FragmentShaderSize;
			THROW_ON_FALSE(GetFileSizeEx(FragmentShaderFile, &FragmentShaderSize));

			HANDLE FragmentShaderFileMap = CreateFileMappingW(FragmentShaderFile, NULL, PAGE_READONLY, 0, 0, NULL);
			VALIDATE_HANDLE(FragmentShaderFileMap);

			const void* FragmentShaderBytecode = MapViewOfFile(FragmentShaderFileMap, FILE_MAP_READ, 0, 0, 0);

			VkShaderModuleCreateInfo createInfo = { 0 };
			createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
			createInfo.codeSize = FragmentShaderSize;
			createInfo.pCode = FragmentShaderBytecode;

			THROW_ON_FAIL_VK(vkCreateShaderModule(VulkanObjects.Device, &createInfo, NULL, &FragmentShaderModule));

			THROW_ON_FALSE(UnmapViewOfFile(FragmentShaderBytecode));
			THROW_ON_FALSE(CloseHandle(FragmentShaderFileMap));
			THROW_ON_FALSE(CloseHandle(FragmentShaderFile));
		}

		VkPipelineShaderStageCreateInfo ShaderStages[2] = { 0 };
		ShaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		ShaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		ShaderStages[0].module = VertexShaderModule;
		ShaderStages[0].pName = "main";
		
		ShaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		ShaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		ShaderStages[1].module = FragmentShaderModule;
		ShaderStages[1].pName = "main";
		
		VkVertexInputBindingDescription BindingDescription = { 0 };
		BindingDescription.binding = 0;
		BindingDescription.stride = sizeof(struct Vertex);
		BindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		VkVertexInputAttributeDescription AttributeDescriptions[3] = { 0 };
		AttributeDescriptions[0].binding = 0;
		AttributeDescriptions[0].location = 0;
		AttributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
		AttributeDescriptions[0].offset = offsetof(struct Vertex, Pos);

		AttributeDescriptions[1].binding = 0;
		AttributeDescriptions[1].location = 1;
		AttributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
		AttributeDescriptions[1].offset = offsetof(struct Vertex, Color);

		AttributeDescriptions[2].binding = 0;
		AttributeDescriptions[2].location = 2;
		AttributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
		AttributeDescriptions[2].offset = offsetof(struct Vertex, TexCoord);

		VkPipelineVertexInputStateCreateInfo VertexInputInfo = { 0 };
		VertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		VertexInputInfo.vertexBindingDescriptionCount = 1;
		VertexInputInfo.pVertexBindingDescriptions = &BindingDescription;
		VertexInputInfo.vertexAttributeDescriptionCount = ARRAYSIZE(AttributeDescriptions);
		VertexInputInfo.pVertexAttributeDescriptions = AttributeDescriptions;

		VkPipelineInputAssemblyStateCreateInfo InputAssembly = { 0 };
		InputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		InputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		InputAssembly.primitiveRestartEnable = VK_FALSE;

		VkPipelineViewportStateCreateInfo ViewportState = { 0 };
		ViewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		ViewportState.viewportCount = 1;
		ViewportState.scissorCount = 1;

		VkPipelineRasterizationStateCreateInfo Rasterizer = { 0 };
		Rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		Rasterizer.depthClampEnable = VK_FALSE;
		Rasterizer.rasterizerDiscardEnable = VK_FALSE;
		Rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
		Rasterizer.lineWidth = 1.0f;
		Rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
		Rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		Rasterizer.depthBiasEnable = VK_FALSE;

		VkPipelineMultisampleStateCreateInfo Multisampling = { 0 };
		Multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		Multisampling.sampleShadingEnable = VK_FALSE;
		Multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		VkPipelineDepthStencilStateCreateInfo DepthStencil = { 0 };
		DepthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		DepthStencil.depthTestEnable = VK_TRUE;
		DepthStencil.depthWriteEnable = VK_TRUE;
		DepthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
		DepthStencil.depthBoundsTestEnable = VK_FALSE;
		DepthStencil.stencilTestEnable = VK_FALSE;

		VkPipelineColorBlendAttachmentState ColorBlendAttachment = { 0 };
		ColorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		ColorBlendAttachment.blendEnable = VK_FALSE;

		VkPipelineColorBlendStateCreateInfo ColorBlending = { 0 };
		ColorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		ColorBlending.logicOpEnable = VK_FALSE;
		ColorBlending.logicOp = VK_LOGIC_OP_COPY;
		ColorBlending.attachmentCount = 1;
		ColorBlending.pAttachments = &ColorBlendAttachment;
		ColorBlending.blendConstants[0] = 0.0f;
		ColorBlending.blendConstants[1] = 0.0f;
		ColorBlending.blendConstants[2] = 0.0f;
		ColorBlending.blendConstants[3] = 0.0f;

		VkDynamicState DynamicStates[] = {
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR
		};

		VkPipelineDynamicStateCreateInfo DynamicState = { 0 };
		DynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		DynamicState.dynamicStateCount = ARRAYSIZE(DynamicStates);
		DynamicState.pDynamicStates = DynamicStates;

		VkPipelineLayoutCreateInfo PipelineLayoutInfo = { 0 };
		PipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		PipelineLayoutInfo.setLayoutCount = 1;
		PipelineLayoutInfo.pSetLayouts = &DescriptorSetLayout;

		THROW_ON_FAIL_VK(vkCreatePipelineLayout(VulkanObjects.Device, &PipelineLayoutInfo, NULL, &VulkanObjects.PipelineLayout));

		VkGraphicsPipelineCreateInfo PipelineInfo = { 0 };
		PipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		PipelineInfo.stageCount = ARRAYSIZE(ShaderStages);
		PipelineInfo.pStages = ShaderStages;
		PipelineInfo.pVertexInputState = &VertexInputInfo;
		PipelineInfo.pInputAssemblyState = &InputAssembly;
		PipelineInfo.pViewportState = &ViewportState;
		PipelineInfo.pRasterizationState = &Rasterizer;
		PipelineInfo.pMultisampleState = &Multisampling;
		PipelineInfo.pDepthStencilState = &DepthStencil;
		PipelineInfo.pColorBlendState = &ColorBlending;
		PipelineInfo.pDynamicState = &DynamicState;
		PipelineInfo.layout = VulkanObjects.PipelineLayout;
		PipelineInfo.renderPass = VulkanObjects.RenderPass;
		PipelineInfo.subpass = 0;
		PipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
		THROW_ON_FAIL_VK(vkCreateGraphicsPipelines(VulkanObjects.Device, VK_NULL_HANDLE, 1, &PipelineInfo, NULL, &VulkanObjects.GraphicsPipeline));

		vkDestroyShaderModule(VulkanObjects.Device, FragmentShaderModule, NULL);
		vkDestroyShaderModule(VulkanObjects.Device, VertexShaderModule, NULL);
	}

	VkCommandPool CommandPool;

	{
		VkCommandPoolCreateInfo PoolInfo = { 0 };
		PoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		PoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		PoolInfo.queueFamilyIndex = VulkanObjects.QueueFamilyIndices.GraphicsFamily;
		THROW_ON_FAIL_VK(vkCreateCommandPool(VulkanObjects.Device, &PoolInfo, NULL, &CommandPool));
	}

	VkImage TextureImage;
	VkDeviceMemory TextureImageMemory;

	{
		VkDeviceSize ImageSize = TEXTURE_WIDTH * TEXTURE_HEIGHT * 4;

		VkBuffer StagingBuffer;
		VkDeviceMemory StagingBufferMemory;
		CreateBuffer(VulkanObjects.PhysicalDevice, VulkanObjects.Device, ImageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &StagingBuffer, &StagingBufferMemory);

		{
			uint16_t* Data;
			vkMapMemory(VulkanObjects.Device, StagingBufferMemory, 0, ImageSize, 0, &Data);

			for (UINT y = 0; y < TEXTURE_HEIGHT; y++)
			{
				for (UINT x = 0; x < TEXTURE_WIDTH; x++)
				{
					Data[(y * TEXTURE_WIDTH + x) * (BYTES_PER_TEXEL / sizeof(WORD))] = x == 0 || x == (TEXTURE_WIDTH - 1) || y == 0 || y == (TEXTURE_HEIGHT - 1) ? 0b1111100000000000 : rand() * (UINT16_MAX / RAND_MAX);
				}
			}

			vkUnmapMemory(VulkanObjects.Device, StagingBufferMemory);
		}

		{
			VkImageCreateInfo ImageInfo = { 0 };
			ImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			ImageInfo.imageType = VK_IMAGE_TYPE_2D;
			ImageInfo.extent.width = TEXTURE_WIDTH;
			ImageInfo.extent.height = TEXTURE_HEIGHT;
			ImageInfo.extent.depth = 1;
			ImageInfo.mipLevels = 1;
			ImageInfo.arrayLayers = 1;
			ImageInfo.format = IMAGE_FORMAT;
			ImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
			ImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			ImageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
			ImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
			ImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			THROW_ON_FAIL_VK(vkCreateImage(VulkanObjects.Device, &ImageInfo, NULL, &TextureImage));
		}

		{
			VkMemoryRequirements MemRequirements;
			vkGetImageMemoryRequirements(VulkanObjects.Device, TextureImage, &MemRequirements);

			VkMemoryAllocateInfo AllocInfo = { 0 };
			AllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			AllocInfo.allocationSize = MemRequirements.size;
			AllocInfo.memoryTypeIndex = FindMemoryType(VulkanObjects.PhysicalDevice, MemRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			THROW_ON_FAIL_VK(vkAllocateMemory(VulkanObjects.Device, &AllocInfo, NULL, &TextureImageMemory));
		}

		vkBindImageMemory(VulkanObjects.Device, TextureImage, TextureImageMemory, 0);

		{
			VkCommandBuffer CommandBuffer = BeginSingleTimeCommands(VulkanObjects.Device, CommandPool);

			{
				VkImageMemoryBarrier Barrier = { 0 };
				Barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				Barrier.srcAccessMask = 0;
				Barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				Barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
				Barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				Barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				Barrier.image = TextureImage;
				Barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				Barrier.subresourceRange.baseMipLevel = 0;
				Barrier.subresourceRange.levelCount = 1;
				Barrier.subresourceRange.baseArrayLayer = 0;
				Barrier.subresourceRange.layerCount = 1;
				vkCmdPipelineBarrier(
					CommandBuffer,
					VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
					VK_PIPELINE_STAGE_TRANSFER_BIT,
					0,
					0,
					NULL,
					0,
					NULL,
					1,
					&Barrier
				);
			}

			{
				VkBufferImageCopy Region = { 0 };
				Region.bufferOffset = 0;
				Region.bufferRowLength = 0;
				Region.bufferImageHeight = 0;
				Region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				Region.imageSubresource.mipLevel = 0;
				Region.imageSubresource.baseArrayLayer = 0;
				Region.imageSubresource.layerCount = 1;
				Region.imageOffset.x = 0;
				Region.imageOffset.y = 0;
				Region.imageOffset.z = 0;
				Region.imageExtent.width = TEXTURE_WIDTH;
				Region.imageExtent.height = TEXTURE_HEIGHT;
				Region.imageExtent.depth = 1;
				vkCmdCopyBufferToImage(CommandBuffer, StagingBuffer, TextureImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &Region);
			}

			{
				VkImageMemoryBarrier Barrier = { 0 };
				Barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
				Barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				Barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
				Barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				Barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				Barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				Barrier.image = TextureImage;
				Barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				Barrier.subresourceRange.baseMipLevel = 0;
				Barrier.subresourceRange.levelCount = 1;
				Barrier.subresourceRange.baseArrayLayer = 0;
				Barrier.subresourceRange.layerCount = 1;

				vkCmdPipelineBarrier(
					CommandBuffer,
					VK_PIPELINE_STAGE_TRANSFER_BIT,
					VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
					0,
					0,
					NULL,
					0,
					NULL,
					1,
					&Barrier
				);
			}

			EndSingleTimeCommands(VulkanObjects.Device, CommandPool, VulkanObjects.GraphicsQueue, CommandBuffer);
		}

		vkDestroyBuffer(VulkanObjects.Device, StagingBuffer, NULL);
		vkFreeMemory(VulkanObjects.Device, StagingBufferMemory, NULL);
	}

	VkImageView TextureImageView;

	{
		VkImageViewCreateInfo ViewInfo = { 0 };
		ViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		ViewInfo.image = TextureImage;
		ViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		ViewInfo.format = IMAGE_FORMAT;
		ViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		ViewInfo.subresourceRange.baseMipLevel = 0;
		ViewInfo.subresourceRange.levelCount = 1;
		ViewInfo.subresourceRange.baseArrayLayer = 0;
		ViewInfo.subresourceRange.layerCount = 1;
		THROW_ON_FAIL_VK(vkCreateImageView(VulkanObjects.Device, &ViewInfo, NULL, &TextureImageView));
	}

	VkSampler TextureSampler;

	{
		VkPhysicalDeviceProperties DeviceProperties = { 0 };
		vkGetPhysicalDeviceProperties(VulkanObjects.PhysicalDevice, &DeviceProperties);

		VkSamplerCreateInfo SamplerInfo = { 0 };
		SamplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
		SamplerInfo.magFilter = VK_FILTER_LINEAR;
		SamplerInfo.minFilter = VK_FILTER_LINEAR;
		SamplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		SamplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		SamplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		SamplerInfo.anisotropyEnable = VK_TRUE;
		SamplerInfo.maxAnisotropy = DeviceProperties.limits.maxSamplerAnisotropy;
		SamplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
		SamplerInfo.unnormalizedCoordinates = VK_FALSE;
		SamplerInfo.compareEnable = VK_FALSE;
		SamplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
		SamplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		THROW_ON_FAIL_VK(vkCreateSampler(VulkanObjects.Device, &SamplerInfo, NULL, &TextureSampler));
	}

	{
		VkBuffer StagingBuffer;
		VkDeviceMemory stagingBufferMemory;
		CreateBuffer(VulkanObjects.PhysicalDevice, VulkanObjects.Device, sizeof(Vertices), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &StagingBuffer, &stagingBufferMemory);

		void* Data;
		vkMapMemory(VulkanObjects.Device, stagingBufferMemory, 0, sizeof(Vertices), 0, &Data);
		memcpy(Data, Vertices, sizeof(Vertices));
		vkUnmapMemory(VulkanObjects.Device, stagingBufferMemory);

		CreateBuffer(VulkanObjects.PhysicalDevice, VulkanObjects.Device, sizeof(Vertices), VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &VulkanObjects.VertexBuffer, &VulkanObjects.VertexBufferMemory);

		VkCommandBuffer CommandBuffer = BeginSingleTimeCommands(VulkanObjects.Device, CommandPool);

		{
			VkBufferCopy CopyRegion = { 0 };
			CopyRegion.size = sizeof(Vertices);
			vkCmdCopyBuffer(CommandBuffer, StagingBuffer, VulkanObjects.VertexBuffer, 1, &CopyRegion);
		}

		EndSingleTimeCommands(VulkanObjects.Device, CommandPool, VulkanObjects.GraphicsQueue, CommandBuffer);

		vkDestroyBuffer(VulkanObjects.Device, StagingBuffer, NULL);
		vkFreeMemory(VulkanObjects.Device, stagingBufferMemory, NULL);
	}

	{
		VkBuffer stagingBuffer;
		VkDeviceMemory stagingBufferMemory;
		CreateBuffer(VulkanObjects.PhysicalDevice, VulkanObjects.Device, sizeof(Indices), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &stagingBuffer, &stagingBufferMemory);

		{
			void* Data;
			vkMapMemory(VulkanObjects.Device, stagingBufferMemory, 0, sizeof(Indices), 0, &Data);
			memcpy(Data, Indices, sizeof(Indices));
			vkUnmapMemory(VulkanObjects.Device, stagingBufferMemory);
		}

		CreateBuffer(VulkanObjects.PhysicalDevice, VulkanObjects.Device, sizeof(Indices), VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &VulkanObjects.IndexBuffer, &VulkanObjects.IndexBufferMemory);

		VkCommandBuffer CommandBuffer = BeginSingleTimeCommands(VulkanObjects.Device, CommandPool);

		{
			VkBufferCopy CopyRegion = { 0 };
			CopyRegion.size = sizeof(Indices);
			vkCmdCopyBuffer(CommandBuffer, stagingBuffer, VulkanObjects.IndexBuffer, 1, &CopyRegion);
		}

		EndSingleTimeCommands(VulkanObjects.Device, CommandPool, VulkanObjects.GraphicsQueue, CommandBuffer);

		vkDestroyBuffer(VulkanObjects.Device, stagingBuffer, NULL);
		vkFreeMemory(VulkanObjects.Device, stagingBufferMemory, NULL);
	}

	VkDeviceMemory UniformBuffersMemory[MAX_FRAMES_IN_FLIGHT];
	VkBuffer UniformBuffers[MAX_FRAMES_IN_FLIGHT];

	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		CreateBuffer(VulkanObjects.PhysicalDevice, VulkanObjects.Device, sizeof(struct UniformBufferObject), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &UniformBuffers[i], &UniformBuffersMemory[i]);
		vkMapMemory(VulkanObjects.Device, UniformBuffersMemory[i], 0, sizeof(struct UniformBufferObject), 0, &VulkanObjects.UniformBuffersMapped[i]);
	}

	VkDescriptorPool DescriptorPool;

	{
		VkDescriptorPoolSize PoolSizes[2] = { 0 };
		PoolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		PoolSizes[0].descriptorCount = MAX_FRAMES_IN_FLIGHT;
		PoolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		PoolSizes[1].descriptorCount = MAX_FRAMES_IN_FLIGHT;

		VkDescriptorPoolCreateInfo PoolInfo = { 0 };
		PoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		PoolInfo.poolSizeCount = ARRAYSIZE(PoolSizes);
		PoolInfo.pPoolSizes = PoolSizes;
		PoolInfo.maxSets = MAX_FRAMES_IN_FLIGHT;
		THROW_ON_FAIL_VK(vkCreateDescriptorPool(VulkanObjects.Device, &PoolInfo, NULL, &DescriptorPool));
	}

	{
		VkDescriptorSetLayout DescriptorSetLayouts[MAX_FRAMES_IN_FLIGHT] = { 0 };

		for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
		{
			DescriptorSetLayouts[i] = DescriptorSetLayout;
		}

		{
			VkDescriptorSetAllocateInfo AllocInfo = { 0 };
			AllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			AllocInfo.descriptorPool = DescriptorPool;
			AllocInfo.descriptorSetCount = MAX_FRAMES_IN_FLIGHT;
			AllocInfo.pSetLayouts = DescriptorSetLayouts;
			THROW_ON_FAIL_VK(vkAllocateDescriptorSets(VulkanObjects.Device, &AllocInfo, VulkanObjects.DescriptorSets));
		}
	}
	
	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		VkDescriptorBufferInfo BufferInfo = { 0 };
		BufferInfo.buffer = UniformBuffers[i];
		BufferInfo.offset = 0;
		BufferInfo.range = sizeof(struct UniformBufferObject);

		VkDescriptorImageInfo ImageInfo = { 0 };
		ImageInfo.sampler = TextureSampler;
		ImageInfo.imageView = TextureImageView;
		ImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkWriteDescriptorSet DescriptorWrites[2] = { 0 };
		DescriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		DescriptorWrites[0].dstSet = VulkanObjects.DescriptorSets[i];
		DescriptorWrites[0].dstBinding = 0;
		DescriptorWrites[0].dstArrayElement = 0;
		DescriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		DescriptorWrites[0].descriptorCount = 1;
		DescriptorWrites[0].pBufferInfo = &BufferInfo;

		DescriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		DescriptorWrites[1].dstSet = VulkanObjects.DescriptorSets[i];
		DescriptorWrites[1].dstBinding = 1;
		DescriptorWrites[1].dstArrayElement = 0;
		DescriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
		DescriptorWrites[1].descriptorCount = 1;
		DescriptorWrites[1].pImageInfo = &ImageInfo;

		vkUpdateDescriptorSets(VulkanObjects.Device, ARRAYSIZE(DescriptorWrites), DescriptorWrites, 0, NULL);
	}

	{
		VkCommandBufferAllocateInfo AllocInfo = { 0 };
		AllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		AllocInfo.commandPool = CommandPool;
		AllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		AllocInfo.commandBufferCount = ARRAYSIZE(VulkanObjects.CommandBuffers);
		THROW_ON_FAIL_VK(vkAllocateCommandBuffers(VulkanObjects.Device, &AllocInfo, VulkanObjects.CommandBuffers));
	}
	
	{
		VkSemaphoreCreateInfo SemaphoreInfo = { 0 };
		SemaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

		VkFenceCreateInfo FenceInfo = { 0 };
		FenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		FenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

		for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
			THROW_ON_FAIL_VK(vkCreateSemaphore(VulkanObjects.Device, &SemaphoreInfo, NULL, &VulkanObjects.ImageAvailableSemaphores[i]));
			THROW_ON_FAIL_VK(vkCreateSemaphore(VulkanObjects.Device, &SemaphoreInfo, NULL, &VulkanObjects.RenderFinishedSemaphores[i]));
			THROW_ON_FAIL_VK(vkCreateFence(VulkanObjects.Device, &FenceInfo, NULL, &VulkanObjects.InFlightFences[i]));
		}
	}

	THROW_ON_FALSE(SetWindowLongPtrW(Window, GWLP_WNDPROC, (LONG_PTR)WndProc) != 0);

	
	DispatchMessageW(&(MSG) {
		.hwnd = Window,
		.message = WM_INIT,
		.wParam = &VulkanObjects,
		.lParam = 0
	});
	

	DispatchMessageW(&(MSG) {
		.hwnd = Window,
		.message = WM_SIZE,
		.wParam = SIZE_RESTORED,
		.lParam = MAKELONG(WindowRect.right - WindowRect.left, WindowRect.bottom - WindowRect.top)
	});

	MSG Message = { 0 };

	while (Message.message != WM_QUIT)
	{
		if (PeekMessageW(&Message, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&Message);
			DispatchMessageW(&Message);
		}
	}

	vkDeviceWaitIdle(VulkanObjects.Device);

	vkDestroyImageView(VulkanObjects.Device, VulkanObjects.DepthImageView, NULL);
	vkDestroyImage(VulkanObjects.Device, VulkanObjects.DepthImage, NULL);
	vkFreeMemory(VulkanObjects.Device, VulkanObjects.DepthImageMemory, NULL);

	for (int i = 0; i < VulkanObjects.SwapChainImageCount; i++)
	{
		vkDestroyFramebuffer(VulkanObjects.Device, VulkanObjects.SwapChainFramebuffers[i], NULL);
	}

	for (int i = 0; i < VulkanObjects.SwapChainImageCount; i++)
	{
		vkDestroyImageView(VulkanObjects.Device, VulkanObjects.SwapChainImageViews[i], NULL);
	}

	vkDestroySwapchainKHR(VulkanObjects.Device, VulkanObjects.SwapChain, NULL);

	vkDestroyPipeline(VulkanObjects.Device, VulkanObjects.GraphicsPipeline, NULL);
	vkDestroyPipelineLayout(VulkanObjects.Device, VulkanObjects.PipelineLayout, NULL);
	vkDestroyRenderPass(VulkanObjects.Device, VulkanObjects.RenderPass, NULL);

	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		vkDestroyBuffer(VulkanObjects.Device, UniformBuffers[i], NULL);
		vkFreeMemory(VulkanObjects.Device, UniformBuffersMemory[i], NULL);
	}

	vkDestroyDescriptorPool(VulkanObjects.Device, DescriptorPool, NULL);

	vkDestroySampler(VulkanObjects.Device, TextureSampler, NULL);
	vkDestroyImageView(VulkanObjects.Device, TextureImageView, NULL);

	vkDestroyImage(VulkanObjects.Device, TextureImage, NULL);
	vkFreeMemory(VulkanObjects.Device, TextureImageMemory, NULL);

	vkDestroyDescriptorSetLayout(VulkanObjects.Device, DescriptorSetLayout, NULL);

	vkDestroyBuffer(VulkanObjects.Device, VulkanObjects.IndexBuffer, NULL);
	vkFreeMemory(VulkanObjects.Device, VulkanObjects.IndexBufferMemory, NULL);

	vkDestroyBuffer(VulkanObjects.Device, VulkanObjects.VertexBuffer, NULL);
	vkFreeMemory(VulkanObjects.Device, VulkanObjects.VertexBufferMemory, NULL);

	for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
	{
		vkDestroySemaphore(VulkanObjects.Device, VulkanObjects.RenderFinishedSemaphores[i], NULL);
		vkDestroySemaphore(VulkanObjects.Device, VulkanObjects.ImageAvailableSemaphores[i], NULL);
		vkDestroyFence(VulkanObjects.Device, VulkanObjects.InFlightFences[i], NULL);
	}

	vkDestroyCommandPool(VulkanObjects.Device, CommandPool, NULL);

	vkDestroyDevice(VulkanObjects.Device, NULL);

#ifdef _DEBUG
		DestroyDebugUtilsMessengerEXT(VulkanInstance, VulkanObjects.DebugMessenger, NULL);
#endif

	vkDestroySurfaceKHR(VulkanInstance, VulkanObjects.Surface, NULL);
	vkDestroyInstance(VulkanInstance, NULL);

	THROW_ON_FALSE(UnregisterClassW(WindowClassName, Instance));

	THROW_ON_FALSE(DestroyCursor(Cursor));
	THROW_ON_FALSE(DestroyIcon(Icon));

	return EXIT_SUCCESS;
}

LRESULT CALLBACK PreInitProc(HWND Window, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProcW(Window, message, wParam, lParam);
	}
	return 0;
}

LRESULT CALLBACK IdleProc(HWND Window, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_PAINT:
		Sleep(25);
		break;
	case WM_SIZE:
		if (wParam == SIZE_RESTORED)
			THROW_ON_FALSE(SetWindowLongPtrW(Window, GWLP_WNDPROC, (LONG_PTR)WndProc) != 0);
		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProcW(Window, message, wParam, lParam);
	}
	return 0;
}

LRESULT CALLBACK WndProc(HWND Window, UINT message, WPARAM wParam, LPARAM lParam)
{
	static uint32_t CurrentFrame = 0;
	static bool bFullScreen = false;

	static int WindowWidth = 0;
	static int WindowHeight = 0;

	static struct VulkanObjects* restrict VulkanObjects = NULL;

	static struct
	{
		LARGE_INTEGER ProcessorFrequency;
		LARGE_INTEGER TickCount;
	} Timer = { 0 };

	switch (message)
	{
	case WM_INIT:
		QueryPerformanceFrequency(&Timer.ProcessorFrequency);

		VulkanObjects = ((struct VulkanObjects*)wParam);
		break;
	case WM_PAINT:
		vkWaitForFences(VulkanObjects->Device, 1, &VulkanObjects->InFlightFences[CurrentFrame], VK_TRUE, UINT64_MAX);

		uint32_t ImageIndex;
		THROW_ON_FAIL_VK(vkAcquireNextImageKHR(VulkanObjects->Device, VulkanObjects->SwapChain, UINT64_MAX, VulkanObjects->ImageAvailableSemaphores[CurrentFrame], VK_NULL_HANDLE, &ImageIndex));

		{
			LARGE_INTEGER TickCountNow;
			QueryPerformanceCounter(&TickCountNow);
			ULONGLONG TickCountDelta = TickCountNow.QuadPart - Timer.TickCount.QuadPart;

			float Time = TickCountDelta / ((float)Timer.ProcessorFrequency.QuadPart);

			struct UniformBufferObject Ubo = { 0 };
			glm_mat4_identity(Ubo.Model);
			glm_rotate(Ubo.Model, Time * glm_rad(90.0f), (vec3) { 0.0f, 0.0f, 1.0f });
			glm_lookat_rh((vec3) { 2.0f, 2.0f, 2.0f }, (vec3) { 0.0f, 0.0f, 0.0f }, (vec3) { 0.0f, 0.0f, 1.0f }, Ubo.View);
			glm_perspective_rh_zo(glm_rad(45.0f), VulkanObjects->SwapChainExtent.width / (float)VulkanObjects->SwapChainExtent.height, 0.1f, 10.0f, Ubo.Proj);
			Ubo.Proj[1][1] *= -1;

			memcpy(VulkanObjects->UniformBuffersMapped[CurrentFrame], &Ubo, sizeof(Ubo));
		}

		vkResetFences(VulkanObjects->Device, 1, &VulkanObjects->InFlightFences[CurrentFrame]);

		vkResetCommandBuffer(VulkanObjects->CommandBuffers[CurrentFrame], 0);

		{
			VkCommandBufferBeginInfo BeginInfo = { 0 };
			BeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			THROW_ON_FAIL_VK(vkBeginCommandBuffer(VulkanObjects->CommandBuffers[CurrentFrame], &BeginInfo));
		}

		{
			VkClearValue ClearValues[2] = { 0 };
			ClearValues[0].color = (VkClearColorValue){ {0.0f, 0.0f, 0.0f, 1.0f} };
			ClearValues[1].depthStencil.depth = 1.0f;
			ClearValues[1].depthStencil.stencil = 0;

			VkRenderPassBeginInfo RenderPassInfo = { 0 };
			RenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
			RenderPassInfo.renderPass = VulkanObjects->RenderPass;
			RenderPassInfo.framebuffer = VulkanObjects->SwapChainFramebuffers[ImageIndex];
			RenderPassInfo.renderArea.offset.x = 0;
			RenderPassInfo.renderArea.offset.y = 0;
			RenderPassInfo.renderArea.extent = VulkanObjects->SwapChainExtent;
			RenderPassInfo.clearValueCount = ARRAYSIZE(ClearValues);
			RenderPassInfo.pClearValues = ClearValues;
			vkCmdBeginRenderPass(VulkanObjects->CommandBuffers[CurrentFrame], &RenderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
		}

		vkCmdBindPipeline(VulkanObjects->CommandBuffers[CurrentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, VulkanObjects->GraphicsPipeline);

		{
			VkViewport Viewport = { 0 };
			Viewport.x = 0.0f;
			Viewport.y = 0.0f;
			Viewport.width = VulkanObjects->SwapChainExtent.width;
			Viewport.height = VulkanObjects->SwapChainExtent.height;
			Viewport.minDepth = 0.0f;
			Viewport.maxDepth = 1.0f;
			vkCmdSetViewport(VulkanObjects->CommandBuffers[CurrentFrame], 0, 1, &Viewport);
		}

		{
			VkRect2D Scissor = { 0 };
			Scissor.offset.x = 0;
			Scissor.offset.y = 0;
			Scissor.extent = VulkanObjects->SwapChainExtent;
			vkCmdSetScissor(VulkanObjects->CommandBuffers[CurrentFrame], 0, 1, &Scissor);
		}

		{
			VkBuffer vertexBuffers[] = { VulkanObjects->VertexBuffer };
			VkDeviceSize offsets[] = { 0 };
			vkCmdBindVertexBuffers(VulkanObjects->CommandBuffers[CurrentFrame], 0, 1, vertexBuffers, offsets);
		}

		vkCmdBindIndexBuffer(VulkanObjects->CommandBuffers[CurrentFrame], VulkanObjects->IndexBuffer, 0, VK_INDEX_TYPE_UINT16);

		vkCmdBindDescriptorSets(VulkanObjects->CommandBuffers[CurrentFrame], VK_PIPELINE_BIND_POINT_GRAPHICS, VulkanObjects->PipelineLayout, 0, 1, &VulkanObjects->DescriptorSets[CurrentFrame], 0, NULL);

		vkCmdDrawIndexed(VulkanObjects->CommandBuffers[CurrentFrame], ARRAYSIZE(Indices), 1, 0, 0, 0);

		vkCmdEndRenderPass(VulkanObjects->CommandBuffers[CurrentFrame]);

		THROW_ON_FAIL_VK(vkEndCommandBuffer(VulkanObjects->CommandBuffers[CurrentFrame]));

		VkSemaphore SignalSemaphores[] = { VulkanObjects->RenderFinishedSemaphores[CurrentFrame] };

		{
			VkSemaphore WaitSemaphores[] = { VulkanObjects->ImageAvailableSemaphores[CurrentFrame] };
			VkPipelineStageFlags WaitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };

			VkSubmitInfo SubmitInfo = { 0 };
			SubmitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
			SubmitInfo.waitSemaphoreCount = 1;
			SubmitInfo.pWaitSemaphores = WaitSemaphores;
			SubmitInfo.pWaitDstStageMask = WaitStages;
			SubmitInfo.commandBufferCount = 1;
			SubmitInfo.pCommandBuffers = &VulkanObjects->CommandBuffers[CurrentFrame];
			SubmitInfo.signalSemaphoreCount = 1;
			SubmitInfo.pSignalSemaphores = SignalSemaphores;
			THROW_ON_FAIL_VK(vkQueueSubmit(VulkanObjects->GraphicsQueue, 1, &SubmitInfo, VulkanObjects->InFlightFences[CurrentFrame]));
		}

		{
			VkSwapchainKHR SwapChains[] = { VulkanObjects->SwapChain };
			VkPresentInfoKHR PresentInfo = { 0 };
			PresentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
			PresentInfo.waitSemaphoreCount = 1;
			PresentInfo.pWaitSemaphores = SignalSemaphores;
			PresentInfo.swapchainCount = 1;
			PresentInfo.pSwapchains = SwapChains;
			PresentInfo.pImageIndices = &ImageIndex;
			THROW_ON_FAIL_VK(vkQueuePresentKHR(VulkanObjects->PresentQueue, &PresentInfo));
		}

		CurrentFrame = (CurrentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
		break;
		case WM_KEYDOWN:
			switch (wParam)
			{
			case VK_ESCAPE:
				THROW_ON_FALSE(DestroyWindow(Window));
				break;
			}
			break;
		case WM_SYSKEYDOWN:
			if (wParam == VK_RETURN && (lParam & 0x60000000) == 0x20000000)
			{
				bFullScreen = !bFullScreen;

				if (bFullScreen)
				{
					THROW_ON_FALSE(SetWindowLongPtrW(Window, GWL_EXSTYLE, WS_EX_TOPMOST) != 0);
					THROW_ON_FALSE(SetWindowLongPtrW(Window, GWL_STYLE, 0) != 0);

					THROW_ON_FALSE(ShowWindow(Window, SW_SHOWMAXIMIZED));
				}
				else
				{
					THROW_ON_FALSE(SetWindowLongPtrW(Window, GWL_STYLE, WS_OVERLAPPEDWINDOW) != 0);
					THROW_ON_FALSE(SetWindowLongPtrW(Window, GWL_EXSTYLE, 0) != 0);

					THROW_ON_FALSE(ShowWindow(Window, SW_SHOWMAXIMIZED));
				}
			}
			break;
	case WM_SIZE:
		if (wParam == SIZE_MINIMIZED)
		{
			THROW_ON_FALSE(SetWindowLongPtrW(Window, GWLP_WNDPROC, (LONG_PTR)IdleProc) != 0);
			break;
		}

		if (WindowWidth == LOWORD(lParam) && WindowHeight == HIWORD(lParam))
			break;

		vkDeviceWaitIdle(VulkanObjects->Device);

		vkDestroyImageView(VulkanObjects->Device, VulkanObjects->DepthImageView, NULL);
		vkDestroyImage(VulkanObjects->Device, VulkanObjects->DepthImage, NULL);
		vkFreeMemory(VulkanObjects->Device, VulkanObjects->DepthImageMemory, NULL);

		for (int i = 0; i < VulkanObjects->SwapChainImageCount; i++)
		{
			vkDestroyFramebuffer(VulkanObjects->Device, VulkanObjects->SwapChainFramebuffers[i], NULL);
		}

		for (int i = 0; i < VulkanObjects->SwapChainImageCount; i++)
		{
			vkDestroyImageView(VulkanObjects->Device, VulkanObjects->SwapChainImageViews[i], NULL);
		}

		vkDestroySwapchainKHR(VulkanObjects->Device, VulkanObjects->SwapChain, NULL);

		if (VulkanObjects->SurfaceCapabilities.currentExtent.width != UINT32_MAX)
		{
			VulkanObjects->SwapChainExtent = VulkanObjects->SurfaceCapabilities.currentExtent;
		}
		else
		{
			VulkanObjects->SwapChainExtent.width = ClampU32(WindowWidth, VulkanObjects->SurfaceCapabilities.minImageExtent.width, VulkanObjects->SurfaceCapabilities.maxImageExtent.width);
			VulkanObjects->SwapChainExtent.height = ClampU32(WindowHeight, VulkanObjects->SurfaceCapabilities.minImageExtent.height, VulkanObjects->SurfaceCapabilities.maxImageExtent.height);
		}

		{
			VkSwapchainCreateInfoKHR SwapchainCreateInfo = { 0 };
			SwapchainCreateInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
			SwapchainCreateInfo.surface = VulkanObjects->Surface;
			SwapchainCreateInfo.minImageCount = VulkanObjects->SwapChainImageCount;
			SwapchainCreateInfo.imageFormat = VulkanObjects->SwapChainImageFormat.format;
			SwapchainCreateInfo.imageColorSpace = VulkanObjects->SwapChainImageFormat.colorSpace;
			SwapchainCreateInfo.imageExtent = VulkanObjects->SwapChainExtent;
			SwapchainCreateInfo.imageArrayLayers = 1;
			SwapchainCreateInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

			uint32_t QueueFamilyIndicesU32[] = { VulkanObjects->QueueFamilyIndices.GraphicsFamily, VulkanObjects->QueueFamilyIndices.PresentFamily };

			if (VulkanObjects->QueueFamilyIndices.GraphicsFamily != VulkanObjects->QueueFamilyIndices.PresentFamily)
			{
				SwapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
				SwapchainCreateInfo.queueFamilyIndexCount = 2;
				SwapchainCreateInfo.pQueueFamilyIndices = QueueFamilyIndicesU32;
			}
			else
			{
				SwapchainCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
			}

			SwapchainCreateInfo.preTransform = VulkanObjects->SurfaceCapabilities.currentTransform;
			SwapchainCreateInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
			SwapchainCreateInfo.presentMode = VulkanObjects->SwapChainPresentMode;
			SwapchainCreateInfo.clipped = VK_TRUE;

			THROW_ON_FAIL_VK(vkCreateSwapchainKHR(VulkanObjects->Device, &SwapchainCreateInfo, NULL, &VulkanObjects->SwapChain));
		}

		vkGetSwapchainImagesKHR(VulkanObjects->Device, VulkanObjects->SwapChain, &VulkanObjects->SwapChainImageCount, NULL);
		vkGetSwapchainImagesKHR(VulkanObjects->Device, VulkanObjects->SwapChain, &VulkanObjects->SwapChainImageCount, VulkanObjects->SwapChainImages);

		for (int i = 0; i < VulkanObjects->SwapChainImageCount; i++)
		{
			VkImageViewCreateInfo ViewInfo = { 0 };
			ViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			ViewInfo.image = VulkanObjects->SwapChainImages[i];
			ViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			ViewInfo.format = VulkanObjects->SwapChainImageFormat.format;
			ViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			ViewInfo.subresourceRange.baseMipLevel = 0;
			ViewInfo.subresourceRange.levelCount = 1;
			ViewInfo.subresourceRange.baseArrayLayer = 0;
			ViewInfo.subresourceRange.layerCount = 1;
			THROW_ON_FAIL_VK(vkCreateImageView(VulkanObjects->Device, &ViewInfo, NULL, &VulkanObjects->SwapChainImageViews[i]));
		}
		
		{
			VkImageCreateInfo ImageInfo = { 0 };
			ImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
			ImageInfo.imageType = VK_IMAGE_TYPE_2D;
			ImageInfo.extent.width = VulkanObjects->SwapChainExtent.width;
			ImageInfo.extent.height = VulkanObjects->SwapChainExtent.height;
			ImageInfo.extent.depth = 1;
			ImageInfo.mipLevels = 1;
			ImageInfo.arrayLayers = 1;
			ImageInfo.format = VulkanObjects->DepthFormat;
			ImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
			ImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			ImageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
			ImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
			ImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
			THROW_ON_FAIL_VK(vkCreateImage(VulkanObjects->Device, &ImageInfo, NULL, &VulkanObjects->DepthImage));
		}

		{
			VkMemoryRequirements MemRequirements;
			vkGetImageMemoryRequirements(VulkanObjects->Device, VulkanObjects->DepthImage, &MemRequirements);

			VkMemoryAllocateInfo AllocInfo = { 0 };
			AllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
			AllocInfo.allocationSize = MemRequirements.size;
			AllocInfo.memoryTypeIndex = FindMemoryType(VulkanObjects->PhysicalDevice, MemRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			THROW_ON_FAIL_VK(vkAllocateMemory(VulkanObjects->Device, &AllocInfo, NULL, &VulkanObjects->DepthImageMemory));
		}

		vkBindImageMemory(VulkanObjects->Device, VulkanObjects->DepthImage, VulkanObjects->DepthImageMemory, 0);

		{
			VkImageViewCreateInfo ViewInfo = { 0 };
			ViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			ViewInfo.image = VulkanObjects->DepthImage;
			ViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			ViewInfo.format = VulkanObjects->DepthFormat;
			ViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
			ViewInfo.subresourceRange.baseMipLevel = 0;
			ViewInfo.subresourceRange.levelCount = 1;
			ViewInfo.subresourceRange.baseArrayLayer = 0;
			ViewInfo.subresourceRange.layerCount = 1;
			THROW_ON_FAIL_VK(vkCreateImageView(VulkanObjects->Device, &ViewInfo, NULL, &VulkanObjects->DepthImageView));
		}

		for (int i = 0; i < VulkanObjects->SwapChainImageCount; i++)
		{
			VkImageView Attachments[2] = {
				VulkanObjects->SwapChainImageViews[i],
				VulkanObjects->DepthImageView
			};

			VkFramebufferCreateInfo FramebufferInfo = { 0 };
			FramebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			FramebufferInfo.renderPass = VulkanObjects->RenderPass;
			FramebufferInfo.attachmentCount = ARRAYSIZE(Attachments);
			FramebufferInfo.pAttachments = Attachments;
			FramebufferInfo.width = VulkanObjects->SwapChainExtent.width;
			FramebufferInfo.height = VulkanObjects->SwapChainExtent.height;
			FramebufferInfo.layers = 1;

			THROW_ON_FAIL_VK(vkCreateFramebuffer(VulkanObjects->Device, &FramebufferInfo, NULL, &VulkanObjects->SwapChainFramebuffers[i]));
		}

		break;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProcW(Window, message, wParam, lParam);
	}
	return 0;
}
