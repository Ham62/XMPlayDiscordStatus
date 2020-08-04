#include <windows.h>
#include <cstdlib>
#include "xmp-sdk/xmpdsp.h" // requires the XMPlay "DSP/general plugin SDK"
#include "resource.h"
#include "rpc.h"

#define SHOW_DEBUG 0

#define TIMER_PERCISION 2000 // in milliseconds (Please keep round seconds)

static XMPFUNC_MISC *xmpfmisc;
static XMPFUNC_STATUS *xmpfstatus;

static HINSTANCE hInstance;
static HWND hWndConf = 0;

static HWND hWndXMP;

static UINT_PTR hTimer;        // Timer for tracking play/pause status
static int iExpectedRemaining; // How much time should be left on current track
DWORD dwTickCounter = 0;       // Used to track play/pause status

static BOOL CALLBACK DSPDialogProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static void WINAPI DSP_About(HWND win);
static void *WINAPI DSP_New(void);
static void WINAPI DSP_Free(void *inst);
static const char *WINAPI DSP_GetDescription(void *inst);
static void WINAPI DSP_Config(void *inst, HWND win);
static DWORD WINAPI DSP_GetConfig(void *inst, void *config);
static BOOL WINAPI DSP_SetConfig(void *inst, void *config, DWORD size);
static void WINAPI DSP_NewTrack(void *inst, const char *file);
static void WINAPI DSP_Reset(void *inst);
static void WINAPI DSP_SetFormat(void *inst, const XMPFORMAT *form);
static DWORD WINAPI DSP_Process(void *inst, float *buffer, DWORD count);
static void WINAPI DSP_NewTitle(void *inst, const char *title);

// Constants and struct for the configuration

#define TIME_DISPLAY_REMAINING 0
#define TIME_DISPLAY_ELAPSED   1
#define TIME_DISPLAY_DISABLED  2

typedef struct {
	BOOL showAlbumDate;
	BOOL showXMPVersion;
	int  timeDisplayType;
} Config;
static Config conf;

static XMPDSP dsp = {
	XMPDSP_FLAG_TITLE, // Don't support multiple instances
	"XMPlay Discord Status",
	DSP_About,
	DSP_New,
	DSP_Free,
	DSP_GetDescription,
	DSP_Config,
	DSP_GetConfig,
	DSP_SetConfig,
	DSP_NewTrack,
	DSP_SetFormat,
	DSP_Reset,
	DSP_Process,
	DSP_NewTitle,
};

static void UpdateStatus(PlayingStatus status) {
	static char *pszFirstLine, *pszSecondLine;
	int iPos = -1, iLen = 0;

	char *pszFormattedTitle = xmpfmisc->GetTag(TAG_FORMATTED_TITLE);

	switch (status) {
		case playing: {
			// Set timestamp for current track if enabled
			if (conf.timeDisplayType != TIME_DISPLAY_DISABLED) {
				iPos = (int)xmpfstatus->GetTime();

				// Only get track length if displaying remaining time
				if (conf.timeDisplayType == TIME_DISPLAY_REMAINING) {
					char *pszLen = xmpfmisc->GetTag(TAG_LENGTH);
					iLen = strtol(pszLen, NULL, 10);
					xmpfmisc->Free(pszLen);
				}
			}
		}
		case paused: {
			pszFirstLine = pszFormattedTitle;

			char *pszAlbum = xmpfmisc->GetTag(TAG_ALBUM);
			char *pszYear = xmpfmisc->GetTag(TAG_DATE);

			static char szBuffer[128];
			if (conf.showAlbumDate) // Format 2nd line as 'Album (year)'
				sprintf_s(szBuffer, sizeof(szBuffer), "%s (%s)\0", pszAlbum, pszYear);
			else                    // Or just show 'Album'
				sprintf_s(szBuffer, sizeof(szBuffer), "%s\0", pszAlbum);
			pszSecondLine = szBuffer;

			xmpfmisc->Free(pszAlbum); // Free the tags now that we're done with them
			xmpfmisc->Free(pszYear);
			break;
		}
		case stopped:
			pszFirstLine = "No tracks playing.";
			pszSecondLine = "";
			break;
	}


	UpdatePresence(pszFirstLine, pszSecondLine, status, iPos, iLen);

	xmpfmisc->Free(pszFormattedTitle);
}

// Force a resync of the status next timer tick regardless of track state
static void UpdateStatus() {
	iExpectedRemaining = -1;
	dwTickCounter = 0;
}

