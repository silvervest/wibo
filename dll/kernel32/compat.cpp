#include "compat.h"

#include "common.h"
#include "context.h"
#include "errors.h"
#include "files.h"
#include "handles.h"
#include "heap.h"
#include "internal.h"
#include "libloaderapi.h"
#include "winnls.h"
#include "strutil.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {

constexpr DWORD FORMAT_MESSAGE_ALLOCATE_BUFFER = 0x00000100;
constexpr DWORD FORMAT_MESSAGE_FROM_SYSTEM = 0x00001000;

// Locale-agnostic comparison: bytes are compared as unsigned values and only ASCII letters are case-folded. The real
// lstrcmp(i) use the word-sort rules of the thread locale; the tools we target only compare identifiers and paths.
template <typename Char, typename Fold> int compareCStrings(const Char *a, const Char *b, Fold fold) {
	if (!a || !b) {
		return a == b ? 0 : (a ? 1 : -1);
	}
	for (;; ++a, ++b) {
		unsigned c = fold(*a);
		unsigned d = fold(*b);
		if (c != d) {
			return c < d ? -1 : 1;
		}
		if (c == 0) {
			return 0;
		}
	}
}

unsigned foldAscii(unsigned char c) { return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c; }
unsigned foldNone(unsigned char c) { return c; }
unsigned foldWide(uint16_t c) { return wcharToLower(c); }
unsigned foldWideNone(uint16_t c) { return c; }

} // namespace

