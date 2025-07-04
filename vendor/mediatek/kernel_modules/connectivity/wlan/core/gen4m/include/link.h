/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * Copyright (c) 2021 MediaTek Inc.
 */

/*! \file   link.h
 *    \brief  Definition for simple doubly linked list operations.
 *
 *    In this file we define the simple doubly linked list data structure
 *    and its operation MACROs and INLINE functions.
 */

#ifndef _LINK_H
#define _LINK_H

/*******************************************************************************
 *                         C O M P I L E R   F L A G S
 *******************************************************************************
 */

/*******************************************************************************
 *                    E X T E R N A L   R E F E R E N C E S
 *******************************************************************************
 */
#include "gl_typedef.h"

/*******************************************************************************
 *                              C O N S T A N T S
 *******************************************************************************
 */
/* May cause page fault & unalignment issue (data abort) */
#define INVALID_LINK_POISON1    ((void *) 0x00100101)

/* Used to verify that nonbody uses non-initialized link entries. */
#define INVALID_LINK_POISON2    ((void *) 0x00100201)

/*******************************************************************************
 *                             D A T A   T Y P E S
 *******************************************************************************
 */
/* Simple Doubly Linked List Structures - Entry Part */
struct LINK_ENTRY {
	struct LINK_ENTRY *prNext, *prPrev;
};

/* Simple Doubly Linked List Structures - List Part */
struct LINK {
	struct LINK_ENTRY *prNext;
	struct LINK_ENTRY *prPrev;
	uint32_t u4NumElem;
};

/* Support AP Selection */
struct LINK_MGMT {
	struct LINK rUsingLink;
	struct LINK rFreeLink;
};
/* end Support AP Selection */
/*******************************************************************************
 *                            P U B L I C   D A T A
 *******************************************************************************
 */

/*******************************************************************************
 *                           P R I V A T E   D A T A
 *******************************************************************************
 */

/*******************************************************************************
 *                                 M A C R O S
 *******************************************************************************
 */
#if 0
/* No one use it, temporarily mark it for [Lint - Info 773] */
#define LINK_ADDR(rLink)        \
	{ (struct LINK_ENTRY *)(&(rLink)), \
	  (struct LINK_ENTRY *)(&(rLink)), 0 }

#define LINK_DECLARATION(rLink) \
	struct LINK rLink = LINK_ADDR(rLink)
#endif

#define LINK_INITIALIZE(prLink) \
	do { \
		((struct LINK *)(prLink))->prNext = \
			(struct LINK_ENTRY *)(prLink); \
		((struct LINK *)(prLink))->prPrev = \
			(struct LINK_ENTRY *)(prLink); \
		((struct LINK *)(prLink))->u4NumElem = 0; \
	} while (0)
/* Support AP Selection */
#define LINK_MGMT_INIT(prLinkMgmt) \
	do { \
		LINK_INITIALIZE(&((struct LINK_MGMT *)prLinkMgmt)->rUsingLink); \
		LINK_INITIALIZE(&((struct LINK_MGMT *)prLinkMgmt)->rFreeLink); \
	} while (0)

#define LINK_MGMT_GET_ENTRY(prLinkMgmt, prEntry, EntryType, memType) \
	do { \
		if (LINK_IS_INVALID( \
		    &((struct LINK_MGMT *)prLinkMgmt)->rFreeLink)) { \
			DBGLOG(INIT, ERROR, "FreeLink is invalid\n"); \
		} \
		else if (LINK_IS_INVALID( \
		    &((struct LINK_MGMT *)prLinkMgmt)->rUsingLink)) { \
			DBGLOG(INIT, ERROR, "UsingLink is invalid\n"); \
		} \
		else { \
			LINK_REMOVE_HEAD( \
				&((struct LINK_MGMT *)prLinkMgmt)->rFreeLink, \
				prEntry, EntryType*); \
			if (!prEntry) \
				prEntry = kalMemAlloc(sizeof(EntryType), \
						  memType); \
			if (prEntry) { \
				kalMemZero(prEntry, sizeof(EntryType)); \
				LINK_INSERT_TAIL( \
				    &((struct LINK_MGMT *)prLinkMgmt) \
				    ->rUsingLink, &prEntry->rLinkEntry); \
			} \
		} \
	} while (0)

