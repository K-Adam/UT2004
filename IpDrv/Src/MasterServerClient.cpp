/*============================================================================
	MasterServerClient.cpp - master server client

	Revision history:
		* Created by Jack Porter
============================================================================*/

#include "UnIpDrv.h"

/*-----------------------------------------------------------------------------
	FServerQueryLink
-----------------------------------------------------------------------------*/

#define PINGTIMEOUT 2.0f
// How long a query over WebRTC waits for its channel. A browser host is reached through the web master, which takes longer.
#define QUERYOPENTIMEOUT 6.0f
#define QUERYSIGNALTIMEOUT 12.0f
// The list pings its selected row again every few seconds, and each query of a browser host costs an offer at the web master
#define QUERYSIGNALPERIOD 30.0

struct FOutstandingPingInfo
{
	INT ServerID;
	BYTE PingCause;
	BYTE PingType;
	FIpAddr QueryAddr;
	DOUBLE PingSendTime;
	FServerResponseLine CurrentState;
	UBOOL GotReply;
	INT PingCount;
	// The peer of a query over WebRTC, and until when it waits for its channel before the command can go out
	INT WebRtcId;
	DOUBLE OpenDeadline;

	~FOutstandingPingInfo()
	{
		if( WebRtcId )
			WebRtcQueryClose( WebRtcId );
	}
};

class FServerQueryLink : public FUdpLink
{
	AServerQueryClient* Actor;
	TArray<FOutstandingPingInfo> OutstandingPings;
	TMap<FString,DOUBLE> SignalQueryTimes;
	struct FUnsentPing
	{
		INT ServerID;
		BYTE PingCause;
	};
	TArray<FUnsentPing> UnsentPings;

