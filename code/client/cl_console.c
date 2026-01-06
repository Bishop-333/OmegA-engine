/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
// console.c

#include "client.h"

// time that it takes for a notify line to fade away
#define NOTIFY_FADE_TIME 250

// time that console tab blinks when it receives a message
#define NOTIFY_BLINK_TIME 250

#define  DEFAULT_CONSOLE_WIDTH 78
#define  MAX_CONSOLE_WIDTH 120

#define  NUM_CON_TIMES 8
#define  MAX_CONSOLES 5

#define  CON_TEXTSIZE   65536

int bigchar_width;
int bigchar_height;
int mediumchar_width;
int mediumchar_height;
int smallchar_width;
int smallchar_height;

typedef struct {
	qboolean	initialized;

	short	text[CON_TEXTSIZE];
	int		current;		// line where next message will be printed
	int		x;				// offset in current line for next print
	int		display;		// bottom of console displays this line

	int 	linewidth;		// characters across screen
	int		totallines;		// total lines in console scrollback

	float	xadjust;		// for wide aspect screens

	float	displayFrac;	// aproaches finalFrac at scr_conspeed
	float	finalFrac;		// 0.0 to 1.0 lines of console to display
	float	userFrac;

	int		vislines;		// in scanlines

	int		times[NUM_CON_TIMES];	// cls.realtime time the line was generated
								// for transparent notify lines
	vec4_t	color;

	int		viswidth;
	int		vispage;		

	qboolean newline;

	char		*name;
	qboolean	notify;
	qboolean	active;
} console_t;

#define  CON_ALL	0
#define  CON_SYS	1
#define  CON_CHAT	2
#define  CON_TEAM	3
#define  CON_TELL	4
#define  DEFAULT_CON	CON_ALL

char *conName[] = {
	"all",
	"sys",
	"chat",
	"team",
	"tell"
};

const int conColors[] = {
	1,
	8,
	2,
	5,
	6
};

int numConsoles = MAX_CONSOLES;
int activeConsoleNum = DEFAULT_CON;
int lastSrvCmdNum = -1;

extern  qboolean	chat_team;
extern  int		chat_playerNum;

console_t	con[MAX_CONSOLES];
console_t	*activeCon = &con[DEFAULT_CON];

cvar_t		*con_conspeed;
cvar_t		*con_autoclear;
cvar_t		*con_notifytime;
cvar_t		*con_scale;
cvar_t		*con_fps;
cvar_t		*con_clock;
cvar_t		*con_drawHelp;

int			g_console_field_width;

/*
================
Con_AcceptLine
================
*/
void Con_AcceptLine( void ) {
	// if not in the game explicitly prepend a slash if needed
	if ( cls.state != CA_ACTIVE
		&& g_consoleField.buffer[0] != '\0'
		&& g_consoleField.buffer[0] != '\\'
		&& g_consoleField.buffer[0] != '/' ) {
		char	temp[MAX_EDIT_LINE-1];

		Q_strncpyz( temp, g_consoleField.buffer, sizeof( temp ) );
		Com_sprintf( g_consoleField.buffer, sizeof( g_consoleField.buffer ), "\\%s", temp );
		g_consoleField.cursor++;
	}

	Com_Printf( "]%s\n", g_consoleField.buffer );

	// leading slash is an explicit command
	if ( g_consoleField.buffer[0] == '\\' || g_consoleField.buffer[0] == '/' ) {
		Cbuf_AddText( g_consoleField.buffer+1 );	// valid command
		Cbuf_AddText( "\n" );
	} else {
		// other text will be chat messages
		if ( !g_consoleField.buffer[0] ) {
			return;	// empty lines just scroll the console without adding to history
		} else if ( activeCon == &con[CON_SYS] ) {
			Cbuf_AddText( "cmd " );
			Cbuf_AddText( g_consoleField.buffer );
			Cbuf_AddText( "\n" );
		} else if ( activeCon == &con[CON_TEAM] ) {
			Cbuf_AddText( "cmd say_team " );
			Cbuf_AddText( g_consoleField.buffer );
			Cbuf_AddText( "\n" );
		} else if ( activeCon == &con[CON_TELL] ) {
			Cbuf_AddText( "cmd tell " );
			Cbuf_AddText( g_consoleField.buffer );
			Cbuf_AddText( "\n" );
		} else {
			Cbuf_AddText( "cmd say " );
			Cbuf_AddText( g_consoleField.buffer );
			Cbuf_AddText( "\n" );
		}
	}

	// copy line to history buffer
	Con_SaveField( &g_consoleField );

	Field_Clear( &g_consoleField );
	g_consoleField.widthInChars = g_console_field_width;

	if ( cls.state == CA_DISCONNECTED ) {
		SCR_UpdateScreen ();	// force an update, because the command
	}							// may take some time
	return;
}


/*
================
Con_ToggleConsole_f
================
*/
void Con_ToggleConsole_f( void ) {
	// Can't toggle the console when it's the only thing available
    if ( cls.state == CA_DISCONNECTED && Key_GetCatcher() == KEYCATCH_CONSOLE ) {
		return;
	}

	if ( con_autoclear->integer ) {
		Field_Clear( &g_consoleField );
	}

	g_consoleField.widthInChars = g_console_field_width;

	Con_ClearNotify();
	Key_SetCatcher( Key_GetCatcher() ^ KEYCATCH_CONSOLE );
}


