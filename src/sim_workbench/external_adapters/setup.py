from setuptools import find_packages, setup

package_name = "external_adapters"

setup(
    name=package_name,
    version="0.1.0",
    packages=find_packages(exclude=["test"]),
    data_files=[
        ("share/ament_index/resource_index/packages", [f"resource/{package_name}"]),
        (f"share/{package_name}", ["package.xml"]),
    ],
    install_requires=["setuptools"],
    zip_safe=True,
    maintainer="MASS-L3",
    maintainer_email="devnull@example.com",
    description="External module adapters for MASS-L3 TDL ingress and route egress.",
    license="Proprietary",
    tests_require=["pytest"],
    entry_points={
        "console_scripts": [
            "external_tdl_ingress = external_adapters.tdl_ingress_node:main",
            "l2_route_plan_adaptor = external_adapters.l2_route_plan_adaptor:main",
            "external_route_out_tdl = external_adapters.route_out_tdl_node:main",
            "external_route_out_path = external_adapters.route_out_external_path_node:main",
        ],
    },
)
