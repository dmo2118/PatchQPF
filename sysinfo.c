#include "common.h"

#include <windows.h>
#include <tchar.h>

int WINAPI _tWinMain(HINSTANCE instance, HINSTANCE prev_instance, _TCHAR *cmd_line, int show_cmd)
{
	_TCHAR text[128];
	LARGE_INTEGER frequency;
	const _TCHAR *error = _T("");
	BOOL result = QueryPerformanceFrequency(&frequency);
	if(!result)
	{
		frequency.QuadPart = GetLastError();
		error = _T("Error ");
	}
	_sntprintf(
		text,
		arraysize(text),
		_T("QueryPerformanceFrequency() = %s%I64u\n\nCtrl-C copies this to the clipboard."),
		error,
		frequency.QuadPart);
	MessageBox(NULL, text, TEXT("PatchQPF System Info"), MB_ICONINFORMATION | MB_OK);
	return 0;
}