	void SendCommand( INT i )
	{
		BYTE Command = OutstandingPings(i).PingType;
		FArchiveUdpWriter ArSend( this, OutstandingPings(i).QueryAddr );
		ArSend << Command;
		ArSend.Flush();

		// HACK: also ping the server when we request rules.
		if( Command == 3 )
		{
			Command = 0;
			ArSend << Command;
			ArSend.Flush();
		}
	}
public:
	FServerQueryLink( AServerQueryClient* InActor, INT InPort )
	:	FUdpLink()
	,	Actor(InActor)
	{
		guard(FServerQueryLink::FServerQueryLink);
		BindPort(InPort);
		unguard;
	}
	void Poll()
	{
		while( UnsentPings.Num() )
		{
			FUnsentPing Ping = UnsentPings(0);
			UnsentPings.Remove(0);
			Actor->delegateOnPingTimeout( Ping.ServerID, Ping.PingCause );
		}

		BYTE Buffer[4096];
		for( INT i=0;i<OutstandingPings.Num();i++ )
		{
			INT Id = OutstandingPings(i).WebRtcId;
			if( !Id )
				continue;
			if( OutstandingPings(i).OpenDeadline )
			{
				// The ping is timed from the send, the channel's handshake is not part of it
				OutstandingPings(i).PingSendTime = appSeconds();
				if( OutstandingPings(i).PingSendTime > OutstandingPings(i).OpenDeadline )
					Fail( i );
				else
					SendCommand( i );
			}
			// Script answers a reply with new pings or cancels them all, so the entry is checked again after each one
			INT Count;
			while( i<OutstandingPings.Num() && OutstandingPings(i).WebRtcId==Id && (Count = WebRtcQueryRecv( Id, Buffer, sizeof(Buffer) ))!=0 )
			{
				if( Count < 0 )
				{
					if( !OutstandingPings(i).GotReply )
						Fail( i );
					break;
				}
				OnReceivedData( OutstandingPings(i).QueryAddr, Buffer, Count );
			}
		}

		// GCurrentTime is sometimes slightly behind appSeconds() at this point, so using GCurrentTime here 
		// runs the risk of invalidating some (not many) server's ping results
		DOUBLE CurrentTime = appSeconds();

		// check for ping timeouts.
		for( INT i=0;i<OutstandingPings.Num();i++ )
		{
			if( CurrentTime > OutstandingPings(i).PingSendTime + PINGTIMEOUT )
			{
				if( !OutstandingPings(i).GotReply && OutstandingPings(i).QueryAddr.Addr != INADDR_BROADCAST )
				{
					if( OutstandingPings(i).PingCause==PC_AutoPing && OutstandingPings(i).PingCount<2 )
					{
						// re-ping
						OutstandingPings(i).PingCount++;
						OutstandingPings(i).PingSendTime = CurrentTime;
						BYTE Command = OutstandingPings(i).PingType;
                   		FArchiveUdpWriter ArSend( this, OutstandingPings(i).QueryAddr );
						ArSend << Command;
						ArSend.Flush();
					}
					else
					{
						Actor->delegateOnPingTimeout( OutstandingPings(i).ServerID, OutstandingPings(i).PingCause );
						if ( i < OutstandingPings.Num() )
							OutstandingPings.Remove(i);
						i--;
					}
				}
				else
				{
					OutstandingPings.Remove(i);
					i--;
				}
			}
		}

		FUdpLink::Poll();
	}
	// Lets the timeout check below report the ping without another try
	void Fail( INT i )
	{
		OutstandingPings(i).PingSendTime = 0;
		OutstandingPings(i).PingCount = 2;
		OutstandingPings(i).OpenDeadline = 0;
	}
	INT SendTo( FIpAddr DstAddr, BYTE* Data, INT Count )
	{
		for( INT i=0;i<OutstandingPings.Num();i++ )
		{
			FOutstandingPingInfo& Ping = OutstandingPings(i);
			if( Ping.WebRtcId && Ping.QueryAddr==DstAddr )
			{
				// The server knows a query by its single byte, so the archive's version header stays behind
				if( !WebRtcQuerySend( Ping.WebRtcId, Data+Count-1, 1 ) )
					return 0;
				Ping.OpenDeadline = 0;
				return 1;
			}
		}
		return FUdpLink::SendTo( DstAddr, Data, Count );
	}
	void PingServer( INT ServerID, BYTE PingCause, FIpAddr Addr, BYTE Command, FServerResponseLine& CurrentState )
	{
		guard(FServerQueryLink::PingServer);

		// UDP reaches neither a web only server nor anything from a browser, those are asked over WebRTC
		FString WebRtc;
		if( HasServerDetail( CurrentState.ServerInfo, TEXT("WebQuery") ) && (NetworkError() || HasServerDetail( CurrentState.ServerInfo, TEXT("WebOnly") )) )
			WebRtc = GetServerDetail( CurrentState.ServerInfo, TEXT("WebRTC") );
		UBOOL Signal = !appStrnicmp( *WebRtc, TEXT("signal:"), 7 );
		if( Signal && PingCause==PC_Clicked )
		{
			DOUBLE* Last = SignalQueryTimes.Find( WebRtc );
			if( Last && appSeconds()-*Last < QUERYSIGNALPERIOD )
				return;
			SignalQueryTimes.Set( *WebRtc, appSeconds() );
		}

		INT WebRtcId = 0;
		if( WebRtc.Len() )
		{
			if( !Signal || PingCause==PC_Clicked )
				WebRtcId = WebRtcQueryOpen( Addr.GetString(0), WebRtc );
			if( !WebRtcId )
			{
				// Reported from Poll, because script pings the next row from the timeout and would nest once per failing row.
				// It takes no entry either, browser hosts often share an address and would take over each other's.
				INT f = UnsentPings.Add();
				UnsentPings(f).ServerID = ServerID;
				UnsentPings(f).PingCause = PingCause;
				return;
			}
		}

		// find existing ping.
		INT i;
		for( i=0;i<OutstandingPings.Num();i++ )
			if( Addr == OutstandingPings(i).QueryAddr )
				break;
		if( i == OutstandingPings.Num() )
			i = OutstandingPings.AddZeroed();

		// Same issue with GCurrentTime here....GCurrentTime is sometimes slightly behind what appSeconds() would return,
		// so using GCurrentTime runs the risk of invalidating some ping results
		OutstandingPings(i).PingCount = 0;
		OutstandingPings(i).PingSendTime = appSeconds();
		OutstandingPings(i).QueryAddr = Addr;
		OutstandingPings(i).CurrentState = CurrentState;
		OutstandingPings(i).ServerID = ServerID;
		OutstandingPings(i).PingCause = PingCause;
		OutstandingPings(i).PingType  = Command;
		OutstandingPings(i).GotReply = 0;

		// A reused entry gets a new peer, the server stops answering one after a few queries
		if( OutstandingPings(i).WebRtcId )
			WebRtcQueryClose( OutstandingPings(i).WebRtcId );
		OutstandingPings(i).WebRtcId = WebRtcId;
		OutstandingPings(i).OpenDeadline = WebRtcId ? appSeconds() + (Signal ? QUERYSIGNALTIMEOUT : QUERYOPENTIMEOUT) : 0;

		SendCommand( i );
		unguard;
	}
	void CancelPings()
	{
		guard(FServerQueryLink::CancelPings);
		OutstandingPings.Empty();
		UnsentPings.Empty();
		// A cancelled query of a browser host got no answer, so the next click may ask again
		SignalQueryTimes.Empty();
		unguard;
	}
	void OnReceivedData( FIpAddr SrcAddr, BYTE* Data, INT Count )
	{
		guard(FServerQueryLink::OnReceivedData);
		FArchiveUdpReader ArRecv( Data, Count );

		// find server
		INT i;
		INT bc = -1;
		for( i=0;i<OutstandingPings.Num();i++ )
		{
			if( SrcAddr == OutstandingPings(i).QueryAddr )
				break;
			if( OutstandingPings(i).QueryAddr.Addr == INADDR_BROADCAST )
				bc = i;
		}

		// Same issue with GCurrentTime here....GCurrentTime is sometimes slightly behind what appSeconds() would return,
		// but here, using GCurrentTime can result in server's appearing in the list with zero ping
		FServerResponseLine* CurrentState=NULL;
		INT Ping;
		if( i < OutstandingPings.Num() )
		{
			Ping = appRound( (appSeconds() - OutstandingPings(i).PingSendTime) * 1000 );
			CurrentState = &OutstandingPings(i).CurrentState;
			OutstandingPings(i).GotReply = 1;
		}
		else
		if( bc >= 0 )
		{
			Ping = appRound( (appSeconds() - OutstandingPings(bc).PingSendTime) * 1000 );
		}
		else
			Ping = 9999;

		BYTE Command;
		ArRecv << Command;

		if( ArRecv.IsError() )
		{
			GWarn->Logf(TEXT("Received bad UDP ping data from %s (%i bytes)"), *SrcAddr.GetString(1), Count);
		}
		else
		{
			switch( Command )
			{
			case QI_Ping:
				{
					FServerResponseLine NewState;
					ArRecv << NewState;
						NewState.Ping = Ping;
					NewState.IP = SrcAddr.GetString(0);
					NewState.QueryPort = SrcAddr.Port;

					if( CurrentState )
					{
						// HACK don't update the data if you clicked on the server while we're pinging!
						if( OutstandingPings(i).PingType!=QI_Ping )
						{
							if( OutstandingPings.Num() > 5 && CurrentState->Ping < 9999 )
							{
								NewState.CurrentPlayers = CurrentState->CurrentPlayers;
								NewState.MaxPlayers = CurrentState->MaxPlayers;
								NewState.MapName = CurrentState->MapName;
	                            NewState.Ping = CurrentState->Ping;
							}
							else
							if( CurrentState->Ping < 9999 )
	                            NewState.Ping = CurrentState->Ping;
						}

						// HACK keep master server's map capitalization
						if( NewState.MapName == CurrentState->MapName )
							NewState.MapName = CurrentState->MapName;

						NewState.Flags      = CurrentState->Flags;
						NewState.SkillLevel = CurrentState->SkillLevel;
						NewState.PlayerInfo = CurrentState->PlayerInfo;
						// Servers put only their WebRTC details in the ping, so a restart replaces or drops the old ones
						FString WebRtc = GetServerDetail( NewState.ServerInfo, TEXT("WebRTC") );
						NewState.ServerInfo = CurrentState->ServerInfo;
						SetServerDetail( NewState.ServerInfo, TEXT("WebRTC"), WebRtc );
						// The rules and players replies report this copy, which would bring back a ping that timed out earlier
						CurrentState->Ping = NewState.Ping;
						Actor->delegateOnReceivedPingInfo(OutstandingPings(i).ServerID, OutstandingPings(i).PingCause, NewState );
					}
					else
						Actor->delegateOnReceivedPingInfo( -1, PC_Unknown, NewState );
				}
				break;
			case QI_Rules:
				if( CurrentState )
				{
					TArray<FKeyValuePair> NewInfo;
					while( !ArRecv.AtEnd() && !ArRecv.IsError() )
					{
						INT i = NewInfo.AddZeroed();
						ArRecv << NewInfo(i);
					}

					UBOOL HadIP = 0;
					for(INT ci=0;ci<CurrentState->ServerInfo.Num();ci++ )
					{
						for( INT ni=0;ni<NewInfo.Num();ni++ )
						{
							if( CurrentState->ServerInfo(ci).Key==NewInfo(ni).Key && CurrentState->ServerInfo(ci).Value==NewInfo(ni).Value )
							{
								NewInfo.Remove(ni);
								ni--;
							}
						}

						if( CurrentState->ServerInfo(ci).Key == TEXT("IP") )
							HadIP = 1;
					}

					for( INT ni=0;ni<NewInfo.Num();ni++ )
					{
						INT ci=CurrentState->ServerInfo.AddZeroed();
						CurrentState->ServerInfo(ci).Key = NewInfo(ni).Key;
						CurrentState->ServerInfo(ci).Value = NewInfo(ni).Value;
					}

					// Add IP:Port to rules
					if( !HadIP )
					{
						INT ci=CurrentState->ServerInfo.AddZeroed();
						CurrentState->ServerInfo(ci).Key = TEXT("IP");
						CurrentState->ServerInfo(ci).Value = FString::Printf(TEXT("%s:%d"), *CurrentState->IP, CurrentState->Port);
					}

					Actor->delegateOnReceivedPingInfo(OutstandingPings(i).ServerID, OutstandingPings(i).PingCause, *CurrentState );
				}
				else
					GWarn->Logf(TEXT("Couldn't find outstanding request for rules from %s"), *SrcAddr.GetString(1) );
				break;
			case QI_Players:
				if( CurrentState )
				{
					TArray<FPlayerResponseLine> NewInfo;
					while( !ArRecv.AtEnd() && !ArRecv.IsError() )
					{
						INT i = NewInfo.AddZeroed();
						ArRecv << NewInfo(i);
					}

					for( INT np=0;np<NewInfo.Num();np++ )
					{
						for(INT cp=0;cp<CurrentState->PlayerInfo.Num();cp++ )
						{
							if( CurrentState->PlayerInfo(cp).PlayerNum==NewInfo(np).PlayerNum )
							{
								CurrentState->PlayerInfo.Remove(cp);
								cp--;
							}
						}
					}

					for( INT np=0;np<NewInfo.Num();np++ )
					{
						INT cp=CurrentState->PlayerInfo.AddZeroed();
						CurrentState->PlayerInfo(cp) = NewInfo(np);
					}

					Actor->delegateOnReceivedPingInfo(OutstandingPings(i).ServerID, OutstandingPings(i).PingCause, *CurrentState );
				}
				else
					GWarn->Logf(TEXT("Couldn't find outstanding request for players from %s"), *SrcAddr.GetString(1) );
				break;
			}
		}
		unguard;
	}
};

