#include <Python.h>

#if PY_MAJOR_VERSION >= 3
#define IF_PY3(three, two) (three)
#else
#define IF_PY3(three, two) (two)
#endif

#define MAX(x, y) (((x) > (y)) ? (x) : (y))

static PyObject* NotFound;
static PyObject* _builtins_module;


static inline PyObject* _ns_lookup(char* key, PyObject* ns) {
    PyObject* ret = NULL;

    if ((ret = PyMapping_GetItemString(ns, key))) {
        return ret;
    }

    PyErr_Clear();

    {
        PyObject* fmt = PyUnicode_FromString("Cannot find '{}'");
        PyObject* fmted = PyObject_CallMethod(
            fmt, "format", IF_PY3("y", "s"), key
        );
        PyErr_SetObject(NotFound, fmted);
        Py_XDECREF(fmted);
        Py_XDECREF(fmt);
    }
    return NULL;
}


static inline PyObject* _self_lookup(char* key, PyObject* selfobj) {
    PyObject* ret = NULL;

    if ((ret = PyObject_GetAttrString(selfobj, key))) {
        return ret;
    }

    PyErr_Clear();

    return ret;
}


static inline PyObject* _frame_lookup(char* key, PyObject* locals, PyObject* globals) {
    PyObject* ret = NULL;

    if ((ret = PyMapping_GetItemString(locals, key))) {
        return ret;
    }

    PyErr_Clear();

    if ((ret = PyMapping_GetItemString(globals, key))) {
        return ret;
    }

    PyErr_Clear();

    if ((ret = PyObject_GetAttrString(_builtins_module, key))) {
        return ret;
    }

    PyErr_Clear();

    return ret;
}


static PyObject* value_from_namespace(PyObject* _, PyObject* args) {
    char* key;
    PyObject* ns;

    if (!PyArg_ParseTuple(args, "sO", &key, &ns)) {
        return NULL;
    }

    return _ns_lookup(key, ns);
}


static PyObject* value_from_frame_or_namespace(PyObject* _, PyObject* args) {
    char* key;
    PyObject* locals;
    PyObject* globals;
    PyObject* ns;
    PyObject* ret;

    if (!PyArg_ParseTuple(args, "sOOO", &key, &locals, &globals, &ns)) {
        return NULL;
    }

    if ((ret = _frame_lookup(key, locals, globals))) {
        return ret;
    } else {
        return _ns_lookup(key, ns);
    }
}

static PyObject* value_from_search_list(PyObject* _, PyObject* args) {
    char* key;
    PyObject* selfobj;
    PyObject* ns;
    PyObject* ret;

    if (!PyArg_ParseTuple(args, "sOO", &key, &selfobj, &ns)) {
        return NULL;
    }

    if ((ret = _self_lookup(key, selfobj))) {
        return ret;
    } else {
        return _ns_lookup(key, ns);
    }
}


static PyObject* value_from_frame_or_search_list(PyObject* _, PyObject* args) {
    char* key;
    PyObject* locals;
    PyObject* globals;
    PyObject* selfobj;
    PyObject* ns;
    PyObject* ret;

    if (!PyArg_ParseTuple(args, "sOOOO", &key, &locals, &globals, &selfobj, &ns)) {
        return NULL;
    }

    if ((ret = _frame_lookup(key, locals, globals))) {
        return ret;
    } else if ((ret = _self_lookup(key, selfobj))) {
        return ret;
    } else {
        return _ns_lookup(key, ns);
    }
}

// ====== CheetahStringIO
#define RESIZE_FACTOR 200

typedef struct {
    PyObject_HEAD

    void* buf;
    Py_ssize_t buf_size; // Size of the buffer
    Py_ssize_t string_length;

    Py_ssize_t index;
    int memory_delegated;
} CheetahStringIOObject;

static void
CheetahStringIO_dealloc(CheetahStringIOObject* self) {
    if (!self->memory_delegated) {
        PyObject_DEL(self->buf);
    }
    Py_TYPE(self)->tp_free((PyObject*) self);
}

static int
CheetahStringIO_init(CheetahStringIOObject* self, PyObject* args, PyObject* kwds) {
    Py_ssize_t buf_size = RESIZE_FACTOR;

    if (!PyArg_ParseTuple(args, "|n", &buf_size)) {
        return -1;
    }

    buf_size *= sizeof(Py_UNICODE);

    self->buf = PyObject_MALLOC(buf_size);
    self->buf_size = buf_size;
    self->index = 0;
    self->string_length = 0;
    self->memory_delegated = 0;

    return 0;
}

