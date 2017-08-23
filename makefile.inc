#============================================================================
#
# makefile-include for the rtos: 
# sets 
# - the search path (the rtos directory)
# - the dependencies (all rtos source files)
# - the sources (only the cpp and asm files)
# 
# (c) Wouter van Ooijen (wouter@voti.nl) 2017
#
# Distributed under the Boost Software License, Version 1.0.
# (See accompanying file LICENSE_1_0.txt or copy at 
# http://www.boost.org/LICENSE_1_0.txt)   
#
#============================================================================

SEARCH            += $(RTOS)

SOURCES           += switch_to.asm coroutine.cpp rtos.cpp
HEADERS           += switch_to.hpp coroutine.hpp rtos.hpp
