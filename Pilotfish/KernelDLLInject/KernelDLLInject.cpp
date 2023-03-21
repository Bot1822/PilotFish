#include <iostream>
#include <Windows.h>
#include <cstdio>
#include <array>
#include <stdexcept>
#include <string>

using namespace std;

class VirtualMemory
{
private:
	LPVOID address;
public:
	const HANDLE process;
	const SIZE_T size;
	const DWORD protectFlag;
	explicit VirtualMemory(HANDLE hProcess, SIZE_T dwSize, DWORD flProtect) :
		process(hProcess), size(dwSize), protectFlag(flProtect)
	{
		address = VirtualAllocEx(process, NULL, size, MEM_COMMIT, protectFlag);
		if (address == NULL)
			throw runtime_error("Failed to allocate virtual memory!");
	}
	~VirtualMemory()
	{
		if (address != NULL)
			VirtualFreeEx(process, address, 0, MEM_RELEASE);
	}
	BOOL copyFromBuffer(LPCVOID buffer, SIZE_T size)
	{
		if (size > this->size)
			return FALSE;
		return WriteProcessMemory(process, address, buffer, size, NULL);
	}
	LPVOID getAddress()
	{
		return address;
	}
};

class ChildProcess
{
private:
	STARTUPINFOA si;
	PROCESS_INFORMATION pi;
public:
	explicit ChildProcess(LPSTR command, LPCSTR dir, DWORD creationFlags)
	{
		ZeroMemory(&si, sizeof(si));
		si.cb = sizeof(si);
		ZeroMemory(&pi, sizeof(pi));
		if (!CreateProcessA(NULL,
			command, NULL, NULL, FALSE, creationFlags, NULL, dir,
			&si, &pi))
		{
			throw runtime_error("Failed to create child process!");
		}
	}
	PROCESS_INFORMATION& getProcessInformation()
	{
		return pi;
	}
	~ChildProcess()
	{
		WaitForSingleObject(pi.hProcess, INFINITE);
		MessageBoxA(NULL, "Exit Children Process", "Exit", 3);
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);

	}
};

BOOL EnableDebugPriv()
{
	HANDLE   hToken;
	LUID   sedebugnameValue;
	TOKEN_PRIVILEGES   tkp;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
	{
		return   FALSE;
	}

	if (!LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &sedebugnameValue))
	{
		CloseHandle(hToken);
		return   FALSE;
	}
	tkp.PrivilegeCount = 1;
	tkp.Privileges[0].Luid = sedebugnameValue;
	tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

	if (!AdjustTokenPrivileges(hToken, FALSE, &tkp, sizeof(tkp), NULL, NULL))
	{
		return   FALSE;
	}
	CloseHandle(hToken);
	return TRUE;
}

FARPROC getLibraryProcAddress(LPCSTR libName, LPCSTR procName)
{
	auto dllModule = LoadLibraryA(libName);
	if (dllModule == NULL) {
		throw runtime_error("Unable to load library!");
	}
	auto procAddress = GetProcAddress(dllModule, procName);
	if (procAddress == NULL) {
		throw runtime_error("Unable to get proc address!");
	}
	return procAddress;
}

inline FARPROC getLoadLibraryAddress()
{
	return getLibraryProcAddress("kernel32.dll", "LoadLibraryA");
}