static PyObject*
CheetahStringIO_InPlaceAdd(PyObject* _self, PyObject* other) {
    CheetahStringIOObject* self = (CheetahStringIOObject*) _self;
    Py_ssize_t str_len;
    char* str;

    if (self->memory_delegated) {
        PyErr_SetString(PyExc_RuntimeError, "You cannot modify a CheetahStringIO after getvalue() has been called");
        return NULL;
    }

    if (PyUnicode_Check(other)) {
        str_len = PyUnicode_GET_DATA_SIZE(other);
        str = (char*) PyUnicode_AS_UNICODE(other);
    } else {
        PyErr_SetString(PyExc_TypeError, "StringIO only supports concatenation with unicode objects");
        return NULL;
    }

    if (str_len + self->index > self->buf_size) {
        int difference = (str_len + self->index) - self->buf_size;

        self->buf = PyObject_REALLOC(self->buf, MAX(self->buf_size + RESIZE_FACTOR, self->buf_size + difference));
    }

    memcpy(self->buf + self->index, str, str_len);
    self->index += str_len;
    self->string_length += PyUnicode_GET_SIZE(other);

    Py_INCREF(_self);
    return _self;
}

static PyObject*
CheetahStringIO_Write(PyObject* self, PyObject *arg) {
    if (!PyUnicode_Check(arg)) {
        PyErr_SetString(PyExc_TypeError, "write only supports unicode objects");
        return NULL;
    }

    CheetahStringIO_InPlaceAdd(self, arg);
    Py_DECREF(self);

    Py_RETURN_NONE;
}

static PyObject*
CheetahStringIO_GetValue(CheetahStringIOObject* self) {
#if PY_MAJOR_VERSION == 2 && PY_MINOR_VERSION >= 6
    if (self->memory_delegated) {
        // We've delegated our internal buffer to the UnicodeObject, returning another UnicodeObject from the
        // same buffer would be extremely dangerous and potentially segfault the interpreter
        PyErr_SetString(PyExc_RuntimeError, "You cannot call getvalue() twice on a _cheetah.StringIO object");
        return NULL;
    }

    PyUnicodeObject* unicode = PyObject_New(PyUnicodeObject, &PyUnicode_Type);

    unicode->length = self->string_length;
    unicode->hash = -1;
    unicode->str = self->buf;
    unicode->defenc = NULL;

    self->memory_delegated = 1;

    return (PyObject*) unicode;
#else
    return PyUnicode_FromUnicode(self->buf, self->string_length);
#endif
}

static PyTypeObject CheetahStringIOType = {
    PyObject_HEAD_INIT(NULL)
    0,                             /*ob_size*/
    "_cheetah.StringIO",           /*tp_name*/
    sizeof(CheetahStringIOObject), /*tp_basicsize*/
    0,                             /*tp_itemsize*/
    (destructor) CheetahStringIO_dealloc, /*tp_dealloc*/
};

static PyMethodDef CheetahStringIO_methods[] = {
    {
        "write", (PyCFunction) CheetahStringIO_Write, METH_O,
    },
    {
        "getvalue", (PyCFunction) CheetahStringIO_GetValue, METH_NOARGS,
    },
    {NULL}
};

static PyNumberMethods CheetahStringIO_as_number;
// ======

static PyObject* _setup_module(PyObject* module) {
    if (module) {
        NotFound = PyErr_NewException("_cheetah.NotFound", PyExc_LookupError, NULL);
        PyModule_AddObject(module, "NotFound", NotFound);

    #if PY_MAJOR_VERSION == 2
        CheetahStringIOType.tp_new = PyType_GenericNew;
        CheetahStringIOType.tp_flags = Py_TPFLAGS_DEFAULT;
        CheetahStringIOType.tp_init = (initproc) CheetahStringIO_init;
        CheetahStringIOType.tp_methods = CheetahStringIO_methods;

        CheetahStringIO_as_number.nb_inplace_add = CheetahStringIO_InPlaceAdd;
        CheetahStringIOType.tp_as_number = &CheetahStringIO_as_number;

        if (PyType_Ready(&CheetahStringIOType) < 0) {
            Py_DECREF(module);
            return NULL;
        }
        PyModule_AddObject(module, "StringIO", (PyObject*) &CheetahStringIOType);
    #endif

        _builtins_module = PyImport_ImportModule(IF_PY3("builtins", "__builtin__"));
        if (!_builtins_module) {
            Py_DECREF(module);
            return NULL;
        }
    }
    return module;
}

static struct PyMethodDef methods[] = {
    {
        "value_from_namespace",
        (PyCFunction)value_from_namespace,
        METH_VARARGS
    },
    {
        "value_from_frame_or_namespace",
        (PyCFunction)value_from_frame_or_namespace,
        METH_VARARGS
    },
    {
        "value_from_search_list",
        (PyCFunction)value_from_search_list,
        METH_VARARGS
    },
    {
        "value_from_frame_or_search_list",
        (PyCFunction)value_from_frame_or_search_list,
        METH_VARARGS
    },
    {NULL, NULL}
};

#if PY_MAJOR_VERSION >= 3
static struct PyModuleDef module = {
    PyModuleDef_HEAD_INIT,
    "_cheetah",
    NULL,
    -1,
    methods
};

PyMODINIT_FUNC PyInit__cheetah(void) {
    return _setup_module(PyModule_Create(&module));
}
#else
PyMODINIT_FUNC init_cheetah(void) {
    _setup_module(Py_InitModule3("_cheetah", methods, NULL));
}
#endif
