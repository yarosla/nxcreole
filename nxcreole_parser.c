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

#include <assert.h>
#include <stdio.h>
#include <wchar.h>
#include <string.h>
#include <stdlib.h>

#include "nxcreole_parser.h"

#define SKIP_WS(p) while (*(p)>0 && *(p)<=L' ' && *(p)!=L'\n') (p)++;

#define IS_LIST_CHAR(c) ((c)==L'*' || (c)==L'-' || (c)==L'#' || (c)==L'>' || (c)==L':' || (c)==L'!')
#define IS_FORMAT_CHAR(c) ((c)==L'*' || (c)==L'/' || (c)==L'_' || (c)==L'#')

// From MediaWiki: "._\\/~%-+&#?!=()@"
// From http://www.ietf.org/rfc/rfc2396.txt :
//   reserved:   ";/?:@&=+$,"
//   unreserved: "-_.!~*'()"
//   delim:      "%#"
// Note: I excluded apostrophe
#define IS_URL_CHAR(c) (((c)>=L'a' && (c)<=L'z') || ((c)>=L'A' && (c)<=L'Z') || ((c)>=L'0' && (c)<=L'9') \
        || wcschr(L"/?@&=+,-_.!~()%#;:$*", c))

static void close_lists_and_tables(nxcreole_parse_ctx* ctx) {
  // close unclosed lists
  while (ctx->list_level>=0) {
    ctx->append1(ctx, FN_APPEND_LIST_CLOSE, &ctx->list_levels[ctx->list_level--], 1);
  }
  // close table
  if (ctx->in_table) {
    ctx->append0(ctx, FN_APPEND_TABLE_CLOSE);
    ctx->in_table=0;
  }
  // mediawiki-style tables not closed by this function
}

static const wchar_t* find_end_of_nowiki(const wchar_t* p) {
  for (p=wcschr(p, L'}'); p; p=wcschr(p+1, L'}')) {
    if (p[-1]==L'~') continue;
    if (p[1]==L'}' && p[2]==L'}') {
      while (p[3]==L'}') p++; // shift to end of sequence of more than 3x'}' (eg. '}}}}}')
      return p;
    }
  }
  return 0;
}

static int remove_escapes_from_nowiki(const wchar_t* s, size_t len, const wchar_t** res, size_t* res_len) {
  const wchar_t* src=s;
  wchar_t* res_buf=0;
  wchar_t* res_ptr=0;
  const wchar_t* end=s+len-3; // account for }}}
  const wchar_t* p;
  for (p=wcschr(s, L'~'); p && p<end; p=wcschr(p+1, L'~')) {
    if (p[1]==L'}' && p[2]==L'}' && p[3]==L'}') {
      // found escape sequence ~}}}
      if (!res_ptr) {
        res_ptr=res_buf=malloc((len-1)*sizeof(wchar_t));
        if (!res_buf) { // error - should not happen
          *res=s, *res_len=len; // pass through without processing
          return 0; // caller must not free(res)
        }
      }
      if (p>src) {
        size_t cnt=p-src;
        memcpy(res_ptr, src, cnt*sizeof(wchar_t));
        res_ptr+=cnt;
      }
      src=p+1; // src points to }}}
    }
  }
  if (res_ptr) {
    // copy remainder
    size_t cnt=end+3-src;
    if (cnt) {
      memcpy(res_ptr, src, cnt*sizeof(wchar_t));
      res_ptr+=cnt;
    }
    *res_len=res_ptr-res_buf;
    *res=res_buf;
    return 1; // caller must free(res)
  }
  else { // no ~}}} sequence found
    *res=s, *res_len=len; // pass through
    return 0; // caller must not free(res)
  }
}

static void append_nowiki(nxcreole_parse_ctx* ctx, nxcreole_fn_id_t fn_id, const wchar_t* s, size_t len) {
  // appending nowiki needs special treatment - removal of tilde in every ~}}}
  const wchar_t* clean;
  size_t clean_len;
  int should_free=remove_escapes_from_nowiki(s, len, &clean, &clean_len);
  ctx->append1(ctx, fn_id, clean, clean_len);
  if (should_free) free((void*)clean);
}

static const wchar_t* find_delimiter(const wchar_t* p, wchar_t c) {
  for (p=wcschr(p, c); p; p=wcschr(p+1, c)) {
    if (p[1]==c) return p;
  }
  return 0;
}

