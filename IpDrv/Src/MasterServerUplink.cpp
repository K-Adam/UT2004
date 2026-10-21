/*============================================================================
	MasterServerUplink.cpp - game server to master server uplink

	Revision history:
		* Created by Jack Porter
============================================================================*/

#include "UnIpDrv.h"
#include "UnTcpNetDriver.h"

// Advertises the WebRTC port and fingerprint as a server detail. Script rebuilds ServerInfo on every refresh, so each sender adds it again.
static UBOOL AddWebRtcDetails( AActor* Actor, TArray<FKeyValuePair>& ServerInfo )
{
	UTcpNetDriver* NetDriver = Cast<UTcpNetDriver>( Actor->XLevel->NetDriver );
	if( !NetDriver || !NetDriver->WebRtcDetails.Len() )
		return 0;
	SetServerDetail( ServerInfo, TEXT("WebRTC"), NetDriver->WebRtcDetails );
	// Older servers take a query on the WebRTC port for a joining player, so clients only send one to servers that say this
	SetServerDetail( ServerInfo, TEXT("WebQuery"), TEXT("1") );
	// Regular clients take such servers from the web master, the main masters can't list them
	if( WebRtcOnly() )
		SetServerDetail( ServerInfo, TEXT("WebOnly"), TEXT("1") );
	return 1;
}

/*-----------------------------------------------------------------------------
	FNATHeartbeatLink
-----------------------------------------------------------------------------*/

class FNATHeartbeatLink : public FUdpLink
{
	BYTE HeartbeatType;
public:
	// tors
	FNATHeartbeatLink(FSocketData InSocketData, BYTE InHeartbeatType)
	:	FUdpLink(InSocketData)
	,	HeartbeatType(InHeartbeatType)
	{}
	FNATHeartbeatLink(BYTE InHeartbeatType)
	:	HeartbeatType(InHeartbeatType)
	{}

	// FUdpLink interface.
	virtual void OnReceivedData( FIpAddr SrcAddr, BYTE* Data, INT Count )
	{}

	// FNATHeartbeatLink interface.
	FSocketData* GetSocketData()
	{
		return &SocketData;
	}
	virtual void SendHeartbeat( FIpAddr MasterServerAddr, INT Code )
	{
		FArchiveUdpWriter ArSend(this, MasterServerAddr);
		ArSend << HeartbeatType << Code;
		ArSend.Flush();
	}
};

/*-----------------------------------------------------------------------------
	FServerQueryInterface
-----------------------------------------------------------------------------*/

class FServerQueryInterface : public FNATHeartbeatLink
{
protected:
	class FMasterServerUplinkLink* Uplink;
public:
	FServerQueryInterface( INT InPort, class FMasterServerUplinkLink* InUplink )
	:	FNATHeartbeatLink(HB_QueryInterface)
	,	Uplink(InUplink)
	{
		// A web only server has no UDP port to answer on, its queries come in over WebRTC
		if( !WebRtcOnly() )
			BindPort(InPort);
	}

	virtual void OnReceivedData( FIpAddr SrcAddr, BYTE* Data, INT Count );
	void Answer( BYTE Command, FArchive& Ar );
	void SendPing( FArchive& Ar );
	void SendSmallPing( FArchive& Ar );
	void SendRules( FArchive& Ar );
	void SendPlayers( FArchive& Ar );
};

static FServerQueryInterface* GServerQuery = NULL;

void ServerQueryAnswer( BYTE Command, FArchive& ArSend )
{
	if( GServerQuery )
		GServerQuery->Answer( Command, ArSend );
}

/*-----------------------------------------------------------------------------
	FServerQueryInterface
-----------------------------------------------------------------------------*/

class FServerLANInterface : public FServerQueryInterface
{
	FServerQueryInterface* ReplyInterface;
public:
	FServerLANInterface( INT Port, class FMasterServerUplinkLink* InUplink, FServerQueryInterface* InReplyInterface );
	virtual void OnReceivedData( FIpAddr SrcAddr, BYTE* Data, INT Count );
};


/*-----------------------------------------------------------------------------
	FMasterServerUplinkLink
-----------------------------------------------------------------------------*/

#define REFRESH_TIME 60.f

enum EMastserServerUplinkState
{
	MSUS_WaitingChallenge		= 0,
	MSUS_WaitingApproval		= 1,
	MSUS_WaitingForUDPResponse  = 2,
	MSUS_ChannelOpen			= 3,
};

class FMasterConnection;

