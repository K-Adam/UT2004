class FixDetailSettings extends Settings_Tabs;

var automated GUISectionBackground sb_Display, sb_Options;
var automated moComboBox co_Resolution, co_Reflections;
var automated moCheckBox ch_Fullscreen, ch_Shadows, ch_Antialiasing, ch_AmbientOcclusion, ch_ContactShadows, ch_DepthOfField, ch_VolumetricFog, ch_GlobalIllumination, ch_HeatHaze, ch_MotionBlur, ch_SmoothTerrain;
var automated moSlider sl_Gamma;
var() localized string ResolutionText[2], ReflectionsText[2];

function InitComponent(GUIController MyController, GUIComponent MyOwner)
{
	Super.InitComponent(MyController, MyOwner);

	co_Resolution.AddItem(ResolutionText[0]);
	co_Resolution.AddItem(ResolutionText[1]);
	co_Resolution.ReadOnly(true);
	co_Reflections.AddItem(ReflectionsText[0]);
	co_Reflections.AddItem(ReflectionsText[1]);
	co_Reflections.ReadOnly(true);

	sb_Display.ManageComponent(co_Resolution);
	sb_Display.ManageComponent(ch_Fullscreen);
	sb_Display.ManageComponent(sl_Gamma);
	sb_Display.ManageComponent(co_Reflections);
	sb_Options.ManageComponent(ch_Shadows);
	sb_Options.ManageComponent(ch_Antialiasing);
	sb_Options.ManageComponent(ch_AmbientOcclusion);
	sb_Options.ManageComponent(ch_ContactShadows);
	sb_Options.ManageComponent(ch_DepthOfField);
	sb_Options.ManageComponent(ch_VolumetricFog);
	sb_Options.ManageComponent(ch_GlobalIllumination);
	sb_Options.ManageComponent(ch_HeatHaze);
	sb_Options.ManageComponent(ch_MotionBlur);
	sb_Options.ManageComponent(ch_SmoothTerrain);
}

