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

#include <stddef.h>
#include <stdio.h>
#include <wchar.h>
#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <locale.h>

#include "nxcreole_parser.h"

#define ERROR(msg, p) fprintf(stderr, "ERROR: " msg " %s\n", (p));

static char* out;

static char* unicode2utf8(const wchar_t* text, size_t length, char* b) {
  while (length--) {
    unsigned int c=(unsigned int)*text++;
    if (c<0x80) *b++=(char)c;
    else if (c<0x800) *b++=(char)(192+c/64), *b++=(char)(128+c%64);
    else if (c-0xd800u<0x800) goto error;
    else if (c<0x10000) *b++=(char)(224+c/4096), *b++=(char)(128+c/64%64), *b++=(char)(128+c%64);
    else if (c<0x110000) *b++=(char)(240+c/262144), *b++=(char)(128+c/4096%64), *b++=(char)(128+c/64%64), *b++=(char)(128+c%64);
    else goto error;
  }
  return b;
  error:
  fprintf(stderr, "invalid unicode code point\n");
  return b;
}

static void print_unicode(const wchar_t* text, size_t length) {
/*
  char buf[length*4], *end;
  end=unicode2utf8(text, length, buf);
  fwrite(buf, 1, end-buf, stdout);
*/
  out=unicode2utf8(text, length, out);
}

static void print_unicode_sz(const wchar_t* text) {
  print_unicode(text, wcslen(text));
}

static void print_unicode_html(const wchar_t* text, size_t length) {
  while (length--) {
    wchar_t c=*text++;
    switch (c) {
      case L'<':
        print_unicode_sz(L"&lt;");
        break;
      case L'>':
        print_unicode_sz(L"&gt;");
        break;
      case L'"':
        print_unicode_sz(L"&quot;");
        break;
      case L'\'':
        print_unicode_sz(L"&#39;");
        break;
      case L'&':
        print_unicode_sz(L"&amp;");
        break;
      default:
        print_unicode(&c, 1);
        break;
    }
  }
}

static void append_text(const wchar_t* s, size_t len) {
  // fprintf(stderr, "append_text %d wchars\n", (int)len);
  print_unicode_html(s, len);
}

static void append_table_open() {
  print_unicode_sz(L"<table>");
}

static void append_table_row_open() {
  print_unicode_sz(L"<tr>");
}

static void append_table_head_cell_open(const wchar_t* s, size_t len) {
  if (len==1 && *s==L'1') {
    print_unicode_sz(L"<th>");
  }
  else {
    print_unicode_sz(L"<th colspan=\"");
    print_unicode(s, len);
    print_unicode_sz(L"\">");
  }
}

static void append_table_head_cell_close() {
  print_unicode_sz(L"</th>");
}

static void append_table_cell_open(const wchar_t* s, size_t len) {
  if (len==1 && *s==L'1') {
    print_unicode_sz(L"<td>");
  }
  else {
    print_unicode_sz(L"<td colspan=\"");
    print_unicode(s, len);
    print_unicode_sz(L"\">");
  }
}

static void append_table_cell_close() {
  print_unicode_sz(L"</td>");
}

static void append_table_row_close() {
  print_unicode_sz(L"</tr>");
}

static void append_table_close() {
  print_unicode_sz(L"</table>");
}

static void append_list_open(const wchar_t* s, size_t len) {
  const wchar_t* r=L"?";
  assert(len==1);
  switch (*s) {
    case L'*': r=L"<ul><li>"; break;
    case L'-': r=L"<ul><li>"; break;
    case L'#': r=L"<ol><li>"; break;
    case L'>': r=L"<blockquote>"; break;
    case L':': r=L"<div class=\"indent\">"; break;
    case L'!': r=L"<div class=\"center\">"; break;
  }
  print_unicode_sz(r);
}

static void append_list_next_item(const wchar_t* s, size_t len) {
  const wchar_t* r=0;
  assert(len==1);
  switch (*s) {
    case L'*': r=L"</li>\n<li>"; break;
    case L'-': r=L"</li>\n<li>"; break;
    case L'#': r=L"</li>\n<li>"; break;
    case L'>': r=0; break;
    case L':': r=0; break;
    case L'!': r=L"</div>\n<div class=\"center\">"; break;
  }
  if (r) print_unicode_sz(r);
}

