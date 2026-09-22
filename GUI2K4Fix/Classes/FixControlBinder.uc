// Without a mouse, Left and Right pick the column that Enter binds and Backspace clears.
class FixControlBinder extends ControlBinder;

var int KeyCol;
var float KeyMouseX, KeyMouseY;

function bool KeyMode()
{
	return (Controller.MouseX == KeyMouseX && Controller.MouseY == KeyMouseY) || !(Super.MouseOnCol1() || Super.MouseOnCol2());
}

function bool ListOnKeyEvent(out byte Key, out byte State, float delta)
{
	local Interactions.EInputKey iKey;

	iKey = EInputKey(Key);
	// Enter and Backspace keep following the mouse when it rests on a column.
	if ( State == 1 && iKey != IK_Enter && iKey != IK_Backspace )
	{
		KeyMouseX = Controller.MouseX;
		KeyMouseY = Controller.MouseY;
		if ( iKey == IK_Left || iKey == IK_Right )
		{
			KeyCol = int(iKey == IK_Right);
			return true;
		}
	}
	return Super.ListOnKeyEvent(Key, State, delta);
}

function bool MouseOnCol1()
{
	if ( KeyMode() )
		return KeyCol == 0;
	return Super.MouseOnCol1();
}

function bool MouseOnCol2()
{
	if ( KeyMode() )
		return KeyCol == 1;
	return Super.MouseOnCol2();
}

function DrawBinding(Canvas Canvas, int Item, float X, float Y, float W, float H, bool bSelected, bool bPending)
{
	local float CellLeft, CellWidth;

	Super.DrawBinding(Canvas, Item, X, Y, W, H, bSelected, bPending);
	if ( !bSelected || bPendingRawInput || !KeyMode() || !ValidBindIndex(li_Binds.SortData[Item].SortItem) || Bindings[li_Binds.SortData[Item].SortItem].bIsSectionLabel )
		return;

	li_Binds.GetCellLeftWidth(KeyCol + 1, CellLeft, CellWidth);
	Canvas.SetDrawColor(255, 255, 255);
	Canvas.SetPos(CellLeft, Y);
	Canvas.DrawBox(Canvas, CellWidth - 2, H - 2);
}

defaultproperties
{
	KeyMouseX=-1
}