static const wchar_t* find_triple_delimiter(const wchar_t* p, wchar_t c) {
  for (p=wcschr(p, c); p; p=wcschr(p+1, c)) {
    if (p[1]==c && p[2]==c) return p;
  }
  return 0;
}

typedef enum {
  ITEM_CTX_PARAGRAPH,
  ITEM_CTX_LIST_ITEM,
  ITEM_CTX_TABLE_CELL,
  ITEM_CTX_HEADER
} item_ctx_t;

typedef enum {
  END_OF_ITEM,
  END_OF_CELL,
  END_OF_BLOCK,
} end_of_context_t;

#define FLUSH_TB() if (tb_ptr!=tb) {ctx->append1(ctx, FN_APPEND_TEXT, tb, tb_ptr-tb); tb_ptr=tb;}
#define END_OF_ITEM_CONTEXT(p) {FLUSH_TB(); *end_ptr=(p); return END_OF_ITEM;}
#define END_OF_CELL_CONTEXT(p) {FLUSH_TB(); *end_ptr=(p); return END_OF_CELL;}
#define END_OF_BLOCK_CONTEXT(p) {FLUSH_TB(); *end_ptr=(p); return END_OF_BLOCK;}
#define PROPAGATE_END_OF_CONTEXT(p, r) {*end_ptr=(p); return (r);}

#define TMP_BUF_SIZE 1024

