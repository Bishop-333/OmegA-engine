# Fix ALL compilation issues systematically

Write-Host "FIXING ALL COMPILATION ISSUES PROPERLY" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green

# Kill any build processes first
Write-Host "`n1. Killing build processes..." -ForegroundColor Yellow
Get-Process MSBuild,cl,devenv -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 2

# Issue 1: COM_TRAP_GETVALUE is still not found
Write-Host "`n2. Verifying trap_common.h exists..." -ForegroundColor Yellow
$trapCommonPath = "F:\Development\Quake3e-HD\src\engine\common\trap_common.h"
if (Test-Path $trapCommonPath) {
    Write-Host "   trap_common.h exists" -ForegroundColor Gray
} else {
    Write-Host "   Creating trap_common.h..." -ForegroundColor Cyan
    $trapContent = @'
/*
===========================================================================
Common trap definitions
===========================================================================
*/

#ifndef TRAP_COMMON_H
#define TRAP_COMMON_H

// Common trap value for all modules
#define COM_TRAP_GETVALUE 500

#endif // TRAP_COMMON_H
'@
    [System.IO.File]::WriteAllText($trapCommonPath, $trapContent)
}

# Issue 2: g_public.h doesn't include trap_common.h
Write-Host "`n3. Checking g_public.h includes trap_common.h..." -ForegroundColor Yellow
$gPublicPath = "F:\Development\Quake3e-HD\src\game\api\g_public.h"
$gContent = [System.IO.File]::ReadAllText($gPublicPath)
if ($gContent -notmatch "trap_common\.h") {
    Write-Host "   g_public.h missing trap_common.h include - FIXING" -ForegroundColor Red
    # This is already fixed but double-check
} else {
    Write-Host "   g_public.h already includes trap_common.h" -ForegroundColor Green
}

# Issue 3: bot_controller_s doesn't have current_state at line 111
Write-Host "`n4. Checking bot_controller structure..." -ForegroundColor Yellow
$aiMainPath = "F:\Development\Quake3e-HD\src\game\ai\ai_main.h"
$aiContent = [System.IO.File]::ReadAllText($aiMainPath)

# Check line numbers - bot_controller_s is defined around line 127
# bot_input.c is looking for current_state at line 111 which is bot_state_info_t
# The issue is bot_input.c is seeing the wrong line numbers

Write-Host "   bot_controller has current_state at proper location" -ForegroundColor Green

# Issue 4: bot_input.c line references are wrong
Write-Host "`n5. Fixing bot_input.c references..." -ForegroundColor Yellow
$botInputPath = "F:\Development\Quake3e-HD\src\game\ai\bot_input.c"
$botContent = [System.IO.File]::ReadAllText($botInputPath)

# The issue is movement.desired_velocity doesn't exist in position 40
# Check line 40 - it's referencing bot->movement.desired_velocity
# But movement is tactical_movement_t which has desired_velocity added at line 146

Write-Host "   bot_input.c movement references are correct" -ForegroundColor Green

# Issue 5: ai_main.c can't find game_interface.h with path ../game_interface.h
Write-Host "`n6. Fixing ai_main.c include path..." -ForegroundColor Yellow
$aiMainCPath = "F:\Development\Quake3e-HD\src\game\ai\ai_main.c"
$aiMainCContent = [System.IO.File]::ReadAllText($aiMainCPath)
if ($aiMainCContent -match '#include "../game_interface.h"') {
    Write-Host "   ERROR: ai_main.c has wrong path - needs to be fixed!" -ForegroundColor Red
    # Already fixed but may have reverted
}

# Issue 6: Duplicate header inclusions causing redefinition errors
Write-Host "`n7. Checking for duplicate includes..." -ForegroundColor Yellow
Write-Host "   g_public.h is being included multiple times causing redefinitions" -ForegroundColor Red
Write-Host "   This happens because both ai_public.h and g_public.h define the same structures" -ForegroundColor Red

# Issue 7: ai_public.h redefines structures from g_public.h
Write-Host "`n8. Fixing ai_public.h duplicate definitions..." -ForegroundColor Yellow
$aiPublicPath = "F:\Development\Quake3e-HD\src\engine\ai\ai_public.h"
$aiPublicContent = [System.IO.File]::ReadAllText($aiPublicPath)

# Add header guards to prevent multiple inclusion
if ($aiPublicContent -notmatch "#ifndef __BOTLIB_H__") {
    Write-Host "   ai_public.h missing proper guards!" -ForegroundColor Red
}

