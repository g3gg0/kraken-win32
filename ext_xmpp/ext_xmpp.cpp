

#include "ext_xmpp.h"


extern "C"
{
	XMPPServerCore *Core;

	void async_thread(void *arg)
	{
		Core->asyncHandler();
	}

	int send_ping(xmpp_conn_t * const conn, xmpp_ctx_t *ctx, const char *from)
	{
		xmpp_stanza_t *iq;
		xmpp_stanza_t *ping;

		iq = xmpp_stanza_new(ctx);
		xmpp_stanza_set_name(iq, "iq");
		xmpp_stanza_set_type(iq, "get");
		xmpp_stanza_set_id(iq, "c2a1");
		xmpp_stanza_set_attribute(iq, "from", from);

		ping = xmpp_stanza_new(ctx);
		xmpp_stanza_set_name(ping, "ping");
		xmpp_stanza_set_attribute(ping, "xmlns", "jabber:iq:ping");

		xmpp_stanza_add_child(iq, ping);

		xmpp_send(conn, iq);
		xmpp_stanza_release(iq);

		return 1;
	}

	int send_message(xmpp_conn_t * const conn, xmpp_ctx_t *ctx, const char *to, const char *msg, const char *type)
	{
		xmpp_stanza_t *reply;
		xmpp_stanza_t *text;
		xmpp_stanza_t *body;

		reply = xmpp_stanza_new(ctx);
		xmpp_stanza_set_name(reply, "message");
		xmpp_stanza_set_type(reply, type);
		xmpp_stanza_set_attribute(reply, "to", to);

		body = xmpp_stanza_new(ctx);
		xmpp_stanza_set_name(body, "body");

		text = xmpp_stanza_new(ctx);
		xmpp_stanza_set_text(text, msg);
		xmpp_stanza_add_child(body, text);
		xmpp_stanza_add_child(reply, body);

		xmpp_send(conn, reply);
		xmpp_stanza_release(reply);

		return 1;
	}

	int version_handler(xmpp_conn_t * const conn, xmpp_stanza_t * const stanza, void * const userdata)
	{
		xmpp_stanza_t *reply, *query, *name, *version, *text;
		char *ns;
		xmpp_ctx_t *ctx = (xmpp_ctx_t*)userdata;
		printf("Received version request from %s\n", xmpp_stanza_get_attribute(stanza, "from"));

		reply = xmpp_stanza_new(ctx);
		xmpp_stanza_set_name(reply, "iq");
		xmpp_stanza_set_type(reply, "result");
		xmpp_stanza_set_id(reply, xmpp_stanza_get_id(stanza));
		xmpp_stanza_set_attribute(reply, "to", xmpp_stanza_get_attribute(stanza, "from"));

		query = xmpp_stanza_new(ctx);
		xmpp_stanza_set_name(query, "query");
		ns = xmpp_stanza_get_ns(xmpp_stanza_get_children(stanza));
		if (ns) 
		{
			xmpp_stanza_set_ns(query, ns);
		}

		name = xmpp_stanza_new(ctx);
		xmpp_stanza_set_name(name, "name");
		xmpp_stanza_add_child(query, name);

		text = xmpp_stanza_new(ctx);
		xmpp_stanza_set_text(text, "Kraken-win32");
		xmpp_stanza_add_child(name, text);

		version = xmpp_stanza_new(ctx);
		xmpp_stanza_set_name(version, "version");
		xmpp_stanza_add_child(query, version);

		text = xmpp_stanza_new(ctx);
		xmpp_stanza_set_text(text, "1.0");
		xmpp_stanza_add_child(version, text);

		xmpp_stanza_add_child(reply, query);

		xmpp_send(conn, reply);
		xmpp_stanza_release(reply);

		return 1;
	}

	int presence_handler(xmpp_conn_t * const conn, xmpp_stanza_t * const stanza, void * const userdata)
	{
		xmpp_ctx_t *ctx = (xmpp_ctx_t*)userdata;

		char *from = xmpp_stanza_get_attribute(stanza, "from");
		char *type = xmpp_stanza_get_attribute(stanza, "type");
		char *status = xmpp_stanza_get_attribute(stanza, "status");

		Core->handlePresence(from, type, ctx);

		return 1;
	}


	int message_handler(xmpp_conn_t * const conn, xmpp_stanza_t * const stanza, void * const userdata)
	{
		char *intext;
		xmpp_ctx_t *ctx = (xmpp_ctx_t*)userdata;

		char *from = xmpp_stanza_get_attribute(stanza, "from");
		char *type = xmpp_stanza_get_attribute(stanza, "type");

		Core->handleMessageSession(from, ctx);

		if(!xmpp_stanza_get_child_by_name(stanza, "body")) 
		{
			return 1;
		}
		if(xmpp_stanza_get_child_by_name(stanza, "delay")) 
		{
			return 1;
		}

		intext = xmpp_stanza_get_text(xmpp_stanza_get_child_by_name(stanza, "body"));

		Core->handleMessage(from, intext, type, ctx);
		return 1;
	}

	void send_presence(xmpp_conn_t * const conn, xmpp_ctx_t *ctx, char *from, char *to)
	{
		xmpp_stanza_t* pres;

		/* Send initial <presence/> so that we appear online to contacts */
		pres = xmpp_stanza_new(ctx);
		xmpp_stanza_set_name(pres, "presence");

		if(to)
		{
			xmpp_stanza_set_attribute(pres, "to", to);
		}

		if(from)
		{
			xmpp_stanza_set_attribute(pres, "from", from);
		}

		xmpp_send(conn, pres);
		xmpp_stanza_release(pres);
	}

	/* define a handler for connection events */
	void conn_handler(xmpp_conn_t * const conn, const xmpp_conn_event_t status, 
		const int error, xmpp_stream_error_t * const stream_error,
		void * const userdata)
	{
		xmpp_ctx_t *ctx = (xmpp_ctx_t *)userdata;

		if (status == XMPP_CONN_CONNECT) 
		{
			xmpp_handler_add(conn,version_handler, "jabber:iq:version", "iq", NULL, ctx);
			xmpp_handler_add(conn,message_handler, NULL, "message", NULL, ctx);
			xmpp_handler_add(conn,presence_handler, NULL, "presence", NULL, ctx);

			send_presence(conn, ctx, NULL, NULL);

			Core->onConnect(conn, status, error, stream_error, userdata);
		}
		else 
		{
			Core->onDisconnect(conn, status, error, stream_error, userdata);
			xmpp_stop(ctx);
		}
	}

	bool WriteClient(void *ctx, int client, char *msg)
	{
		return ((XMPPServerCore*)ctx)->writeClient(client, msg);
	}
}

