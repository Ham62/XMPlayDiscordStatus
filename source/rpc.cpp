#include <windows.h>
#include <stdio.h>
#include <ctime>
#include "discord-rpc/include/discord_rpc.h"
#include "rpc.h"
#pragma comment(lib, "discord-rpc/lib/discord-rpc.lib")

const char* APPLICATION_ID = "738447017889759243";
DiscordRichPresence discordPresence;
bool initialized = false;

// Dummy function to please Discord
void handleDiscordReady(const DiscordUser *request) {}
void handleDiscordDisconnected(int errcode, const char* message) {}
void handleDiscordError(int errcode, const char* message) {}
void handleDiscordJoin(const char* secret) {}
void handleDiscordSpectate(const char* secret) {}
void handleDiscordJoinRequest(const DiscordUser *request) {}

void InitDiscord()
{
	// Create and populate the Discord message handler struct
	DiscordEventHandlers handlers;
	memset(&handlers, 0, sizeof(handlers));

	handlers.ready = handleDiscordReady;
	handlers.disconnected = handleDiscordDisconnected;
	handlers.errored = handleDiscordError;
	handlers.joinGame = handleDiscordJoin;
	handlers.spectateGame = handleDiscordSpectate;
	handlers.joinRequest = handleDiscordJoinRequest;

	// Register our app as running for Discord
	Discord_Initialize(APPLICATION_ID, &handlers, 1, NULL);
}

// Initalize the status struct
void InitPresence()
{
	if (initialized) return;
	memset(&discordPresence, 0, sizeof(discordPresence));

	discordPresence.largeImageKey = "icon"; // The large XMPlay icon
	discordPresence.largeImageText = "XMPlay";
	discordPresence.instance = 1;
}

void UpdateIconText(char *pszVer) {
	static char szBuff[128]; // Copy text to buffer so it can be safely free'd
	strcpy_s(szBuff, 128, pszVer);
	discordPresence.largeImageText = szBuff;
}

void UpdatePresence(char *pszFirstLine, char *pszSecondLine, PlayingStatus status, int iTrackPos, int iTrackLen) {
	static char szDetails[128], szState[128];

	// Copy the strings to static buffers to avoid memory leaks
	strcpy_s(szDetails, 128, pszFirstLine);
	strcpy_s(szState, 128, pszSecondLine);

	discordPresence.details = szDetails;
	discordPresence.state = szState;

	// Reset the timestamps before determining which mode they're in
	discordPresence.startTimestamp = 0;
	discordPresence.endTimestamp = 0;

	// Set small image and time remaining (if currently playing)
	switch (status) {
	case playing:
		discordPresence.smallImageKey = "play";
		discordPresence.smallImageText = "Playing";

		if (iTrackLen > 0) {
			// If track length specified use countdown timer
			discordPresence.endTimestamp = time(NULL) + (iTrackLen - iTrackPos);
		} else if (iTrackPos >= 0) {
			// If no length specified and position positive, use time elapsed
			discordPresence.startTimestamp = time(NULL) - iTrackPos;
		}
		break;
	case paused:
		discordPresence.smallImageKey = "pause";
		discordPresence.smallImageText = "Paused";
		break;
	case stopped:
		discordPresence.smallImageKey = "stopped";
		discordPresence.smallImageText = "Stopped";
		break;
	}

	Discord_UpdatePresence(&discordPresence);
}

void ClearPresence()
{
	Discord_ClearPresence();
	initialized = false;
}