static void append_list_blank_item(const wchar_t* s, size_t len) {
  const wchar_t* r=0;
  assert(len==1);
  switch (*s) {
    case L'*': r=L"&nbsp;"; break;
    case L'-': r=L"&nbsp;"; break;
    case L'#': r=L"&nbsp;"; break;
    case L'>': r=L"<br/><br/>\n"; break;
    case L':': r=L"<br/><br/>\n"; break;
    case L'!': r=L"&nbsp;"; break;
  }
  if (r) print_unicode_sz(r);
}

static void append_list_close(const wchar_t* s, size_t len) {
  const wchar_t* r=0;
  assert(len==1);
  switch (*s) {
    case L'*': r=L"</li></ul>\n"; break;
    case L'-': r=L"</li></ul>\n"; break;
    case L'#': r=L"</li></ol>\n"; break;
    case L'>': r=L"</blockquote>\n"; break;
    case L':': r=L"</div>\n"; break;
    case L'!': r=L"</div>\n"; break;
  }
  if (r) print_unicode_sz(r);
}

static void append_paragraph_open() {
  print_unicode_sz(L"<p>");
}

static void append_paragraph_close() {
  print_unicode_sz(L"</p>\n");
}

static void append_heading_open(const wchar_t* s, size_t len) {
  print_unicode_sz(L"<h");
  print_unicode(s, len);
  print_unicode_sz(L">");
}

static void append_heading_close(const wchar_t* s, size_t len) {
  print_unicode_sz(L"</h");
  print_unicode(s, len);
  print_unicode_sz(L">\n");
}

static void append_format_open(const wchar_t* s, size_t len) {
  const wchar_t* r=0;
  assert(len==1);
  switch (*s) {
    case L'*': r=L"<strong>"; break;
    case L'/': r=L"<em>"; break;
    case L'_': r=L"<span class=\"underline\">"; break;
    case L'#': r=L"<code>"; break;
  }
  if (r) print_unicode_sz(r);
}

static void append_format_close(const wchar_t* s, size_t len) {
  const wchar_t* r=0;
  assert(len==1);
  switch (*s) {
    case L'*': r=L"</strong>"; break;
    case L'/': r=L"</em>"; break;
    case L'_': r=L"</span>"; break;
    case L'#': r=L"</code>"; break;
  }
  if (r) print_unicode_sz(r);
}

static void append_hr() {
  print_unicode_sz(L"\n<hr/>\n");
}

static void append_br() {
  print_unicode_sz(L"<br/>\n");
}

static void append_nowiki_block(const wchar_t* s, size_t len) {
  print_unicode_sz(L"<pre>");
  print_unicode_html(s, len);
  print_unicode_sz(L"</pre>\n");
}

static void append_nowiki_inline(const wchar_t* s, size_t len) {
  print_unicode_sz(L"<span class=\"nowiki\">");
  print_unicode_html(s, len);
  print_unicode_sz(L"</span>");
}

static void append_image(const wchar_t* s, size_t len) {
  const wchar_t* title=wcschr(s, L'|');
  if (title-s>=len) title=0;
  print_unicode_sz(L"<img src=\"");
  print_unicode_html(s, title?title-s : len);
  print_unicode_sz(L"\"");
  if (title) {
    print_unicode_sz(L" alt=\"");
    print_unicode_html(title+1, len-(title-s)-1);
    print_unicode_sz(L"\"");
  }
  print_unicode_sz(L" />");
}

static void append_link(const wchar_t* s, size_t len) {
  const wchar_t* title=wcschr(s, L'|');
  if (title-s>=len) title=0;
  print_unicode_sz(L"<a href=\"");
  print_unicode_html(s, title?title-s : len);
  print_unicode_sz(L"\">");
  if (title)
    print_unicode_html(title+1, len-(title-s)-1);
  else
    print_unicode_html(s, len);
  print_unicode_sz(L"</a>");
}

static void append_placeholder(const wchar_t* s, size_t len) {
  print_unicode_sz(L"&lt;&lt;&lt;Placeholder:");
  print_unicode_html(s, len);
  print_unicode_sz(L"&gt;&gt;&gt;");
}


typedef void (*append0_sig)(void);
typedef void (*append1_sig)(const wchar_t* s, size_t len);

