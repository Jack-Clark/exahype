# This is the core-makefile for ExaHyPE
# generated by the Peano Development Tools (PDT)


# *********************************************************************************************
# README
# *********************************************************************************************
#
# Available configuration Parameters for ExaHyPE
#
# export variable  |  default-value  |  further values         |  description
#--------------------------------------------------------------------------------------------------
# ARCHITECTURE        CPU               Phi, KNL, HSW             Hardware-plattfrom
# COMPILER            Intel             GNU, Manual               Used compiler (and linker)
# MODE                Release           Debug, PeanoProfile       Verbosity and Debug level
#                                       Profile, Asserts          
# SHAREDMEM           None              OMP, TBB                  Shared-memory parallelisation
# DISTRIBUTEDMEM      None              MPI                       Distributed-memory parallelisation
# BOUNDARYCONDITIONS  None              Periodic                  Type of boundary conditions
# *********************************************************************************************

SHELL = bash
tolower = $(shell echo $(1) | tr '[:upper:]' '[:lower:]')

# ************************************************

#
# Set default values of configuration parameters
ifeq ($(ARCHITECTURE),)
	ARCHITECTURE=CPU
endif
ifeq ($(COMPILER),)
	COMPILER=Intel
endif
ifeq ($(MODE),)
	MODE=Release
endif
ifeq ($(SHAREDMEM),)
	SHAREDMEM=None
endif
ifeq ($(DISTRIBUTEDMEM),)
	DISTRIBUTEDMEM=None
endif
ifeq ($(BOUNDARYCONDITIONS),)
	BOUNDARYCONDITIONS=None
endif

ifeq ($(call tolower,$(GPROF)),on)
	COMPILER_CFLAGS += -pg 
	COMPILER_LFLAGS += -pg 
endif
# ************************************************
#
#Set compiler and linker flags for the different Debug/Release modes
#These are only Peano/ExaHyPE flags. Nothing that has to do with the
#compiler. 
#
#
ifeq ($(call tolower,$(MODE)),release)
	# Settings for the Release mode
	# ----------------------------------------------
	COMPILER_CFLAGS +=
	COMPILER_LFLAGS +=

else ifeq ($(call tolower,$(MODE)),peanoprofile)
	# Settings for the Release mode
	# ----------------------------------------------
	COMPILER_CFLAGS += -DPerformanceAnalysis -DTrackGridStatistics 
	COMPILER_LFLAGS +=

else ifeq ($(call tolower,$(MODE)),debug)
	# Settings for the Debug mode
	# --------------------------------------------
	COMPILER_CFLAGS += -DDebug -DAsserts -DTrackGridStatistics
	COMPILER_LFLAGS +=

else ifeq ($(call tolower,$(MODE)),profile)
	# Settings for the Profile mode
	# --------------------------------------------
	COMPILER_CFLAGS += -DPerformanceAnalysis -DTrackGridStatistics 
	COMPILER_LFLAGS += 

else ifeq ($(call tolower,$(MODE)),asserts)
	# Settings for the Asserts mode
	# --------------------------------------------
	COMPILER_CFLAGS += -DAsserts -DTrackGridStatistics
	COMPILER_LFLAGS += 

else
	# Option not recognized
	# ----------------------------------------------
	$(error Unknown option '$(MODE)' for variable [MODE])
endif



