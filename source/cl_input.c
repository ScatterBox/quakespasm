/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2010-2014 QuakeSpasm developers

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// cl.input.c  -- builds an intended movement command to send to the server

// Quake is a trademark of Id Software, Inc., (c) 1996 Id Software, Inc. All
// rights reserved.

#include "quakedef.h"

#ifdef VITA
#include <vitasdk.h>
#endif
#ifdef __SWITCH__
#include <switch.h>
#include <switch/runtime/pad.h>
#endif

extern cvar_t joy_invert;
extern cvar_t cl_maxpitch; //johnfitz -- variable pitch clamping
extern cvar_t cl_minpitch; //johnfitz -- variable pitch clamping

cvar_t motioncam = {"motioncam", "0", CVAR_ARCHIVE};
cvar_t gyromode = {"gyromode", "0", CVAR_ARCHIVE};
cvar_t gyrosensx = {"gyrosensx", "1.0", CVAR_ARCHIVE};
cvar_t gyrosensy = {"gyrosensy", "1.0", CVAR_ARCHIVE};

#ifdef VITA
SceMotionState motionstate;
#endif
#ifdef __SWITCH__
PadState gyropad;
HidVibrationValue VibrationValue;
HidVibrationValue VibrationValue_stop;
HidVibrationValue VibrationValues[2];
HidVibrationDeviceHandle VibrationDeviceHandles[2][2];
HidSixAxisSensorHandle handles[4];
#endif

/*
===============================================================================

KEY BUTTONS

Continuous button event tracking is complicated by the fact that two different
input sources (say, mouse button 1 and the control key) can both press the
same button, but the button should only be released when both of the
pressing key have been released.

When a key event issues a button command (+forward, +attack, etc), it appends
its key number as a parameter to the command so it can be matched up with
the release.

state bit 0 is the current state of the key
state bit 1 is edge triggered on the up to down transition
state bit 2 is edge triggered on the down to up transition

===============================================================================
*/


kbutton_t	in_mlook, in_klook;
kbutton_t	in_left, in_right, in_forward, in_back;
kbutton_t	in_lookup, in_lookdown, in_moveleft, in_moveright;
kbutton_t	in_strafe, in_speed, in_use, in_jump, in_attack, in_grenade, in_reload, in_switch, in_knife, in_aim;
kbutton_t	in_up, in_down;

int			in_impulse;

void KeyDown (kbutton_t *b)
{
	int		k;
	const char	*c;

	c = Cmd_Argv(1);
	if (c[0])
		k = atoi(c);
	else
		k = -1;		// typed manually at the console for continuous down

	if (k == b->down[0] || k == b->down[1])
		return;		// repeating key

	if (!b->down[0])
		b->down[0] = k;
	else if (!b->down[1])
		b->down[1] = k;
	else
	{
		Con_Printf ("Three keys down for a button!\n");
		return;
	}

	if (b->state & 1)
		return;		// still down
	b->state |= 1 + 2;	// down + impulse down
}

void KeyUp (kbutton_t *b)
{
	int		k;
	const char	*c;

	c = Cmd_Argv(1);
	if (c[0])
		k = atoi(c);
	else
	{ // typed manually at the console, assume for unsticking, so clear all
		b->down[0] = b->down[1] = 0;
		b->state = 4;	// impulse up
		return;
	}

	if (b->down[0] == k)
		b->down[0] = 0;
	else if (b->down[1] == k)
		b->down[1] = 0;
	else
		return;		// key up without coresponding down (menu pass through)
	if (b->down[0] || b->down[1])
		return;		// some other key is still holding it down

	if (!(b->state & 1))
		return;		// still up (this should not happen)
	b->state &= ~1;		// now up
	b->state |= 4; 		// impulse up
}

