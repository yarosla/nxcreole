# coding=utf-8

import StringIO, time, gc
from nxcreole import CreoleParser, render_xhtml
from nxcreole import html_escape

# NOTE: run this script from project root directory:
#       python tests/nxcreole_test.py

PATH_TO_TESTS='tests/'

def file_read(fname):
  try:
    with open(fname, 'r') as f:
      return f.read().decode('utf-8')
  except:
    return None

def file_write(fname, content):
  with open(fname, 'w') as f:
    f.write(content.encode('utf-8'))

def run_all_tests():
  for i in xrange(1, 100):
    text=file_read(PATH_TO_TESTS+'%03d.creole' % i)
    if text is None:
      break
    expected=file_read(PATH_TO_TESTS+'%03d.expected' % i)

    out=StringIO.StringIO()
    parser=CreoleParser(out)
    parser.parse(text)
    result=out.getvalue()

    file_write((PATH_TO_TESTS+'%03d.htm' % i), result)
    if result==expected:
      print '%03d PASSED' % i
    else:
      print '%03d FAILED' % i

def long_run(num_iterations):
  # C version is 30 times faster than https://pypi.python.org/pypi/creole in this test
  text=file_read(PATH_TO_TESTS+'006.creole')
  if text is None:
    return
  expected=file_read(PATH_TO_TESTS+'006.expected')
  tm1=time.time()
  for count in xrange(num_iterations):
    result=render_xhtml(text)
    if result!=expected:
      print '%d FAILED' % count
    if not count%10000:
      gc.collect()
      print '%d: %d objects' % (count, len(gc.get_objects()))
  tm2=time.time()
  print 'Completed %d iterations in %.3f seconds' % (num_iterations, tm2-tm1)

def _html_escape(s):
  if s is None: return ''
  return unicode(s).replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;').replace('"', '&quot;').replace('\'', '&#39;')

def test_html_escape(num_iterations):
  sample=u'<a>"'*1000 # C version is 2.4 times faster in this test
  expected=u'&lt;a&gt;&quot;'*1000
  tm1=time.time()
  for count in xrange(num_iterations):
    result=html_escape(sample)
    if result!=expected:
      print '%d FAILED' % count
  tm2=time.time()
  print 'Completed %d iterations in %.3f seconds' % (num_iterations, tm2-tm1)

#test_html_escape(500000)
#long_run(50000)
run_all_tests()
