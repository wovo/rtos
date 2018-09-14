//***************************************************************************
//
// file      RTOS.cpp
// version   5.00
// author    Marten Wensink / Wouter van Ooijen
// date      2016-06-13
// 
//***************************************************************************

#include "bmptk.h"
#include "rtos.hpp"
#include "switch_to.hpp"
#include <stdlib.h>

#define task_logging          (global_logging && 0)
#define debug_task_logging    (global_logging && 0)
#define hartbeat_logging      (global_logging && 0)

#define TASK_STATE( task ) \
   ( (task)->is_blocked()   ? "B" : "-" ) << \
   ( (task)->is_suspended() ? "S" : "-" ) << \
   ( (task)->is_ready()     ? "R" : "-" )

#define task_trace \
   if (debug_task_logging) \
      HWLIB_TRACE \
         <<  name() \
         << " " << TASK_STATE( this ) \
         << " "

#define TASK_NAME( t ) (((t) == nullptr)? "-" : (t)->name())

/*
 * Some utility functions
 */
 
int strlen( const char * s ){
   int n = 0;
   while( s[ n] != '\0' ){
      n++;
   }
   return n;
}

void strcpy( char *dest, const char * source ){
   int n = 0;
   do {
      dest[ n ] = source[ n ];
      n++;
   } while( source[ n - 1 ] != '\0' );
}

const char * string_allocate( const char * str ) {
   int len = strlen( str );
   if( len > 16 ) len = 16;
   // should be: new char[ len + 1 ]; //(char *) malloc( 1 + len);
   char * result = (char *) malloc( 1 + len ); 
   strcpy( result, str );
   result[ len ] = '\0';
   return result;
}

int nr_from_mask( unsigned int mask ) {
   for( int i = 0; i < 32; i++ ) {
      if( mask & ( 1 << i )) {
         return i;
      }
   }
   return -1;
}

void task_trampoline( void ) {
   rtos::current_task()->main();
   hwlib::cout
      << "\n>> Fatal error: task "
      << rtos::current_task()->name()
      << " returned from main()\n";
   for(;;);
}

//***************************************************************************
//
// task
//
//***************************************************************************

rtos::task_base::task_base( 
   coroutine<> & cor,
   unsigned int priority, 
   const char * tname
 ): 
   cor( cor ), 
   task_is_blocked( false ),
   task_is_suspended( false ),
   task_priority( priority ),
   waitables( this ),
   sleep_timer( this, "sleeper" )
{

   RTOS_STATISTICS( task_name = string_allocate( tname ); )
   ignore_this_activation = true;
   statistics_clear();
   rtos::add( this );
   task_trace << "CREATED";
}

void rtos::task_base::suspend() {
   task_trace << "suspend";
   task_is_suspended = true;
   release();
}

void rtos::task_base::resume() {
   task_trace << "resume";
   task_is_suspended = false;
   release();
}

void rtos::task_base::unblock() {
   if( ! rtos::scheduler_running ) {
      return;
   }
   task_trace << "unblock";
   task_is_blocked = false;
   release();
}

void rtos::task_base::block() {
   if( ! rtos::scheduler_running ) {
      return;
   }
   task_trace << "block";

   // Only a running task can block itself
   if (rtos::current_task() != this) {
      rtos_fatal ("task not blocked by itself");
   }

   if( !is_ready() ) {
      rtos_fatal ("running task is not READY!?");
   }

   task_is_blocked = true;
   release();
}

void rtos::task_base::release() {
   if( ! rtos::scheduler_running ) {
      task_trace << "scheduler not running";        
      return;
   }

   task_trace << "release";   
   
   // resume the main thread, 
   // which handles switching to another task
   cor.resume_main();
}