void IN_KLookDown (void) {KeyDown(&in_klook);}
void IN_KLookUp (void) {KeyUp(&in_klook);}
void IN_MLookDown (void) {KeyDown(&in_mlook);}
void IN_MLookUp (void) {
	KeyUp(&in_mlook);
	if ( !(in_mlook.state&1) &&  lookspring.value)
		V_StartPitchDrift();
}
void IN_UpDown(void) {KeyDown(&in_up);}
void IN_UpUp(void) {KeyUp(&in_up);}
void IN_DownDown(void) {KeyDown(&in_down);}
void IN_DownUp(void) {KeyUp(&in_down);}
void IN_LeftDown(void) {KeyDown(&in_left);}
void IN_LeftUp(void) {KeyUp(&in_left);}
void IN_RightDown(void) {KeyDown(&in_right);}
void IN_RightUp(void) {KeyUp(&in_right);}
void IN_ForwardDown(void) {KeyDown(&in_forward);}
void IN_ForwardUp(void) {KeyUp(&in_forward);}
void IN_BackDown(void) {KeyDown(&in_back);}
void IN_BackUp(void) {KeyUp(&in_back);}
void IN_LookupDown(void) {KeyDown(&in_lookup);}
void IN_LookupUp(void) {KeyUp(&in_lookup);}
void IN_LookdownDown(void) {KeyDown(&in_lookdown);}
void IN_LookdownUp(void) {KeyUp(&in_lookdown);}
void IN_MoveleftDown(void) {KeyDown(&in_moveleft);}
void IN_MoveleftUp(void) {KeyUp(&in_moveleft);}
void IN_MoverightDown(void) {KeyDown(&in_moveright);}
void IN_MoverightUp(void) {KeyUp(&in_moveright);}

void IN_SpeedDown(void) {KeyDown(&in_speed);}
void IN_SpeedUp(void) {KeyUp(&in_speed);}
void IN_StrafeDown(void) {KeyDown(&in_strafe);}
void IN_StrafeUp(void) {KeyUp(&in_strafe);}

void IN_AttackDown(void) {KeyDown(&in_attack);}
void IN_AttackUp(void) {KeyUp(&in_attack);}

void IN_UseDown (void) {KeyDown(&in_use);}
void IN_UseUp (void) {KeyUp(&in_use);}
void IN_JumpDown (void) {KeyDown(&in_jump);}
void IN_JumpUp (void) {KeyUp(&in_jump);}
void IN_GrenadeDown (void) {KeyDown(&in_grenade);}
void IN_GrenadeUp (void) {KeyUp(&in_grenade);}
void IN_SwitchDown (void) {KeyDown(&in_switch);}
void IN_SwitchUp (void) {KeyUp(&in_switch);}
void IN_ReloadDown (void) {KeyDown(&in_reload);}
void IN_ReloadUp (void) {KeyUp(&in_reload);}
void IN_KnifeDown (void) {KeyDown(&in_knife);}
void IN_KnifeUp (void) {KeyUp(&in_knife);}
void IN_AimDown (void) {KeyDown(&in_aim);}
void IN_AimUp (void) {KeyUp(&in_aim);}

void IN_Impulse (void) {in_impulse=Q_atoi(Cmd_Argv(1));}

/*
===============
CL_KeyState

Returns 0.25 if a key was pressed and released during the frame,
0.5 if it was pressed and held
0 if held then released, and
1.0 if held for the entire time
===============
*/
float CL_KeyState (kbutton_t *key)
{
	float		val;
	qboolean	impulsedown, impulseup, down;

	impulsedown = key->state & 2;
	impulseup = key->state & 4;
	down = key->state & 1;
	val = 0;

	if (impulsedown && !impulseup)
	{
		if (down)
			val = 0.5;	// pressed and held this frame
		else
			val = 0;	//	I_Error ();
	}
	if (impulseup && !impulsedown)
	{
		if (down)
			val = 0;	//	I_Error ();
		else
			val = 0;	// released this frame
	}
	if (!impulsedown && !impulseup)
	{
		if (down)
			val = 1.0;	// held the entire frame
		else
			val = 0;	// up the entire frame
	}
	if (impulsedown && impulseup)
	{
		if (down)
			val = 0.75;	// released and re-pressed this frame
		else
			val = 0.25;	// pressed and released this frame
	}

	key->state &= 1;		// clear impulses

	return val;
}


//==========================================================================