#define LINK_MGMT_RETURN_ENTRY(prLinkMgmt, prEntry) \
	do {\
		if (!prEntry)\
			break;\
		LINK_REMOVE_KNOWN_ENTRY(                                       \
			&((struct LINK_MGMT *)prLinkMgmt)->rUsingLink,         \
			prEntry); \
		LINK_INSERT_TAIL(&((struct LINK_MGMT *)prLinkMgmt)->rFreeLink, \
				 &prEntry->rLinkEntry);                        \
	} while (0)

#define LINK_MGMT_UNINIT(prLinkMgmt, EntryType, memType) \
	do { \
		EntryType *prEntry = NULL; \
		struct LINK *prFreeList = &((struct LINK_MGMT *)prLinkMgmt)->rFreeLink; \
		struct LINK *prUsingList = &((struct LINK_MGMT *)prLinkMgmt)->rUsingLink; \
		LINK_REMOVE_HEAD(prFreeList, prEntry, EntryType *); \
		while (prEntry) { \
			kalMemFree(prEntry, memType, sizeof(EntryType)); \
			LINK_REMOVE_HEAD(prFreeList, prEntry, EntryType *); \
		} \
		LINK_REMOVE_HEAD(prUsingList, prEntry, EntryType *); \
		while (prEntry) { \
			kalMemFree(prEntry, memType, sizeof(EntryType)); \
			LINK_REMOVE_HEAD(prUsingList, prEntry, EntryType *); \
		} \
	} while (0)
/* end Support AP Selection */

#define LINK_ENTRY_INITIALIZE(prEntry) \
	do { \
		((struct LINK_ENTRY *)(prEntry))->prNext = \
			(struct LINK_ENTRY *)NULL; \
		((struct LINK_ENTRY *)(prEntry))->prPrev = \
			(struct LINK_ENTRY *)NULL; \
	} while (0)

#define LINK_ENTRY_INVALID(prEntry) \
	do { \
		((struct LINK_ENTRY *)(prEntry))->prNext = \
			(struct LINK_ENTRY *)INVALID_LINK_POISON1; \
		((struct LINK_ENTRY *)(prEntry))->prPrev = \
			(struct LINK_ENTRY *)INVALID_LINK_POISON2; \
	} while (0)

#define LINK_IS_EMPTY(prLink)           \
	(((struct LINK *)(prLink))->prNext == (struct LINK_ENTRY *)(prLink))

#define LINK_ENTRY_IS_VALID(prEntry) \
	(((struct LINK_ENTRY *)(prEntry))->prNext != (struct LINK_ENTRY *)NULL && \
	((struct LINK_ENTRY *)(prEntry))->prNext != \
		(struct LINK_ENTRY *)INVALID_LINK_POISON1 && \
	((struct LINK_ENTRY *)(prEntry))->prPrev != (struct LINK_ENTRY *)NULL && \
	((struct LINK_ENTRY *)(prEntry))->prPrev != \
	(struct LINK_ENTRY *)INVALID_LINK_POISON2)

#define LINK_ENTRY_IS_VALID_LINK_LIST(prEntry) \
	(((struct LINK_ENTRY *)(prEntry))->prNext->prPrev == \
		(struct LINK_ENTRY *)prEntry && \
	((struct LINK_ENTRY *)(prEntry))->prPrev->prNext == \
		(struct LINK_ENTRY *)prEntry)

/* NOTE: We should do memory zero before any LINK been initiated,
 *       so we can check if it is valid before parsing the LINK.
 */
#define LINK_IS_INVALID(prLink)         \
	(((struct LINK *)(prLink))->prNext == (struct LINK_ENTRY *)NULL)

#define LINK_IS_VALID(prLink)           \
	(((struct LINK *)(prLink))->prNext != (struct LINK_ENTRY *)NULL)

#define LINK_ENTRY(ptr, type, member)   CONTAINER_OF(ptr, type, member)

/* Insert an entry into a link list's head */
#define LINK_INSERT_HEAD(prLink, prEntry) \
	{ \
	    linkAdd(prEntry, prLink); \
	    ((prLink)->u4NumElem)++; \
	}

/* Append an entry into a link list's tail */
#define LINK_INSERT_TAIL(prLink, prEntry) \
	{ \
	    linkAddTail(prEntry, prLink); \
	    ((prLink)->u4NumElem)++; \
	}

/* Insert an entry after a speceified entry */
#define LINK_INSERT_AFTER(prLink, prEntry, prNew) \
	{ \
		__linkAdd( \
			(struct LINK_ENTRY *)prNew, \
			(struct LINK_ENTRY *)prEntry, \
			(struct LINK_ENTRY *)(prEntry)->prNext); \
		((prLink)->u4NumElem)++; \
	}