# ************************************************
#
#Set compiler and linker flags for the different conmpilers
ifeq ($(call tolower,$(COMPILER)),intel)
	# Settings for the Intel Compiler
	# ----------------------------------------------
	ifndef EXAHYPE_CC 
	  ifeq ($(call tolower,$(DISTRIBUTEDMEM)),mpi)
	    CC=mpiicpc
	  else
	    CC=icpc
	  endif
	else
	  CC=$(EXAHYPE_CC)
	endif
	ifndef EXAHYPE_FC
	  ifeq ($(call tolower,$(DISTRIBUTEDMEM)),mpi)
	    FC=mpiifort
	  else
	    FC=ifort
	  endif
	else
	  FC=$(EXAHYPE_FC)
	endif
	
	FCOMPILER_CFLAGS  += -r8 -cpp -auto 
	# REMOVING "-fast" as it induces compiler bugs!
	LINK_FORTRAN      += -lifcore

	COMPILER_CFLAGS   += -restrict -std=c++11 -pedantic -Wall  
	COMPILER_LFLAGS   +=
	
	
	ifeq ($(call tolower,$(MODE)),debug)
	  COMPILER_CFLAGS   += -O0 -ggdb
	else ifeq ($(call tolower,$(MODE)),asserts)
	  COMPILER_CFLAGS += -g3
	  COMPILER_LFLAGS += -g3
	else ifeq ($(call tolower,$(MODE)),profile)
	  #COMPILER_CFLAGS += -p -pg -g3 -fast -no-ipo -ip
	  COMPILER_CFLAGS += -p -pg -g3 -O3 -no-ipo -ip
	  COMPILER_LFLAGS += -p -pg
	else
	  #COMPILER_CFLAGS   += -fast -no-ipo -ip
	  COMPILER_CFLAGS   += -O3 -no-ipo -ip
	endif

	ifeq ($(call tolower,$(SHAREDMEM)),tbb)
	else
	  COMPILER_CFLAGS   += -fno-rtti  
	  COMPILER_LFLAGS   += -lrt
	endif
	
    ifeq ($(call tolower,$(ARCHITECTURE)),)
	  COMPILER_CFLAGS += 
      COMPILER_LFLAGS += 
    else ifeq ($(call tolower,$(ARCHITECTURE)),cpu)
 	  COMPILER_CFLAGS += -xHost
      COMPILER_LFLAGS += -xHost
	else ifeq ($(call tolower,$(ARCHITECTURE)),phi)
	  COMPILER_CFLAGS += -mmic
	  COMPILER_LFLAGS += -mmic
	else ifeq ($(call tolower,$(ARCHITECTURE)),knl)
	  COMPILER_CFLAGS += -xMIC-AVX512 -fma
      #FCOMPILER_CFLAGS += -xMIC-AVX512 -fma
	  #TODO SVENK: make fortran code compatible with these flags
      COMPILER_LFLAGS += -xMIC-AVX512 -fma
    else ifeq ($(call tolower,$(ARCHITECTURE)),hsw)
      COMPILER_CFLAGS += -xCORE-AVX2 -fma
      #FCOMPILER_CFLAGS += -xCORE-AVX2 -fma 
	  #TODO SVENK: make fortran code compatible with these flags
      COMPILER_LFLAGS += -xCORE-AVX2 -fma
    else
      $(error Unknown option for variable [ARCHITECTURE])
    endif

else ifeq ($(call tolower,$(COMPILER)),gnu)
	# Settings for the GNU Compiler
	# --------------------------------------------
	ifndef EXAHYPE_CC 
	ifeq ($(call tolower,$(DISTRIBUTEDMEM)),mpi)
	    CC=mpicc
	  else
	    CC=g++
	  endif
	else
	  CC=$(EXAHYPE_CC)
	endif
	ifndef EXAHYPE_FC
	ifeq ($(call tolower,$(DISTRIBUTEDMEM)),mpi)
	    FC=mpifort
	  else
	    FC=gfortran
	  endif
	else
	  FC=$(EXAHYPE_FC)
	endif
	
	FCOMPILER_CFLAGS  += -fdefault-real-8 -fdefault-double-8 -ffree-line-length-none -cpp -Wall -Wno-tabs -Wno-unused-variable
	# REMOVING "-O3"  as it creates compiler bugs!
	LINK_FORTRAN      += -lgfortran

	COMPILER_CFLAGS   += -std=c++11 -pedantic -Wall -Drestrict=__restrict__ -pipe -D__assume_aligned=__builtin_assume_aligned -Wstrict-aliasing 
	COMPILER_LFLAGS   += -lm -lstdc++
	
	ifeq ($(call tolower,$(MODE)),debug)
	  COMPILER_CFLAGS  += -O0 -ggdb
          FCOMPILER_CFLAGS += -ggdb
	else ifeq ($(call tolower,$(MODE)),asserts)
          COMPILER_CFLAGS += -g3
          COMPILER_LFLAGS += -g3
          FCOMPILER_CFLAGS += -g3
	else ifeq ($(call tolower,$(MODE)),profile)
          COMPILER_CFLAGS += -p -pg -g3 -O3
          COMPILER_LFLAGS += -p -pg
	else
	  COMPILER_CFLAGS   += -O3
          FCOMPILER_CFLAGS  += -O3
	endif

	ifeq ($(call tolower,$(SHAREDMEM)),tbb)
	  COMPILER_CFLAGS += 
	  COMPILER_LFLAGS += 
	else
	  COMPILER_CFLAGS   += -fno-rtti
	  COMPILER_LFLAGS   += -lrt
	endif

	ifeq ($(call tolower,$(ARCHITECTURE)),cpu)
	  COMPILER_CFLAGS += 
	  COMPILER_LFLAGS += 
	else ifeq ($(call tolower,$(ARCHITECTURE)),cpu)
	  COMPILER_CFLAGS += -march=native
	  COMPILER_LFLAGS += -march=native
	else ifeq ($(call tolower,$(ARCHITECTURE)),knl)
	  COMPILER_CFLAGS += -march=knl
	  COMPILER_LFLAGS += -march=knl
	else ifeq ($(call tolower,$(ARCHITECTURE)),hsw)
	  COMPILER_CFLAGS += -march=haswell
      COMPILER_LFLAGS += -march=haswell
    else
	  $(error Unknown option for variable [ARCHITECTURE])
	endif