/*-----------------------------------------------------------------------------
	AServerQueryClient
-----------------------------------------------------------------------------*/

void AServerQueryClient::Init()
{
	if( !LinkPtr )
    {
        if ((!bLANQuery) || (LANPort >= 0))
		    LinkPtr = (PTRINT)(new FServerQueryLink(this, bLANQuery ? LANPort : 0));
    }
}

void AServerQueryClient::execPingServer( FFrame& Stack, RESULT_DECL )
{
	P_GET_INT(ServerID);
	P_GET_BYTE(PingCause)
	P_GET_STR(IP);
	P_GET_INT(Port)
	P_GET_BYTE(Command);
	P_GET_STRUCT(FServerResponseLine,CurrentState);
	P_FINISH;

	Init();

	FIpAddr Addr( *IP, Port );
	if( IP == TEXT("BROADCAST") )
		Addr.Addr = INADDR_BROADCAST;

	((FServerQueryLink*)LinkPtr)->PingServer( ServerID, PingCause, Addr, Command, CurrentState );
}

void AServerQueryClient::execNetworkError( FFrame& Stack, RESULT_DECL )
{
	P_FINISH;
	
	Init();
	*(DWORD*)Result = ((FServerQueryLink*)LinkPtr)->NetworkError() ? 1 : 0;
}

