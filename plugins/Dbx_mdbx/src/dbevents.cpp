/*

Miranda NG: the free IM client for Microsoft* Windows*

Copyright (c) 2012-18 Miranda NG team (https://miranda-ng.org)
all portions of this codebase are copyrighted to the people
listed in contributors.txt.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

#include "stdafx.h"

STDMETHODIMP_(LONG) CDbxMDBX::GetEventCount(MCONTACT contactID)
{
	DBCachedContact *cc = m_cache->GetCachedContact(contactID);
	return (cc == nullptr) ? 0 : cc->dbc.dwEventCount;
}

///////////////////////////////////////////////////////////////////////////////

STDMETHODIMP_(MEVENT) CDbxMDBX::AddEvent(MCONTACT contactID, DBEVENTINFO *dbei)
{
	if (dbei == nullptr) return 0;
	if (dbei->timestamp == 0) return 0;

	DBEvent dbe;
	dbe.contactID = contactID; // store native or subcontact's id
	dbe.iModuleId = GetModuleID(dbei->szModule);

	MCONTACT contactNotifyID = contactID;
	DBCachedContact *cc, *ccSub = nullptr;
	if ((cc = m_cache->GetCachedContact(contactID)) == nullptr)
		return 0;

	if (cc->IsSub()) {
		ccSub = cc;
		if ((cc = m_cache->GetCachedContact(cc->parentID)) == nullptr)
			return 0;

		// set default sub to the event's source
		if (!(dbei->flags & DBEF_SENT))
			db_mc_setDefault(cc->contactID, contactID, false);
		contactID = cc->contactID; // and add an event to a metahistory
		if (db_mc_isEnabled())
			contactNotifyID = contactID;
	}

	if (m_safetyMode)
		if (NotifyEventHooks(hEventFilterAddedEvent, contactNotifyID, (LPARAM)dbei))
			return 0;

	dbe.timestamp = dbei->timestamp;
	dbe.flags = dbei->flags;
	dbe.wEventType = dbei->eventType;
	dbe.cbBlob = dbei->cbBlob;
	BYTE *pBlob = dbei->pBlob;

	mir_ptr<BYTE> pCryptBlob;
	if (m_bEncrypted) {
		size_t len;
		BYTE *pResult = m_crypto->encodeBuffer(pBlob, dbe.cbBlob, &len);
		if (pResult != nullptr) {
			pCryptBlob = pBlob = pResult;
			dbe.cbBlob = (uint16_t)len;
			dbe.flags |= DBEF_ENCRYPTED;
		}
	}

	MEVENT dwEventId = InterlockedIncrement(&m_dwMaxEventId);
	{
		txn_ptr trnlck(StartTran());

		MDBX_val key = { &dwEventId, sizeof(MEVENT) }, data = { nullptr, sizeof(DBEvent) + dbe.cbBlob };
		if (mdbx_put(trnlck, m_dbEvents, &key, &data, MDBX_RESERVE) != MDBX_SUCCESS)
			return 0;

		DBEvent *pNewEvent = (DBEvent*)data.iov_base;
		*pNewEvent = dbe;
		memcpy(pNewEvent + 1, pBlob, dbe.cbBlob);

		// add a sorting key
		DBEventSortingKey key2 = { contactID, dwEventId, dbe.timestamp };
		key.iov_len = sizeof(key2); key.iov_base = &key2;
		data.iov_len = 1; data.iov_base = (char*)("");
		if (mdbx_put(trnlck, m_dbEventsSort, &key, &data, 0) != MDBX_SUCCESS)
			return 0;

		cc->Advance(dwEventId, dbe);
		MDBX_val keyc = { &contactID, sizeof(MCONTACT) }, datac = { &cc->dbc, sizeof(DBContact) };
		if (mdbx_put(trnlck, m_dbContacts, &keyc, &datac, 0) != MDBX_SUCCESS)
			return 0;

		// insert an event into a sub's history too
		if (ccSub != nullptr) {
			key2.hContact = ccSub->contactID;
			if (mdbx_put(trnlck, m_dbEventsSort, &key, &data, 0) != MDBX_SUCCESS)
				return 0;

			ccSub->Advance(dwEventId, dbe);
			datac.iov_base = &ccSub->dbc;
			keyc.iov_base = &ccSub->contactID;
			if (mdbx_put(trnlck, m_dbContacts, &keyc, &datac, 0) != MDBX_SUCCESS)
				return 0;
		}

		if (trnlck.commit() != MDBX_SUCCESS)
			return 0;
	}

	// Notify only in safe mode or on really new events
	if (m_safetyMode)
		NotifyEventHooks(hEventAddedEvent, contactNotifyID, dwEventId);

	return dwEventId;
}

///////////////////////////////////////////////////////////////////////////////

STDMETHODIMP_(BOOL) CDbxMDBX::DeleteEvent(MCONTACT contactID, MEVENT hDbEvent)
{
	DBCachedContact *cc = m_cache->GetCachedContact(contactID), *cc2 = nullptr;
	if (cc == nullptr || cc->dbc.dwEventCount == 0)
		return 1;

	DBEvent dbe;
	{
		txn_ptr_ro txn(m_txn_ro);
		MDBX_val key = { &hDbEvent, sizeof(MEVENT) }, data;
		if (mdbx_get(txn, m_dbEvents, &key, &data) != MDBX_SUCCESS)
			return 1;
		dbe = *(DBEvent*)data.iov_base;
	}

	if (contactID != dbe.contactID)
		cc2 = m_cache->GetCachedContact(dbe.contactID);

	{
		txn_ptr trnlck(StartTran());
		DBEventSortingKey key2 = { contactID, hDbEvent, dbe.timestamp };
		MDBX_val key = { &key2, sizeof(key2) }, data;

		if (mdbx_del(trnlck, m_dbEventsSort, &key, nullptr) != MDBX_SUCCESS)
			return 1;

		{
			key.iov_len = sizeof(MCONTACT); key.iov_base = &contactID;
			cc->dbc.dwEventCount--;
			if (cc->dbc.evFirstUnread == hDbEvent)
				FindNextUnread(trnlck, cc, key2);

			data.iov_len = sizeof(DBContact); data.iov_base = &cc->dbc;
			if (mdbx_put(trnlck, m_dbContacts, &key, &data, 0) != MDBX_SUCCESS)
				return 1;
		}

		if (cc2) {
			key2.hContact = dbe.contactID;
			if (mdbx_del(trnlck, m_dbEventsSort, &key, nullptr) != MDBX_SUCCESS)
				return 1;

			key.iov_len = sizeof(MCONTACT); key.iov_base = &contactID;
			cc2->dbc.dwEventCount--;
			if (cc2->dbc.evFirstUnread == hDbEvent)
				FindNextUnread(trnlck, cc2, key2);

			data.iov_len = sizeof(DBContact); data.iov_base = &cc2->dbc;
			if (mdbx_put(trnlck, m_dbContacts, &key, &data, 0) != MDBX_SUCCESS)
				return 1;
		}

		// remove a event
		key.iov_len = sizeof(MEVENT); key.iov_base = &hDbEvent;
		if (mdbx_del(trnlck, m_dbEvents, &key, nullptr) != MDBX_SUCCESS)
			return 1;

		if (trnlck.commit() != MDBX_SUCCESS)
			return 1;
	}

	NotifyEventHooks(hEventDeletedEvent, contactID, hDbEvent);
	return 0;
}

///////////////////////////////////////////////////////////////////////////////

STDMETHODIMP_(LONG) CDbxMDBX::GetBlobSize(MEVENT hDbEvent)
{
	txn_ptr_ro txn(m_txn_ro);

	MDBX_val key = { &hDbEvent, sizeof(MEVENT) }, data;
	if (mdbx_get(txn, m_dbEvents, &key, &data) != MDBX_SUCCESS)
		return -1;
	return ((const DBEvent*)data.iov_base)->cbBlob;
}

///////////////////////////////////////////////////////////////////////////////

STDMETHODIMP_(BOOL) CDbxMDBX::GetEvent(MEVENT hDbEvent, DBEVENTINFO *dbei)
{
	if (dbei == nullptr) return 1;
	if (dbei->cbBlob > 0 && dbei->pBlob == nullptr) {
		dbei->cbBlob = 0;
		return 1;
	}

	const DBEvent *dbe;
	{
		txn_ptr_ro txn(m_txn_ro);

		MDBX_val key = { &hDbEvent, sizeof(MEVENT) }, data;
		if (mdbx_get(txn, m_dbEvents, &key, &data) != MDBX_SUCCESS)
			return 1;

		dbe = (const DBEvent*)data.iov_base;
	}

	dbei->szModule = GetModuleName(dbe->iModuleId);
	dbei->timestamp = dbe->timestamp;
	dbei->flags = dbe->flags;
	dbei->eventType = dbe->wEventType;
	size_t bytesToCopy = min(dbei->cbBlob, dbe->cbBlob);
	dbei->cbBlob = dbe->cbBlob;
	if (bytesToCopy && dbei->pBlob) {
		BYTE *pSrc = (BYTE*)dbe + sizeof(DBEvent);
		if (dbe->flags & DBEF_ENCRYPTED) {
			dbei->flags &= ~DBEF_ENCRYPTED;
			size_t len;
			BYTE* pBlob = (BYTE*)m_crypto->decodeBuffer(pSrc, dbe->cbBlob, &len);
			if (pBlob == nullptr)
				return 1;

			memcpy(dbei->pBlob, pBlob, bytesToCopy);
			if (bytesToCopy > len)
				memset(dbei->pBlob + len, 0, bytesToCopy - len);
			mir_free(pBlob);
		}
		else memcpy(dbei->pBlob, pSrc, bytesToCopy);
	}
	return 0;
}

///////////////////////////////////////////////////////////////////////////////

void CDbxMDBX::FindNextUnread(const txn_ptr &txn, DBCachedContact *cc, DBEventSortingKey &key2)
{
	cursor_ptr cursor(txn, m_dbEventsSort);

	MDBX_val key = { &key2, sizeof(key2) }, data;

	for (int res = mdbx_cursor_get(cursor, &key, &data, MDBX_SET); res == MDBX_SUCCESS; res = mdbx_cursor_get(cursor, &key, &data, MDBX_NEXT)) {
		const DBEvent *dbe = (const DBEvent*)data.iov_base;
		if (dbe->contactID != cc->contactID)
			break;
		if (!dbe->markedRead()) {
			cc->dbc.evFirstUnread = key2.hEvent;
			cc->dbc.tsFirstUnread = key2.ts;
			return;
		}
	}

	cc->dbc.evFirstUnread = cc->dbc.tsFirstUnread = 0;
}

///////////////////////////////////////////////////////////////////////////////

STDMETHODIMP_(BOOL) CDbxMDBX::MarkEventRead(MCONTACT contactID, MEVENT hDbEvent)
{
	if (hDbEvent == 0) return -1;

	DBCachedContact *cc = m_cache->GetCachedContact(contactID);
	if (cc == nullptr)
		return -1;

	uint32_t wRetVal = -1;
	{
		txn_ptr trnlck(StartTran());
		MDBX_val key = { &hDbEvent, sizeof(MEVENT) }, data;
		if (mdbx_get(trnlck, m_dbEvents, &key, &data) != MDBX_SUCCESS)
			return -1;

		const DBEvent *cdbe = (const DBEvent*)data.iov_base;
		if (cdbe->markedRead())
			return cdbe->flags;

		DBEventSortingKey keyVal = { contactID, hDbEvent, cdbe->timestamp };
		if (mdbx_put(trnlck, m_dbEvents, &key, &data, MDBX_RESERVE) != MDBX_SUCCESS)
			return -1;

		DBEvent *pNewEvent = (DBEvent*)data.iov_base;
		*pNewEvent = *cdbe;

		wRetVal = (pNewEvent->flags |= DBEF_READ);

		FindNextUnread(trnlck, cc, keyVal);
		key.iov_len = sizeof(MCONTACT); key.iov_base = &contactID;
		data.iov_base = &cc->dbc; data.iov_len = sizeof(cc->dbc);
		if (mdbx_put(trnlck, m_dbContacts, &key, &data, 0) != MDBX_SUCCESS)
			return -1;

		if (trnlck.commit() != MDBX_SUCCESS)
			return -1;
	}

	NotifyEventHooks(hEventMarkedRead, contactID, (LPARAM)hDbEvent);
	return wRetVal;
}

///////////////////////////////////////////////////////////////////////////////

STDMETHODIMP_(MCONTACT) CDbxMDBX::GetEventContact(MEVENT hDbEvent)
{
	if (hDbEvent == 0)
		return INVALID_CONTACT_ID;

	txn_ptr_ro txn(m_txn_ro);

	MDBX_val key = { &hDbEvent, sizeof(MEVENT) }, data;
	if (mdbx_get(txn, m_dbEvents, &key, &data) != MDBX_SUCCESS)
		return INVALID_CONTACT_ID;

	return ((const DBEvent*)data.iov_base)->contactID;
}

///////////////////////////////////////////////////////////////////////////////

thread_local uint64_t t_tsLast = 0;
thread_local MEVENT t_evLast = 0;

STDMETHODIMP_(MEVENT) CDbxMDBX::FindFirstEvent(MCONTACT contactID)
{
	DBEventSortingKey keyVal = { contactID, 0, 0 };
	MDBX_val key = { &keyVal, sizeof(keyVal) }, data;

	txn_ptr_ro txn(m_txn_ro);

	cursor_ptr_ro cursor(m_curEventsSort);
	if (mdbx_cursor_get(cursor, &key, &data, MDBX_SET_RANGE) != MDBX_SUCCESS)
		return t_evLast = 0;

	const DBEventSortingKey *pKey = (const DBEventSortingKey*)key.iov_base;
	t_tsLast = pKey->ts;
	return t_evLast = (pKey->hContact == contactID) ? pKey->hEvent : 0;
}

///////////////////////////////////////////////////////////////////////////////

STDMETHODIMP_(MEVENT) CDbxMDBX::FindFirstUnreadEvent(MCONTACT contactID)
{
	DBCachedContact *cc = m_cache->GetCachedContact(contactID);
	return (cc == nullptr) ? 0 : cc->dbc.evFirstUnread;
}

///////////////////////////////////////////////////////////////////////////////

STDMETHODIMP_(MEVENT) CDbxMDBX::FindLastEvent(MCONTACT contactID)
{
	DBEventSortingKey keyVal = { contactID, 0xFFFFFFFF, 0xFFFFFFFFFFFFFFFF };
	MDBX_val key = { &keyVal, sizeof(keyVal) }, data;

	txn_ptr_ro txn(m_txn_ro);
	cursor_ptr_ro cursor(m_curEventsSort);

	if (mdbx_cursor_get(cursor, &key, &data, MDBX_SET_RANGE) != MDBX_SUCCESS) {
		if (mdbx_cursor_get(cursor, &key, &data, MDBX_LAST) != MDBX_SUCCESS)
			return t_evLast = 0;
	}
	else {
		if (mdbx_cursor_get(cursor, &key, &data, MDBX_PREV) != MDBX_SUCCESS)
			return t_evLast = 0;
	}

	const DBEventSortingKey *pKey = (const DBEventSortingKey*)key.iov_base;
	t_tsLast = pKey->ts;
	return t_evLast = (pKey->hContact == contactID) ? pKey->hEvent : 0;
}

///////////////////////////////////////////////////////////////////////////////

STDMETHODIMP_(MEVENT) CDbxMDBX::FindNextEvent(MCONTACT contactID, MEVENT hDbEvent)
{
	if (hDbEvent == 0)
		return t_evLast = 0;

	txn_ptr_ro txn(m_txn_ro);

	if (t_evLast != hDbEvent) {
		MDBX_val key = { &hDbEvent, sizeof(MEVENT) }, data;
		if (mdbx_get(txn, m_dbEvents, &key, &data) != MDBX_SUCCESS)
			return 0;
		t_tsLast = ((DBEvent*)data.iov_base)->timestamp;
	}

	DBEventSortingKey keyVal = { contactID, hDbEvent, t_tsLast };
	MDBX_val key = { &keyVal, sizeof(keyVal) }, data;

	cursor_ptr_ro cursor(m_curEventsSort);
	if (mdbx_cursor_get(cursor, &key, &data, MDBX_SET) != MDBX_SUCCESS)
		return t_evLast = 0;

	if (mdbx_cursor_get(cursor, &key, &data, MDBX_NEXT) != MDBX_SUCCESS)
		return t_evLast = 0;

	const DBEventSortingKey *pKey = (const DBEventSortingKey*)key.iov_base;
	t_tsLast = pKey->ts;
	return t_evLast = (pKey->hContact == contactID) ? pKey->hEvent : 0;
}

///////////////////////////////////////////////////////////////////////////////

STDMETHODIMP_(MEVENT) CDbxMDBX::FindPrevEvent(MCONTACT contactID, MEVENT hDbEvent)
{
	if (hDbEvent == 0)
		return t_evLast = 0;

	MDBX_val data;

	txn_ptr_ro txn(m_txn_ro);

	if (t_evLast != hDbEvent) {
		MDBX_val key = { &hDbEvent, sizeof(MEVENT) };
		if (mdbx_get(txn, m_dbEvents, &key, &data) != MDBX_SUCCESS)
			return 0;
		t_tsLast = ((DBEvent*)data.iov_base)->timestamp;
	}

	DBEventSortingKey keyVal = { contactID, hDbEvent, t_tsLast };
	MDBX_val key = { &keyVal, sizeof(keyVal) };

	cursor_ptr_ro cursor(m_curEventsSort);
	if (mdbx_cursor_get(cursor, &key, &data, MDBX_SET) != MDBX_SUCCESS)
		return t_evLast = 0;

	if (mdbx_cursor_get(cursor, &key, &data, MDBX_PREV) != MDBX_SUCCESS)
		return t_evLast = 0;

	const DBEventSortingKey *pKey = (const DBEventSortingKey*)key.iov_base;
	t_tsLast = pKey->ts;
	return t_evLast = (pKey->hContact == contactID) ? pKey->hEvent : 0;
}

///////////////////////////////////////////////////////////////////////////////
// checker 

int CDbxMDBX::CheckEvents1()
{
/*	txn_ptr_ro trnlck(m_txn_ro);
	cursor_ptr_ro cursor(m_curEvents);

	uint32_t eventID = 0;
	MDBX_val key = { &eventID, sizeof(eventID) }, data;

	while (mdbx_cursor_get(cursor, &key, &data, MDBX_NEXT) == MDBX_SUCCESS) {
		const DBEvent *dbe = (const DBEvent*)data.iov_base;

		DBEventSortingKey lookupkey = { dbe->contactID, eventID, dbe->timestamp };
		MDBX_val key2 = { &lookupkey, sizeof(lookupkey) }, data2;
		if (!mdbx_get(m_txn_ro, m_dbEventsSort, &key, nullptr))
			continue;

		txn_ptr trnlck(StartTran());
		if (mdbx_del(txn, m_dbEvents, &key, nullptr) == MDBX_SUCCESS)
			txn.commit();
	}
  */
	return ERROR_NO_MORE_ITEMS;
}

int CDbxMDBX::CheckEvents2()
{
	return ERROR_NO_MORE_ITEMS;
}