/*
================
Con_MessageMode_f
================
*/
static void Con_MessageMode_f( void ) {
	chat_playerNum = -1;
	chat_team = qfalse;
	Field_Clear( &chatField );
	chatField.widthInChars = 30;

	Key_SetCatcher( Key_GetCatcher() ^ KEYCATCH_MESSAGE );
}


/*
================
Con_MessageMode2_f
================
*/
static void Con_MessageMode2_f( void ) {
	chat_playerNum = -1;
	chat_team = qtrue;
	Field_Clear( &chatField );
	chatField.widthInChars = 25;
	Key_SetCatcher( Key_GetCatcher() ^ KEYCATCH_MESSAGE );
}


/*
================
Con_MessageMode3_f
================
*/
static void Con_MessageMode3_f( void ) {
	chat_playerNum = cgvm ? VM_Call( cgvm, 0, CG_CROSSHAIR_PLAYER ) : -1;
	if ( chat_playerNum < 0 || chat_playerNum >= MAX_CLIENTS ) {
		chat_playerNum = -1;
		return;
	}
	chat_team = qfalse;
	Field_Clear( &chatField );
	chatField.widthInChars = 30;
	Key_SetCatcher( Key_GetCatcher() ^ KEYCATCH_MESSAGE );
}


/*
================
Con_MessageMode4_f
================
*/
static void Con_MessageMode4_f( void ) {
	chat_playerNum = cgvm ? VM_Call( cgvm, 0, CG_LAST_ATTACKER ) : -1;
	if ( chat_playerNum < 0 || chat_playerNum >= MAX_CLIENTS ) {
		chat_playerNum = -1;
		return;
	}
	chat_team = qfalse;
	Field_Clear( &chatField );
	chatField.widthInChars = 30;
	Key_SetCatcher( Key_GetCatcher() ^ KEYCATCH_MESSAGE );
}


/*
================
Con_Clear_f
================
*/
static void Con_Clear_f( void ) {
	int		i;

	for ( i = 0 ; i < activeCon->linewidth ; i++ ) {
		activeCon->text[i] = ( ColorIndex( COLOR_WHITE ) << 8 ) | ' ';
	}

	activeCon->x = 0;
	activeCon->current = 0;
	activeCon->newline = qtrue;

	Con_Bottom();		// go to end
}

						
/*
================
Con_Dump_f

Save the console contents out to a file
================
*/
static void Con_Dump_f( void )
{
	int		l, x, i, n;
	short	*line;
	fileHandle_t	f;
	int		bufferlen;
	char	*buffer;
	char	filename[ MAX_OSPATH ];
	const char *ext;

	if ( Cmd_Argc() != 2 )
	{
		Com_Printf( "usage: condump <filename>\n" );
		return;
	}

	Q_strncpyz( filename, Cmd_Argv( 1 ), sizeof( filename ) );
	COM_DefaultExtension( filename, sizeof( filename ), ".txt" );

	if ( !FS_AllowedExtension( filename, qfalse, &ext ) ) {
		Com_Printf( "%s: Invalid filename extension '%s'.\n", __func__, ext );
		return;
	}

	f = FS_FOpenFileWrite( filename );
	if ( f == FS_INVALID_HANDLE )
	{
		Com_Printf( "ERROR: couldn't open %s.\n", filename );
		return;
	}

	Com_Printf( "Dumped console text to %s.\n", filename );

	if ( activeCon->current >= activeCon->totallines ) {
		n = activeCon->totallines;
		l = activeCon->current + 1;
	} else {
		n = activeCon->current + 1;
		l = 0;
	}

	bufferlen = activeCon->linewidth + ARRAY_LEN( Q_NEWLINE ) * sizeof( char );
	buffer = Hunk_AllocateTempMemory( bufferlen );

	// write the remaining lines
	buffer[ bufferlen - 1 ] = '\0';

	for ( i = 0; i < n ; i++, l++ ) 
	{
		line = activeCon->text + (l % activeCon->totallines) * activeCon->linewidth;
		// store line
		for( x = 0; x < activeCon->linewidth; x++ )
			buffer[ x ] = line[ x ] & 0xff;
		buffer[ activeCon->linewidth ] = '\0';
		// terminate on ending space characters
		for ( x = activeCon->linewidth - 1 ; x >= 0 ; x-- ) {
			if ( buffer[ x ] == ' ' )
				buffer[ x ] = '\0';
			else
				break;
		}
		Q_strcat( buffer, bufferlen, Q_NEWLINE );
		FS_Write( buffer, strlen( buffer ), f );
	}

	Hunk_FreeTempMemory( buffer );
	FS_FCloseFile( f );
}

						
/*
================
Con_ClearNotify
================
*/
void Con_ClearNotify( void ) {
	int		i;
	
	for ( i = 0 ; i < NUM_CON_TIMES ; i++ ) {
		activeCon->times[i] = 0;
	}
}