function InternalOnLoadINI(GUIComponent Sender, string s)
{
	local PlayerController PC;

	PC = PlayerOwner();
	switch (Sender)
	{
	case co_Resolution:
		co_Resolution.SilentSetIndex(int(int(PC.ConsoleCommand("get ini:Engine.Engine.RenderDevice ResolutionScale")) < 100));
		break;
	case co_Reflections:
		co_Reflections.SilentSetIndex(int(bool(PC.ConsoleCommand("get ini:Engine.Engine.RenderDevice ScreenSpaceReflections"))));
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
	case ch_AmbientOcclusion:
		ch_AmbientOcclusion.SetComponentValue(bool(PC.ConsoleCommand("get ini:Engine.Engine.RenderDevice AmbientOcclusion")), true);
		break;
	case ch_ContactShadows:
		ch_ContactShadows.SetComponentValue(bool(PC.ConsoleCommand("get ini:Engine.Engine.RenderDevice ContactShadows")), true);
		break;
	case ch_DepthOfField:
		ch_DepthOfField.SetComponentValue(bool(PC.ConsoleCommand("get ini:Engine.Engine.RenderDevice DepthOfField")), true);
		break;
	case ch_VolumetricFog:
		ch_VolumetricFog.SetComponentValue(bool(PC.ConsoleCommand("get ini:Engine.Engine.RenderDevice VolumetricFog")), true);
		break;
	case ch_GlobalIllumination:
		ch_GlobalIllumination.SetComponentValue(bool(PC.ConsoleCommand("get ini:Engine.Engine.RenderDevice GlobalIllumination")), true);
		break;
	case ch_HeatHaze:
		ch_HeatHaze.SetComponentValue(bool(PC.ConsoleCommand("get ini:Engine.Engine.RenderDevice HeatHaze")), true);
		break;
	case ch_MotionBlur:
		ch_MotionBlur.SetComponentValue(bool(PC.ConsoleCommand("get ini:Engine.Engine.RenderDevice MotionBlur")), true);
		break;
	case ch_SmoothTerrain:
		ch_SmoothTerrain.SetComponentValue(bool(PC.ConsoleCommand("get ini:Engine.Engine.RenderDevice SmoothTerrain")), true);
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
		PC.ConsoleCommand("set ini:Engine.Engine.RenderDevice ResolutionScale" @ (100 - 25 * co_Resolution.GetIndex()));
		break;
	case co_Reflections:
		PC.ConsoleCommand("set ini:Engine.Engine.RenderDevice ScreenSpaceReflections" @ bool(co_Reflections.GetIndex()));
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
	case ch_AmbientOcclusion:
		PC.ConsoleCommand("set ini:Engine.Engine.RenderDevice AmbientOcclusion" @ ch_AmbientOcclusion.IsChecked());
		break;
	case ch_ContactShadows:
		PC.ConsoleCommand("set ini:Engine.Engine.RenderDevice ContactShadows" @ ch_ContactShadows.IsChecked());
		break;
	case ch_DepthOfField:
		PC.ConsoleCommand("set ini:Engine.Engine.RenderDevice DepthOfField" @ ch_DepthOfField.IsChecked());
		break;
	case ch_VolumetricFog:
		PC.ConsoleCommand("set ini:Engine.Engine.RenderDevice VolumetricFog" @ ch_VolumetricFog.IsChecked());
		break;
	case ch_GlobalIllumination:
		PC.ConsoleCommand("set ini:Engine.Engine.RenderDevice GlobalIllumination" @ ch_GlobalIllumination.IsChecked());
		break;
	case ch_HeatHaze:
		PC.ConsoleCommand("set ini:Engine.Engine.RenderDevice HeatHaze" @ ch_HeatHaze.IsChecked());
		break;
	case ch_MotionBlur:
		PC.ConsoleCommand("set ini:Engine.Engine.RenderDevice MotionBlur" @ ch_MotionBlur.IsChecked());
		break;
	case ch_SmoothTerrain:
		PC.ConsoleCommand("set ini:Engine.Engine.RenderDevice SmoothTerrain" @ ch_SmoothTerrain.IsChecked());
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
	PC.ConsoleCommand("RESETRENDEROPTIONS");
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
		WinHeight=0.400000
		WinLeft=0.000948
		WinTop=0.012761
		Caption="Display"
		RenderWeight=0.01
	End Object
	sb_Display=sbDisplay

	Begin Object class=GUISectionBackground Name=sbOptions
		WinWidth=0.502751
		WinHeight=0.580000
		WinLeft=0.495826
		WinTop=0.012761
		Caption="Options"
		RenderWeight=0.01
	End Object
	sb_Options=sbOptions

	Begin Object class=moComboBox Name=ResolutionCombo
		Caption="Resolution"
		Hint="75% renders the 3D scene at three quarters of the window size, the HUD stays sharp."
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

	Begin Object class=moComboBox Name=ReflectionsCombo
		Caption="Reflections"
		Hint="Normal adds reflections of the scene to shiny surfaces, Low shows only their reflection maps."
		IniOption="@Internal"
		CaptionWidth=0.55
		bHeightFromComponent=false
		OnLoadINI=InternalOnLoadINI
		OnChange=InternalOnChange
		TabOrder=11
	End Object
	co_Reflections=ReflectionsCombo

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

	Begin Object class=moCheckBox Name=AmbientOcclusionCheck
		Caption="Ambient Occlusion"
		Hint="Darkens corners and creases where light is blocked."
		IniOption="@Internal"
		CaptionWidth=0.94
		bSquare=true
		ComponentJustification=TXTA_Left
		OnLoadINI=InternalOnLoadINI
		OnChange=InternalOnChange
		TabOrder=5
	End Object
	ch_AmbientOcclusion=AmbientOcclusionCheck

	Begin Object class=moCheckBox Name=ContactShadowsCheck
		Caption="Contact Shadows"
		Hint="Small shadows where objects, pickups and the weapon meet surfaces, cast from the strongest nearby light."
		IniOption="@Internal"
		CaptionWidth=0.94
		bSquare=true
		ComponentJustification=TXTA_Left
		OnLoadINI=InternalOnLoadINI
		OnChange=InternalOnChange
		TabOrder=6
	End Object
	ch_ContactShadows=ContactShadowsCheck

	Begin Object class=moCheckBox Name=DepthOfFieldCheck
		Caption="Depth of Field"
		Hint="Softens what is much nearer or farther than the point you look at, like a camera lens."
		IniOption="@Internal"
		CaptionWidth=0.94
		bSquare=true
		ComponentJustification=TXTA_Left
		OnLoadINI=InternalOnLoadINI
		OnChange=InternalOnChange
		TabOrder=7
	End Object
	ch_DepthOfField=DepthOfFieldCheck

	Begin Object class=moCheckBox Name=VolumetricFogCheck
		Caption="Volumetric Fog"
		Hint="Lights glow in a thin haze, sunlight streams through gaps and distant fog fades in more naturally."
		IniOption="@Internal"
		CaptionWidth=0.94
		bSquare=true
		ComponentJustification=TXTA_Left
		OnLoadINI=InternalOnLoadINI
		OnChange=InternalOnChange
		TabOrder=8
	End Object
	ch_VolumetricFog=VolumetricFogCheck

	Begin Object class=moCheckBox Name=GlobalIlluminationCheck
		Caption="Global Illumination"
		Hint="Light bounces off lit surfaces onto those nearby, tinted by their colour."
		IniOption="@Internal"
		CaptionWidth=0.94
		bSquare=true
		ComponentJustification=TXTA_Left
		OnLoadINI=InternalOnLoadINI
		OnChange=InternalOnChange
		TabOrder=9
	End Object
	ch_GlobalIllumination=GlobalIlluminationCheck

	Begin Object class=moCheckBox Name=HeatHazeCheck
		Caption="Heat Haze"
		Hint="The air shimmers over fire, lava and firing weapons, and explosions send out a blast wave."
		IniOption="@Internal"
		CaptionWidth=0.94
		bSquare=true
		ComponentJustification=TXTA_Left
		OnLoadINI=InternalOnLoadINI
		OnChange=InternalOnChange
		TabOrder=10
	End Object
	ch_HeatHaze=HeatHazeCheck

	Begin Object class=moCheckBox Name=MotionBlurCheck
		Caption="Motion Blur"
		Hint="Blurs the view slightly while the camera turns or moves."
		IniOption="@Internal"
		CaptionWidth=0.94
		bSquare=true
		ComponentJustification=TXTA_Left
		OnLoadINI=InternalOnLoadINI
		OnChange=InternalOnChange
		TabOrder=12
	End Object
	ch_MotionBlur=MotionBlurCheck

	Begin Object class=moCheckBox Name=SmoothTerrainCheck
		Caption="Smooth Terrain"
		Hint="Rounds off the facets of hills. Applied when a map loads, so reopen the map after changing it."
		IniOption="@Internal"
		CaptionWidth=0.94
		bSquare=true
		ComponentJustification=TXTA_Left
		OnLoadINI=InternalOnLoadINI
		OnChange=InternalOnChange
		TabOrder=13
	End Object
	ch_SmoothTerrain=SmoothTerrainCheck

	ResolutionText(0)="Full"
	ResolutionText(1)="75%"
	ReflectionsText(0)="Low"
	ReflectionsText(1)="Normal"

	WinTop=0.15
	WinLeft=0
	WinWidth=1
	WinHeight=0.74
	bAcceptsInput=false
	PanelCaption="Display"
}