cvar_t	cl_upspeed = {"cl_upspeed","200",CVAR_NONE};
cvar_t	cl_movespeedkey = {"cl_movespeedkey","2.0",CVAR_NONE};

cvar_t	cl_yawspeed = {"cl_yawspeed","140",CVAR_NONE};
cvar_t	cl_pitchspeed = {"cl_pitchspeed","150",CVAR_NONE};

cvar_t	cl_anglespeedkey = {"cl_anglespeedkey","0.75",CVAR_NONE};

cvar_t	cl_alwaysrun = {"cl_alwaysrun","0",CVAR_ARCHIVE}; // QuakeSpasm -- new always run
cvar_t	in_aimassist = {"in_aimassist", "1", true};

/*
================
CL_AdjustAngles

Moves the local angle positions
================
*/

void CL_AdjustAngles (void)
{
	float	speed;
	float	up, down;

	if ((in_speed.state & 1) ^ (cl_alwaysrun.value != 0.0))
		speed = host_frametime * cl_anglespeedkey.value;
	else
		speed = host_frametime;

	if (!(in_strafe.state & 1))
	{
		cl.viewangles[YAW] -= speed*cl_yawspeed.value*CL_KeyState (&in_right);
		cl.viewangles[YAW] += speed*cl_yawspeed.value*CL_KeyState (&in_left);
		cl.viewangles[YAW] = anglemod(cl.viewangles[YAW]);
	}
	if (in_klook.state & 1)
	{
		V_StopPitchDrift ();
		cl.viewangles[PITCH] -= speed*cl_pitchspeed.value * CL_KeyState (&in_forward);
		cl.viewangles[PITCH] += speed*cl_pitchspeed.value * CL_KeyState (&in_back);
	}

	up = CL_KeyState (&in_lookup);
	down = CL_KeyState(&in_lookdown);

	cl.viewangles[PITCH] -= speed*cl_pitchspeed.value *up; // *up
	cl.viewangles[PITCH] += speed*cl_pitchspeed.value *down; //down

	if (up || down)
		V_StopPitchDrift ();

	//johnfitz -- variable pitch clamping
	if (cl.viewangles[PITCH] > cl_maxpitch.value)
		cl.viewangles[PITCH] = cl_maxpitch.value;
	if (cl.viewangles[PITCH] < cl_minpitch.value)
		cl.viewangles[PITCH] = cl_minpitch.value;
	//johnfitz

	if (cl.viewangles[ROLL] > 50)
		cl.viewangles[ROLL] = 50;
	if (cl.viewangles[ROLL] < -50)
		cl.viewangles[ROLL] = -50;

	// vita gyro support by rinnegatamante (originally from vitaquake)
	// creds to the switch-examples for nx support
	if (motioncam.value) {

		// If gyro is set to ADS only and we're not ADSing, goodbye.
		if (gyromode.value && cl.stats[STAT_ZOOM] != 1 && cl.stats[STAT_ZOOM] != 2)
			return;

#ifdef VITA
		sceMotionGetState(&motionstate);

		// not sure why YAW or the horizontal x axis is the controlled by angularVelocity.y
		// and the PITCH or the vertical y axis is controlled by angularVelocity.x but its what seems to work
		float x_gyro_cam = motionstate.angularVelocity.y * gyrosensx.value;
		float y_gyro_cam = motionstate.angularVelocity.x * gyrosensy.value;
#endif
#ifdef __SWITCH__
		padUpdate(&gyropad);
		// Read from the correct sixaxis handle depending on the current input style
		HidSixAxisSensorState sixaxis = {0};
		u64 style_set = padGetStyleSet(&gyropad);
		if (style_set & HidNpadStyleTag_NpadHandheld)
			hidGetSixAxisSensorStates(handles[0], &sixaxis, 1);
		else if (style_set & HidNpadStyleTag_NpadFullKey)
			hidGetSixAxisSensorStates(handles[1], &sixaxis, 1);
		else if (style_set & HidNpadStyleTag_NpadJoyDual) {
			// For JoyDual, read from either the Left or Right Joy-Con depending on which is/are connected
			u64 attrib = padGetAttributes(&gyropad);
			if (attrib & HidNpadAttribute_IsLeftConnected)
				hidGetSixAxisSensorStates(handles[2], &sixaxis, 1);
			else if (attrib & HidNpadAttribute_IsRightConnected)
				hidGetSixAxisSensorStates(handles[3], &sixaxis, 1);
		}

		float x_gyro_cam = sixaxis.angular_velocity.y * (gyrosensx.value*4);
		float y_gyro_cam = sixaxis.angular_velocity.x * (gyrosensy.value*4);
#endif // VITA

#ifndef __SWITCH__
#ifndef VITA
		float x_gyro_cam = 0;
		float y_gyro_cam = 0;
#endif
#endif

		cl.viewangles[YAW] += x_gyro_cam;

		V_StopPitchDrift();
		
		if (joy_invert.value)
			cl.viewangles[PITCH] += y_gyro_cam;
		else
			cl.viewangles[PITCH] -= y_gyro_cam;
	}
}

