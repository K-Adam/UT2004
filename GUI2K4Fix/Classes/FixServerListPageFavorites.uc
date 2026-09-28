class FixServerListPageFavorites extends UT2K4Browser_ServerListPageFavorites;

function InitServerList()
{
	if ( li_Server == None )
		li_Server = new(None) class'FixServersList';
	Super.InitServerList();
}

defaultproperties
{
	HeaderColumnSizes(0)=(ColumnSizes=(0.09,0.07,0.40,0.25,0.11,0.08))
}
