// Server browser with the Web column and the "Web Capable Servers Only" filter
class FixServerBrowser extends UT2K4ServerBrowser;

var() globalconfig bool bWebServersOnly;

function InitComponent( GUIController MyController, GUIComponent MyOwner )
{
	Super.InitComponent(MyController, MyOwner);

	FixBrowserFooter(t_Footer).ch_Web.OnChange = WebOptionChanged;
	FixBrowserFooter(t_Footer).ch_Web.SetComponentValue(bWebServersOnly, True);
}

function InitializeGameTypeCombo( optional bool ClearFirst )
{
	local int i;

	Super.InitializeGameTypeCombo(ClearFirst);
	for ( i = 0; i < co_GameType.ItemCount(); i++ )
		co_GameType.MyComboBox.List.SetObjectAtIndex(i, new(None) class'FixServersList');
}

function WebOptionChanged( GUIComponent Sender )
{
	if ( moCheckBox(Sender).IsChecked() != bWebServersOnly )
	{
		bWebServersOnly = moCheckBox(Sender).IsChecked();
		SaveConfig();
		if ( tp_Active != None )
			tp_Active.RefreshClicked();
	}
}

defaultproperties
{
	PanelClass(2)="GUI2K4Fix.FixServerListPageFavorites"
	PanelClass(3)="GUI2K4Fix.FixServerListPageLAN"
	PanelClass(5)="GUI2K4Fix.FixServerListPageInternet"

	Begin Object Class=FixBrowserFooter Name=FixFooterPanel
		WinWidth=1.000000
		WinLeft=0.000000
		WinTop=0.917943
		TabOrder=4
	End Object
	t_Footer=FixFooterPanel
}
