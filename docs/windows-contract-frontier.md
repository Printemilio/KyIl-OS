# Exact chrome_elf.dll static frontier

Baseline: 94; still absent: 0.
Resolved means compiled binding only; limitations and execution tests remain separate.

| DLL / symbol | Importer | Family | Native dependencies | Complexity | Priority | State |
|---|---|---|---|---|---:|---|
| `KERNEL32.dll!DiscardVirtualMemory` | chrome_elf.dll | memory | process_memory inventory, VMM, usercopy, native mapping/protection transactions | medium/large | 1 | resolved-partial-contract |
| `KERNEL32.dll!K32GetMappedFileNameW` | chrome_elf.dll | memory | process_memory inventory, VMM, usercopy, native mapping/protection transactions | medium/large | 1 | resolved-partial-contract |
| `KERNEL32.dll!K32QueryWorkingSetEx` | chrome_elf.dll | memory | process_memory inventory, VMM, usercopy, native mapping/protection transactions | medium/large | 1 | resolved-partial-contract |
| `KERNEL32.dll!ReadProcessMemory` | chrome_elf.dll | memory | process_memory inventory, VMM, usercopy, native mapping/protection transactions | medium/large | 1 | resolved-partial-contract |
| `KERNEL32.dll!VirtualProtectEx` | chrome_elf.dll | memory | process_memory inventory, VMM, usercopy, native mapping/protection transactions | medium/large | 1 | resolved-partial-contract |
| `KERNEL32.dll!VirtualQuery` | chrome_elf.dll | memory | process_memory inventory, VMM, usercopy, native mapping/protection transactions | medium/large | 1 | resolved-partial-contract |
| `KERNEL32.dll!WriteProcessMemory` | chrome_elf.dll | memory | process_memory inventory, VMM, usercopy, native mapping/protection transactions | medium/large | 1 | resolved-partial-contract |
| `KERNEL32.dll!RaiseException` | chrome_elf.dll | exception-unwind | PE .pdata/.xdata, safe self-memory copy, synchronous events/VEH | large | 2 | resolved-partial-contract |
| `KERNEL32.dll!RtlCaptureContext` | chrome_elf.dll | exception-unwind | PE .pdata/.xdata, safe self-memory copy, synchronous events/VEH | large | 2 | resolved-partial-contract |
| `KERNEL32.dll!RtlCaptureStackBackTrace` | chrome_elf.dll | exception-unwind | PE .pdata/.xdata, safe self-memory copy, synchronous events/VEH | large | 2 | resolved-partial-contract |
| `KERNEL32.dll!RtlLookupFunctionEntry` | chrome_elf.dll | exception-unwind | PE .pdata/.xdata, safe self-memory copy, synchronous events/VEH | large | 2 | resolved-partial-contract |
| `KERNEL32.dll!RtlPcToFileHeader` | chrome_elf.dll | exception-unwind | PE .pdata/.xdata, safe self-memory copy, synchronous events/VEH | large | 2 | resolved-partial-contract |
| `KERNEL32.dll!RtlUnwind` | chrome_elf.dll | exception-unwind | PE .pdata/.xdata, safe self-memory copy, synchronous events/VEH | large | 2 | resolved-partial-contract |
| `KERNEL32.dll!RtlUnwindEx` | chrome_elf.dll | exception-unwind | PE .pdata/.xdata, safe self-memory copy, synchronous events/VEH | large | 2 | resolved-partial-contract |
| `KERNEL32.dll!RtlVirtualUnwind` | chrome_elf.dll | exception-unwind | PE .pdata/.xdata, safe self-memory copy, synchronous events/VEH | large | 2 | resolved-partial-contract |
| `KERNEL32.dll!SetUnhandledExceptionFilter` | chrome_elf.dll | exception-unwind | PE .pdata/.xdata, safe self-memory copy, synchronous events/VEH | large | 2 | resolved-partial-contract |
| `KERNEL32.dll!UnhandledExceptionFilter` | chrome_elf.dll | exception-unwind | PE .pdata/.xdata, safe self-memory copy, synchronous events/VEH | large | 2 | resolved-partial-contract |
| `KERNEL32.dll!WerRegisterRuntimeExceptionModule` | chrome_elf.dll | exception-unwind | PE .pdata/.xdata, safe self-memory copy, synchronous events/VEH | large | 2 | resolved-partial-contract |
| `KERNEL32.dll!FindClose` | chrome_elf.dll | file-io | root capabilities, FD inventory, file locks/metadata, native IPC/IOCP | medium/large | 2 | resolved-partial-contract |
| `KERNEL32.dll!FindFirstFileExW` | chrome_elf.dll | file-io | root capabilities, FD inventory, file locks/metadata, native IPC/IOCP | medium/large | 2 | resolved-partial-contract |
| `KERNEL32.dll!FindNextFileW` | chrome_elf.dll | file-io | root capabilities, FD inventory, file locks/metadata, native IPC/IOCP | medium/large | 2 | resolved-partial-contract |
| `KERNEL32.dll!GetDriveTypeW` | chrome_elf.dll | file-io | root capabilities, FD inventory, file locks/metadata, native IPC/IOCP | medium/large | 2 | resolved-partial-contract |
| `KERNEL32.dll!GetFileInformationByHandle` | chrome_elf.dll | file-io | root capabilities, FD inventory, file locks/metadata, native IPC/IOCP | medium/large | 2 | resolved-partial-contract |
| `KERNEL32.dll!GetFileType` | chrome_elf.dll | file-io | root capabilities, FD inventory, file locks/metadata, native IPC/IOCP | medium/large | 2 | resolved-partial-contract |
| `KERNEL32.dll!GetFullPathNameW` | chrome_elf.dll | file-io | root capabilities, FD inventory, file locks/metadata, native IPC/IOCP | medium/large | 2 | resolved-partial-contract |
| `KERNEL32.dll!GetLongPathNameW` | chrome_elf.dll | file-io | root capabilities, FD inventory, file locks/metadata, native IPC/IOCP | medium/large | 2 | resolved-partial-contract |
| `KERNEL32.dll!LockFileEx` | chrome_elf.dll | file-io | root capabilities, FD inventory, file locks/metadata, native IPC/IOCP | medium/large | 2 | resolved-partial-contract |
| `KERNEL32.dll!PeekNamedPipe` | chrome_elf.dll | file-io | root capabilities, FD inventory, file locks/metadata, native IPC/IOCP | medium/large | 2 | resolved-partial-contract |
| `KERNEL32.dll!SetEndOfFile` | chrome_elf.dll | file-io | root capabilities, FD inventory, file locks/metadata, native IPC/IOCP | medium/large | 2 | resolved-partial-contract |
| `KERNEL32.dll!TransactNamedPipe` | chrome_elf.dll | file-io | root capabilities, FD inventory, file locks/metadata, native IPC/IOCP | medium/large | 2 | resolved-partial-contract |
| `KERNEL32.dll!UnlockFileEx` | chrome_elf.dll | file-io | root capabilities, FD inventory, file locks/metadata, native IPC/IOCP | medium/large | 2 | resolved-partial-contract |
| `ntdll.dll!NtCreateKey` | chrome_elf.dll | registry | confined prefix root, durable VexFS values, shared transaction/locks | large | 2 | resolved-partial-contract |
| `ntdll.dll!NtDeleteKey` | chrome_elf.dll | registry | confined prefix root, durable VexFS values, shared transaction/locks | large | 2 | resolved-partial-contract |
| `ntdll.dll!NtOpenKeyEx` | chrome_elf.dll | registry | confined prefix root, durable VexFS values, shared transaction/locks | large | 2 | resolved-partial-contract |
| `ntdll.dll!NtQueryValueKey` | chrome_elf.dll | registry | confined prefix root, durable VexFS values, shared transaction/locks | large | 2 | resolved-partial-contract |
| `ntdll.dll!NtSetValueKey` | chrome_elf.dll | registry | confined prefix root, durable VexFS values, shared transaction/locks | large | 2 | resolved-partial-contract |
| `ntdll.dll!RtlFormatCurrentUserKeyPath` | chrome_elf.dll | registry | confined prefix root, durable VexFS values, shared transaction/locks | large | 2 | resolved-partial-contract |
| `KERNEL32.dll!HeapLock` | chrome_elf.dll | heap | existing process heap, native allocation and object lifetime | medium | 3 | resolved-partial-contract |
| `KERNEL32.dll!HeapUnlock` | chrome_elf.dll | heap | existing process heap, native allocation and object lifetime | medium | 3 | resolved-partial-contract |
| `KERNEL32.dll!HeapWalk` | chrome_elf.dll | heap | existing process heap, native allocation and object lifetime | medium | 3 | resolved-partial-contract |
| `KERNEL32.dll!LocalFree` | chrome_elf.dll | heap | existing process heap, native allocation and object lifetime | medium | 3 | resolved-partial-contract |
| `KERNEL32.dll!SetInformationJobObject` | chrome_elf.dll | process-job | process capabilities, supervision/lifetime, loader CFG and native W^X | large | 3 | resolved-partial-contract |
| `KERNEL32.dll!SetProcessMitigationPolicy` | chrome_elf.dll | process-job | process capabilities, supervision/lifetime, loader CFG and native W^X | large | 3 | resolved-partial-contract |
| `KERNEL32.dll!InitializeSListHead` | chrome_elf.dll | synchronization | native futex/thread waits, typed handles, atomic queues | medium/large | 3 | resolved-partial-contract |
| `KERNEL32.dll!InterlockedFlushSList` | chrome_elf.dll | synchronization | native futex/thread waits, typed handles, atomic queues | medium/large | 3 | resolved-partial-contract |
| `KERNEL32.dll!UnregisterWaitEx` | chrome_elf.dll | synchronization | native futex/thread waits, typed handles, atomic queues | medium/large | 3 | resolved-partial-contract |
| `KERNEL32.dll!FlsAlloc` | chrome_elf.dll | thread-runtime | native contexts/accounting, per-thread TLS and stop/wake lifecycle | medium/large | 3 | resolved-partial-contract |
| `KERNEL32.dll!FlsFree` | chrome_elf.dll | thread-runtime | native contexts/accounting, per-thread TLS and stop/wake lifecycle | medium/large | 3 | resolved-partial-contract |
| `KERNEL32.dll!FlsGetValue` | chrome_elf.dll | thread-runtime | native contexts/accounting, per-thread TLS and stop/wake lifecycle | medium/large | 3 | resolved-partial-contract |
| `KERNEL32.dll!FlsSetValue` | chrome_elf.dll | thread-runtime | native contexts/accounting, per-thread TLS and stop/wake lifecycle | medium/large | 3 | resolved-partial-contract |
| `KERNEL32.dll!GetThreadContext` | chrome_elf.dll | thread-runtime | native contexts/accounting, per-thread TLS and stop/wake lifecycle | medium/large | 3 | resolved-partial-contract |
| `KERNEL32.dll!GetThreadPriority` | chrome_elf.dll | thread-runtime | native contexts/accounting, per-thread TLS and stop/wake lifecycle | medium/large | 3 | resolved-partial-contract |
| `KERNEL32.dll!GetThreadPriorityBoost` | chrome_elf.dll | thread-runtime | native contexts/accounting, per-thread TLS and stop/wake lifecycle | medium/large | 3 | resolved-partial-contract |
| `KERNEL32.dll!QueryThreadCycleTime` | chrome_elf.dll | thread-runtime | native contexts/accounting, per-thread TLS and stop/wake lifecycle | medium/large | 3 | resolved-partial-contract |
| `KERNEL32.dll!SetThreadInformation` | chrome_elf.dll | thread-runtime | native contexts/accounting, per-thread TLS and stop/wake lifecycle | medium/large | 3 | resolved-partial-contract |
| `KERNEL32.dll!SetThreadPriority` | chrome_elf.dll | thread-runtime | native contexts/accounting, per-thread TLS and stop/wake lifecycle | medium/large | 3 | resolved-partial-contract |
| `KERNEL32.dll!SetThreadPriorityBoost` | chrome_elf.dll | thread-runtime | native contexts/accounting, per-thread TLS and stop/wake lifecycle | medium/large | 3 | resolved-partial-contract |
| `KERNEL32.dll!SleepEx` | chrome_elf.dll | thread-runtime | native contexts/accounting, per-thread TLS and stop/wake lifecycle | medium/large | 3 | resolved-partial-contract |
| `KERNEL32.dll!SuspendThread` | chrome_elf.dll | thread-runtime | native contexts/accounting, per-thread TLS and stop/wake lifecycle | medium/large | 3 | resolved-partial-contract |
| `KERNEL32.dll!SwitchToThread` | chrome_elf.dll | thread-runtime | native contexts/accounting, per-thread TLS and stop/wake lifecycle | medium/large | 3 | resolved-partial-contract |
| `KERNEL32.dll!GetConsoleMode` | chrome_elf.dll | console | native console resource and process-owned file capabilities | medium | 4 | resolved-partial-contract |
| `KERNEL32.dll!GetConsoleOutputCP` | chrome_elf.dll | console | native console resource and process-owned file capabilities | medium | 4 | resolved-partial-contract |
| `KERNEL32.dll!ReadConsoleW` | chrome_elf.dll | console | native console resource and process-owned file capabilities | medium | 4 | resolved-partial-contract |
| `KERNEL32.dll!SetConsoleCtrlHandler` | chrome_elf.dll | console | native console resource and process-owned file capabilities | medium | 4 | resolved-partial-contract |
| `KERNEL32.dll!SetStdHandle` | chrome_elf.dll | console | native console resource and process-owned file capabilities | medium | 4 | resolved-partial-contract |
| `KERNEL32.dll!WriteConsoleW` | chrome_elf.dll | console | native console resource and process-owned file capabilities | medium | 4 | resolved-partial-contract |
| `KERNEL32.dll!GetUserDefaultLCID` | chrome_elf.dll | runtime | existing userspace loader and native resource contracts | medium/large | 4 | resolved-partial-contract |
| `KERNEL32.dll!GetUserDefaultLangID` | chrome_elf.dll | runtime | existing userspace loader and native resource contracts | medium/large | 4 | resolved-partial-contract |
| `KERNEL32.dll!GlobalMemoryStatusEx` | chrome_elf.dll | runtime | existing userspace loader and native resource contracts | medium/large | 4 | resolved-partial-contract |
| `KERNEL32.dll!K32GetModuleInformation` | chrome_elf.dll | runtime | existing userspace loader and native resource contracts | medium/large | 4 | resolved-partial-contract |
| `KERNEL32.dll!EnumSystemLocalesW` | chrome_elf.dll | system-nls | native BULK facts, online CPU metadata, RTC/clock, explicit locale data/policy | medium/large | 4 | resolved-partial-contract |
| `KERNEL32.dll!FormatMessageA` | chrome_elf.dll | system-nls | native BULK facts, online CPU metadata, RTC/clock, explicit locale data/policy | medium/large | 4 | resolved-partial-contract |
| `KERNEL32.dll!FormatMessageW` | chrome_elf.dll | system-nls | native BULK facts, online CPU metadata, RTC/clock, explicit locale data/policy | medium/large | 4 | resolved-partial-contract |
| `KERNEL32.dll!GetCPInfo` | chrome_elf.dll | system-nls | native BULK facts, online CPU metadata, RTC/clock, explicit locale data/policy | medium/large | 4 | resolved-partial-contract |
| `KERNEL32.dll!GetComputerNameExW` | chrome_elf.dll | system-nls | native BULK facts, online CPU metadata, RTC/clock, explicit locale data/policy | medium/large | 4 | resolved-partial-contract |
| `KERNEL32.dll!GetDateFormatW` | chrome_elf.dll | system-nls | native BULK facts, online CPU metadata, RTC/clock, explicit locale data/policy | medium/large | 4 | resolved-partial-contract |
| `KERNEL32.dll!GetLocalTime` | chrome_elf.dll | system-nls | native BULK facts, online CPU metadata, RTC/clock, explicit locale data/policy | medium/large | 4 | resolved-partial-contract |
| `KERNEL32.dll!GetLocaleInfoW` | chrome_elf.dll | system-nls | native BULK facts, online CPU metadata, RTC/clock, explicit locale data/policy | medium/large | 4 | resolved-partial-contract |
| `KERNEL32.dll!GetLogicalProcessorInformation` | chrome_elf.dll | system-nls | native BULK facts, online CPU metadata, RTC/clock, explicit locale data/policy | medium/large | 4 | resolved-partial-contract |
| `KERNEL32.dll!GetNativeSystemInfo` | chrome_elf.dll | system-nls | native BULK facts, online CPU metadata, RTC/clock, explicit locale data/policy | medium/large | 4 | resolved-partial-contract |
| `KERNEL32.dll!GetProductInfo` | chrome_elf.dll | system-nls | native BULK facts, online CPU metadata, RTC/clock, explicit locale data/policy | medium/large | 4 | resolved-partial-contract |
| `KERNEL32.dll!GetStringTypeW` | chrome_elf.dll | system-nls | native BULK facts, online CPU metadata, RTC/clock, explicit locale data/policy | medium/large | 4 | resolved-partial-contract |
| `KERNEL32.dll!GetSystemInfo` | chrome_elf.dll | system-nls | native BULK facts, online CPU metadata, RTC/clock, explicit locale data/policy | medium/large | 4 | resolved-partial-contract |
| `KERNEL32.dll!GetTimeFormatW` | chrome_elf.dll | system-nls | native BULK facts, online CPU metadata, RTC/clock, explicit locale data/policy | medium/large | 4 | resolved-partial-contract |
| `KERNEL32.dll!GetTimeZoneInformation` | chrome_elf.dll | system-nls | native BULK facts, online CPU metadata, RTC/clock, explicit locale data/policy | medium/large | 4 | resolved-partial-contract |
| `KERNEL32.dll!GetVersionExW` | chrome_elf.dll | system-nls | native BULK facts, online CPU metadata, RTC/clock, explicit locale data/policy | medium/large | 4 | resolved-partial-contract |
| `KERNEL32.dll!IsProcessorFeaturePresent` | chrome_elf.dll | system-nls | native BULK facts, online CPU metadata, RTC/clock, explicit locale data/policy | medium/large | 4 | resolved-partial-contract |
| `KERNEL32.dll!IsValidLocale` | chrome_elf.dll | system-nls | native BULK facts, online CPU metadata, RTC/clock, explicit locale data/policy | medium/large | 4 | resolved-partial-contract |
| `KERNEL32.dll!LCMapStringW` | chrome_elf.dll | system-nls | native BULK facts, online CPU metadata, RTC/clock, explicit locale data/policy | medium/large | 4 | resolved-partial-contract |
| `KERNEL32.dll!SystemTimeToTzSpecificLocalTime` | chrome_elf.dll | system-nls | native BULK facts, online CPU metadata, RTC/clock, explicit locale data/policy | medium/large | 4 | resolved-partial-contract |
| `KERNEL32.dll!VerSetConditionMask` | chrome_elf.dll | system-nls | native BULK facts, online CPU metadata, RTC/clock, explicit locale data/policy | medium/large | 4 | resolved-partial-contract |
| `KERNEL32.dll!VerifyVersionInfoW` | chrome_elf.dll | system-nls | native BULK facts, online CPU metadata, RTC/clock, explicit locale data/policy | medium/large | 4 | resolved-partial-contract |
| `KERNEL32.dll!lstrcmpiA` | chrome_elf.dll | system-nls | native BULK facts, online CPU metadata, RTC/clock, explicit locale data/policy | medium/large | 4 | resolved-partial-contract |
| `KERNEL32.dll!lstrcmpiW` | chrome_elf.dll | system-nls | native BULK facts, online CPU metadata, RTC/clock, explicit locale data/policy | medium/large | 4 | resolved-partial-contract |
