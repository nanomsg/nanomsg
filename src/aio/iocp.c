#include "iocp.h"
#include "../utils/err.h"

#ifdef NN_HAVE_WINDOWS
typedef BOOL	 (WINAPI *api_CancelIoEx)(HANDLE hFile, LPOVERLAPPED lpOverlapped);
typedef BOOL	 (WINAPI *api_GetQueuedCompletionStatusEx)(HANDLE CompletionPort, LPOVERLAPPED_ENTRY lpCompletionPortEntries, ULONG ulCount, PULONG ulNumEntriesRemoved, DWORD dwMilliseconds, BOOL fAlertable);

/* Holds the pointer to our > Vista+ APIs*/
api_CancelIoEx ptr_api_CancelIoEx = NULL;
api_GetQueuedCompletionStatusEx ptr_api_GetQueuedCompletionStatusEx = NULL;

/* on Vista and up this is true*/
BOOL bIsVistaPlus = TRUE;
BOOL bOSQueried = FALSE;

BOOL nn_cancelioex(HANDLE hFile, LPOVERLAPPED lpOverlapped)
{
	/* Check our if the DLLs has the normal APIs if not set our own implementation*/
	if(ptr_api_CancelIoEx == NULL) {
		ptr_api_CancelIoEx = (api_CancelIoEx)GetProcAddress(GetModuleHandleA("Kernel32.dll"), "CancelIoEx");
	}

	nn_assert(ptr_api_CancelIoEx); //should be calling this in Vista+

	return ptr_api_CancelIoEx(hFile, lpOverlapped);
}

BOOL nn_getqueuedcompletionstatusex(HANDLE CompletionPort, LPOVERLAPPED_ENTRY lpCompletionPortEntries, ULONG ulCount, 
	PULONG ulNumEntriesRemoved, DWORD dwMilliseconds, BOOL fAlertable)
{
	HMODULE hLib = NULL;
	/* Check our if the DLLs has the normal APIs if not set our own implementation*/
	if(ptr_api_GetQueuedCompletionStatusEx == NULL) {
		ptr_api_GetQueuedCompletionStatusEx = (api_GetQueuedCompletionStatusEx)GetProcAddress(GetModuleHandleA("Kernel32.dll"), "GetQueuedCompletionStatusEx");
	}

	nn_assert(ptr_api_GetQueuedCompletionStatusEx); //should be calling this in Vista+

	return ptr_api_GetQueuedCompletionStatusEx(CompletionPort, lpCompletionPortEntries, ulCount, ulNumEntriesRemoved, dwMilliseconds, fAlertable);
}

BOOL nn_isvistaplus()
{
#ifdef SIM_XP
	return FALSE;
#else
	OSVERSIONINFO osvi;
	if(bOSQueried) return bIsVistaPlus;

	memset(&osvi, 0, sizeof(osvi));
	osvi.dwOSVersionInfoSize = sizeof(osvi);
	nn_assert(GetVersionEx(&osvi));	
	if(osvi.dwMajorVersion < 6) bIsVistaPlus = FALSE;
	bOSQueried = TRUE;

	return bIsVistaPlus;
#endif
}

#endif