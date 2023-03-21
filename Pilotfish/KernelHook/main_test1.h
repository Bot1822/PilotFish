#include <thread>
#include <map>
#include <iostream>
#include <process.h>

using std::ofstream;
using std::map;
using std::string;
using std::cout;
using std::endl;
using std::ifstream;
using std::pair;

ofstream hook_log; // hook��־
ofstream debug_log; // debug��־
ofstream kernel_log; // kernel��־
ofstream kernel_unhooked_log; // kernelδhook��־
ofstream kernel_avetime; // kernelƽ��ʱ����־
int kernel_count = 0; // kernel������ȫ�ֱ���
map<CUfunction, string> kernel_function; // kernel��Ӧ�ĺ�����
map<CUfunction, int> kernel_num; // kernel����
map<CUfunction, int> kernel_unhooked; // kernelδhook����
map<CUfunction, string> kernel_unregistered; // kernelδע�����
map<CUfunction, float> kernel_totaltime; // kernel��ʱ��
map<CUfunction, int> kernel_app; // kernel����app
map<CUfunction, double> kernel_time; // kernelʱ��
map<string, double> kernel_offline; // kernel����ʱ��

// ����һ������ָ������cudaLaunchKernelt������������cudaLaunchKernelһ��
typedef cudaError_t(CUDAAPI* cudaLaunchKernelt)(const void* func, dim3 gridDim, dim3 blockDim, void** args, size_t sharedMem, cudaStream_t stream);
// ����һ������ָ��ocudaLaunchKernel������ΪcudaLaunchKernelt
cudaLaunchKernelt ocudaLaunchKernel = NULL; // ocudaLaunchKernel���ڴ�ԭʼ��cudaLaunchKernel����
cudaLaunchKernelt cudaLaunchKerneladdr; // cudaLaunchKerneladdr���ڴ�cudaLaunchKernel�����ĵ�ַ

// ����һ������ָ������cuGetProcAddresst������������cuGetProcAddressһ��
typedef CUresult(CUDAAPI* cuGetProcAddresst) ( const char* symbol, void** pfn, int  cudaVersion, cuuint64_t flags);
// ����һ������ָ��ocuGetProcAddress������ΪcuGetProcAddresst
cuGetProcAddresst ocuGetProcAddress = NULL; // ocuGetProcAddress���ڴ�ԭʼ��cuGetProcAddress����
cuGetProcAddresst cuGetProcAddressaddr; // cuGetProcAddressaddr���ڴ�cuGetProcAddress�����ĵ�ַ

//CUresult CUDAAPI cuLaunchKernel
// ������������ָ������
typedef CUresult(CUDAAPI* cuLaunchKernelt)(CUfunction f, unsigned int gridDimX, unsigned int gridDimY, unsigned int gridDimZ, unsigned int blockDimX, unsigned int blockDimY, unsigned int blockDimZ, unsigned int sharedMemBytes, CUstream hStream, void** kernelParams, void** extra);
cuLaunchKernelt ocuLaunchKernel = NULL;
cuLaunchKernelt cuLaunchKerneladdr;

typedef CUresult(CUDAAPI* cuModuleGetFunctiont)(CUfunction* hfunc, CUmodule hmod, const char* name);
cuModuleGetFunctiont ocuModuleGetFunction = NULL;
cuModuleGetFunctiont cuModuleGetFunctionaddr;

