from glob import glob
from setuptools import find_packages, setup

package_name = 'mock_scenarios'

setup(
    name=package_name,
    version='0.1.0',
    packages=find_packages(),
    data_files=[
        ('share/ament_index/resource_index/packages', ['resource/' + package_name]),
        ('share/' + package_name, ['package.xml', 'README.md']),
        ('share/' + package_name + '/launch', glob('launch/*.launch.py')),
        ('share/' + package_name + '/config/examples', glob('config/examples/*')),
        ('share/' + package_name + '/config/scenarios', glob('config/scenarios/*.yaml')),
        ('share/' + package_name + '/config/validation', glob('config/validation/*.yaml')),
    ],
    install_requires=['setuptools', 'PyYAML'],
    zip_safe=True,
    maintainer='tiger.wang',
    maintainer_email='tiger.wang@sangoai.cn',
    description='Scenario-driven mock data publishers for MASS ADAS simulation and safety validation.',
    license='Apache-2.0',
    tests_require=['pytest'],
    entry_points={
        'console_scripts': [
            'dynamics_equation_probe = mock_scenarios.dynamics_equation_probe:main',
            'dynamics_allocation_probe = mock_scenarios.dynamics_allocation_probe:main',
            'mock_data_player = mock_scenarios.mock_data_player:main',
            'mock_contract_check = mock_scenarios.mock_contract_check:main',
            'model_consistency_check = mock_scenarios.model_consistency_check:main',
            'regression_gate_check = mock_scenarios.regression_gate_check:main',
            'scenario_acceptance = mock_scenarios.scenario_acceptance:main',
            'scenario_metrics_merge = mock_scenarios.scenario_metrics_merge:main',
            'scenario_metrics_from_csv = mock_scenarios.scenario_metrics_from_csv:main',
            'scenario_suite_report = mock_scenarios.scenario_suite_report:main',
            'scenario_visualize = mock_scenarios.scenario_visualization:main',
            'scenario_runtime_publisher = mock_scenarios.scenario_runtime_publisher:main',
            'validation_observer = mock_scenarios.validation_observer:main',
            'validate_scenario = mock_scenarios.scenario_validator:main',
        ],
    },
)
