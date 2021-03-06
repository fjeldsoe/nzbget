/*
 *  This file is part of nzbget
 *
 *  Copyright (C) 2004 Sven Henkel <sidddy@users.sourceforge.net>
 *  Copyright (C) 2007-2015 Andrey Prygunkov <hugbug@users.sourceforge.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * $Revision$
 * $Date$
 *
 */

#include "nzbget.h"
#include "Options.h"
#include "Log.h"
#include "Util.h"

Log* g_Log = NULL;

void Log::Init()
{
	g_Log = new Log();
}

void Log::Final()
{
	delete g_Log;
	g_Log = NULL;
}

Log::Log()
{
	m_messages.clear();
	m_idGen = 0;
	m_optInit = false;
	m_logFilename = NULL;
	m_lastWritten = 0;
#ifdef DEBUG
	m_extraDebug = Util::FileExists("extradebug");
#endif
}

Log::~Log()
{
	Clear();
	free(m_logFilename);
}

void Log::LogDebugInfo()
{
	info("--------------------------------------------");
	info("Dumping debug info to log");
	info("--------------------------------------------");

	m_debugMutex.Lock();
	for (Debuggables::iterator it = m_debuggables.begin(); it != m_debuggables.end(); it++)
	{
		Debuggable* debuggable = *it;
		debuggable->LogDebugInfo();
	}
	m_debugMutex.Unlock();

	info("--------------------------------------------");
}

void Log::Filelog(const char* msg, ...)
{
	if (!m_logFilename)
	{
		return;
	}

	char tmp2[1024];

	va_list ap;
	va_start(ap, msg);
	vsnprintf(tmp2, 1024, msg, ap);
	tmp2[1024-1] = '\0';
	va_end(ap);

	time_t rawtime = time(NULL) + g_Options->GetTimeCorrection();

	char time[50];
#ifdef HAVE_CTIME_R_3
	ctime_r(&rawtime, time, 50);
#else
	ctime_r(&rawtime, time);
#endif
	time[50-1] = '\0';
	time[strlen(time) - 1] = '\0'; // trim LF

	if ((int)rawtime/86400 != (int)m_lastWritten/86400 && g_Options->GetWriteLog() == Options::wlRotate)
	{
		RotateLog();
	}

	m_lastWritten = rawtime;

	FILE* file = fopen(m_logFilename, FOPEN_ABP);
	if (file)
	{
#ifdef WIN32
		uint64 processId = GetCurrentProcessId();
		uint64 threadId = GetCurrentThreadId();
#else
		uint64 processId = (uint64)getpid();
		uint64 threadId = (uint64)pthread_self();
#endif
#ifdef DEBUG
		fprintf(file, "%s\t%llu\t%llu\t%s%s", time, processId, threadId, tmp2, LINE_ENDING);
#else
		fprintf(file, "%s\t%s%s", time, tmp2, LINE_ENDING);
#endif
		fclose(file);
	}
	else
	{
		perror(m_logFilename);
	}
}

#ifdef DEBUG
#undef debug
#ifdef HAVE_VARIADIC_MACROS
void debug(const char* filename, const char* funcname, int lineNr, const char* msg, ...)
#else
void debug(const char* msg, ...)
#endif
{
	char tmp1[1024];

	va_list ap;
	va_start(ap, msg);
	vsnprintf(tmp1, 1024, msg, ap);
	tmp1[1024-1] = '\0';
	va_end(ap);

	char tmp2[1024];
#ifdef HAVE_VARIADIC_MACROS
	if (funcname)
	{
		snprintf(tmp2, 1024, "%s (%s:%i:%s)", tmp1, Util::BaseFileName(filename), lineNr, funcname);
	}
	else
	{
		snprintf(tmp2, 1024, "%s (%s:%i)", tmp1, Util::BaseFileName(filename), lineNr);
	}
#else
	snprintf(tmp2, 1024, "%s", tmp1);
#endif
	tmp2[1024-1] = '\0';

	g_Log->m_logMutex.Lock();

	if (!g_Options && g_Log->m_extraDebug)
	{
		printf("%s\n", tmp2);
	}

	Options::EMessageTarget messageTarget = g_Options ? g_Options->GetDebugTarget() : Options::mtScreen;
	if (messageTarget == Options::mtScreen || messageTarget == Options::mtBoth)
	{
		g_Log->AddMessage(Message::mkDebug, tmp2);
	}
	if (messageTarget == Options::mtLog || messageTarget == Options::mtBoth)
	{
		g_Log->Filelog("DEBUG\t%s", tmp2);
	}

	g_Log->m_logMutex.Unlock();
}
#endif

