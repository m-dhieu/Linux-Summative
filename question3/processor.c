#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <math.h>

/* Converts Python numeric object into C double
 * Boolean values are rejected even if bool is a subclass of int
 * NaN & infinity aren't accepted as sensor readings
 * Returns 0 on success & -1 on failure, with a Python exception set
 */
static int extract_double(PyObject *item, double *out_val) {
    if (PyBool_Check(item) || !PyNumber_Check(item)) {
        PyErr_SetString(
            PyExc_TypeError,
            "All elements in dataset must be numeric"
        );
        return -1;
    }

    double val = PyFloat_AsDouble(item);
    if (val == -1.0 && PyErr_Occurred()) {return -1;}
    if (!isfinite(val)) {
        PyErr_SetString(
            PyExc_ValueError,
            "Sensor readings cannot be NaN or infinite"
        );
        return -1;
    }

    *out_val = val;
    return 0;
}

/* validates input is a Python list/tuple
 * PySequence_Fast returns a new owned reference 
 * Caller must release it using Py_DECREF()
 * Returned item pointers are borrowed references owned by the
 * sequence and remain valid while fast_seq is alive
 */
static PyObject *validate_fast_sequence(
    PyObject *seq_obj,
    PyObject ***out_items,
    Py_ssize_t *out_len
) {
    if (!PyList_Check(seq_obj) && !PyTuple_Check(seq_obj)) {
        PyErr_SetString(
            PyExc_TypeError,
            "Expected a list or tuple of numeric sensor readings"
        );
        return NULL;
    }

    PyObject *fast_seq = PySequence_Fast(
        seq_obj,
        "Expected a list or tuple"
    );

    if (!fast_seq) {return NULL;}
    *out_len = PySequence_Fast_GET_SIZE(fast_seq);
    *out_items = PySequence_Fast_ITEMS(fast_seq);
    return fast_seq;
}

/* Allocates buffer for double values
 * Checks multiplication overflow before allocating mem
 * Caller is responsible for releasing buffer with PyMem_Free()
 */
static double *allocate_buffer(Py_ssize_t len) {
    if (len > PY_SSIZE_T_MAX / (Py_ssize_t)sizeof(double)) {
        PyErr_SetString(
            PyExc_OverflowError,
            "Dataset is too large for memory allocation"
        );
        return NULL;
    }

    double *buffer = PyMem_Malloc((size_t)len * sizeof(double));
    if (!buffer) {
        PyErr_NoMemory();
        return NULL;
    }

    return buffer;
}

/* filter_readings(data, minimum, maximum)
 * Formula: Return all x_i satisfying minimum <= x_i <= maximum
 * Time complexity: O(n), where n is number of readings
 * Auxiliary C space: O(1), excluding returned Python list
 * Readings are converted to C doubles before comparison
 */
static PyObject *sensor_filter_readings(PyObject *self, PyObject *args) {
    (void)self;
    PyObject *data_obj;
    double minimum, maximum;
    if (!PyArg_ParseTuple(args, "Odd", &data_obj, &minimum, &maximum)) {
        return NULL;
    }

    if (isnan(minimum) || isnan(maximum) || minimum > maximum) {
        PyErr_SetString(
            PyExc_ValueError,
            "Invalid range: minimum must be <= maximum and non-NaN"
        );
        return NULL;
    }

    PyObject **items = NULL;
    Py_ssize_t len = 0;
    PyObject *fast_seq = validate_fast_sequence(data_obj, &items, &len);
    if (!fast_seq) {return NULL;}

    PyObject *result_list = PyList_New(0);
    if (!result_list) {
        Py_DECREF(fast_seq);
        return NULL;
    }

    for (Py_ssize_t i = 0; i < len; i++) {
	double val;
        if (extract_double(items[i], &val) < 0) {
            Py_DECREF(result_list);
            Py_DECREF(fast_seq);
            return NULL;
        }
        if (val >= minimum && val <= maximum) {
            PyObject *py_val = PyFloat_FromDouble(val);
            if (!py_val) {
                Py_DECREF(result_list);
                Py_DECREF(fast_seq);
                return NULL;
            }
            if (PyList_Append(result_list, py_val) < 0) {
                Py_DECREF(py_val);
                Py_DECREF(result_list);
                Py_DECREF(fast_seq);
                return NULL;
            }
            Py_DECREF(py_val);
        }
    }

    Py_DECREF(fast_seq);
    return result_list;
}

/* count_outliers(data, minimum, maximum)
 * Formula: Count all x_i satisfying x_i < minimum OR x_i > maximum
 * Time complexity: O(n)
 * Auxiliary C space: O(1)
 * Boundary values are accepted and not counted as outliers
 */
