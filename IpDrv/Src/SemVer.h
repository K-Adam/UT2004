// Semantic version precedence, standard library only so the tests can include it without the engine.

#pragma once

#include <cstdlib>
#include <string>
#include <vector>

struct FSemVer
{
	bool Ok = false;
	long Core[3] = {};
	std::vector<std::string> Pre;
};

// The length limit keeps the value inside a long.
inline bool SemVerIsNumber( const std::string& S )
{
	return !S.empty() && S.size() <= 9 && S.find_first_not_of( "0123456789" ) == std::string::npos;
}

inline std::vector<std::string> SemVerSplit( const std::string& S )
{
	std::vector<std::string> Parts;
	size_t Start = 0;
	for( size_t Dot; (Dot = S.find( '.', Start )) != std::string::npos; Start = Dot + 1 )
		Parts.push_back( S.substr( Start, Dot - Start ) );
	Parts.push_back( S.substr( Start ) );
	return Parts;
}

inline FSemVer SemVerParse( std::string S )
{
	FSemVer V;
	// The string comes from the network.
	if( S.size() > 64 )
		return V;
	S = S.substr( 0, S.find( '+' ) );
	size_t Dash = S.find( '-' );
	std::vector<std::string> Core = SemVerSplit( S.substr( 0, Dash ) );
	if( Core.size() != 3 )
		return V;
	for( int i=0; i<3; i++ )
	{
		if( !SemVerIsNumber( Core[i] ) )
			return V;
		V.Core[i] = atol( Core[i].c_str() );
	}
	if( Dash != std::string::npos )
		V.Pre = SemVerSplit( S.substr( Dash + 1 ) );
	V.Ok = true;
	return V;
}

// False when either does not parse, so a build without a release version never counts as outdated.
inline bool SemVerIsNewer( const std::string& Remote, const std::string& Local )
{
	FSemVer R = SemVerParse( Remote ), L = SemVerParse( Local );
	if( !R.Ok || !L.Ok )
		return false;
	for( int i=0; i<3; i++ )
		if( R.Core[i] != L.Core[i] )
			return R.Core[i] > L.Core[i];
	// A release is newer than its own prereleases.
	if( R.Pre.empty() || L.Pre.empty() )
		return R.Pre.empty() && !L.Pre.empty();
	for( size_t i=0; i<R.Pre.size() && i<L.Pre.size(); i++ )
	{
		const std::string &A = R.Pre[i], &B = L.Pre[i];
		bool ANumber = SemVerIsNumber( A ), BNumber = SemVerIsNumber( B );
		if( ANumber && BNumber )
		{
			long X = atol( A.c_str() ), Y = atol( B.c_str() );
			if( X != Y )
				return X > Y;
		}
		else if( ANumber != BNumber )
			return BNumber;
		else if( A != B )
			return A > B;
	}
	return R.Pre.size() > L.Pre.size();
}
