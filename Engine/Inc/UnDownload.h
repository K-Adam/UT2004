/*=============================================================================
	UnDownload.h: Unreal file-download interface
	Copyright 1997-2000 Epic Games, Inc. All Rights Reserved.

Revision history:
	* Created by Jack Porter.
=============================================================================*/

class ENGINE_API UDownload : public UObject
{
	DECLARE_ABSTRACT_CLASS(UDownload,UObject,CLASS_Transient|CLASS_Config,Engine);
    NO_DEFAULT_CONSTRUCTOR(UDownload);

	// Variables.
	UNetConnection* Connection;			// Connection
	INT				PackageIndex;		// Index of package in Map.
	FPackageInfo*	Info;				// Package Info
	FString			DownloadParams;		// Download params sent to the client.
	UBOOL			UseCompression;		// Send compressed files to the client.
	FArchive*		RecvFileAr;			// File being received.
	TCHAR			TempFilename[256];	// Filename being transfered.
	TCHAR			Error[256];			// A download error occurred.
	INT				Transfered;			// Bytes transfered.
	UBOOL			SkippedFile;		// File was skipped.
	UBOOL			IsCompressed;		// Use file compression.
	INT				AttemptNumber;		// Attempt number
	INT				FileNumber;			// Position of this file among the files to download.
	INT				FileCount;
	INT				ExpectedSize;		// Size of the whole file, which a stock package's own source knows better than the server.

	// Constructors.
	void StaticConstructor();

	// UObject interface.
	void Destroy();
	void Serialize( FArchive& Ar );

	// UDownload Interface.
	virtual UBOOL TrySkipFile();
	virtual void ReceiveFile( UNetConnection* InConnection, INT PackageIndex, const TCHAR *Params, UBOOL InCompression, INT InAttempt );
	virtual void ReceiveData( BYTE* Data, INT Count );
	virtual void Tick() {} 
	virtual void DownloadError( const TCHAR* Error );
	virtual void DownloadDone();

	// The retail size, MD5 and content path of a stock package, from StockPackages.ini.
	static UBOOL StockPackage( const FGuid& Guid, INT& Size, FString& MD5, FString& Path );
};

class ENGINE_API UChannelDownload : public UDownload
{
	DECLARE_CLASS(UChannelDownload,UDownload,CLASS_Transient|CLASS_Config,Engine);
    NO_DEFAULT_CONSTRUCTOR(UChannelDownload);
	
	// Variables.
	UFileChannel* Ch;

	// Constructors.
	void StaticConstructor();

	// UObject interface.
	void Destroy();
	void Serialize( FArchive& Ar );

	// UDownload Interface.
	void ReceiveFile( UNetConnection* InConnection, INT PackageIndex, const TCHAR *Params, UBOOL InCompression, INT InAttempt );
	UBOOL TrySkipFile();
};

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/