/*
================
Con_CheckResize

If the line width has changed, reformat the buffer.
================
*/
void Con_CheckResize( console_t *con )
{
	int		i, j, width, oldwidth, oldtotallines, oldcurrent, numlines, numchars;
	short	tbuf[CON_TEXTSIZE], *src, *dst;
	static int old_width, old_vispage;
	int		vispage;
	float	scale;

	if ( con->viswidth == cls.glconfig.vidWidth && !con_scale->modified ) {
		return;
	}

	if ( cls.glconfig.vidWidth == 0 ) // video hasn't been initialized yet
	{
		scale = con_scale->value;
	}
	else if ( Cvar_VariableIntegerValue( "r_ext_supersample" ) )
	{
		scale = con_scale->value * ((cls.glconfig.vidWidth / 1920.0f) + (cls.glconfig.vidHeight / 1080.0f)) / 2.0f;
	}
	else
	{
		scale = con_scale->value * ((cls.glconfig.vidWidth / 1920.0f) + (cls.glconfig.vidHeight / 1080.0f));
	}

	con->viswidth = cls.glconfig.vidWidth;

	smallchar_width = SMALLCHAR_WIDTH * scale * cls.con_factor;
	smallchar_height = SMALLCHAR_HEIGHT * scale * cls.con_factor;
	bigchar_width = BIGCHAR_WIDTH * scale * cls.con_factor;
	bigchar_height = BIGCHAR_HEIGHT * scale * cls.con_factor;

	if ( cls.glconfig.vidWidth == 0 ) // video hasn't been initialized yet
	{
		g_console_field_width = DEFAULT_CONSOLE_WIDTH;
		width = DEFAULT_CONSOLE_WIDTH * scale;
		con->linewidth = width;
		con->totallines = CON_TEXTSIZE / con->linewidth;
		con->vispage = 4;

		Con_Clear_f();
	}
	else
	{
		width = ((cls.glconfig.vidWidth / smallchar_width) - 2);

		g_console_field_width = width;
		g_consoleField.widthInChars = g_console_field_width;

		if ( width > MAX_CONSOLE_WIDTH )
			width = MAX_CONSOLE_WIDTH;

		vispage = cls.glconfig.vidHeight / ( smallchar_height * 2 ) - 1;

		if ( old_vispage == vispage && old_width == width )
			return;

		oldwidth = con->linewidth;
		oldtotallines = con->totallines;
		oldcurrent = con->current;

		con->linewidth = width;
		con->totallines = CON_TEXTSIZE / con->linewidth;
		con->vispage = vispage;

		old_vispage = vispage;
		old_width = width;

		numchars = oldwidth;
		if ( numchars > con->linewidth )
			numchars = con->linewidth;

		if ( oldcurrent > oldtotallines )
			numlines = oldtotallines;	
		else
			numlines = oldcurrent + 1;	

		if ( numlines > con->totallines )
			numlines = con->totallines;

		Com_Memcpy( tbuf, con->text, CON_TEXTSIZE * sizeof( short ) );

		for ( i = 0; i < CON_TEXTSIZE; i++ ) 
			con->text[i] = (ColorIndex(COLOR_WHITE)<<8) | ' ';

		for ( i = 0; i < numlines; i++ )
		{
			src = &tbuf[ ((oldcurrent - i + oldtotallines) % oldtotallines) * oldwidth ];
			dst = &con->text[ (numlines - 1 - i) * con->linewidth ];
			for ( j = 0; j < numchars; j++ )
				*dst++ = *src++;
		}

		Con_ClearNotify();

		con->current = numlines - 1;
	}

	con->display = con->current;

	con_scale->modified = qfalse;
}


/*
==================
Con_PrevTab
==================
*/
void Con_PrevTab( void ){
	int i = MAX_CONSOLES - 1;

	activeConsoleNum--;

	if ( activeConsoleNum < CON_ALL ) {
		while ( !con[i].active ) {
			i--;
		}
		activeConsoleNum = i;
	}

	activeCon = &con[activeConsoleNum];
	activeCon->notify = qfalse;
}


/*
==================
Con_NextTab
==================
*/
void Con_NextTab( void ) {
	int i = 0;

	activeConsoleNum++;

	if ( !con[activeConsoleNum].active ) {
		while ( !con[i].active ) {
			i++;
			if ( i >= MAX_CONSOLES ) {
				i = 0;
			}
		}
		activeConsoleNum = i;
	}

	activeCon = &con[activeConsoleNum];
	activeCon->notify = qfalse;
}


/*
==================
Con_SwitchToTab
==================
*/
void Con_SwitchToTab( int i ) {
	if ( i >= MAX_CONSOLES || i < 0 ) {
		return;
	}

	activeConsoleNum = i;
	activeCon = &con[activeConsoleNum];
	activeCon->notify = qfalse;
}


/*
==================
Cmd_CompleteTxtName
==================
*/
static void Cmd_CompleteTxtName(const char *args, int argNum ) {
	if ( argNum == 2 ) {
		Field_CompleteFilename( "", "txt", qfalse, FS_MATCH_EXTERN | FS_MATCH_STICK );
	}
}