else ifeq ($(call tolower,$(COMPILER)),manual)
	ifndef EXAHYPE_CC
	  $(error COMPILER=manual at least requries user to set EXAHYPE_CC)
	else
	  CC=$(EXAHYPE_CC)
	endif
	FC=$(EXAHYPE_FC)
else
	# Option not recognized
	# ----------------------------------------------
	$(error Unknown option for variable [COMPILER])
endif






# Include files defining $CSOURCES, $FSOURCES, $FMODULESOURCES.
# They are generated by the make target "cfiles.mk" and "ffiles.mk" and contain the
# list of files to compile. Remember that make evaluates expressions lazy, thus we can
# do an include here but run the make target afterwards to fill the actual files.
-include cfiles.mk
-include ffiles.mk



# Configure System
# ----------------
# These values are used to assemble the symbols SYSTEM_CFLAGS and SYSTEM_LFLAGS.
INCLUDE_TBB=$(TBB_INC)
#INCLUDE_OMP=$(OMP_INC)
INCLUDE_OMP=-fopenmp
INCLUDE_MPI=$(MPI_INC)


LINK_TBB=$(TBB_SHLIB)
#LINK_OMP=$(OMP_SHLIB)
LINK_OMP=-fopenmp
#LINK_MPI=-lpthread -lrt -lmpich
LINK_MPI=



# Assemble compiler flags for shared-memory parallelization
# -----------------------
ifeq ($(call tolower,$(SHAREDMEM)),omp)
	SYSTEM_CFLAGS   += $(INCLUDE_OMP)
	SYSTEM_LFLAGS   += $(LINK_OMP)
	COMPILER_CFLAGS += -DSharedOMP
else ifeq ($(call tolower,$(SHAREDMEM)),tbb)
	SYSTEM_CFLAGS   += $(INCLUDE_TBB)
	SYSTEM_LFLAGS   += $(LINK_TBB)
	COMPILER_CFLAGS += -DSharedTBB
endif


ifeq ($(call tolower,$(DISTRIBUTEDMEM)),mpi)
	COMPILER_CFLAGS += -DParallel
endif



# Assemble Compiler Flags for boundary conditions
# -----------------------
ifeq ($(call tolower,$(BOUNDARYCONDITIONS)),periodic)
	COMPILER_CFLAGS += -DPeriodicBC
endif

#
# Linker Settings
# ---------------
# By default, I use the compiler command. But you might wanna modify it.
LL=$(CC)


COBJECTS=$(CSOURCES:.cpp=.o)
FOBJECTS=$(FSOURCES:.f90=.o)
FMODULEOBJECTS=$(FMODULESOURCES:.f90=.o)
FOBJECTS+=$(FMODULEOBJECTS)


