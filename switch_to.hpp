/// @file

#ifndef _switch_to_
#define _switch_to_

#include <cstdint>

/// Switch from the current execution context to a next one.
//
/// The store_old_sp pointer points to a location 
/// where the SP of the current execution context will be saved.
/// The next_sp contains the SP (not its address!) 
/// of the next executuion context
/// (which was previously saved in this manner).
///
/// The initial stack of a context must be constructed
/// to be compatible with the switching code (file swicth_to.asm).
extern "C" void switch_from_to( 
   int32_t *store_old_sp, 
   int32_t next_sp 
);

#endif