/*
================
Con_Init
================
*/
void Con_Init( void ) 
{
	con_notifytime = Cvar_Get( "con_notifytime", "3", 0 );
	Cvar_SetDescription( con_notifytime, "Defines how long messages (from players or the system) are on the screen (in seconds)." );
	con_conspeed = Cvar_Get( "scr_conspeed", "3", 0 );
	Cvar_SetDescription( con_conspeed, "Console opening/closing scroll speed." );
	con_autoclear = Cvar_Get("con_autoclear", "1", CVAR_ARCHIVE_ND);
	Cvar_SetDescription( con_autoclear, "Enable/disable clearing console input text when console is closed." );
	con_scale = Cvar_Get( "con_scale", "0.8", CVAR_ARCHIVE_ND );
	Cvar_CheckRange( con_scale, "0.5", "8", CV_FLOAT );
	Cvar_SetDescription( con_scale, "Console font size scale." );
	con_fps = Cvar_Get( "con_fps", "1", CVAR_ARCHIVE_ND );
	Cvar_SetDescription( con_fps, "Enable/disable console fps display." );
	con_clock = Cvar_Get( "con_clock", "1", CVAR_ARCHIVE_ND );
	Cvar_SetDescription( con_clock, "Console clock.\n 1: 24-hour clock\n 2: 12-hour clock" );
	con_drawHelp = Cvar_Get( "con_drawHelp", "1", CVAR_ARCHIVE_ND );
	Cvar_SetDescription( con_drawHelp, "Enable/disable automatic CVAR description display." );

	Field_Clear( &g_consoleField );
	g_consoleField.widthInChars = g_console_field_width;

	Cmd_AddCommand( "clear", Con_Clear_f );
	Cmd_AddCommand( "condump", Con_Dump_f );
	Cmd_SetCommandCompletionFunc( "condump", Cmd_CompleteTxtName );
	Cmd_AddCommand( "toggleconsole", Con_ToggleConsole_f );
	Cmd_AddCommand( "messagemode", Con_MessageMode_f );
	Cmd_AddCommand( "messagemode2", Con_MessageMode2_f );
	Cmd_AddCommand( "messagemode3", Con_MessageMode3_f );
	Cmd_AddCommand( "messagemode4", Con_MessageMode4_f );
}


/*
================
Con_Shutdown
================
*/
void Con_Shutdown( void )
{
	Cmd_RemoveCommand( "clear" );
	Cmd_RemoveCommand( "condump" );
	Cmd_RemoveCommand( "toggleconsole" );
	Cmd_RemoveCommand( "messagemode" );
	Cmd_RemoveCommand( "messagemode2" );
	Cmd_RemoveCommand( "messagemode3" );
	Cmd_RemoveCommand( "messagemode4" );
}


/*
===============
Con_Fixup
===============
*/
static void Con_Fixup( void ) 
{
	int filled;

	if ( activeCon->current >= activeCon->totallines ) {
		filled = activeCon->totallines;
	} else {
		filled = activeCon->current + 1;
	}

	if ( filled <= activeCon->vispage ) {
		activeCon->display = activeCon->current;
	} else if ( activeCon->current - activeCon->display > filled - activeCon->vispage ) {
		activeCon->display = activeCon->current - filled + activeCon->vispage;
	} else if ( activeCon->display > activeCon->current ) {
		activeCon->display = activeCon->current;
	}
}


/*
===============
Con_NewLine

Move to newline only when we _really_ need this
===============
*/
static void Con_NewLine( console_t *con )
{
	short *s;
	int i;

	// follow last line
	if ( con->display == con->current )
		con->display++;
	con->current++;

	s = &con->text[ ( con->current % con->totallines ) * con->linewidth ];
	for ( i = 0; i < con->linewidth ; i++ ) 
		*s++ = (ColorIndex(COLOR_WHITE)<<8) | ' ';

	con->x = 0;
}


/*
===============
Con_Linefeed
===============
*/
static void Con_Linefeed( console_t *con, qboolean skipnotify )
{
	// mark time for transparent overlay
	if ( con->current >= 0 )	{
		if ( skipnotify )
			con->times[ con->current % NUM_CON_TIMES ] = 0;
		else
			con->times[ con->current % NUM_CON_TIMES ] = cls.realtime;
	}

	if ( con->newline ) {
		Con_NewLine( con );
	} else {
		con->newline = qtrue;
		con->x = 0;
	}

	Con_Fixup();
}


int cmdchk( char str1[], char str2[] ) {
	int i, j;

	if ( strlen(str1) >= strlen( str2 ) ) {

		j = strlen( str2 );

		for ( i = 0; i < j; i++ ) {
			if ( str1[i] != str2[i] ) {
				return 1;
			}
		}
	} else {
		 return 1;
	}
	return 0;
}


#define Q_RAW_ESCAPE 1
/*
================
CL_OutputToConsole
================
*/
void CL_OutputToConsole( console_t *con, const char *txt, qboolean skipnotify ) {
	int		y, l, prev;
	unsigned char 	c;
	int		colorIndex;
	char		raw;

	colorIndex = ColorIndex( COLOR_WHITE );

	while ( (c = *txt) != 0 ) {
		if ( Q_IsColorString( txt ) && *(txt+1) != '\n' ) {
			colorIndex = ColorIndexFromChar( *(txt+1) );
			txt += 2;
			continue;
		}

		// count word length
		for ( l = 0 ; l < con->linewidth ; l++ ) {
			if ( txt[l] <= ' ' ) {
				break;
			}
		}

		// word wrap
		if ( l != con->linewidth && ( con->x + l >= con->linewidth ) ) {
			Con_Linefeed( con, skipnotify );
		}

		txt++;

		switch( c )
		{
		case Q_RAW_ESCAPE:
			raw = raw^1;
			break;
		case '\n':
			Con_Linefeed( con, skipnotify );
			break;
		case '\r':
			con->x = 0;
			break;
		default:
			if ( con->newline ) {
				Con_NewLine( con );
				Con_Fixup();
				con->newline = qfalse;
			}
			// display character and advance
			y = con->current % con->totallines;
			con->text[y * con->linewidth + con->x ] = (colorIndex << 8) | (c & 255);
			con->x++;
			if ( con->x >= con->linewidth ) {
				Con_Linefeed( con, skipnotify );
			}
			break;
		}
	}

	// mark time for transparent overlay
	if ( con->current >= 0 ) {
		if ( skipnotify ) {
			prev = con->current % NUM_CON_TIMES - 1;
			if ( prev < 0 )
				prev = NUM_CON_TIMES - 1;
			con->times[ prev ] = 0;
		} else {
			con->times[ con->current % NUM_CON_TIMES ] = cls.realtime;
		}
	}
}