/*
================
CL_BaseMove

Send the intended movement message to the server
================
*/

float cl_backspeed;
float cl_forwardspeed;
float cl_sidespeed;

extern cvar_t	waypoint_mode;


void CL_BaseMove (usercmd_t *cmd)
{
	if (cls.signon != SIGNONS)
		return;

	CL_AdjustAngles ();

	Q_memset (cmd, 0, sizeof(*cmd));

	// cypress - we handle movespeed in QC now.
	cl_backspeed = cl_forwardspeed = cl_sidespeed = cl.maxspeed*0.71;

	// Throttle side and back speeds
	cl_sidespeed *= 0.8;
	cl_backspeed *= 0.7;

	if (waypoint_mode.value)
		cl_backspeed = cl_forwardspeed = cl_sidespeed *= 1.5;



	// Limit max 
	if (cl_backspeed > cl.maxspeed) {
		cl_backspeed = cl.maxspeed;
	}
	if (cl_sidespeed > cl.maxspeed) {
		cl_sidespeed = cl.maxspeed;
	}
	if (cl_forwardspeed > cl.maxspeed) {
		cl_forwardspeed = cl.maxspeed;
	}


	if (in_strafe.state & 1)
	{
		cmd->sidemove += cl_sidespeed * CL_KeyState (&in_right);
		cmd->sidemove -= cl_sidespeed * CL_KeyState (&in_left);
	}

	cmd->sidemove += cl_sidespeed * CL_KeyState (&in_moveright);
	cmd->sidemove -= cl_sidespeed * CL_KeyState (&in_moveleft);

	cmd->upmove += cl_upspeed.value * CL_KeyState (&in_up);
	cmd->upmove -= cl_upspeed.value * CL_KeyState (&in_down);

	if (! (in_klook.state & 1) )
	{
		cmd->forwardmove += cl_forwardspeed * CL_KeyState (&in_forward);
		cmd->forwardmove -= cl_backspeed * CL_KeyState (&in_back);
	}

//
// adjust for speed key
//
	if ((in_speed.state & 1) ^ (cl_alwaysrun.value != 0.0))
	{
		cmd->forwardmove *= cl_movespeedkey.value;
		cmd->sidemove *= cl_movespeedkey.value;
		cmd->upmove *= cl_movespeedkey.value;
	}

}

int infront(entity_t ent1, entity_t *ent2)
{
	vec3_t vec;
	float dot;
	VectorSubtract(ent2->origin, ent1.origin, vec);
	VectorNormalize(vec);

	vec3_t temp_angle,temp_forward,temp_right,temp_up;
	VectorCopy(cl.viewangles,temp_angle);

	AngleVectors(temp_angle,temp_forward,temp_right,temp_up);

	dot = DotProduct(vec,temp_forward);
	if(dot > 0.98)
	{
		return 1;
	}
	return 0;
}

//
// CL_FindZombieEnt(stat_pos, ent_type)
// Client-safe way to grab a Zombie ent in current PVS
// at the given start position (usually last index).
// entity_t structs do not hold anything like classnames,
// so we have to, erm, uh, string compare models. yea.
//
#define FINDENT_ZOMBIE_BODY	0
#define FINDENT_ZOMBIE_HEAD	1

