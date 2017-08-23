#include "coroutine.hpp"

int32_t coroutine_context::main_sp;      
   
int32_t * coroutine_context::current_coroutine_sp_ptr = & coroutine_context::main_sp;     