/*
================
CL_ConsolePrint

Handles cursor positioning, line wrapping, etc
All console printing must go through this in order to be logged to disk
If no console is visible, the text will appear at the top of the game window
================
*/
void CL_ConsolePrint( const char *txt ) {
	qboolean skipnotify = qfalse;		// NERVE - SMF
	int		serverCommandNumber;
	int 		i, j;
	char		*cmdstring;

	// TTimo - prefix for text that shows up in console but not in notify
	// backported from RTCW
	if ( !Q_strncmp( txt, "[skipnotify]", 12 ) ) {
		skipnotify = qtrue;
		txt += 12;
	}

	// for some demos we don't want to ever show anything on the console
	if ( cl_noprint && cl_noprint->integer ) {
		return;
	}
	
	for( i=0; i < MAX_CONSOLES; i++ ) {
		if ( !con[i].initialized ) {
			static cvar_t null_cvar = { 0 };
			con[i].color[0] =
			con[i].color[1] =
			con[i].color[2] =
			con[i].color[3] = 1.0f;
			con[i].viswidth = -9999;
			con[i].name = conName[i];
			cls.con_factor = 1.0f;
			con_scale = &null_cvar;
			con_scale->value = 1.0f;
			con_scale->modified = qtrue;
			Con_CheckResize( &con[i] );
			con[i].initialized = qtrue;

			if ( i < numConsoles ) {
				con[i].active = qtrue;
				con[i].name = conName[i];
			} else {
				con[i].active = qfalse;
				con[i].name = NULL;
			}
			con[i].notify = qfalse;
		}
	}

	serverCommandNumber = clc.lastExecutedServerCommand;

	cmdstring = clc.serverCommands[ serverCommandNumber & ( MAX_RELIABLE_COMMANDS - 1 ) ];

	j = CON_SYS;

	if ( serverCommandNumber > lastSrvCmdNum ) {
		if ( !cmdchk( cmdstring, "chat" ) ) {
			j = CON_CHAT;
		}
		if ( !cmdchk( cmdstring, "chat \"\x19" ) || !cmdchk( cmdstring, "tell" ) ) {
			j = CON_TELL;
		}
		if ( !cmdchk( cmdstring, "tchat" ) ) {
			j = CON_TEAM;
		}
	}

	lastSrvCmdNum = serverCommandNumber;

	if ( j >= CON_CHAT && con != &con[j] && activeCon != &con[j] ) {
		con[j].notify = qtrue;
	}

	char buf[ CON_TEXTSIZE ];
	int out = 0;
	const unsigned char *in = (const unsigned char *)txt;

	while ( *in && out < sizeof( buf ) - 1 ) {
		if ( *in < 0x80 ) {
			buf[out++] = *in++;
		} else if ( *in == 0xC3 ) {
			in++;
			switch ( *in ) {
				case 0xA0: case 0xA1: case 0xA2: case 0xA3: case 0xA4: case 0xA5:
				buf[out++] = 'a'; break; // à á â ã ä å

			case 0xA7:
				buf[out++] = 'c'; break; // ç

			case 0xA8: case 0xA9: case 0xAA: case 0xAB:
				buf[out++] = 'e'; break; // è é ê ë

			case 0xAC: case 0xAD: case 0xAE: case 0xAF:
				buf[out++] = 'i'; break; // ì í î ï

			case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: case 0xB7:
				buf[out++] = 'o'; break; // ò ó ô õ ö

			case 0xB9: case 0xBA: case 0xBB: case 0xBC: case 0xBD:
				buf[out++] = 'u'; break; // ù ú û ü

			case 0xBF:
				buf[out++] = 'y'; break; // ÿ

			default:
				break;
			}
		} else {
			in++;
		}
	}

	buf[out] = '\0';

	CL_OutputToConsole( &con[j], buf, skipnotify );
	CL_OutputToConsole( &con[CON_ALL], buf, skipnotify );
}


/*
==============================================================================

DRAWING

==============================================================================
*/


/*
================
Con_DrawInput

Draw the editline after a ] prompt
================
*/
static void Con_DrawInput( void ) {
	int		y;

	if ( cls.state != CA_DISCONNECTED && !(Key_GetCatcher( ) & KEYCATCH_CONSOLE ) ) {
		return;
	}

	y = activeCon->vislines - ( smallchar_height * 2 );

	re.SetColor( activeCon->color );

	SCR_DrawSmallChar( activeCon->xadjust + 1 * smallchar_width, y, ']' );

	Field_Draw( &g_consoleField, activeCon->xadjust + 2 * smallchar_width, y,
		SCREEN_WIDTH - 3 * smallchar_width, qtrue, qtrue );
}


