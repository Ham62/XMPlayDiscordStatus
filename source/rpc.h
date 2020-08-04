#include <windows.h>
#include <stdio.h>
#include "discord-rpc/include/discord_rpc.h"

enum PlayingStatus { playing, paused, stopped };


void handleDiscordReady(const DiscordUser *request);

void handleDiscordDisconnected(int errcode, const char* message);

void handleDiscordError(int errcode, const char* message);

void handleDiscordJoin(const char* secret);

void handleDiscordSpectate(const char* secret);

void handleDiscordJoinRequest(const DiscordUser *request);

void InitDiscord();

void InitPresence();

void UpdateIconText(char *pszVer);

void UpdatePresence(char *pszFirstLine, char *pszSecondLine, PlayingStatus status, int iTrackPos, int iTrackLen = 0);

void ClearPresence();