# 
# Project-wise Settings
# ---------------------
# Projects can easily hook in with the variables
#
#  PROJECT_CFLAGS   (eg. for additional include paths)
#  PROJECT_LFLAGS   (eg. for additional dynamic links)
#  PROJECT_LINK     (eg. for additional objects/archives, ia. static links)
#
# As everywhere in GNU Make, these variables can be passed as arguments to make,
#  > make PROJECT_CFLAGS=-I/my/initial/data/code/ PROJECT_LINK=/my/initial/data/code.a
# or set as environment variables.
#

all: wellsortedbuild

# try this for serial making. Parallel make of header, buildinfo and build
# results in all text over each other and a race condition when buildinfo
# has not finished yet but the main.cpp code is already compiling.

wellsortedbuild: header buildinfo ffiles.mk cfiles.mk
ifneq ($(FMODULEOBJECTS),)
		$(MAKE) $(FMODULEOBJECTS)
endif
	$(MAKE) build


buildinfo:
#	add any variables which you want to be able to see
#	in the binary when calling with --version:
	@echo "generate-buildinfo.sh > $(PROJECT_PATH)/buildinfo.h"
	@$(EXAHYPE_PATH)/generate-buildinfo.sh \
		"COMPILER = $(COMPILER)" \
		"MODE = $(MODE)" \
		"SHAREDMEM = $(SHAREDMEM)" \
		"DISTRIBUTEDMEM = $(DISTRIBUTEDMEM)" \
		"------" \
		"ARCHITECTURE = $(ARCHITECTURE)" \
		"CC = $(CC)" \
		"BOUNDARYCONDITIONS = $(BOUNDARYCONDITIONS)" \
		"------" \
		"EXAHYPE_PATH = $(EXAHYPE_PATH)" \
		"PROJECT_PATH = $(PROJECT_PATH)" \
		"PEANO_KERNEL_PEANO_PATH = $(PEANO_KERNEL_PEANO_PATH)" \
		"PEANO_KERNEL_TARCH_PATH = $(PEANO_KERNEL_TARCH_PATH)" \
		"------" \
		"PROJECT_CFLAGS = $(PROJECT_CFLAGS)" \
		"PROJECT_LFLAGS = $(PROJECT_LFLAGS)" \
		> $(PROJECT_PATH)/buildinfo.h
#	touch the file which loads the buildinfo in order to have the most recent ones
	touch $(EXAHYPE_PATH)/exahype/Version.cpp
	
		
cfiles.mk:
	touch cfiles.mk
	echo -n CSOURCES= > cfiles.mk
	find -H $(PEANO_KERNEL_PEANO_PATH) -name '*.cpp' | tr '\n' ' ' >> cfiles.mk
	find -H $(PEANO_KERNEL_TARCH_PATH) -name '*.cpp' | tr '\n' ' ' >> cfiles.mk
	find -H $(PEANO_TOOLBOX_MULTISCALELINKEDCELL_PATH) -name '*.cpp' | tr '\n' ' ' >> cfiles.mk
	find -H $(PEANO_TOOLBOX_SHAREDMEMORY_ORACLES_PATH) -name '*.cpp' | tr '\n' ' ' >> cfiles.mk
	find -H $(PEANO_TOOLBOX_MPI_BLANCING_PATH) -name '*.cpp' | tr '\n' ' ' >> cfiles.mk
	find -H $(EXAHYPE_PATH) -name '*.cpp'  -not -path $(EXAHYPE_PATH)'/kernels/aderdg/generic/fortran/*' -not -path $(EXAHYPE_PATH)'/kernels/aderdg/optimised/*' | tr '\n' ' ' >> cfiles.mk
	find -L $(PROJECT_PATH) -name '*.cpp' | tr '\n' ' ' >> cfiles.mk
ifneq (,$(findstring Yes, $(MIXEDLANG) ))  
	find -H $(EXAHYPE_PATH)'/kernels/aderdg/generic/fortran' -name '*.cpp' | tr '\n' ' ' >> cfiles.mk
endif
ifneq (,$(findstring Yes, $(COMPILE_OPT_KERNEL) )) 
	find -H $(EXAHYPE_PATH)'/kernels/aderdg/optimised' -name '*.cpp' | tr '\n' ' ' >> cfiles.mk
endif

ffiles.mk:
	touch ffiles.mk
