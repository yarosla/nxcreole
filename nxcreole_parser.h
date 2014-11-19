/*
 * Copyright (c) 2014 Yaroslav Stavnichiy <yarosla@gmail.com>
 *
 * This file is part of NXCREOLE.
 *
 * NXCREOLE is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation, either version 3
 * of the License, or (at your option) any later version.
 *
 * NXCREOLE is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with NXCREOLE. If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * nxcreole is a parser for Wiki Creole syntax (http://www.wikicreole.org/).
 */

typedef enum {
  FN_APPEND_TEXT,
  FN_APPEND_TABLE_OPEN,
  FN_APPEND_TABLE_ROW_OPEN,
  FN_APPEND_TABLE_HEAD_CELL_OPEN,
  FN_APPEND_TABLE_HEAD_CELL_CLOSE,
  FN_APPEND_TABLE_CELL_OPEN,
  FN_APPEND_TABLE_CELL_CLOSE,
  FN_APPEND_TABLE_ROW_CLOSE,
  FN_APPEND_TABLE_CLOSE,
  FN_APPEND_LIST_OPEN,
  FN_APPEND_LIST_NEXT_ITEM,
  FN_APPEND_LIST_BLANK_ITEM,
  FN_APPEND_LIST_CLOSE,
  FN_APPEND_PARAGRAPH_OPEN,
  FN_APPEND_PARAGRAPH_CLOSE,
  FN_APPEND_HEADING_OPEN,
  FN_APPEND_HEADING_CLOSE,
  FN_APPEND_FORMAT_OPEN,
  FN_APPEND_FORMAT_CLOSE,
  FN_APPEND_HR,
  FN_APPEND_BR,
  FN_APPEND_NOWIKI_BLOCK,
  FN_APPEND_NOWIKI_INLINE,
  FN_APPEND_IMAGE,
  FN_APPEND_LINK,
  FN_APPEND_PLACEHOLDER,
  FN_COUNT
} nxcreole_fn_id_t;

#define MAX_LIST_LEVELS 128

//typedef void (*append0_t)(struct parse_ctx* ctx, fn_id_t fn);
//typedef void (*append1_t)(struct parse_ctx* ctx, fn_id_t fn, const wchar_t* u, size_t length);

typedef struct nxcreole_parse_ctx {
  const wchar_t* ptr;
  void (*append0)(struct nxcreole_parse_ctx* ctx, nxcreole_fn_id_t fn);
  void (*append1)(struct nxcreole_parse_ctx* ctx, nxcreole_fn_id_t fn, const wchar_t* u, size_t length);
  void* fn[FN_COUNT];
  wchar_t list_levels[MAX_LIST_LEVELS];
  short list_level;
  short mediawiki_table_level;
  unsigned in_table:1;
  unsigned blockquote_br:1;
} nxcreole_parse_ctx;

void nxcreole_init(nxcreole_parse_ctx* ctx, const wchar_t* text);
void nxcreole_parse(nxcreole_parse_ctx* ctx);
