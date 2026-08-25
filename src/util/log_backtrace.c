#include "log_backtrace.h"
#include <esp_debug_helpers.h>

void log_backtrace(Stream * out)
{
    esp_backtrace_frame_t frame = { 0 };
    esp_backtrace_get_start(&(frame.pc), &(frame.sp), &(frame.next_pc));

    esp_backtrace_frame_t stk_frame;
    memcpy(&stk_frame, frame, sizeof(esp_backtrace_frame_t));

    out->print("Backtrace:");

    out->printf(" 0x%08" PRIX32 ":0x%08" PRIX32, 
	esp_cpu_process_stack_pc(stk_frame.pc), stk_frame.sp);

    //Check if first frame is valid
    if (!(esp_stack_ptr_is_sane(stk_frame.sp) &&
                       (esp_ptr_executable((void *)esp_cpu_process_stack_pc(stk_frame.pc)) ||
                        /* Ignore the first corrupted PC in case of InstrFetchProhibited */
                        (stk_frame.exc_frame && ((XtExcFrame *)stk_frame.exc_frame)->exccause == EXCCAUSE_INSTR_PROHIBITED)))) {
	out->println(" CORRUPTED!");
        return;
    };

    while (stk_frame.next_pc) {
        if (!esp_backtrace_get_next_frame(&stk_frame)) {    //Get previous stack frame
            corrupted = true;
        }
    	out->printf(" 0x%08" PRIX32 ":0x%08" PRIX32, 
        	esp_cpu_process_stack_pc(stk_frame.pc), stk_frame.sp);
    }
	out->println();
}
