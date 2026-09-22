// Retail moves on to map selection whenever the game type changes, so arrow keys could never step through the list.
// Keys now only change the game type, and Enter or a click moves on as before.
class FixGameType extends UT2K4Tab_GameTypeBase;

var bool bKeyChange;

function InitComponent(GUIController MyController, GUIComponent MyOwner)
{
	Super.InitComponent(MyController, MyOwner);
	li_Games.OnKeyEvent = ListKeyEvent;
}

function bool ListKeyEvent(out byte Key, out byte State, float Delta)
{
	local bool bResult;

	// On press like buttons, so the release of the key that opened this page does not commit. Treated as a click.
	if ( State == 1 && EInputKey(Key) == IK_Enter )
	{
		InternalOnChange(lb_Games);
		return true;
	}

	bKeyChange = true;
	bResult = li_Games.InternalOnKeyEvent(Key, State, Delta);
	bKeyChange = false;
	return bResult;
}

function InternalOnChange(GUIComponent Sender)
{
	local int Index;

	if ( !bKeyChange || Sender != lb_Games )
	{
		Super.InternalOnChange(Sender);
		return;
	}

	Index = FindRecordIndex(li_Games.Get());
	if ( Index < 0 || Index >= GameTypes.Length || li_Games.IsSection() )
		return;

	if ( GameTypes[Index].ClassName != Controller.LastGameType )
	{
		InitPreview();
		Controller.LastGameType = GameTypes[Index].ClassName;
	}
	OnChangeGameType(True);
}
