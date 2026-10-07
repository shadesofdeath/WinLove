#pragma once
// A crash leaves a trace: an unhandled exception writes %LOCALAPPDATA%\WinLove\logs\crash-<time>.txt
// (exception, module + offset, the stack with function names and lines when the PDB is next to the
// exe) and crash-<time>.dmp (a minidump for a debugger), with Windows' own dbghelp. Renders too:
// they have no log otherwise.
namespace wl::app {

void installCrashReport();

} // namespace wl::app
