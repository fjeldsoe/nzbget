/*
 *  This file is part of nzbget
 *
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
#include "QueueScript.h"
#include "NzbScript.h"
#include "Options.h"
#include "Log.h"
#include "Util.h"

static const char* QUEUE_EVENT_NAMES[] = { "FILE_DOWNLOADED", "URL_COMPLETED", "NZB_ADDED", "NZB_DOWNLOADED", "NZB_DELETED" };

class QueueScriptController : public Thread, public NzbScriptController
{
private:
	char*				m_nzbName;
	char*				m_nzbFilename;
	char*				m_url;
	char*				m_category;
	char*				m_destDir;
	int					m_id;
	int					m_priority;
	char*				m_dupeKey;
	EDupeMode			m_dupeMode;
	int					m_dupeScore;
	NzbParameterList	m_parameters;
	int					m_prefixLen;
	ScriptConfig::Script*	m_script;
	QueueScriptCoordinator::EEvent	m_event;
	bool				m_markBad;
	NzbInfo::EDeleteStatus m_deleteStatus;
	NzbInfo::EUrlStatus	m_urlStatus;

	void				PrepareParams(const char* scriptName);

protected:
	virtual void		ExecuteScript(ScriptConfig::Script* script);
	virtual void		AddMessage(Message::EKind kind, const char* text);

public:
	virtual				~QueueScriptController();
	virtual void		Run();
	static void			StartScript(NzbInfo* nzbInfo, ScriptConfig::Script* script, QueueScriptCoordinator::EEvent event);
};


QueueScriptController::~QueueScriptController()
{
	free(m_nzbName);
	free(m_nzbFilename);
	free(m_url);
	free(m_category);
	free(m_destDir);
	free(m_dupeKey);
}

void QueueScriptController::StartScript(NzbInfo* nzbInfo, ScriptConfig::Script* script, QueueScriptCoordinator::EEvent event)
{
	QueueScriptController* scriptController = new QueueScriptController();

	scriptController->m_nzbName = strdup(nzbInfo->GetName());
	scriptController->m_nzbFilename = strdup(nzbInfo->GetFilename());
	scriptController->m_url = strdup(nzbInfo->GetUrl());
	scriptController->m_category = strdup(nzbInfo->GetCategory());
	scriptController->m_destDir = strdup(nzbInfo->GetDestDir());
	scriptController->m_id = nzbInfo->GetId();
	scriptController->m_priority = nzbInfo->GetPriority();
	scriptController->m_dupeKey = strdup(nzbInfo->GetDupeKey());
	scriptController->m_dupeMode = nzbInfo->GetDupeMode();
	scriptController->m_dupeScore = nzbInfo->GetDupeScore();
	scriptController->m_parameters.CopyFrom(nzbInfo->GetParameters());
	scriptController->m_script = script;
	scriptController->m_event = event;
	scriptController->m_prefixLen = 0;
	scriptController->m_markBad = false;
	scriptController->m_deleteStatus = nzbInfo->GetDeleteStatus();
	scriptController->m_urlStatus = nzbInfo->GetUrlStatus();
	scriptController->SetAutoDestroy(true);

	scriptController->Start();
}

void QueueScriptController::Run()
{
	ExecuteScript(m_script);

	SetLogPrefix(NULL);

	if (m_markBad)
	{
		DownloadQueue* downloadQueue = DownloadQueue::Lock();
		NzbInfo* nzbInfo = downloadQueue->GetQueue()->Find(m_id);
		if (nzbInfo)
		{
			PrintMessage(Message::mkWarning, "Cancelling download and deleting %s", m_nzbName);
			nzbInfo->SetDeleteStatus(NzbInfo::dsBad);
			downloadQueue->EditEntry(m_id, DownloadQueue::eaGroupDelete, 0, NULL);
		}
		DownloadQueue::Unlock();
	}

	g_QueueScriptCoordinator->CheckQueue();
}

void QueueScriptController::ExecuteScript(ScriptConfig::Script* script)
{
	PrintMessage(m_event == QueueScriptCoordinator::qeFileDownloaded ? Message::mkDetail : Message::mkInfo,
		"Executing queue-script %s for %s", script->GetName(), Util::BaseFileName(m_nzbName));

	SetScript(script->GetLocation());
	SetArgs(NULL, false);

	char infoName[1024];
	snprintf(infoName, 1024, "queue-script %s for %s", script->GetName(), Util::BaseFileName(m_nzbName));
	infoName[1024-1] = '\0';
	SetInfoName(infoName);

	SetLogPrefix(script->GetDisplayName());
	m_prefixLen = strlen(script->GetDisplayName()) + 2; // 2 = strlen(": ");
	PrepareParams(script->GetName());

	Execute();

	SetLogPrefix(NULL);
}

void QueueScriptController::PrepareParams(const char* scriptName)
{
	ResetEnv();

	SetEnvVar("NZBNA_NZBNAME", m_nzbName);
	SetIntEnvVar("NZBNA_NZBID", m_id);
	SetEnvVar("NZBNA_FILENAME", m_nzbFilename);
	SetEnvVar("NZBNA_DIRECTORY", m_destDir);
	SetEnvVar("NZBNA_URL", m_url);
	SetEnvVar("NZBNA_CATEGORY", m_category);
	SetIntEnvVar("NZBNA_PRIORITY", m_priority);
	SetIntEnvVar("NZBNA_LASTID", m_id);	// deprecated

	SetEnvVar("NZBNA_DUPEKEY", m_dupeKey);
	SetIntEnvVar("NZBNA_DUPESCORE", m_dupeScore);

	const char* dupeModeName[] = { "SCORE", "ALL", "FORCE" };
	SetEnvVar("NZBNA_DUPEMODE", dupeModeName[m_dupeMode]);

	SetEnvVar("NZBNA_EVENT", QUEUE_EVENT_NAMES[m_event]);

	const char* deleteStatusName[] = { "NONE", "MANUAL", "HEALTH", "DUPE", "BAD", "GOOD", "COPY", "SCAN" };
	SetEnvVar("NZBNA_DELETESTATUS", deleteStatusName[m_deleteStatus]);

	const char* urlStatusName[] = { "NONE", "UNKNOWN", "SUCCESS", "FAILURE", "UNKNOWN", "SCAN_SKIPPED", "SCAN_FAILURE" };
	SetEnvVar("NZBNA_URLSTATUS", urlStatusName[m_urlStatus]);

	PrepareEnvScript(&m_parameters, scriptName);
}

void QueueScriptController::AddMessage(Message::EKind kind, const char* text)
{
	const char* msgText = text + m_prefixLen;

	if (!strncmp(msgText, "[NZB] ", 6))
	{
		debug("Command %s detected", msgText + 6);
		if (!strncmp(msgText + 6, "NZBPR_", 6))
		{
			char* param = strdup(msgText + 6 + 6);
			char* value = strchr(param, '=');
			if (value)
			{
				*value = '\0';
				DownloadQueue* downloadQueue = DownloadQueue::Lock();
				NzbInfo* nzbInfo = downloadQueue->GetQueue()->Find(m_id);
				if (nzbInfo)
				{
					nzbInfo->GetParameters()->SetParameter(param, value + 1);
				}
				DownloadQueue::Unlock();
			}
			else
			{
				error("Invalid command \"%s\" received from %s", msgText, GetInfoName());
			}
			free(param);
		}
		else if (!strncmp(msgText + 6, "MARK=BAD", 8))
		{
			m_markBad = true;
			DownloadQueue* downloadQueue = DownloadQueue::Lock();
			NzbInfo* nzbInfo = downloadQueue->GetQueue()->Find(m_id);
			if (nzbInfo)
			{
				SetLogPrefix(NULL);
				PrintMessage(Message::mkWarning, "Marking %s as bad", m_nzbName);
				SetLogPrefix(m_script->GetDisplayName());
				nzbInfo->SetMarkStatus(NzbInfo::ksBad);
			}
			DownloadQueue::Unlock();
		}
		else
		{
			error("Invalid command \"%s\" received from %s", msgText, GetInfoName());
		}
	}
	else
	{
		ScriptController::AddMessage(kind, text);
	}
}


QueueScriptCoordinator::QueueItem::QueueItem(int nzbId, ScriptConfig::Script* script, EEvent event)
{
	m_nzbId = nzbId;
	m_script = script;
	m_event = event;
}

QueueScriptCoordinator::QueueScriptCoordinator()
{
	m_curItem = NULL;
	m_stopped = false;
}

QueueScriptCoordinator::~QueueScriptCoordinator()
{
	delete m_curItem;
	for (Queue::iterator it = m_queue.begin(); it != m_queue.end(); it++ )
	{
		delete *it;
	}
}

void QueueScriptCoordinator::InitOptions()
{
	m_hasQueueScripts = false;
	for (ScriptConfig::Scripts::iterator it = g_ScriptConfig->GetScripts()->begin(); it != g_ScriptConfig->GetScripts()->end(); it++)
	{
		ScriptConfig::Script* script = *it;
		if (script->GetQueueScript())
		{
			m_hasQueueScripts = true;
			break;
		}
	}
}

void QueueScriptCoordinator::EnqueueScript(NzbInfo* nzbInfo, EEvent event)
{
	if (!m_hasQueueScripts)
	{
		return;
	}

	m_queueMutex.Lock();

	if (event == qeNzbDownloaded)
	{
		// delete all other queued scripts for this nzb
		for (Queue::iterator it = m_queue.begin(); it != m_queue.end(); )
		{
			QueueItem* queueItem = *it;
			if (queueItem->GetNzbId() == nzbInfo->GetId())
			{
				delete queueItem;
				it = m_queue.erase(it);
				continue;
			}
			it++;
		}
	}

	// respect option "EventInterval"
	time_t curTime = time(NULL);
	if (event == qeFileDownloaded &&
		(g_Options->GetEventInterval() == -1 ||
		 (g_Options->GetEventInterval() > 0 && curTime - nzbInfo->GetQueueScriptTime() > 0 &&
		 (int)(curTime - nzbInfo->GetQueueScriptTime()) < g_Options->GetEventInterval())))
	{
		m_queueMutex.Unlock();
		return;
	}

	for (ScriptConfig::Scripts::iterator it = g_ScriptConfig->GetScripts()->begin(); it != g_ScriptConfig->GetScripts()->end(); it++)
	{
		ScriptConfig::Script* script = *it;

		if (!script->GetQueueScript())
		{
			continue;
		}

		bool useScript = false;

		// check queue-scripts
		const char* queueScript = g_Options->GetQueueScript();
		if (!Util::EmptyStr(queueScript))
		{
			// split szQueueScript into tokens
			Tokenizer tok(queueScript, ",;");
			while (const char* scriptName = tok.Next())
			{
				if (Util::SameFilename(scriptName, script->GetName()))
				{
					useScript = true;
					break;
				}
			}
		}

		// check post-processing-scripts
		if (!useScript)
		{
			for (NzbParameterList::iterator it = nzbInfo->GetParameters()->begin(); it != nzbInfo->GetParameters()->end(); it++)
			{
				NzbParameter* parameter = *it;
				const char* varname = parameter->GetName();
				if (strlen(varname) > 0 && varname[0] != '*' && varname[strlen(varname)-1] == ':' &&
					(!strcasecmp(parameter->GetValue(), "yes") ||
					 !strcasecmp(parameter->GetValue(), "on") ||
					 !strcasecmp(parameter->GetValue(), "1")))
				{
					char scriptName[1024];
					strncpy(scriptName, varname, 1024);
					scriptName[1024-1] = '\0';
					scriptName[strlen(scriptName)-1] = '\0'; // remove trailing ':'
					if (Util::SameFilename(scriptName, script->GetName()))
					{
						useScript = true;
						break;
					}
				}
			}
		}

		useScript &= Util::EmptyStr(script->GetQueueEvents()) || strstr(script->GetQueueEvents(), QUEUE_EVENT_NAMES[event]);

		if (useScript)
		{
			bool alreadyQueued = false;
			if (event == qeFileDownloaded)
			{
				// check if this script is already queued for this nzb
				for (Queue::iterator it2 = m_queue.begin(); it2 != m_queue.end(); it2++)
				{
					QueueItem* queueItem = *it2;
					if (queueItem->GetNzbId() == nzbInfo->GetId() && queueItem->GetScript() == script)
					{
						alreadyQueued = true;
						break;
					}
				}
			}

			if (!alreadyQueued)
			{
				QueueItem* queueItem = new QueueItem(nzbInfo->GetId(), script, event);
				if (m_curItem)
				{
					m_queue.push_back(queueItem);
				}
				else
				{
					StartScript(nzbInfo, queueItem);
				}
			}

			nzbInfo->SetQueueScriptTime(time(NULL));
		}
	}

	m_queueMutex.Unlock();
}

NzbInfo* QueueScriptCoordinator::FindNzbInfo(DownloadQueue* downloadQueue, int nzbId)
{
	NzbInfo* nzbInfo = downloadQueue->GetQueue()->Find(nzbId);
	if (!nzbInfo)
	{
		for (HistoryList::iterator it = downloadQueue->GetHistory()->begin(); it != downloadQueue->GetHistory()->end(); it++)
		{
			HistoryInfo* historyInfo = *it;
			if (historyInfo->GetNzbInfo() && historyInfo->GetNzbInfo()->GetId() == nzbId)
			{
				nzbInfo = historyInfo->GetNzbInfo();
				break;
			}
		}
	}
	return nzbInfo;
}

void QueueScriptCoordinator::CheckQueue()
{
	if (m_stopped)
	{
		return;
	}

	DownloadQueue* downloadQueue = DownloadQueue::Lock();
	m_queueMutex.Lock();

	delete m_curItem;

	m_curItem = NULL;
	NzbInfo* curNzbInfo = NULL;
	Queue::iterator itCurItem = m_queue.end();

	for (Queue::iterator it = m_queue.begin(); it != m_queue.end(); )
	{
		QueueItem* queueItem = *it;

		NzbInfo* nzbInfo = FindNzbInfo(downloadQueue, queueItem->GetNzbId());

		// in a case this nzb must not be processed further - delete queue script from queue
		if (!nzbInfo ||
			(nzbInfo->GetDeleteStatus() != NzbInfo::dsNone && queueItem->GetEvent() != qeNzbDeleted) ||
			nzbInfo->GetMarkStatus() == NzbInfo::ksBad)
		{
			delete queueItem;
			it = m_queue.erase(it);
			continue;
		}

		if (!m_curItem || queueItem->GetEvent() > m_curItem->GetEvent())
		{
			m_curItem = queueItem;
			itCurItem = it;
			curNzbInfo = nzbInfo;
		}

		it++;
	}

	if (m_curItem)
	{
		m_queue.erase(itCurItem);
		StartScript(curNzbInfo, m_curItem);
	}

	m_queueMutex.Unlock();
	DownloadQueue::Unlock();
}

void QueueScriptCoordinator::StartScript(NzbInfo* nzbInfo, QueueItem* queueItem)
{
	m_curItem = queueItem;
	QueueScriptController::StartScript(nzbInfo, queueItem->GetScript(), queueItem->GetEvent());
}

bool QueueScriptCoordinator::HasJob(int nzbId, bool* active)
{
	m_queueMutex.Lock();
	bool working = m_curItem && m_curItem->GetNzbId() == nzbId;
	if (active)
	{
		*active = working;
	}
	if (!working)
	{
		for (Queue::iterator it = m_queue.begin(); it != m_queue.end(); it++)
		{
			QueueItem* queueItem = *it;
			working = queueItem->GetNzbId() == nzbId;
			if (working)
			{
				break;
			}
		}
	}
	m_queueMutex.Unlock();

	return working;
}

int QueueScriptCoordinator::GetQueueSize()
{
	m_queueMutex.Lock();
	int queuedCount = m_queue.size();
	if (m_curItem)
	{
		queuedCount++;
	}
	m_queueMutex.Unlock();

	return queuedCount;
}