// ����hook��cudaLaunchKernel����
cudaError_t CUDAAPI hkcudaLaunchKernel(const void* func, 
	dim3 gridDim, dim3 blockDim, void** args, size_t sharedMem, cudaStream_t stream)
{
	MessageBoxA(NULL, "hook!!! In cudaLaunchKernel", "hook!!! In cudaLaunchKernel", 3);
	hook_log << "hook!!! In cudaLaunchKernel" << endl;

	// �ж��Ƿ��Ѿ�hook������Ѿ�hook���������1
	if (kernel_num.find((CUfunction)func) != kernel_num.end())
	{
		kernel_num.find((CUfunction)func)->second += 1;
	}
	// ���δhook������kernel_num
	else
	{
		kernel_num.insert(pair<CUfunction, int>((CUfunction)func, 1));
	}

	// ����kernelʱ��
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

	// ����kernel��ʱ��
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

// ����hook��cuGetProcAddress����
CUresult CUDAAPI hkcuGetProcAddress(const char* symbol, void** pfn, int  cudaVersion, cuuint64_t flags)
{
	// // �ж��Ƿ���cuLaunchKernel����
	// else if (strcmp(symbol, "cuLaunchKernel") == 0)
	// {
	// 	// ��ȡcuLaunchKernel�����ĵ�ַ
	// 	cuLaunchKerneladdr = (cuLaunchKernelt)ocuGetProcAddress(symbol, pfn, cudaVersion, flags, symbolStatus);
	// 	// ��cuLaunchKernel�����ĵ�ַ��ֵ��ocuLaunchKernel
	// 	ocuLaunchKernel = cuLaunchKerneladdr;
	// 	// ��hkcudaLaunchKernel�����ĵ�ַ��ֵ��cuLaunchKerneladdr
	// 	cuLaunchKerneladdr = hkcudaLaunchKernel;
	// 	// ��cuLaunchKerneladdr��ֵ��pfn
	// 	*pfn = cuLaunchKerneladdr;
	// }
	hook_log << "hook!!! In cuGetProcAddress" << endl;
	hook_log << "symbol: " << symbol << endl;
	return ocuGetProcAddress(symbol, pfn, cudaVersion, flags);

}

/**
 * @brief ����hook��cuLaunchKernel����
 *
 */
CUresult CUDAAPI hkcuLaunchKernel(CUfunction f, unsigned int gridDimX, unsigned int gridDimY, unsigned int gridDimZ, unsigned int blockDimX, unsigned int blockDimY, unsigned int blockDimZ, unsigned int sharedMemBytes, CUstream hStream, void** kernelParams, void** extra)
{
	// �ж��Ƿ��Ѿ�hook������Ѿ�hook���������1
	if (kernel_num.find(f) != kernel_num.end())
	{
		kernel_num.find(f)->second += 1;
	}
	// ���δhook����δhook���У����δhook�����д��ڣ��������1���������Ϊ1
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

	// ��������cudaEvent_t���͵ı��������ڼ���kernelִ��ʱ��
	cudaEvent_t start, stop;
	float elapsedTime;

	cudaEventCreate(&start);
	cudaEventRecord(start, 0);

	//Do kernel activity here
	CUresult ret = ocuLaunchKernel(f, gridDimX, gridDimY, gridDimZ, blockDimX, blockDimY, blockDimZ, sharedMemBytes, hStream, kernelParams, extra);

	// ����kernel
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
// Dll��ڣ�����ΪDLL�ľ����DLL�ļ���ԭ���Լ�DLL�ļ��ز���
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
		// ��ֹ�߳̿����
		DisableThreadLibraryCalls(hInstance);

		// ��ȡDLL��·��������ΪDLL�ľ�������·���Ļ���������������С
		GetModuleFileNameA(hInstance, dlldir, 512);
		hook_log << "dll path: " << dlldir << endl;
		// ɾ���ļ�����ֻ����·��
		for (size_t i = strlen(dlldir); i > 0; i--) { if (dlldir[i] == '\\') { dlldir[i + 1] = 0; break; } }
		hook_log << "dll`s path: " << dlldir << endl;

		// ��nvcuda.dll�л�ȡcuLaunchKernel��cuModuleGetFunction��cuGetProcAddress�ĵ�ַ
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

		// ��ȡcudaLaunchKernel�ĵ�ַ
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

		
		// ��ʼ��MinHook
		if (MH_Initialize() != MH_OK) hook_log << "initialize hook failed" << endl; else hook_log << "initialize hook sucess" << endl;
		// �������ӣ���סcuLaunchKernel��cuModuleGetFunction����
		// ����1��Ҫ���ĺ�����ַ������2�����Ӻ�����ַ������3�����ڱ���ԭ������ַ��ָ��
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

		// ���ù��ӣ�ʹ������Ч������ΪҪ���ĺ�����ַ
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