// Registers the server with every master in MasterServerList, through one FMasterConnection each.
class FMasterServerUplinkLink
{
	FServerQueryInterface*		QueryInterface;			// Native query interface / NAT heartbeat.
	FServerLANInterface*		LANInterface;			// LAN query interface
	FNATHeartbeatLink*			GameHeartbeat;			// NAT heartbeat for game protocol port
	FNATHeartbeatLink*			GSQueryHeartbeat;		// NAT heartbeat for gamespy query port.

	AMasterServerUplink*		Actor;
	TArray<FMasterConnection*>	Connections;
	DOUBLE						LastRefreshTime;

	friend class FServerQueryInterface;
	friend class FServerLANInterface;
	friend class FMasterConnection;
public:
	FMasterServerUplinkLink( AMasterServerUplink* InActor )
	:	QueryInterface(NULL)
	,	LANInterface(NULL)
	,	GameHeartbeat(NULL)
	,	GSQueryHeartbeat(NULL)
	,	Actor(InActor)
	{
		guard(FMasterServerUplinkLink::FMasterServerUplinkLink)

		LastRefreshTime = -REFRESH_TIME;

		UTcpNetDriver* NetDriver = Cast<UTcpNetDriver>( Actor->XLevel->NetDriver );
		check(NetDriver);
		GServerQuery = QueryInterface = new FServerQueryInterface( NetDriver->GetSocketData().Port+1, this );
		// Without a UDP game socket there is nothing to keep open, and the web master lists the server
		if( WebRtcOnly() )
			return;

		// Game protocol port NAT heartbeat.
		GameHeartbeat = new FNATHeartbeatLink( NetDriver->GetSocketData(), HB_GamePort );
		// Gamespy query port NAT heartbeat.
		if( Actor->GamespyQueryLink )
			GSQueryHeartbeat = new FNATHeartbeatLink( Actor->GamespyQueryLink->GetSocketData(), HB_GamespyQueryPort );

        if (Actor->LANServerPort >= 0)
		    LANInterface = new FServerLANInterface( Actor->LANServerPort, this, QueryInterface );

		unguard;
	}

	~FMasterServerUplinkLink() noexcept(false);
	void Connect();
	UBOOL Poll( INT WaitTime );
	void ForceStateRefresh( INT When );
	void CheckRefresh();
	UBOOL SendStatLine( const TCHAR* StatLine );
};

class FMasterConnection : public FTcpLink
{
	FMasterServerUplinkLink*	Owner;
	AMasterServerUplink*		Actor;
	EMastserServerUplinkState	UplinkState;
	TArray<FString>				OutstandingChallenges;	// CD Key challenge clients we're waiting for
	TArray<FString>				StatsBuffer;
	INT							HeartbeatPeriod;
	DOUBLE						LastHeartbeatTime;
	FIpAddr						RemoteHost;
	DWORD						QueryNatPort;
	DWORD						GameNatPort;
	DWORD						GamespyNatPort;

public:
	FString						MasterServerName;
	INT							MasterServerPort;
	UBOOL						Active;				// Connecting or connected, until a failure is reported
	UBOOL						ConnectionFailed;
	UBOOL						ShouldTryReconnect;

	FMasterConnection( FMasterServerUplinkLink* InOwner, const FString& InName, INT InPort )
	:	FTcpLink()
	,	Owner(InOwner)
	,	Actor(InOwner->Actor)
	,	UplinkState(MSUS_WaitingChallenge)
	,	HeartbeatPeriod(0)
	,	QueryNatPort(0)
	,	GameNatPort(0)
	,	GamespyNatPort(0)
	,	MasterServerName(InName)
	,	MasterServerPort(InPort)
	,	Active(0)
	,	ConnectionFailed(0)
	,	ShouldTryReconnect(1)
	{
		SetLinkMode( TCPLINK_FArchive );
		LastHeartbeatTime = GCurrentTime;
	}

	void TryConnect()
	{
		// restore state
		UplinkState = MSUS_WaitingChallenge;
		ConnectionFailed = 0;
		ShouldTryReconnect = 1;
		Active = 1;

		Resolve( *MasterServerName );
	}

	void OnResolved( FIpAddr a )
	{
		a.Port = MasterServerPort;
		RemoteHost = a;
		GWarn->Logf(TEXT("MasterServerUplink: Resolved %s as %s."), *MasterServerName, *RemoteHost.GetString(0) );
		Connect( a );
	}

	void OnResolveFailed()
	{
		GWarn->Logf(TEXT("MasterServerUplink: Failed to resolve master server %s."), *MasterServerName );
		ConnectionFailed = 1;
	}

	void OnConnectionSucceeded()
	{
		GWarn->Logf(TEXT("MasterServerUplink: Connection to %s established."), *MasterServerName);
	}