static void append0(nxcreole_parse_ctx* ctx, nxcreole_fn_id_t fn) {
  ((append0_sig)ctx->fn[fn])();
}

static void append1(nxcreole_parse_ctx* ctx, nxcreole_fn_id_t fn, const wchar_t* s, size_t len) {
  ((append1_sig)ctx->fn[fn])(s, len);
}

static void* fns[FN_COUNT]={
    &append_text,
    &append_table_open,
    &append_table_row_open,
    &append_table_head_cell_open,
    &append_table_head_cell_close,
    &append_table_cell_open,
    &append_table_cell_close,
    &append_table_row_close,
    &append_table_close,
    &append_list_open,
    &append_list_next_item,
    &append_list_blank_item,
    &append_list_close,
    &append_paragraph_open,
    &append_paragraph_close,
    &append_heading_open,
    &append_heading_close,
    &append_format_open,
    &append_format_close,
    &append_hr,
    &append_br,
    &append_nowiki_block,
    &append_nowiki_inline,
    &append_image,
    &append_link,
    &append_placeholder,
};

int render_xhtml(const char* input) {
  size_t text_len=mbstowcs(0, input, 0);
  if (text_len==(size_t)-1) {
    fprintf(stderr, "invalid input multi-byte string\n");
    return 0;
  }
  wchar_t* text=malloc((text_len+1)*sizeof(wchar_t));
  if ((size_t)-1==mbstowcs(text, input, (size_t)text_len+1)) {
    fprintf(stderr, "invalid input multi-byte string (2)\n");
    free(text);
    return 0;
  }

  nxcreole_parse_ctx ctx;

  nxcreole_init(&ctx, text);
  ctx.append0=append0;
  ctx.append1=append1;
  memcpy(ctx.fn, fns, sizeof(ctx.fn));
  nxcreole_parse(&ctx);

  free(text);
  return 0;
}

static char* load_file(const char* filepath) {
  struct stat st;
  if (stat(filepath, &st)==-1) {
    // ERROR("can't find file", filepath);
    return 0;
  }
  int fd=open(filepath, O_RDONLY);
  if (fd==-1) {
    ERROR("can't open file", filepath);
    return 0;
  }
  char* text=malloc((size_t)(st.st_size+1));
  if (st.st_size!=read(fd, text, (size_t)st.st_size)) {
    ERROR("can't read file", filepath);
    close(fd);
    free(text);
    return 0;
  }
  close(fd);
  text[st.st_size]='\0';
  return text;
}

static int save_file(const char* filepath, const char* text) {
  int fd=open(filepath, O_WRONLY|O_CREAT|O_TRUNC, 0644);
  if (fd==-1) {
    ERROR("can't open file", filepath);
    return -1;
  }
  size_t length=strlen(text);
  if (length!=write(fd, text, length)) {
    ERROR("can't write file", filepath);
    close(fd);
    return -1;
  }
  close(fd);
  return 0;
}

static int run_test(int test_number, char* input, const char* expected_output) {
  size_t input_length=strlen(input);
  char* buf=malloc(input_length*32+40000); // hope this will be large enough
  out=buf;
  render_xhtml(input);
  *out='\0';

  char fname[32];
  sprintf(fname, "tests/%03d.html", test_number);
  save_file(fname, buf);

  if (expected_output && !strcmp(buf, expected_output)) {
    printf("[%03d] PASSED\n", test_number);
    free(buf);
    return 1;
  }
  else {
    printf("[%03d] FAILED\n", test_number);
    free(buf);
    return 0;
  }
}

static void run_tests() {
  char infile[32];
  char expfile[32];
  int i, total=0, passed=0;
  for (i=1; i<100; i++) {
    sprintf(infile, "tests/%03d.creole", i);
    sprintf(expfile, "tests/%03d.expected", i);
    char* input=load_file(infile);
    if (!input) break;
    char* expected_output=load_file(expfile);
    passed+=run_test(i, input, expected_output);
    total++;
    free(input);
    if (expected_output) free(expected_output);
  }
  printf("\nPASSED %d OUT OF %d\n", passed, total);
}

int main() {
  if (!setlocale(LC_CTYPE, "en_US.UTF-8")) {
    perror("setlocale");
    exit(EXIT_FAILURE);
  }
  run_tests();
  return 0;
}
