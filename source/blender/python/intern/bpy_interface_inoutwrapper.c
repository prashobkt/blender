/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

/** \file
 * \ingroup pythonintern
 *
 * This file wraps around pythons stdin stdout and exposes it in buffer
 */

#include <BLI_dynstr.h>
#include <Python.h>

#include "MEM_guardedalloc.h"

#include "CLG_log.h"

#include "BLI_fileops.h"
#include "BLI_listbase.h"
#include "BLI_path_util.h"
#include "BLI_string.h"
#include "BLI_string_utf8.h"
#include "BLI_threads.h"
#include "BLI_utildefines.h"

#include "RNA_types.h"

#include "bpy.h"
#include "bpy_capi_utils.h"
#include "bpy_intern_string.h"
#include "bpy_path.h"
#include "bpy_rna.h"
#include "bpy_traceback.h"

#include "bpy_app_translations.h"

#include "DNA_text_types.h"

#include "BKE_appdir.h"
#include "BKE_context.h"
#include "BKE_global.h" /* only for script checking */
#include "BKE_main.h"
#include "BKE_text.h"
#include "bpy_interface_inoutwrapper.h"

// todo avoid single global buffer
static DynStr *io_buffer = NULL;

// Internal state
PyObject *g_stdout;
PyObject *g_stdout_saved;
PyObject *g_stderr;
PyObject *g_stderr_saved;

// instead of printing to stdout, use this function
typedef size_t (*print_handle)(char *);

typedef struct InOutWrapper {
  PyObject_HEAD print_handle write;
} InOutWrapper;

static PyObject *Stdout_write(PyObject *self, PyObject *args)
{
  size_t written = 0;
  InOutWrapper *selfimpl = (InOutWrapper *)(self);
  if (selfimpl->write) {
    char *data;
    if (!PyArg_ParseTuple(args, "s", &data))
      return 0;

    written = selfimpl->write(data);
  }
  return PyLong_FromSize_t(written);
}

static PyObject *Stdout_flush(PyObject *UNUSED(self), PyObject *UNUSED(args))
{
  // no-op
  return Py_BuildValue("");
}

PyMethodDef InOut_methods[] = {
    {"write", Stdout_write, METH_VARARGS, "sys.stdout.write"},
    {"flush", Stdout_flush, METH_VARARGS, "sys.stdout.write"},
    {0, 0, 0, 0}  // sentinel
};

PyTypeObject InOutHandlerType = {
    PyVarObject_HEAD_INIT(0, 0) "InOutHandlerType", /* tp_name */
    sizeof(InOutWrapper),                           /* tp_basicsize */
    0,                                              /* tp_itemsize */
    0,                                              /* tp_dealloc */
    0,                                              /* tp_print */
    0,                                              /* tp_getattr */
    0,                                              /* tp_setattr */
    0,                                              /* tp_reserved */
    0,                                              /* tp_repr */
    0,                                              /* tp_as_number */
    0,                                              /* tp_as_sequence */
    0,                                              /* tp_as_mapping */
    0,                                              /* tp_hash  */
    0,                                              /* tp_call */
    0,                                              /* tp_str */
    0,                                              /* tp_getattro */
    0,                                              /* tp_setattro */
    0,                                              /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT,                             /* tp_flags */
    "InOutWrapper objects",                         /* tp_doc */
    0,                                              /* tp_traverse */
    0,                                              /* tp_clear */
    0,                                              /* tp_richcompare */
    0,                                              /* tp_weaklistoffset */
    0,                                              /* tp_iter */
    0,                                              /* tp_iternext */
    InOut_methods,                                  /* tp_methods */
};

PyModuleDef in_out_wrapper_module = {
    PyModuleDef_HEAD_INIT,
    "_in_out_wrapper",
    0,
    -1,
    0,
};

PyMODINIT_FUNC PyInit_in_out_wrapper(void)
{
  g_stdout = NULL;
  g_stdout_saved = NULL;

  InOutHandlerType.tp_new = PyType_GenericNew;
  if (PyType_Ready(&InOutHandlerType) < 0)
    return 0;

  PyObject *m = PyModule_Create(&in_out_wrapper_module);
  if (m) {
    Py_INCREF(&InOutHandlerType);
    PyModule_AddObject(m, "InOutWrapper", (PyObject *)(&InOutHandlerType));
  }
  return m;
}

static void set_stdout(print_handle write)
{
  if (!g_stdout) {
    g_stdout_saved = PySys_GetObject("stdout");  // borrowed
    g_stdout = InOutHandlerType.tp_new(&InOutHandlerType, NULL, NULL);
  }

  InOutWrapper *impl = (InOutWrapper *)(g_stdout);
  impl->write = write;
  PySys_SetObject("stdout", g_stdout);
}

static void set_stderr(print_handle write)
{
  if (!g_stderr) {
    g_stderr_saved = PySys_GetObject("stderr");  // borrowed
    g_stderr = InOutHandlerType.tp_new(&InOutHandlerType, NULL, NULL);
  }

  InOutWrapper *impl = (InOutWrapper *)(g_stderr);
  impl->write = write;
  PySys_SetObject("stderr", g_stdout);
}

// todo investigate if not calling reset causes memory leak
static void reset_stdout(void)
{
  if (g_stdout_saved)
    PySys_SetObject("stdout", g_stdout_saved);

  Py_XDECREF(g_stdout);
  g_stdout = 0;
}

static void reset_stderr(void)
{
  if (g_stdout_saved)
    PySys_SetObject("stderr", g_stderr_saved);

  Py_XDECREF(g_stdout);
  g_stdout = 0;
}

// todo there is no use for returning written bytes
static size_t custom_write(char *input)
{
  int len = BLI_dynstr_get_len(io_buffer);
  BLI_dynstr_append(io_buffer, input);
  printf("custom write> %s", input);
  return BLI_dynstr_get_len(io_buffer) - len;
}

// todo investigate possible conflicts with BPy_reports_write_stdout
/* use it anywhere after Py_Initialize */
void BPY_intern_init_inoutwrapper()
{
  PyImport_ImportModule("_inoutwrapper");
  BLI_assert(io_buffer == NULL);
  io_buffer = BLI_dynstr_new();

  // switch sys.stdout to custom handler
  print_handle stdout_write_hadle = custom_write;
  set_stdout(stdout_write_hadle);
  set_stderr(stdout_write_hadle);
}

char *BPY_intern_get_inout_buffer()
{
  BLI_assert(io_buffer != NULL);
  return BLI_dynstr_get_cstring(io_buffer);
}

void BPY_intern_free_inoutwrapper()
{
  BLI_assert(io_buffer != NULL);
  BLI_dynstr_free(io_buffer);
  io_buffer = NULL;
  reset_stderr();
  reset_stdout();
}