namespace kernel32 {

int WINAPI lstrcmpA(LPCSTR lpString1, LPCSTR lpString2) {
	HOST_CONTEXT_GUARD();
	VERBOSE_LOG("lstrcmpA(%s, %s)\n", lpString1 ? lpString1 : "(null)", lpString2 ? lpString2 : "(null)");
	return compareCStrings(reinterpret_cast<const unsigned char *>(lpString1),
						   reinterpret_cast<const unsigned char *>(lpString2), foldNone);
}

int WINAPI lstrcmpiA(LPCSTR lpString1, LPCSTR lpString2) {
	HOST_CONTEXT_GUARD();
	VERBOSE_LOG("lstrcmpiA(%s, %s)\n", lpString1 ? lpString1 : "(null)", lpString2 ? lpString2 : "(null)");
	return compareCStrings(reinterpret_cast<const unsigned char *>(lpString1),
						   reinterpret_cast<const unsigned char *>(lpString2), foldAscii);
}

int WINAPI lstrcmpW(LPCWSTR lpString1, LPCWSTR lpString2) {
	HOST_CONTEXT_GUARD();
	VERBOSE_LOG("lstrcmpW(%p, %p)\n", lpString1, lpString2);
	return compareCStrings(lpString1, lpString2, foldWideNone);
}

int WINAPI lstrcmpiW(LPCWSTR lpString1, LPCWSTR lpString2) {
	HOST_CONTEXT_GUARD();
	VERBOSE_LOG("lstrcmpiW(%p, %p)\n", lpString1, lpString2);
	return compareCStrings(lpString1, lpString2, foldWide);
}

BOOL WINAPI CopyFileA(LPCSTR lpExistingFileName, LPCSTR lpNewFileName, BOOL bFailIfExists) {
	HOST_CONTEXT_GUARD();
	DEBUG_LOG("CopyFileA(%s, %s, %d)\n", lpExistingFileName ? lpExistingFileName : "(null)",
			  lpNewFileName ? lpNewFileName : "(null)", bFailIfExists);
	if (!lpExistingFileName || !lpNewFileName) {
		setLastError(ERROR_INVALID_PARAMETER);
		return FALSE;
	}
	std::string fromPath = files::pathFromWindows(lpExistingFileName).string();
	std::string toPath = files::pathFromWindows(lpNewFileName).string();

	int in = open(fromPath.c_str(), O_RDONLY | O_CLOEXEC);
	if (in < 0) {
		setLastErrorFromErrno();
		return FALSE;
	}
	struct stat st;
	if (fstat(in, &st) != 0 || S_ISDIR(st.st_mode)) {
		if (S_ISDIR(st.st_mode)) {
			errno = EISDIR;
		}
		setLastErrorFromErrno();
		close(in);
		return FALSE;
	}
	int flags = O_WRONLY | O_CREAT | O_CLOEXEC | (bFailIfExists ? O_EXCL : O_TRUNC);
	int out = open(toPath.c_str(), flags, st.st_mode & 0777);
	if (out < 0) {
		int err = errno;
		if (err == EEXIST) {
			setLastError(ERROR_FILE_EXISTS);
		} else {
			setLastErrorFromErrno();
		}
		close(in);
		return FALSE;
	}

	char buffer[64 * 1024];
	bool ok = true;
	for (;;) {
		ssize_t got = read(in, buffer, sizeof(buffer));
		if (got < 0) {
			if (errno == EINTR) {
				continue;
			}
			ok = false;
			break;
		}
		if (got == 0) {
			break;
		}
		for (ssize_t done = 0; done < got;) {
			ssize_t put = write(out, buffer + done, static_cast<size_t>(got - done));
			if (put < 0) {
				if (errno == EINTR) {
					continue;
				}
				ok = false;
				break;
			}
			done += put;
		}
		if (!ok) {
			break;
		}
	}
	if (!ok) {
		setLastErrorFromErrno();
	} else {
		// Like Windows, keep the source's last-write time so timestamp-driven tools don't see a fresh file.
		struct timespec times[2] = {st.st_atim, st.st_mtim};
		futimens(out, times);
	}
	close(in);
	if (close(out) != 0 && ok) {
		setLastErrorFromErrno();
		ok = false;
	}
	return ok ? TRUE : FALSE;
}

BOOL WINAPI CopyFileW(LPCWSTR lpExistingFileName, LPCWSTR lpNewFileName, BOOL bFailIfExists) {
	HOST_CONTEXT_GUARD();
	DEBUG_LOG("CopyFileW -> ");
	if (!lpExistingFileName || !lpNewFileName) {
		setLastError(ERROR_INVALID_PARAMETER);
		return FALSE;
	}
	std::string from = wideStringToString(lpExistingFileName);
	std::string to = wideStringToString(lpNewFileName);
	return CopyFileA(from.c_str(), to.c_str(), bFailIfExists);
}

HANDLE WINAPI OpenEventA(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCSTR lpName) {
	HOST_CONTEXT_GUARD();
	DEBUG_LOG("OpenEventA(%u, %d, %s)\n", dwDesiredAccess, bInheritHandle, lpName ? lpName : "(null)");
	if (!lpName) {
		setLastError(ERROR_INVALID_PARAMETER);
		return NO_HANDLE;
	}
	auto ev = wibo::g_namespace.getAs<EventObject>(stringToUtf16(lpName));
	if (!ev) {
		setLastError(ERROR_FILE_NOT_FOUND);
		return NO_HANDLE;
	}
	uint32_t handleFlags = bInheritHandle ? HANDLE_FLAG_INHERIT : 0;
	return wibo::handles().alloc(std::move(ev), EVENT_ALL_ACCESS, handleFlags);
}

HANDLE WINAPI OpenFileMappingA(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCSTR lpName) {
	HOST_CONTEXT_GUARD();
	DEBUG_LOG("OpenFileMappingA(%u, %d, %s)\n", dwDesiredAccess, bInheritHandle, lpName ? lpName : "(null)");
	(void)bInheritHandle;
	// CreateFileMapping doesn't register named mappings, so no mapping can exist under a name: always "not found".
	setLastError(lpName ? ERROR_FILE_NOT_FOUND : ERROR_INVALID_PARAMETER);
	return NO_HANDLE;
}

LCID WINAPI GetThreadLocale() {
	HOST_CONTEXT_GUARD();
	DEBUG_LOG("GetThreadLocale()\n");
	return GetUserDefaultLCID();
}

HMODULE WINAPI LoadLibraryExA(LPCSTR lpLibFileName, HANDLE hFile, DWORD dwFlags) {
	HOST_CONTEXT_GUARD();
	(void)hFile;
	// TODO: handle dwFlags properly (same as LoadLibraryExW)
	DEBUG_LOG("LoadLibraryExA(%x) -> ", dwFlags);
	return LoadLibraryA(lpLibFileName);
}

DWORD WINAPI FormatMessageW(DWORD dwFlags, LPCVOID lpSource, DWORD dwMessageId, DWORD dwLanguageId, LPWSTR lpBuffer,
							DWORD nSize, GUEST_PTR Arguments) {
	HOST_CONTEXT_GUARD();
	DEBUG_LOG("FormatMessageW(%x, %p, %u, %u, %p, %u, %x)\n", dwFlags, lpSource, dwMessageId, dwLanguageId, lpBuffer,
			  nSize, Arguments);
	(void)lpSource;
	(void)dwLanguageId;
	(void)Arguments;

	// Only FORMAT_MESSAGE_FROM_SYSTEM is supported. There's no Windows message table here, so produce a generic text.
	if (!(dwFlags & FORMAT_MESSAGE_FROM_SYSTEM) || !lpBuffer) {
		setLastError(ERROR_CALL_NOT_IMPLEMENTED);
		return 0;
	}
	char text[64];
	int length = std::snprintf(text, sizeof(text), "Win32 error %u.\r\n", dwMessageId);
	std::vector<uint16_t> wide = stringToWideString(text, static_cast<size_t>(length));
	size_t chars = static_cast<size_t>(length);
	if (dwFlags & FORMAT_MESSAGE_ALLOCATE_BUFFER) {
		auto *allocation = static_cast<uint16_t *>(wibo::heap::guestMalloc((chars + 1) * sizeof(uint16_t)));
		if (!allocation) {
			setLastError(ERROR_NOT_ENOUGH_MEMORY);
			return 0;
		}
		std::memcpy(allocation, wide.data(), chars * sizeof(uint16_t));
		allocation[chars] = 0;
		// With ALLOCATE_BUFFER, lpBuffer is really an LPWSTR* (a pointer to a 32-bit guest pointer).
		*reinterpret_cast<GUEST_PTR *>(lpBuffer) = toGuestPtr(allocation);
		return static_cast<DWORD>(chars);
	}
	if (nSize <= chars) {
		setLastError(ERROR_INSUFFICIENT_BUFFER);
		return 0;
	}
	std::memcpy(lpBuffer, wide.data(), chars * sizeof(uint16_t));
	lpBuffer[chars] = 0;
	return static_cast<DWORD>(chars);
}

} // namespace kernel32
