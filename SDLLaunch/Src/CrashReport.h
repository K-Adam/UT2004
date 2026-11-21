// The part of the log a crash report carries, with what identifies the player taken out.

#pragma once

// Replaces every match of Find in any case. With ValueEnd the match stays and what follows it goes, up to one of those characters.
inline FString ReplaceAll( const FString& Text, const FString& Find, const TCHAR* With, const TCHAR* ValueEnd=NULL )
{
	FString Caps = Text.Caps(), Key = Find.Caps(), Result;
	INT From = 0;
	for( const TCHAR* Hit; (Hit = appStrstr( *Caps + From, *Key )) != NULL; )
	{
		INT End = (INT)(Hit - *Caps) + Key.Len();
		Result += Text.Mid( From, End - From - (ValueEnd ? 0 : Key.Len()) );
		while( ValueEnd && End < Text.Len() && !appStrchr( ValueEnd, Text[End] ) )
			End++;
		Result += With;
		From = End;
	}
	return Result + Text.Mid( From );
}

// Four numbers up to 255. A driver version has a larger one and stays readable.
inline bool IsAddress( const TCHAR* P )
{
	for( INT Parts = 1; ; Parts++, P++ )
	{
		INT Value = 0, Digits = 0;
		for( ; appIsDigit(*P); P++, Digits++ )
			Value = Min( Value * 10 + *P - '0', 1000 );
		if( !Digits || Value > 255 )
			return false;
		if( *P != '.' )
			return !*P && Parts == 4;
	}
}

inline FString RedactAddresses( const FString& Text )
{
	FString Result;
	INT Copied = 0;
	for( INT i = 0; i < Text.Len(); )
	{
		// A token starts at a digit, so dots before an address do not hide it.
		if( !appIsDigit(Text[i]) )
		{
			i++;
			continue;
		}
		INT End = i;
		while( End < Text.Len() && (appIsDigit(Text[End]) || Text[End] == '.') )
			End++;
		// A sentence ends in a dot too.
		while( End > i && Text[End - 1] == '.' )
			End--;
		if( IsAddress( *Text.Mid( i, End - i ) ) )
		{
			Result += Text.Mid( Copied, i - Copied ) + TEXT("<ip>");
			Copied = End;
		}
		i = End;
	}
	return Result + Text.Mid( Copied );
}

// Names are the user, computer and player names. A short one would match all over the log and is left in.
inline FString RedactLog( FString Text, const TArray<FString>& Names )
{
	for( INT i = 0; i < Names.Num(); i++ )
		if( Names(i).Len() >= 3 )
			Text = ReplaceAll( Text, Names(i), TEXT("<name>") );
	Text = ReplaceAll( Text, TEXT("Password="), TEXT("<password>"), TEXT("?& \t\r\n") );
	Text = ReplaceAll( Text, TEXT(" id="), TEXT("<id>"), TEXT(" \t\r\n") );
	return RedactAddresses( Text );
}

// The last MaxChars characters, from the start of a line on.
inline FString LogTail( const FString& Text, INT MaxChars )
{
	if( Text.Len() <= MaxChars )
		return Text;
	FString Tail = Text.Right( MaxChars );
	return Tail.Mid( Tail.InStr( TEXT("\n") ) + 1 );
}
