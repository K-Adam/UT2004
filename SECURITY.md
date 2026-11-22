# Security

The game talks to master servers, game servers and other players, and a dedicated server takes traffic from anyone. Bugs in that code can put players and server hosts at risk, so reports are welcome.

## What a server can do to a player

Joining a server runs that server's UnrealScript on the player's machine, as the retail game does. On Windows, Linux and Android that script runs with the game's own rights. It can change the game's settings and write logs, saves, screenshots and demos in the game's folders. It is not meant to reach other files, load native code or start programs, and anything that lets it is a bug to report. A web page it wants to open is shown to the player first, who can say no. The web build is confined by the browser's sandbox.

A package whose classes, structures and functions do not fit together the way the compiler makes them is repaired as it loads and refused: none of its script runs and the game leaves the level with a message. The log names the reason in a line that starts with `Refused`. The rules were run over 1,427 retail, downloaded and mod packages without a refusal, so one on a server that works in the retail game is a bug to report.

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