static end_of_context_t parse_item(nxcreole_parse_ctx* ctx, const wchar_t* ptr, const wchar_t** end_ptr, wchar_t delimiter, item_ctx_t item_ctx) {
  wchar_t tb[TMP_BUF_SIZE], *tb_ptr=tb, *tb_end=tb+TMP_BUF_SIZE;
  // const wchar_t* start_ptr=ptr;

  for (;;) {
    wchar_t c=*ptr;
    if (!c) END_OF_BLOCK_CONTEXT(ptr); // eot
    if (c==delimiter && ptr[1]==delimiter) END_OF_ITEM_CONTEXT(ptr+2);

    int at_line_start=0;
    if (c==L'\n') {
      at_line_start=1;
      if (item_ctx==ITEM_CTX_HEADER || item_ctx==ITEM_CTX_TABLE_CELL)
        END_OF_BLOCK_CONTEXT(ptr);
      ptr++; SKIP_WS(ptr);
      c=*ptr;
      if (!c) END_OF_BLOCK_CONTEXT(ptr); // eot
      if (c==L'\n') // \n\n => blank line delimits everything
        END_OF_BLOCK_CONTEXT(ptr); // leave second \n unparsed so parse_block() can close all lists

      // special handling at line start
      if (IS_LIST_CHAR(c)) { // start of list item?
        if (!IS_FORMAT_CHAR(c)) END_OF_BLOCK_CONTEXT(ptr);
        // here we have a list char, which also happen to be a format char
        if (ptr[1]!=c) END_OF_BLOCK_CONTEXT(ptr); // format chars go in pairs
        if (ctx->list_level>=0 && c==ctx->list_levels[0])
          // c matches current list's first level, so it must be new list item
          END_OF_BLOCK_CONTEXT(ptr);
        // otherwise it must be just formatting sequence => no break of context
      }

      switch (c) {
        case L'=': // heading
        case L'|': // table or mediawiki table
          END_OF_BLOCK_CONTEXT(ptr);
        case L'{': // start of mediawiki table?
          if (ptr[1]==L'|') {
            const wchar_t* p=ptr+2;
            SKIP_WS(p);
            if (!*p || *p==L'\n') END_OF_BLOCK_CONTEXT(ptr); // yes, it's start of a table
          }
          break;
/*
        case L'-': // can be ---- <hr>, but '-' is list char, so it's been handled already
          if (ptr[1]==L'-' && ptr[2]==L'-' && ptr[3]==L'-') {
            const wchar_t* p=ptr+4;
            SKIP_WS(p);
            if (!*p || *p==L'\n') END_OF_BLOCK_CONTEXT(ptr); // yes, it's <hr>
          }
          break;
*/
      }
      // if none matched add '\n' to text buffer
      *tb_ptr++=L'\n';
      if (tb_ptr==tb_end) FLUSH_TB();
      // ptr and c already shifted past the '\n' and whitespace after, so go on
    }

    if (IS_FORMAT_CHAR(c) && ptr[1]==c) { // double format character
      FLUSH_TB();
      ctx->append1(ctx, FN_APPEND_FORMAT_OPEN, &c, 1);
      end_of_context_t res=parse_item(ctx, ptr+2, &ptr, c, item_ctx);
      ctx->append1(ctx, FN_APPEND_FORMAT_CLOSE, &c, 1);
      if (res!=END_OF_ITEM) PROPAGATE_END_OF_CONTEXT(ptr, res); // propagate EOC to the top
      continue;
    }

    switch (c) {
      case L'|':
        if (item_ctx==ITEM_CTX_TABLE_CELL) END_OF_CELL_CONTEXT(ptr);
        break;
      case L'{':
        if (ptr[1]==L'{') {
          if (ptr[2]==L'{') { // inline {{{nowiki}}}
            const wchar_t* start_of_nowiki=ptr+3;
            const wchar_t* end_of_nowiki=find_end_of_nowiki(start_of_nowiki);
            const wchar_t* next_ptr=end_of_nowiki+3;
            if (end_of_nowiki) {
              FLUSH_TB();
              wchar_t* newline=wcschr(start_of_nowiki, L'\n');
              if (newline && newline<end_of_nowiki) { // block <pre>
                SKIP_WS(start_of_nowiki);
                if (start_of_nowiki[0]==L'\n') start_of_nowiki++; // eat first newline
                if (end_of_nowiki[-1]==L'\n') end_of_nowiki--; // eat last newline
                if (end_of_nowiki>start_of_nowiki) { // non-empty
                  if (item_ctx==ITEM_CTX_PARAGRAPH) ctx->append0(ctx, FN_APPEND_PARAGRAPH_CLOSE); // break the paragraph because XHTML does not allow <pre> children of <p>
                  append_nowiki(ctx, FN_APPEND_NOWIKI_BLOCK, start_of_nowiki, end_of_nowiki-start_of_nowiki);
                  if (item_ctx==ITEM_CTX_PARAGRAPH) ctx->append0(ctx, FN_APPEND_PARAGRAPH_OPEN);
                }
              }
              else { // inline {{{nowiki}}}
                append_nowiki(ctx, FN_APPEND_NOWIKI_INLINE, start_of_nowiki, end_of_nowiki-start_of_nowiki);
              }
              ptr=next_ptr;
              continue;
            }
          }
          else { // {{image}}
            const wchar_t* start_of_image=ptr+2;
            const wchar_t* end_of_image=find_delimiter(start_of_image, L'}');
            if (end_of_image) {
              FLUSH_TB();
              ptr=end_of_image+2;
              ctx->append1(ctx, FN_APPEND_IMAGE, start_of_image, end_of_image-start_of_image);
              continue;
            }
          }
        }
        break;
      case L'[':
        if (ptr[1]==L'[') {
          const wchar_t* start_of_link=ptr+2;
          const wchar_t* end_of_link=find_delimiter(start_of_link, L']');
          if (end_of_link) {
            FLUSH_TB();
            ptr=end_of_link+2;
            ctx->append1(ctx, FN_APPEND_LINK, start_of_link, end_of_link-start_of_link);
            continue;
          }
        }
        break;
      case L'\\':
        if (ptr[1]==L'\\') {
          FLUSH_TB();
          ctx->append0(ctx, FN_APPEND_BR);
          ptr+=2;
          continue;
        }
        break;
      case L'<':
        if (ptr[1]==L'<') {
          if (ptr[2]==L'<') { // <<<placeholder>>>
            const wchar_t* start_of_placeholder=ptr+3;
            const wchar_t* end_of_placeholder=find_triple_delimiter(start_of_placeholder, L'>');
            if (end_of_placeholder) {
              FLUSH_TB();
              ptr=end_of_placeholder+3;
              ctx->append1(ctx, FN_APPEND_PLACEHOLDER, start_of_placeholder, end_of_placeholder-start_of_placeholder);
              continue;
            }
          }
        }
        break;
      case L'=': // heading trailer?
        if (item_ctx==ITEM_CTX_HEADER) {
          // check if it is at the end of line
          const wchar_t* p=ptr+1;
          while (*p==L'=') p++;
          SKIP_WS(p);
          if (!*p || *p==L'\n') { // yes, this is trailer
            while (tb_ptr>tb && tb_ptr[-1]<=L' ') tb_ptr--; // undo trailing spaces
            END_OF_BLOCK_CONTEXT(p);
          }
        }
        break;
      case L'~': // escape
      { // some escapes are dealt with on a block level
        wchar_t nc=ptr[1];
        if ((at_line_start && (IS_LIST_CHAR(nc) || nc==L'=' || nc==L'|' || nc=='{')) // these are escaped at line start only
            || (IS_FORMAT_CHAR(nc) && ptr[2]==nc) // these are escaped in pairs only
            || ((nc==L'{' || nc==L'[' || nc==L'\\' || nc==L'<' || nc==L'-') && ptr[2]==nc) // these are escaped in pairs only
            || nc==L'~') { // escape tilde itself
          // skip tilde and go ahead
          c=nc;
          ptr++;
        }
        break;
      }
      case L':': // http://... URL?
        // examine tb buffer for http
        if (tb_ptr>=tb+4 && ptr[-4]==L'h' && ptr[-3]==L't' && ptr[-2]==L't' && ptr[-1]==L'p' && ptr[1]==L'/' && ptr[2]==L'/') {
          // http:// prefix recognized
          if (tb_ptr>=tb+5 && ptr[-5]==L'~') { // but it is escaped
            // eat tilde
            memmove(tb_ptr-5, tb_ptr-4, 4*sizeof(wchar_t));
            tb_ptr--;
            // copy '://' straight into tb so it's not considered italics
            if (tb_ptr+3>=tb_end) FLUSH_TB();
            *tb_ptr++=L':'; *tb_ptr++=L'/'; *tb_ptr++=L'/';
            ptr+=3;
            continue;
          }
          else {
            const wchar_t* start_of_link=ptr-4;
            const wchar_t* end_of_link=ptr+3;
            while (IS_URL_CHAR(*end_of_link)) end_of_link++;
            while (wcschr(L",.;:?!%)", end_of_link[-1])) end_of_link--; // don't want these chars at the end of URI
            if (end_of_link>start_of_link+7) {
              tb_ptr-=4; // undo "http" from buffer
              FLUSH_TB();
              ptr=end_of_link;
              ctx->append1(ctx, FN_APPEND_LINK, start_of_link, end_of_link-start_of_link);
              continue;
            }
          }
        }
        break;
      case L'-': // -- dash?
        if (tb_ptr>tb && tb_ptr[-1]==L' ' && ptr[1]==L'-' && ptr[2]==L' ') {
          c=L'–'; // &ndash;
          ptr++; // skip one '-'
        }
        break;
    }

    *tb_ptr++=c;
    if (tb_ptr==tb_end) FLUSH_TB();
    ptr++;
  }
  assert(0); // not reachable
}