void rtos::task_base::print( hwlib::ostream & stream, bool header ) const {
#if RTOS_STATISTICS_ENABLED
   if( header ) {
      stream
         << "\n" << hwlib::dec << hwlib::setfill( ' ' )
         << hwlib::setw( 16 ) << hwlib::left  << "task name"
         << hwlib::setw(  6 ) << hwlib::right << "prio"
         << hwlib::setw(  5 ) << hwlib::right << "stat"
         << hwlib::setw( 11 ) << hwlib::right << "stack u/m"
         << hwlib::setw( 12 ) << hwlib::right << "rt_max (us)"
         << hwlib::setw( 11 ) << hwlib::right << "active"
         << "\n";
   }
   stream
      << hwlib::dec
      << hwlib::setw( 16 ) << hwlib::left  << task_name
      << hwlib::setw(  6 ) << hwlib::right << task_priority
      << hwlib::setw(  3 ) << hwlib::right << TASK_STATE( this )
      << hwlib::setw(  6 ) << hwlib::right << cor.stack_used()
      << '/'
      << hwlib::setw(  5 ) << hwlib::left  << cor.stack_size
      << hwlib::setw( 11 ) << hwlib::right << runtime_max
      << hwlib::setw( 11 ) << hwlib::right << activations
      << "\n";
#endif
}

//***************************************************************************
//
// event
//
//***************************************************************************

bool rtos::event :: operator==( const event & rhs ) const {
   if( t != rhs.t ) {
      rtos_fatal ("comparing incompatible waitables");
   }
   return mask == rhs.mask;
}

bool rtos::event :: operator!=( const event & rhs ) const {
   if( t != rhs.t ) {
      rtos_fatal ("comparing incompatible waitables");
   }
   return mask != rhs.mask;
}

bool rtos::event :: operator==( const waitable & rhs ) const {
   return *this == (event)rhs;
}

bool rtos::event :: operator!=( const waitable & rhs ) const {
   return *this != (event)rhs;
}

rtos::event rtos::event :: operator+( const event & rhs ) const {
   if( t != rhs.t ) {
      rtos_fatal ("adding incompatible waitables");
   }
   return event( t, mask | rhs.mask );
}

void rtos::event::print( hwlib::ostream & s ) const {
   s 
      << "event t=" << t->name() 
      << " m=0x" << hwlib::hex << mask;
}


//***************************************************************************
//
// waitable
//
//***************************************************************************

rtos::waitable::waitable( task_base * t, const char * arg_name ) :
   event( t, 0 )
{
   RTOS_STATISTICS( waitable_name = string_allocate( arg_name ); )
   mask = t->waitables.waitable_allocate();
}

//***************************************************************************
//
// flag
//
//***************************************************************************

rtos::flag::flag( task_base * t, const char * name ):
   waitable( t, name )
#if RTOS_STATISTICS_ENABLED
   , n_sets( 0 )
   , n_gets( 0 )
#endif
{
   RTOS_STATISTICS( rtos::add( this ); )
}

void rtos::flag::set() {
   RTOS_STATISTICS( n_sets++; )
   waitable::set();
}

void rtos::flag::print( hwlib::ostream & stream, bool header ) const {
#if RTOS_STATISTICS_ENABLED
   if( header ) {
      stream << hwlib::setfill( ' ' )
         << hwlib::setw( 18 ) << hwlib::left  << "flag name"
         << hwlib::setw( 18 ) << hwlib::left  << "client"
         << hwlib::setw(  2 ) << hwlib::right << "fn"
         << hwlib::setw( 12 ) << hwlib::right << "sets"
         << hwlib::setw( 11 ) << hwlib::right << "gets"
         << "\n";
   }
   stream 
      << hwlib::dec
      << hwlib::setw( 18 ) << hwlib::left  << waitable_name
      << hwlib::setw( 18 ) << hwlib::left  << TASK_NAME( t )
      << hwlib::setw(  2 ) << hwlib::right << nr_from_mask( mask )
      << hwlib::setw( 12 ) << hwlib::right << n_sets
      << hwlib::setw( 11 ) << hwlib::right << n_gets
      << "\n";
#endif
}

//***************************************************************************
//
// timer
//
//***************************************************************************

rtos::timer::timer( task_base * t, const char * name ):
   waitable( t, name ),
   callback( name )
#if RTOS_STATISTICS_ENABLED
   , n_sets( 0 )
   , n_cancels( 0 )
#endif
{
   RTOS_STATISTICS( rtos::add( this ); )
}