void AServerQueryClient::execCancelPings( FFrame& Stack, RESULT_DECL )
{
	P_FINISH;
	Init();
	((FServerQueryLink*)LinkPtr)->CancelPings();
}

UBOOL AServerQueryClient::Poll( INT WaitTime )
{
	guard(AServerQueryClient::Poll);

	if( LinkPtr )
		((FServerQueryLink*)LinkPtr)->Poll();
	return 0;
	unguard;
}

void AServerQueryClient::Destroy()
{
	if( LinkPtr )
		delete ((FServerQueryLink*)LinkPtr);
	LinkPtr = 0;
	Super::Destroy();
}

void AServerQueryClient::PostScriptDestroyed()
{
	Super::PostScriptDestroyed();
	if( LinkPtr )
		delete ((FServerQueryLink*)LinkPtr);
	LinkPtr = 0;
}

IMPLEMENT_CLASS(AServerQueryClient);

/*-----------------------------------------------------------------------------
	FMasterServerClientLink
-----------------------------------------------------------------------------*/

#define DOWNLOADLISTFILE TEXT("AutoPatch.txt")

enum EMasterServerClientState
{
	MSCS_WaitingChallenge		= 0,
	MSCS_WaitingApproval		= 1,
	MSCS_SentQuery				= 2,
	MSCS_WaitQueryData			= 3,
	MSCS_WaitingReview			= 4,
};

class FMasterServerClientLink : public FTcpLink
{
	EMasterServerClientState ClientState;
	AMasterServerClient* Actor;
	INT ResultCount, ReceiveCount;
	BYTE ResultsCompressed;
	BYTE QueryType;

	FString						MasterServerName;
	INT							MasterServerPort;
	struct FWebMasterRequest*	WebList;

	// A server list query asks every master at once through one child link each. A child keeps its servers
	// and its result for the owner, which hands them to script from its own Poll, never from a socket callback.
	UBOOL								IsChild;
	TArray<FMasterServerClientLink*>	Children;
	DOUBLE								ChildrenStarted;
	TArray<FServerResponseLine>			Pending;
	INT									FinishCode;	// -1 while running
	TMap<FString,UBOOL>					Listed;		// IP:port of every server script got, so each shows once
public:
	FMasterServerClientLink( AMasterServerClient* InActor, UBOOL InIsChild=0 )
	:	FTcpLink()
	,	ClientState(MSCS_WaitingChallenge)
	,	Actor(InActor)
	,	ResultCount(-1)
	,	ReceiveCount(0)
	,	WebList(NULL)
	,	IsChild(InIsChild)
	,	FinishCode(-1)
	{
		SetLinkMode( TCPLINK_FArchive );
	}

	virtual ~FMasterServerClientLink()
	{
		if( WebList )
			WebMasterRequestFree( WebList );
		DeleteChildren();
	}

	void DeleteChildren()
	{
		for( INT i=0; i<Children.Num(); i++ )
			delete Children(i);
		Children.Empty();
	}

	void Finish( BYTE Code, INT Info )
	{
		if( !IsChild )
			Actor->delegateOnQueryFinished( Code, Info );
		else if( FinishCode < 0 )
			FinishCode = Code;
	}

	// Hands script the servers it has not seen yet. Script callbacks may stop the query and delete this link,
	// so it returns whether the link still runs this query, and the caller only touches members when it does.
	UBOOL Deliver( TArray<FServerResponseLine>& Servers )
	{
		TArray<FServerResponseLine> New;
		for( INT i=0; i<Servers.Num(); i++ )
		{
			FString Key = FString::Printf( TEXT("%s:%i"), *Servers(i).IP, Servers(i).Port );
			if( !Listed.Find( *Key ) )
			{
				Listed.Set( *Key, 1 );
				new(New) FServerResponseLine( Servers(i) );
			}
		}
		AMasterServerClient* Client = Actor;
		PTRINT Link = Client->MSLinkPtr;
		for( INT i=0; i<New.Num() && Client->MSLinkPtr==Link; i++ )
		{
			Client->ResultCount++;
			Client->delegateOnReceivedServer( New(i) );
		}
		return Client->MSLinkPtr==Link;
	}

