// Adds the Web column, the web filter and the WebRTC join details. Servers advertise "WebRTC=<port>,<fingerprint>" in their ping reply.
class FixServersList extends UT2K4Browser_ServersList;

var Material WebIcon;
var bool bShiftCells;

static function string WebRtcDetails( GameInfo.ServerResponseLine s )
{
	local int i;

	for ( i = 0; i < s.ServerInfo.Length; i++ )
		if ( s.ServerInfo[i].Key == "WebRTC" )
			return s.ServerInfo[i].Value;
	return "";
}

function Connect( bool Spectator )
{
	local string URL, WebRtc;

	if ( !IsValid() )
		return;

	URL = PlayerOwner().GetURLProtocol()$"://"$Servers[CurrentListId()].IP$":"$string(Servers[CurrentListId()].Port);
	if ( Spectator )
		URL $= "?SpectatorOnly=1";
	if ( tp_MyPage.ConnectLAN )
		URL $= "?LAN";
	WebRtc = WebRtcDetails(Servers[CurrentListId()]);
	// The details come from the server or a master, so they must not carry further URL options
	if ( WebRtc != "" && InStr(WebRtc, "?") < 0 && InStr(WebRtc, "#") < 0 )
		URL $= "?WebRTC="$WebRtc;

	Controller.CloseAll(false, True);
	PlayerOwner().ClientTravel(URL, TRAVEL_Absolute, False);
}

function MyOnReceivedServer( GameInfo.ServerResponseLine s )
{
	// LAN replies already carry the details, master rows only get them from the ping
	if ( tp_MyPage.ConnectLAN && class'FixServerBrowser'.default.bWebServersOnly && WebRtcDetails(s) == "" )
		return;
	Super.MyOnReceivedServer(s);
}

function AutoPingServers()
{
	local int i;
	local bool bStopped;

	Super.AutoPingServers();

	// Replies are matched to rows by index and removing moves the last row, so rows only go once the auto pings are done
	// and the clicked row's refresh ping is cancelled
	if ( OutstandingPings.Length == 0 && class'FixServerBrowser'.default.bWebServersOnly )
		for ( i = Servers.Length - 1; i >= 0; i-- )
			if ( Servers[i].Ping != 9999 && WebRtcDetails(Servers[i]) == "" )
			{
				if ( !bStopped )
					StopPings();
				bStopped = True;
				RemoveServerAt(i);
				if ( i < Servers.Length )
					UpdatedItem(i);
			}
}

// The retail drawing addresses cells 0 to 4, the Web column sits in cell 1 so its cells move one to the right
function GetCellLeftWidth( int Column, out float Left, out float Width )
{
	if ( bShiftCells && Column > 0 )
		Column++;
	Super.GetCellLeftWidth(Column, Left, Width);
}

function MyOnDrawItem( Canvas Canvas, int i, float X, float Y, float W, float H, bool bSelected, bool bPending )
{
	local float CellLeft, CellWidth;

	bShiftCells = True;
	Super.MyOnDrawItem(Canvas, i, X, Y, W, H, bSelected, bPending);
	bShiftCells = False;

	// Same icon and size as the flag icons in the first column
	if ( WebRtcDetails(Servers[SortData[i].SortItem]) != "" )
	{
		GetCellLeftWidth(1, CellLeft, CellWidth);
		Canvas.DrawColor = Canvas.MakeColor(255, 255, 255, 255);
		Canvas.SetPos(CellLeft + (CellWidth - 14) / 2, Y + (H - 14) / 2);
		Canvas.DrawTile(WebIcon, 14, 14, 0, 0, 15, 15);
	}
}

function string GetSortString( int i )
{
	local int Column;
	local string s;

	// The retail keys use the retail column numbers, the Web column sorts by web support, then ping
	Column = SortColumn;
	if ( Column == 1 )
		SortColumn = 4;
	else if ( Column > 1 )
		SortColumn = Column - 1;
	s = Super.GetSortString(i);
	SortColumn = Column;
	if ( Column == 1 )
		s = int(WebRtcDetails(Servers[i]) == "") $ s;
	return s;
}

defaultproperties
{
	ColumnHeadings(1)="Web"
	ColumnHeadings(2)="Server Name"
	ColumnHeadings(3)="Map"
	ColumnHeadings(4)="Players"
	ColumnHeadings(5)="Ping"
	InitColumnPerc(0)=0.09
	InitColumnPerc(1)=0.07
	InitColumnPerc(2)=0.40
	InitColumnPerc(3)=0.25
	InitColumnPerc(4)=0.11
	InitColumnPerc(5)=0.08
	SortColumn=5
	WebIcon=Material'ServerIcons.Standard'
}
