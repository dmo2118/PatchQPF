#include "patchqpf.h"

#include "common.h"

#include <psapi.h>

/*
EnumProcessModules (PSAPI) -> Windows NT 4.
CreateRemoteThread -> Windows NT
*/

/* 
Usage: rundll32.exe pqpf????.dll,apply process_id
No UI, because we're normally running as a subprocess for a Windows service.
*/

/*
From MSDN QueryPerformanceCounter: "On systems that run Windows XP or later, the function will always succeed and will thus
never return zero." But we can run on Windows 2000 if we really need to.
*/

HANDLE _heap;

/* &__ImageBase == GetModuleHandle(NULL) on Windows NT. */
extern const IMAGE_DOS_HEADER __ImageBase;
HINSTANCE _instance;

LARGE_INTEGER _frequency;
BOOL _qpf_result;
DWORD _qpf_error;

static BOOL WINAPI _qpf_fast(LARGE_INTEGER *frequency)
{
	if(!_qpf_result)
		SetLastError(_qpf_error);
	else
		*frequency = _frequency;
	return _qpf_result;
}

static DWORD _enum_modules(HANDLE process, HMODULE **modules, DWORD *modules_size)
{
	*modules = NULL;
	*modules_size = 0;

	/* Tool Help would also work here, but that doesn't support NT 4. */

	for(;;)
	{
		DWORD old_size = *modules_size;
		/* Needs PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, according to MSDN example code. */
		if(!EnumProcessModules(process, *modules, old_size, modules_size))
		{
			DWORD result = _last_error();
			HeapFree(_heap, 0, *modules);
			return result;
		}

		if(old_size >= *modules_size)
			break;

		free(*modules);
		*modules = HeapAlloc(_heap, 0, *modules_size);
		if(!*modules)
			return ERROR_NOT_ENOUGH_MEMORY;
	}

	assert(!(*modules_size % sizeof(HMODULE)));
	*modules_size /= sizeof(HMODULE);
	return ERROR_SUCCESS;
}

static void *_rva(IMAGE_DOS_HEADER *dos_header, ULONG_PTR rva)
{
	return (BYTE *)dos_header + rva;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void *reserved)
{
	switch(reason)
	{
	case DLL_PROCESS_ATTACH:
		_instance = instance;

		_heap = GetProcessHeap();
		if(!_heap)
		{
			/* Implicitly propagate last error code. */
			return FALSE;
		}

		/* We can hook in DllMain, because kernel32.dll is guaranteed to be already loaded. With other DLLs, this isn't safe. */
		_qpf_result = QueryPerformanceFrequency(&_frequency);
		if(!_qpf_result)
			_qpf_error = GetLastError();

#if 0
/* #ifdef _M_IX86 */
		// TODO: How do we detect hotpatched executables, anyway?

		{
			// TODO: Other CPUs.
			WORD *old_qpf = (WORD *)(BOOL (WINAPI *)())QueryPerformanceFrequency;
			WORD pad = *old_qpf;

			const WORD mov_edi_edi = 0xFF8B, jmp_n5 = 0xF9EB;

			if(pad == mov_edi_edi || pad == jmp_n5)
			{
				DWORD old_protect;
				VirtualProtect((BYTE *)old_qpf - 5, 7, PAGE_EXECUTE_READWRITE, &old_protect);

				// TODO: kernel32.dll hotpatching is only available for x86-32.
				((BYTE *)old_qpf)[-5] = 0xE9; // JMP w/ full displacement
				((DWORD *)old_qpf)[-1] = (DWORD)_qpf_fast - (DWORD)old_qpf;
				*old_qpf = jmp_n5;

				VirtualProtect((BYTE *)old_qpf - 5, 7, old_protect, &old_protect);
			}
		}
#endif

		{
			HMODULE *modules;
			DWORD modules_size, i;

			// TODO: Error handling
			_enum_modules(GetCurrentProcess(), &modules, &modules_size);

			/* Windows by this point has already validated that these structures are OK. */
			for(i = 0; i != modules_size; ++i)
			{
				IMAGE_DOS_HEADER *hdr = (IMAGE_DOS_HEADER *)modules[i];
				if(hdr->e_magic == IMAGE_DOS_SIGNATURE)
				{
					IMAGE_NT_HEADERS *nt_header = (IMAGE_NT_HEADERS *)_rva(hdr, hdr->e_lfanew);
					if(
						nt_header->Signature == IMAGE_NT_SIGNATURE && 
						nt_header->FileHeader.SizeOfOptionalHeader == sizeof(IMAGE_OPTIONAL_HEADER) &&
						nt_header->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress)
					{
						IMAGE_IMPORT_DESCRIPTOR *import_desc = (IMAGE_IMPORT_DESCRIPTOR *)_rva(
							hdr, 
							nt_header->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);

						while(import_desc->Name)
						{
							CHAR *name = _rva(hdr, import_desc->Name);
							if(!lstrcmpiA(name, "kernel32.dll"))
							{
								SIZE_T import = 0;
								IMAGE_THUNK_DATA *hint_name = (IMAGE_THUNK_DATA *)_rva(
									hdr, 
									import_desc->OriginalFirstThunk);

								IMAGE_THUNK_DATA *thunk = (IMAGE_THUNK_DATA *)_rva(hdr, import_desc->FirstThunk);

								while(hint_name[import].u1.AddressOfData)
								{
									IMAGE_IMPORT_BY_NAME *import_name = (IMAGE_IMPORT_BY_NAME *)_rva(
										hdr, 
										hint_name[import].u1.AddressOfData);

									if(!lstrcmpA((CHAR *)import_name->Name, "QueryPerformanceFrequency"))
									{
										DWORD old_protect;
										if(VirtualProtect(
											&thunk[import], 
											sizeof(thunk[import]), 
											PAGE_EXECUTE_READWRITE, 
											&old_protect))
										{
											thunk[import].u1.Function = (ptrdiff_t)_qpf_fast;
											VirtualProtect(&thunk[import], sizeof(thunk[import]), old_protect, &old_protect);
										}
									}

									++import;
								}
							}

							++import_desc;
						}
					}
				}
			}

			HeapFree(_heap, 0, modules);
		}

#ifndef NDEBUG
		{
			LARGE_INTEGER frequency;
			QueryPerformanceFrequency(&frequency);
		}
#endif

		break;
	}

	return TRUE;
}

