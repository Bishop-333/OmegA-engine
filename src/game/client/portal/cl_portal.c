/*
===========================================================================
Portal System - Client Commands
Handles client-side portal commands
===========================================================================
*/

#include "../../common/q_shared.h"
#include "../cl_client.h"

/*
================
CL_FirePortal_f

Client command to fire a portal
================
*/
void CL_FirePortal_f(void) {
    char arg[MAX_TOKEN_CHARS];
    
    if (Cmd_Argc() < 2) {
        Com_Printf("Usage: fireportal <orange|blue>\n");
        return;
    }
    
    Cmd_Argv(1, arg, sizeof(arg));
    
    // Send command to server
    Cbuf_AddText(va("cmd fireportal %s\n", arg));
}

/*
================
CL_ClosePortals_f

Client command to close all portals
================
*/
void CL_ClosePortals_f(void) {
    Cbuf_AddText("cmd closeportals\n");
}

/*
================
CL_PortalDebug_f

Client command for portal debug info
================
*/
void CL_PortalDebug_f(void) {
    Cbuf_AddText("cmd portaldebug\n");
}

/*
================
CL_PortalStats_f

Client command for portal statistics
================
*/
void CL_PortalStats_f(void) {
    Cbuf_AddText("cmd portalstats\n");
}

/*
================
CL_InitPortalCommands

Register client-side portal commands
================
*/
void CL_InitPortalCommands(void) {
    Cmd_AddCommand("fireportal", CL_FirePortal_f);
    Cmd_AddCommand("closeportals", CL_ClosePortals_f);
    Cmd_AddCommand("portaldebug", CL_PortalDebug_f);
    Cmd_AddCommand("portalstats", CL_PortalStats_f);
    
    Com_Printf("^2Portal client commands registered\n");
}

/*
================
CL_ShutdownPortalCommands

Unregister client-side portal commands
================
*/
void CL_ShutdownPortalCommands(void) {
    Cmd_RemoveCommand("fireportal");
    Cmd_RemoveCommand("closeportals");
    Cmd_RemoveCommand("portaldebug");
    Cmd_RemoveCommand("portalstats");
}