	void OnConnectionFailed()
	{
		GWarn->Logf(TEXT("MasterServerUplink: Uplink failed to connect to master server %s."), *MasterServerName);
		ConnectionFailed = 1;
	}

	void OnClosed()
	{
		ConnectionFailed = 1;
	}

	void OnChannelOpened()
	{
		// Send our master MD5 revision
		SendMD5Revision();

		// This master may connect long after the others, it gets the game state on the next poll
		Owner->ForceStateRefresh(0);

		// Send any pending stats.
		if( StatsBuffer.Num() )
		{
			for( INT i=0;i<StatsBuffer.Num();i++ )
				SendStatLine( *StatsBuffer(i) );
			StatsBuffer.Empty();
		}
	}

	void SendMD5Revision()
	{
		guard(FMasterServerUplink::SendMD5Revision);
		BYTE Command = STM_MD5Version;

		INT MaxRevision = -1;
		UGameEngine* GE = Cast<UGameEngine>(Actor->XLevel->Engine);

		if (GE)
			MaxRevision = GE->PackageRevisionLevel();

		// Send our current maximum revision number to the master server.
		*ArSend << Command << MaxRevision;
		ArSend->Flush();
		unguard;
	}

	void OnDataReceived()
	{
		guard(FMasterConnection::OnDataReceived);
		while( DataAvailable() )
		{
			switch( UplinkState )
			{
			case MSUS_WaitingChallenge:
				{
					guard(MSUS_WaitingChallenge);
					FString Challenge;
					FString CDKeyHash, Response, ClientType;
					INT Version = ENGINE_VERSION;
					INT MatchID = Actor->GameStats ? Actor->MatchID : -1;
					BYTE Platform = GRunningOS;
					FString Language = UObject::GetLanguage();

					*ArRecv << Challenge;

					CDKeyHash = GetCDKeyHash();
					Response = GetCDKeyResponse( *Challenge );
					ClientType = TEXT("UT2K4SERVER");

					BYTE IUTB = Actor->bIgnoreUTANBans;

					*ArSend << CDKeyHash << Response << ClientType << Version << Platform << Language << MatchID << IUTB;
					ArSend->Flush();

					UplinkState = MSUS_WaitingApproval;
					unguard;
				}
				break;
			case MSUS_WaitingApproval:
				{
					guard(MSUS_WaitingApproval);
					FString Approval;
					*ArRecv << Approval;

					GWarn->Logf(TEXT("Approval %s from %s"),*Approval,*MasterServerName);

					if( Approval == TEXT("APPROVED") )
					{
						// Server was approved.  Send our configuration and wait for
						// command to send UDP heartbeats.
						SendServerConfig();
						UplinkState = MSUS_WaitingForUDPResponse;
					}

					// Updating the master server List

					else if( Approval == TEXT("MSUPDATE") )
					{
						// The master sends the server elsewhere, so it is not asked again and the new entries connect instead
						AddMasterServers( Actor, *ArRecv );
						ShouldTryReconnect = 0;
						ConnectionFailed = 1;
					}
					else if( Approval == TEXT("UPGRADE") )
					{
						INT UpgradeVersion;
						*ArRecv << UpgradeVersion;
						GWarn->Logf(TEXT("MasterServerUplink: Rejected.  Must upgrade to version %d."), UpgradeVersion);
						ShouldTryReconnect = 0;
						ConnectionFailed = 1;
					}
					else if( Approval == TEXT("DENIED") )
					{
						GWarn->Logf(TEXT("MasterServerUplink: Master server rejected authentication request."));
						GWarn->Logf(TEXT("Check your CD key."));
						if( GetCDKeyHash() == TEXT("d41d8cd98f00b204e9800998ecf8427e") )
							GWarn->Logf(TEXT("Your CD key appears to be blank.  Double-check it's installed in the registry correctly."));
						else
							GWarn->Logf(TEXT("Sent CD key hash: \"%s\""), *GetCDKeyHash() );
						ShouldTryReconnect = 0;
						ConnectionFailed = 1;
					}
					else
					{
						// master server busy, or i didn't understand its response
						ShouldTryReconnect = 1;
						ConnectionFailed = 1;
					}
					unguard;
				}
				break;
			case MSUS_WaitingForUDPResponse:
				{
					guard(MSUS_WaitingForUDPResponse);
					BYTE Success;
					*ArRecv << Success;

					if( Success )
					{
						*ArRecv << HeartbeatPeriod;
						*ArRecv << QueryNatPort << GameNatPort << GamespyNatPort;
						UplinkState = MSUS_ChannelOpen;
						OnChannelOpened();
					}
					else
					{
						BYTE HeartbeatType;
						INT HeartbeatCode;
						*ArRecv << HeartbeatType << HeartbeatCode;
						GWarn->Logf(TEXT("Master server requests heartbeat %d with code %d"), HeartbeatType, HeartbeatCode);
						switch( HeartbeatType )
						{
						case HB_QueryInterface:
							Owner->QueryInterface->SendHeartbeat(RemoteHost, HeartbeatCode);
							break;
						case HB_GamePort:
							Owner->GameHeartbeat->SendHeartbeat(RemoteHost, HeartbeatCode);
							break;
						case HB_GamespyQueryPort:
							if( Owner->GSQueryHeartbeat )
								Owner->GSQueryHeartbeat->SendHeartbeat(RemoteHost, HeartbeatCode);
							break;
						}
					}
					unguard;
				}
				break;
			case MSUS_ChannelOpen:
				{
					guard(MSUS_ChannelOpen);
                    BYTE Command;
					*ArRecv << Command;
					switch( Command )
					{
					case MTS_ClientChallenge:
						{
							FString Client;
							FString ClientChallenge;
							*ArRecv << Client;
							*ArRecv << ClientChallenge;
							ChallengeClient( *Client, *ClientChallenge );
						}
						break;
					case MTS_ClientAuthFailed:
					case MTS_ClientDupKey:
						{
							FString Client;
							*ArRecv << Client;
							ClientAuthFailed( *Client, TEXT("RI_AuthenticationFailed"), TEXT("") );
						}
						break;
					case MTS_UTANBan:
						{
							FString Client;
							*ArRecv << Client;
							ClientAuthFailed( *Client, TEXT("RI_UTANBan"), TEXT("") );
						}
						break;
					case MTS_ClientMD5Fail:
						{
							FString Client;
							*ArRecv << Client;
							ClientAuthFailed(*Client, TEXT("RI_BadClient"),TEXT("") );
						}
					case MTS_ClientBanned:
						{
							FString Client, Banned;
							*ArRecv << Client << Banned;
							ClientAuthFailed(*Client, TEXT("RI_BannedClient"),*Banned );
						}
					case MTS_Shutdown:
						{
							Close();
							ConnectionFailed = 1;
						}
						break;
					case MTS_MatchID:
						{
							INT MatchID;
							*ArRecv << MatchID;
							GWarn->Logf(TEXT("Master server assigned our MatchID: %d"), MatchID);
							// A master without stats must not turn them off for one that assigned a match
							if( !MatchID && Actor->MatchID )
								break;
							// save in the GRI.
							Actor->MatchID = MatchID;
							if( Actor->Level->Game && Actor->Level->Game->GameReplicationInfo )
								Actor->Level->Game->GameReplicationInfo->MatchID = MatchID;

							// If we were denied a match ID, don't try to send stats.
							if( !MatchID && Actor->GameStats )
							{
								Actor->GameStats->Uplink = NULL;
								Actor->GameStats = NULL;
							}

							Owner->ForceStateRefresh(0);	// Force the initial update of the database.
						}
						break;
					case MTS_MD5Update:
						{
							TArray<FMD5UpdateData> UpdateData;
							*ArRecv << UpdateData;
							GWarn->Logf(TEXT("Received updated MD5 data from master server."));

							UGameEngine* GE = Cast<UGameEngine>(Actor->XLevel->Engine);
							if (GE)
							{
								for( INT i=0;i<UpdateData.Num();i++ )
								{
									GE->AddMD5(*UpdateData(i).Guid, *UpdateData(i).MD5, UpdateData(i).Revision);
								}

								GE->SaveMD5Database();
							}
						}
						break;
					case MTS_UpdateOption:
						{
							// A master is a third party, so it gets no write access to the server's ini
							FString Section, Key, Value;
							UBOOL Unique;
							*ArRecv << Section << Key << Value << Unique;
						}
						break;
					case MTS_CheckOption:
						{
							// The reply stays empty, since the ini holds the admin and game passwords
							FString Section, Key, Result;
							*ArRecv << Section << Key;
							BYTE Command = STM_CheckOptionReply;
							*ArSend << Command << Section << Key << Result;
							ArSend->Flush();
						}
						break;
					default:
						GWarn->Logf(TEXT("MSUS_ChannelOpen: received unknown command %d"), Command);
					}
					unguard;
				}
				break;
			}
		}
		unguard;
	}