	void StartChildren()
	{
		for( INT i=0; i<Actor->MasterServerList.Num(); i++ )
		{
			FMasterServerClientLink* Child = new FMasterServerClientLink( Actor, 1 );
			Child->QueryType = CTM_Query;
			Child->MasterServerName = Actor->MasterServerList(i).Address;
			Child->MasterServerPort = Actor->MasterServerList(i).Port;
			Children.AddItem( Child );
			Child->Resolve( *Child->MasterServerName );
		}
		ChildrenStarted = GCurrentTime;
		if( !Children.Num() )
			Actor->delegateOnQueryFinished( RI_ConnectionFailed, 0 );
	}

	UBOOL PollChildren( INT WaitTime )
	{
		UBOOL Result = 0;
		UBOOL Done = 1, Ok = 0;
		// A master's own answer, such as a ban, tells the player more than a failed connection
		BYTE Failure = RI_ConnectionFailed;
		TArray<FServerResponseLine> Servers;
		for( INT i=0; i<Children.Num(); i++ )
		{
			FMasterServerClientLink* Child = Children(i);
			Result |= Child->Poll( WaitTime );
			// A master that accepts the connection and then goes quiet would hold up the whole list
			if( GCurrentTime - ChildrenStarted > 20.0 )
				Child->Finish( RI_ConnectionTimeout, 0 );
			for( INT j=0; j<Child->Pending.Num(); j++ )
				new(Servers) FServerResponseLine( Child->Pending(j) );
			Child->Pending.Empty();
			if( Child->FinishCode < 0 )
				Done = 0;
			else if( Child->FinishCode == RI_Success )
				Ok = 1;
			else if( Failure == RI_ConnectionFailed )
				Failure = Child->FinishCode;
		}
		if( Done )
			DeleteChildren();

		AMasterServerClient* Client = Actor;
		if( Deliver( Servers ) && Done )
			Client->delegateOnQueryFinished( Ok ? RI_Success : Failure, Ok ? CTM_Query : 0 );
		return Result;
	}

	void StartQuery( BYTE InQueryType )
	{
		QueryType = InQueryType;
		// A WebRTC only client lists the web master, which holds only servers it can join.
		// Others list the main masters after it, web-only servers are on the web master alone.
		if( WebRtcOnly() || QueryType==CTM_Query )
		{
			if( !WebList )
				WebList = WebMasterList();
			return;
		}
		StartMasterQuery();
	}

	void StartMasterQuery()
	{
		Actor->eventGetMasterServer( MasterServerName, MasterServerPort );
		Resolve( *MasterServerName );
	}

	virtual UBOOL Poll( INT WaitTime )
	{
		UBOOL Result = FTcpLink::Poll( WaitTime );
		if( Children.Num() )
			return PollChildren( WaitTime ) | Result;
		UBOOL Ok;
		TArray<FServerResponseLine> Servers;
		FString News;
		if( !WebList || !WebMasterListDone( WebList, Ok, Servers, News ) )
			return Result;
		WebMasterRequestFree( WebList );
		WebList = NULL;

		// Script callbacks may stop the query and delete this link, so only locals are used from here.
		AMasterServerClient* Client = Actor;
		BYTE Type = QueryType;
		UBOOL WebOnly = WebRtcOnly();
		if( !Ok && WebOnly )
		{
			Client->delegateOnQueryFinished( RI_ConnectionFailed, 0 );
			return Result;
		}
		if( Type == CTM_Query )
		{
			FString GameType;
			for( INT i=0; i<Client->Query.Num(); i++ )
				if( Client->Query(i).Key == TEXT("gametype") && Client->Query(i).QueryType == QT_Equals )
					GameType = Client->Query(i).Value;
			TArray<FServerResponseLine> Listable;
			for( INT i=0; i<Servers.Num(); i++ )
				if( (!GameType.Len() || Servers(i).GameType == GameType) && (WebOnly || HasServerDetail( Servers(i).ServerInfo, TEXT("WebOnly") )) )
					new(Listable) FServerResponseLine( Servers(i) );
			UBOOL Running = Deliver( Listable );
			if( !WebOnly )
			{
				// A new link at the same address is still waiting for its own web list
				if( Running && !WebList )
					StartChildren();
				return Result;
			}
		}
		else if( Type == CTM_GetMOTD )
			Client->delegateOnReceivedMOTDData( MR_MOTD, News );
		Client->delegateOnQueryFinished( RI_Success, Type );
		return Result;
	}

	void OnResolved( FIpAddr a )
	{
		a.Port = MasterServerPort;
		GWarn->Logf(TEXT("Resolved %s -> %s"), *MasterServerName, *a.GetString(0) );
		Connect( a );
     }

	void OnConnectionSucceeded()
	{
		GWarn->Logf(TEXT("Connection to %s established."), *MasterServerName);
	}

