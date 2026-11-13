// Fuzz-harness helper compiled into libogq: drop everything a parse left in the thread's memory contexts.
// raw_parser_opengauss_dolphin re-runs knl_thread_init + create_session_context on every call and never
// frees them (one-shot design), which costs ~MBs of RSS per input and would OOM the fuzzer.
#include "postgres.h"
#include "knl/knl_thread.h"
#include "utils/memutils.h"

extern "C" void og_query_release_state()
{
    MemoryContext top = t_thrd.top_mem_cxt;
    if (top == NULL) {
        return;
    }
    t_thrd.proc = NULL;
    CurrentMemoryContext = top;
    MemoryContextUnSeal(top);
    MemoryContextDeleteChildren(top, NULL);
    MemoryContextUnSeal(top);
    MemoryContextReset(top);
    u_sess = NULL;
    t_thrd.fake_session = NULL;
    t_thrd.mcxt_group = NULL;
    // Recreate the children MemoryContextInit() makes (it returns early once the top context exists).
    ErrorContext = AllocSetContextCreate(top, "ErrorContext", 8 * 1024, 8 * 1024, 8 * 1024);
    AlignMemoryContext = AllocSetContextCreate(top, "AlignContext", ALLOCSET_DEFAULT_MINSIZE,
        ALLOCSET_DEFAULT_INITSIZE, ALLOCSET_DEFAULT_MAXSIZE, MEMALIGN_CONTEXT);
    t_thrd.mem_cxt.profile_log_mem_cxt = AllocSetContextCreate(top, "Profile Logging", ALLOCSET_DEFAULT_MINSIZE,
        ALLOCSET_DEFAULT_INITSIZE, ALLOCSET_DEFAULT_MAXSIZE);
}
