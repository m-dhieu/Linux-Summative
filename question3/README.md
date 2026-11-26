# Python C Extension for IoT Sensor Data Processing

## Overview

This project implements a Python C extension module named `sensor_processor` for processing and analysing IoT sensor readings.

The extension uses Python C API to expose 5 data-processing C functions. It accepts Python lists and tuples containing numerical sensor readings, performs calculations using C's `double` data type, and returns results as standard Python objects. The main objective is to show C can be integrated with Python to perform numerical operations while maintaining compatibility with Python's data types, exception handling, and memory management.

## Requirements

- Linux operating system
- Python 3
- GCC compiler
- Python development headers
- setuptools

The extension was developed and tested using Python 3.8.

## Building the Extension

The extension is built using the `setup.py` file.

Run this command from the project directory:

```bash
python3 setup.py build_ext --inplace
```

This compiles `processor.c` and creates the importable `sensor_processor` extension in the project directory.

To import the module in Python:

```python
import sensor_processor
```

## Implemented Functions

### filter_readings(data, minimum, maximum)

Filters sensor readings and returns values within specified inclusive range.

A reading is included when:

```text
minimum <= reading <= maximum
```

The function returns a Python list containing filtered readings.

**Time complexity:** O(n)  
**Auxiliary C space complexity:** O(1), excluding the returned list.

### count_outliers(data, minimum, maximum)

Counts readings outside specified range.

A reading is considered an outlier when:

```text
reading < minimum or reading > maximum
```

The function returns an integer representing number of outliers.

**Time complexity:** O(n)  
**Auxiliary C space complexity:** O(1).

### moving_average(data, window)

Calculates moving average of sensor readings using a specified window size.

For each window, the function calculates:

```text
average = sum of readings in window / window size
```

It uses a sliding-window algorithm, updating the running sum by adding the incoming reading and removing the outgoing reading. This avoids recalculating the sum for every window.

For a dataset containing n readings and a window size of w, the function returns n - w + 1 averages.

**Time complexity:** O(n)  
**Auxiliary C space complexity:** O(n), for the converted readings buffer.

### maximum_difference(data)

Calculates difference between the maximum and minimum sensor readings.

```text
maximum difference = maximum reading - minimum reading
```

The function scans the dataset once while maintaining the minimum and maximum values.

It returns the result as a Python floating-point number.

**Time complexity:** O(n)  
**Auxiliary C space complexity:** O(1).

### sensor_summary(data)

Produces a summary of sensor readings and returns the result as a Python dictionary.

The dictionary contains:

| Key | Description |
|---|---|
| samples | Total number of readings |
| minimum | Lowest reading |
| maximum | Highest reading |
| average | Mean of all readings |
| above_average | Number of readings greater than the mean |

The function uses two passes through the dataset. The first calculates minimum, maximum, and average. The second counts readings above average. Average is calculated by accumulating each reading divided by dataset size, reducing the risk of intermediate sum overflow compared with accumulating the full sum first.

**Time complexity:** O(n)  
**Auxiliary C space complexity:** O(1).

## Python C API and Memory Management

The extension uses the Python C API to communicate between Python and C.

### Input conversion

The functions accept Python lists and tuples. `PySequence_Fast` is used to obtain a sequence suitable for indexed access.

Each reading is converted into a C `double` using `PyFloat_AsDouble`. Boolean values and non-numeric readings are rejected. Non-finite sensor readings, including NaN and infinity, are also rejected.

### Python object creation

The extension converts C results back into Python objects using Python C API functions such as:

- `PyFloat_FromDouble`
- `PyLong_FromSsize_t`
- `PyList_New`
- `PyList_Append`
- `PyDict` construction through `Py_BuildValue`

### Reference management

Python objects are managed using reference counting.

- The reference returned by `PySequence_Fast` is owned by the extension and released using `Py_DECREF`.
- Individual sequence elements are borrowed references and are not independently decremented.
- Objects appended to Python lists have their references managed appropriately.
- `PyList_SET_ITEM` is used where the list takes ownership of the inserted object.
- Dynamically allocated C memory is released using `PyMem_Free`.

These help prevent memory leaks and invalid reference handling.

## Input Validation and Error Handling

| Condition | Expected behaviour |
|---|---|
| Python list or tuple | Accepted |
| Unsupported container | TypeError |
| Non-numeric reading | TypeError |
| Boolean reading | TypeError |
| NaN or infinite reading | ValueError |
| NaN range boundary | ValueError |
| Minimum greater than maximum | ValueError |
| Empty dataset for filtering | Returns an empty list |
| Empty dataset for outlier counting | Returns 0 |
| Empty dataset for moving average | ValueError |
| Empty dataset for maximum difference | ValueError |
| Empty dataset for sensor summary | ValueError |
| Zero or negative window | ValueError |
| Window larger than dataset | ValueError |
| Boolean or non-integer window | TypeError |
| Arithmetic overflow | OverflowError |

Infinite range boundaries are permitted, but sensor readings themselves must be finite.

The extension uses Python exceptions to report invalid inputs and arithmetic errors rather than silently returning incorrect results.

## 8. Testing

Testing is performed using `test_processor.py` which verifies:

1. Correct results from all functions.
2. Handling of empty datasets.
3. Acceptance of both lists and tuples.
4. Inclusive filtering boundaries.
5. Invalid containers and non-numeric inputs.
6. Invalid ranges and moving-average windows.
7. NaN and infinite values.
8. Arithmetic overflow handling.
9. Performance comparison between the C extension and a pure Python implementation.

The tests use expected results and exception checks to verify the behaviour of the extension.

### Test Results

All functional tests passed successfully.

All empty-dataset tests passed successfully.

All 15 invalid-input and arithmetic-error checks passed successfully, with expected exception types.

The test script also completed performance comparison successfully.

## Performance Comparison

| Measurement | Result |
|---|---:|
| Dataset size | 500,000 readings |
| Repetitions | 5 |
| C extension median | 13.51 ms |
| Pure Python median | 44.57 ms |
| Measured speed ratio | 3.30x |

In this benchmark, the C extension was 3.30 times faster than the pure Python implementation, using 70% less execution time.

These results show the performance improvement measured in this test environment. Actual execution times may vary depending on hardware, system load, Python version, and other environmental factors.

## Compilation and Verification

The source was checked using GCC with warning and error flags:

```bash
gcc -Wall -Wextra -Werror -pedantic \
    -fsyntax-only $(python3-config --includes) processor.c
```

The check completed without warnings or errors.

The extension was subsequently rebuilt using:

```bash
python3 setup.py build_ext --inplace
```

The build completed successfully, and the test script reported:

```text
All tests passed successfully!
```

## Conclusion

This project demonstrates the development of a Python C extension for numerical sensor data processing.

It implements five processing functions using C, integrates them with Python through the Python C API, and applies input validation, exception handling, and reference-counted memory management.

The implementation also demonstrates the use of algorithmic optimisation through a sliding-window moving-average calculation and evaluates performance against a pure Python implementation.

The successful compilation, functional testing, invalid-input testing, and performance benchmark demonstrate that the extension operates as intended within the tested environment.