XMPPServerCore::XMPPServerCore(Kraken *instance, char *parms) 
{
	currentId = 0x40000000;
	krakenInstance = instance;
	parameters = parms;

	stopAsyncHandler = false;
	finishedAsyncHandler = false;
	presenceAvailable = false;
	joinConferenceTries = 0;
	joinConferenceTime = 0;

	/* set our code as client interface */
	krakenInstance->mWriteClientCtx = this;
	krakenInstance->mWriteClient = WriteClient;
}


string encodeForXml( const std::string &sSrc )
{
	ostringstream sRet;

	for( string::const_iterator iter = sSrc.begin(); iter!=sSrc.end(); iter++ )
	{
		unsigned char c = (unsigned char)*iter;

		switch( c )
		{
		case '&': sRet << "&amp;"; break;
		case '<': sRet << "&lt;"; break;
		case '>': sRet << "&gt;"; break;
		case '"': sRet << "&quot;"; break;
		case '\'': sRet << "&apos;"; break;

		default:
			if ( c<32 || c>127 )
			{
				sRet << "&#" << (unsigned int)c << ";";
			}
			else
			{
				sRet << c;
			}
		}
	}

	return sRet.str();
}

XMPPServerCore::~XMPPServerCore() 
{
}

bool XMPPServerCore::writeClient(int client, string msg)
{
	if(contactIdMapRev.find(client) != contactIdMapRev.end())
	{
		if(contactSessionMap.find(contactIdMapRev[client]) != contactSessionMap.end())
		{
			string escaped = msg;
			escaped = encodeForXml(msg);
			send_message(connection, contactSessionMap[contactIdMapRev[client]], contactIdMapRev[client].c_str(), escaped.c_str(), "chat");

			return true;
		}
	}

	return false;
}

