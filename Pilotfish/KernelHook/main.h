#define _CRT_SECURE_NO_WARNINGS
#include <Windows.h>
#include <stdint.h>
#include <dxgi.h>
#include <d3d12.h>
#pragma comment(lib, "d3d12.lib")
#include "MinHook/include/MinHook.h"
#include <stdlib.h>
#include <stdio.h>
#include <thread>
#include <fstream>
#include "cuda.h"
#include "cuda_runtime.h"
#include <cudaTypedefs.h>
#pragma comment( lib, "winmm" )
#include <string>
#include <queue>
#define FULL // OFF; FULL; FPS; PF ; SOFT; CSPEED; NAIVE
#define KERNELAVG1 "D:\\CloudGaming\\OfflineData\\kernel_avetime.txt"
#define KERNELAVG2 "D:\\CloudGaming\\Log\\kernel_avetime.txt"
#define TESTLOGDIR "D:\\CloudGaming\\Log\\kernel_test.txt"
using namespace std;

char dlldir[320];
char *GetDirectoryFile(char *filename)
{
	static char path[320];
	strcpy_s(path, dlldir);
	strcat_s(path, filename);
	return path;
}

namespace dx12
{
	struct Status
	{
		enum Enum
		{
			UnknownError = -1,
			NotSupportedError = -2,
			ModuleNotFoundError = -3,

			Success = 0,
		};
	};

	struct RenderType
	{
		enum Enum
		{
			None,

			D3D12,
		};
	};

	/**
	 * @brief 好像只是为了获得D3D12的方法表，存在g_methodsTable中？
	 * 
	 * @param renderType 
	 * @return Status::Enum 
	 */
	Status::Enum init(RenderType::Enum renderType);

	RenderType::Enum getRenderType();

#if _M_X64
	uint64_t* getMethodsTable();
#elif defined _M_IX86
	uint32_t* getMethodsTable();
#endif
}


static dx12::RenderType::Enum g_renderType = dx12::RenderType::None;

#if _M_X64
static uint64_t* g_methodsTable = NULL;
#elif defined _M_IX86
static uint32_t* g_methodsTable = NULL;
#endif

