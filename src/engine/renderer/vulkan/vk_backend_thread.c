/*
===========================================================================
Copyright (C) 2024 Quake3e-HD Project

This file is part of Quake3e-HD.

Quake3e-HD is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.
===========================================================================
*/
// vk_backend_thread.c - Vulkan backend thread support

#include "vk.h"

#ifdef _WIN32
#include <windows.h>
#include <process.h>
#else
#include <pthread.h>
#endif

typedef struct {
    qboolean initialized;
    qboolean shouldTerminate;
    qboolean isRunning;
    
#ifdef _WIN32
    HANDLE thread;
    HANDLE workSemaphore;
    HANDLE doneSemaphore;
    CRITICAL_SECTION commandMutex;
#else
    pthread_t thread;
    pthread_mutex_t commandMutex;
    pthread_cond_t workCond;
    pthread_cond_t doneCond;
#endif
    
    // Command buffer for backend thread
    VkCommandPool commandPool;
    VkCommandBuffer commandBuffer;
    VkFence fence;
    
    // Work queue
    void (*workFunction)(void*);
    void *workData;
    qboolean hasWork;
} backendThread_t;

static backendThread_t backendThread;

#if defined(_DEBUG)
static void backend_log_cmd(const char *action, VkCommandBuffer buffer, const char *tag) {
    if (buffer == VK_NULL_HANDLE) {
        return;
    }
    if (!tag) {
        tag = "";
    }
    ri.Printf(PRINT_DEVELOPER, "VK-BACKEND %s %p %s\n", action, (void*)buffer, tag);
}
#else
#define backend_log_cmd(action, buffer, tag) ((void)0)
#endif

// Forward declaration
void vk_shutdown_backend_thread( void );

/*
================
vk_backend_thread_function

Backend thread main loop
================
*/
#ifdef _WIN32
static unsigned __stdcall vk_backend_thread_function( void *arg )
#else
static void* vk_backend_thread_function( void *arg )
#endif
{
    (void)arg;
    
    backendThread.isRunning = qtrue;
    
    while ( !backendThread.shouldTerminate ) {
#ifdef _WIN32
        // Wait for work
        WaitForSingleObject( backendThread.workSemaphore, INFINITE );
        
        if ( backendThread.shouldTerminate ) {
            break;
        }
        
        EnterCriticalSection( &backendThread.commandMutex );
#else
        pthread_mutex_lock( &backendThread.commandMutex );
        
        while ( !backendThread.hasWork && !backendThread.shouldTerminate ) {
            pthread_cond_wait( &backendThread.workCond, &backendThread.commandMutex );
        }
        
        if ( backendThread.shouldTerminate ) {
            pthread_mutex_unlock( &backendThread.commandMutex );
            break;
        }
#endif
        
        // Execute work function if available
        if ( backendThread.workFunction && backendThread.hasWork ) {
            backendThread.workFunction( backendThread.workData );
            backendThread.workFunction = NULL;
            backendThread.workData = NULL;
            backendThread.hasWork = qfalse;
        }
        
#ifdef _WIN32
        LeaveCriticalSection( &backendThread.commandMutex );
        ReleaseSemaphore( backendThread.doneSemaphore, 1, NULL );
#else
        pthread_cond_signal( &backendThread.doneCond );
        pthread_mutex_unlock( &backendThread.commandMutex );
#endif
    }
    
    backendThread.isRunning = qfalse;
    
#ifdef _WIN32
    return 0;
#else
    return NULL;
#endif
}

