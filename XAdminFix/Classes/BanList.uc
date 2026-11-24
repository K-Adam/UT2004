// Retail bans either the ID or the IP. A client chooses its own ID, and all clients without a CD key share one.
// Used by both access control subclasses.
class BanList extends Object;

static function bool IsSharedID( string ID )
{
	return ID ~= "0aa1700a23b72d6d8cf32fc70dda27ed" || ID ~= "6295f0aadbde98d7e4aa9c6fc34f9ead";
}

static function bool Add( AccessControl AC, PlayerController C, bool bSession )
{
	local string IP, ID;
	local int Port;

	IP = C.GetPlayerNetworkAddress();
	if ( AC.CheckIPPolicy(IP) != 0 )
		return false;
	Port = InStr(IP, ":");
	if ( Port != -1 )
		IP = Left(IP, Port);
	ID = C.GetPlayerIDHash();
	Log("Adding"@Eval(bSession, "Session", "Global")@"Ban for: "$IP@ID@C.PlayerReplicationInfo.PlayerName);

	if ( bSession )
	{
		if ( IP != "" )
			AC.SessionIPPolicies[AC.SessionIPPolicies.Length] = "DENY;"$IP;
		if ( AC.bBanByID && !IsSharedID(ID) )
			AC.SessionBannedIDs[AC.SessionBannedIDs.Length] = ID$"|"$C.PlayerReplicationInfo.PlayerName;
	}
	else
	{
		if ( IP != "" )
			AC.IPPolicies[AC.IPPolicies.Length] = "DENY;"$IP;
		if ( AC.bBanByID && !IsSharedID(ID) )
			AC.BannedIDs[AC.BannedIDs.Length] = ID@C.PlayerReplicationInfo.PlayerName;
	}
	AC.SaveConfig();
	C.ClientNetworkMessage(Eval(bSession, "AC_SessionBan", "AC_Ban"), AC.Level.Game.GameReplicationInfo.AdminEmail);
	return true;
}

static function bool BanPlayer( AccessControl AC, PlayerController C, bool bSession )
{
	if ( AC.IsAdmin(C) || !Add(AC, C, bSession) )
		return false;
	if ( C.Pawn != None )
		C.Pawn.Destroy();
	C.Destroy();
	return true;
}

static function bool KickBanPlayer( AccessControl AC, PlayerController P )
{
	if ( AC.IsAdmin(P) )
		return false;
	if ( !Add(AC, P, false) )
		P.ClientNetworkMessage("AC_Kicked", AC.DefaultKickReason);
	if ( P.Pawn != None )
		P.Pawn.Destroy();
	P.Destroy();
	return true;
}
