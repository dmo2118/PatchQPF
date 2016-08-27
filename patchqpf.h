#ifndef PATCHQPF_H
#define PATCHQPF_H

#include <assert.h>

#include <windows.h>
#include <psapi.h>

#include "common.h"

/*
The code in here gets compiled for both pqpfXXXX.dll and patchqpf.exe. This is in a .h rather than a .c because multiple CPU
types are involved.
*/

static __inline DWORD _last_error()
{
	DWORD result = GetLastError();
	assert(result);
	return result;
}

#if 0
static int patchqpf_error_str(HWND wnd, const TCHAR *message, UINT type)
{
	MessageBox(NULL, message, L"PatchQPF", type);
	return 1;
}

static int patchqpf_error_code(HWND wnd, DWORD error)
{
	/* Somewhat copy-pasted from elsewhere. */
	TCHAR *message;
	UINT type = error ? MB_ICONERROR : MB_ICONINFORMATION;

	if(FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
			FORMAT_MESSAGE_IGNORE_INSERTS |
			FORMAT_MESSAGE_FROM_SYSTEM |
			FORMAT_MESSAGE_MAX_WIDTH_MASK,
		NULL,
		error,
		0,
		(LPTSTR)&message, /* Older GCC: likely spurious type-punning warning here. */
		0,
		NULL))
	{
		patchqpf_error_str(wnd, message, type);
		LocalFree(message);
		return 1;
	}
	else
	{
		TCHAR text[11];
		wsprintf(text, (SHORT)error == error ? L"%d" : L"%.8X", error);
		return patchqpf_error_str(wnd, text, type);
	}
}

static int patchqpf_error_last(HWND wnd)
{
	return patchqpf_error_code(wnd, _last_error());
}
#endif

void patchqpf_write_error(DWORD error_code)
{
	HANDLE event_log = RegisterEventSource(NULL, TEXT("PatchQPF"));
	if(event_log)
	{
		ReportEvent(event_log, EVENTLOG_ERROR_TYPE, 0, error_code, NULL, 0, 0, NULL, NULL);
		DeregisterEventSource(event_log);
	}
}

/*
Revision   | Machine | Windows NT   | Windows CE
MIPS I     | R3000   |              |       2000
MIPS II    |         |              |       3000
MIPS III   | R4000   | [4000, 5000) |       4000
MIPS IV    | R10000  |        10000 |          ?
MIPS V     |         |              |       6000
MIPS16     |         |              |         16
*/

struct patchqpf_compat_list
{
	WORD machine;
	const WORD *compat;
	UINT_PTR compat_size;
};

static const WORD _machine_mrx000[] = {IMAGE_FILE_MACHINE_R3000, IMAGE_FILE_MACHINE_R4000, IMAGE_FILE_MACHINE_R10000};
static const WORD _machine_mipsfpu[] = {IMAGE_FILE_MACHINE_R3000, IMAGE_FILE_MACHINE_MIPSFPU};
static const WORD _machine_powerpc[] = {IMAGE_FILE_MACHINE_POWERPC, IMAGE_FILE_MACHINE_POWERPCFP};

/* Not present on older Windows SDKs. */
#define IMAGE_FILE_MACHINE_ARM64 0xaa64

/* Given an image file machine ID, what image types are compatible? */
/* Is 158 bytes is too big? This could be reduced with #ifdefs for the architecture-specific DLLs. */
static const struct patchqpf_compat_list patchqpf_compat_map[] = 
{
	{IMAGE_FILE_MACHINE_I386,  &patchqpf_compat_map[0].machine,  1}, 
	{IMAGE_FILE_MACHINE_R3000,     _machine_mrx000,  1}, 
	{IMAGE_FILE_MACHINE_R4000,     _machine_mrx000,  2}, 
	{IMAGE_FILE_MACHINE_R10000,    _machine_mrx000,  3}, 
	{IMAGE_FILE_MACHINE_MIPSFPU,   _machine_mipsfpu, arraysize(_machine_mipsfpu)}, 
	{IMAGE_FILE_MACHINE_ALPHA, &patchqpf_compat_map[5].machine,  1}, 
	{IMAGE_FILE_MACHINE_POWERPC,   _machine_powerpc, 1}, 
	{IMAGE_FILE_MACHINE_POWERPCFP, _machine_powerpc, 2}, 
	{IMAGE_FILE_MACHINE_IA64,  &patchqpf_compat_map[8].machine,  1}, 
	{IMAGE_FILE_MACHINE_AMD64, &patchqpf_compat_map[9].machine,  1}, 
	{IMAGE_FILE_MACHINE_ARM,   &patchqpf_compat_map[10].machine, 1}, 
	{IMAGE_FILE_MACHINE_ARM64, &patchqpf_compat_map[11].machine, 1}, 
};