	void OnConnectionFailed()
	{

		if (!Actor)
			return;
		if( IsChild )
		{
			GWarn->Logf(TEXT("Master server %s connection failed"), *MasterServerName);
			Finish(RI_ConnectionFailed,0);
			return;
		}

		// Kind of hacky to put this here, but it saves having to setup a communication system
		// for the restart.

		GWarn->Logf(TEXT("Master Server Connection Failed!"));

		if (Actor->ActiveMasterServerList.Num()>0 && Actor->LastMSIndex < Actor->ActiveMasterServerList.Num() )
			Actor->ActiveMasterServerList.Remove(Actor->LastMSIndex,1);

		if (Actor->ActiveMasterServerList.Num()== 0)
			Finish(RI_ConnectionFailed,0);
		else
			StartMasterQuery();
	}

	void OnResolveFailed()
	{
		GWarn->Logf(TEXT("Failed to resolve '%s'"), *MasterServerName);
		Finish(RI_ConnectionFailed,0);
	}

	void OnClosed()
	{
		GWarn->Logf(TEXT("Connection closed!! %i %i"), ResultCount, ReceiveCount);
		if( ResultCount < 0 || ReceiveCount != ResultCount )
			Finish(RI_ConnectionTimeout,0);
	}

	void SaveDownloadSites( TArray<FString>& DownloadSites )
	{
		FString s;
		for( INT i=0;i<DownloadSites.Num();i++ )
			s = s + DownloadSites(i) + TEXT("\n");

		// Save AutoPatch.txt
		GFileManager->Delete( DOWNLOADLISTFILE, 0, 1 );
		appSaveStringToFile( s, DOWNLOADLISTFILE );
	}