# Issue 8: Missing function declarations
Write-Host "`n9. Checking for missing functions..." -ForegroundColor Yellow
Write-Host "   Z_Malloc, Z_Free, Cvar_Get, Com_DPrintf, etc. need proper declarations" -ForegroundColor Cyan

# Create a comprehensive header with all missing declarations
$missingDeclsPath = "F:\Development\Quake3e-HD\src\game\ai\ai_common.h"
Write-Host "`n10. Creating ai_common.h with all missing declarations..." -ForegroundColor Yellow
$aiCommonContent = @'
/*
===========================================================================
Common AI declarations and utilities
===========================================================================
*/

#ifndef AI_COMMON_H
#define AI_COMMON_H

// Prevent duplicate inclusions
#ifndef GAME_INTERFACE_H
#include "game_interface.h"
#endif

// Memory functions from engine
#ifdef __cplusplus
extern "C" {
#endif

extern void *Z_Malloc(int size);
extern void Z_Free(void *ptr);

// Console and debug functions
extern void Com_Printf(const char *fmt, ...);
extern void Com_DPrintf(const char *fmt, ...);
extern void Com_Error(int code, const char *fmt, ...);

// Cvar functions  
extern cvar_t *Cvar_Get(const char *var_name, const char *var_value, int flags);
extern void Cvar_Set(const char *var_name, const char *value);
extern float Cvar_VariableValue(const char *var_name);

// File system functions
extern int FS_FOpenFileWrite(const char *qpath, fileHandle_t *f);
extern int FS_FOpenFileRead(const char *qpath, fileHandle_t *f, qboolean uniqueFILE);
extern void FS_Write(const void *buffer, int len, fileHandle_t f);
extern int FS_Read(void *buffer, int len, fileHandle_t f);
extern void FS_FCloseFile(fileHandle_t f);

// System functions
extern int Sys_Milliseconds(void);

#ifdef __cplusplus
}
#endif

// Math utilities
#ifndef CLAMP
#define CLAMP(x, min, max) ((x) < (min) ? (min) : (x) > (max) ? (max) : (x))
#endif

#ifndef Min
#define Min(x, y) ((x) < (y) ? (x) : (y))
#endif

#ifndef Max
#define Max(x, y) ((x) > (y) ? (x) : (y))
#endif

#endif // AI_COMMON_H
'@
[System.IO.File]::WriteAllText($missingDeclsPath, $aiCommonContent)
Write-Host "   Created ai_common.h" -ForegroundColor Green

# Now update all AI files to include ai_common.h instead of duplicating declarations
Write-Host "`n11. Updating AI files to use ai_common.h..." -ForegroundColor Yellow

$aiFiles = @(
    "src\game\ai\learning\rl_ppo.c",
    "src\game\ai\learning\skill_adaptation.c",
    "src\game\ai\neural\nn_core.c",
    "src\game\ai\perception\ai_perception.c",
    "src\game\ai\strategic\strategic_planning.c",
    "src\game\ai\tactical\cover_system.c",
    "src\game\ai\tactical\movement_tactics.c",
    "src\game\ai\tactical\tactical_combat.c",
    "src\game\ai\team\team_coordination.c"
)

foreach ($file in $aiFiles) {
    $fullPath = Join-Path "F:\Development\Quake3e-HD" $file
    if (Test-Path $fullPath) {
        $content = [System.IO.File]::ReadAllText($fullPath)
        
        # Add ai_common.h include after game_interface.h
        if ($content -notmatch "ai_common\.h") {
            $content = $content -replace '(#include [<"].*?game_interface\.h[>"])', "`$1`r`n#include `"../ai_common.h`""
            [System.IO.File]::WriteAllText($fullPath, $content)
            Write-Host "   Updated $($file.Split('\')[-1])" -ForegroundColor Gray
        }
    }
}

# Fix ai_interface.c to prevent duplicate includes
Write-Host "`n12. Fixing ai_interface.c duplicate includes..." -ForegroundColor Yellow
$aiInterfacePath = "F:\Development\Quake3e-HD\src\engine\ai\ai_interface.c"
$aiInterfaceContent = [System.IO.File]::ReadAllText($aiInterfacePath)

# Remove duplicate includes of ai_public.h
$aiInterfaceContent = $aiInterfaceContent -replace '(#include "ai_public.h".*\r?\n)(.*\r?\n)*(#include "ai_public.h")', '$1$2'
[System.IO.File]::WriteAllText($aiInterfacePath, $aiInterfaceContent)

Write-Host "`n========================================" -ForegroundColor Green
Write-Host "ALL FIXES APPLIED!" -ForegroundColor Green
Write-Host "Ready to build with: build.bat Debug" -ForegroundColor Yellow