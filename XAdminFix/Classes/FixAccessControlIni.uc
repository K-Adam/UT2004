class FixAccessControlIni extends AccessControlIni;

var LoginThrottle Throttle;

event PreBeginPlay()
{
	Super.PreBeginPlay();
	Throttle = new class'LoginThrottle';
}

// Nobody chose these: an empty password, and the Admin and Admin pair that retail creates and an old xAdmin.ini still holds.
function bool UnsetPassword( string Username, string Password )
{
	return Password == "" || (Username ~= "Admin" && Password == "Admin");
}

// Also decides the admin login in the join URL, which skips the game password.
function bool ValidLogin( string Username, string Password )
{
	return !UnsetPassword(Username, Password) && Super.ValidLogin(Username, Password);
}

function bool AdminLogin( PlayerController P, string Username, string Password )
{
	local bool bOk;

	if ( UnsetPassword(Username, Password) || Throttle.AdminBlocked(self, P, Username, Password) )
		return false;
	bOk = Super.AdminLogin(P, Username, Password);
	Throttle.AdminDone(self, P, Username, Password, bOk);
	return bOk;
}

event PreLogin( string Options, string Address, string PlayerID, out string Error, out string FailCode, bool bSpectator )
{
	if ( Throttle.PreLoginBlocked(self, Options, Address, Error, FailCode) )
		return;
	Super.PreLogin(Options, Address, PlayerID, Error, FailCode, bSpectator);
	Throttle.PreLoginDone(self, Options, Address);
}

defaultproperties
{
	bDontAddDefaultAdmin=True
}
