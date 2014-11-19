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

#include <assert.h>
#include <stdio.h>
#include <wchar.h>
#include <Python.h>

#include "nxcreole_parser.h"

const char* fn_names[]={
  "append_text",
  "append_table_open",
  "append_table_row_open",
  "append_table_head_cell_open",
  "append_table_head_cell_close",
  "append_table_cell_open",
  "append_table_cell_close",
  "append_table_row_close",
  "append_table_close",
  "append_list_open",
  "append_list_next_item",
  "append_list_blank_item",
  "append_list_close",
  "append_paragraph_open",
  "append_paragraph_close",
  "append_heading_open",
  "append_heading_close",
  "append_format_open",
  "append_format_close",
  "append_hr",
  "append_br",
  "append_nowiki_block",
  "append_nowiki_inline",
  "append_image",
  "append_link",
  "append_placeholder",
};

static void append0(nxcreole_parse_ctx* ctx, nxcreole_fn_id_t fn_id) {
  PyObject* fn=ctx->fn[fn_id];
  PyObject* res=PyObject_CallObject(fn, NULL);
  PyErr_Print();
  Py_DECREF(res);
}

static void append1(nxcreole_parse_ctx* ctx, nxcreole_fn_id_t fn_id, const wchar_t* s, size_t len) {
  PyObject* fn=ctx->fn[fn_id];
  PyObject* arg=PyUnicode_FromWideChar(s, len);
  PyObject* call_args=PyTuple_Pack(1, arg);
  PyObject* res=PyObject_CallObject(fn, call_args);
  PyErr_Print();
  Py_DECREF(call_args);
  Py_DECREF(arg);
  Py_DECREF(res);
}

static PyObject* get_fn_attr(PyObject* o, const char* name) {
  PyObject* fn=PyObject_GetAttrString(o, name);
  if (!PyCallable_Check(fn)) {
    PyErr_Format(PyExc_TypeError, "method %s not defined", name);
    return NULL;
  }
  return fn;
}

static int init_fns(PyObject* serializer, nxcreole_parse_ctx* ctx) {
  int i;
  for (i=0; i<FN_COUNT; i++) {
    if (!(ctx->fn[i]=get_fn_attr(serializer, fn_names[i]))) return -1;
  }
  return 0;
}

static void finalize_fns(nxcreole_parse_ctx* ctx) {
  int i;
  for (i=0; i<FN_COUNT; i++) {
    if (ctx->fn[i]) Py_DECREF(ctx->fn[i]);
  }
}

static PyObject* parse(PyObject *ignored, PyObject *args)
{
  PyObject* serializer;
  PyObject* text;

  if (!PyArg_UnpackTuple(args, "parse", 2, 2, &serializer, &text)
      || /*!PyObject_Check(serializer) ||*/ !PyUnicode_Check(text)) {
    PyErr_SetString(PyExc_TypeError, "parse() expects object and unicode string as arguments");
    return NULL;
  }

  const wchar_t* text_ptr=(const wchar_t*)PyUnicode_AS_UNICODE(text);
  nxcreole_parse_ctx ctx;
  nxcreole_init(&ctx, text_ptr);
  ctx.append0=append0;
  ctx.append1=append1;
  if (init_fns(serializer, &ctx)) return NULL;

  nxcreole_parse(&ctx);

  // deinit
  finalize_fns(&ctx);

  Py_RETURN_NONE;
}

static PyObject* html_escape(PyObject *ignored, PyObject *args) {
  PyObject* text;
  if (!PyArg_UnpackTuple(args, "html_escape", 1, 1, &text)
      || !PyUnicode_Check(text)) {
    PyErr_SetString(PyExc_TypeError, "html_escape() expects unicode string as argument");
    return NULL;
  }

  const wchar_t* src=(const wchar_t*)PyUnicode_AS_UNICODE(text);
  Py_ssize_t length=PyUnicode_GET_SIZE(text);
  Py_ssize_t buf_size=length+30;
  PyObject* result=PyUnicode_FromUnicode(NULL, buf_size);
  Py_ssize_t space_left=buf_size;
  wchar_t* dst=(wchar_t*)PyUnicode_AS_UNICODE(result);
  wchar_t c;
  while (length--) {
    if (space_left<6) { // extend buffer
      buf_size+=30;
      space_left+=30;
      if (PyUnicode_Resize(&result, buf_size)==-1) return NULL;
      // PyUnicode_Resize() could have moved dst in memory
      dst=(wchar_t*)PyUnicode_AS_UNICODE(result)+buf_size-space_left;
    }
    c=*src++;
    switch (c) {
      case L'<':
        *dst++=L'&', *dst++=L'l', *dst++=L't', *dst++=L';';
        space_left-=4;
        break;
      case L'>':
        *dst++=L'&', *dst++=L'g', *dst++=L't', *dst++=L';';
        space_left-=4;
        break;
      case L'"':
        *dst++=L'&', *dst++=L'q', *dst++=L'u', *dst++=L'o', *dst++=L't', *dst++=L';';
        space_left-=6;
        break;
      case L'\'':
        *dst++=L'&', *dst++=L'#', *dst++=L'3', *dst++=L'9', *dst++=L';';
        space_left-=5;
        break;
      case L'&':
        *dst++=L'&', *dst++=L'a', *dst++=L'm', *dst++=L'p', *dst++=L';';
        space_left-=5;
        break;
      default:
        *dst++=c;
        space_left-=1;
        break;
    }
  }
  if (PyUnicode_Resize(&result, buf_size-space_left)==-1) return NULL; // set result size
  return result;
}

static PyMethodDef nxcreole_ext_methods[] =
{
  {"parse", parse, METH_VARARGS, "Parse wiki text."},
  {"html_escape", html_escape, METH_VARARGS, "Escape HTML characters."},
  {NULL, NULL, 0, NULL}
};

PyMODINIT_FUNC init_ext(void)
{
  assert(sizeof(Py_UNICODE)==sizeof(wchar_t));
  (void)Py_InitModule("_ext", nxcreole_ext_methods);
}