static PyObject *sensor_count_outliers(PyObject *self, PyObject *args) {
    (void)self;
    PyObject *data_obj;
    double minimum, maximum;
    if (!PyArg_ParseTuple(args, "Odd", &data_obj, &minimum, &maximum)) {
        return NULL;
    }

    if (isnan(minimum) || isnan(maximum) || minimum > maximum) {
        PyErr_SetString(
            PyExc_ValueError,
            "Invalid range: minimum must be <= maximum and non-NaN"
        );
        return NULL;
    }

    PyObject **items = NULL;
    Py_ssize_t len = 0;
    PyObject *fast_seq = validate_fast_sequence(data_obj, &items, &len);
    if (!fast_seq) {return NULL;}

    Py_ssize_t outlier_count = 0;
    for (Py_ssize_t i = 0; i < len; i++) {
        double val;
        if (extract_double(items[i], &val) < 0) {
            Py_DECREF(fast_seq);
            return NULL;
        }
        if (val < minimum || val > maximum) {outlier_count++;}
    }

    Py_DECREF(fast_seq);
    return PyLong_FromSsize_t(outlier_count);
}

/* moving_average(data, window)
 * Formula: MA_i = (x_i + x_(i+1) + ... + x_(i+w-1)) / w
 * A sliding-window technique updates the previous sum by removing
 * the outgoing value and adding the incoming value
 * Time complexity: O(n)
 * Auxiliary C space: O(n), for converted double buffer
 * The function raises ValueError for empty datasets or invalid windows
 * Arithmetic overflow during summation raises OverflowError
 */
static PyObject *sensor_moving_average(PyObject *self, PyObject *args) {
    (void)self;
    PyObject *data_obj;
    PyObject *window_obj;
    if (!PyArg_ParseTuple(args, "OO", &data_obj, &window_obj)) {return NULL;}

    if (PyBool_Check(window_obj) || !PyLong_Check(window_obj)) {
        PyErr_SetString(
            PyExc_TypeError,
            "Window size must be an integer"
        );
        return NULL;
    }

    Py_ssize_t window = PyLong_AsSsize_t(window_obj);
    if (window == -1 && PyErr_Occurred()) {return NULL;}
    if (window <= 0) {
        PyErr_SetString(
            PyExc_ValueError,
            "Window size must be positive integer"
        );
        return NULL;
    }

    PyObject **items = NULL;
    Py_ssize_t len = 0;
    PyObject *fast_seq = validate_fast_sequence(data_obj, &items, &len);
    if (!fast_seq) {return NULL;}

    if (len == 0) {
        Py_DECREF(fast_seq);
        PyErr_SetString(
            PyExc_ValueError,
            "Cannot compute moving average of empty dataset"
        );
        return NULL;
    }

    if (window > len) {
        Py_DECREF(fast_seq);
        PyErr_SetString(
            PyExc_ValueError,
            "Window size cannot exceed number of readings"
        );
        return NULL;
    }

    double *buffer = allocate_buffer(len);
    if (!buffer) {
        Py_DECREF(fast_seq);
        return NULL;
    }

    for (Py_ssize_t i = 0; i < len; i++) {
        if (extract_double(items[i], &buffer[i]) < 0) {
            PyMem_Free(buffer);
            Py_DECREF(fast_seq);
            return NULL;
        }
    }

    Py_DECREF(fast_seq);
    Py_ssize_t out_len = len - window + 1;
    PyObject *result_list = PyList_New(out_len);
    if (!result_list) {
        PyMem_Free(buffer);
        return NULL;
    }

    double window_sum = 0.0;
    for (Py_ssize_t i = 0; i < window; i++) {
        window_sum += buffer[i];
        if (!isfinite(window_sum)) {
            Py_DECREF(result_list);
            PyMem_Free(buffer);
            PyErr_SetString(
                PyExc_OverflowError,
                "Moving-average sum exceeds supported double range"
            );
            return NULL;
        }
    }

    PyObject *first_avg = PyFloat_FromDouble(window_sum / (double)window);
    if (!first_avg) {
        Py_DECREF(result_list);
        PyMem_Free(buffer);
        return NULL;
    }

    PyList_SET_ITEM(result_list, 0, first_avg);
    for (Py_ssize_t i = 1; i < out_len; i++) {
        window_sum += buffer[i + window - 1] - buffer[i - 1];
        if (!isfinite(window_sum)) {
            Py_DECREF(result_list);
            PyMem_Free(buffer);
            PyErr_SetString(
                PyExc_OverflowError,
                "Moving-average sum exceeds supported double range"
            );
            return NULL;
        }

        PyObject *avg_obj = PyFloat_FromDouble(window_sum / (double)window);
        if (!avg_obj) {
            Py_DECREF(result_list);
            PyMem_Free(buffer);
            return NULL;
        }

        PyList_SET_ITEM(result_list, i, avg_obj);
    }

    PyMem_Free(buffer);
    return result_list;
}

/* maximum_difference(data)
 * Formula: Difference = maximum reading - minimum reading
 * Time complexity: O(n)
 * Auxiliary C space: O(1)
 * Requires at least one reading
 */
