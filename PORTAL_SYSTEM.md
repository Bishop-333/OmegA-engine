# Portal System for Quake3e-HD

## Quick Start

1. **Run the game with portals:**
   ```
   run_portal_test.bat
   ```

2. **Or manually in-game:**
   - Open console (`~` key)
   - Type: `exec autoexec_portal.cfg`

## Controls

| Action | Primary Key | Alternative | Console Command |
|--------|------------|-------------|----------------|
| Orange Portal | MOUSE1 | Q | `fireportal_orange` |
| Blue Portal | MOUSE2 | E | `fireportal_blue` |
| Close Portals | MOUSE3 | R | `closeportals` |
| Portal Pair | - | T | `portal_pair` |
| Debug Info | - | F9 | `portaldebug` |
| Portal Stats | - | F10 | `portalstats` |

## How It Works

### Server Commands
The portal system runs server-side. All commands must be sent to the server using the `cmd` prefix:
- `cmd fireportal orange` - Fires an orange portal
- `cmd fireportal blue` - Fires a blue portal
- `cmd closeportals` - Closes all portals

### Aliases
The configuration file creates aliases that automatically add the `cmd` prefix:
- `fireportal_orange` → `cmd fireportal orange`
- `fireportal_blue` → `cmd fireportal blue`
- `closeportals` → `cmd closeportals`

## Portal Mechanics

1. **Portal Creation:**
   - Fire at any solid surface
   - Portal appears where projectile hits
   - Maximum range: 4096 units

2. **Portal Linking:**
   - Orange and blue portals auto-link when both exist
   - See "^2Portals LINKED!" message when connected
   - Walk through one portal to exit the other

3. **Portal Properties:**
   - Radius: 32 units
   - Activation time: 1 second
   - Fall damage immunity: 3 seconds after exit

## Troubleshooting

### "Unknown command fireportal"
**Solution:** Commands must be sent to server. Either:
- Use the aliases: `fireportal_orange`, `fireportal_blue`
- Add `cmd` prefix: `cmd fireportal orange`
- Execute the config: `exec autoexec_portal.cfg`

### Portals Not Appearing
**Check:**
1. You're in a game (not main menu)
2. Surface is valid (not sky or water)
3. Within range (4096 units)
4. Server has portal module loaded

### Portals Not Linking
**Verify:**
- Both orange AND blue portals placed
- Same player owns both portals
- Look for "Portals LINKED!" message

## Debug Commands

- `cmd portaldebug` - Show all portal information
- `cmd portalstats` - Display portal statistics
- `g_portalDebug 1` - Enable debug mode

## Files

- `baseq3/autoexec_portal.cfg` - Main portal configuration
- `baseq3/portal_commands.cfg` - Alternative command setup
- `src/game/server/portal/` - Portal system source code
- `run_portal_test.bat` - Quick start script

## Development

The portal system consists of:
- **Game Module** (`g_portal*.c`) - Server-side portal logic
- **Fixed Implementation** (`g_portal_fixed.c`) - Improved portal mechanics
- **Syscalls** (`g_syscalls.c`) - Engine interface
- **Utilities** (`g_portal_utils.c`) - Math and validation

## Known Issues

1. Portal rendering is placeholder (shows as colored sphere)
2. Velocity transformation simplified
3. No portal gun model (uses plasma gun)
4. Client prediction not implemented

## Testing

1. Start game: `run_portal_test.bat`
2. Fire orange portal: `MOUSE1`
3. Fire blue portal: `MOUSE2`
4. Check console for "Portals LINKED!" message
5. Walk through portal to teleport
6. Press `F10` for statistics