void XMPPServerCore::start()
{
	char uname[192];
	char pass[128];
	char nick[128];
	char own[128];
	char auth[192];
	int debug = 0;

	if(sscanf(parameters, "j=%127s p=%127s a=%127s n=%127s o=%127s d=%i", uname, pass, auth, nick, own, debug) < 5)
	{
		printf( " [E] XMPP NOT connected. Please specify username and pass as parameters\n" );
		printf( " [E] in format j=[jabber-id] p=[password] a=[auth token] n=[nick] o=[owner] d=[debuglevel]\n" );
		return;
	}
	strcat(uname, "/Kraken-win32");

	authToken = strdup(auth);
	username = strdup(uname);
	password = strdup(pass);
	nickname = strdup(nick);
	owner = strdup(own);
	conference = strdup("krakencluster@conference.jabber.org");
	conferenceNick = (char*)malloc(strlen(conference) + 2 + strlen(nickname));
	sprintf(conferenceNick, "%s/%s", conference, nickname);
	joinConferenceTime = 0;
	presenceAvailable = false;

	xmpp_log_t *log;

	/* init library */
	xmpp_initialize();

	/* create a context */
	if(debug)
	{
		log = xmpp_get_default_logger(XMPP_LEVEL_DEBUG);
	}
	else
	{
		log = xmpp_get_default_logger(XMPP_LEVEL_ERROR);
	}
	serverContext = xmpp_ctx_new(NULL, log);

	/* create a connection */
	connection = xmpp_conn_new(serverContext);

	/* setup authentication information */
	xmpp_conn_set_jid(connection, uname);
	xmpp_conn_set_pass(connection, pass);

	/* initiate connection */
	xmpp_connect_client(connection, NULL, 0, conn_handler, serverContext);

	/* async stuff here */
	_beginthread(async_thread, 0, NULL);

	/* enter the event loop - our connect handler will trigger an exit */
	xmpp_run(serverContext);

	/* make sure async handler is being finished */
	stopAsyncHandler = true;
	while(!finishedAsyncHandler)
	{
		Sleep(100);
	}

	/* release our connection and context */
	xmpp_conn_release(connection);
	xmpp_ctx_free(serverContext);

	/* final shutdown of the library */
	xmpp_shutdown();
}

void XMPPServerCore::onConnect(xmpp_conn_t * const conn, const xmpp_conn_event_t status, 
							   const int error, xmpp_stream_error_t * const stream_error,
							   void * const userdata)
{
	printf( " [i] XMPP connected\n" );
}

void XMPPServerCore::onDisconnect(xmpp_conn_t * const conn, const xmpp_conn_event_t status, 
								  const int error, xmpp_stream_error_t * const stream_error,
								  void * const userdata)
{
	printf( " [E] XMPP: disconnected: %d\n", error );
	presenceAvailable = false;
}

void XMPPServerCore::handleMessage( char *from, char *msg, char *type, xmpp_ctx_t *session )
{
	if(!from ||!session || !msg || !type)
	{
		return;
	}

	if(strlen(msg) > 0)
	{
		if(!strcmp(type, "error")) 
		{
			return;
		}

		if(!strcmp(type, "groupchat"))
		{
			if(!strcmp(msg,"#discover"))
			{
				char group[512];
				char message[512];

				strcpy(group, from);
				char *sep = strchr((char*)group, '/');

				if(sep)
				{
					*sep = '\000';
				}

				char *state = "READY";
				int increment = krakenInstance->jobIncrement();
				int load = krakenInstance->jobLoad();

				sprintf(message, "## S:%s | L:%i | I:%i | O:%s", state, load, increment, owner);

				string escaped = encodeForXml(string(message));
				send_message(connection, session, group, escaped.c_str(), "groupchat");
			}
		}

		if(!strcmp(type, "chat"))
		{
			//printf(" [i] XMPP: Message from: %s, msg: %s\n", from, msg);
			if(contactIdMap.find(from) != contactIdMap.end())
			{
				/* not authorized yet? */
				if(!contactAuthMap[from])
				{
					char expected[128];
					sprintf(expected, "auth %s", authToken);

					if(!strcmp(expected, msg))
					{
						contactAuthMap[from] = true;
						send_message(connection, session, from, "201 Welcome to Kraken-win32 XMPP/Jabber remote", "chat");
					}
					else
					{
						send_message(connection, session, from, "401 You are not authorized", "chat");
					}
				}
				else
				{
					int id = contactIdMap[from];
					krakenInstance->mServerCmd(id, msg);
				}
			}
		}
	}
}

