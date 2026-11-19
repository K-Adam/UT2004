# Security

The game talks to master servers, game servers and other players, and a dedicated server takes traffic from anyone. Bugs in that code can put players and server hosts at risk, so reports are welcome.

## What a server can do to a player

Joining a server runs that server's UnrealScript on the player's machine, as the retail game does. On Windows, Linux and Android that script runs with the game's own rights. It can read and write the game's ini files and logs. It cannot load native code or start programs. A web page it wants to open is shown to the player first, who can say no. The web build is confined by the browser's sandbox.

`AllowDownloads=False` in `[IpDrv.TcpNetDriver]` keeps new packages from being downloaded. Packages already in the game folders or in the cache are still used. Join servers you trust.

## Running a server

- The shipped access control, `XAdminFix.FixAccessControl`, has no admin until `AdminPassword` is set, and refuses an empty password.
- `XAdminFix.FixAccessControlIni`, for per-user admin rights, creates no default admin and refuses an empty password and the retail `Admin`/`Admin` pair. Create the first admin with `?AdminName=...?AdminPassword=...` on the server's command line. The retail `XAdmin.AccessControlIni` has neither protection, do not use it.
- The UTV proxy (`ucc utv`) has no primary client and no remote control until `PrimaryPassword` is set in `utv.ini`. Remote control sends its password unencrypted, keep its port off the internet.
- The release page lists the SHA-256 sums of the archives in `SHA256SUMS`.

## Supported versions

Fixes go into `main` and the next release. Only the latest release is supported, older ones are not patched.

## Reporting a vulnerability

Please do not open a public issue. Report it privately instead, either way works:

- GitHub: the Security tab of this repository, then "Report a vulnerability".
- Email: security@ut2004.dev

Include the platform, the build or commit, and the steps or the packet that triggers the problem. A proof of concept helps but is not required.

The report is credited in the fix unless you ask otherwise.