/*
================
Con_DrawNotify

Draws the last few lines of output transparently over the game top
================
*/
static void Con_DrawNotify( void )
{
	int		x, v;
	short	*text;
	int		i;
	int		time;
	int		skip;
	int		currentColorIndex;
	int		colorIndex;
	int notifytime = con_notifytime->value * 1000 + 2 * (int)NOTIFY_FADE_TIME;

	currentColorIndex = ColorIndex( COLOR_WHITE );
	re.SetColor( g_color_table[ currentColorIndex ] );

	v = -smallchar_height;
	for (i= activeCon->current-NUM_CON_TIMES+1 ; i<=activeCon->current ; i++)
	{
		if (i < 0)
			continue;
		time = activeCon->times[i % NUM_CON_TIMES];
		if (time == 0)
			continue;
		time = cls.realtime - time;
		if ( time >= notifytime )
			continue;

		float fade = 2.0f;

		if (notifytime - time < NOTIFY_FADE_TIME * 2.0f) {
			fade = (notifytime - time) / (float)NOTIFY_FADE_TIME;
		}

		if (fade > 1.0f) {
			color_table_alpha( fade - 1.0f );
		} else {
			color_table_alpha( 0.0f );
		}

		re.SetColor( g_color_table[currentColorIndex] );

		if (fade > 1.0f) {
			v += smallchar_height;
		} else {
			v += fade * smallchar_height;
		}

		text = activeCon->text + (i % activeCon->totallines)*activeCon->linewidth;

		if (cl.snap.ps.pm_type != PM_INTERMISSION && Key_GetCatcher( ) & (KEYCATCH_UI | KEYCATCH_CGAME) ) {
			continue;
		}

		for (x = 0 ; x < activeCon->linewidth ; x++) {
			if ( ( text[x] & 0xff ) == ' ' ) {
				continue;
			}
			colorIndex = ( text[x] >> 8 ) & 63;
			if ( currentColorIndex != colorIndex ) {
				currentColorIndex = colorIndex;
				re.SetColor( g_color_table[ colorIndex ] );
			}
			SCR_DrawSmallChar( cl_conXOffset->integer + activeCon->xadjust + (x+1)*smallchar_width, v, text[x] & 0xff );
		}
	}

	if (v < 0) {
		v = 0;
	}

	color_table_alpha( 1.0f );

	re.SetColor( NULL );

	if ( Key_GetCatcher() & (KEYCATCH_UI | KEYCATCH_CGAME) ) {
		return;
	}

	// draw the chat line
	if ( Key_GetCatcher( ) & KEYCATCH_MESSAGE )
	{
		// rescale to virtual 640x480 space
		v /= cls.glconfig.vidHeight / 480.0;

		if (chat_team)
		{
			SCR_DrawMediumString( SMALLCHAR_WIDTH, v + SMALLCHAR_HEIGHT, "say_team:", 1.0f, qfalse );
			skip = 10;
		}
		else
		{
			SCR_DrawMediumString( SMALLCHAR_WIDTH, v + SMALLCHAR_HEIGHT, "say:", 1.0f, qfalse );
			skip = 5;
		}

		Field_MediumDraw( &chatField, skip * MEDIUMCHAR_WIDTH, v + SMALLCHAR_HEIGHT,
			SCREEN_WIDTH - ( skip + 1 ) * MEDIUMCHAR_WIDTH, qtrue, qtrue );
	}
}

/*
================
Con_DrawFPS
================
*/
#define FPS_FRAMES 4
static void Con_DrawFPS(float y) {
	const char		*s;
	int			w;
	static int	previousTimes[FPS_FRAMES];
	static int	index;
	int		i, total;
	int		fps;
	static	int	previous;
	int		t, frameTime;

	// don't use serverTime, because that will be drifting to
	// correct for internet lag changes, timescales, timedemos, etc
	t = Sys_Milliseconds();
	frameTime = t - previous;
	previous = t;

	previousTimes[index % FPS_FRAMES] = frameTime;
	index++;
	if ( index > FPS_FRAMES ) {
		// average multiple frames together to smooth changes out a bit
		total = 0;
		for ( i = 0 ; i < FPS_FRAMES ; i++ ) {
			total += previousTimes[i];
		}
		if ( !total ) {
			total = 1;
		}
		fps = 1000 * FPS_FRAMES / total;

		s = va( "%ifps", fps );
		w = strlen( s ) * smallchar_width;

		SCR_DrawSmallStringExt( cls.glconfig.vidWidth - w - 8, y + 2, s, g_color_table[1], qfalse, qtrue);
	}
}


/*
================
Con_DrawHelp
================
*/
static void Con_DrawHelp( int y ) {
	int		i;
	char	buf[ MAX_CVAR_VALUE_STRING ];
	cvar_t	*cv;

    if ( !con_drawHelp->integer ) {
        return;
    }

    Q_strncpyz( buf, g_consoleField.buffer, sizeof( buf ) );

	if ( buf[0] == '\\' || buf[0] == '/' ) {
		for ( i = 0 ; buf[i] != '\0' ; i++ ) {
			buf[i] = buf[i+1];
		}
	}

	for ( i = 0 ; buf[i] != '\0' ; i++ ) {
		if ( buf[i] <= ' ' ) {
			buf[i] = '\0';
			break;
		}
	}

	cv = Cvar_FindVar( buf );
	if ( !cv || !cv->description ) {
		return;
	}

	SCR_DrawSmallStringExt( activeCon->xadjust + smallchar_width, y, cv->description, g_color_table[ ColorIndex( COLOR_GREEN ) ], qfalse, qtrue );
}


