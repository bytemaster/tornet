include_directories( include )
include_directories( ../move/include )

IF( UNIX )
	IF( ENABLE_ARM )
		SET_PROPERTY( SOURCE 
			      src/asm/fcontext_arm_aapcs_elf_gas.S 
			      PROPERTY LANGUAGE C)
		SET( sources src/protected_stack_posix.cpp 
			     src/stack_helper_posix.cpp 
			     src/asm/fcontext_arm_aapcs_elf_gas.S 
			     )
	ELSE()

		IF( APPLE )
        SET_PROPERTY( SOURCE 
                src/asm/fcontext_x86_64_sysv_macho_gas.S 
                PROPERTY LANGUAGE C)
        SET( sources src/stack_utils_posix.cpp
                     src/stack_allocator_posix.cpp
                     src/fcontext.cpp
                     src/asm/fcontext_x86_64_sysv_macho_gas.S 
               )
		ELSE (APPLE)
    IF( CMAKE_SIZEOF_VOID_P EQUAL 4 )
        SET_PROPERTY( SOURCE 
                src/asm/fcontext_i386_sysv_elf_gas.S
                PROPERTY LANGUAGE C)
      SET( fcontext_asm src/asm/fcontext_i386_sysv_elf_gas.S )
    ELSE( )
			SET( fcontext_asm src/asm/fcontext_x86_64_sysv_elf_gas.S )
			SET_PROPERTY( SOURCE 
			    		      src/asm/fcontext_x86_64_sysv_elf_gas.S
				    	      PROPERTY LANGUAGE C)
    ENDIF( )

    SET( sources src/stack_utils_posix.cpp
                 src/stack_allocator_posix.cpp
                 src/fcontext.cpp
			           ${fcontext_asm}
			     )

		ENDIF(APPLE)

	ENDIF()
ENDIF( UNIX )

IF( WIN32 )

ENABLE_LANGUAGE(ASM_MASM) 

SET( sources src/fcontext.cpp
             src/seh.cpp
             src/stack_allocator_windows.cpp
             src/stack_utils_windows.cpp 
             src/seh.cpp 
             src/asm/fcontext_i386_ms_pe_masm.asm
             )
ENDIF(WIN32)

SETUP_LIBRARY( boost_context SOURCES ${sources}
                             LIBRARIES 
                             LIBRARY_TYPE STATIC )

ADD_SUBDIRECTORY( example )

install( DIRECTORY include/boost/context DESTINATION include/boost )