int CL_FindZombieEnt(int start_pos, int ent_type)
{
	entity_t* 	ent;

	// Start +1 so we aren't always just returning the same dude
	// over, and over..
	for (int i = start_pos + 1; i < cl_numvisedicts; i++) {
		ent = cl_visedicts[i];

		//
		// Here's the part I hate!
		//
		char ident_char = ent->model->name[strlen(ent->model->name) - 5];

		if ((ident_char == '%' && FINDENT_ZOMBIE_BODY) || (ident_char == '^' && FINDENT_ZOMBIE_HEAD)) {
			return i;
		}
		//
		// End awful part!
		//
	}

	return 0;
}

#define 	P_DEAD 		64 // FUCK
void CL_Aim_Snap(void)
{
	entity_t 	*zombie, *best_zombie;
	entity_t 	client;
	vec3_t 		distance_vector, zombie_org, client_org;
	float 		best_distance = 10000;
	int 		last_visedict_index;

	best_zombie = cl_visedicts[0]; // set best to world.

	int aim_offset = 20;

	client = cl_entities[cl.viewentity];
	VectorCopy(client.origin, client_org);
	client_org[2] += cl.viewheight; // cypress -- actually grab viewheight now, so stances make sense.
									// (also helps with crawlers, probably?)

	// Snap to the head instead of the torso with Deadshot
	if (cl.perks & P_DEAD)
		last_visedict_index = CL_FindZombieEnt(0, FINDENT_ZOMBIE_HEAD);
	else
		last_visedict_index = CL_FindZombieEnt(0, FINDENT_ZOMBIE_BODY);

	zombie = cl_visedicts[last_visedict_index];

	while(last_visedict_index != 0) {
		if (infront(client, zombie)) {
			VectorCopy(zombie->origin, zombie_org);

			zombie_org[2] += aim_offset;
			// If using Deadshot, go up a little more to hit the
			// center of their head, makes it more obvious.
			if (cl.perks & P_DEAD)
				zombie_org[2] += 10;

			VectorSubtract(client_org, zombie_org, distance_vector);

			if (VectorLength(distance_vector) < best_distance) {
				vec3_t impact;
				vec3_t normal;
				if (!TraceLineN(zombie_org, client_org, impact, normal)) {
					best_distance = VectorLength(distance_vector);
					best_zombie = zombie;
				}
			}
		}

		if (cl.perks & P_DEAD)
			last_visedict_index = CL_FindZombieEnt(last_visedict_index, FINDENT_ZOMBIE_HEAD);
		else
			last_visedict_index = CL_FindZombieEnt(last_visedict_index, FINDENT_ZOMBIE_BODY);

		zombie = cl_visedicts[last_visedict_index];
	}

	// We got a decent Zombie, not world.
	if (best_zombie != cl_visedicts[0]) {
		VectorCopy(best_zombie->origin, zombie_org);

		zombie_org[2] += aim_offset;
		if (cl.perks & P_DEAD)
			zombie_org[2] += 10;

		VectorSubtract(zombie_org, client_org, distance_vector);
		VectorNormalize(distance_vector);
		vectoangles(distance_vector, distance_vector);
		distance_vector[0] += (distance_vector[0]  > 180)? -360 : 0; // Need to bound pitch around 0, from -180, to + 180
		distance_vector[0] *= -1; // inverting pitch

		if(distance_vector[0] < -70 || distance_vector[0] > 80)
	 		return;

	 	VectorCopy(distance_vector, cl.viewangles);
	}	
}

/*
==============
CL_SendMove
==============
*/
int zoom_snap;
float angledelta(float a);
float deltaPitch,deltaYaw;