void rtos::timer::set( unsigned long int time ) {
   RTOS_STATISTICS( n_sets++; )
   rtos::callback::start( time );
}

void rtos::timer::cancel() {
   RTOS_STATISTICS( n_cancels++; )
   rtos::callback::cancel();
   rtos::waitable::clear();
}

void rtos::timer::start( unsigned long int time ) {
   RTOS_STATISTICS( n_sets++; )
   rtos::callback::start( time );
}

void rtos::timer::print( hwlib::ostream & stream, bool header ) const {
#if RTOS_STATISTICS_ENABLED
   if( header ) {
      stream 
         << hwlib::dec << hwlib::setfill( ' ' )
         << hwlib::setw( 18 ) << hwlib::left  << "timer name"
         << hwlib::setw( 18 ) << hwlib::left  << "client"
         << hwlib::setw(  2 ) << hwlib::right << "fn"
         << hwlib::setw( 12 ) << hwlib::right << "sets"
         << hwlib::setw( 11 ) << hwlib::right << "cancels"
         << "\n";
   }
   stream 
      << hwlib::dec
      << hwlib::setw( 18 ) << hwlib::left  << waitable_name
      << hwlib::setw( 18 ) << hwlib::left  << TASK_NAME( t )
      << hwlib::setw(  2 ) << hwlib::right << nr_from_mask( mask )
      << hwlib::setw( 12 ) << hwlib::right << n_sets
      << hwlib::setw( 11 ) << hwlib::right << n_cancels
      << "\n";
#endif
}

//***************************************************************************
//
// clock
//
//***************************************************************************

rtos::clock::clock(
   task_base * t,
   unsigned long int _period,
   const char *name
):
   waitable( t, name ),
   callback( name ),
   period( _period )
#if RTOS_STATISTICS_ENABLED
   , ticks( 0 )
#endif
{
   callback::start( period );
   RTOS_STATISTICS( rtos::add( this ); )
}

void rtos::clock::time_up() {
   RTOS_STATISTICS( ticks++; )
   callback::restart( period );
   waitable::set();
}

void rtos::clock::print( hwlib::ostream & stream, bool header ) const {
#if RTOS_STATISTICS_ENABLED
   if( header ) {
      stream 
         << hwlib::dec << hwlib::setfill( ' ' )
         << hwlib::setw( 18 ) << hwlib::left  << "clock name"
         << hwlib::setw( 18 ) << hwlib::left  << "client"
         << hwlib::setw(  2 ) << hwlib::right << "fn"
         << hwlib::setw( 12 ) << hwlib::right << "period (us)"
         << hwlib::setw( 11 ) << hwlib::right << "ticks"
         << "\n";
   }
   stream 
      << hwlib::dec
      << hwlib::setw( 18 ) << hwlib::left  << waitable_name
      << hwlib::setw( 18 ) << hwlib::left  << TASK_NAME( t )
      << hwlib::setw(  2 ) << hwlib::right << nr_from_mask( mask )
      << hwlib::setw( 12 ) << hwlib::right << period 
      << hwlib::setw( 11 ) << hwlib::right << ticks
      << "\n";
#endif
}

//***************************************************************************
//
// waitable_set
//
//***************************************************************************

unsigned int rtos::waitable_set :: waitable_allocate( void ) {
   if( used > 8 * sizeof( current_waitables )) {
      rtos_fatal ("max 32 waitables per group");
   }
   return 1 << used++;
}

void rtos::waitable_set::set ( const waitable &w ) {

//HWLIB_TRACE << current_waitables << " |= " << w.mask;
//HWLIB_TRACE << *w.t;

   // set the waitable bit
   current_waitables |= w.mask;

   // the client will figure out whether he is runnable again
   if( requested_waitables != 0 ) {
      w.t->unblock();
   }
}

void rtos::waitable_set::clear( const waitable & w ) {
//HWLIB_TRACE << current_waitables << " &=~ " << w.mask;
//HWLIB_TRACE << *w.t;

   current_waitables &= ~ w.mask;
}

