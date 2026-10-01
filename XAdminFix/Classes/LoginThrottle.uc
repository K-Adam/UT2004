// Retail checks admin and game passwords without any server side limit. Each failure locks the address
// out for twice as long as the previous one, up to ten minutes. Used by both access control subclasses.
class LoginThrottle extends Object;

struct Attempt
{
	var string Key;
	var int Failures;
	var float Until;
};
var array<Attempt> Attempts;

static function string StripPort( string Address )
{
	local int i;

	i = InStr(Address, ":");
	if ( i >= 0 )
		return Left(Address, i);
	return Address;
}

// Webadmin logs in through a spectator without a connection, so the address comes from the request carrying these credentials.
static function string AdminKey( Actor A, PlayerController P, string Username, string Password )
{
	local WebConnection C;

	if ( P != None && P.GetPlayerNetworkAddress() != "" )
		return StripPort(P.GetPlayerNetworkAddress());
	foreach A.DynamicActors(class'WebConnection', C)
		if ( C.Request != None && C.Request.Username == Username && C.Request.Password == Password )
			return StripPort(C.IpAddrToString(C.RemoteAddr));
	return "";
}

// Browsers ask without credentials first, which guesses nothing. Without an address it is a local
// player or the join time check of a password PreLogin already counted.
function bool AdminBlocked( Actor A, PlayerController P, string Username, string Password )
{
	local string Key;

	Key = AdminKey(A, P, Username, Password);
	return Password != "" && Key != "" && Blocked(Key, A.Level.TimeSeconds);
}

function AdminDone( Actor A, PlayerController P, string Username, string Password, bool bOk )
{
	local string Key;

	Key = AdminKey(A, P, Username, Password);
	if ( Password != "" && Key != "" )
		Record(Key, bOk, A.Level.TimeSeconds);
}

function bool PreLoginBlocked( Actor A, string Options, string Address, out string Error, out string FailCode )
{
	if ( A.Level.Game.ParseOption(Options, "Password") == "" || !Blocked(StripPort(Address), A.Level.TimeSeconds) )
		return false;
	Error = "";
	FailCode = "WRONGPW";
	return true;
}

// The failure code can't tell a wrong guess from a full server, so the password is checked here.
// GamePassword is private and globalconfig, so it is read as the class default.
function PreLoginDone( AccessControl AC, string Options, string Address )
{
	local string Password;

	Password = AC.Level.Game.ParseOption(Options, "Password");
	if ( Password != "" && !AC.CheckOptionsAdmin(Options) && !(Password ~= AC.ConsoleCommand("get Engine.AccessControl GamePassword")) )
		Record(StripPort(Address), false, AC.Level.TimeSeconds);
}

function bool Blocked( string Key, float Now )
{
	local int i;

	for ( i = 0; i < Attempts.Length; i++ )
		if ( Attempts[i].Key == Key )
			return Now < Attempts[i].Until;
	return false;
}

function Record( string Key, bool bOk, float Now )
{
	local int i, Failures;

	for ( i = Attempts.Length - 1; i >= 0; i-- )
	{
		if ( Attempts[i].Key == Key )
		{
			Failures = Attempts[i].Failures;
			Attempts.Remove(i, 1);
		}
		else if ( Attempts[i].Until < Now - 3600 )
			Attempts.Remove(i, 1);
	}
	if ( bOk )
		return;

	Failures++;
	i = Attempts.Length;
	Attempts.Length = i + 1;
	Attempts[i].Key = Key;
	Attempts[i].Failures = Failures;
	Attempts[i].Until = Now + FMin(2.0 ** Failures, 600);
}