void CL_SendMove (const usercmd_t *cmd)
{
	int		i;
	int		bits;
	sizebuf_t	buf;
	byte	data[128];
	vec3_t tempv;
	buf.maxsize = 128;
	buf.cursize = 0;
	buf.data = data;

	cl.cmd = *cmd;


////////////////////////////
// NZP SPECIFICS          //
////////////////////////////

	//==== Aim Assist Code ====
	if((cl.stats[STAT_ZOOM]==1 || cl.stats[STAT_ZOOM]==2) && ((in_aimassist.value) || (cl.perks & 64)))
	{
		if(!zoom_snap)
		{

			CL_Aim_Snap();
			zoom_snap = 1;
		}
	}
	else
		zoom_snap = 0;

	//==== Sniper Scope Swaying ====
	if(cl.stats[STAT_ZOOM] == 2 && !(cl.perks & 64))
	{
		vec3_t vang;

		VectorCopy(cl.viewangles,vang);

		vang[0] -= deltaPitch;
		vang[1] -= deltaYaw;

		deltaPitch =(cos(cl.time/0.7) + cos(cl.time) + sin(cl.time/1.1)) * 0.5;
		deltaYaw = (sin(cl.time/0.4) + cos(cl.time/0.56) + sin(cl.time)) * 0.5;
		vang[0] += deltaPitch;
		vang[1] += deltaYaw;
		vang[0] = angledelta(vang[0]);
		vang[1] = angledelta(vang[1]);

		VectorCopy(vang,cl.viewangles);
	}

////////////////////////////
// NZP END                //
////////////////////////////

//
// send the movement message
//
	MSG_WriteByte (&buf, clc_move);

	MSG_WriteFloat (&buf, cl.mtime[0]);	// so server can get ping times

	VectorAdd(cl.gun_kick, cl.viewangles, tempv);
	for (i=0 ; i<3 ; i++)
		//johnfitz -- 16-bit angles for PROTOCOL_FITZQUAKE
		if (cl.protocol == PROTOCOL_NETQUAKE)
			MSG_WriteAngle (&buf, tempv[i], cl.protocolflags);
		else
			MSG_WriteAngle16 (&buf, tempv[i], cl.protocolflags);
		//johnfitz

	MSG_WriteShort (&buf, cmd->forwardmove);
	MSG_WriteShort (&buf, cmd->sidemove);
	MSG_WriteShort (&buf, cmd->upmove);

//
// send button bits
//
	bits = 0;

	if (in_attack.state & 3 )
		bits |= 1;
	in_attack.state &= ~2;

	if (in_jump.state & 3)
		bits |= 2;
	in_jump.state &= ~2;

	if (in_grenade.state & 3)
		bits |= 8;
	in_grenade.state &= ~2;

	if (in_switch.state & 3)
		bits |= 16;
	in_switch.state &= ~2;

	if (in_reload.state & 3)
		bits |= 32;
	in_reload.state &= ~2;

	if (in_knife.state & 3)
		bits |= 64;
	in_knife.state &= ~2;
	
	if (in_use.state & 3)
		bits |= 128;
	in_use.state &= ~2;
	
	if (in_aim.state & 3)
		bits |= 256;
	in_aim.state &= ~2; 

	MSG_WriteLong (&buf, bits);

	MSG_WriteByte (&buf, in_impulse);
	in_impulse = 0;

//
// deliver the message
//
	if (cls.demoplayback)
		return;

//
// allways dump the first two message, because it may contain leftover inputs
// from the last level
//
	if (++cl.movemessages <= 2)
		return;

	if (NET_SendUnreliableMessage (cls.netcon, &buf) == -1)
	{
		Con_Printf ("CL_SendMove: lost server connection\n");
		CL_Disconnect ();
	}
}