/*
================
Con_DrawSolidConsole

Draws the console with the solid background
================
*/
static void Con_DrawSolidConsole( float frac ) {

	static float conColorValue[4] = { 0.0, 0.0, 0.0, 0.0 };
	// for cvar value change tracking
	static char  conColorString[ MAX_CVAR_VALUE_STRING ] = { '\0' };

	int				i, x, y;
	int				rows;
	short			*text;
	int				row;
	int				lines;
	int				helpLine;
	int				currentColorIndex;
	int				colorIndex;
	float			yf, wf;
	char			buf[ MAX_CVAR_VALUE_STRING ], *v[4];
	qtime_t			qt;
	const char		*time;
	char			*meridiem;
	int			hour;
	int			j;
	int			margin;
	vec4_t			darkTextColor;

	lines = cls.glconfig.vidHeight * frac;
	if ( lines <= 0 )
		return;

	if ( re.FinishBloom )
		re.FinishBloom();

	if ( lines > cls.glconfig.vidHeight )
		lines = cls.glconfig.vidHeight;

	wf = SCREEN_WIDTH;

	// draw the background
	yf = frac * SCREEN_HEIGHT;

	// on wide screens, we will center the text
	activeCon->xadjust = 0;
	SCR_AdjustFrom640( &activeCon->xadjust, &yf, &wf, NULL );

	if ( yf < 1.0 ) {
		yf = 0;
	} else {
		// custom console background color
		if ( cl_conColor->string[0] ) {
			// track changes
			if ( strcmp( cl_conColor->string, conColorString ) ) 
			{
				Q_strncpyz( conColorString, cl_conColor->string, sizeof( conColorString ) );
				Q_strncpyz( buf, cl_conColor->string, sizeof( buf ) );
				Com_Split( buf, v, 4, ' ' );
				for ( i = 0; i < 4 ; i++ ) {
					conColorValue[ i ] = Q_atof( v[ i ] ) / 255.0f;
					if ( conColorValue[ i ] > 1.0f ) {
						conColorValue[ i ] = 1.0f;
					} else if ( conColorValue[ i ] < 0.0f ) {
						conColorValue[ i ] = 0.0f;
					}
				}
			}
			re.SetColor( conColorValue );
			re.DrawStretchPic( 0, 0, wf, yf, 0, 0, 1, 1, cls.whiteShader );
		} else {
			re.SetColor( g_color_table[ ColorIndex( COLOR_WHITE ) ] );
			re.DrawStretchPic( 0, 0, wf, yf, 0, 0, 1, 1, cls.consoleShader );
		}

	}

	re.SetColor( g_color_table[ ColorIndex( COLOR_RED ) ] );
	re.DrawStretchPic( 0, yf, wf, 2, 0, 0, 1, 1, cls.whiteShader );

	//y = yf;

	// draw the version number
	SCR_DrawSmallString( cls.glconfig.vidWidth - ( ARRAY_LEN( Q3_VERSION ) ) * smallchar_width,
		lines - smallchar_height, Q3_VERSION, ARRAY_LEN( Q3_VERSION ) - 1 );

	// draw the text
	if ( con_drawHelp->integer ) {
		helpLine = 1;
	} else {
		helpLine = 0;
	}

	activeCon->vislines = lines;
	rows = lines / smallchar_width - 1;	// rows of text to draw

	y = lines - (smallchar_height * (3 + helpLine));

	row = activeCon->display;

	// draw from the bottom up
	if ( activeCon->display != activeCon->current )
	{
		// draw arrows to show the buffer is backscrolled
		re.SetColor( g_color_table[ ColorIndex( COLOR_RED ) ] );
		for ( x = 0 ; x < activeCon->linewidth ; x += 4 )
			SCR_DrawSmallChar( activeCon->xadjust + (x+1)*smallchar_width, y, '^' );
		y -= smallchar_height;
		row--;
	}

	// draw console fps frames
	if ( con_fps->integer ) {
		Con_DrawFPS( 2 );
	}

	// draw console clock (fx3)
	if ( con_clock->integer ) {
		Com_RealTime( &qt );
		if ( con_clock->integer == 2 ) {
			if ( qt.tm_hour < 13 ) {
				meridiem = "AM";
				hour = qt.tm_hour;
			} else {
				meridiem = "PM";
				hour = qt.tm_hour - 12;
			}
			time = va( "%02i:%02i%s", hour, qt.tm_min, meridiem );
		} else {
			time = va( "%02i:%02i:%02i", qt.tm_hour, qt.tm_min, qt.tm_sec );
		}

		if ( con_fps->integer ) {
			SCR_DrawSmallStringExt( cls.glconfig.vidWidth - ( strlen( time ) ) * smallchar_width - 8, smallchar_height + 6, time, g_color_table[1], qfalse, qtrue);
		} else {
			SCR_DrawSmallStringExt( cls.glconfig.vidWidth - ( strlen( time ) ) * smallchar_width - 8, 2, time, g_color_table[1], qfalse, qtrue);
		}
	}

	// draw console tabs (fX3)
	margin = 13;

	darkTextColor[0] = darkTextColor[1] = darkTextColor[2] = 0.25;
	darkTextColor[3] = 1;

	for ( j = 0; j < MAX_CONSOLES; j++ ) {
		if ( con[j].active ) {
			if ( activeCon == &con[j] ) {
				SCR_DrawSmallStringExt( margin + 10, lines - smallchar_height, con[j].name, g_color_table[conColors[j]], qfalse, qtrue );
			} else {
				if ( con[j].notify ) {
					SCR_DrawSmallStringExt( margin + 10, lines - smallchar_height, con[j].name, g_color_table[3], qfalse, qtrue );
				} else {
					SCR_DrawSmallStringExt( margin + 10, lines - smallchar_height, con[j].name, darkTextColor, qfalse, qtrue );
				}
			}
			margin += strlen( con[j].name ) * smallchar_width + 20;
		}
	}

#ifdef USE_CURL
	if ( download.progress[ 0 ] ) 
	{
		currentColorIndex = ColorIndex( COLOR_CYAN );
		re.SetColor( g_color_table[ currentColorIndex ] );

		i = strlen( download.progress );
		for ( x = 0 ; x < i ; x++ ) 
		{
			SCR_DrawSmallChar( ( x + 1 ) * smallchar_width,
				lines - smallchar_height, download.progress[x] );
		}
	}
#endif

	currentColorIndex = ColorIndex( COLOR_WHITE );
	re.SetColor( g_color_table[ currentColorIndex ] );

	for ( i = 0 ; i < rows ; i++, y -= smallchar_height, row-- )
	{
		if ( row < 0 )
			break;

		if ( activeCon->current - row >= activeCon->totallines ) {
			// past scrollback wrap point
			continue;
		}

		text = activeCon->text + (row % activeCon->totallines) * activeCon->linewidth;

		for ( x = 0 ; x < activeCon->linewidth ; x++ ) {
			// skip rendering whitespace
			if ( ( text[x] & 0xff ) == ' ' ) {
				continue;
			}
			// track color changes
			colorIndex = ( text[ x ] >> 8 ) & 63;
			if ( currentColorIndex != colorIndex ) {
				currentColorIndex = colorIndex;
				re.SetColor( g_color_table[ colorIndex ] );
			}
			SCR_DrawSmallChar( activeCon->xadjust + (x + 1) * smallchar_width, y, text[x] & 0xff );
		}
	}

	// draw the input prompt, user text, and cursor if desired
	Con_DrawInput();

	Con_DrawHelp( lines - smallchar_height * 3 );

	re.SetColor( NULL );
}