static DWORD patchqpf_machine_from_process(HANDLE process, WORD *machine)
{
	/* process requires PROCESS_QUERY_INFORMATION | PROCESS_VM_READ. */

	/*
	This gives us the CPU architecture for the image loaded into the process. An alternate approach would check the value of the
	CS register: 0x33 -> 64-bit code, 0x22 -> 32-bit code.
	*/

	HMODULE module;
	IMAGE_DOS_HEADER dos_header;

	/* TODO: Replace ERROR_PARTIAL_COPY with ERROR_EXE_MACHINE_TYPE_MISMATCH...or not? */

	{
		DWORD bytes_read;

		if(!EnumProcessModules(process, &module, sizeof(module), &bytes_read))
			return _last_error();
		if(bytes_read < sizeof(module))
			return ERROR_EXE_MACHINE_TYPE_MISMATCH;
	}

	{
		struct
		{
			DWORD Signature;
			IMAGE_FILE_HEADER FileHeader;
		} nt_header;

		SIZE_T bytes_read;

		if(!ReadProcessMemory(process, module, &dos_header, sizeof(dos_header), &bytes_read))
			return _last_error();
		if(bytes_read < sizeof(dos_header))
			return ERROR_PARTIAL_COPY;

		if(dos_header.e_magic != IMAGE_DOS_SIGNATURE)
			return ERROR_BAD_FORMAT;

		if(!ReadProcessMemory(process, (BYTE *)module + dos_header.e_lfanew, &nt_header, sizeof(nt_header), &bytes_read))
			return _last_error();
		if(bytes_read < sizeof(nt_header))
			return ERROR_PARTIAL_COPY;

		if(nt_header.Signature != IMAGE_NT_SIGNATURE)
			return ERROR_BAD_FORMAT;

		*machine = nt_header.FileHeader.Machine;
	}

	return ERROR_SUCCESS;
}

struct patchqpf_process
{
	TOKEN_PRIVILEGES old_privs;
	HANDLE token;
	HANDLE process;
};

static void _patchqpf_process_restore_privs(struct patchqpf_process *self)
{
	if(self->token)
	{
		if(self->old_privs.PrivilegeCount)
			AdjustTokenPrivileges(self->token, FALSE, &self->old_privs, sizeof(self->old_privs), NULL, NULL);
		CloseHandle(self->token);
	}
}

static DWORD patchqpf_process_new(struct patchqpf_process *self, DWORD process_id, DWORD desired_access)
{
	/* TOKEN_PRIVILEGES ends with an ANYSIZE_ARRAY, where ANYSIZE_ARRAY == 1, which is what's needed here. */
	/* sizeof(TOKEN_PRIVILEGES) == sizeof(DWORD) + sizeof(LUID_AND_ATTRIBUTES) * 1 */
	TOKEN_PRIVILEGES tp;
	tp.PrivilegeCount = 1;
	if(!LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &tp.Privileges[0].Luid))
		return _last_error();

	tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

	if(!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &self->token))
	{
		return _last_error();
	}
	else
	{
		DWORD old_privs_length = sizeof(self->old_privs);
		if(!AdjustTokenPrivileges(self->token, FALSE, &tp, sizeof(tp), &self->old_privs, &old_privs_length))
		{
			CloseHandle(self->token);
			self->token = NULL;
			self->old_privs.PrivilegeCount = 0;
		}
	}

	self->process = OpenProcess(desired_access, FALSE, process_id);

	if(!self->process)
	{
		DWORD result = _last_error();
		_patchqpf_process_restore_privs(self);
		return result;
	}

	return ERROR_SUCCESS;
}

static void patchqpf_process_delete(struct patchqpf_process *self)
{
	CloseHandle(self->process);
	_patchqpf_process_restore_privs(self);
}

#endif