	UBOOL IsOpen()
	{
		return UplinkState==MSUS_ChannelOpen && (LinkState==LINK_Connected || LinkState==LINK_ClosePending);
	}

	void CheckOutstandingChallenges()
	{
		guard(FMasterConnection::CheckOutstandingChallenges);
		if( OutstandingChallenges.Num() )
		{
			UNetDriver* NetDriver = Actor->XLevel->NetDriver;
			if( NetDriver )
			{
				for( INT conn=0;conn<NetDriver->ClientConnections.Num(); conn++ )
				{
						INT idx = OutstandingChallenges.FindItemIndex( NetDriver->ClientConnections(conn)->LowLevelGetRemoteAddress() );
						if( idx != INDEX_NONE )
						{
							// Send response to master server.

							UNetConnection* NetConn = NetDriver->ClientConnections(conn);

							BYTE Command = STM_ClientResponse;
							FString Client = OutstandingChallenges(idx);
							FString CDKeyHash = NetConn->CDKeyHash;
							FString Response = NetConn->CDKeyResponse;
							FString GlobalMD5;

							for (INT i=0; i<16; i++)
								GlobalMD5 += FString::Printf(TEXT("%02x"), NetConn->GMD5[i]);

							FString PlayerName=TEXT("");
							if (NetConn->Actor && NetConn->Actor->PlayerReplicationInfo)
								PlayerName = NetConn->Actor->PlayerReplicationInfo->PlayerName;

							*ArSend << Command << Client << CDKeyHash << Response << GlobalMD5 << PlayerName;
							ArSend->Flush();
							OutstandingChallenges.Remove(idx);
						}
				}
			}
		}
		unguard;
	}