/*
================
vk_initialize_backend_thread

Initialize backend thread for Vulkan rendering
================
*/
void vk_initialize_backend_thread( void ) {
    VkResult result;
    
    if ( backendThread.initialized ) {
        return;
    }
    
    Com_Memset( &backendThread, 0, sizeof(backendThread) );
    
    // Create command pool for backend thread
    VkCommandPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = vk.queue_family_index
    };
    
    result = vkCreateCommandPool( vk.device, &poolInfo, NULL, &backendThread.commandPool );
    if ( result != VK_SUCCESS ) {
        ri.Printf( PRINT_WARNING, "vk_initialize_backend_thread: Failed to create command pool\n" );
        return;
    }
    
    // Allocate command buffer
    VkCommandBufferAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = backendThread.commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };
    
    result = vkAllocateCommandBuffers( vk.device, &allocInfo, &backendThread.commandBuffer );
    if ( result != VK_SUCCESS ) {
        ri.Printf( PRINT_WARNING, "vk_initialize_backend_thread: Failed to allocate command buffer\n" );
        vkDestroyCommandPool( vk.device, backendThread.commandPool, NULL );
        backendThread.commandPool = VK_NULL_HANDLE;
        return;
    }
    backend_log_cmd("alloc", backendThread.commandBuffer, "backend");
    vk_cmd_register("backend_thread", backendThread.commandBuffer, backendThread.commandPool);
    
    // Create fence for synchronization
    VkFenceCreateInfo fenceInfo = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT
    };
    
    result = vkCreateFence( vk.device, &fenceInfo, NULL, &backendThread.fence );
    if ( result != VK_SUCCESS ) {
        ri.Printf( PRINT_WARNING, "vk_initialize_backend_thread: Failed to create fence\n" );
        vkDestroyCommandPool( vk.device, backendThread.commandPool, NULL );
        backendThread.commandPool = VK_NULL_HANDLE;
        return;
    }
    
#ifdef _WIN32
    // Initialize Windows synchronization primitives
    InitializeCriticalSection( &backendThread.commandMutex );
    backendThread.workSemaphore = CreateSemaphore( NULL, 0, 1, NULL );
    backendThread.doneSemaphore = CreateSemaphore( NULL, 0, 1, NULL );
    
    if ( !backendThread.workSemaphore || !backendThread.doneSemaphore ) {
        ri.Printf( PRINT_WARNING, "vk_initialize_backend_thread: Failed to create semaphores\n" );
        vk_shutdown_backend_thread();
        return;
    }
    
    // Create thread
    backendThread.thread = (HANDLE)_beginthreadex( NULL, 0, vk_backend_thread_function, NULL, 0, NULL );
    if ( !backendThread.thread ) {
        ri.Printf( PRINT_WARNING, "vk_initialize_backend_thread: Failed to create thread\n" );
        vk_shutdown_backend_thread();
        return;
    }
#else
    // Initialize POSIX synchronization primitives
    pthread_mutex_init( &backendThread.commandMutex, NULL );
    pthread_cond_init( &backendThread.workCond, NULL );
    pthread_cond_init( &backendThread.doneCond, NULL );
    
    // Create thread
    if ( pthread_create( &backendThread.thread, NULL, vk_backend_thread_function, NULL ) != 0 ) {
        ri.Printf( PRINT_WARNING, "vk_initialize_backend_thread: Failed to create thread\n" );
        vk_shutdown_backend_thread();
        return;
    }
#endif
    
    backendThread.initialized = qtrue;
    ri.Printf( PRINT_DEVELOPER, "Backend thread initialized\n" );
}

