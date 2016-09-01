#include "common.h"

#include <windows.h>
#include <tchar.h>

int WINAPI _tWinMain(HINSTANCE instance, HINSTANCE prev_instance, _TCHAR *cmd_line, int show_cmd)
{
	/*
	The SetupDi* functions can show you if the Device Manager lists an HPET, but apparently Windows mis-identifies both the
	ACPI PM clock and TSC as an HPET, according to an incredibly detailed (uncited) footnote in Wikipedia.
	https://en.wikipedia.org/wiki/High_Precision_Event_Timer#cite_note-7

	Expected values for QueryPerformanceFrequency():
	PM clock                        | 3579545         |
	CPU TSC                         | CPU clock speed |
	Intel 8253 PIT ("System timer") | 1193182         | ACPI\PNP0100 on ACPI systems, unlisted on a "Standard PC"
	*/

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
		_T("QueryPerformanceFrequency() = %s%I64u\n\nCtrl+C copies this to the clipboard."),
		error,
		frequency.QuadPart);
	MessageBox(NULL, text, TEXT("PatchQPF System Info"), MB_ICONINFORMATION | MB_OK);
	return 0;
}