rtos::event rtos::waitable_set::wait ( unsigned int mask ) {
//HWLIB_TRACE << hwlib::hex << mask;
//HWLIB_TRACE << *client;
//HWLIB_TRACE << hwlib::hex << current_waitables;
   for( ; ; ) {
      // try to find a waitable for which we are waiting
      for (unsigned int i = 0 ; i < used; i++) {
         if( current_waitables & mask & ( 1U << i )) {
            
            // clear the waitable
//HWLIB_TRACE << current_waitables << " &=~ " << (1U << i);
//HWLIB_TRACE << *client;
            
            current_waitables &= ~(1U << i);

#if RTOS_STATISTICS_ENABLED
            // update statistics
            for( flag * f = flags; f != nullptr; f = f->next_flag ) {
                if (f->t == client && f->mask == (1U << i)) {
                    f->n_gets++;
                    break;
                }
            }
#endif
//HWLIB_TRACE;
            // return an event for the waitable
            return event( client, 1U << i );
         }
      }
//HWLIB_TRACE;
      // no waitable found? wait for better times..
      requested_waitables = mask;
      client->block();
      requested_waitables = 0;
   }
}

//***************************************************************************
//
// mutex
//
//***************************************************************************

rtos::mutex::mutex( const char * name ) :
   owner( nullptr ),
   waiters( nullptr )
#if RTOS_STATISTICS_ENABLED
   , wait_count( 0 )
#endif
{
   RTOS_STATISTICS( mutex_name = string_allocate( name ); )
   RTOS_STATISTICS( rtos::add( this ); )
}

void rtos::mutex::wait( void ) {
   RTOS_STATISTICS( wait_count++; )
   if( owner == nullptr ) {
      owner = rtos::current_task();
   }
   else {
      task_base *  t = rtos::current_task();
      task_base ** p = &waiters;

      // get p to point to the last pointer
      while( *p != nullptr ) {
         p = & (*p)-> next_mutex_waiter;
      }

      // insert t after the last pointer
      *p = t;
      t->next_mutex_waiter = nullptr;

      // we'll wait for better times...
      t->block();
   }
}

void rtos::mutex::signal( void ) {
   if( owner != rtos::current_task()) {
      rtos_fatal ("mutex not signaled by owner task");
   }
   else {
      task_base * t = waiters;
      if ( t != nullptr ) {
         // remove task t from the queue
         waiters = waiters->next_mutex_waiter;

         // t is now the owner of the mutex and can run again
         owner = t;
         t->unblock();
      }
      else
         owner = nullptr;
   }
}

void rtos::mutex::print( hwlib::ostream & stream, bool header ) const {
#if RTOS_STATISTICS_ENABLED
   if( header ) {
      stream << hwlib::setfill( ' ' )
         << hwlib::setw( 18 ) << hwlib::left  << "mutex name"
         << hwlib::setw( 19 ) << hwlib::left  << "owner"
         << hwlib::setw( 13 ) << hwlib::right << "waits"
         << hwlib::setw( 11 ) << hwlib::right << "waiters"
         << "\n";
   }
   stream << hwlib::setw ( 18 ) << hwlib::left  << mutex_name;
   if( owner == nullptr ) {
      stream << hwlib::setw( 19 ) << hwlib::left  << "-";
   }
   else {
      stream << hwlib::setw( 19 ) << hwlib::left  << owner->task_name;
   }
   stream 
      << hwlib::dec
      << hwlib::setw( 13 ) << hwlib::right  << wait_count
      << hwlib::setw(  6 ) << hwlib::right << "[ ";
   if (waiters == nullptr)
      stream << '-';
   for( task_base *t = waiters; t != nullptr; t = t->next_mutex_waiter ) {
      stream << hwlib::left << t->task_name;
      if (t->next_mutex_waiter != nullptr)
         stream << ", ";
   }
   stream << " ]\n";
#endif
}

rtos::mutex::~mutex( void ) {
   rtos_fatal ("mutex destructor called");
}

//***************************************************************************
//
// callback
//
//***************************************************************************

rtos::callback::callback( const char * name ) :
    time_to_wait( -1 )
{
   RTOS_STATISTICS( object_name = string_allocate( name ); )
   rtos::add( this );
}