static BOOL _machine_compatible(WORD ext_machine)
{
	const WORD machine = ((const IMAGE_NT_HEADERS *)((const BYTE *)&__ImageBase + __ImageBase.e_lfanew))->FileHeader.Machine;
	size_t i = 0;
	for(i = 0; i != arraysize(patchqpf_compat_map); ++i)
	{
		if(patchqpf_compat_map[i].machine == machine)
		{
			size_t j;
			for(j = 0; j != patchqpf_compat_map[i].compat_size; ++j)
			{
				if(ext_machine == patchqpf_compat_map[i].compat[j])
					return TRUE;
			}

			break;
		}
	}

	return FALSE;
}

#if 0
#ifdef _M_IX86
	return file_machine == IMAGE_FILE_MACHINE_I386;
#elif defined _M_MRX000
	return FALSE
#	if _M_MRX000 <= 10000
			|| file_machine == IMAGE_FILE_MACHINE_R10000 /* MIPS IV */
#	endif
#	if _M_MRX000 < 5000
			|| file_machine == IMAGE_FILE_MACHINE_R4000 /* MIPS III */
#	endif
#	if _M_MRX000 < 4000
		file_machine == IMAGE_FILE_MACHINE_R3000 || file_machine == IMAGE_FILE_MACHINE_MIPSFPU /* MIPS I */
#	endif
		;
#elif defined _M_ALPHA
	return file_machine == IMAGE_FILE_MACHINE_ALPHA; /* 32-bit */
#elif defined _M_PPC
	return file_machine == IMAGE_FILE_MACHINE_POWERPC || file_machine == IMAGE_FILE_MACHINE_POWERPCFP;
#elif defined _M_IA64
	return file_machine == IMAGE_FILE_MACHINE_IA64;
#elif defined _M_X64
	return file_machine == IMAGE_FILE_MACHINE_AMD64;
#elif defined _M_ARM
	return file_machine == IMAGE_FILE_MACHINE_ARM;
#elif defined _M_ARM64
	return file_machine == IMAGE_FILE_MACHINE_ARM64;
#endif
	/* Windows also supported SuperH at one point. */
}
#endif