/* Insert an entry before a speceified entry */
#define LINK_INSERT_BEFORE(prLink, prEntry, prNew) \
	{ \
		__linkAdd( \
			(struct LINK_ENTRY *)prNew, \
			(struct LINK_ENTRY *)(prEntry)->prPrev,	\
			(struct LINK_ENTRY *)prEntry); \
		((prLink)->u4NumElem)++; \
	}

/* Peek head entry, but keep still in link list */
#define LINK_PEEK_HEAD(prLink, _type, _member) \
	( \
	    LINK_IS_EMPTY(prLink) ? \
	    NULL : LINK_ENTRY((prLink)->prNext, _type, _member) \
	)

/* Peek tail entry, but keep still in link list */
#define LINK_PEEK_TAIL(prLink, _type, _member) \
	( \
	    LINK_IS_EMPTY(prLink) ? \
	    NULL : LINK_ENTRY((prLink)->prPrev, _type, _member) \
	)

/* Get first entry from a link list */
/* NOTE: We assume the link entry located at the beginning of "prEntry Type",
 * so that we can cast the link entry to other data type without doubts.
 * And this macro also decrease the total entry count at the same time.
 */
#define LINK_REMOVE_HEAD(prLink, prEntry, _P_TYPE) \
	{ \
		ASSERT(prLink); \
		if (LINK_IS_INVALID(prLink)) { \
			prEntry = (_P_TYPE)NULL; \
			DBGLOG(INIT, ERROR, "link is invalid\n"); \
			ASSERT(LINK_IS_VALID(prLink)); \
		} \
		else if (LINK_IS_EMPTY(prLink)) {  \
			prEntry = (_P_TYPE)NULL; \
		} \
		else { \
			prEntry = \
				(_P_TYPE)(((struct LINK *)(prLink))->prNext); \
			ASSERT(LINK_ENTRY_IS_VALID(prEntry)); \
			ASSERT(LINK_ENTRY_IS_VALID_LINK_LIST(prEntry)); \
			if ((LINK_ENTRY_IS_VALID(prEntry)) \
			    && (LINK_ENTRY_IS_VALID_LINK_LIST(prEntry))) { \
				linkDel((struct LINK_ENTRY *)prEntry); \
				((prLink)->u4NumElem)--; \
			} \
			else { \
				prEntry = (_P_TYPE)NULL; \
				DBGLOG(INIT, ERROR, \
					"link entry is invalid\n"); \
			} \
		} \
	}

/* Get first entry from a link list */
/* NOTE: We assume the link entry located at the beginning of "prEntry Type",
 * so that we can cast the link entry to other data type without doubts.
 * And this macro also decrease the total entry count at the same time.
 */
#define LINK_REMOVE_HEAD_VAR(prLink, prEntry, _P_TYPE) \
	{ \
		if (LINK_IS_EMPTY(prLink)) { \
			prEntry = (_P_TYPE)NULL; \
	    } \
		else { \
			prEntry = \
			    (_P_TYPE)(((struct LINK *)(prLink))->prNext); \
			linkDel((struct LINK_ENTRY *)prEntry); \
			((prLink)->u4NumElem)--; \
	    } \
	}

/* Assume the link entry located at the beginning of prEntry Type.
 * And also decrease the total entry count.
 */
#define LINK_REMOVE_KNOWN_ENTRY(prLink, prEntry) \
	{ \
	    ASSERT(prLink); \
	    ASSERT(prEntry); \
	    linkDel((struct LINK_ENTRY *)prEntry); \
	    ((prLink)->u4NumElem)--; \
	}

/* Merge prSrcLink to prDstLink and put prSrcLink ahead of prDstLink */
/* Check if the LINK is VALID first */
#define LINK_MERGE_TO_HEAD(prDstLink, prSrcLink) \
	{ \
		if ((!LINK_IS_INVALID(prSrcLink)) \
		    && (!LINK_IS_INVALID(prDstLink))) { \
			if (!LINK_IS_EMPTY(prSrcLink)) { \
				linkMergeToHead( \
					(struct LINK *)prDstLink, \
					(struct LINK *)prSrcLink); \
				(prDstLink)->u4NumElem += \
					(prSrcLink)->u4NumElem; \
				LINK_INITIALIZE(prSrcLink); \
			} \
		} \
		else { \
			DBGLOG(INIT, ERROR, "link is invalid\n"); \
		} \
	}