//***************************************************************************
//
// channel
//
//***************************************************************************

rtos::channel_base::channel_base( task_base * t, const char * name ):
   waitable( t, name ),
#if RTOS_STATISTICS_ENABLED
   writes( 0 ),
   ignores( 0 ),
#endif
   qSize( 0 ),
   head( 0 ),
   tail ( 0 )
{
#if RTOS_STATISTICS_ENABLED
   channel_name = string_allocate( name );
   rtos::add( this );
#endif
}

void rtos::channel_base::print( hwlib::ostream & stream, bool header ) const {
#if RTOS_STATISTICS_ENABLED
   if( header ) {
      stream << hwlib::setfill( ' ' )
         << hwlib::setw( 18 ) << hwlib::left  << "channel name"
         << hwlib::setw( 18 ) << hwlib::left  << "owner"
         << hwlib::setw(  2 ) << hwlib::right << "fn"
         << hwlib::setw( 12 ) << hwlib::right << "writes"
         << hwlib::setw( 11 ) << hwlib::right << "ignores"
         << hwlib::setw(  8 ) << hwlib::right << "queued"
         << "\n";
   }
   stream 
      << hwlib::dec
      << hwlib::setw( 18 ) << hwlib::left  << channel_name
      << hwlib::setw( 18 ) << hwlib::left  << t->task_name
      << hwlib::setw(  2 ) << hwlib::right << nr_from_mask( mask )
      << hwlib::setw( 12 ) << hwlib::right << writes
      << hwlib::setw( 11 ) << hwlib::right << ignores
      << hwlib::setw(  8 ) << hwlib::right << qSize
      << "\n";
#endif
}

//***************************************************************************
//
// pool
//
//***************************************************************************

rtos::pool_base::pool_base( const char * name )
#if RTOS_STATISTICS_ENABLED
   : reads( 0 ),
   writes( 0 )
#endif
{
   RTOS_STATISTICS( pool_name = string_allocate( name ); )
   RTOS_STATISTICS( rtos::add( this ); )
}

void rtos::pool_base::print( hwlib::ostream & stream, bool header ) const {
#if RTOS_STATISTICS_ENABLED
   if( header ) {
      stream << hwlib::setfill( ' ' )
         << hwlib::setw( 18 ) << hwlib::left << "pool name"
         << hwlib::setw( 10 ) << hwlib::left << "writes"
         << hwlib::setw( 10 ) << hwlib::left << "reads"
         << "\n";
   }
   stream << hwlib::dec
      << hwlib::setw( 18 ) << hwlib::left << pool_name
      << hwlib::setw( 10 ) << hwlib::left << writes
      << hwlib::setw( 10 ) << hwlib::left << reads
      << "\n";
#endif
}

//***************************************************************************
//
// mailbox
//
//***************************************************************************

rtos::mailbox_base::mailbox_base( const char * name )
#if RTOS_STATISTICS_ENABLED
   : writer( nullptr),
   reader( nullptr ),
   writes( 0 ),
   reads( 0 )
#endif
{
   RTOS_STATISTICS( mailbox_name = string_allocate( name ); )
   RTOS_STATISTICS( rtos::add( this ); )
}

void rtos::mailbox_base::print( hwlib::ostream & stream, bool header ) const {
#if RTOS_STATISTICS_ENABLED
   if( header ) {
      stream << hwlib::setfill( ' ' )
         << hwlib::setw( 18 ) << hwlib::left << "mailbox name"
         << hwlib::setw( 18 ) << hwlib::left << "writer"
         << hwlib::setw( 18 ) << hwlib::left << "reader"
         << hwlib::setw( 10 ) << hwlib::left << "writes"
         << hwlib::setw( 10 ) << hwlib::left << "reads"
         << "\n";
   }
   stream << hwlib::dec
      << hwlib::setw( 18 ) << hwlib::left << mailbox_name
      << hwlib::setw( 18 ) << hwlib::left << TASK_NAME( writer )
      << hwlib::setw( 18 ) << hwlib::left << TASK_NAME( reader )
      << hwlib::setw( 10 ) << hwlib::left << writes
      << hwlib::setw( 10 ) << hwlib::left << reads
      << "\n";
#endif
}