	// Sends the clients and the game state that the owner's refresh just gathered.
	void SendGameState()
	{
		guard(FMasterConnection::SendGameState);
		if( !IsOpen() )
			return;

		TArray<FString> Clients;
		UNetDriver* NetDriver = Actor->XLevel->NetDriver;
		if( NetDriver )
			for( INT conn=0;conn<NetDriver->ClientConnections.Num(); conn++ )
				if( NetDriver->ClientConnections(conn)->Actor )
					Clients(Clients.AddZeroed()) = NetDriver->ClientConnections(conn)->LowLevelGetRemoteAddress();

		BYTE Command = STM_GameState;
		*ArSend << Command << Clients << Actor->ServerState;
		ArSend->Flush();
		unguard;
	}

	void CheckUDPHeartbeats()
	{
		guard(FMasterConnection::CheckUDPHeartbeats);

		if( !IsOpen() )
			return;

		if( Actor->ServerBehindNAT && HeartbeatPeriod && (GCurrentTime - LastHeartbeatTime > HeartbeatPeriod) )
		{
			LastHeartbeatTime = GCurrentTime;

			// send UDP heartbeats.
			Owner->QueryInterface->SendHeartbeat(RemoteHost, 0);
			Owner->GameHeartbeat->SendHeartbeat(RemoteHost, 0);
			if( Owner->GSQueryHeartbeat )
				Owner->GSQueryHeartbeat->SendHeartbeat(RemoteHost, 0);
		}

		unguard;
	}

	UBOOL SendStatLine( const TCHAR* StatLine )
	{
		guard(SendStatLine);
		if( IsOpen() )
		{
			BYTE Command = STM_Stats;
			FString StatLineStr = StatLine;
			*ArSend << Command << StatLineStr;
			ArSend->Flush();
		}
		// A master that never opens would otherwise keep every line for the life of the server
		else if( StatsBuffer.Num() < 1000 )
		{
			StatsBuffer(StatsBuffer.AddZeroed()) = StatLine;
		}

		return 1;
		unguard;
	}

	UNetConnection* FindClient( const TCHAR* ClientIP )
	{
		guard(FMasterConnection::FindClient);
		UNetDriver* NetDriver = Actor->XLevel->NetDriver;
        if( NetDriver )
		{
			for( INT i=0;i<NetDriver->ClientConnections.Num(); i++ )
				if( !appStrcmp( *NetDriver->ClientConnections(i)->LowLevelGetRemoteAddress(), ClientIP ) )
					return NetDriver->ClientConnections(i);
		}
		return NULL;
		unguard;
	}

	void ChallengeClient( const TCHAR* ClientIP, const TCHAR* Challenge )
	{
		guard(FMasterConnection::ChallengeClient);
		UNetConnection* Conn = FindClient(ClientIP);
		if( Conn )
		{
			Conn->CDKeyResponse = TEXT("");
			OutstandingChallenges(OutstandingChallenges.AddZeroed()) = ClientIP;
		}
		unguard;
	}

