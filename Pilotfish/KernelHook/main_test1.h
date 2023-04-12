#include <thread>
#include <map>
#include <iostream>
#include <process.h>
#include <cuda.h>
#include <cuda_runtime.h>
#include <fstream>
#include <windows.h>
#include "Minhook/include/Minhook.h"

using std::ofstream;
using std::map;
using std::string;
using std::cout;
using std::endl;
using std::ifstream;
using std::pair;

ofstream hook_log; // hook日志
ofstream debug_log; // debug日志
ofstream kernel_log; // kernel日志
ofstream kernel_unhooked_log; // kernel未hook日志
ofstream kernel_avetime; // kernel平均时间日志
int kernel_count = 0; // kernel计数，全局变量
map<CUfunction, string> kernel_function; // kernel对应的函数名
map<CUfunction, int> kernel_num; // kernel计数
map<CUfunction, int> kernel_unhooked; // kernel未hook计数
map<CUfunction, string> kernel_unregistered; // kernel未注册计数
map<CUfunction, float> kernel_totaltime; // kernel总时间
map<CUfunction, int> kernel_app; // kernel所属app
map<CUfunction, double> kernel_time; // kernel时间
map<string, double> kernel_offline; // kernel离线时间

// 定义一个函数指针类型cudaLaunchKernelt，参数类型与cudaLaunchKernel一致
typedef cudaError_t(CUDAAPI* cudaLaunchKernelt)(const void* func, dim3 gridDim, dim3 blockDim, void** args, size_t sharedMem, cudaStream_t stream);
// 定义一个函数指针ocudaLaunchKernel，类型为cudaLaunchKernelt
cudaLaunchKernelt ocudaLaunchKernel = NULL; // ocudaLaunchKernel用于存原始的cudaLaunchKernel函数
cudaLaunchKernelt cudaLaunchKerneladdr; // cudaLaunchKerneladdr用于存cudaLaunchKernel函数的地址

// 定义一个函数指针类型cuGetProcAddresst，参数类型与cuGetProcAddress一致
typedef CUresult(CUDAAPI* cuGetProcAddresst) ( const char* symbol, void** pfn, int  cudaVersion, cuuint64_t flags);
// 定义一个函数指针ocuGetProcAddress，类型为cuGetProcAddresst
cuGetProcAddresst ocuGetProcAddress = NULL; // ocuGetProcAddress用于存原始的cuGetProcAddress函数
cuGetProcAddresst cuGetProcAddressaddr; // cuGetProcAddressaddr用于存cuGetProcAddress函数的地址

//CUresult CUDAAPI cuLaunchKernel
// 定义两个函数指针类型
typedef CUresult(CUDAAPI* cuLaunchKernelt)(CUfunction f, unsigned int gridDimX, unsigned int gridDimY, unsigned int gridDimZ, unsigned int blockDimX, unsigned int blockDimY, unsigned int blockDimZ, unsigned int sharedMemBytes, CUstream hStream, void** kernelParams, void** extra);
cuLaunchKernelt ocuLaunchKernel = NULL;
cuLaunchKernelt cuLaunchKerneladdr;

typedef CUresult(CUDAAPI* cuModuleGetFunctiont)(CUfunction* hfunc, CUmodule hmod, const char* name);
cuModuleGetFunctiont ocuModuleGetFunction = NULL;
cuModuleGetFunctiont cuModuleGetFunctionaddr;

// 用于hook的cudaLaunchKernel函数
cudaError_t CUDAAPI hkcudaLaunchKernel(const void* func, 
	dim3 gridDim, dim3 blockDim, void** args, size_t sharedMem, cudaStream_t stream)
{
	MessageBoxA(NULL, "hook!!! In cudaLaunchKernel", "hook!!! In cudaLaunchKernel", 3);
	hook_log << "hook!!! In cudaLaunchKernel" << endl;

	// 判断是否已经hook，如果已经hook，则计数加1
	if (kernel_num.find((CUfunction)func) != kernel_num.end())
	{
		kernel_num.find((CUfunction)func)->second += 1;
	}
	// 如果未hook，加入kernel_num
	else
	{
		kernel_num.insert(pair<CUfunction, int>((CUfunction)func, 1));
	}

	// 计算kernel时间
	cudaEvent_t start, end;
	cudaEventCreate(&start);
	cudaEventCreate(&end);
	cudaEventRecord(start, 0);
	cudaError_t ret = ocudaLaunchKernel(func, gridDim, blockDim, args, sharedMem, stream);
	cudaEventRecord(end, 0);
	cudaEventSynchronize(end);
	float time = 0;
	cudaEventElapsedTime(&time, start, end);
	cudaEventDestroy(start);
	cudaEventDestroy(end);

	// 计算kernel总时间
	if (kernel_totaltime.find((CUfunction)func) != kernel_totaltime.end())
	{
		kernel_totaltime.find((CUfunction)func)->second += time;
	}
	else
	{
		kernel_totaltime.insert(pair<CUfunction, float>((CUfunction)func, time));
	}

	return ret;
}

