class FixInputSettings extends UT2K4Tab_IForceSettings;

var automated GUISectionBackground i_BG4;
var automated moComboBox co_VirtualJoystick;
var automated moCheckBox ch_Gyro, ch_PadInvertY, ch_VJSwap;
var automated moSlider sl_VJScale;
var() localized string VirtualJoystickText[3];

var int iVJ;
var bool bGyro, bPadInvertY, bVJSwap;
var float fVJScale;

const VIEWPORT = "ini:Engine.Engine.ViewportManager";

event InitComponent(GUIController MyController, GUIComponent MyOwner)
{
	local int i;

	Super.InitComponent(MyController, MyOwner);

	for (i = 0; i < ArrayCount(VirtualJoystickText); i++)
		co_VirtualJoystick.AddItem(VirtualJoystickText[i]);
	co_VirtualJoystick.ReadOnly(true);

	i_BG1.UnmanageComponent(ch_InvertMouse);
	i_BG1.UnmanageComponent(ch_MouseSmoothing);
	i_BG1.UnmanageComponent(ch_MouseLag);
	i_BG3.ManageComponent(ch_InvertMouse);
	i_BG3.ManageComponent(ch_MouseSmoothing);
	i_BG3.ManageComponent(ch_MouseLag);

	i_BG1.ManageComponent(co_VirtualJoystick);
	i_BG1.ManageComponent(ch_Gyro);
	i_BG1.ManageComponent(ch_PadInvertY);

	i_BG4.ManageComponent(sl_VJScale);
	i_BG4.ManageComponent(ch_VJSwap);

	// Retail greys these out anywhere but 32 bit Windows, where the TouchSense mice worked. Here they drive pad rumble.
	i_BG2.ManageComponent(ch_WeaponEffects);
	i_BG2.ManageComponent(ch_PickupEffects);
	i_BG2.ManageComponent(ch_DamageEffects);
	i_BG2.ManageComponent(ch_GUIEffects);
	ch_WeaponEffects.EnableMe();
	ch_PickupEffects.EnableMe();
	ch_DamageEffects.EnableMe();
	ch_GUIEffects.EnableMe();
}

function InternalOnLoadINI(GUIComponent Sender, string s)
{
	local PlayerController PC;

	PC = PlayerOwner();
	switch (Sender)
	{
	case co_VirtualJoystick:
		iVJ = int(PC.ConsoleCommand("get" @ VIEWPORT @ "VirtualJoystick"));
		co_VirtualJoystick.SilentSetIndex(iVJ);
		break;
	case ch_Gyro:
		bGyro = bool(PC.ConsoleCommand("get" @ VIEWPORT @ "PadGyro"));
		ch_Gyro.SetComponentValue(bGyro, true);
		break;
	case ch_PadInvertY:
		bPadInvertY = bool(PC.ConsoleCommand("get" @ VIEWPORT @ "PadInvertY"));
		ch_PadInvertY.SetComponentValue(bPadInvertY, true);
		break;
	case sl_VJScale:
		fVJScale = float(PC.ConsoleCommand("get" @ VIEWPORT @ "VirtualJoystickScale"));
		sl_VJScale.SetComponentValue(fVJScale, true);
		break;
	case ch_VJSwap:
		bVJSwap = bool(PC.ConsoleCommand("get" @ VIEWPORT @ "VirtualJoystickSwap"));
		ch_VJSwap.SetComponentValue(bVJSwap, true);
		break;
	default:
		Super.InternalOnLoadINI(Sender, s);
	}
}

function InternalOnChange(GUIComponent Sender)
{
	Super.InternalOnChange(Sender);
	switch (Sender)
	{
	case co_VirtualJoystick:
		iVJ = co_VirtualJoystick.GetIndex();
		break;
	case ch_Gyro:
		bGyro = ch_Gyro.IsChecked();
		break;
	case ch_PadInvertY:
		bPadInvertY = ch_PadInvertY.IsChecked();
		break;
	case sl_VJScale:
		fVJScale = sl_VJScale.GetValue();
		break;
	case ch_VJSwap:
		bVJSwap = ch_VJSwap.IsChecked();
		break;
	}
}

function SaveSettings()
{
	local PlayerController PC;

	Super.SaveSettings();

	PC = PlayerOwner();
	PC.ConsoleCommand("set" @ VIEWPORT @ "VirtualJoystick" @ iVJ);
	PC.ConsoleCommand("set" @ VIEWPORT @ "PadGyro" @ bGyro);
	PC.ConsoleCommand("set" @ VIEWPORT @ "PadInvertY" @ bPadInvertY);
	PC.ConsoleCommand("set" @ VIEWPORT @ "VirtualJoystickScale" @ fVJScale);
	PC.ConsoleCommand("set" @ VIEWPORT @ "VirtualJoystickSwap" @ bVJSwap);
}

