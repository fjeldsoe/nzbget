/*
 *  This file is part of nzbget
 *
 *  Copyright (C) 2013-2015 Andrey Prygunkov <hugbug@users.sourceforge.net>
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
#include "FeedFile.h"
#include "Log.h"
#include "DownloadInfo.h"
#include "Options.h"
#include "Util.h"

FeedFile::FeedFile(const char* fileName)
{
	debug("Creating FeedFile");

	m_fileName = strdup(fileName);
	m_feedItemInfos = new FeedItemInfos();
	m_feedItemInfos->Retain();

#ifndef WIN32
	m_feedItemInfo = NULL;
	m_tagContent = NULL;
	m_tagContentLen = 0;
#endif
}

FeedFile::~FeedFile()
{
	debug("Destroying FeedFile");

	// Cleanup
	free(m_fileName);
	m_feedItemInfos->Release();

#ifndef WIN32
	delete m_feedItemInfo;
	free(m_tagContent);
#endif
}

void FeedFile::LogDebugInfo()
{
	info(" FeedFile %s", m_fileName);
}

void FeedFile::AddItem(FeedItemInfo* feedItemInfo)
{
	m_feedItemInfos->Add(feedItemInfo);
}

void FeedFile::ParseSubject(FeedItemInfo* feedItemInfo)
{
	// if title has quatation marks we use only part within quatation marks
	char* p = (char*)feedItemInfo->GetTitle();
	char* start = strchr(p, '\"');
	if (start)
	{
		start++;
		char* end = strchr(start + 1, '\"');
		if (end)
		{
			int len = (int)(end - start);
			char* point = strchr(start + 1, '.');
			if (point && point < end)
			{
				char* filename = (char*)malloc(len + 1);
				strncpy(filename, start, len);
				filename[len] = '\0';

				char* ext = strrchr(filename, '.');
				if (ext && !strcasecmp(ext, ".par2"))
				{
					*ext = '\0';
				}

				feedItemInfo->SetFilename(filename);
				free(filename);
				return;
			}
		}
	}

	feedItemInfo->SetFilename(feedItemInfo->GetTitle());
}

#ifdef WIN32
FeedFile* FeedFile::Create(const char* fileName)
{
	CoInitialize(NULL);

	HRESULT hr;

	MSXML::IXMLDOMDocumentPtr doc;
	hr = doc.CreateInstance(MSXML::CLSID_DOMDocument);
	if (FAILED(hr))
	{
		return NULL;
	}

	// Load the XML document file...
	doc->put_resolveExternals(VARIANT_FALSE);
	doc->put_validateOnParse(VARIANT_FALSE);
	doc->put_async(VARIANT_FALSE);

	// filename needs to be properly encoded
	char* url = (char*)malloc(strlen(fileName)*3 + 1);
	EncodeUrl(fileName, url);
	debug("url=\"%s\"", url);
	_variant_t v(url);
	free(url);

	VARIANT_BOOL success = doc->load(v);
	if (success == VARIANT_FALSE)
	{
		_bstr_t r(doc->GetparseError()->reason);
		const char* errMsg = r;
		error("Error parsing rss feed: %s", errMsg);
		return NULL;
	}

	FeedFile* file = new FeedFile(fileName);
	if (!file->ParseFeed(doc))
	{
		delete file;
		file = NULL;
	}

	return file;
}

void FeedFile::EncodeUrl(const char* filename, char* url)
{
	while (char ch = *filename++)
	{
		if (('0' <= ch && ch <= '9') ||
			('a' <= ch && ch <= 'z') ||
			('A' <= ch && ch <= 'Z') )
		{
			*url++ = ch;
		}
		else
		{
			*url++ = '%';
			int a = ch >> 4;
			*url++ = a > 9 ? a - 10 + 'a' : a + '0';
			a = ch & 0xF;
			*url++ = a > 9 ? a - 10 + 'a' : a + '0';
		}
	}
	*url = NULL;
}

bool FeedFile::ParseFeed(IUnknown* nzb)
{
	MSXML::IXMLDOMDocumentPtr doc = nzb;
	MSXML::IXMLDOMNodePtr root = doc->documentElement;

	MSXML::IXMLDOMNodeListPtr itemList = root->selectNodes("/rss/channel/item");
	for (int i = 0; i < itemList->Getlength(); i++)
	{
		MSXML::IXMLDOMNodePtr node = itemList->Getitem(i);

		FeedItemInfo* feedItemInfo = new FeedItemInfo();
		AddItem(feedItemInfo);

		MSXML::IXMLDOMNodePtr tag;
		MSXML::IXMLDOMNodePtr attr;

		// <title>Debian 6</title>
		tag = node->selectSingleNode("title");
		if (!tag)
		{
			// bad rss feed
			return false;
		}
		_bstr_t title(tag->Gettext());
		feedItemInfo->SetTitle(title);
		ParseSubject(feedItemInfo);

		// <pubDate>Wed, 26 Jun 2013 00:02:54 -0600</pubDate>
		tag = node->selectSingleNode("pubDate");
		if (tag)
		{
			_bstr_t time(tag->Gettext());
			time_t unixtime = WebUtil::ParseRfc822DateTime(time);
			if (unixtime > 0)
			{
				feedItemInfo->SetTime(unixtime);
			}
		}

		// <category>Movies &gt; HD</category>
		tag = node->selectSingleNode("category");
		if (tag)
		{
			_bstr_t category(tag->Gettext());
			feedItemInfo->SetCategory(category);
		}

		// <description>long text</description>
		tag = node->selectSingleNode("description");
		if (tag)
		{
			_bstr_t bdescription(tag->Gettext());
			// cleanup CDATA
			char* description = strdup((const char*)bdescription);
			WebUtil::XmlStripTags(description);
			WebUtil::XmlDecode(description);
			WebUtil::XmlRemoveEntities(description);
			feedItemInfo->SetDescription(description);
			free(description);
		}

		//<enclosure url="http://myindexer.com/fetch/9eeb264aecce961a6e0d" length="150263340" type="application/x-nzb" />
		tag = node->selectSingleNode("enclosure");
		if (tag)
		{
			attr = tag->Getattributes()->getNamedItem("url");
			if (attr)
			{
				_bstr_t url(attr->Gettext());
				feedItemInfo->SetUrl(url);
			}

			attr = tag->Getattributes()->getNamedItem("length");
			if (attr)
			{
				_bstr_t bsize(attr->Gettext());
				int64 size = atoll(bsize);
				feedItemInfo->SetSize(size);
			}
		}

		if (!feedItemInfo->GetUrl())
		{
			// <link>https://nzb.org/fetch/334534ce/4364564564</link>
			tag = node->selectSingleNode("link");
			if (!tag)
			{
				// bad rss feed
				return false;
			}
			_bstr_t link(tag->Gettext());
			feedItemInfo->SetUrl(link);
		}


		// newznab special

		//<newznab:attr name="size" value="5423523453534" />
		if (feedItemInfo->GetSize() == 0)
		{
			tag = node->selectSingleNode("newznab:attr[@name='size']");
			if (tag)
			{
				attr = tag->Getattributes()->getNamedItem("value");
				if (attr)
				{
					_bstr_t bsize(attr->Gettext());
					int64 size = atoll(bsize);
					feedItemInfo->SetSize(size);
				}
			}
		}

		//<newznab:attr name="imdb" value="1588173"/>
		tag = node->selectSingleNode("newznab:attr[@name='imdb']");
		if (tag)
		{
			attr = tag->Getattributes()->getNamedItem("value");
			if (attr)
			{
				_bstr_t bval(attr->Gettext());
				int val = atoi(bval);
				feedItemInfo->SetImdbId(val);
			}
		}

		//<newznab:attr name="rageid" value="33877"/>
		tag = node->selectSingleNode("newznab:attr[@name='rageid']");
		if (tag)
		{
			attr = tag->Getattributes()->getNamedItem("value");
			if (attr)
			{
				_bstr_t bval(attr->Gettext());
				int val = atoi(bval);
				feedItemInfo->SetRageId(val);
			}
		}

		//<newznab:attr name="episode" value="E09"/>
		//<newznab:attr name="episode" value="9"/>
		tag = node->selectSingleNode("newznab:attr[@name='episode']");
		if (tag)
		{
			attr = tag->Getattributes()->getNamedItem("value");
			if (attr)
			{
				_bstr_t val(attr->Gettext());
				feedItemInfo->SetEpisode(val);
			}
		}

		//<newznab:attr name="season" value="S03"/>
		//<newznab:attr name="season" value="3"/>
		tag = node->selectSingleNode("newznab:attr[@name='season']");
		if (tag)
		{
			attr = tag->Getattributes()->getNamedItem("value");
			if (attr)
			{
				_bstr_t val(attr->Gettext());
				feedItemInfo->SetSeason(val);
			}
		}

		MSXML::IXMLDOMNodeListPtr itemList = node->selectNodes("newznab:attr");
		for (int i = 0; i < itemList->Getlength(); i++)
		{
			MSXML::IXMLDOMNodePtr node = itemList->Getitem(i);
			MSXML::IXMLDOMNodePtr name = node->Getattributes()->getNamedItem("name");
			MSXML::IXMLDOMNodePtr value = node->Getattributes()->getNamedItem("value");
			if (name && value)
			{
				_bstr_t bname(name->Gettext());
				_bstr_t bval(value->Gettext());
				feedItemInfo->GetAttributes()->Add(bname, bval);
			}
		}
	}
	return true;
}

#else

FeedFile* FeedFile::Create(const char* fileName)
{
	FeedFile* file = new FeedFile(fileName);

	xmlSAXHandler SAX_handler = {0};
	SAX_handler.startElement = reinterpret_cast<startElementSAXFunc>(SAX_StartElement);
	SAX_handler.endElement = reinterpret_cast<endElementSAXFunc>(SAX_EndElement);
	SAX_handler.characters = reinterpret_cast<charactersSAXFunc>(SAX_characters);
	SAX_handler.error = reinterpret_cast<errorSAXFunc>(SAX_error);
	SAX_handler.getEntity = reinterpret_cast<getEntitySAXFunc>(SAX_getEntity);

	file->m_ignoreNextError = false;

	int ret = xmlSAXUserParseFile(&SAX_handler, file, fileName);

	if (ret != 0)
	{
		error("Failed to parse rss feed");
		delete file;
		file = NULL;
	}

	return file;
}

void FeedFile::Parse_StartElement(const char *name, const char **atts)
{
	ResetTagContent();

	if (!strcmp("item", name))
	{
		delete m_feedItemInfo;
		m_feedItemInfo = new FeedItemInfo();
	}
	else if (!strcmp("enclosure", name) && m_feedItemInfo)
	{
		//<enclosure url="http://myindexer.com/fetch/9eeb264aecce961a6e0d" length="150263340" type="application/x-nzb" />
		for (; *atts; atts+=2)
		{
			if (!strcmp("url", atts[0]))
			{
				char* url = strdup(atts[1]);
				WebUtil::XmlDecode(url);
				m_feedItemInfo->SetUrl(url);
				free(url);
			}
			else if (!strcmp("length", atts[0]))
			{
				int64 size = atoll(atts[1]);
				m_feedItemInfo->SetSize(size);
			}
		}
	}
	else if (m_feedItemInfo && !strcmp("newznab:attr", name) &&
		atts[0] && atts[1] && atts[2] && atts[3] &&
		!strcmp("name", atts[0]) && !strcmp("value", atts[2]))
	{
		m_feedItemInfo->GetAttributes()->Add(atts[1], atts[3]);

		//<newznab:attr name="size" value="5423523453534" />
		if (m_feedItemInfo->GetSize() == 0 &&
			!strcmp("size", atts[1]))
		{
			int64 size = atoll(atts[3]);
			m_feedItemInfo->SetSize(size);
		}

		//<newznab:attr name="imdb" value="1588173"/>
		else if (!strcmp("imdb", atts[1]))
		{
			m_feedItemInfo->SetImdbId(atoi(atts[3]));
		}

		//<newznab:attr name="rageid" value="33877"/>
		else if (!strcmp("rageid", atts[1]))
		{
			m_feedItemInfo->SetRageId(atoi(atts[3]));
		}

		//<newznab:attr name="episode" value="E09"/>
		//<newznab:attr name="episode" value="9"/>
		else if (!strcmp("episode", atts[1]))
		{
			m_feedItemInfo->SetEpisode(atts[3]);
		}

		//<newznab:attr name="season" value="S03"/>
		//<newznab:attr name="season" value="3"/>
		else if (!strcmp("season", atts[1]))
		{
			m_feedItemInfo->SetSeason(atts[3]);
		}
	}
}

void FeedFile::Parse_EndElement(const char *name)
{
	if (!strcmp("item", name))
	{
		// Close the file element, add the new file to file-list
		AddItem(m_feedItemInfo);
		m_feedItemInfo = NULL;
	}
	else if (!strcmp("title", name) && m_feedItemInfo)
	{
		m_feedItemInfo->SetTitle(m_tagContent);
		ResetTagContent();
		ParseSubject(m_feedItemInfo);
	}
	else if (!strcmp("link", name) && m_feedItemInfo &&
		(!m_feedItemInfo->GetUrl() || strlen(m_feedItemInfo->GetUrl()) == 0))
	{
		m_feedItemInfo->SetUrl(m_tagContent);
		ResetTagContent();
	}
	else if (!strcmp("category", name) && m_feedItemInfo)
	{
		m_feedItemInfo->SetCategory(m_tagContent);
		ResetTagContent();
	}
	else if (!strcmp("description", name) && m_feedItemInfo)
	{
		// cleanup CDATA
		char* description = strdup(m_tagContent);
		WebUtil::XmlStripTags(description);
		WebUtil::XmlDecode(description);
		WebUtil::XmlRemoveEntities(description);
		m_feedItemInfo->SetDescription(description);
		free(description);
		ResetTagContent();
	}
	else if (!strcmp("pubDate", name) && m_feedItemInfo)
	{
		time_t unixtime = WebUtil::ParseRfc822DateTime(m_tagContent);
		if (unixtime > 0)
		{
			m_feedItemInfo->SetTime(unixtime);
		}
		ResetTagContent();
	}
}

void FeedFile::Parse_Content(const char *buf, int len)
{
	m_tagContent = (char*)realloc(m_tagContent, m_tagContentLen + len + 1);
	strncpy(m_tagContent + m_tagContentLen, buf, len);
	m_tagContentLen += len;
	m_tagContent[m_tagContentLen] = '\0';
}

void FeedFile::ResetTagContent()
{
	free(m_tagContent);
	m_tagContent = NULL;
	m_tagContentLen = 0;
}

void FeedFile::SAX_StartElement(FeedFile* file, const char *name, const char **atts)
{
	file->Parse_StartElement(name, atts);
}

void FeedFile::SAX_EndElement(FeedFile* file, const char *name)
{
	file->Parse_EndElement(name);
}

void FeedFile::SAX_characters(FeedFile* file, const char * xmlstr, int len)
{
	char* str = (char*)xmlstr;

	// trim starting blanks
	int off = 0;
	for (int i = 0; i < len; i++)
	{
		char ch = str[i];
		if (ch == ' ' || ch == 10 || ch == 13 || ch == 9)
		{
			off++;
		}
		else
		{
			break;
		}
	}

	int newlen = len - off;

	// trim ending blanks
	for (int i = len - 1; i >= off; i--)
	{
		char ch = str[i];
		if (ch == ' ' || ch == 10 || ch == 13 || ch == 9)
		{
			newlen--;
		}
		else
		{
			break;
		}
	}

	if (newlen > 0)
	{
		// interpret tag content
		file->Parse_Content(str + off, newlen);
	}
}

void* FeedFile::SAX_getEntity(FeedFile* file, const char * name)
{
	xmlEntityPtr e = xmlGetPredefinedEntity((xmlChar* )name);
	if (!e)
	{
		warn("entity not found");
		file->m_ignoreNextError = true;
	}

	return e;
}

void FeedFile::SAX_error(FeedFile* file, const char *msg, ...)
{
	if (file->m_ignoreNextError)
	{
		file->m_ignoreNextError = false;
		return;
	}

	va_list argp;
	va_start(argp, msg);
	char errMsg[1024];
	vsnprintf(errMsg, sizeof(errMsg), msg, argp);
	errMsg[1024-1] = '\0';
	va_end(argp);

	// remove trailing CRLF
	for (char* pend = errMsg + strlen(errMsg) - 1; pend >= errMsg && (*pend == '\n' || *pend == '\r' || *pend == ' '); pend--) *pend = '\0';
	error("Error parsing rss feed: %s", errMsg);
}
#endif