// 用于hook的cuGetProcAddress函数
CUresult CUDAAPI hkcuGetProcAddress(const char* symbol, void** pfn, int  cudaVersion, cuuint64_t flags)
{
	// // 判断是否是cuLaunchKernel函数
	// else if (strcmp(symbol, "cuLaunchKernel") == 0)
	// {
	// 	// 获取cuLaunchKernel函数的地址
	// 	cuLaunchKerneladdr = (cuLaunchKernelt)ocuGetProcAddress(symbol, pfn, cudaVersion, flags, symbolStatus);
	// 	// 将cuLaunchKernel函数的地址赋值给ocuLaunchKernel
	// 	ocuLaunchKernel = cuLaunchKerneladdr;
	// 	// 将hkcudaLaunchKernel函数的地址赋值给cuLaunchKerneladdr
	// 	cuLaunchKerneladdr = hkcudaLaunchKernel;
	// 	// 将cuLaunchKerneladdr赋值给pfn
	// 	*pfn = cuLaunchKerneladdr;
	// }
	hook_log << "hook!!! In cuGetProcAddress" << endl;
	hook_log << "symbol: " << symbol << endl;
	return ocuGetProcAddress(symbol, pfn, cudaVersion, flags);

}

/**
 * @brief 用于hook的cuLaunchKernel函数
 *
 */
CUresult CUDAAPI hkcuLaunchKernel(CUfunction f, unsigned int gridDimX, unsigned int gridDimY, unsigned int gridDimZ, unsigned int blockDimX, unsigned int blockDimY, unsigned int blockDimZ, unsigned int sharedMemBytes, CUstream hStream, void** kernelParams, void** extra)
{
	// 判断是否已经hook，如果已经hook，则计数加1
	if (kernel_num.find(f) != kernel_num.end())
	{
		kernel_num.find(f)->second += 1;
	}
	// 如果未hook，查未hook队列，如果未hook队列中存在，则计数加1，否则计数为1
	else
	{
		if (kernel_unhooked.find(f) != kernel_unhooked.end())
		{
			kernel_unhooked.find(f)->second += 1;
		}
		else
		{
			kernel_unhooked.insert(pair<CUfunction, int>(f, 1));
		}
	}

	/*if (kernel_time.find(f) == kernel_time.end())
	{
		hook_log << "can't find !!!!:   " << f << '\n' << kernel_function.find(f)->second << endl;
		if (kernel_unregistered.find(f) == kernel_unregistered.end())
			kernel_unregistered.insert(pair<CUfunction, string>(f, kernel_function.find(f)->second));
	}*/

	// 定义两个cudaEvent_t类型的变量，用于计算kernel执行时间
	cudaEvent_t start, stop;
	float elapsedTime;

	cudaEventCreate(&start);
	cudaEventRecord(start, 0);

	//Do kernel activity here
	CUresult ret = ocuLaunchKernel(f, gridDimX, gridDimY, gridDimZ, blockDimX, blockDimY, blockDimZ, sharedMemBytes, hStream, kernelParams, extra);

	// 计算kernel
	cudaEventCreate(&stop);
	cudaEventRecord(stop, 0);
	cudaEventSynchronize(stop);

	cudaEventElapsedTime(&elapsedTime, start, stop);

	if (kernel_totaltime.find(f) != kernel_totaltime.end())
	{
		kernel_totaltime.find(f)->second += elapsedTime;
	}
	else
	{
		kernel_totaltime.insert(pair<CUfunction, float>(f, elapsedTime));
	}
	return ret;
}

//load offline data
CUresult CUDAAPI hkcuModuleGetFunction(CUfunction* hfunc, CUmodule hmod, const char* name)
{
	string str(name);
	//kernel_function.insert(pair<CUfunction, string>(*hfunc, str));
	//kernel_num.insert(pair<CUfunction, int>(*hfunc, 0));
	/*if (kernel_offline.find(str) != kernel_offline.end())
	{
		kernel_time.insert(pair<CUfunction, double>(*hfunc, kernel_offline.find(str)->second));
	}*/

	CUresult ret = ocuModuleGetFunction(hfunc, hmod, name);
	kernel_function.insert(pair<CUfunction, string>(*hfunc, str));
	kernel_num.insert(pair<CUfunction, int>(*hfunc, 0));
	return ret;
}


