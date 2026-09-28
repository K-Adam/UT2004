class FixBrowserFooter extends UT2K4Browser_Footer;

var automated moCheckBox ch_Web;

function InitComponent( GUIController MyController, GUIComponent MyOwner )
{
	Super.InitComponent(MyController, MyOwner);

	// The status text is right aligned, it gives up its left part to the Web check box
	t_StatusBar.WinLeft = 0.53;
	t_StatusBar.WinWidth = 0.47;
}

defaultproperties
{
	Begin Object class=moCheckBox Name=OnlyWebCheckBox
		bStandardized=False
		WinWidth=0.243945
		WinHeight=0.308203
		WinLeft=0.275
		WinTop=0.093073
		Caption="Web Capable Servers Only"
		Hint="Only display servers that browsers can join over WebRTC."
		CaptionWidth=0.9
		TabOrder=6
		bBoundToParent=True
		bScaleToParent=True
		FontScale=FNS_Small
	End Object
	ch_Web=OnlyWebCheckBox
}