/*
==================
Con_DrawConsole
==================
*/
void Con_DrawConsole( void ) {

	// check for console width changes from a vid mode change
	Con_CheckResize( activeCon );

	// if disconnected, render console full screen
	if ( cls.state == CA_DISCONNECTED ) {
		if ( !( Key_GetCatcher( ) & (KEYCATCH_UI | KEYCATCH_CGAME)) ) {
			Con_DrawSolidConsole( 1.0 );
			return;
		}
	}

	if ( activeCon->displayFrac ) {
		Con_DrawSolidConsole( activeCon->displayFrac );
	} else {
		// draw notify lines
		if ( cls.state == CA_ACTIVE ) {
			Con_DrawNotify();
		}
	}
}

//================================================================

/*
==================
Con_RunConsole

Scroll it up or down
==================
*/
void Con_RunConsole( void ) 
{
	int i;

	for ( i = 0; i < MAX_CONSOLES; i++ ){
		// decide on the destination height of the console
		if ( Key_GetCatcher( ) & KEYCATCH_CONSOLE )
			con[i].finalFrac = cl_consoleHeight->value;
		else
			con[i].finalFrac = 0.0;	// none visible
	
		// scroll towards the destination height
		if ( con[i].finalFrac < con[i].displayFrac )
		{
			con[i].displayFrac -= con_conspeed->value * cls.realFrametime * 0.001;
			if ( con[i].finalFrac > con[i].displayFrac )
				con[i].displayFrac = con[i].finalFrac;

		}
		else if ( con[i].finalFrac > con[i].displayFrac )
		{
			con[i].displayFrac += con_conspeed->value * cls.realFrametime * 0.001;
			if ( con[i].finalFrac < con[i].displayFrac )
				con[i].displayFrac = con[i].finalFrac;
		}
	}
}


/*
==================
Con_SetFrac
==================
*/
void Con_SetFrac( const float conFrac )
{
	int i;

	for ( i = 0 ; i < MAX_CONSOLES ; i++ ) {
		if ( conFrac < .1f ) {
			con[i].userFrac = .1f;
		} else if ( conFrac > 1.0f ) {
			con[i].userFrac = 1.0f;
		} else {
			con[i].userFrac = conFrac;
		}
	}
}


void Con_PageUp( int lines )
{
	if ( lines == 0 )
		lines = activeCon->vispage - 2;

	activeCon->display -= lines;
	
	Con_Fixup();
}


void Con_PageDown( int lines )
{
	if ( lines == 0 )
		lines = activeCon->vispage - 2;

	activeCon->display += lines;

	Con_Fixup();
}


void Con_Top( void )
{
	// this is generally incorrect but will be adjusted in Con_Fixup()
	activeCon->display = activeCon->current - activeCon->totallines;

	Con_Fixup();
}


void Con_Bottom( void )
{
	activeCon->display = activeCon->current;

	Con_Fixup();
}


void Con_Close( void )
{
	if ( !com_cl_running->integer )
		return;

	Field_Clear( &g_consoleField );
	Con_ClearNotify();
	Key_SetCatcher( Key_GetCatcher( ) & ~KEYCATCH_CONSOLE );
	activeCon->finalFrac = 0.0;			// none visible
	activeCon->displayFrac = 0.0;
}