void error(const char* msg, ...)
{
	char tmp2[1024];

	va_list ap;
	va_start(ap, msg);
	vsnprintf(tmp2, 1024, msg, ap);
	tmp2[1024-1] = '\0';
	va_end(ap);

	g_Log->m_logMutex.Lock();

	Options::EMessageTarget messageTarget = g_Options ? g_Options->GetErrorTarget() : Options::mtBoth;
	if (messageTarget == Options::mtScreen || messageTarget == Options::mtBoth)
	{
		g_Log->AddMessage(Message::mkError, tmp2);
	}
	if (messageTarget == Options::mtLog || messageTarget == Options::mtBoth)
	{
		g_Log->Filelog("ERROR\t%s", tmp2);
	}

	g_Log->m_logMutex.Unlock();
}

void warn(const char* msg, ...)
{
	char tmp2[1024];

	va_list ap;
	va_start(ap, msg);
	vsnprintf(tmp2, 1024, msg, ap);
	tmp2[1024-1] = '\0';
	va_end(ap);

	g_Log->m_logMutex.Lock();

	Options::EMessageTarget messageTarget = g_Options ? g_Options->GetWarningTarget() : Options::mtScreen;
	if (messageTarget == Options::mtScreen || messageTarget == Options::mtBoth)
	{
		g_Log->AddMessage(Message::mkWarning, tmp2);
	}
	if (messageTarget == Options::mtLog || messageTarget == Options::mtBoth)
	{
		g_Log->Filelog("WARNING\t%s", tmp2);
	}

	g_Log->m_logMutex.Unlock();
}

void info(const char* msg, ...)
{
	char tmp2[1024];

	va_list ap;
	va_start(ap, msg);
	vsnprintf(tmp2, 1024, msg, ap);
	tmp2[1024-1] = '\0';
	va_end(ap);

	g_Log->m_logMutex.Lock();

	Options::EMessageTarget messageTarget = g_Options ? g_Options->GetInfoTarget() : Options::mtScreen;
	if (messageTarget == Options::mtScreen || messageTarget == Options::mtBoth)
	{
		g_Log->AddMessage(Message::mkInfo, tmp2);
	}
	if (messageTarget == Options::mtLog || messageTarget == Options::mtBoth)
	{
		g_Log->Filelog("INFO\t%s", tmp2);
	}

	g_Log->m_logMutex.Unlock();
}

void detail(const char* msg, ...)
{
	char tmp2[1024];

	va_list ap;
	va_start(ap, msg);
	vsnprintf(tmp2, 1024, msg, ap);
	tmp2[1024-1] = '\0';
	va_end(ap);

	g_Log->m_logMutex.Lock();

	Options::EMessageTarget messageTarget = g_Options ? g_Options->GetDetailTarget() : Options::mtScreen;
	if (messageTarget == Options::mtScreen || messageTarget == Options::mtBoth)
	{
		g_Log->AddMessage(Message::mkDetail, tmp2);
	}
	if (messageTarget == Options::mtLog || messageTarget == Options::mtBoth)
	{
		g_Log->Filelog("DETAIL\t%s", tmp2);
	}

	g_Log->m_logMutex.Unlock();
}

//************************************************************
// Message

Message::Message(uint32 id, EKind kind, time_t time, const char* text)
{
	m_id = id;
	m_kind = kind;
	m_time = time;
	if (text)
	{
		m_text = strdup(text);
	}
	else
	{
		m_text = NULL;
	}
}

Message::~ Message()
{
	free(m_text);
}

MessageList::~MessageList()
{
	Clear();
}

void MessageList::Clear()
{
	for (iterator it = begin(); it != end(); it++)
	{
		delete *it;
	}
	clear();
}

void Log::Clear()
{
	m_logMutex.Lock();
	m_messages.Clear();
	m_logMutex.Unlock();
}

void Log::AddMessage(Message::EKind kind, const char * text)
{
	Message* message = new Message(++m_idGen, kind, time(NULL), text);
	m_messages.push_back(message);

	if (m_optInit && g_Options)
	{
		while (m_messages.size() > (uint32)g_Options->GetLogBufferSize())
		{
			Message* message = m_messages.front();
			delete message;
			m_messages.pop_front();
		}
	}
}

MessageList* Log::LockMessages()
{
	m_logMutex.Lock();
	return &m_messages;
}

void Log::UnlockMessages()
{
	m_logMutex.Unlock();
}

void Log::ResetLog()
{
	remove(g_Options->GetLogFile());
}