void XMPPServerCore::joinConference(xmpp_ctx_t *session)
{
	send_presence(connection, session, username, conferenceNick);
}

void XMPPServerCore::handlePresence(char *from, char *type, xmpp_ctx_t *session)
{
	if(!from ||!session)
	{
		return;
	}

	//printf("from: %s, type: %s\n", from, type);

	if(!strcmp(from, username))
	{
		if(type)
		{
			/* we are unavailable, set available again */
			presenceAvailable = false;
			send_presence(connection, session, NULL, NULL);
		}
		else
		{
			/* available, join group chat */
			presenceAvailable = true;
			joinConferenceTime = time(NULL);
		}
	}
	else if(!strcmp(from, conferenceNick))
	{
		if(type)
		{
			/* failed to join conference */
			printf("Failed joining conference\n");

			joinConferenceTries++;
			if(joinConferenceTries < 5)
			{
				joinConferenceTime = time(NULL) + 1;
			}
			else
			{
				joinConferenceTime = time(NULL) + 60;
			}
		}
		else
		{
			/* joined conference */
			printf(" [i] Joined KrakenNet\n");
			joinConferenceTime = 0;
		}
	}
	else
	{
		if(contactIdMap.find(from) != contactIdMap.end())
		{
			int client = contactIdMap[from];

			contactIdMap.erase(contactIdMap.find(from));
			contactIdMapRev.erase(contactIdMapRev.find(client));
			contactAuthMap.erase(contactAuthMap.find(from));
		}
	}
}
void XMPPServerCore::handleMessageSession( char *from, xmpp_ctx_t *session )
{
	if(!from ||!session)
	{
		return;
	}

	if(contactIdMap.find(from) == contactIdMap.end())
	{
		//printf(" [i] XMPP: New session with: %s\n", from);
		contactIdMapRev[currentId] = from;
		contactIdMap[from] = currentId;
		contactAuthMap[from] = false;

		currentId++;
	}

	if(session != NULL)
	{
		contactSessionMap[from] = session;
	}
}

void XMPPServerCore::asyncHandler()
{
	int nextPing = time(NULL) + 10;

	while(!stopAsyncHandler)
	{
		Sleep(250);

		if(joinConferenceTime != 0 && joinConferenceTime < time(NULL))
		{
			joinConference(serverContext);
			joinConferenceTime = time(NULL) + 20;
		}

		if(presenceAvailable && nextPing < time(NULL))
		{
			nextPing = time(NULL) + 30;
			send_ping(connection, serverContext, username);
		}
	}

	finishedAsyncHandler = true;
}

extern "C" {
	Kraken *kraken_instance;
	char parameters[1024];

	void ConnectionThread(void *arg)
	{
		time_t lastConnect = 0;
		int connectionTries = 0;

		while(true)
		{
			lastConnect = time(NULL);
			Core = new XMPPServerCore(kraken_instance, parameters);			
			Core->start();
			delete Core;

			time_t expired = time(NULL) - lastConnect;

			/* was last connection established longer than 60 seconds? */
			if(expired > 60)
			{
				/* reset try counter */
				connectionTries = 0;
			}

			/* the more tries we start, the longer we have to wait */
			if(connectionTries > 5)
			{
				/* maximum every 60 seconds */
				Sleep(60000);
			}
			else
			{
				/* wait 1, 2, 4, 8, 16, 32 seonds */
				Sleep((1<<connectionTries) * 1000);
				connectionTries++;
			}
		}
	}


	bool EXT_XMPP_API ext_init(Kraken* instance, char *parms)
	{
		kraken_instance = instance;
		strncpy(parameters, parms, 1023);
		_beginthread(ConnectionThread, 0, NULL);

		return true;
	}

}