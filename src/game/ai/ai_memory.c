/*
===========================================================================
Copyright (C) 2024 Quake3e-HD Project

Memory allocation for AI modules
===========================================================================
*/

#include <stdlib.h>
#include <stdio.h>

// Undefine macros if they exist
#ifdef Z_Malloc
#undef Z_Malloc
#endif
#ifdef Z_Free
#undef Z_Free
#endif

// Simple memory allocation wrappers for AI modules
void *Z_Malloc(int size) {
    void *ptr = calloc(1, size);
    if (!ptr) {
        fprintf(stderr, "AI: Z_Malloc failed on allocation of %i bytes\n", size);
        exit(1);
    }
    return ptr;
}

void Z_Free(void *ptr) {
    if (ptr) {
        free(ptr);
    }
}