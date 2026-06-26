from glob import glob
from setuptools import find_packages, setup

package_name = 'mission_supervisor'

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
    description='Shadow-mode mission HSM supervisor for MASS ADAS validation.',
    license='Apache-2.0',
    tests_require=['pytest'],
    entry_points={
        'console_scripts': [
            'captain_decision_node = mission_supervisor.captain_decision_node:main',
            'mission_supervisor_node = mission_supervisor.mission_supervisor_node:main',
            'propulsion_policy_compliance_node = mission_supervisor.propulsion_policy_compliance_node:main',
            'propulsion_policy_node = mission_supervisor.propulsion_policy_node:main',
        ],
    },
)
