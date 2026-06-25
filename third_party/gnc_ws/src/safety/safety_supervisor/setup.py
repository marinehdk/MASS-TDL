from glob import glob
from setuptools import find_packages, setup

package_name = 'safety_supervisor'

setup(
    name=package_name,
    version='0.1.0',
    packages=find_packages(),
    data_files=[
        ('share/ament_index/resource_index/packages', ['resource/' + package_name]),
        ('share/' + package_name, ['package.xml', 'README.md']),
        ('share/' + package_name + '/config', glob('config/*.yaml')),
        ('share/' + package_name + '/launch', glob('launch/*.launch.py')),
    ],
    install_requires=['setuptools', 'PyYAML'],
    zip_safe=True,
    maintainer='tiger.wang',
    maintainer_email='tiger.wang@sangoai.cn',
    description='Shadow-mode safety supervisor for MASS ADAS validation.',
    license='Apache-2.0',
    tests_require=['pytest'],
    entry_points={
        'console_scripts': [
            'safety_supervisor_node = safety_supervisor.safety_supervisor_node:main',
        ],
    },
)