// Should the XMPlay icon contain the version when hovering?
static void ShowXMPVersion(BOOL isShowing) {
	static char szVersion[128];
	if (isShowing) {
		// Get XMPlay version
		DWORD dwVersion = xmpfmisc->GetVersion();
		int patch = (dwVersion & 0xFF);
		int rev = (dwVersion & 0xFF00) >> 8;
		int minor = (dwVersion & 0xFF0000) >> 16;
		int major = (dwVersion & 0xFF000000) >> 24;

		sprintf_s(szVersion, sizeof(szVersion), "XMPlay v%d.%d.%d.%d\0", major, minor, rev, patch);
	} else {
		sprintf_s(szVersion, sizeof(szVersion), "XMPlay\0");
	}
	UpdateIconText(szVersion);
}

#define MESS(_id, _m, _w, _l) SendDlgItemMessage(hWnd, _id, _m, (WPARAM)(_w), (LPARAM)(_l))
static BOOL CALLBACK DSPDialogProc(HWND hWnd, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	switch (iMsg) {
		case WM_COMMAND:
			switch (LOWORD(wParam)) {
				case IDOK: {
					conf.showAlbumDate = (BST_CHECKED == MESS(IDC_SHOWALBUMDATE, BM_GETCHECK, 0, 0));
					BOOL showVersion = (BST_CHECKED == MESS(IDC_SHOWVERSION, BM_GETCHECK, 0, 0));
					if (conf.showXMPVersion != showVersion) {
						conf.showXMPVersion = showVersion;
						ShowXMPVersion(showVersion);
					}

					// Check which style of timer we want
					if (BST_CHECKED == MESS(IDC_TIMEELAPSED, BM_GETCHECK, 0, 0))
						conf.timeDisplayType = TIME_DISPLAY_ELAPSED;
					else if (BST_CHECKED == MESS(IDC_TIMEREMAINING, BM_GETCHECK, 0, 0))
						conf.timeDisplayType = TIME_DISPLAY_REMAINING;
					else
						conf.timeDisplayType = TIME_DISPLAY_DISABLED;

					UpdateStatus();
				}
				case IDCANCEL:
					EndDialog(hWnd, 0);
					break;
				}
			break;

		case WM_INITDIALOG:
			hWndConf = hWnd;
			MESS(IDC_SHOWALBUMDATE, BM_SETCHECK, conf.showAlbumDate, 0);
			MESS(IDC_SHOWVERSION, BM_SETCHECK, conf.showXMPVersion, 0);
			switch (conf.timeDisplayType) {
				case TIME_DISPLAY_REMAINING:
					CheckRadioButton(hWnd, IDC_TIMEREMAINING, IDC_TIMEDISABLED, IDC_TIMEREMAINING);
					break;
				case TIME_DISPLAY_ELAPSED:
					CheckRadioButton(hWnd, IDC_TIMEREMAINING, IDC_TIMEDISABLED, IDC_TIMEELAPSED);
					break;
				case TIME_DISPLAY_DISABLED:
					CheckRadioButton(hWnd, IDC_TIMEREMAINING, IDC_TIMEDISABLED, IDC_TIMEDISABLED);
					break;
			}
			return TRUE;

		case WM_DESTROY:
			hWndConf = 0;
			break;
	}
	return FALSE;
}

// Responsible for checking the play/pause/stopped state
static void CALLBACK TimerProc(HWND hWnd, UINT uMsg, UINT idEvent, DWORD dwTime) {
	static DWORD dwOldCount;
	static PlayingStatus oldStatus, newStatus;

	BOOL isPlaying = xmpfstatus->IsPlaying(); // Only returns play/stop, not paused


	newStatus = (!isPlaying ? stopped : (dwOldCount == dwTickCounter ? paused : playing));

	if (newStatus == playing) {
		iExpectedRemaining -= (TIMER_PERCISION / 1000);

		// Calculate remaining time on track
		int iRemaining = strtol(xmpfmisc->GetTag(TAG_LENGTH), NULL, 10) - (int)xmpfstatus->GetTime();
	
		// If expected time is more than 5 secs off, resync it
		if (abs(iRemaining - iExpectedRemaining) > 5) {
			#if (SHOW_DEBUG)
			printf("Out of sync... resyncing\r\n");
			#endif
			iExpectedRemaining = iRemaining;
			UpdateStatus(playing);
			return;
		}
	}

	#if (SHOW_DEBUG)
	// Check if song is stopped
	if (!isPlaying) {
		printf("Song is stopped\r\n");
	} else {
		if (dwOldCount == dwTickCounter) {
			printf("Song is paused\r\n");
		} else {
			printf("Song is playing\r\n");
		}
	}
	#endif

	dwOldCount = dwTickCounter;

	// Update the status if it's changed
	if (newStatus != oldStatus) {
		oldStatus = newStatus;
		UpdateStatus(newStatus);
	}
}