/*
Rundll32 is documented in <https://support.microsoft.com/en-us/kb/164787>.

Also, note: "What’s the guidance on when to use rundll32? Easy: Don’t use it"
https://blogs.msdn.microsoft.com/oldnewthing/20130104-00/?p=5643

Rundll32 is used here because it's smart enough to handle 32-bit and 64-bit DLLs.
*/
__declspec(dllexport) void applyW(HWND wnd, HINSTANCE instance, LPWSTR cmd_line, int cmd_show)
{
	DWORD result;
	DWORD process_id;
	struct patchqpf_process proc;

	while(isspace(*cmd_line))
		++cmd_line;

	{
		/* sizeof(DWORD) == sizeof(unsigned long) */
		wchar_t *end_ptr;
		process_id = wcstoul(cmd_line, &end_ptr, 0);

		while(isspace(*end_ptr))
			++end_ptr;

		if(*end_ptr)
		{
			patchqpf_write_error(ERROR_INVALID_PARAMETER);
			return;
		}
	}

	result = patchqpf_process_new(
		&proc, 
		process_id,
		PROCESS_CREATE_THREAD | 
			PROCESS_QUERY_INFORMATION | 
			PROCESS_VM_OPERATION | 
			PROCESS_VM_WRITE | 
			PROCESS_VM_READ);

	if(!result)
	{
		/*
		An alternative would be to embed to DLL into the patchqpf.exe image, and WriteProcessMemory() it into svchost.exe.
		But DLL dependencies wouldn't work without rebuilding the machinery of LoadLibrary. Plus, it wastes memory if we
		hook it repeatedly.

		Not sure if CreateFileMapping(INVALID_FILE_HANDLE, SEC_IMAGE) works.

		Arbitrary 32/64->32/64 injection requires some impressively gnarly code:
		https://github.com/OpenWireSec/metasploit/blob/master/external/source/meterpreter/source/common/arch/win/i386/base_inject.c
		https://github.com/OpenWireSec/metasploit/blob/master/external/source/shellcode/windows/x86/src/migrate/executex64.asm

		Once we've patched a function, we can't always unpatch it: something else can come along and apply a second patch on
		top of ours. We must stay in memory until process shutdown.

		The machine architectures for the hotpatch DLL, the out-of-process patch DLL, and svchost.exe must match.
		*/

		WORD ext_machine;
		result = patchqpf_machine_from_process(proc.process, &ext_machine);
		if(!result)
		{
			if(!_machine_compatible(ext_machine))
			{
				result = ERROR_EXE_MACHINE_TYPE_MISMATCH;
			}
			else
			{
				// HMODULE ext_kernel32;

				// TODO: Compensate for ASLR.
				// TODO: Errors!
				WCHAR dllpath[MAX_PATH];
				size_t dllpath_bytes = (GetModuleFileName(_instance, dllpath, MAX_PATH) + 1) * sizeof(WCHAR);

				void *ext_dllname = VirtualAllocEx(proc.process, NULL, dllpath_bytes, MEM_COMMIT, PAGE_READWRITE);
				if(!ext_dllname)
				{
					result = _last_error();
				}
				else
				{
					SIZE_T bytes_written;
					if(!WriteProcessMemory(proc.process, ext_dllname, dllpath, dllpath_bytes, &bytes_written))
					{
						result = _last_error();
					}
					else if(bytes_written != dllpath_bytes)
					{
						result = ERROR_PARTIAL_COPY;
					}
					else
					{
						HANDLE thread = CreateRemoteThread(
							proc.process, 
							NULL, 
							0, 
							(LPTHREAD_START_ROUTINE)LoadLibraryW, 
							ext_dllname, 
							0, 
							NULL);

						if(!thread)
						{
							result = _last_error();
						}
						else
						{
							DWORD exit_code;

							WaitForSingleObject(thread, INFINITE);

							GetExitCodeThread(thread, &exit_code);

							if(!exit_code)
							{
								/*
								THREAD_BASIC_INFORMATION thread_basic_information;
								NtQueryInformationThread(
									thread, 
									ThreadBasicInformation, 
									&thread_basic_information, 
									sizeof(thread_basic_information), 
									NULL);
								*/
								result = EVENT_E_FIRST; /* TODO: :( */
							}
							else
							{
								result = ERROR_SUCCESS;
							}

							CloseHandle(thread);
						}
					}

					VirtualFreeEx(proc.process, ext_dllname, 0, MEM_RELEASE);
				}
			}
		}

		patchqpf_process_delete(&proc);
	}

	if(result)
		patchqpf_write_error(result);
}
