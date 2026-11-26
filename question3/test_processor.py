import math
import statistics
import time
import sensor_processor

def python_summary(data):
    """Pure Python implementation for performance comparison"""
    mean = sum(data) / len(data)

    return {
        "samples": len(data),
        "minimum": min(data),
        "maximum": max(data),
        "average": mean,
        "above_average": sum(1 for x in data if x > mean),
    }

def expect_exception(function, args, expected_exception):
    """Verify that function raises expected exception"""
    try:
        function(*args)
    except expected_exception:
        return
    except Exception as error:
        raise AssertionError(
            f"Expected {expected_exception.__name__}, "
            f"but received {type(error).__name__}: {error}"
        ) from error

    raise AssertionError(
        f"Expected {expected_exception.__name__}, but no exception was raised"
    )

def run_tests():
    print(" Functional Tests ")

    readings = [10.5, 25.0, 110.0, 35.5]
    tuple_readings = (10.0, 20.0, 30.0, 40.0)

    # filtering
    filtered = sensor_processor.filter_readings(
        readings, 20.0, 40.0
    )
    print("filter_readings:", filtered)
    assert filtered == [25.0, 35.5]

    # inclusive boundaries
    assert sensor_processor.filter_readings(
        [20.0, 30.0, 40.0], 20.0, 40.0
    ) == [20.0, 30.0, 40.0]

    # outlier counting
    outliers = sensor_processor.count_outliers(
        readings, 20.0, 40.0
    )
    print("count_outliers:", outliers)
    assert outliers == 2

    # boundary values aren't outliers
    assert sensor_processor.count_outliers(
        [20.0, 30.0, 40.0], 20.0, 40.0
    ) == 0

    # moving average
    moving_avg = sensor_processor.moving_average(
        (10, 20, 30, 40), 2
    )
    print("moving_average:", moving_avg)
    assert moving_avg == [15.0, 25.0, 35.0]
    assert sensor_processor.moving_average(tuple_readings, 4) == [25.0]
    assert sensor_processor.moving_average([12.5], 1) == [12.5]

    # maximum difference
    max_diff = sensor_processor.maximum_difference(
        [10.0, 25.0, 40.0, 15.0]
    )
    print("maximum_difference:", max_diff)
    assert max_diff == 30.0
    assert sensor_processor.maximum_difference([42.5]) == 0.0

    # summary statistics
    summary = sensor_processor.sensor_summary(tuple_readings)
    print("sensor_summary:", summary)
    
    assert summary == {
        "samples": 4,
        "minimum": 10.0,
        "maximum": 40.0,
        "average": 25.0,
        "above_average": 2,
    }

    print("PASS: all functions produced expected results")
    print("\n Empty Dataset Tests ")

    assert sensor_processor.filter_readings([], 0.0, 100.0) == []
    assert sensor_processor.count_outliers((), 0.0, 100.0) == 0

    expect_exception(
        sensor_processor.moving_average,
        ([], 2),
        ValueError
    )

    expect_exception(
        sensor_processor.maximum_difference,
        ([],),
        ValueError
    )

    expect_exception(
        sensor_processor.sensor_summary,
        ([],),
        ValueError
    )

    print("PASS: empty datasets handled correctly")
    print("\n Invalid Input Tests ")

    invalid_cases = [
        (
            "Unsupported container",
            sensor_processor.maximum_difference,
            ({1.0, 2.0},),
            TypeError
        ),
        (
            "String input",
            sensor_processor.sensor_summary,
            ("1234",),
            TypeError
        ),
        (
            "Non-numeric element",
            sensor_processor.filter_readings,
            ([10.0, "bad", 30.0], 0.0, 50.0),
            TypeError
        ),
        (
            "Boolean reading",
            sensor_processor.count_outliers,
            ([10.0, True, 30.0], 0.0, 50.0),
            TypeError
        ),
        (
            "Invalid range",
            sensor_processor.filter_readings,
            ([10.0, 20.0], 50.0, 10.0),
            ValueError
        ),
        (
            "NaN range boundary",
            sensor_processor.count_outliers,
            ([10.0, 20.0], float("nan"), 50.0),
            ValueError
        ),
        (
            "Zero window",
            sensor_processor.moving_average,
            ([10.0, 20.0], 0),
            ValueError
        ),
        (
            "Negative window",
            sensor_processor.moving_average,
            ([10.0, 20.0], -3),
            ValueError
        ),
        (
            "Window exceeds dataset length",
            sensor_processor.moving_average,
            ([10.0, 20.0], 3),
            ValueError
        ),
        (
            "Boolean window",
            sensor_processor.moving_average,
            ([10.0, 20.0], True),
            TypeError
        ),
        (
            "Floating-point window",
            sensor_processor.moving_average,
            ([10.0, 20.0], 2.0),
            TypeError
        ),
        (
            "NaN reading",
            sensor_processor.sensor_summary,
            ([10.0, float("nan")],),
            ValueError
        ),
        (
            "Infinite reading",
            sensor_processor.maximum_difference,
            ([10.0, float("inf")],),
            ValueError
        ),
        (
            "Moving-average arithmetic overflow",
            sensor_processor.moving_average,
            ([1.7e308, 1.7e308], 2),
            OverflowError
        ),
        (
            "Maximum-difference arithmetic overflow",
            sensor_processor.maximum_difference,
            ([-1.7e308, 1.7e308],),
            OverflowError
        ),
    ]

    for label, function, args, expected_exception in invalid_cases:
        expect_exception(function, args, expected_exception)
        print(f"PASS: {label}: {expected_exception.__name__}")

    print("\n Performance Comparison ")

    large_data = [float(i % 100) for i in range(500_000)]
    repetitions = 5
    c_times = []
    python_times = []
    for _ in range(repetitions):
        start = time.perf_counter()
        c_result = sensor_processor.sensor_summary(large_data)
        c_times.append(time.perf_counter() - start)

        start = time.perf_counter()
        py_result = python_summary(large_data)
        python_times.append(time.perf_counter() - start)

    assert math.isclose(
        c_result["average"],
        py_result["average"],
        rel_tol=1e-12
    )

    assert c_result["samples"] == py_result["samples"]
    assert c_result["minimum"] == py_result["minimum"]
    assert c_result["maximum"] == py_result["maximum"]
    assert c_result["above_average"] == py_result["above_average"]

    c_median = statistics.median(c_times)
    python_median = statistics.median(python_times)

    print(f"Dataset size: {len(large_data):,} readings")
    print(f"Repetitions: {repetitions}")
    print(f"C extension median: {c_median * 1000:.2f} ms")
    print(f"Python median: {python_median * 1000:.2f} ms")

    if c_median > 0:
        print(f"Measured speed ratio: {python_median / c_median:.2f}x")

    print("\nAll tests passed successfully!")

if __name__ == "__main__":
    run_tests()