static PyObject *sensor_maximum_difference(PyObject *self, PyObject *args) {
    (void)self;
    PyObject *data_obj;
    if (!PyArg_ParseTuple(args, "O", &data_obj)) {return NULL;}

    PyObject **items = NULL;
    Py_ssize_t len = 0;
    PyObject *fast_seq = validate_fast_sequence(data_obj, &items, &len);
    if (!fast_seq) {return NULL;}
    if (len == 0) {
        Py_DECREF(fast_seq);
        PyErr_SetString(
            PyExc_ValueError,
            "Cannot compute maximum difference of empty dataset"
        );
        return NULL;
    }

    double first_val;
    if (extract_double(items[0], &first_val) < 0) {
        Py_DECREF(fast_seq);
        return NULL;
    }

    double min_val = first_val;
    double max_val = first_val;
    for (Py_ssize_t i = 1; i < len; i++) {
        double val;
        if (extract_double(items[i], &val) < 0) {
            Py_DECREF(fast_seq);
            return NULL;
        }
        if (val < min_val) {min_val = val;}
        if (val > max_val) {max_val = val;}
    }

    Py_DECREF(fast_seq);
    double difference = max_val - min_val;
    if (!isfinite(difference)) {
        PyErr_SetString(
            PyExc_OverflowError,
            "Maximum difference exceeds supported double range"
        );
        return NULL;
    }

    return PyFloat_FromDouble(difference);
}

/* sensor_summary(data)
 * Formulas:
 *   Mean = sum(x_i) / n
 *   Above average = count(x_i > mean)
 * The 1st traversal calculates minimum, maximum & mean
 * The 2nd traversal counts readings above mean
 * Time complexity: O(n)
 * Auxiliary C space: O(1)
 * No intermediate array is required
 */
static PyObject *sensor_summary(PyObject *self, PyObject *args) {
    (void)self;
    PyObject *data_obj;
    if (!PyArg_ParseTuple(args, "O", &data_obj)) {return NULL;}

    PyObject **items = NULL;
    Py_ssize_t len = 0;
    PyObject *fast_seq = validate_fast_sequence(data_obj, &items, &len);
    if (!fast_seq) {return NULL;}
    if (len == 0) {
        Py_DECREF(fast_seq);
        PyErr_SetString(
            PyExc_ValueError,
            "Cannot generate summary for empty dataset"
        );
        return NULL;
    }

    double first_val;
    if (extract_double(items[0], &first_val) < 0) {
        Py_DECREF(fast_seq);
        return NULL;
    }

    double min_val = first_val;
    double max_val = first_val;
    double mean = first_val / (double)len;
    for (Py_ssize_t i = 1; i < len; i++) {
        double val;
        if (extract_double(items[i], &val) < 0) {
            Py_DECREF(fast_seq);
            return NULL;
        }

        if (val < min_val) {min_val = val;}
        if (val > max_val) {max_val = val;}

        mean += val / (double)len;
        if (!isfinite(mean)) {
            Py_DECREF(fast_seq);
            PyErr_SetString(
                PyExc_OverflowError,
                "Mean calculation exceeds supported double range"
            );
            return NULL;
        }
    }

    Py_ssize_t above_avg_count = 0;
    for (Py_ssize_t i = 0; i < len; i++) {
        double val;
        if (extract_double(items[i], &val) < 0) {
            Py_DECREF(fast_seq);
            return NULL;
        }
        if (val > mean) {above_avg_count++;}
    }

    Py_DECREF(fast_seq);
    return Py_BuildValue(
        "{s:n, s:d, s:d, s:d, s:n}",
        "samples", len,
        "minimum", min_val,
        "maximum", max_val,
        "average", mean,
        "above_average", above_avg_count
    );
}

/* method table to expose functions to Python */
static PyMethodDef SensorMethods[] = {
    {
        "filter_readings",
        sensor_filter_readings,
        METH_VARARGS,
        "Return readings within inclusive minimum and maximum range"
    },
    {
        "count_outliers",
        sensor_count_outliers,
        METH_VARARGS,
        "Count readings outside acceptable range"
    },
    {
        "moving_average",
        sensor_moving_average,
        METH_VARARGS,
        "Calculate sliding-window moving average"
    },
    {
        "maximum_difference",
        sensor_maximum_difference,
        METH_VARARGS,
        "Calculate difference between maximum and minimum readings"
    },
    {
        "sensor_summary",
        sensor_summary,
        METH_VARARGS,
        "Return summary statistics for sensor readings"
    },
    {NULL, NULL, 0, NULL}
};

/* module definition */
static struct PyModuleDef sensor_module = {
    PyModuleDef_HEAD_INIT,
    "sensor_processor",
    "C extension for IoT sensor data filtering and analysis",
    -1,
    SensorMethods,
    NULL,
    NULL,
    NULL,
    NULL
};

/* initialisation function called when Python imports module
PyMODINIT_FUNC PyInit_sensor_processor(void) {
    return PyModule_Create(&sensor_module);
}

