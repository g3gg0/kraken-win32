// Folgender ifdef-Block ist die Standardmethode zum Erstellen von Makros, die das Exportieren 
// aus einer DLL vereinfachen. Alle Dateien in dieser DLL werden mit dem EXT_XMPP_EXPORTS-Symbol
// kompiliert, das in der Befehlszeile definiert wurde. Das Symbol darf nicht für ein Projekt definiert werden,
// das diese DLL verwendet. Alle anderen Projekte, deren Quelldateien diese Datei beinhalten, erkennen 
// EXT_XMPP_API-Funktionen als aus einer DLL importiert, während die DLL
// mit diesem Makro definierte Symbole als exportiert ansieht.
#ifdef EXT_XMPP_EXPORTS
#define EXT_XMPP_API __declspec(dllexport)
#else
#define EXT_XMPP_API __declspec(dllimport)
#endif

#pragma once

extern "C" 
{
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <strophe.h>
}

#include <Kraken.h>
#include <process.h>
#include <stdio.h>
#include <locale.h>
#include <map>
#include <string>
#include <sstream>
using namespace std;

class XMPPServerCore
{
  public:
    XMPPServerCore(Kraken *instance, char *parms);
    virtual ~XMPPServerCore();
	bool writeClient(int client, string msg);
    void start();
    void onConnect(xmpp_conn_t * const conn, const xmpp_conn_event_t status, 
		const int error, xmpp_stream_error_t * const stream_error,
		void * const userdata);
    void onDisconnect(xmpp_conn_t * const conn, const xmpp_conn_event_t status, 
		const int error, xmpp_stream_error_t * const stream_error,
		void * const userdata);
    void handleMessage( char *from, char *text, char *type, xmpp_ctx_t *session );
    void handleMessageSession( char *from, xmpp_ctx_t *session  );
	void handlePresence(char *from, char *type, xmpp_ctx_t *session);
	void joinConference(xmpp_ctx_t *session);
	void asyncHandler();

	char *username;
	int joinConferenceTries;
	int joinConferenceTime;
	xmpp_ctx_t *serverContext;
	
	bool stopAsyncHandler;
	bool finishedAsyncHandler;
	bool presenceAvailable;

  private:
	xmpp_conn_t *connection;
	char *password;
	char *authToken;
	char *parameters;
	char *owner;
	char *nickname;
	char *conference;
	char *conferenceNick;
	Kraken *krakenInstance;
	map<string, int> contactIdMap;
	map<string, bool> contactAuthMap;
	map<int, string> contactIdMapRev;
	map<string, xmpp_ctx_t *> contactSessionMap;
	
	int currentId;
};