//***************************************************************************
//
// << operators
//
//***************************************************************************

hwlib::ostream & operator<< ( hwlib::ostream & s, const rtos::task_base & t ) {
   t.print( s, false );
   return s;
}

hwlib::ostream & operator<< ( hwlib::ostream & s, const rtos::flag & f ) {
   f.print( s, false );
   return s;
}

hwlib::ostream & operator<< ( hwlib::ostream & s, const rtos::event & e ) {
   e.print( s );
   return s;
}

hwlib::ostream & operator<< ( hwlib::ostream & s, const rtos::timer & t ) {
   t.print( s, false );
   return s;
}

hwlib::ostream & operator<< ( hwlib::ostream & s, const rtos::clock & c ) {
   c.print( s, false );
   return s;
}

hwlib::ostream & operator<< ( hwlib::ostream & s, const rtos::channel_base & cb ) {
   cb.print( s, false );
   return s;
}

hwlib::ostream & operator<< ( hwlib::ostream & s, const rtos::mutex & m ) {
   m.print( s, false );
   return s;
}

hwlib::ostream & operator<< ( hwlib::ostream & s, const rtos::mailbox_base & mb ) {
   mb.print( s, false );
   return s;
}

hwlib::ostream & operator<< ( hwlib::ostream & s, const rtos::pool_base & pb ) {
   pb.print( s, false );
   return s;
}

//***************************************************************************
//
// RTOS
//
//***************************************************************************

// Reference to the task currently executed:
rtos::task_base * rtos::rtos_current_task = nullptr;

// timer list head
rtos::callback * rtos::timerList = nullptr;

// list of tasks, highest prority first
rtos::task_base * rtos::taskList = nullptr;

// flag set to clear statistics
bool rtos::must_clear = false;

// not running yet
bool rtos::scheduler_running = false;

const char * rtos::task_base::name( void ) const {
#if RTOS_STATISTICS_ENABLED
   return task_name;
#else
   return "";
#endif
}

// adding various objects to the RTOS lists
#if RTOS_STATISTICS_ENABLED

rtos::flag          * rtos::flags      = nullptr;
rtos::timer         * rtos::timers     = nullptr;
rtos::clock         * rtos::clocks     = nullptr;
rtos::mutex         * rtos::mutexes    = nullptr;
rtos::channel_base  * rtos::channels   = nullptr;
rtos::mailbox_base  * rtos::mailboxes  = nullptr;
rtos::pool_base     * rtos::pools      = nullptr;

void rtos::add( flag * f ) {
   f->next_flag = flags;
   flags = f;
}

void rtos::add( timer * t ) {
   t->next_timer = timers;
   timers = t;
}

void rtos::add( clock * c ) {
   c->next_clock = clocks;
   clocks = c;
}

void rtos::add( mutex * m ) {
   m->next_mutex = mutexes;
   mutexes = m;
}

void rtos::add( channel_base * cb ) {
   cb->next_channel = channels;
   channels = cb;
}

void rtos::add( mailbox_base * mb ) {
   mb->next_mailbox = mailboxes;
   mailboxes = mb;
}

void rtos::add( pool_base * pb ) {
   pb->next_pool = pools;
   pools = pb;
}

#endif