// When the 'About' button is clicked
static void WINAPI DSP_About(HWND hWnd)
{
	MessageBoxA(hWnd,
		"XMPlay to Discord Status plugin\r\n"
		"Copyright 2020 Graham Downey\r\n\r\n"
		"Based on xmp-discordrichpres by Cynthia (Cynosphere)\r\n"
		"and XMPlay2MSN by Elliot Sales de Andrade",
		"XMPlay Discord Status",
		MB_OK | MB_ICONINFORMATION);
}

// Called when XMPlay initializes the plugin
static void *WINAPI DSP_New() {
	#if (SHOW_DEBUG)
	AllocConsole();
	FILE *fDummy;
	freopen_s(&fDummy, "CONOUT$", "w", stdout);
	#endif

	// Get XMPlay window handle
	hWndXMP = xmpfmisc->GetWindow();

	InitDiscord();	// Initalize the Discord interface
	InitPresence(); // Initalize the Discord status struct

	// Default config
	conf.showAlbumDate = TRUE;
	conf.showXMPVersion = TRUE;
	conf.timeDisplayType = TIME_DISPLAY_REMAINING;
	ShowXMPVersion(conf.showXMPVersion);

	// Create timer to monitor play/pause status
	hTimer = SetTimer(hWndXMP, 0, TIMER_PERCISION, TimerProc);

	return (void*)1; // Do not allow more than one instance of this plugin
}

// When XMPlay unloads the plugin
static void WINAPI DSP_Free(void *inst)
{
	if (hWndConf) EndDialog(hWndConf, 0);

	KillTimer(hWndXMP, hTimer); // Kill the playing status timer

	ClearPresence();    // Clear the Discord status
	Discord_Shutdown(); // Close the Discord interface
}

// For the XMPlay plugin UI screen
static const char *WINAPI DSP_GetDescription(void *inst)
{
	return dsp.name;
}

// Open the configuration window
static void WINAPI DSP_Config(void *inst, HWND hWnd)
{
	DialogBox(hInstance, MAKEINTRESOURCE(IDD_CONFIG), hWnd, &DSPDialogProc);
}

// Save config
static DWORD WINAPI DSP_GetConfig(void *inst, void *config)
{
	memcpy(config, &conf, sizeof(conf));
	return sizeof(conf); // return size of config info
}

// Load config
static BOOL WINAPI DSP_SetConfig(void *inst, void *config, DWORD size)
{
	memcpy(&conf, config, min(size, sizeof(conf)));

	// Show XMPlay version on icon?
	ShowXMPVersion(conf.showXMPVersion);

	return TRUE;
}

// New track opened (or closed if file == NULL)
static void WINAPI DSP_NewTrack(void *inst, const char *file)
{
	if (file == NULL) {
		UpdateStatus(stopped);
	}
	else {
		UpdateStatus(playing);
	}
}

// We don't actually need this information, but XMPlay needs this to exist
void WINAPI DSP_SetFormat(void *inst, const XMPFORMAT *form) {}

// Called by XMPlay when seeking in a track
static void WINAPI DSP_Reset(void *inst)
{
	UpdateStatus(playing);
}

// We are just using this function as a counter to check the pause status of a track
static DWORD WINAPI DSP_Process(void *inst, float *buffer, DWORD count) {
	dwTickCounter++;
	return 0;
}

// If the track title changes, update it
static void WINAPI DSP_NewTitle(void *inst, const char *title) {
	UpdateStatus(playing);
}

#ifdef __cplusplus
extern "C"
#endif

// get the plugin's XMPDSP interface
XMPDSP *WINAPI XMPDSP_GetInterface2(DWORD face, InterfaceProc faceproc)
{
	if (face != XMPDSP_FACE) return NULL;
	xmpfmisc = (XMPFUNC_MISC*)faceproc(XMPFUNC_MISC_FACE); // import "misc" functions
	xmpfstatus = (XMPFUNC_STATUS*)faceproc(XMPFUNC_STATUS_FACE); // import "status" functions
	return &dsp;
}

BOOL WINAPI DllMain(HINSTANCE hDLL, DWORD reason, LPVOID reserved)
{
	if (reason == DLL_PROCESS_ATTACH) {
		hInstance = hDLL;
		DisableThreadLibraryCalls(hDLL);
	}
	return TRUE;
}