	void ClientAuthFailed( const TCHAR* ClientIP, const TCHAR* FailCode, const TCHAR* Why )
	{
		guard(FMasterConnection::ClientAuthFailed);

		OutstandingChallenges.RemoveItem(FString(ClientIP));

		UNetConnection* Conn = FindClient(ClientIP);
		if( Conn )
		{
				GWarn->Logf(TEXT("Client %s failed auth. Reason= [%s]\"%s\"... disconnecting"), ClientIP,FailCode, Why );
			if (Conn && Conn->Actor)
			{
				if ( !appStrcmp(FailCode,TEXT("RI_UTANBan")) && Conn->NegotiatedVer < 3352 )
					Conn->Actor->eventClientNetworkMessage(TEXT("RI_BannedClient"),TEXT("you contact abuse@epicgames.com!"));
				else
					Conn->Actor->eventClientNetworkMessage(FString(FailCode),FString(Why));
			}

			// Disconnect the player.
			delete Conn;
		}
		else
		{
			// Couldn't find the client to disconnect.
			GWarn->Logf(TEXT("Disconnect of client %s failed"), ClientIP );

			// Tell the master server about it.
			BYTE Command = STM_ClientDisconnectFailed;
			FString StrClient = ClientIP;
			*ArSend << Command << StrClient;
			ArSend->Flush();
		}
		unguard;
	}

	void SendServerConfig()
	{
		UBOOL NAT = Actor->ServerBehindNAT;
		UBOOL Gamespy = Owner->GSQueryHeartbeat != NULL;
		*ArSend << NAT << Gamespy;
		ArSend->Flush();
	}

// Some versions of GNU C++ seem to need a little nudging here.
#ifdef __GNUC__
    void *operator new(size_t x) { return ::operator new(x); }
#endif
};

FMasterServerUplinkLink::~FMasterServerUplinkLink() noexcept(false)
{
	guard(FMasterServerUplinkLink::~FMasterServerUplinkLink)
	for( INT i=0; i<Connections.Num(); i++ )
		delete Connections(i);
	delete LANInterface;
	if( GServerQuery==QueryInterface )
		GServerQuery = NULL;
	delete QueryInterface;
	delete GameHeartbeat;
	delete GSQueryHeartbeat;
	unguard;
}

// Script calls this at start and after a failure, so only masters without a live connection connect again.
void FMasterServerUplinkLink::Connect()
{
	guard(FMasterServerUplinkLink::Connect);
	for( INT i=0; i<Actor->MasterServerList.Num(); i++ )
	{
		const FtMasterServerEntry& Entry = Actor->MasterServerList(i);
		FMasterConnection* Connection = NULL;
		for( INT j=0; j<Connections.Num() && !Connection; j++ )
			if( Connections(j)->MasterServerName == Entry.Address && Connections(j)->MasterServerPort == Entry.Port )
				Connection = Connections(j);
		if( !Connection )
			Connection = Connections( Connections.AddItem( new FMasterConnection( this, Entry.Address, Entry.Port ) ) );
		// Connect closes the old socket once the name resolves
		if( !Connection->Active && Connection->ShouldTryReconnect )
			Connection->TryConnect();
	}
	unguard;
}

UBOOL FMasterServerUplinkLink::Poll( INT WaitTime )
{
	guard(FMasterServerUplinkLink::Poll);

	UBOOL Result = 0;
	for( INT i=0; i<Connections.Num(); i++ )
		Result |= Connections(i)->Poll( WaitTime );

	// answer queries
	if( LANInterface )
		LANInterface->Poll();
	if( QueryInterface )
		QueryInterface->Poll();

	UBOOL Failed = 0;
	for( INT i=0; i<Connections.Num(); i++ )
	{
		FMasterConnection* Connection = Connections(i);
		if( Connection->ConnectionFailed )
		{
			Failed = 1;
			Connection->ConnectionFailed = 0;
			Connection->Active = 0;
		}
		else if( Connection->Active )
		{
			// See if any CD key challenges have been answered.
			Connection->CheckOutstandingChallenges();

			// See if we need to send any heartbeats;
			Connection->CheckUDPHeartbeats();
		}
	}
	if( Failed )
	{
		// Script keeps a single retry timer, so it is asked to retry while any master still waits for one
		UBOOL Retry = Actor->MasterServerList.Num() > Connections.Num();
		for( INT i=0; i<Connections.Num(); i++ )
			Retry |= !Connections(i)->Active && Connections(i)->ShouldTryReconnect;
		Actor->eventConnectionFailed(Retry);
	}

	// See if we need to update the masterservers' copy of our server state.
	CheckRefresh();

	return Result;
	unguard;
}

