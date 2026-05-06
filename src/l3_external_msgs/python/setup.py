from setuptools import setup, find_packages

package_name = "l3_external_mock_publisher"

setup(
    name=package_name,
    version="0.1.0",
    packages=find_packages(),
    data_files=[
        ("share/ament_index/resource_index/packages", ["resource/" + package_name]),
        ("share/" + package_name, ["package.xml"]),
    ],
    install_requires=["setuptools"],
    zip_safe=True,
    maintainer="MASS ADAS Team",
    maintainer_email="mass-adas@sango.com",
    description="Mock publisher for cross-layer messages",
    license="Proprietary",
    tests_require=["pytest"],
    entry_points={
        "console_scripts": [
            "external_mock_publisher = l3_external_mock_publisher.external_mock_publisher:main",
        ],
    },
)
