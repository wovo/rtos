#include "coroutine.hpp"

int coroutine_context::main_sp;      
   
int * coroutine_context::current_coroutine_sp_ptr = & coroutine_context::main_sp;     