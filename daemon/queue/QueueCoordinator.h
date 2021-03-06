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


#ifndef QUEUECOORDINATOR_H
#define QUEUECOORDINATOR_H

#include "Log.h"
#include "Thread.h"
#include "NzbFile.h"
#include "ArticleDownloader.h"
#include "DownloadInfo.h"
#include "Observer.h"
#include "QueueEditor.h"
#include "NntpConnection.h"

class QueueCoordinator : public Thread, public Observer, public Debuggable
{
public:
	typedef std::list<ArticleDownloader*>	ActiveDownloads;

private:
	class CoordinatorDownloadQueue : public DownloadQueue
	{
	private:
		QueueCoordinator*	m_owner;
		bool				m_massEdit;
		bool				m_wantSave;
		friend class QueueCoordinator;
	public:
							CoordinatorDownloadQueue(): m_massEdit(false), m_wantSave(false) {}
		virtual bool		EditEntry(int ID, EEditAction action, int offset, const char* text);
		virtual bool		EditList(IdList* idList, NameList* nameList, EMatchMode matchMode, EEditAction action, int offset, const char* text);
		virtual void		Save();
	};

private:
	CoordinatorDownloadQueue	m_downloadQueue;
	ActiveDownloads				m_activeDownloads;
	QueueEditor					m_queueEditor;
	bool						m_hasMoreJobs;
	int							m_downloadsLimit;
	int							m_serverConfigGeneration;

	bool					GetNextArticle(DownloadQueue* downloadQueue, FileInfo* &fileInfo, ArticleInfo* &articleInfo);
	void					StartArticleDownload(FileInfo* fileInfo, ArticleInfo* articleInfo, NntpConnection* connection);
	void					ArticleCompleted(ArticleDownloader* articleDownloader);
	void					DeleteFileInfo(DownloadQueue* downloadQueue, FileInfo* fileInfo, bool completed);
	void					StatFileInfo(FileInfo* fileInfo, bool completed);
	void					CheckHealth(DownloadQueue* downloadQueue, FileInfo* fileInfo);
	void					ResetHangingDownloads();
	void					AdjustDownloadsLimit();
	void					Load();
	void					SavePartialState();

protected:
	virtual void			LogDebugInfo();

public:
							QueueCoordinator();
	virtual					~QueueCoordinator();
	virtual void			Run();
	virtual void 			Stop();
	void					Update(Subject* Caller, void* Aspect);

	// editing queue
	void					AddNzbFileToQueue(NzbFile* nzbFile, NzbInfo* urlInfo, bool addFirst);
	void					CheckDupeFileInfos(NzbInfo* nzbInfo);
	bool					HasMoreJobs() { return m_hasMoreJobs; }
	void					DiscardDiskFile(FileInfo* fileInfo);
	bool					DeleteQueueEntry(DownloadQueue* downloadQueue, FileInfo* fileInfo);
	bool					SetQueueEntryCategory(DownloadQueue* downloadQueue, NzbInfo* nzbInfo, const char* category);
	bool					SetQueueEntryName(DownloadQueue* downloadQueue, NzbInfo* nzbInfo, const char* name);
	bool					MergeQueueEntries(DownloadQueue* downloadQueue, NzbInfo* destNzbInfo, NzbInfo* srcNzbInfo);
	bool					SplitQueueEntries(DownloadQueue* downloadQueue, FileList* fileList, const char* name, NzbInfo** newNzbInfo);
};

extern QueueCoordinator* g_QueueCoordinator;

#endif
