class FixDetailSettings extends Settings_Tabs;

var automated GUISectionBackground sb_Display, sb_Options;
var automated moComboBox co_Resolution;
var automated moCheckBox ch_Fullscreen, ch_Shadows, ch_Antialiasing;
var automated moSlider sl_Gamma;
var() localized string ResolutionText[2];

function InitComponent(GUIController MyController, GUIComponent MyOwner)
{
	Super.InitComponent(MyController, MyOwner);

	co_Resolution.AddItem(ResolutionText[0]);
	co_Resolution.AddItem(ResolutionText[1]);
	co_Resolution.ReadOnly(true);

	sb_Display.ManageComponent(co_Resolution);
	sb_Display.ManageComponent(ch_Fullscreen);
	sb_Display.ManageComponent(sl_Gamma);
	sb_Options.ManageComponent(ch_Shadows);
	sb_Options.ManageComponent(ch_Antialiasing);
}

function InternalOnLoadINI(GUIComponent Sender, string s)
{
	local PlayerController PC;

	PC = PlayerOwner();
	switch (Sender)
	{
	case co_Resolution:
		co_Resolution.SilentSetIndex(int(bool(PC.ConsoleCommand("get ini:Engine.Engine.RenderDevice HalfResolution"))));
		break;
	case ch_Fullscreen:
		ch_Fullscreen.SetComponentValue(bool(PC.ConsoleCommand("ISFULLSCREEN")), true);
		break;
	case sl_Gamma:
		sl_Gamma.SetComponentValue(float(PC.ConsoleCommand("get ini:Engine.Engine.ViewportManager Gamma")), true);
		break;
	case ch_Shadows:
		ch_Shadows.SetComponentValue(class'UnrealPawn'.default.bPlayerShadows, true);
		break;
	case ch_Antialiasing:
		ch_Antialiasing.SetComponentValue(bool(PC.ConsoleCommand("get ini:Engine.Engine.RenderDevice Antialiasing")), true);
		break;
	}
}

function InternalOnChange(GUIComponent Sender)
{
	local PlayerController PC;

	Super.InternalOnChange(Sender);

	PC = PlayerOwner();
	switch (Sender)
	{
	case co_Resolution:
		PC.ConsoleCommand("set ini:Engine.Engine.RenderDevice HalfResolution" @ (co_Resolution.GetIndex() == 1));
		break;
	case ch_Fullscreen:
		if (ch_Fullscreen.IsChecked() != bool(PC.ConsoleCommand("ISFULLSCREEN")))
			PC.ConsoleCommand("TOGGLEFULLSCREEN");
		break;
	case sl_Gamma:
		PC.ConsoleCommand("GAMMA" @ sl_Gamma.GetValue());
		break;
	case ch_Shadows:
		SetShadows(ch_Shadows.IsChecked());
		break;
	case ch_Antialiasing:
		PC.ConsoleCommand("set ini:Engine.Engine.RenderDevice Antialiasing" @ ch_Antialiasing.IsChecked());
		break;
	}
}

function SetShadows(bool bOn)
{
	local UnrealPawn P;

	P = UnrealPawn(PlayerOwner().Pawn);
	if (P != None)
	{
		P.bPlayerShadows = bOn;
		P.bBlobShadow = false;
	}
	class'UnrealPawn'.default.bPlayerShadows = bOn;
	class'UnrealPawn'.default.bBlobShadow = false;
	class'UnrealPawn'.static.StaticSaveConfig();
	class'Vehicle'.default.bVehicleShadows = bOn;
	class'Vehicle'.static.StaticSaveConfig();
}

function ResetClicked()
{
	local PlayerController PC;
	local int i;

	Super.ResetClicked();

	PC = PlayerOwner();
	PC.ConsoleCommand("set ini:Engine.Engine.RenderDevice HalfResolution False");
	PC.ConsoleCommand("set ini:Engine.Engine.RenderDevice Antialiasing True");
	PC.ConsoleCommand("GAMMA 1.0");
	SetShadows(true);

	for (i = 0; i < Components.Length; i++)
		Components[i].LoadINI();
}

// Alt+Enter changes the mode while the menu is open.
event ResolutionChanged(int ResX, int ResY)
{
	Super.ResolutionChanged(ResX, ResY);
	ch_Fullscreen.LoadINI();
}

defaultproperties
{
	Begin Object class=GUISectionBackground Name=sbDisplay
		WinWidth=0.491849
		WinHeight=0.300000
		WinLeft=0.000948
		WinTop=0.012761
		Caption="Display"
		RenderWeight=0.01
	End Object
	sb_Display=sbDisplay

	Begin Object class=GUISectionBackground Name=sbOptions
		WinWidth=0.502751
		WinHeight=0.300000
		WinLeft=0.495826
		WinTop=0.012761
		Caption="Options"
		RenderWeight=0.01
	End Object
	sb_Options=sbOptions

	Begin Object class=moComboBox Name=ResolutionCombo
		Caption="Resolution"
		Hint="Half renders the world at half the window size, the HUD stays sharp."
		IniOption="@Internal"
		CaptionWidth=0.55
		bHeightFromComponent=false
		OnLoadINI=InternalOnLoadINI
		OnChange=InternalOnChange
		TabOrder=0
	End Object
	co_Resolution=ResolutionCombo

	Begin Object class=moCheckBox Name=FullscreenCheck
		Caption="Full Screen"
		Hint="Fills the desktop, otherwise the game runs in a resizable window."
		IniOption="@Internal"
		CaptionWidth=0.94
		bSquare=true
		ComponentJustification=TXTA_Left
		OnLoadINI=InternalOnLoadINI
		OnChange=InternalOnChange
		TabOrder=1
	End Object
	ch_Fullscreen=FullscreenCheck

	Begin Object class=moSlider Name=GammaSlider
		Caption="Gamma"
		Hint="Adjusts the gamma of the world to suit your monitor."
		MinValue=0.5
		MaxValue=2.5
		IniOption="@Internal"
		CaptionWidth=0.55
		ComponentWidth=-1
		LabelJustification=TXTA_Left
		bHeightFromComponent=false
		SliderCaptionStyleName=""
		OnLoadINI=InternalOnLoadINI
		OnChange=InternalOnChange
		TabOrder=2
	End Object
	sl_Gamma=GammaSlider

	Begin Object class=moCheckBox Name=ShadowsCheck
		Caption="Dynamic Shadows"
		Hint="Characters and vehicles cast shadows."
		IniOption="@Internal"
		CaptionWidth=0.94
		bSquare=true
		ComponentJustification=TXTA_Left
		OnLoadINI=InternalOnLoadINI
		OnChange=InternalOnChange
		TabOrder=3
	End Object
	ch_Shadows=ShadowsCheck

	Begin Object class=moCheckBox Name=AntialiasingCheck
		Caption="Antialiasing"
		Hint="Smooths jagged edges."
		IniOption="@Internal"
		CaptionWidth=0.94
		bSquare=true
		ComponentJustification=TXTA_Left
		OnLoadINI=InternalOnLoadINI
		OnChange=InternalOnChange
		TabOrder=4
	End Object
	ch_Antialiasing=AntialiasingCheck

	ResolutionText(0)="Full"
	ResolutionText(1)="Half"

	WinTop=0.15
	WinLeft=0
	WinWidth=1
	WinHeight=0.74
	bAcceptsInput=false
	PanelCaption="Display"
}
