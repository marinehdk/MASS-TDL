import os
from glob import glob
from setuptools import find_packages, setup

package_name = 'rl_workbench'

setup(
    name=package_name,
    version='0.1.0',
    packages=find_packages(exclude=['test']),
    data_files=[
        ('share/ament_index/resource_index/packages',
            ['resource/' + package_name]),
        ('share/' + package_name, ['package.xml']),
    ],
    install_requires=['setuptools', 'gymnasium', 'numpy'],
    zip_safe=True,
    maintainer='Team-E3',
    maintainer_email='team-e3@mass-l3-tdl.local',
    description='RL environment and tools for MASS L3 TDL',
    license='Proprietary',
    tests_require=['pytest'],
    entry_points={
        'console_scripts': [],
    },
)
