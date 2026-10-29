// Retail checks admin and game passwords without any server side limit. Each failure locks the address
// out for twice as long as the previous one, up to ten minutes. Used by both access control subclasses.
class LoginThrottle extends Object;

struct Attempt
{
	var string Key;
	var int Failures;
	var int Until;
};
// Only the class defaults are used. Every level creates a new AccessControl and a table in the instance would
// be empty after each map change. Ordered by last failure, oldest first.
var array<Attempt> Attempts;

const MaxAttempts = 256;
const MaxLockout = 600;

// Level.TimeSeconds restarts with every map, so the lockouts run on the wall clock. Counted from the year 2000
// to fit an int, a float can't tell seconds apart at this size.
static function int Now( LevelInfo L )
{
	local int Y, M;

	Y = L.Year;
	M = L.Month;
	if ( M <= 2 )
	{
		Y--;
		M += 12;
	}
	return (365 * Y + Y / 4 - Y / 100 + Y / 400 + (153 * (M - 3) + 2) / 5 + L.Day - 730426) * 86400
		+ L.Hour * 3600 + L.Minute * 60 + L.Second;
}

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
	return Password != "" && Key != "" && Blocked(Key, Now(A.Level));
}

function AdminDone( Actor A, PlayerController P, string Username, string Password, bool bOk )
{
	local string Key;

	Key = AdminKey(A, P, Username, Password);
	if ( Password != "" && Key != "" )
		Record(Key, bOk, Now(A.Level));
}

function bool PreLoginBlocked( Actor A, string Options, string Address, out string Error, out string FailCode )
{
	if ( A.Level.Game.ParseOption(Options, "Password") == "" || !Blocked(StripPort(Address), Now(A.Level)) )
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
		Record(StripPort(Address), false, Now(AC.Level));
}

// The local clock can step back, which must not stretch a lockout beyond the longest one.
function bool Blocked( string Key, int T )
{
	local int i;

	for ( i = 0; i < default.Attempts.Length; i++ )
		if ( default.Attempts[i].Key == Key )
			return T < default.Attempts[i].Until && default.Attempts[i].Until - T <= MaxLockout;
	return false;
}

function Record( string Key, bool bOk, int T )
{
	local int i, Failures;

	for ( i = default.Attempts.Length - 1; i >= 0; i-- )
	{
		if ( default.Attempts[i].Key == Key )
		{
			Failures = default.Attempts[i].Failures;
			default.Attempts.Remove(i, 1);
		}
		else if ( default.Attempts[i].Until < T - 3600 )
			default.Attempts.Remove(i, 1);
	}
	if ( bOk )
		return;

	if ( default.Attempts.Length >= MaxAttempts )
		default.Attempts.Remove(0, default.Attempts.Length - MaxAttempts + 1);

	Failures++;
	i = default.Attempts.Length;
	default.Attempts.Length = i + 1;
	default.Attempts[i].Key = Key;
	default.Attempts[i].Failures = Failures;
	default.Attempts[i].Until = T + int(FMin(2.0 ** Failures, MaxLockout));
}
