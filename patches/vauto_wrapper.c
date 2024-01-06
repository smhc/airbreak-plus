// Author: Llamamoe @ Discord

// Jump to ASV subroutine at `0x080f0576` in function `run_queue_offset_-0x7ffffc9` found at `080f054e`, using a `blx r1` instruction.
// VAuto task(22) at `0x080e24dc`, +1 pointer at `0x000f4e64`

float * const fvars = (void*) 0x2000e948;
int * const ivars = (void*) 0x2000e750;

void start(int param_1) {
    ////// Execute the VAuto task //////
    void (*task_vauto)(int) = (void(*)(int))(0x080e24dc+1); // +1 since it's an ARM thumb call
    task_vauto(param_1);
}