/*
 * mini-posix.c: POSIX signal handling support for Mono.
 *
 * Authors:
 *   Mono Team (mono-list@lists.ximian.com)
 *
 * Copyright 2001-2003 Ximian, Inc.
 * Copyright 2003-2008 Ximian, Inc.
 *
 * See LICENSE for licensing information.
 */
#include <config.h>
#include <signal.h>
#include <math.h>

#include <mono/metadata/assembly.h>
#include <mono/metadata/loader.h>
#include <mono/metadata/tabledefs.h>
#include <mono/metadata/class.h>
#include <mono/metadata/object.h>
#include <mono/metadata/tokentype.h>
#include <mono/metadata/tabledefs.h>
#include <mono/metadata/threads.h>
#include <mono/metadata/appdomain.h>
#include <mono/metadata/debug-helpers.h>
#include <mono/io-layer/io-layer.h>
#include "mono/metadata/profiler.h"
#include <mono/metadata/profiler-private.h>
#include <mono/metadata/mono-config.h>
#include <mono/metadata/environment.h>
#include <mono/metadata/mono-debug.h>
#include <mono/metadata/gc-internal.h>
#include <mono/metadata/threads-types.h>
#include <mono/metadata/verify.h>
#include <mono/metadata/verify-internals.h>
#include <mono/metadata/mempool-internals.h>
#include <mono/metadata/attach.h>
#include <mono/utils/mono-math.h>
#include <mono/utils/mono-compiler.h>
#include <mono/utils/mono-counters.h>
#include <mono/utils/mono-logger-internal.h>
#include <mono/utils/mono-mmap.h>
#include <mono/utils/dtrace.h>
#include <mono/utils/mono-threads.h>

#include "mini.h"
#include <string.h>
#include <ctype.h>
#include "trace.h"
#include "version.h"

#include "jit-icalls.h"

extern LPTOP_LEVEL_EXCEPTION_FILTER mono_old_win_toplevel_exception_filter;
extern guint64 mono_win_chained_exception_filter_result;
extern gboolean mono_win_chained_exception_filter_didrun;

void
mono_runtime_install_handlers (void)
{
#ifndef MONO_CROSS_COMPILE
	win32_seh_init();
	win32_seh_set_handler(SIGFPE, mono_sigfpe_signal_handler);
	win32_seh_set_handler(SIGILL, mono_sigill_signal_handler);
	win32_seh_set_handler(SIGSEGV, mono_sigsegv_signal_handler);
	if (mini_get_debug_options ()->handle_sigint)
		win32_seh_set_handler(SIGINT, mono_sigint_signal_handler);
#endif
}

void
mono_runtime_cleanup_handlers (void)
{
#ifndef MONO_CROSS_COMPILE
	win32_seh_cleanup();
#endif
}


/* mono_chain_signal:
 *
 *   Call the original signal handler for the signal given by the arguments, which
 * should be the same as for a signal handler. Returns TRUE if the original handler
 * was called, false otherwise.
 */
gboolean
SIG_HANDLER_SIGNATURE (mono_chain_signal)
{
	int signal = _dummy;
	GET_CONTEXT;

	if (mono_old_win_toplevel_exception_filter) {
		mono_win_chained_exception_filter_didrun = TRUE;
		mono_win_chained_exception_filter_result = (*mono_old_win_toplevel_exception_filter)(info);
		return TRUE;
	}
	return FALSE;
}

static MMRESULT win32_timer;

static void CALLBACK
win32_time_proc (UINT uID, UINT uMsg, DWORD dwUser, DWORD dw1, DWORD dw2)
{
	THREAD_INFO_TYPE *info;

	FOREACH_THREAD_SAFE (info) {
		DWORD id = mono_thread_info_get_tid (info);
		HANDLE handle = OpenThread (READ_CONTROL | THREAD_GET_CONTEXT, FALSE, id);
		CONTEXT context;

		if (id == GetCurrentThreadId ()) {
			CloseHandle (handle);
			continue;
		}

		g_assert (id != GetCurrentThreadId ());

		context.ContextFlags = CONTEXT_CONTROL;

		if (GetThreadContext (handle, &context)) {
#ifdef _WIN64
			mono_profiler_stat_hit ((guchar *) context.Rip, id, &context);
#else
			mono_profiler_stat_hit ((guchar *) context.Eip, id, &context);
#endif
		}

		CloseHandle (handle);
	} END_FOREACH_THREAD_SAFE
}

void
mono_runtime_setup_stat_profiler (void)
{
	static int inited = 0;
	TIMECAPS timecaps;

	if (inited)
		return;

	inited = 1;
	if (timeGetDevCaps (&timecaps, sizeof (timecaps)) != TIMERR_NOERROR)
		return;

	if (timeBeginPeriod (1) != TIMERR_NOERROR)
		return;

	if ((win32_timer = timeSetEvent (1, 0, win32_time_proc, 0, TIME_PERIODIC)) == 0) {
		timeEndPeriod (1);
		return;
	}
}

void
mono_runtime_shutdown_stat_profiler (void)
{
}

gboolean
mono_thread_state_init_from_handle (MonoThreadUnwindState *tctx, MonoNativeThreadId thread_id, MonoNativeThreadHandle thread_handle)
{
	g_error ("Windows systems haven't been ported to support mono_thread_state_init_from_handle");
	return FALSE;
}

