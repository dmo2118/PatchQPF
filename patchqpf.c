#include "patchqpf.h"

#include <process.h>
#include <tchar.h>

/* Requires Windows 2000, because of QueryServiceStatusEx. */

/*
We could embed this in svchost.exe in-process with Windows Update, Although:

"Note that Svchost.exe is reserved for use by the operating system and should not be used by non-Windows services. Instead,
developers should implement their own service hosting programs."
https://msdn.microsoft.com/en-us/library/windows/desktop/ms685967(v=vs.85).aspx

This *is* a special circumstance, however, so the above shouldn't matter too much.
*/

const TCHAR _service_name[] = L"PatchQPF";

struct _service
{
	SERVICE_STATUS_HANDLE _status_handle;
	HANDLE _event_shutdown;
	HANDLE _thread;
};

unsigned _stop(struct _service *self, DWORD result)
{
	SERVICE_STATUS status = 
	{
		SERVICE_WIN32_OWN_PROCESS,
		SERVICE_STOPPED, 
		0,
		result, 
		0, 
		0, 
		0
	};

	SetServiceStatus(self->_status_handle, &status);
	return result;
}

static unsigned __stdcall _service_thread(void *self_raw)
{
	struct _service *self = self_raw;

	DWORD process_id = 0;

	DWORD timeout = 
#ifdef NDEBUG
		3 * 60 * 1000;
#else
		0;
#endif

	SYSTEM_INFO system_info;
	GetNativeSystemInfo(&system_info);

	for(;;)
	{
		DWORD new_process_id = 0;
		SC_HANDLE sc_manager;

		/* 1. Pause for a moment. */
		if(WaitForSingleObject(self->_event_shutdown, timeout) == WAIT_OBJECT_0)
			break;
		timeout = 15 * 1000;

		/* 2. Check for the wuauserv PID. */
		sc_manager = OpenSCManager(NULL, SERVICES_ACTIVE_DATABASE, SC_MANAGER_CONNECT);
		if(!sc_manager)
		{
			return _stop(self, _last_error());
		}
		else
		{
			DWORD result;

			SC_HANDLE service = OpenService(sc_manager, L"wuauserv", SERVICE_QUERY_STATUS);
			if(!service)
			{
				result = _last_error();
				if(result == ERROR_SERVICE_DOES_NOT_EXIST)
				{
					/* wuauserv got deleted, but that's OK. Remain running in case it comes back. */
					result = ERROR_SUCCESS;
					new_process_id = 0;
				}
			}
			else
			{
				SERVICE_STATUS_PROCESS status;
				DWORD bytes_needed;
				if(!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, (BYTE *)&status, sizeof(status), &bytes_needed))
				{
					result = _last_error();
					if(result == ERROR_SHUTDOWN_IN_PROGRESS)
					{
						/* System shutdown? We should shut down, too. */
						result = ERROR_SUCCESS;
						new_process_id = 0;
						SetEvent(self->_event_shutdown);
					}
				}
				else
				{
					result = ERROR_SUCCESS;

					switch(status.dwCurrentState)
					{
					case SERVICE_RUNNING:
					case SERVICE_PAUSE_PENDING:
					case SERVICE_PAUSED:
					case SERVICE_CONTINUE_PENDING:
						new_process_id = status.dwProcessId;
						break;
					default:
						new_process_id = 0;
						break;
					}
				}

				CloseServiceHandle(service);
			}

			CloseServiceHandle(sc_manager);

			if(result)
				return _stop(self, result);
		}

		/* 3. If there's a new PID, hook the process. */
		if(new_process_id != process_id)
		{
			struct patchqpf_process proc;
			DWORD result;
			
			process_id = new_process_id;

			result = patchqpf_process_new(&proc, process_id, PROCESS_QUERY_INFORMATION | PROCESS_VM_READ);
			if(!result)
			{
				WORD machine;
				result = patchqpf_machine_from_process(proc.process, &machine); /* TODO: This doesn't work with 32-bit reading 64-bit. */

				if(result == ERROR_PARTIAL_COPY)
				{
					/* TODO: GetProcAddress & LoadLibrary for this. */
					BOOL wow64;
					if(IsWow64Process(proc.process, &wow64) && !wow64)
					{
						static const WORD machine_map[] =
						{
							IMAGE_FILE_MACHINE_I386, 
							IMAGE_FILE_MACHINE_R4000,
							IMAGE_FILE_MACHINE_ALPHA,
							IMAGE_FILE_MACHINE_POWERPC,
							IMAGE_FILE_MACHINE_SH3,
							IMAGE_FILE_MACHINE_ARM,
							IMAGE_FILE_MACHINE_IA64,
							IMAGE_FILE_MACHINE_ALPHA64,
							IMAGE_FILE_MACHINE_UNKNOWN, // MSIL
							IMAGE_FILE_MACHINE_AMD64,
							IMAGE_FILE_MACHINE_I386,
						};

						if(system_info.wProcessorArchitecture < arraysize(machine_map))
						{
							machine = machine_map[system_info.wProcessorArchitecture];
							if(machine)
								result = ERROR_SUCCESS;
						}					
					}
				}

				if(!result)
				{
					size_t i = 0;
					for(;;)
					{
						if(i == arraysize(patchqpf_compat_map))
						{
							result = ERROR_EXE_MACHINE_TYPE_MISMATCH;
							break;
						}

						if(patchqpf_compat_map[i].machine == machine)
						{
							size_t j = 0;
							for(;;)
							{
								TCHAR dllname[13];

								if(j == patchqpf_compat_map[i].compat_size)
								{
									result = ERROR_FILE_NOT_FOUND;
									break;
								}

								wsprintf(dllname, L"pqpf%.4x.dll", patchqpf_compat_map[i].compat[j]);
								if(GetFileAttributes(dllname) != INVALID_FILE_ATTRIBUTES)
								{
									TCHAR cmd[64];
									STARTUPINFO si;
									PROCESS_INFORMATION pi;

									/* Rundll32 has the smarts to handle both 32 and 64-bit DLLs. */
									wsprintf(cmd, L"rundll32.exe %s,apply %d", dllname, process_id);

									ZeroMemory(&si, sizeof(si));
									si.cb = sizeof(si);
									if(CreateProcess(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
									{
										DWORD exit_code;
										WaitForSingleObject(pi.hProcess, INFINITE);
										GetExitCodeProcess(pi.hProcess, &exit_code);

										if(!exit_code)
										{
											result = ERROR_SUCCESS;
											break;
										}

										CloseHandle(pi.hProcess);
										CloseHandle(pi.hThread);
									}
								}

								++j;
							}

							break;
						}

						++i;
					}
				}

				patchqpf_process_delete(&proc);
			}

			if(result)
				return _stop(self, result);
		}
	}

	return _stop(self, ERROR_SUCCESS);
}

struct _service _self;

void WINAPI _handler(DWORD control)
{
	switch(control)
	{
	/* case SERVICE_CONTROL_SHUTDOWN: */
	case SERVICE_CONTROL_STOP:
		SetEvent(_self._event_shutdown);
		WaitForSingleObject(_self._thread, INFINITE);

		CloseHandle(_self._event_shutdown);
		CloseHandle(_self._thread);

		_self._event_shutdown = NULL;
		_self._thread = NULL;

		break;
	}
}

void WINAPI _service_main(DWORD argc, TCHAR **argv)
{
	SERVICE_STATUS status =
	{
		SERVICE_WIN32_OWN_PROCESS,
		SERVICE_RUNNING, 
		SERVICE_ACCEPT_STOP,
		ERROR_SUCCESS, 
		0, 
		0, 
		0
	};

	{
		TCHAR path[MAX_PATH];
		TCHAR *sep;

		GetModuleFileName(GetModuleHandle(NULL), path, arraysize(path));

		sep = _tcsrchr(path, '\\');
		if(sep)
		{
			sep[1] = 0;
			SetCurrentDirectory(path);
		}
	}
	
	_self._status_handle = RegisterServiceCtrlHandler(_service_name, _handler);
	_self._event_shutdown = CreateEvent(NULL, FALSE, FALSE, NULL);
	_self._thread = (HANDLE)_beginthreadex(NULL, 0, _service_thread, &_self, 0, NULL);

	SetServiceStatus(_self._status_handle, &status);
}

static DWORD _puts(const TCHAR *s, DWORD n)
{
	DWORD written = 0;
	WriteConsole(GetStdHandle(STD_OUTPUT_HANDLE), s, n, &written, NULL);
	return written;
}

int _tmain()
{
	static const SERVICE_TABLE_ENTRY service_table[] = 
	{
		{(TCHAR *)_service_name, _service_main}, 
		{NULL, NULL}
	};

	if(!StartServiceCtrlDispatcher(service_table))
	{
		TCHAR *message;
		DWORD error = _last_error();
		DWORD n = FormatMessage(
			FORMAT_MESSAGE_ALLOCATE_BUFFER |
				FORMAT_MESSAGE_IGNORE_INSERTS |
				FORMAT_MESSAGE_FROM_SYSTEM,
			NULL,
			error,
			0,
			(LPTSTR)&message, /* Older GCC: likely spurious type-punning warning here. */
			0,
			NULL);
		
		if(n)
		{
			_puts(message, n);
			LocalFree(message);
		}
		else
		{
			TCHAR text[11];
			_puts(text, wsprintf(text, (SHORT)error == error ? L"%d" : L"%.8X", error));
		}

		patchqpf_write_error(error);

		Sleep(5000);
	}

	return 0;
}