void injectWithRemoteThread(PROCESS_INFORMATION& pi, const char* dllPath)
{
	cout << "Allocating Memory in Child Process For dll path" << endl;
	//计算DLL路径名需要的内存空间
	const int bufferSize = strlen(dllPath) + 1;
	//使用VirtualAllocEx函数在Childprocess的内存地址空间分配DLL文件名缓冲区
	VirtualMemory dllPathMemory(pi.hProcess, bufferSize, PAGE_READWRITE);
	//使用WriteProcessMemory函数将DLL的路径名复制到Childprocess的内存空间
	dllPathMemory.copyFromBuffer(dllPath, bufferSize);
	cout << "Have written dll path to child process`s Virtual memory!" << endl;
	/*	LoadLibrary载入指定的动态链接库，并将它映射到当前进程使用的地址空间。一旦载入，即可访问库内保存的资源。
		GetProcAddress功能是检索指定的动态链接库(DLL)中的输出库函数地址。lpProcName参数能够识别DLL中的函数。
		CreateRemoteThread是一个Windows API函数，它能够创建一个在其它进程地址空间中运行的线程(也称:创建远程线程)。
		LoadLibraryA这个函数是在Kernel32.dll这个核心DLL里的，而这个DLL很特殊，不管对于哪个进程，Windows总是把它加载到相同的地址上去。
		因此你的进程中LoadLibraryA的地址和目标进程中LoadLibraryA的地址是相同的(其实，这个DLL里的所有函数都是如此)。至此，DLL注入结束了。
		*/
	cout << "Getting LoadLibraryA address" << endl;
	PTHREAD_START_ROUTINE startRoutine = (PTHREAD_START_ROUTINE)getLoadLibraryAddress();
	std::cout << "startRoutine: " << startRoutine << std::endl;

	cout << "Creating remote thread" << endl;
	HANDLE remoteThreadHandle = CreateRemoteThread(
		pi.hProcess, NULL, NULL, startRoutine, dllPathMemory.getAddress(), CREATE_SUSPENDED, NULL);
	if (remoteThreadHandle == NULL) {
		cout << "Failed to create remote thread!" << endl;
		return;
	}

	cout << "Resume remote thread to inject DLL" << endl;
	ResumeThread(remoteThreadHandle);
	WaitForSingleObject(remoteThreadHandle, INFINITE);
	CloseHandle(remoteThreadHandle);

	cout << "Resume Children Process`s main thread" << endl;
	ResumeThread(pi.hThread);
}


int main(int argc, char* argv[])
{
	//Sleep(10000);
	//return 0;
	if (!EnableDebugPriv()) {
		cout << "Failed to enable debug privileges" << endl;
		return -1;
	}

	// 启动训练文件？
	char cmd[1024];
	sprintf_s(cmd, "%s%s%s", argv[1], " ", argv[2]);
	//cout << cmd << endl;
	if (argc != 5)
	{
		cout << "参数数量不正确" << endl;
		MessageBoxA(0, "参数数量不正确", "error", 1);
		return 1;
	}
	std::cout << "Creating child process" << std::endl;
	ChildProcess process(cmd, argv[3], CREATE_SUSPENDED);
	std::cout << "Child process created with PID: " << process.getProcessInformation().dwProcessId << std::endl;
	std::cout << "Child process has been suspended" << std::endl;

	std::cout << "Injecting dll into child process" << std::endl;
	injectWithRemoteThread(process.getProcessInformation(), argv[4]);
	std::cout << "Successfully injected dll into child process" << std::endl;
	//injectWithRemoteThread(process.getProcessInformation(), "D:\\github\\Pilotfish\\Pilotfish\\KernelHook\\x64\\Debug\\KernelHook.dll");
	//injectWithRemoteThread(process.getProcessInformation(), "E:\\CloudGaming\\Pilotfish单机\\KernelHook\\x64\\Debug\\KernelHook.dll");
	//injectWithRemoteThread(process.getProcessInformation(), "E:\\CloudGaming\\Pilotfish单机\\exe\\KernelHook_profile_full.dll");
	//injectWithRemoteThread(process.getProcessInformation(), "E:\\CloudGaming\\Pilotfish单机\\exe\\KernelHook_profile.dll");
	//injectWithRemoteThread(process.getProcessInformation(), "E:\\CloudGaming\\Pilotfish单机\\exe\\KernelHook_co.dll");
	//injectWithRemoteThread(process.getProcessInformation(), "E:\\CloudGaming\\Pilotfish单机\\exe\\KernelHook_naive.dll");
	//injectWithRemoteThread(process.getProcessInformation(), "E:\\CloudGaming\\Pilotfish单机\\exe\\KernelHook_kc.dll");
	//injectWithRemoteThread(process.getProcessInformation(), "E:\\CloudGaming\\Pilotfish单机\\exe\\KernelHook_to.dll");
	//injectWithRemoteThread(process.getProcessInformation(), "D:\\github\\Pilotfish\\Pilotfish单机\\KernelHook\\x64\\Debug\\KernelHook.dll");

	return 0;
}