// 总体来说，好像只是为了获得D3D12的方法表，存在g_methodsTable中？
dx12::Status::Enum dx12::init(RenderType::Enum _renderType)
{
	if (_renderType != RenderType::None)
	{
		if (_renderType == RenderType::D3D12)
		{
			WNDCLASSEX windowClass;
			windowClass.cbSize = sizeof(WNDCLASSEX);
			windowClass.style = CS_HREDRAW | CS_VREDRAW;
			windowClass.lpfnWndProc = DefWindowProc;
			windowClass.cbClsExtra = 0;
			windowClass.cbWndExtra = 0;
			windowClass.hInstance = GetModuleHandle(NULL);
			windowClass.hIcon = NULL;
			windowClass.hCursor = NULL;
			windowClass.hbrBackground = NULL;
			windowClass.lpszMenuName = NULL;
			windowClass.lpszClassName = TEXT("dx12");
			windowClass.hIconSm = NULL;

			// 用于注册窗口类，注册成功后，才能使用CreateWindow函数创建窗口
			::RegisterClassEx(&windowClass);

			HWND window = ::CreateWindow(windowClass.lpszClassName, TEXT("DirectX Window"), WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, NULL, NULL, windowClass.hInstance, NULL);


			if (_renderType == RenderType::D3D12)
			{
				// 初始化DXGI和D3D12的DLL模块
				// 如果没有找到DXGI或者D3D12的DLL模块，就销毁窗口和取消注册窗口类，返回ModuleNotFoundError错误
				HMODULE libDXGI;
				HMODULE libD3D12;
				if ((libDXGI = ::GetModuleHandle(TEXT("dxgi.dll"))) == NULL || (libD3D12 = ::GetModuleHandle(TEXT("d3d12.dll"))) == NULL)
				{
					::DestroyWindow(window);
					::UnregisterClass(windowClass.lpszClassName, windowClass.hInstance);
					return Status::ModuleNotFoundError;
				}

				// 用GetProcAddress找到CreateDXGIFactory函数，返回一个函数指针，如果返回NULL，说明找不到，直接返回；
				// 用::DestroyWindow销毁窗口，用::UnregisterClass销毁注册的窗口类，返回未知错误。
				void* CreateDXGIFactory;
				if ((CreateDXGIFactory = ::GetProcAddress(libDXGI, "CreateDXGIFactory")) == NULL)
				{
					::DestroyWindow(window);
					::UnregisterClass(windowClass.lpszClassName, windowClass.hInstance);
					return Status::UnknownError;
				}

				// 用CreateDXGIFactory函数创建IDXGIFactory对象，返回一个指针，如果返回小于0，说明创建失败，直接返回；
				IDXGIFactory* factory;
				if (((long(__stdcall*)(const IID&, void**))(CreateDXGIFactory))(__uuidof(IDXGIFactory), (void**)&factory) < 0)
				{
					::DestroyWindow(window);
					::UnregisterClass(windowClass.lpszClassName, windowClass.hInstance);
					return Status::UnknownError;
				}

				// 用IDXGIFactory对象的EnumAdapters函数枚举适配器，返回一个指针，如果返回DXGI_ERROR_NOT_FOUND，说明枚举失败，直接返回；
				IDXGIAdapter* adapter;
				if (factory->EnumAdapters(0, &adapter) == DXGI_ERROR_NOT_FOUND)
				{
					::DestroyWindow(window);
					::UnregisterClass(windowClass.lpszClassName, windowClass.hInstance);
					return Status::UnknownError;
				}

				// 用GetProcAddress找到D3D12CreateDevice函数，返回一个函数指针，如果返回NULL，说明找不到，直接返回；
				void* D3D12CreateDevice;
				if ((D3D12CreateDevice = ::GetProcAddress(libD3D12, "D3D12CreateDevice")) == NULL)
				{
					::DestroyWindow(window);
					::UnregisterClass(windowClass.lpszClassName, windowClass.hInstance);
					return Status::UnknownError;
				}

				// 用D3D12CreateDevice函数创建ID3D12Device对象，返回一个指针，如果返回小于0，说明创建失败，直接返回；
				ID3D12Device* device;
				if (((long(__stdcall*)(IUnknown*, D3D_FEATURE_LEVEL, const IID&, void**))(D3D12CreateDevice))(adapter, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void**)&device) < 0) //why is D3D_FEATURE_LEVEL_12_0 wrong?
				{
					::DestroyWindow(window);
					::UnregisterClass(windowClass.lpszClassName, windowClass.hInstance);
					return Status::UnknownError;
				}

				// 用ID3D12Device对象的CreateCommandQueue函数创建ID3D12CommandQueue对象，返回一个指针，如果返回小于0，说明创建失败，直接返回；
				D3D12_COMMAND_QUEUE_DESC queueDesc;
				queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
				queueDesc.Priority = 0;
				queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
				queueDesc.NodeMask = 0;

				ID3D12CommandQueue* commandQueue;
				if (device->CreateCommandQueue(&queueDesc, __uuidof(ID3D12CommandQueue), (void**)&commandQueue) < 0)
				{
					::DestroyWindow(window);
					::UnregisterClass(windowClass.lpszClassName, windowClass.hInstance);
					return Status::UnknownError;
				}

				// 用ID3D12Device对象的CreateCommandAllocator函数创建ID3D12CommandAllocator对象，返回一个指针，如果返回小于0，说明创建失败，直接返回；
				ID3D12CommandAllocator* commandAllocator;
				if (device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&commandAllocator) < 0)
				{
					::DestroyWindow(window);
					::UnregisterClass(windowClass.lpszClassName, windowClass.hInstance);
					return Status::UnknownError;
				}

				// 用ID3D12Device对象的CreateCommandList函数创建ID3D12GraphicsCommandList对象，返回一个指针，如果返回小于0，说明创建失败，直接返回；
				ID3D12GraphicsCommandList* commandList;
				if (device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator, NULL, __uuidof(ID3D12GraphicsCommandList), (void**)&commandList) < 0)
				{
					::DestroyWindow(window);
					::UnregisterClass(windowClass.lpszClassName, windowClass.hInstance);
					return Status::UnknownError;
				}

				// 用ID3D12GraphicsCommandList对象的
				DXGI_RATIONAL refreshRate;
				refreshRate.Numerator = 60;
				refreshRate.Denominator = 1;

				DXGI_MODE_DESC bufferDesc;
				bufferDesc.Width = 100;
				bufferDesc.Height = 100;
				bufferDesc.RefreshRate = refreshRate;
				bufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
				bufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
				bufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;

				DXGI_SAMPLE_DESC sampleDesc;
				sampleDesc.Count = 1;
				sampleDesc.Quality = 0;

				DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
				swapChainDesc.BufferDesc = bufferDesc;
				swapChainDesc.SampleDesc = sampleDesc;
				swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
				swapChainDesc.BufferCount = 2;
				swapChainDesc.OutputWindow = window;
				swapChainDesc.Windowed = 1;
				swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
				swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

				// 用IDXGIFactory对象的CreateSwapChain函数创建IDXGISwapChain对象，返回一个指针，如果返回小于0，说明创建失败，直接返回；
				IDXGISwapChain* swapChain;
				if (factory->CreateSwapChain(commandQueue, &swapChainDesc, &swapChain) < 0)
				{
					::DestroyWindow(window);
					::UnregisterClass(windowClass.lpszClassName, windowClass.hInstance);
					return Status::UnknownError;
				}

#if _M_X64
				// 开辟g_methodsTable指针空间，大小为150个uint64_t
				// 函数指针的大小为8个字节，150个uint64_t的大小为1200个字节
				// 存放ID3D12Device对象的前44个函数指针，ID3D12CommandQueue对象的前19个函数指针，ID3D12CommandAllocator对象的前9个函数指针，ID3D12GraphicsCommandList对象的前60个函数指针，IDXGISwapChain对象的前18个函数指针
				// 疑问：这个数量是怎么来的？是否会根据版本的不同而不同？
				g_methodsTable = (uint64_t*)::calloc(150, sizeof(uint64_t));
				memcpy(g_methodsTable, *(uint64_t**)device, 44 * sizeof(uint64_t));
				memcpy(g_methodsTable + 44, *(uint64_t**)commandQueue, 19 * sizeof(uint64_t));
				memcpy(g_methodsTable + 44 + 19, *(uint64_t**)commandAllocator, 9 * sizeof(uint64_t));
				memcpy(g_methodsTable + 44 + 19 + 9, *(uint64_t**)commandList, 60 * sizeof(uint64_t));
				memcpy(g_methodsTable + 44 + 19 + 9 + 60, *(uint64_t**)swapChain, 18 * sizeof(uint64_t));
#elif defined _M_IX86
				g_methodsTable = (uint32_t*)::calloc(150, sizeof(uint32_t));
				memcpy(g_methodsTable, *(uint32_t**)device, 44 * sizeof(uint32_t));
				memcpy(g_methodsTable + 44, *(uint32_t**)commandQueue, 19 * sizeof(uint32_t));
				memcpy(g_methodsTable + 44 + 19, *(uint32_t**)commandAllocator, 9 * sizeof(uint32_t));
				memcpy(g_methodsTable + 44 + 19 + 9, *(uint32_t**)commandList, 60 * sizeof(uint32_t));
				memcpy(g_methodsTable + 44 + 19 + 9 + 60, *(uint32_t**)swapChain, 18 * sizeof(uint32_t));
#endif

				device->Release();
				device = NULL;

				commandQueue->Release();
				commandQueue = NULL;

				commandAllocator->Release();
				commandAllocator = NULL;

				commandList->Release();
				commandList = NULL;

				swapChain->Release();
				swapChain = NULL;

				::DestroyWindow(window);
				::UnregisterClass(windowClass.lpszClassName, windowClass.hInstance);

				g_renderType = RenderType::D3D12;

				return Status::Success;
			}

			return Status::NotSupportedError;
		}

	}

	return Status::Success;
}


// 用于获取渲染类型
dx12::RenderType::Enum dx12::getRenderType()
{
	return g_renderType;
}

#if defined _M_X64
// 用于获取g_methodsTable指针
uint64_t* dx12::getMethodsTable()
{
	return g_methodsTable;
}
#elif defined _M_IX86
uint32_t* dx12::getMethodsTable()
{
	return g_methodsTable;
}
#endif