void FMasterServerUplinkLink::ForceStateRefresh(INT When)
{
	guard(FMasterServerUplinkLink::ForceStateRefresh);

	if (GCurrentTime < LastRefreshTime + REFRESH_TIME - When)
		LastRefreshTime = GCurrentTime + When - REFRESH_TIME;

	unguard;
}

void FMasterServerUplinkLink::CheckRefresh()
{
	guard(FMasterServerUplinkLink::CheckRefresh);

	if( GCurrentTime - LastRefreshTime > REFRESH_TIME )
	{

		LastRefreshTime = GCurrentTime;
		Actor->eventRefresh();

		// The script port is the URL port, the query interface follows the socket's actual port.
		FServerResponseLine WebState = Actor->ServerState;
		WebState.Port = Cast<UTcpNetDriver>( Actor->XLevel->NetDriver )->GetSocketData().Port;
		if( Actor->DoUplink && AddWebRtcDetails( Actor, WebState.ServerInfo ) )
			WebMasterAdvertise( WebState );

		{
			// Insure the proper mutator count

			check(Actor);
			check(Actor->Level);
			check(Actor->GameGroup);

			guard(CheckingServerList);

			for (AMutator* Mut = Actor->Level->Game->BaseMutator; Mut; Mut = Mut->NextMutator)
			{
				FString MutName = FString::Printf(TEXT("%s"),Mut->GetName());
				UBOOL bFound=false;
				for (INT i=0;i<Actor->ServerState.ServerInfo.Num();i++)
					if ( Actor->ServerState.ServerInfo(i).Key.Caps() == TEXT("MUTATOR") &&
						 Actor->ServerState.ServerInfo(i).Value.Caps() == MutName.Caps() )
						bFound = true;

				if (!bFound)
				{
					guard(AddingHiddenServer);

					FKeyValuePair* Data= new(Actor->ServerState.ServerInfo) FKeyValuePair();
					Data->Key = TEXT("mutator");
					Data->Value = MutName;
					unguard;
				}
			}

			unguard;

			AddWebRtcDetails( Actor, Actor->ServerState.ServerInfo );
			for( INT i=0; i<Connections.Num(); i++ )
				Connections(i)->SendGameState();
		}
	}
	unguard;
}

UBOOL FMasterServerUplinkLink::SendStatLine( const TCHAR* StatLine )
{
	for( INT i=0; i<Connections.Num(); i++ )
		Connections(i)->SendStatLine( StatLine );
	return 1;
}

/*-----------------------------------------------------------------------------
	FServerQueryInterface implementaion
-----------------------------------------------------------------------------*/

#define MAX_QUERY_PACKET_THRESHOLD 450

void FServerQueryInterface::OnReceivedData( FIpAddr SrcAddr, BYTE* Data, INT Count )
{
	guard(FServerQueryInterface::OnReceivedData);

	FArchiveUdpReader ArRecv( Data, Count );
	FArchiveUdpWriter ArSend( this, SrcAddr );
	BYTE Command;
	ArRecv << Command;
	if( !ArRecv.IsError() && ArRecv.AtEnd() )
		Answer( Command, ArSend );
	unguard;
}

void FServerQueryInterface::Answer( BYTE Command, FArchive& ArSend )
{
	switch( Command )
	{
	case QI_Ping:
		SendPing( ArSend );
		break;
	case QI_Rules:
		SendRules( ArSend );
		break;
	case QI_Players:
		SendPlayers( ArSend );
		break;
	case QI_RulesAndPlayers:
		SendRules( ArSend );
		SendPlayers( ArSend );
		break;
	case QI_SmallPing:
		SendSmallPing( ArSend );
		break;
	default:
		GWarn->Logf(TEXT("Unknown ping request command: %d"), Command);
		break;
	}
}

void FServerQueryInterface::SendSmallPing( FArchive& ArSend )
{
	BYTE Command = QI_SmallPing;
	BYTE Data = 'P';
	ArSend << Command << Data;
	ArSend.Flush();
}

void FServerQueryInterface::SendPing( FArchive& ArSend )
{
	BYTE Command = QI_Ping;
	ArSend << Command;
	FServerResponseLine PingResponse = Uplink->Actor->ServerState;
	PingResponse.PlayerInfo.Empty();
	PingResponse.ServerInfo.Empty();
	// Browsers learn web support and the join details from the ping, rules only come when a row is clicked
	AddWebRtcDetails( Uplink->Actor, PingResponse.ServerInfo );
	ArSend << PingResponse;
	ArSend.Flush();
}