#	ALLOW mixed language Fortran applications! C.f. SRHD, MHD, Z4
#	To resolve the dependency problem  (compile fortran modules first), we detect them heuristically.
	echo -n FMODULESOURCES= > ffiles.mk
	find -L $(PROJECT_PATH) -name '*.f90' -exec grep -il module {} \+ | tr '\n'  ' ' >> ffiles.mk
	echo >> ffiles.mk
	echo -n FSOURCES= >> ffiles.mk
	find -L $(PROJECT_PATH) -name '*.f90' -exec grep -iL module {} \+ | tr '\n' ' ' >> ffiles.mk
#	 only for the projects which use the old fortran kernels:
ifneq (,$(findstring Yes, $(MIXEDLANG) ))
	find -L $(EXAHYPE_PATH) -name '*.f90' | tr '\n' ' ' >> ffiles.mk
endif

	
header:
	@echo ================================
	@echo
	@echo  www.exahype.eu
	@echo
	@echo ================================
	@echo
	@echo The project has received funding from the European Union\'s
	@echo Horizon 2020 research and innovation programme under grant
	@echo agreement No 671698 \(ExaHyPE\). It is based upon the PDE
	@echo framework Peano \(www.peano-framework.org\).
	@echo 
	@echo ================================
	@echo "Building ExaHyPE with options:"
	@echo ================================
	@echo "COMPILER       | $(COMPILER)"
	@echo "MODE           | $(MODE)"
	@echo "SHAREDMEM      | $(SHAREDMEM)"
	@echo "DISTRIBUTEDMEM | $(DISTRIBUTEDMEM)"
	@echo "ARCHITECTURE   | $(ARCHITECTURE)"
	@echo ================================
	@echo
	@echo ExaHyPE uses environment variables EXAHYPE_CC and EXAHYPE_FC for the 
	@echo C++ or Fortran compiler, respectively. If you haven\'t defined EXAHYPE_CC
	@echo and EXAHYPE_FC yourself, ExaHyPE defaults them to mpiicpc, icpc, g++, and 
	@echo so forth depending on the value of COMPILER. If you specify your own 
	@echo compilers via EXAHYPE_CC and EXAHYPE_FC, please specify a valid COMPILER 
	@echo mode nevertheless.
	@echo
	@echo The compiler variant does set various compiler flags that can be 
	@echo augmented by setting COMPILER_FLAGS before you invoke make. If you select
	@echo the compiler manual, ExaHyPE does not set any COMPILER_CFLAGS, i.e. it is up 
	@echo to you to initialise all compiler settings. Notably, you have to specify 
	@echo through EXAHYPE_CC which compiler to use.

build:  $(FOBJECTS) $(COBJECTS)
	$(LL) $(COBJECTS) $(FOBJECTS) $(PROJECT_LINK) $(LINK_FORTRAN) -o $(EXECUTABLE) $(PROJECT_LFLAGS) $(COMPILER_LFLAGS) $(SYSTEM_LFLAGS)
	@echo
	@echo build of ExaHyPE successful
	@echo run code with ./$(EXECUTABLE) and pass config file as argument

clean:
	rm -f $(EXECUTABLE)
	rm -f $(FOBJECTS)
	rm -f ffiles.mk
	rm -f $(COBJECTS)
	rm -f cfiles.mk
	rm -f buildinfo.h


$(COBJECTS): %.o : %.cpp
	$(CC) $(PROJECT_CFLAGS) $(COMPILER_CFLAGS) $(SYSTEM_CFLAGS) -I$(PROJECT_PATH) -I$(PEANO_TOOLBOX_MPI_BLANCING_PATH)/.. -I$(PEANO_TOOLBOX_SHAREDMEMORY_ORACLES_PATH)/.. -I$(PEANO_TOOLBOX_MULTISCALELINKEDCELL_PATH)/.. -I$(PEANO_KERNEL_PEANO_PATH)/.. -I$(PEANO_KERNEL_TARCH_PATH)/.. -I$(EXAHYPE_PATH) -I$(PROJECT_PATH)  -c $< -o $@

$(FOBJECTS): %.o : %.f90
	$(FC) $(PROJECT_CFLAGS) $(FCOMPILER_CFLAGS) -I$(PROJECT_PATH) -c $< -o $@