FARPROC getLibraryProcAddress(LPCSTR libName, LPCSTR procName)
{
	auto dllModule = LoadLibraryA(libName);
	if (dllModule == NULL) {
		throw std::runtime_error("Unable to load library!");
	}
	else cout << "load library" << libName << "in" << dllModule << endl;
	auto procAddress = GetProcAddress(dllModule, procName);
	if (procAddress == NULL) {
		throw std::runtime_error("Unable to get proc address!");
	}
	return procAddress;
}

//=========================================================================================================================//
// Dll入口，参数为DLL的句柄，DLL的加载原因，以及DLL的加载参数
BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD fdwReason, LPVOID)
{
	DisableThreadLibraryCalls(hInstance);

	switch (fdwReason)
	{
	case DLL_PROCESS_ATTACH:
		timeBeginPeriod(1);
		MessageBoxA(0, "DLL_PROCESS_ATTACH", "step 1", 3);
		hook_log.open("D:\\CloudGaming\\Log\\hook_log.txt");
		debug_log.open("D:\\CloudGaming\\Log\\debug_log.txt");
		kernel_log.open("D:\\CloudGaming\\Log\\kernel_log.txt");
		kernel_unhooked_log.open("D:\\CloudGaming\\Log\\kernel_unhooked_log.txt");
		kernel_avetime.open(KERNELAVG2);

		//get_kernel_time(kernel_offline);

		if (hook_log)MessageBoxA(0, "TEST1 DLL injected", "step 4", 3);
		hook_log << "Test1 dll inject" << endl;
		// 禁止线程库调用
		DisableThreadLibraryCalls(hInstance);

		// 获取DLL的路径，参数为DLL的句柄，存放路径的缓冲区，缓冲区大小
		GetModuleFileNameA(hInstance, dlldir, 512);
		hook_log << "dll path: " << dlldir << endl;
		// 删除文件名，只保留路径
		for (size_t i = strlen(dlldir); i > 0; i--) { if (dlldir[i] == '\\') { dlldir[i + 1] = 0; break; } }
		hook_log << "dll`s path: " << dlldir << endl;

		// 从nvcuda.dll中获取cuLaunchKernel和cuModuleGetFunction和cuGetProcAddress的地址
		cuLaunchKerneladdr = (cuLaunchKernelt)getLibraryProcAddress("nvcuda.dll", "cuLaunchKernel");
		hook_log << "\nculaucnkernel addr " << cuLaunchKerneladdr << endl;
		cuModuleGetFunctionaddr = (cuModuleGetFunctiont)getLibraryProcAddress("nvcuda.dll", "cuModuleGetFunction");
		hook_log << "cuModuleGetFunction addr " << cuModuleGetFunctionaddr << endl;
		try {
			cuGetProcAddressaddr = (cuGetProcAddresst)getLibraryProcAddress("nvcuda.dll", "cuGetProcAddress");
		}
		catch (std::runtime_error& e)
		{
			hook_log << "runtime error:" << e.what() << endl;
			hook_log << "cuGetProcAddress can not be found in nvcuda.dll" << endl;
		}
		hook_log << "cuGetProcAddress addr " << cuGetProcAddressaddr << endl;

		// 获取cudaLaunchKernel的地址
		try {
			cudaLaunchKerneladdr = (cudaLaunchKernelt)getLibraryProcAddress("cudart64_110", "cudaLaunchKernel");
		}
		catch (std::runtime_error& e)
		{
			hook_log << "runtime error:" << e.what() << endl;
			hook_log << "cudaLaunchKernel can not be found in cudart64_110" << endl;
		}
		hook_log << "cudaLaunchKernel addr in cudart64_110" << cudaLaunchKerneladdr << endl;
		try {
			cudaLaunchKerneladdr = (cudaLaunchKernelt)getLibraryProcAddress("C:\\Users\\Martini\\.conda\\envs\\pytorch\\Library\\bin\\cudart64_110.dll", "cudaLaunchKernel");
		}
		catch (std::runtime_error& e)
		{
			hook_log << "runtime error:" << e.what() << endl;
			hook_log << "cudaLaunchKerneladdr can not be found in C:\\Users\\Martini\\.conda\\envs\\pytorch\\Library\\bin\\cudart64_110.dll" << endl;
		}
		hook_log << "Hook in C:\\Users\\Martini\\.conda\\envs\\pytorch\\Library\\bin\\cudart64_110.dll" << endl;
		hook_log << "CCCcudaLaunchKernel addr" << cudaLaunchKerneladdr << endl;

		
		// 初始化MinHook
		if (MH_Initialize() != MH_OK) hook_log << "initialize hook failed" << endl; else hook_log << "initialize hook sucess" << endl;
		// 创建钩子，钩住cuLaunchKernel和cuModuleGetFunction函数
		// 参数1：要钩的函数地址，参数2：钩子函数地址，参数3：用于保存原函数地址的指针
		if (MH_CreateHook((LPVOID)cuLaunchKerneladdr, hkcuLaunchKernel, (LPVOID*)&ocuLaunchKernel) != MH_OK) hook_log << "create cuLaunchKernel failed" << endl;
		else hook_log << "\ncreate cuLaunchKernel success" << endl;
		if (MH_CreateHook((LPVOID)cuModuleGetFunctionaddr, hkcuModuleGetFunction, (LPVOID*)&ocuModuleGetFunction) != MH_OK) hook_log << "create cuModuleGetFunction failed" << endl;
		else hook_log << "create cuModuleGetFunction success" << endl;
		if (MH_CreateHook((LPVOID)cuGetProcAddressaddr, hkcuGetProcAddress, (LPVOID*)&ocuGetProcAddress) != MH_OK) hook_log << "create cuGetProcAddress failed" << endl;
		else hook_log << "create cuGetProcAddress success" << endl;
		if (MH_CreateHook((LPVOID)cudaLaunchKerneladdr, hkcudaLaunchKernel, (LPVOID*)&ocudaLaunchKernel) != MH_OK) hook_log << "create cudaLaunchKernel failed" << endl;
		else hook_log << "create cudaLaunchKernel success" << endl;
		hook_log << "\nocuLaunchKernel: " << *ocuLaunchKernel << endl;
		hook_log << "ocuModuleGetFunction: " << *ocuModuleGetFunction << endl;
		hook_log << "ocuGetProcAddress: " << *ocuGetProcAddress << endl;
		hook_log << "ocudaLaunchKernel: " << *ocudaLaunchKernel << endl;

		// 启用钩子，使钩子生效，参数为要钩的函数地址
		if (MH_EnableHook((LPVOID)cuLaunchKerneladdr) != MH_OK) hook_log << "enable cuLaunchKernel failed" << endl;
		else hook_log << "enable cuLaunchKernel success" << endl;
		if (MH_EnableHook((LPVOID)cuModuleGetFunctionaddr) != MH_OK) hook_log << "enable cuModuleGetFunction failed" << endl;
		else hook_log << "enable cuModuleGetFunction success" << endl;
		if (MH_EnableHook((LPVOID)cuGetProcAddressaddr) != MH_OK) hook_log << "enable cuGetProcAddress failed" << endl;
		else hook_log << "enable cuGetProcAddress success" << endl;
		if (MH_EnableHook((LPVOID)cudaLaunchKerneladdr) != MH_OK) hook_log << "enable cudaLaunchKernel failed" << endl;
		else hook_log << "enable cudaLaunchKernel success" << endl;

		MessageBoxA(0, "DLL_PROCESS_ATTACH out", "step 2", 3);
		break;
	case DLL_PROCESS_DETACH: // A process unloads the DLL.
		timeEndPeriod(1);
		MessageBoxA(0, "DLL_PROCESS_DETACH in", "step 3", 3);
		//kernel num
		map<CUfunction, int>::iterator i;
		i = kernel_num.begin();
		map<CUfunction, string> ::iterator j;
		while (i != kernel_num.end())
		{
			if (i->second > 0 && kernel_function.find(i->first) != kernel_function.end())
			{
				kernel_log << kernel_function.find(i->first)->second << '\n' << i->second << endl;
			}
			i++;
		}

		//kernel time
		map<CUfunction, float>::iterator k;
		k = kernel_totaltime.begin();
		while (k != kernel_totaltime.end())
		{
			if (kernel_function.find(k->first) != kernel_function.end() && kernel_num.find(k->first) != kernel_num.end())
			{
				kernel_avetime << kernel_function.find(k->first)->second << '\n' << (k->second) / (kernel_num.find(k->first)->second) << endl;
			}
			if (kernel_unhooked.find(k->first) != kernel_unhooked.end())
			{
				kernel_unhooked_log << k->first << '\n' << (k->second) / (kernel_unhooked.find(k->first)->second) << '\n' << kernel_unhooked.find(k->first)->second << endl;
			}
			k++;
		}

		MessageBoxA(0, "DLL removed", "step 4", 3);

		if (MH_DisableHook((LPVOID)&cuLaunchKerneladdr) != MH_OK) { return 1; }
		if (MH_DisableHook((LPVOID)&cuModuleGetFunctionaddr) != MH_OK) { return 1; }
		if (MH_Uninitialize() != MH_OK) { return 1; }
		break;
	}

	return TRUE;
}