/* Merge prSrcLink to prDstLink and put prSrcLink at tail */
/* Check if the LINK is VALID first */
#define LINK_MERGE_TO_TAIL(prDstLink, prSrcLink) \
	{ \
		if ((!LINK_IS_INVALID(prSrcLink)) \
		    && (!LINK_IS_INVALID(prDstLink))) { \
			if (!LINK_IS_EMPTY(prSrcLink)) { \
				linkMergeToTail( \
					(struct LINK *)prDstLink, \
					(struct LINK *)prSrcLink); \
				(prDstLink)->u4NumElem += \
					(prSrcLink)->u4NumElem; \
				LINK_INITIALIZE(prSrcLink); \
			} \
		} \
		else { \
			DBGLOG(INIT, ERROR, "link is invalid\n"); \
		} \
	}

/* Iterate over a link list */
#define LINK_FOR_EACH(prEntry, prLink) \
	for (prEntry = (prLink)->prNext; \
	 prEntry != (struct LINK_ENTRY *)(prLink); \
	 prEntry = (struct LINK_ENTRY *)prEntry->prNext)

/* Iterate over a link list backwards */
#define LINK_FOR_EACH_PREV(prEntry, prLink) \
	for (prEntry = (prLink)->prPrev; \
	 prEntry != (struct LINK_ENTRY *)(prLink); \
	 prEntry = (struct LINK_ENTRY *)prEntry->prPrev)

/* Iterate over a link list safe against removal of link entry */
#define LINK_FOR_EACH_SAFE(prEntry, prNextEntry, prLink) \
	for (prEntry = (prLink)->prNext, prNextEntry = prEntry->prNext; \
	 prEntry != (struct LINK_ENTRY *)(prLink); \
	 prEntry = prNextEntry, prNextEntry = prEntry->prNext)

/* Iterate over a link list of given type */
#define LINK_FOR_EACH_ENTRY(prObj, prLink, rMember, _TYPE) \
	for (prObj = LINK_ENTRY((prLink)->prNext, _TYPE, rMember); \
	 &prObj->rMember != (struct LINK_ENTRY *)(prLink); \
	 prObj = LINK_ENTRY(prObj->rMember.prNext, _TYPE, rMember))

/* Iterate backwards over a link list of given type */
#define LINK_FOR_EACH_ENTRY_PREV(prObj, prLink, rMember, _TYPE) \
	for (prObj = LINK_ENTRY((prLink)->prPrev, _TYPE, rMember);  \
	 &prObj->rMember != (struct LINK_ENTRY *)(prLink); \
	 prObj = LINK_ENTRY(prObj->rMember.prPrev, _TYPE, rMember))

/* Iterate over a link list of given type safe against removal of link entry */
#define LINK_FOR_EACH_ENTRY_SAFE(prObj, prNextObj, prLink, rMember, _TYPE) \
	for (prObj = LINK_ENTRY((prLink)->prNext, _TYPE, rMember),  \
	 prNextObj = LINK_ENTRY(prObj->rMember.prNext, _TYPE, rMember); \
	 &prObj->rMember != (struct LINK_ENTRY *)(prLink); \
	 prObj = prNextObj, \
	 prNextObj = LINK_ENTRY(prNextObj->rMember.prNext, _TYPE, rMember))

/*******************************************************************************
 *                  F U N C T I O N   D E C L A R A T I O N S
 *******************************************************************************
 */

/*******************************************************************************
 *                              F U N C T I O N S
 *******************************************************************************
 */
/*----------------------------------------------------------------------------*/
/*!
 * \brief This function is only for internal link list manipulation.
 *
 * \param[in] prNew  Pointer of new link head
 * \param[in] prPrev Pointer of previous link head
 * \param[in] prNext Pointer of next link head
 *
 * \return (none)
 */
/*----------------------------------------------------------------------------*/
static __KAL_INLINE__ void __linkAdd(struct LINK_ENTRY
				     *prNew, struct LINK_ENTRY *prPrev,
				     struct LINK_ENTRY *prNext)
{
	if (prNext) {
		prNext->prPrev = prNew;
		prNew->prNext = prNext;
	}
	if (prPrev) {
		prNew->prPrev = prPrev;
		prPrev->prNext = prNew;
	}
}				/* end of __linkAdd() */

/*----------------------------------------------------------------------------*/
/*!
 * \brief This function will add a new entry after the specified link head.
 *
 * \param[in] prNew  New entry to be added
 * \param[in] prHead Specified link head to add it after
 *
 * \return (none)
 */
