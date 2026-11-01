/*****************************************************************************

Copyright (c) 1996, 2015, Oracle and/or its affiliates. All Rights Reserved.
Copyright (c) 2018, 2023, MariaDB Corporation.

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation; version 2 of the License.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1335 USA

*****************************************************************************/

/********************************************************************//**
@file include/btr0types.h
The index tree general types

Created 2/17/1996 Heikki Tuuri
*************************************************************************/

#pragma once

#include "span.h"
#include "page0types.h"
#include "rem0types.h"

/** Persistent cursor */
struct btr_pcur_t;
/** B-tree cursor */
struct btr_cur_t;

/** The size of a reference to data stored on a different page.
The reference is stored at the end of the prefix of the field
in the index record. */
#define FIELD_REF_SIZE			20U
#define BTR_EXTERN_FIELD_REF_SIZE	FIELD_REF_SIZE

/** If the data don't exceed the size, the data are stored locally. */
#define BTR_EXTERN_LOCAL_STORED_MAX_SIZE	\
	(BTR_EXTERN_FIELD_REF_SIZE * 2)

/** Latching modes for btr_cur_t::search_leaf(). */
enum btr_latch_mode {
	/** Search a record on a leaf page and S-latch it. */
	BTR_SEARCH_LEAF = RW_S_LATCH,
	/** (Prepare to) modify a record on a leaf page and X-latch it. */
	BTR_MODIFY_LEAF	= RW_X_LATCH,
	/** U-latch root and X-latch a leaf page */
	BTR_MODIFY_ROOT_AND_LEAF = RW_SX_LATCH,
	/** Obtain no latches. */
	BTR_NO_LATCHES = RW_NO_LATCH,
	/** Search the previous record.
	Used in btr_pcur_move_backward_from_page(). */
	BTR_SEARCH_PREV = 4 | BTR_SEARCH_LEAF,
	/** Start modifying the entire B-tree. */
	BTR_MODIFY_TREE = 8 | BTR_MODIFY_LEAF,
	/** Continue modifying the entire R-tree.
	Only used by rtr_search_to_nth_level(). */
	BTR_CONT_MODIFY_TREE = 4 | BTR_MODIFY_TREE,

	/** The caller is already holding dict_index_t::lock S-latch. */
	BTR_ALREADY_S_LATCHED = 16,
	/** Search and S-latch a leaf page, assuming that the
	dict_index_t::lock S-latch is being held. */
	BTR_SEARCH_LEAF_ALREADY_S_LATCHED = BTR_SEARCH_LEAF
	| BTR_ALREADY_S_LATCHED,
	/** Search and X-latch a leaf page, assuming that the
	dict_index_t::lock is being held in non-exclusive mode. */
	BTR_MODIFY_LEAF_ALREADY_LATCHED = BTR_MODIFY_LEAF
	| BTR_ALREADY_S_LATCHED,
	/** Attempt to modify records in an x-latched tree. */
	BTR_MODIFY_TREE_ALREADY_LATCHED = BTR_MODIFY_TREE
	| BTR_ALREADY_S_LATCHED,
	/** U-latch root and X-latch a leaf page, assuming that
	dict_index_t::lock is being held in U mode. */
	BTR_MODIFY_ROOT_AND_LEAF_ALREADY_LATCHED = BTR_MODIFY_ROOT_AND_LEAF
	| BTR_ALREADY_S_LATCHED,

	/** In the case of BTR_MODIFY_TREE, the caller specifies
	the intention to delete record only. It is used to optimize
	block->lock range.*/
	BTR_LATCH_FOR_DELETE = 32,

	/** In the case of BTR_MODIFY_TREE, the caller specifies
	the intention to delete record only. It is used to optimize
	block->lock range.*/
	BTR_LATCH_FOR_INSERT = 64,