function ResetClicked()
{
	local class ViewportClass;

	ViewportClass = class(DynamicLoadObject(GetNativeClassName("Engine.Engine.ViewportManager"), class'Class'));
	ViewportClass.static.ResetConfig("VirtualJoystick");
	ViewportClass.static.ResetConfig("PadGyro");
	ViewportClass.static.ResetConfig("PadInvertY");
	ViewportClass.static.ResetConfig("VirtualJoystickScale");
	ViewportClass.static.ResetConfig("VirtualJoystickSwap");

	Super.ResetClicked();

	// ResetConfig only reaches the class defaults, the running client follows through set.
	SaveSettings();
}

defaultproperties
{
	Begin Object class=GUISectionBackground Name=FixOptionsBK
		WinWidth=0.381328
		WinHeight=0.600000
		WinLeft=0.021641
		WinTop=0.028176
		Caption="Options"
	End Object
	i_BG1=FixOptionsBK

	Begin Object class=GUISectionBackground Name=FixMouseBK
		WinWidth=0.527812
		WinHeight=0.600000
		WinLeft=0.451289
		WinTop=0.028176
		Caption="Mouse"
	End Object
	i_BG3=FixMouseBK

	Begin Object class=GUISectionBackground Name=FixVirtualJoystickBK
		WinWidth=0.381328
		WinHeight=0.316000
		WinLeft=0.021641
		WinTop=0.655000
		Caption="Virtual Joystick"
	End Object
	i_BG4=FixVirtualJoystickBK

	Begin Object class=GUISectionBackground Name=FixForceFeedbackBK
		WinWidth=0.527812
		WinHeight=0.316000
		WinLeft=0.451289
		WinTop=0.655000
		Caption="Force Feedback"
	End Object
	i_BG2=FixForceFeedbackBK

	Begin Object class=moComboBox Name=VirtualJoystickCombo
		Caption="Virtual Joystick"
		Hint="On-screen sticks and buttons for touch screens. Auto shows them on a touch device until a mouse or a gamepad is used."
		IniOption="@Internal"
		CaptionWidth=0.55
		bHeightFromComponent=false
		OnLoadINI=InternalOnLoadINI
		OnChange=InternalOnChange
		TabOrder=16
	End Object
	co_VirtualJoystick=VirtualJoystickCombo

	Begin Object class=moCheckBox Name=GyroCheck
		Caption="Gyro Aiming"
		Hint="Aim by turning a gamepad that has motion sensors, or the phone or tablet showing the virtual joystick."
		IniOption="@Internal"
		CaptionWidth=0.9
		bSquare=true
		ComponentJustification=TXTA_Left
		OnLoadINI=InternalOnLoadINI
		OnChange=InternalOnChange
		TabOrder=17
	End Object
	ch_Gyro=GyroCheck

	Begin Object class=moCheckBox Name=PadInvertYCheck
		Caption="Invert Look Stick"
		Hint="Pushing the look stick up looks down. Applies to gamepads and the virtual joystick."
		IniOption="@Internal"
		CaptionWidth=0.9
		bSquare=true
		ComponentJustification=TXTA_Left
		OnLoadINI=InternalOnLoadINI
		OnChange=InternalOnChange
		TabOrder=18
	End Object
	ch_PadInvertY=PadInvertYCheck

	Begin Object class=moSlider Name=VJScaleSlider
		Caption="Size"
		Hint="Makes the on-screen sticks and buttons smaller or larger."
		MinValue=0.75
		MaxValue=1.5
		IniOption="@Internal"
		CaptionWidth=0.4
		ComponentWidth=-1
		LabelJustification=TXTA_Left
		bHeightFromComponent=false
		SliderCaptionStyleName=""
		OnLoadINI=InternalOnLoadINI
		OnChange=InternalOnChange
		TabOrder=19
	End Object
	sl_VJScale=VJScaleSlider

	Begin Object class=moCheckBox Name=VJSwapCheck
		Caption="Swap Sticks"
		Hint="Puts the look stick and its buttons on the left and the move stick on the right."
		IniOption="@Internal"
		CaptionWidth=0.9
		bSquare=true
		ComponentJustification=TXTA_Left
		OnLoadINI=InternalOnLoadINI
		OnChange=InternalOnChange
		TabOrder=20
	End Object
	ch_VJSwap=VJSwapCheck

	VirtualJoystickText(0)="Auto"
	VirtualJoystickText(1)="Enabled"
	VirtualJoystickText(2)="Disabled"

	ControlBindMenu="GUI2K4Fix.FixControlBinder"
	SpeechBindMenu="GUI2K4Fix.FixSpeechBinder"
}