void FServerQueryInterface::SendRules( FArchive& ArSend )
{
	BYTE Command = QI_Rules;

	TArray<FKeyValuePair>& ServerInfo = Uplink->Actor->ServerState.ServerInfo;
	AddWebRtcDetails( Uplink->Actor, ServerInfo );
	ArSend << Command;
	for( INT i=0;i<ServerInfo.Num();i++ )
	{
		if( ArSend.Tell() > MAX_QUERY_PACKET_THRESHOLD )
		{
			ArSend.Flush();
			ArSend << Command;
		}
		ArSend << ServerInfo(i);
	}
	ArSend.Flush();
}

void FServerQueryInterface::SendPlayers( FArchive& ArSend )
{
	BYTE Command = QI_Players;

	TArray<FPlayerResponseLine>& PlayerInfo = Uplink->Actor->ServerState.PlayerInfo;

	if( PlayerInfo.Num() )
	{
		ArSend << Command;
		for( INT i=0;i<PlayerInfo.Num();i++ )
		{
			if( ArSend.Tell() > MAX_QUERY_PACKET_THRESHOLD )
			{
				ArSend.Flush();
				ArSend << Command;
			}
			ArSend << PlayerInfo(i);
		}
		ArSend.Flush();
	}
}


/*-----------------------------------------------------------------------------
	FServerLANInterface implementaion
-----------------------------------------------------------------------------*/

FServerLANInterface::FServerLANInterface( INT InPort, class FMasterServerUplinkLink* InUplink, FServerQueryInterface* InReplyInterface )
:	FServerQueryInterface( InPort, InUplink )
,	ReplyInterface(InReplyInterface)
{
    if (Uplink->Actor->LANPort >= 0)
    {
	    // on statup, broadcast our info in case anyone is listening on the server.
    	Uplink->Actor->eventRefresh();
    	FIpAddr Addr;
    	Addr.Addr = INADDR_BROADCAST;
    	Addr.Port = Uplink->Actor->LANPort;
    	FArchiveUdpWriter ArSend( ReplyInterface, Addr );
    	SendPing( ArSend );
    }
}

void FServerLANInterface::OnReceivedData( FIpAddr SrcAddr, BYTE* Data, INT Count )
{
	guard(FServerLANInterface::OnReceivedData);
	Uplink->Actor->eventRefresh();
	ReplyInterface->OnReceivedData( SrcAddr, Data, Count );
	unguard;
}


/*-----------------------------------------------------------------------------
	AMasterServerUplink
-----------------------------------------------------------------------------*/

void AMasterServerUplink::execReconnect( FFrame& Stack, RESULT_DECL )
{
	P_FINISH;
	if( !LinkPtr )
		LinkPtr = (PTRINT)(new FMasterServerUplinkLink(this));

	if( WebRtcOnly() )
		GWarn->Logf(TEXT("MasterServerUplink: WebRTC only, listed on the web master alone"));
	else if( DoUplink )
		((FMasterServerUplinkLink*)LinkPtr)->Connect();
	else
		GWarn->Logf(TEXT("MasterServerUplink: DoUplink is False, not connecting to Epic master server"));
}

void AMasterServerUplink::execForceGameStateRefresh( FFrame& Stack, RESULT_DECL )
{
	P_GET_INT(When);
	P_FINISH;

	if ( LinkPtr )
		((FMasterServerUplinkLink*)LinkPtr)->ForceStateRefresh(When);
}

void AMasterServerUplink::execLogStatLine( FFrame& Stack, RESULT_DECL )
{
	P_GET_STR(StatLine);
	P_FINISH;
	if( !LinkPtr )
		LinkPtr = (PTRINT)(new FMasterServerUplinkLink(this));
	*(UBOOL*)Result = ((FMasterServerUplinkLink*)LinkPtr)->SendStatLine( *StatLine );
}

UBOOL AMasterServerUplink::Poll( INT WaitTime )
{
	guard(AMasterServerClient::Poll);
	if( LinkPtr )
		return ((FMasterServerUplinkLink*)LinkPtr)->Poll(WaitTime);
	return 0;
	unguard;
}

void AMasterServerUplink::Destroy()
{
	if( LinkPtr )
		delete ((FMasterServerUplinkLink*)LinkPtr);
	LinkPtr = 0;
	Super::Destroy();
}

void AMasterServerUplink::PostScriptDestroyed()
{
	if( LinkPtr )
		delete ((FMasterServerUplinkLink*)LinkPtr);
	LinkPtr = 0;
	Super::PostScriptDestroyed();
}

IMPLEMENT_CLASS(AMasterServerUplink);
IMPLEMENT_CLASS(AMasterServerGameStats);