static const wchar_t* parse_table_row(nxcreole_parse_ctx* ctx, const wchar_t* ptr) {
  ctx->append0(ctx, FN_APPEND_TABLE_ROW_OPEN);
  for (;;) {
    assert(*ptr==L'|'); // points to opening '|' of the cell
    int colspan=1, th=0;
    while (*++ptr==L'|') colspan++;
    if (*ptr==L'=') {
      th=1;
      ptr++;
    }
    SKIP_WS(ptr);
    if (!*ptr) break; // eot
    if (*ptr==L'\n') { // eat last '|' on the line
      ptr++;
      break;
    }
    if (colspan>99) colspan=99;
    wchar_t cs[2];
    int cs_len;
    if (colspan<10) { cs[0]=L'0'+colspan; cs_len=1; }
    else { cs[0]=L'0'+colspan/10; cs[1]=L'0'+colspan%10; cs_len=2; }
    ctx->append1(ctx, th? FN_APPEND_TABLE_HEAD_CELL_OPEN:FN_APPEND_TABLE_CELL_OPEN, cs, (size_t)cs_len);
    end_of_context_t res=parse_item(ctx, ptr, &ptr, 0, ITEM_CTX_TABLE_CELL);
    ctx->append0(ctx, th? FN_APPEND_TABLE_HEAD_CELL_CLOSE:FN_APPEND_TABLE_CELL_CLOSE);
    if (res==END_OF_BLOCK) break;
  }
  ctx->append0(ctx, FN_APPEND_TABLE_ROW_CLOSE);
  return ptr;
}

