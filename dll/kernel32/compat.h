#pragma once

#include "types.h"

// Functions needed by the Microsoft Visual C++ 7.1 toolchain shipped with the Xbox XDK
// (CL.Exe, C1xx.Dll, Link.Exe, Lib.Exe, MSPDB60/71.Dll) that don't have a more specific home.

namespace kernel32 {

int WINAPI lstrcmpA(LPCSTR lpString1, LPCSTR lpString2);
int WINAPI lstrcmpiA(LPCSTR lpString1, LPCSTR lpString2);
int WINAPI lstrcmpW(LPCWSTR lpString1, LPCWSTR lpString2);
int WINAPI lstrcmpiW(LPCWSTR lpString1, LPCWSTR lpString2);

BOOL WINAPI CopyFileA(LPCSTR lpExistingFileName, LPCSTR lpNewFileName, BOOL bFailIfExists);
BOOL WINAPI CopyFileW(LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName, BOOL bFailIfExists);

HANDLE WINAPI OpenEventA(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCSTR lpName);
HANDLE WINAPI OpenFileMappingA(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCSTR lpName);

LCID WINAPI GetThreadLocale();
HMODULE WINAPI LoadLibraryExA(LPCSTR lpLibFileName, HANDLE hFile, DWORD dwFlags);
DWORD WINAPI FormatMessageW(DWORD dwFlags, LPCVOID lpSource, DWORD dwMessageId, DWORD dwLanguageId, LPWSTR lpBuffer,
							DWORD nSize, GUEST_PTR Arguments);

} // namespace kernel32
