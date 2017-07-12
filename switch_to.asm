
   .cpu cortex-m0
   .global switch_from_to
   .text
   .align 2
   
   // extern "C" void switch_from_to( 
   //    int *store_old_sp, 
   //    int next_sp  
   // ); 
switch_from_to:

   // save current context on the stack
   push { r4 - r7, lr }
   mov r2, r8
   mov r3, r9
   mov r4, r10
   mov r5, r11
   mov r6, r12
   push { r2 - r6 }
   
   // *store_old_sp = SP
   mov r2, sp
   str r2, [ r0 ]
  
   // SP = next_sp
   mov sp, r1
   
   // restore the new context from the stack
   pop { r2 - r6 }
   mov r12, r6
   mov r11, r5
   mov r10, r4
   mov r9, r3
   mov r8, r2
   pop { r4 - r7, pc }