static const wchar_t* parse_list_item(nxcreole_parse_ctx* ctx, const wchar_t* ptr) {
  SKIP_WS(ptr);
  if (*ptr==L'\n') { // empty line within list (blockquote/div/...)
    if (!ctx->blockquote_br) {
      ctx->append1(ctx, FN_APPEND_LIST_BLANK_ITEM, &ctx->list_levels[ctx->list_level], 1);
      ctx->blockquote_br=1;
    }
    return ptr+1;
  }
  else {
    ctx->blockquote_br=0;
    const wchar_t* end_ptr;
    parse_item(ctx, ptr, &end_ptr, 0, ITEM_CTX_LIST_ITEM);
    return end_ptr;
  }
}

static int parse_block(nxcreole_parse_ctx* ctx) {
  SKIP_WS(ctx->ptr);
  wchar_t c=*ctx->ptr;
  if (!c) return 0; // eot
  if (c==L'\n') { // blank line => end of list/table; no other meaning
    close_lists_and_tables(ctx);
    ctx->ptr++;
    return 1;
  }
  if (c==L'|') { // table
    if (ctx->mediawiki_table_level>0) {
      const wchar_t* p=ctx->ptr+1;
      wchar_t nc=*p;
      if (nc==L'-' || nc==L'}') p++;
      SKIP_WS(p);
      if (!*p) return 0; // table should auto-close on eot
      if (*p==L'\n') { // nothing else on the line => it's mediawiki-table markup
        close_lists_and_tables(ctx);
        ctx->append0(ctx, FN_APPEND_TABLE_CELL_CLOSE);
        wchar_t one=L'1';
        if (nc==L'-') { // next row
          ctx->append0(ctx, FN_APPEND_TABLE_ROW_CLOSE);
          ctx->append0(ctx, FN_APPEND_TABLE_ROW_OPEN);
          ctx->append1(ctx, FN_APPEND_TABLE_CELL_OPEN, &one, 1);
        }
        else if (nc==L'}') { // end of table
          ctx->append0(ctx, FN_APPEND_TABLE_ROW_CLOSE);
          ctx->append0(ctx, FN_APPEND_TABLE_CLOSE);
          ctx->mediawiki_table_level--;
        }
        else { // next cell
          ctx->append1(ctx, FN_APPEND_TABLE_CELL_OPEN, &one, 1);
        }
        ctx->ptr=p+1;
        return 1;
      }
    }
    if (!ctx->in_table) {
      close_lists_and_tables(ctx);
      ctx->append0(ctx, FN_APPEND_TABLE_OPEN);
      ctx->in_table=1;
    }
    ctx->ptr=parse_table_row(ctx, ctx->ptr);
    return 1;
  }
  else if (ctx->in_table) {
    close_lists_and_tables(ctx);
  }

  switch (c) {
    case L'=': // heading
      {
        int heading_level=1;
        while (ctx->ptr[heading_level]==L'=') heading_level++;
        ctx->ptr+=heading_level;
        SKIP_WS(ctx->ptr);
        if (!ctx->ptr[heading_level]) return 0; // eot
        wchar_t h=L'0'+heading_level;
        ctx->append1(ctx, FN_APPEND_HEADING_OPEN, &h, 1);
        parse_item(ctx, ctx->ptr, &ctx->ptr, 0, ITEM_CTX_HEADER);
        ctx->append1(ctx, FN_APPEND_HEADING_CLOSE, &h, 1);
        return 1;
      }
    case L'{': // nowiki block?
      if (ctx->ptr[1]==L'{' && ctx->ptr[2]==L'{') {
        const wchar_t* start_of_nowiki=ctx->ptr+3;
        const wchar_t* end_of_nowiki=find_end_of_nowiki(start_of_nowiki);
        const wchar_t* next_ptr=end_of_nowiki+3;
        if (end_of_nowiki) {
          wchar_t* newline=wcschr(start_of_nowiki, L'\n');
          if (newline && newline<end_of_nowiki) { // block <pre>
            SKIP_WS(start_of_nowiki);
            if (start_of_nowiki[0]==L'\n') start_of_nowiki++; // eat first newline
            if (end_of_nowiki[-1]==L'\n') end_of_nowiki--; // eat last newline
            if (end_of_nowiki>start_of_nowiki) { // non-empty
              append_nowiki(ctx, FN_APPEND_NOWIKI_BLOCK, start_of_nowiki, end_of_nowiki-start_of_nowiki);
            }
            ctx->ptr=next_ptr;
            return 1;
          }
          // else inline <nowiki> - proceed to regular paragraph handling
        }
      }
      else if (ctx->ptr[1]==L'|') { // mediawiki-table?
        const wchar_t* p=ctx->ptr+2;
        SKIP_WS(p);
        // if (!*p) ... // no point in opening table on eot => treat literally
        if (*p==L'\n') { // yes, it's start of a table
          wchar_t one=L'1';
          ctx->append0(ctx, FN_APPEND_TABLE_OPEN);
          ctx->append0(ctx, FN_APPEND_TABLE_ROW_OPEN);
          ctx->append1(ctx, FN_APPEND_TABLE_CELL_OPEN, &one, 1);
          ctx->mediawiki_table_level++;
          ctx->ptr=p+1;
          return 1;
        }
      }
      break;
    case L'-': // hr?
      if (ctx->ptr[1]==L'-' && ctx->ptr[2]==L'-' && ctx->ptr[3]==L'-') {
        const wchar_t* p=ctx->ptr+4;
        SKIP_WS(p);
        if (!*p || *p==L'\n') { // yes, it's <hr>
          ctx->append0(ctx, FN_APPEND_HR);
          ctx->ptr=p;
          return 1;
        }
      }
      break;
    case L'~': // block-level escaping
      {
        wchar_t nc=ctx->ptr[1];
        if (IS_LIST_CHAR(nc) || nc==L'=' || nc==L'|' || nc==L'{') {
          ctx->ptr++; // skip '~' and proceed to regular paragraph handling
        }
        // otherwise escaping will be done at line level
      }
      break;
  }

  if (ctx->list_level>=0 || IS_LIST_CHAR(c)) { // lists
    int lc;
    // count list level
    for (lc=0; lc<=ctx->list_level && ctx->ptr[lc]==ctx->list_levels[lc]; lc++);
    if (!ctx->ptr[lc]) return 0; // eot
    if (lc<=ctx->list_level) { // close list block(s)
      do {
        ctx->append1(ctx, FN_APPEND_LIST_CLOSE, &ctx->list_levels[ctx->list_level--], 1);
      } while (lc<=ctx->list_level);
      // list(s) closed => retry from the same position
      ctx->blockquote_br=1;
      return 1;
    }
    else {
      wchar_t cc=ctx->ptr[lc];
      if (IS_LIST_CHAR(cc) && ctx->ptr[lc+1]!=cc /* not formatting chars */ && ctx->list_level<MAX_LIST_LEVELS) {
        // new list block
        ctx->list_levels[++ctx->list_level]=cc;
        ctx->blockquote_br=1;
        ctx->append1(ctx, FN_APPEND_LIST_OPEN, &cc, 1);
        ctx->ptr=parse_list_item(ctx, ctx->ptr+lc+1);
        return 1;
      }
      else if (ctx->list_level>=0) { // list item - same level
        ctx->append1(ctx, FN_APPEND_LIST_NEXT_ITEM, &ctx->list_levels[ctx->list_level], 1);
        ctx->ptr=parse_list_item(ctx, ctx->ptr+lc);
        return 1;
      }
    }
  }

  { // paragraph handling
    ctx->append0(ctx, FN_APPEND_PARAGRAPH_OPEN);
    parse_item(ctx, ctx->ptr, &ctx->ptr, 0, ITEM_CTX_PARAGRAPH);
    ctx->append0(ctx, FN_APPEND_PARAGRAPH_CLOSE);
    return 1;
  }
}

void nxcreole_init(nxcreole_parse_ctx* ctx, const wchar_t* text) {
  memset(ctx, 0, sizeof(nxcreole_parse_ctx));
  ctx->ptr=text;
  ctx->list_level=-1;
}

void nxcreole_parse(nxcreole_parse_ctx* ctx) {

  while (parse_block(ctx));

  close_lists_and_tables(ctx);

  while (ctx->mediawiki_table_level-->0) {
    // append("</td></tr></table>\n");
    ctx->append0(ctx, FN_APPEND_TABLE_CELL_CLOSE);
    ctx->append0(ctx, FN_APPEND_TABLE_ROW_CLOSE);
    ctx->append0(ctx, FN_APPEND_TABLE_CLOSE);
  }
}
