# Copyright (c) 2014 Yaroslav Stavnichiy <yarosla@gmail.com>
#
# This file is part of NXCREOLE.
#
# NXCREOLE is free software: you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License
# as published by the Free Software Foundation, either version 3
# of the License, or (at your option) any later version.
#
# NXCREOLE is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with NXCREOLE. If not, see <http://www.gnu.org/licenses/>.

import StringIO
import nxcreole._ext


html_escape=nxcreole._ext.html_escape


class CreoleParser(object):
  """
  Parser interface. Usage:

    parser = CreoleParser(output_file_or_stringio)
    parser.parse(wiki_text)

  CreoleParser is also a base class for custom serializers.
  Just subclass it and override required methods.
  """

  def __init__(self, out):
    """
    Initialize parser, set output file or StringIO object.
    """
    self.out=out

  def parse(self, text):
    """
    Parse provided wiki text. append_* methods will be called to generate output.
    """
    return nxcreole._ext.parse(self, text)

  def append_text(self, s):
    self.out.write(html_escape(s))

  def append_table_open(self):
    self.out.write(u'<table>')

  def append_table_row_open(self):
    self.out.write(u'<tr>')

  def append_table_head_cell_open(self, colspan):
    if colspan!='1':
      self.out.write(u'<th colspan="'+colspan+'">')
    else:
      self.out.write(u'<th>')

  def append_table_head_cell_close(self):
    self.out.write(u'</th>')

  def append_table_cell_open(self, colspan):
    if colspan!='1':
      self.out.write(u'<td colspan="'+colspan+'">')
    else:
      self.out.write(u'<td>')

  def append_table_cell_close(self):
    self.out.write(u'</td>')

  def append_table_row_close(self):
    self.out.write(u'</tr>')

  def append_table_close(self):
    self.out.write(u'</table>')

  def append_list_open(self, s):
    self.out.write({
      '*':u'<ul><li>',
      '-':u'<ul><li>',
      '#':u'<ol><li>',
      '>':u'<blockquote>',
      ':':u'<div class="indent">',
      '!':u'<div class="center">',
      }.get(s))

  def append_list_next_item(self, s):
    self.out.write({
      '*':u'</li>\n<li>',
      '-':u'</li>\n<li>',
      '#':u'</li>\n<li>',
      '>':u'',
      ':':u'',
      '!':u'</div>\n<div class="center">',
      }.get(s))

  def append_list_blank_item(self, s):
    self.out.write({
      '*':u'&nbsp;',
      '-':u'&nbsp;',
      '#':u'&nbsp;',
      '>':u'<br/><br/>\n',
      ':':u'<br/><br/>\n',
      '!':u'&nbsp;',
      }.get(s))

  def append_list_close(self, s):
    self.out.write({
      '*':u'</li></ul>\n',
      '-':u'</li></ul>\n',
      '#':u'</li></ol>\n',
      '>':u'</blockquote>\n',
      ':':u'</div>\n',
      '!':u'</div>\n',
      }.get(s))

  def append_paragraph_open(self):
    self.out.write(u'<p>')

  def append_paragraph_close(self):
    self.out.write(u'</p>\n')

  def append_heading_open(self, s):
    self.out.write(u'<h'+s+u'>')

  def append_heading_close(self, s):
    self.out.write(u'</h'+s+u'>\n')

  def append_format_open(self, s):
    self.out.write({
      '*':u'<strong>',
      '/':u'<em>',
      '_':u'<span class="underline">',
      '#':u'<code>',
      }.get(s))

  def append_format_close(self, s):
    self.out.write({
      '*':u'</strong>',
      '/':u'</em>',
      '_':u'</span>',
      '#':u'</code>',
      }.get(s))

  def append_hr(self):
    self.out.write(u'\n<hr/>\n')

  def append_br(self):
    self.out.write(u'<br/>\n')

  def append_nowiki_block(self, s):
    self.out.write(u'<pre>'+html_escape(s)+u'</pre>\n')

  def append_nowiki_inline(self, s):
    self.out.write(u'<span class="nowiki">'+html_escape(s)+u'</span>')

  def append_image(self, s):
    pair=s.split('|', 1)
    src, title=pair if len(pair)==2 else pair+[None]
    self.out.write(u'<img src="'+html_escape(src)+'"')
    if title: self.out.write(u' alt="'+html_escape(title)+u'"')
    self.out.write(u' />')

  def append_link(self, s):
    pair=s.split('|', 1)
    href, title=pair if len(pair)==2 else pair+[None]
    self.out.write(u'<a href="'+html_escape(href)+'">')
    self.out.write(html_escape(title) if title else html_escape(href))
    self.out.write(u'</a>')

  def append_placeholder(self, s):
    self.out.write(html_escape(u'<<<Placeholder:'+s+u'>>>'))


def render_xhtml(text):
  """
  Shortcut method to process wiki text and return serialized XHTML string.
  """
  out=StringIO.StringIO()
  parser=CreoleParser(out)
  parser.parse(text)
  return out.getvalue()
