# Security

The game talks to master servers, game servers and other players, and a dedicated server takes traffic from anyone. Bugs in that code can put players and server hosts at risk, so reports are welcome.

## Reporting a vulnerability

Please do not open a public issue. Use GitHub's private reporting instead: the Security tab of this repository, then "Report a vulnerability".

Include the platform, the build or commit, and the steps or the packet that triggers the problem. A proof of concept helps but is not required.

You get an answer within a week. Fixes land on `main`, and the report is credited unless you ask otherwise.

## Scope

In scope: the code in this repository, which includes the web page and the Android app.

Out of scope: the retail game binaries, third party servers and masters, and the libraries listed in `System/THIRD_PARTY.md` (report those upstream, and tell me if this port needs to update).
