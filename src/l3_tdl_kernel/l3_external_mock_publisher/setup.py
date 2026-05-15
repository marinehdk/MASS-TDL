from setuptools import find_packages, setup

package_name = "l3_external_mock_publisher"

setup(
    name=package_name,
    version="0.1.0",
    packages=find_packages(exclude=["test"]),
    data_files=[
        ("share/ament_index/resource_index/packages",
         ["resource/" + package_name]),
        ("share/" + package_name, ["package.xml"]),
        ("share/" + package_name + "/launch", ["launch/external_mock.launch.py"]),
    ],
    install_requires=["setuptools", "PyYAML>=6.0"],
    entry_points={
        "console_scripts": [
            "external_mock_publisher = l3_external_mock_publisher.external_mock_publisher:main",
        ],
    },
)
