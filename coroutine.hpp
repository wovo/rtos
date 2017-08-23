/// @file

#ifndef coroutine_H
#define coroutine_H

#include <cstdint>
#include "hwlib.hpp"
#include "switch_to.hpp"

template< int32_t N = 0 > class coroutine;

class coroutine_context {
   friend class coroutine< 0 >;   
   
   // a place to store the SP of the main 'couroutine'
   static int32_t main_sp;      
   
   // pointer to the SP of the currently running coroutine
   // Initially it points to a the main_sp
   static int32_t * current_coroutine_sp_ptr;    
};

/// coroutine class
//
/// This class implements a coroutine: an independent thread of execution 
/// that can pass execution to another coroutine by calling resume() on
/// that coroutine. 
/// This can be used as the basis for a co-operative multithreading system.
template<> class coroutine< 0 > {
private:

   // place to store the SP of this coroutine when it is suspended
   int32_t sp;
   
   // pointer to the stack area used by this coroutine
   int32_t * stack;
   
   // size of *stack in integers
   const size_t int_stack_size;
   
   // marker for unused stack locations
   static const int32_t marker = 0xDEADBEEF;
   
public:

   /// the size (in bytes) of the stack allocated for this coroutine
   const size_t stack_size;   

protected:   

   coroutine( void body( void ), int32_t * stack, size_t int_stack_size ): 
      stack( stack ),
      int_stack_size( int_stack_size ),
      stack_size( 4 * int_stack_size )
   {  
   
      // fill all stack locations with the marker
      for( size_t i = 0; i < int_stack_size; ++i ){
         stack[ i ] = marker;
      }
      
      // create a 'fake' initial Cortex stack frame,
      // compatible with the switch_to.asm code
      stack[ int_stack_size - 1 ] = reinterpret_cast< int32_t >( body );
      sp = reinterpret_cast< int32_t >( & stack[ int_stack_size - 10 ] );      
   }   
     
public:
   
   /// pass execution to this coroutine
   //
   /// Call this function on a coroutine object to activate (start or resume)
   /// execution of that coroutine. The current coroutine is suspended.
   /// Execution of a coroutine starts by execution its body function 
   /// (the function that was passed to its constructor). 
   /// Resuming a coroutine continues
   /// execution after the resume() call that suspended it.
   ///
   /// Initially, the main is executing, 
   /// so it must issue the first resume() call on a coroutine. 
   /// A coroutine body function must not return.
   void resume(){   
   
      // this is where the sp must be stored.
      // save it, because we must overwrite it in the next statement
      auto store_old_sp = coroutine_context::current_coroutine_sp_ptr;
            
      // next time, the SP must store in the sp of the current object
      coroutine_context::current_coroutine_sp_ptr = & sp;
   
      // store the old sp and switch to this coroutine
      switch_from_to( store_old_sp, sp );         
   }
   
   /// pass execution to the main 'coroutine'
   //
   /// Call this function to re-activate (resume) the main 'coroutine'.
   /// The current coroutine is suspended.
   static void resume_main(){
      
      // this is where the sp must be stored.
      // save it, because we must overwrite it for the next resume()
      auto store_old_sp = coroutine_context::current_coroutine_sp_ptr;
           
      // next time, the SP must store in the sp of the main
      coroutine_context::current_coroutine_sp_ptr = & coroutine_context::main_sp;
   
      // store the old sp and switch to this coroutine
      switch_from_to( store_old_sp, coroutine_context::main_sp );  
   }
   
   /// return number of unused stack bytes
   int stack_free(){
   
      // count the unused entries, starting at the bottom
      int n = 0;
      while( stack[ n ] == marker ){
         n++;         
      }
      
      // return the number of unused bytes
      return 4 * n;
   }
   
   /// return number of used stack bytes
   int stack_used(){
      return stack_size - stack_free();
   }

};

template< int32_t N > class coroutine : public coroutine<> {
private:

   // convert N to a number of integers, round up
   static const size_t int_stack_size = ( N + 3 ) / 4;
   
   // the room for the stack
   int32_t stack[ int_stack_size ];
   
public:

   /// construct a coroutine from its stack size and main function
   //
   /// This constructor creates a coroutine from its stack size
   /// (this is the template parameter, in bytes),
   /// and a function that is its body.
   /// The body function must not return.
   coroutine( void body( void ) ):
      coroutine< 0 >( body, stack, int_stack_size )
   {}   
};

#endif // coroutine_H
