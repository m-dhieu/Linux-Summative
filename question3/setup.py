from setuptools import Extension, setup

sensor_extension = Extension(
    name="sensor_processor",
    sources=["processor.c"],
)

setup(
    name="sensor_processor",
    version="1.0.0",
    description="C extension for IoT sensor data filtering and analysis",
    ext_modules=[sensor_extension],
)