	void OnDataReceived()
	{
		while( DataAvailable() )
		{
			switch( ClientState )
			{
			case MSCS_WaitingChallenge:
				{
					FString Challenge;
					*ArRecv << Challenge;
					FString CDKeyHash, Response, ClientType;
					CDKeyHash = GetCDKeyHash();
					Response = GetCDKeyResponse( *Challenge ) ;
					ClientType = TEXT("UT2K4CLIENT");
					INT Version = ENGINE_VERSION;
					BYTE Platform = GRunningOS;
					FString Language = UObject::GetLanguage();
					*ArSend << CDKeyHash << Response << ClientType << Version << Platform << Language;
					
					// The protocol still carries CPU speed and type, the engine no longer detects them.
					INT		CPUSpeed	= 0,
							GPUDeviceID	= GGPUDeviceID,
							GPUVendorID	= GGPUVendorID;
					BYTE	CPUType		= 255;
					*ArSend << GPUDeviceID << GPUVendorID << CPUSpeed << CPUType;

					ArSend->Flush();
					ClientState = MSCS_WaitingReview;
				}
				break;
			case MSCS_WaitingReview:
				{
					FString Results;
					*ArRecv << Results;

					if ( Results != TEXT("APPROVED") )
					{
							if ( Results == TEXT("UNKNOWN_CDKEY") )	
							{
								Finish(RI_AuthenticationFailed,0);
								Close();
							}
							else if( Results == TEXT("MODIFIED_CLIENT") )
							{
								Finish(RI_BadClient,0);
								Close();
							}
							else if( Results == TEXT("DEV_CLIENT") )
							{
								Finish(RI_DevClient,0);
								Close();
							}
							else if( Results == TEXT("UTAN_BAN") )
							{
								Finish(RI_UTANBan,0);
								Close();
							}
							else if( Results == TEXT("BANNED_CLIENT" ) )
							{
								*ArRecv << Actor->OptionalResult;
								Finish(RI_BannedClient,0);
								Close();
							}
							else if( Results == TEXT("NEED_UPGRADE") )
							{
								INT UpgradeVersion;
								TArray<FString> DownloadSites;
								FString MOTD;
								*ArRecv << UpgradeVersion << DownloadSites << MOTD;
								SaveDownloadSites(DownloadSites);

								AMasterServerClient* MSClient = Actor;

								// Delegates give unrealscript access to protected members - might invalidate Actor pointer
								if (MSClient)
								{
									MSClient->delegateOnReceivedMOTDData( MR_MandatoryUpgrade, FString::Printf(TEXT("%d"), UpgradeVersion) );
									MSClient->delegateOnReceivedMOTDData( MR_MOTD, MOTD );
									Finish(RI_MustUpgrade, UpgradeVersion);
									Close();
								}
							}
							else if( Results == TEXT("MSUPDATE") )
							{
								AddMasterServers( Actor, *ArRecv );

								// A merged query lists the new masters on the next refresh
								if( IsChild )
								{
									Finish(RI_ConnectionFailed,0);
									Close();
									return;
								}
								ClientState= MSCS_WaitingChallenge;
								StartMasterQuery();
								return;
							}


						else
						{
							// master server busy, or i didn't understand its response
							Finish(RI_ConnectionFailed,0);
							Close();
						}
					}
					else
					{

						*ArRecv << Actor->ModRevLevel;

						FString GlobalMD5;
						for (INT i=0; i<16; i++)
							GlobalMD5 += FString::Printf(TEXT("%02x"), GMD5[i]);	

						*ArSend << GlobalMD5;
						ArSend->Flush();

						ClientState = MSCS_WaitingApproval;
					}
				}
				break;
			case MSCS_WaitingApproval:
				{
					FString Results;
					*ArRecv << Results;
					if( Results == TEXT("VERIFIED") )
					{
						*ArSend << QueryType;
						switch( QueryType )
						{
						case CTM_Query:
							*ArSend << Actor->Query;
							break;
						case CTM_GetMOTD:
							break;
						case CTM_GetModMOTD:
							*ArSend << Actor->OwnageLevel;
							break;
						}
						ArSend->Flush();
						ClientState = MSCS_SentQuery;
					}
					else
					{
						Finish(RI_BadClient,0);
						Close();
					}
				}
				break;
			case MSCS_SentQuery:
				{
					switch( QueryType )
					{
					case CTM_Query:
						{
							*ArRecv << ResultCount << ResultsCompressed;
							if( ResultCount > 0 )
								ClientState = MSCS_WaitQueryData;
							else
							{
								Finish(RI_Success,QueryType);
								Close();
							}
						}
						break;
					case CTM_GetMOTD:
						{
							INT UpgradeVersion = 0;
							FString MOTD;
							*ArRecv << MOTD << UpgradeVersion;
							Actor->delegateOnReceivedMOTDData( MR_MOTD, MOTD );
							if( UpgradeVersion )
							{
								TArray<FString> DownloadSites;
								*ArRecv << DownloadSites;
								SaveDownloadSites(DownloadSites);
								Actor->delegateOnReceivedMOTDData( MR_OptionalUpgrade, FString::Printf(TEXT("%d"), UpgradeVersion) );
							}
							Finish(RI_Success,QueryType);
							Close();
						}
						break;
					case CTM_GetModMOTD:
						{	
							BYTE PacketType;
							*ArRecv << PacketType;

							if ( PacketType == 0 )		// Mod MOTD
							{
								FString ModMOTD;
								*ArRecv << ModMOTD;
								Actor->delegateOnReceivedModMOTDData(ModMOTD);
							}

							else if (PacketType == 1)	// Ownage Entry
							{

								INT		RevisionLevel, OwnagePacketType;
								*ArRecv << OwnagePacketType << RevisionLevel;

								switch (OwnagePacketType)
								{
								case 0:			//	Header
									{
										FString MapName;
										*ArRecv << MapName;
								
										GWarn->Logf(TEXT("Received Ownage Map Header: %i [%s]"),RevisionLevel,*MapName);
										Actor->delegateOnReceivedOwnageItem(RevisionLevel,MapName,TEXT(""),TEXT(""));
									}
									break;

								case 1:			// Description
									{
										FString MapDesc;
										*ArRecv << MapDesc;
										GWarn->Logf(TEXT("Received Ownage Map Body: %i [%s]"),RevisionLevel,*MapDesc);
										Actor->delegateOnReceivedOwnageItem(RevisionLevel,TEXT(""),MapDesc,TEXT(""));
									}
									break;

								case 2:			// URL
									{
										FString MapURL;
										*ArRecv << MapURL;
										GWarn->Logf(TEXT("Received Ownage Map URL: %i [%s]"),RevisionLevel,*MapURL);
										Actor->delegateOnReceivedOwnageItem(RevisionLevel,TEXT(""),TEXT(""),MapURL);
									}
									break;
								}
							}
							else	// End of Packet Sends
							{
								Finish(RI_Success,QueryType);
								Close();
							}
					}
					break;
				}
				break;
			case MSCS_WaitQueryData:
				{
					if( ReceiveCount < ResultCount )
					{
						FServerResponseLine Server;
						appMemzero( &Server, sizeof(FServerResponseLine) );
						ReceiveCount++;
						if( ResultsCompressed )
						{
                            // decode compressed results.
							DWORD IP;
							_WORD Port;
							_WORD QueryPort;
							BYTE CurrentPlayers;
							BYTE MaxPlayers;
							FString SkillLevel;
							INT ServerFlags=0;

							*ArRecv << IP << Port << QueryPort;
							if( ArRecv->IsError() )
							{
								Finish(RI_ConnectionFailed,0);
								Close();
								return;
							}


							*ArRecv << Server.ServerName;
							if( Server.ServerName.Len() == 25 )
								Server.ServerName = Server.ServerName + TEXT("....");

							if( ArRecv->IsError() )
							{
								Finish(RI_ConnectionFailed,0);
								Close();
								return;
							}
							*ArRecv << Server.MapName;

							if( ArRecv->IsError() )
							{
								Finish(RI_ConnectionFailed,0);
								Close();
								return;
							}
							*ArRecv << Server.GameType
									<< CurrentPlayers
									<< MaxPlayers
									<< ServerFlags
									<< SkillLevel;

							INT j = Server.MapName.InStr(TEXT("-"));
							UBOOL HasPrefix = (j==2 || j==3);
							if( Server.GameType == TEXT("0") )
							{
								Server.GameType = TEXT("xDeathMatch");
								if( !HasPrefix )
									Server.MapName = FString(TEXT("DM-")) + Server.MapName;
							}
							else
							if( Server.GameType == TEXT("1") )
							{
								Server.GameType = TEXT("xCTFGame");
								if( !HasPrefix )
									Server.MapName = FString(TEXT("CTF-")) + Server.MapName;
							}
							else
							if( Server.GameType == TEXT("2") )
							{
								Server.GameType = TEXT("xBombingRun");
								if( !HasPrefix )
									Server.MapName = FString(TEXT("BR-")) + Server.MapName;
							}
							else
							if( Server.GameType == TEXT("3") )
							{
								Server.GameType = TEXT("xTeamGame");
								if( !HasPrefix )
									Server.MapName = FString(TEXT("DM-")) + Server.MapName;
							}
							else
							if( Server.GameType == TEXT("4") )
							{
								Server.GameType = TEXT("xDoubleDom");
								if( !HasPrefix )
									Server.MapName = FString(TEXT("DOM-")) + Server.MapName;
							}

							FIpAddr Addr;
							Addr.Addr = ntohl(IP);
							Server.IP = Addr.GetString(0);
							Server.Port = Port;
							Server.QueryPort = QueryPort;
							Server.CurrentPlayers = CurrentPlayers;
							Server.MaxPlayers = MaxPlayers;
							Server.Flags = ServerFlags;
							Server.SkillLevel = SkillLevel;

						}
						else
							*ArRecv << Server;

						new(Pending) FServerResponseLine( Server );
					}

					if( ReceiveCount == ResultCount )
					{
						Finish(RI_Success,QueryType);
						Close();
					}
					break;
				}
			}
			}
		}
	}
};

