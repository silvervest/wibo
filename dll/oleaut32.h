#pragma once

#include "types.h"

namespace oleaut32 {

LPWSTR WINAPI SysAllocString(LPCWSTR psz);
void WINAPI SysFreeString(LPWSTR bstrString);
HRESULT WINAPI GetErrorInfo(DWORD dwReserved, GUEST_PTR *pperrinfo);

} // namespace oleaut32