/*----------------------------------------------------------------------------*/
static __KAL_INLINE__ void linkAdd(struct LINK_ENTRY
				   *prNew, struct LINK *prLink)
{
	__linkAdd(prNew, (struct LINK_ENTRY *) prLink,
		  prLink->prNext);
}				/* end of linkAdd() */

/*----------------------------------------------------------------------------*/
/*!
 * \brief This function will add a new entry before the specified link head.
 *
 * \param[in] prNew  New entry to be added
 * \param[in] prHead Specified link head to add it before
 *
 * \return (none)
 */
/*----------------------------------------------------------------------------*/
static __KAL_INLINE__ void linkAddTail(struct LINK_ENTRY
				       *prNew, struct LINK *prLink)
{
	__linkAdd(prNew, prLink->prPrev,
		  (struct LINK_ENTRY *) prLink);
}				/* end of linkAddTail() */

/*----------------------------------------------------------------------------*/
/*!
 * \brief This function is only for internal link list manipulation.
 *
 * \param[in] prPrev Pointer of previous link head
 * \param[in] prNext Pointer of next link head
 *
 * \return (none)
 */
/*----------------------------------------------------------------------------*/
static __KAL_INLINE__ void __linkDel(struct LINK_ENTRY
				     *prPrev, struct LINK_ENTRY *prNext)
{
	if (prNext)
		prNext->prPrev = prPrev;
	if (prPrev)
		prPrev->prNext = prNext;
}				/* end of __linkDel() */

/*----------------------------------------------------------------------------*/
/*!
 * \brief This function will delete a specified entry from link list.
 *        NOTE: the entry is in an initial state.
 *
 * \param prEntry    Specified link head(entry)
 *
 * \return (none)
 */
/*----------------------------------------------------------------------------*/
static __KAL_INLINE__ void linkDel(struct LINK_ENTRY
				   *prEntry)
{
	__linkDel(prEntry->prPrev, prEntry->prNext);

	LINK_ENTRY_INITIALIZE(prEntry);
}				/* end of linkDel() */

/*----------------------------------------------------------------------------*/
/*!
 * \brief This function will delete a specified entry from link list
 *        and then add it after the specified link head.
 *
 * \param[in] prEntry        Specified link head(entry)
 * \param[in] prOtherHead    Another link head to add it after
 *
 * \return (none)
 */
/*----------------------------------------------------------------------------*/
static __KAL_INLINE__ void linkMove(struct LINK_ENTRY
				    *prEntry, struct LINK *prLink)
{
	__linkDel(prEntry->prPrev, prEntry->prNext);
	linkAdd(prEntry, prLink);
}				/* end of linkMove() */

/*----------------------------------------------------------------------------*/
/*!
 * \brief This function will delete a specified entry from link list
 *        and then add it before the specified link head.
 *
 * \param[in] prEntry        Specified link head(entry)
 * \param[in] prOtherHead    Another link head to add it before
 *
 * \return (none)
 */
/*----------------------------------------------------------------------------*/
static __KAL_INLINE__ void linkMoveTail(struct LINK_ENTRY
					*prEntry, struct LINK *prLink)
{
	__linkDel(prEntry->prPrev, prEntry->prNext);
	linkAddTail(prEntry, prLink);
}				/* end of linkMoveTail() */

/*----------------------------------------------------------------------------*/
/*!
* \brief This function will merge source link to the tail of destination link.
*
* \param[in] prDst    destination link
* \param[in] prSrc    source link
*
* \return (none)
*/
/*----------------------------------------------------------------------------*/
static __KAL_INLINE__ void linkMergeToTail(struct LINK *prDst,
					   struct LINK *prSrc)
{
	prSrc->prNext->prPrev = prDst->prPrev;
	prSrc->prPrev->prNext = (struct LINK_ENTRY *)prDst;
	prDst->prPrev->prNext = prSrc->prNext;
	prDst->prPrev = prSrc->prPrev;
}

/*----------------------------------------------------------------------------*/
/*!
* \brief This function will merge source link to the head of destination link.
*
* \param[in] prDst    destination link
* \param[in] prSrc    source link

*
* \return (none)
*/
/*----------------------------------------------------------------------------*/
static __KAL_INLINE__ void linkMergeToHead(struct LINK *prDst,
					   struct LINK *prSrc)
{
	prSrc->prNext->prPrev = (struct LINK_ENTRY *)prDst;
	prSrc->prPrev->prNext = prDst->prNext;
	prDst->prNext->prPrev = prSrc->prPrev;
	prDst->prNext = prSrc->prNext;
}

#endif /* _LINK_H */