void rtos::print( hwlib::ostream & stream ) {

   // global info
   stream << "\n\nRTOS version    : " 
      << RTOS_VERSION << "\n";
//   if(0) stream << "HEAP free       : " 
//      << hwlib::dec << bmptk_heap_free() 
//         << " (" << hwlib::dec << bmptk_heap_used() 
//         << " used of " << bmptk_heap_size() << ")\n";
//   if(0)stream << "MAIN STACK free : " 
//      << hwlib::dec << bmptk_stack_free() 
//         << " (" << bmptk_stack_used() 
//         << " used of " << bmptk_stack_size() << ")\n";

#if RTOS_STATISTICS_ENABLED
   bool header;

   if( rtos_current_task != nullptr ) {
      rtos_current_task->ignore_this_activation = true;
   }

   // tasks
   header = true;
   for( task_base * t = taskList; t != nullptr; t = t->nextTask ) {
      t->print( stream, header );
      header = false;
   }
   if( header ) {
      stream << "no tasks\n";
   }
   stream << "\n";

   // flags
   header = true;
   for( flag * f = flags; f != nullptr; f = f->next_flag ) {
      f->print( stream, header );
      header = false;
   }
   if( header ) {
      stream << "no flags\n";
   }
   stream << "\n";

   // timers
   header = true;
   for( timer * t = timers; t != nullptr; t = t->next_timer ) {
      t->print( stream, header );
      header = false;
   }
   if( header ) {
      stream << "no timers\n";
   }
   stream << "\n";

   // clocks
   header = true;
   for( clock * c = clocks; c != nullptr; c = c->next_clock ) {
      c->print( stream, header );
      header = false;
   }
   if( header ) {
      stream << "no clocks\n";
   }
   stream << "\n";

   // channels
   header = true;
   for( channel_base * cb = channels; cb != nullptr; cb = cb->next_channel ) {
      cb->print( stream, header );
      header = false;
   }
   if( header ) {
      stream << "no channels\n";
   }
   stream << "\n";

   // mutexes
   header = true;
   for( mutex * m = mutexes; m != nullptr; m = m->next_mutex ) {
      m->print( stream, header );
      header = false;
   }
   if( header ) {
      stream << "no mutexes\n";
   }
   stream << "\n";

   // mailboxes
   header = true;
   for( mailbox_base * mb = mailboxes; mb != nullptr; mb = mb->next_mailbox ) {
      mb->print( stream, header );
      header = false;
   }
   if( header ) {
      stream << "no mailboxes\n";
   }
   stream << "\n";

   // pools
   header = true;
   for( pool_base * pb = pools; pb != nullptr; pb = pb->next_pool ) {
      pb->print( stream, header );
      header = false;
   }
   if( header ) {
      stream << "no pools\n";
   }
   stream << "\n";

#endif
}

long long int last_run_time = 0;

void rtos::beat( void ) {
   
//HWLIB_TRACE;     

   // get the elapse time since last beat, and reset it to 0
   auto new_run_time = hwlib::now_us();
   auto elapsed = new_run_time - last_run_time;
   last_run_time = new_run_time;
   
//HWLIB_TRACE;   

   if (elapsed > 0) {
      // service the callback timer queue
      for ( callback * t = timerList; t != nullptr; t = t->nextTimer ) {
#if ( hartbeat_logging == 1 )
         HWLIB_TRACE
            RTOS_STATISTICS( << t->object_name )
            << "@" << hwlib::hex << (int) t << hwlib::dec
            << " ttw=" << t->time_to_wait
            << " elapsed=" << elapsed ;
#endif
         if( t->time_to_wait >= 0 ) {
            t->time_to_wait -= elapsed;
            if( t->time_to_wait < 0 ) {
#if ( hartbeat_logging == 1 )
         HWLIB_TRACE << "time up!";
#endif         
               t->time_up();
            }
         }
      }
   }

   // find the highest-priority ready task and run it, then return
   // rtos_current_task is a class attribute, used by current_task
   for (
      rtos_current_task = taskList;
      rtos_current_task != nullptr;
      rtos_current_task = rtos_current_task->nextTask
   ) {
      if (rtos_current_task->is_ready()) {
#if ( hartbeat_logging == 1 )
         HWLIB_TRACE
            << "resume " << rtos_current_task->name()
            << " prio="  << (int) rtos_current_task->priority()
            << "\n";
#endif

         auto start = hwlib::now_us();
         rtos_current_task->cor.resume();
         auto end = hwlib::now_us();

#if ( hartbeat_logging == 1 )
         HWLIB_TRACE << "back from " << rtos_current_task->name() << "\n";
#endif

         unsigned long int runtime = end - start;
         if( !rtos_current_task->ignore_this_activation ){
            if (rtos_current_task->activations > 1 &&
                runtime > rtos_current_task->runtime_max ){
               rtos_current_task->runtime_max = runtime;
            }
         }

#if ( hartbeat_logging == 1 )
         if( runtime > 10 * ms ) {
            HWLIB_TRACE
               << rtos_current_task->name()
               << " runtime=" << runtime;
         }
#endif
         rtos_current_task->ignore_this_activation = false;
         rtos_current_task->activations++;
         if (must_clear) {
#if ( hartbeat_logging == 1 )
            do_statistics_clear();
#endif
            must_clear = false;
         }

         return;
      }
   }

#if RTOS_STATISTICS_ENABLED

   // no runnable task has been found, nothing to do right now
   // we might as well do deadlock detection
   for( clock * c = clocks; c != nullptr; c = c->next_clock ) {
      if( c->t->waitables.requested_waitables & c->mask ) {
         return;
      }
   }
   for( timer * t = timers; t != nullptr; t = t->next_timer ) {
      if( 
	     ( t->time_to_wait >= 0 )
         && ( t->t->waitables.requested_waitables & t->mask ) 
	  ) {
         return;
      }
   }

   // no task is waiting for a running timer or clock: DEADLOCK
   hwlib::cout << "\n\n********** DEADLOCK DETECTED **********\n\n";
   print( hwlib::cout );

   rtos_fatal( "deadlock detected" );

#endif
}

