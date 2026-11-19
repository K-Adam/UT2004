// Before the engine headers, which define clock as a macro.
#pragma pack(push,8)
#include <random>
#pragma pack(pop)
#include "UTVRemoteControll.h"
#include "BunchDelayer.h"
#include "UTVPackageMap.h"

// Win64 compiler complains about implicit size_t to INT conversion in plain
//  strlen(), since it could in theory lose bits, but we probably won't have
//  a >4GB string in here.
static inline INT istrlen(const char *str)
{
    return((INT) strlen(str));
}

UTVRemoteControll::UTVRemoteControll(INT port)
{
	BindPort(port);
}

UTVRemoteControll::~UTVRemoteControll(void)
{
}

void UTVRemoteControll::Tick(void)
{
	Poll();
	while(WaitingClients.Size()>10)
		WaitingClients.PopFront();
}

void UTVRemoteControll::OnReceivedData( FIpAddr SrcAddr, BYTE* Data, INT Count )
{
	if(Count>=4 && Data[0]==255 && Data[1]==255 && Data[2]==255 && Data[3]==255){

		if(Count>=18 && memcmp(&Data[4],"challenge rcon\n",14)==0){
			// The reply is parsed as a signed number.
			DWORD Challenge=std::random_device()() & 0x7fffffff;

			WaitingClient wc;
			wc.address=SrcAddr;
			wc.ChallengeGiven=Challenge;
			WaitingClients.PushBack(wc);

			char Response[50];
			sprintf(Response,"xxxxchallenge rcon %u\n",Challenge);
			*(DWORD*)Response=0xffffffff;
			SendTo(SrcAddr,(BYTE*)Response,istrlen(Response));
		} else if(Count>=9 && memcmp(&Data[4],"rcon ",5)==0){
			char* tempBuf=new char[Count];
			memcpy(tempBuf,&Data[9],Count-9);
			tempBuf[Count-9]=0;
			FString s(tempBuf);
			delete[] tempBuf;

			INT pos;
			FString Challenge;
			for(pos=0;pos<s.Len();++pos){
				if(s[pos]==TEXT(' '))
					break;
				Challenge+=s.Mid(pos,1);
			}
			DWORD remoteChallenge=appAtoi(*Challenge);

			bool challengeOk=false;
			for(UTVList<WaitingClient>::TIterator wci(WaitingClients);wci;++wci){
				if(wci->address==SrcAddr && wci->ChallengeGiven==remoteChallenge){
					challengeOk=true;
					break;
				}
			}
			if(!challengeOk) {
				debugf(TEXT("Rcon: Client %s failed challenge"), *SrcAddr.GetString(false));
				return;
			}
			FString password;
			if(++pos>=s.Len() || s[pos]!='\"')
				return;
			for(++pos;pos<s.Len();++pos){
				if(s[pos]==TEXT('\"'))
					break;
				password+=s.Mid(pos,1);
			}
			if(!PasswordMatches(UtvEngine->PrimaryPassword,password)) {
				debugf(TEXT("Rcon: Client %s sent bad password"), *SrcAddr.GetString(false));
				return;
			}

			FString command=s.Mid(pos + 2);
			if(!command.Len())
				return;
			// A set command may carry new passwords.
			debugf(TEXT("Rcon: Client %s sent a command"), *SrcAddr.GetString(false));

			//Check if the command is a special remotecommand and not a regular update url
			if (appStrncmp(*command,TEXT("get"),3)==0) {
				//Need to convert the string values to non unicode
				char ServerAddr[100];
				FToChar(UtvEngine->ServerAdress, ServerAddr, 100);

				// The password fields stay for the tools that parse this, empty. The reply is not encrypted.
				char Response[1000];
				sprintf(Response,"xxxxstatus serveraddress=%s?serverport=%i?listenport=%i?delay=%f?"
					"maxclients=%i?primarypassword=?normalpassword=?vippassword=?joinpassword=?"
					"seeall=%i?noprimary=%i?ignorechainedchat=%i?state=%i?count=%i",
					ServerAddr, UtvEngine->ServerPort, UtvEngine->ListenPort, Delayer->DelayTime,
					UtvEngine->MaxClients,
					UtvEngine->SeeAll, UtvEngine->NoPrimary, UtvEngine->IgnoreChainedChat, 
					UtvEngine->ConnectStatus, UtvEngine->TotalClientsLocal);
				*(DWORD*)Response=0xffffffff;

				//pass it on
				SendTo(SrcAddr,(BYTE*)Response,istrlen(Response));
			}
			else if (appStrncmp(*command,TEXT("ver"),3)==0) {
				FString v(UTVVERSION);
				char Ver[100];
				FToChar(v, Ver, 100);
				char Response[1000];
				//The "1" means remote protocol 1.. will probably never change
				sprintf(Response,"xxxxversion 1 %s", Ver);
				*(DWORD*)Response=0xffffffff;
				SendTo(SrcAddr,(BYTE*)Response,istrlen(Response));
			}
			else if (appStrncmp(*command,TEXT("cur"),3)==0) {
				char Response[1000];
				sprintf(Response,"xxxxstate %i %i", UtvEngine->ConnectStatus, UtvEngine->TotalClientsLocal);
				*(DWORD*)Response=0xffffffff;
				SendTo(SrcAddr,(BYTE*)Response,istrlen(Response));
			}
			else {
				UtvEngine->ParseCmdLine(*command);			
				char Response[1000];
				sprintf(Response,"xxxxsetok");
				*(DWORD*)Response=0xffffffff;
				SendTo(SrcAddr,(BYTE*)Response,istrlen(Response));
			}
		}
	}
}

//De-unicodes an fstring..
void UTVRemoteControll::FToChar(const FString &f, char *outbuf, int max)
{
	int i;
	for (i = 0; (i < f.Len()) && (i < (max - 1)); ++i) {
		outbuf[i] = f[i];
	}
	outbuf[i] = 0;
}