/*-----------------------------------------------------------------------------
	AMasterServerLink
-----------------------------------------------------------------------------*/
void AMasterServerLink::execPoll( FFrame& Stack, RESULT_DECL )
{
	guard(AMasterServerLink::execPoll);
	P_GET_INT(WaitTime);
	P_FINISH;
	*(DWORD*)Result = Poll(WaitTime);
	unguard;
}

// Masters missing from the list are added and the others stay, so no master can take the rest off the list.
void AddMasterServers( AMasterServerLink* Link, FArchive& Ar )
{
	INT ListCnt, Port;
	FString Addr;
	Ar << ListCnt;
	GWarn->Logf(TEXT("Updating Master Server List %i"),ListCnt);
	for( INT i=0; i<ListCnt; i++ )
	{
		Ar << Addr << Port;
		UBOOL Found = 0;
		for( INT j=0; j<Link->MasterServerList.Num() && !Found; j++ )
			Found = Link->MasterServerList(j).Address == Addr && Link->MasterServerList(j).Port == Port;
		if( Found )
			continue;
		INT Index = Link->MasterServerList.AddZeroed();
		Link->MasterServerList(Index).Address = Addr;
		Link->MasterServerList(Index).Port = Port;
		GWarn->Logf(TEXT("    Added: %s [%i]"),*Addr,Port);
	}
	Link->ActiveMasterServerList.Empty();
	Link->LastMSIndex = -1;
	Link->SaveConfig();
}

IMPLEMENT_CLASS(AMasterServerLink);

/*-----------------------------------------------------------------------------
	AMasterServerClient
-----------------------------------------------------------------------------*/
void AMasterServerClient::Init()
{
	Super::Init();

	// Make a new link to the master server
	if( MSLinkPtr )
		delete ((FMasterServerClientLink*)MSLinkPtr);
    MSLinkPtr = (PTRINT)(new FMasterServerClientLink(this));
}

UBOOL AMasterServerClient::Poll( INT WaitTime )
{
	guard(AMasterServerClient::Poll);
	Super::Poll( WaitTime );
	if( MSLinkPtr )
		return ((FMasterServerClientLink*)MSLinkPtr)->Poll(WaitTime);
	return 0;
	unguard;
}

void AMasterServerClient::Destroy()
{
	if( MSLinkPtr )
		delete ((FMasterServerClientLink*)MSLinkPtr);
	MSLinkPtr = 0;
	Super::Destroy();
}

void AMasterServerClient::PostScriptDestroyed()
{
	Super::PostScriptDestroyed();
	if( MSLinkPtr )
		delete ((FMasterServerClientLink*)MSLinkPtr);
	MSLinkPtr = 0;
}

void AMasterServerClient::execStartQuery( FFrame& Stack, RESULT_DECL )
{
	P_GET_BYTE(QueryType)
	P_FINISH;
	Init();
    ResultCount = 0; // gam
	((FMasterServerClientLink*)MSLinkPtr)->StartQuery(QueryType);
}

void AMasterServerClient::execStop( FFrame& Stack, RESULT_DECL )
{
	P_FINISH;
	if( MSLinkPtr )
		delete ((FMasterServerClientLink*)MSLinkPtr);
	MSLinkPtr = 0;
}

void AMasterServerClient::execLaunchAutoUpdate( FFrame& Stack, RESULT_DECL )
{
	guard(AMasterServerClient::execLaunchAutoUpdate);
	P_FINISH;

	// Verify that we have a download location before closing this process
	if ( GFileManager->FileSize( DOWNLOADLISTFILE ) <= 0 )
	{
		debugf(NAME_Error, TEXT("Failed to launch auto-updater! No valid download locations could be found!"));
		return;
	}

#if (defined WIN32)
    appLaunchURL( TEXT("Setup.exe"), TEXT("autopatch") );
#elif (defined __linux__)
    GUnixSpawnOnExit = TEXT("../updater/update");
#endif

	appRequestExit(0);
	unguard;
}

IMPLEMENT_CLASS(AMasterServerClient);

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/