void Log::RotateLog()
{
	char directory[1024];
	strncpy(directory, g_Options->GetLogFile(), 1024);
	directory[1024-1] = '\0';

	// split the full filename into path, basename and extension
	char* baseName = Util::BaseFileName(directory);
	if (baseName > directory)
	{
		baseName[-1] = '\0';
	}

	char baseExt[250];
	char* ext = strrchr(baseName, '.');
	if (ext && ext > baseName)
	{
		strncpy(baseExt, ext, 250);
		baseExt[250-1] = '\0';
		ext[0] = '\0';
	}
	else
	{
		baseExt[0] = '\0';
	}

	char fileMask[1024];
	snprintf(fileMask, 1024, "%s-####-##-##%s", baseName, baseExt);
	fileMask[1024-1] = '\0';

	time_t curTime = time(NULL) + g_Options->GetTimeCorrection();
	int curDay = (int)curTime / 86400;
	char fullFilename[1024];

	WildMask mask(fileMask, true);
	DirBrowser dir(directory);
	while (const char* filename = dir.Next())
	{
		if (mask.Match(filename))
		{
			snprintf(fullFilename, 1024, "%s%c%s", directory, PATH_SEPARATOR, filename);
			fullFilename[1024-1] = '\0';

			struct tm tm;
			memset(&tm, 0, sizeof(tm));
			tm.tm_year = atoi(filename + mask.GetMatchStart(0)) - 1900;
			tm.tm_mon = atoi(filename + mask.GetMatchStart(1)) - 1;
			tm.tm_mday = atoi(filename + mask.GetMatchStart(2));
			time_t fileTime = Util::Timegm(&tm);
			int fileDay = (int)fileTime / 86400;

			if (fileDay <= curDay - g_Options->GetRotateLog())
			{
				char message[1024];
				snprintf(message, 1024, "Deleting old log-file %s\n", filename);
				message[1024-1] = '\0';
				g_Log->AddMessage(Message::mkInfo, message);

				remove(fullFilename);
			}
		}
	}

	struct tm tm;
	gmtime_r(&curTime, &tm);
	snprintf(fullFilename, 1024, "%s%c%s-%i-%.2i-%.2i%s", directory, PATH_SEPARATOR,
		baseName, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, baseExt);
	fullFilename[1024-1] = '\0';

	free(m_logFilename);
	m_logFilename = strdup(fullFilename);
#ifdef WIN32
	WebUtil::Utf8ToAnsi(m_logFilename, strlen(m_logFilename) + 1);
#endif
}

/*
* During intializing stage (when options were not read yet) all messages
* are saved in screen log, even if they shouldn't (according to options).
* Method "InitOptions()" check all messages added to screen log during
* intializing stage and does three things:
* 1) save the messages to log-file (if they should according to options);
* 2) delete messages from screen log (if they should not be saved in screen log).
* 3) renumerate IDs
*/
void Log::InitOptions()
{
	const char* messageType[] = { "INFO", "WARNING", "ERROR", "DEBUG", "DETAIL"};

	if (g_Options->GetWriteLog() != Options::wlNone && g_Options->GetLogFile())
	{
		m_logFilename = strdup(g_Options->GetLogFile());
#ifdef WIN32
		WebUtil::Utf8ToAnsi(m_logFilename, strlen(m_logFilename) + 1);
#endif

		if (g_Options->GetServerMode() && g_Options->GetWriteLog() == Options::wlReset)
		{
			g_Log->ResetLog();
		}
	}

	m_idGen = 0;

	for (uint32 i = 0; i < m_messages.size(); )
	{
		Message* message = m_messages.at(i);
		Options::EMessageTarget target = Options::mtNone;
		switch (message->GetKind())
		{
			case Message::mkDebug:
				target = g_Options->GetDebugTarget();
				break;
			case Message::mkDetail:
				target = g_Options->GetDetailTarget();
				break;
			case Message::mkInfo:
				target = g_Options->GetInfoTarget();
				break;
			case Message::mkWarning:
				target = g_Options->GetWarningTarget();
				break;
			case Message::mkError:
				target = g_Options->GetErrorTarget();
				break;
		}

		if (target == Options::mtLog || target == Options::mtBoth)
		{
			Filelog("%s\t%s", messageType[message->GetKind()], message->GetText());
		}

		if (target == Options::mtLog || target == Options::mtNone)
		{
			delete message;
			m_messages.erase(m_messages.begin() + i);
		}
		else
		{
			message->m_id = ++m_idGen;
			i++;
		}
	}

	m_optInit = true;
}

void Log::RegisterDebuggable(Debuggable* debuggable)
{
	m_debugMutex.Lock();
	m_debuggables.push_back(debuggable);
	m_debugMutex.Unlock();
}

void Log::UnregisterDebuggable(Debuggable* debuggable)
{
	m_debugMutex.Lock();
	m_debuggables.remove(debuggable);
	m_debugMutex.Unlock();
}