/*
================
vk_shutdown_backend_thread

Shutdown backend thread
================
*/
void vk_shutdown_backend_thread( void ) {
    if ( !backendThread.initialized ) {
        return;
    }
    
    // Signal thread to terminate
    backendThread.shouldTerminate = qtrue;
    
#ifdef _WIN32
    // Wake up thread and wait for termination
    if ( backendThread.thread ) {
        ReleaseSemaphore( backendThread.workSemaphore, 1, NULL );
        WaitForSingleObject( backendThread.thread, 5000 );
        CloseHandle( backendThread.thread );
    }
    
    // Cleanup synchronization objects
    if ( backendThread.workSemaphore ) {
        CloseHandle( backendThread.workSemaphore );
    }
    if ( backendThread.doneSemaphore ) {
        CloseHandle( backendThread.doneSemaphore );
    }
    DeleteCriticalSection( &backendThread.commandMutex );
#else
    // Wake up thread and wait for termination
    if ( backendThread.thread ) {
        pthread_mutex_lock( &backendThread.commandMutex );
        backendThread.hasWork = qtrue;
        pthread_cond_signal( &backendThread.workCond );
        pthread_mutex_unlock( &backendThread.commandMutex );
        
        pthread_join( backendThread.thread, NULL );
    }
    
    // Cleanup synchronization objects
    pthread_mutex_destroy( &backendThread.commandMutex );
    pthread_cond_destroy( &backendThread.workCond );
    pthread_cond_destroy( &backendThread.doneCond );
#endif
    
    // Cleanup Vulkan resources
    if ( backendThread.fence != VK_NULL_HANDLE ) {
        vkDestroyFence( vk.device, backendThread.fence, NULL );
        backendThread.fence = VK_NULL_HANDLE;
    }

    if ( backendThread.commandBuffer != VK_NULL_HANDLE && backendThread.commandPool != VK_NULL_HANDLE ) {
        backend_log_cmd("free", backendThread.commandBuffer, "backend_shutdown");
        vk_cmd_unregister(backendThread.commandBuffer);
        vkFreeCommandBuffers( vk.device, backendThread.commandPool, 1, &backendThread.commandBuffer );
        backendThread.commandBuffer = VK_NULL_HANDLE;
    }

    if ( backendThread.commandPool != VK_NULL_HANDLE ) {
        vkDestroyCommandPool( vk.device, backendThread.commandPool, NULL );
        backendThread.commandPool = VK_NULL_HANDLE;
    }
    
    Com_Memset( &backendThread, 0, sizeof(backendThread) );
    ri.Printf( PRINT_DEVELOPER, "Backend thread shutdown\n" );
}

/*
================
vk_submit_backend_work

Submit work to backend thread
================
*/
void vk_submit_backend_work( void (*function)(void*), void *data ) {
    if ( !backendThread.initialized || backendThread.shouldTerminate ) {
        // Execute synchronously if thread not available
        if ( function ) {
            function( data );
        }
        return;
    }
    
#ifdef _WIN32
    EnterCriticalSection( &backendThread.commandMutex );
    
    backendThread.workFunction = function;
    backendThread.workData = data;
    backendThread.hasWork = qtrue;
    
    LeaveCriticalSection( &backendThread.commandMutex );
    
    // Signal thread to start work
    ReleaseSemaphore( backendThread.workSemaphore, 1, NULL );
#else
    pthread_mutex_lock( &backendThread.commandMutex );
    
    backendThread.workFunction = function;
    backendThread.workData = data;
    backendThread.hasWork = qtrue;
    
    pthread_cond_signal( &backendThread.workCond );
    pthread_mutex_unlock( &backendThread.commandMutex );
#endif
}

/*
================
vk_wait_backend_work

Wait for backend thread to complete current work
================
*/
void vk_wait_backend_work( void ) {
    if ( !backendThread.initialized || !backendThread.isRunning ) {
        return;
    }
    
#ifdef _WIN32
    if ( backendThread.hasWork ) {
        WaitForSingleObject( backendThread.doneSemaphore, INFINITE );
    }
#else
    pthread_mutex_lock( &backendThread.commandMutex );
    while ( backendThread.hasWork ) {
        pthread_cond_wait( &backendThread.doneCond, &backendThread.commandMutex );
    }
    pthread_mutex_unlock( &backendThread.commandMutex );
#endif
}

/*
================
find_memory_type

Find appropriate memory type for allocation
================
*/
uint32_t find_memory_type( uint32_t typeFilter, VkMemoryPropertyFlags properties ) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties( vk.physical_device, &memProperties );
    
    for ( uint32_t i = 0; i < memProperties.memoryTypeCount; i++ ) {
        if ( (typeFilter & (1 << i)) && 
             (memProperties.memoryTypes[i].propertyFlags & properties) == properties ) {
            return i;
        }
    }
    
    ri.Error( ERR_FATAL, "Failed to find suitable memory type!" );
    return 0;
}