void rtos::run( void ) {
   
   // initialize the timing
   (void)hwlib::now_us();

   // Show initial statistics
   // print( hwlib::cout );

#if ( global_logging == 1 )
   hwlib::cout << "Scheduler starts" << "\n";
#endif
//HWLIB_TRACE;  
   scheduler_running = true;
//HWLIB_TRACE;   
   rtos_current_task = nullptr;
//HWLIB_TRACE;   
#if ( hartbeat_logging == 1 )   
   int n = 0;
#endif
//HWLIB_TRACE; 
   for( ; ; ) {
#if ( hartbeat_logging == 1 )
      if ( ++n > 10000 ) {
         hwlib::cout << '.';
         n = 0;
      }
#endif
//HWLIB_TRACE;  
      beat();
   }
}

// register a timer
void rtos::add( callback * t ) {

   // add the timer to the timer list
   t->nextTimer = timerList;
   timerList = t;
}

// register a task
void rtos::add( task_base * new_task ) {
#if ( task_logging == 1 )
   HWLIB_TRACE
      << "register task " << new_task->name()
      << " priority=" << new_task->task_priority;
#endif

   if( new_task->task_priority > RTOS_MIN_PRIORITY ) {
      rtos_fatal ("illegal task priority");
   }

   // walk the task queue untill the next task either
   // - does not exist, or
   // - has a lower priority (higher priority number) than the new task
   task_base ** t = &taskList;

   while( ( *t != nullptr ) && ( (*t)->task_priority <= new_task->task_priority ) ) {
      // if the task priorities are equal, increment the priority
      // of the newly allocated task if possible
      if( (*t)->task_priority == new_task->task_priority ) {
         if( new_task->task_priority >= RTOS_MIN_PRIORITY ) {
            new_task->task_priority++;
         }
         else {
            rtos_fatal ("duplicate task priority");
         }
      }
      t = &( (*t)->nextTask );
   }
   // now insert the new task after the current task
   new_task->nextTask = *t;
   *t = new_task;

#if ( task_logging == 1 )
   HWLIB_TRACE << "registering done ";
#endif
}

void rtos::do_statistics_clear( void ) {
   for (
      task_base * task = taskList;
      task!= nullptr;
      task = task->nextTask
   ) {
      task->statistics_clear();
   }
}

void rtos::display_statistics( void ) {
   hwlib::cout << "\n";
   print( hwlib::cout );
   statistics_clear();
}

namespace hwlib {

void wait_ns( int_fast32_t n ){
   // round up to us	
   wait_us( ( n + 999 ) / 1000 );
}

void wait_us( int_fast32_t n ){   
   if( rtos::scheduler_running ){     
      rtos::current_task()->sleep_us( n );   
   } else {
      hwlib::wait_us_busy( n ); 
   } 
}

void wait_ms( int_fast32_t n ){
   while( n > 0 ){
      wait_us( 1000 );
      --n;
   }   
}  

};