/*
============
CL_InitInput
============
*/
void CL_InitInput (void)
{
	Cmd_AddCommand ("+moveup",IN_UpDown);
	Cmd_AddCommand ("-moveup",IN_UpUp);
	Cmd_AddCommand ("+movedown",IN_DownDown);
	Cmd_AddCommand ("-movedown",IN_DownUp);
	Cmd_AddCommand ("+left",IN_LeftDown);
	Cmd_AddCommand ("-left",IN_LeftUp);
	Cmd_AddCommand ("+right",IN_RightDown);
	Cmd_AddCommand ("-right",IN_RightUp);
	Cmd_AddCommand ("+forward",IN_ForwardDown);
	Cmd_AddCommand ("-forward",IN_ForwardUp);
	Cmd_AddCommand ("+back",IN_BackDown);
	Cmd_AddCommand ("-back",IN_BackUp);
	Cmd_AddCommand ("+lookup", IN_LookupDown);
	Cmd_AddCommand ("-lookup", IN_LookupUp);
	Cmd_AddCommand ("+lookdown", IN_LookdownDown);
	Cmd_AddCommand ("-lookdown", IN_LookdownUp);
	Cmd_AddCommand ("+strafe", IN_StrafeDown);
	Cmd_AddCommand ("-strafe", IN_StrafeUp);
	Cmd_AddCommand ("+moveleft", IN_MoveleftDown);
	Cmd_AddCommand ("-moveleft", IN_MoveleftUp);
	Cmd_AddCommand ("+moveright", IN_MoverightDown);
	Cmd_AddCommand ("-moveright", IN_MoverightUp);
	Cmd_AddCommand ("+speed", IN_SpeedDown);
	Cmd_AddCommand ("-speed", IN_SpeedUp);
	Cmd_AddCommand ("+attack", IN_AttackDown);
	Cmd_AddCommand ("-attack", IN_AttackUp);
	Cmd_AddCommand ("+use", IN_UseDown);
	Cmd_AddCommand ("-use", IN_UseUp);
	Cmd_AddCommand ("+jump", IN_JumpDown);
	Cmd_AddCommand ("-jump", IN_JumpUp);
	Cmd_AddCommand ("+grenade", IN_GrenadeDown);
	Cmd_AddCommand ("-grenade", IN_GrenadeUp);
	Cmd_AddCommand ("+switch", IN_SwitchDown);
	Cmd_AddCommand ("-switch", IN_SwitchUp);
	Cmd_AddCommand ("+reload", IN_ReloadDown);
	Cmd_AddCommand ("-reload", IN_ReloadUp);
	Cmd_AddCommand ("+knife", IN_KnifeDown);
	Cmd_AddCommand ("-knife", IN_KnifeUp);
	Cmd_AddCommand ("+aim", IN_AimDown);
	Cmd_AddCommand ("-aim", IN_AimUp);
	Cmd_AddCommand ("impulse", IN_Impulse);
	Cmd_AddCommand ("+klook", IN_KLookDown);
	Cmd_AddCommand ("-klook", IN_KLookUp);
	Cmd_AddCommand ("+mlook", IN_MLookDown);
	Cmd_AddCommand ("-mlook", IN_MLookUp);

#ifdef VITA
	sceMotionReset();
 	sceMotionStartSampling();
#endif
#ifdef __SWITCH__
	padConfigureInput(1, HidNpadStyleSet_NpadStandard);
	padInitializeDefault(&gyropad);

    hidGetSixAxisSensorHandles(&handles[0], 1, HidNpadIdType_Handheld, HidNpadStyleTag_NpadHandheld);
    hidGetSixAxisSensorHandles(&handles[1], 1, HidNpadIdType_No1,      HidNpadStyleTag_NpadFullKey);
    hidGetSixAxisSensorHandles(&handles[2], 2, HidNpadIdType_No1,      HidNpadStyleTag_NpadJoyDual);
    hidStartSixAxisSensor(handles[0]);
    hidStartSixAxisSensor(handles[1]);
    hidStartSixAxisSensor(handles[2]);
    hidStartSixAxisSensor(handles[3]);

	hidInitializeVibrationDevices(VibrationDeviceHandles[0], 2, HidNpadIdType_Handheld, HidNpadStyleTag_NpadHandheld);
	memset(VibrationValues, 0, sizeof(VibrationValues));
    memset(&VibrationValue_stop, 0, sizeof(HidVibrationValue));

	// Switch like stop behavior with muted band channels and frequencies set to default.
    VibrationValue_stop.freq_low  = 160.0f;
    VibrationValue_stop.freq_high = 320.0f;

#endif // VITA
}