	/** Attempt to delete a record in the tree. */
	BTR_PURGE_TREE = BTR_MODIFY_TREE | BTR_LATCH_FOR_DELETE,
	/** Attempt to delete a record in an x-latched tree. */
	BTR_PURGE_TREE_ALREADY_LATCHED = BTR_PURGE_TREE
	| BTR_ALREADY_S_LATCHED,

	/** Attempt to insert a record into the tree. */
	BTR_INSERT_TREE = BTR_MODIFY_TREE | BTR_LATCH_FOR_INSERT,

	/** Rollback in spatial index */
	BTR_RTREE_UNDO_INS = 128,
	/** Try to delete mark a spatial index record */
	BTR_RTREE_DELETE_MARK = 256
};

/** Return value of btr_ra_collect() */
enum btr_ra_result
{
  /* Already read full capacity */
  BTR_RA_FULL,
  /* Current level page was completed; sibling may continue it */
  BTR_RA_EDGE,
  /* Reached stop_page */
  BTR_RA_STOP
};

/** Collector of B-tree leaf child page numbers gathered
during a root-to-leaf descent (at PAGE_LEVEL=1), used to drive
logical read-ahead. */
struct btr_ra_batch
{
  /** Buffer receiving child page numbers; its size is the capacity */
  const st_::span<uint32_t> pages;
  /** number of entries filled in pages */
  uint32_t n= 0;
  /** PAGE_LEVEL=1 page the child numbers were collected from, or
  0 if none (page 0 is never an index page). Used it with l1_child to
  resume the read ahead operation */
  uint32_t l1_page= 0;
  /** child page number of the last node pointer collected from
  l1_page, or 0 if none */
  uint32_t l1_child= 0;

  explicit btr_ra_batch(st_::span<uint32_t> buf) : pages(buf) {}

  /** @return whether the buffer is full */
  bool full() const { return n == pages.size(); }
  /** @return the page numbers collected so far */
  st_::span<const uint32_t> collected() const
  { return st_::span<const uint32_t>(pages.data(), n); }
  /** @return whether a level-1 page was recorded, to resume from */
  bool resumable() const { return l1_page != 0; }

  /** Read node-pointer child page numbers from a single PAGE_LEVEL=1
  page, walking from rec in scan order. Appends to pages and updates
  l1_child, so a later call can resume from where this one stopped by
  re-locating that child.
  @tparam descending  scan direction
  @param index        index the record belongs to
  @param rec          first node pointer to harvest
  @param stop_page    stop right after the child with this page number;
                      0 (never a child page) = no such page
  @retval BTR_RA_FULL when n reached pages.size()
  @retval BTR_RA_EDGE when the level-1 page was completely scanned
  @retval BTR_RA_STOP when a child reached stop_page */
  template<bool descending>
  btr_ra_result collect(const dict_index_t *index, const rec_t *rec,
                        uint32_t stop_page= 0) noexcept;

  /** Find the record on a PAGE_LEVEL=1 page from which read-ahead
  should resume: the node pointer following the one whose child page
  number is 'resume_child'. The page is walked only through its
  genuine record chain, so a concurrent reorganization
  cannot cause an invalid read; at worst the child is not found.
  @tparam descending   scan direction
  @param block         PAGE_LEVEL=1 page
  @param index         index the record belongs to
  @param resume_child  child page number last read, or 0 to
                       resume from the page edge
  @return the record to resume harvesting from
  @retval nullptr if 'resume_child' is no longer present on this page */
  template<bool descending>
  static const rec_t *resume_rec(const buf_block_t *block,
                                 const dict_index_t *index,
                                 uint32_t resume_child) noexcept;

private:
  /** @return the child page number of a node pointer record, read
  directly from the record without rec_get_offsets() */
  static uint32_t child_page(const dict_index_t *index,
                             const rec_t *rec) noexcept;
  /** @return the node pointer after rec in the scan direction;
  nullptr if rec was the last one of the page */
  static const rec_t *next_node_ptr(const rec_t *rec,
                                    bool descending) noexcept;
};
