"""Test W7: Simulation auto-stop timer — TDD red"""
import sys
import types
import asyncio
import time
from pathlib import Path
import importlib.util
from types import SimpleNamespace
import pytest
from unittest.mock import Mock, patch, AsyncMock

class MockNode:
    def __init__(self, name):
        self.name = name
    def get_logger(self):
        return Mock()
    def create_subscription(self, msg_type, topic, callback, qos):
        return Mock()
    def create_publisher(self, msg_type, topic, qos):
        return Mock()
    def create_timer(self, period, callback):
        return Mock()
    def create_client(self, srv_type, srv_name, callback_group=None):
        return Mock()
    def declare_parameter(self, name, default_value):
        class Param:
            value = default_value
        return Param()
    def get_parameter(self, name):
        class Param:
            value = 0.0
        return Param()
    def get_clock(self):
        clock = Mock()
        clock.now = Mock(return_value=SimpleNamespace(to_msg=Mock(return_value=SimpleNamespace(sec=0, nanosec=0))))
        return clock

@pytest.fixture(autouse=True)
def setup_fake_ros(monkeypatch):
    # Fake ROS modules
    rclpy = types.ModuleType("rclpy")
    rclpy.node = types.ModuleType("rclpy.node")
    rclpy.node.Node = MockNode
    rclpy.executors = types.ModuleType("rclpy.executors")
    rclpy.executors.MultiThreadedExecutor = object
    rclpy.qos = types.ModuleType("rclpy.qos")
    rclpy.qos.QoSProfile = lambda **kwargs: kwargs
    rclpy.qos.QoSReliabilityPolicy = SimpleNamespace(BEST_EFFORT=1, RELIABLE=2)
    rclpy.qos.QoSDurabilityPolicy = SimpleNamespace(VOLATILE=1, TRANSIENT_LOCAL=2)
    rclpy.qos.QoSHistoryPolicy = SimpleNamespace(KEEP_LAST=1)

    sil_msgs = types.ModuleType("sil_msgs")
    sil_msgs.msg = types.ModuleType("sil_msgs.msg")
    sil_msgs.msg.OwnShipState = type("OwnShipState", (), {})
    sil_msgs.msg.TargetVesselState = type("TargetVesselState", (), {})
    sil_msgs.msg.EnvironmentState = type("EnvironmentState", (), {})
    sil_msgs.msg.ModulePulse = type("ModulePulse", (), {})
    sil_msgs.msg.ASDREvent = type("ASDREvent", (), {})
    sil_msgs.msg.BridgeState = type("BridgeState", (), {})

    l3_external_msgs = types.ModuleType("l3_external_msgs")
    l3_external_msgs.msg = types.ModuleType("l3_external_msgs.msg")
    l3_external_msgs.msg.FilteredOwnShipState = type("FilteredOwnShipState", (), {})
    l3_external_msgs.msg.TrackedTargetArray = type("TrackedTargetArray", (), {})
    l3_external_msgs.msg.EnvironmentState = type("L3EnvironmentState", (), {})

    l3_msgs = types.ModuleType("l3_msgs")
    l3_msgs.msg = types.ModuleType("l3_msgs.msg")
    for name in (
        "AvoidancePlan",
        "AvoidanceWaypoint",
        "ASDRRecord",
        "UIState",
        "ODDState",
        "WorldState",
        "MissionGoal",
        "BehaviorPlan",
        "COLREGsConstraint",
        "TrackedTarget",
        "ThreatState",
        "MissionState",
    ):
        setattr(l3_msgs.msg, name, type(name, (), {}))

    std_msgs = types.ModuleType("std_msgs")
    std_msgs.msg = types.ModuleType("std_msgs.msg")
    std_msgs.msg.Header = type("Header", (), {})

    lifecycle_msgs = types.ModuleType("lifecycle_msgs")
    lifecycle_msgs.srv = types.ModuleType("lifecycle_msgs.srv")
    lifecycle_msgs.srv.ChangeState = type("ChangeState", (), {
        "Request": type("Request", (), {
            "transition": type("Transition", (), {"id": 0})
        })
    })
    lifecycle_msgs.srv.GetState = type("GetState", (), {
        "Request": type("Request", (), {})
    })
    lifecycle_msgs.msg = types.ModuleType("lifecycle_msgs.msg")
    lifecycle_msgs.msg.Transition = SimpleNamespace(
        TRANSITION_CONFIGURE=1,
        TRANSITION_ACTIVATE=3,
        TRANSITION_DEACTIVATE=4,
        TRANSITION_CLEANUP=2
    )

    rcl_interfaces = types.ModuleType("rcl_interfaces")
    rcl_interfaces.srv = types.ModuleType("rcl_interfaces.srv")
    rcl_interfaces.srv.SetParameters = type("SetParameters", (), {})
    rcl_interfaces.msg = types.ModuleType("rcl_interfaces.msg")
    rcl_interfaces.msg.Parameter = type("Parameter", (), {})
    rcl_interfaces.msg.ParameterValue = type("ParameterValue", (), {})
    rcl_interfaces.msg.ParameterType = SimpleNamespace(
        PARAMETER_DOUBLE=1,
        PARAMETER_STRING=2,
        PARAMETER_INTEGER=3
    )

    for module in (
        rclpy,
        rclpy.node,
        rclpy.executors,
        rclpy.qos,
        sil_msgs,
        sil_msgs.msg,
        l3_external_msgs,
        l3_external_msgs.msg,
        l3_msgs,
        l3_msgs.msg,
        std_msgs,
        std_msgs.msg,
        lifecycle_msgs,
        lifecycle_msgs.srv,
        lifecycle_msgs.msg,
        rcl_interfaces,
        rcl_interfaces.srv,
        rcl_interfaces.msg,
    ):
        monkeypatch.setitem(sys.modules, module.__name__, module)

def get_lifecycle_bridge_class():
    path = Path(__file__).resolve().parents[2] / "src" / "sil_orchestrator" / "lifecycle_bridge.py"
    spec = importlib.util.spec_from_file_location("lifecycle_bridge_under_test", path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    sys.modules["lifecycle_bridge_under_test"] = module
    return module.LifecycleBridge

@pytest.mark.asyncio
async def test_auto_stop_timer_reads_duration_and_activates():
    """Verify configure reads duration and activate starts timer, triggering deactivate on expiration."""
    LifecycleBridge = get_lifecycle_bridge_class()
    
    # Mock _load_scenario_yaml to return a scenario with simulation duration
    mock_yaml = {
        "simulation": {
            "duration_s": 0.1
        }
    }
    
    with patch("lifecycle_bridge_under_test._load_scenario_yaml", return_value=mock_yaml), \
         patch("lifecycle_bridge_under_test._extract_injection_params", return_value={}), \
         patch("lifecycle_bridge_under_test._print_injection_summary"), \
         patch.object(LifecycleBridge, "_reset_to_unconfigured", new_callable=AsyncMock) as mock_reset, \
         patch.object(LifecycleBridge, "_change_state", new_callable=AsyncMock) as mock_change_state, \
         patch.object(LifecycleBridge, "_broadcast_transition", new_callable=AsyncMock):
        
        mock_reset.return_value = SimpleNamespace(success=True)
        mock_change_state.return_value = SimpleNamespace(success=True)
        
        bridge = LifecycleBridge()
        
        # Configure
        await bridge.configure("test-scenario")
        assert bridge._simulation_duration_s == 0.1
        assert bridge._timer_start_time is None
        
        # Activate
        with patch.object(bridge, "deactivate", new_callable=AsyncMock) as mock_deactivate:
            await bridge.activate()
            assert bridge._timer_start_time is not None
            assert bridge._timer_task is not None
            
            # Wait for timer to expire and trigger deactivate
            await asyncio.sleep(0.15)
            assert mock_deactivate.called

@pytest.mark.asyncio
async def test_deactivate_is_idempotent():
    """Verify deactivate is idempotent and safe when called in INACTIVE state."""
    LifecycleBridge = get_lifecycle_class_bridge = get_lifecycle_bridge_class()
    
    with patch.object(LifecycleBridge, "_change_state", new_callable=AsyncMock) as mock_change_state:
        bridge = LifecycleBridge()
        bridge._state = SimpleNamespace(value="inactive") # Mock state
        
        # Call deactivate in inactive state
        res = await bridge.deactivate()
        assert res.success is True
        assert not mock_change_state.called

@pytest.mark.asyncio
async def test_lifecycle_status_returns_time_remaining():
    """Verify lifecycle_status endpoint returns correct time_remaining_s."""
    import sil_orchestrator.main as main
    
    # Check initial status when timer is not active
    main.bridge._timer_start_time = None
    main.bridge._simulation_duration_s = None
    status = await main.lifecycle_status()
    assert status["time_remaining_s"] == -1
    
    # Set active status and active timer
    main.bridge._timer_start_time = time.time()
    main.bridge._simulation_duration_s = 100.0
    main.bridge._state = main.LifecycleState.ACTIVE
    
    status = await main.lifecycle_status()
    assert 0.0 < status["time_remaining_s"] <= 100.0


@pytest.mark.asyncio
async def test_auto_stop_timer_reads_total_time_from_settings():
    """Verify configure reads total_time from metadata.simulation_settings."""
    LifecycleBridge = get_lifecycle_bridge_class()
    
    mock_yaml = {
        "metadata": {
            "simulation_settings": {
                "total_time": 25.0
            }
        }
    }
    
    with patch("lifecycle_bridge_under_test._load_scenario_yaml", return_value=mock_yaml), \
         patch("lifecycle_bridge_under_test._extract_injection_params", return_value={}), \
         patch("lifecycle_bridge_under_test._print_injection_summary"), \
         patch.object(LifecycleBridge, "_reset_to_unconfigured", new_callable=AsyncMock) as mock_reset, \
         patch.object(LifecycleBridge, "_change_state", new_callable=AsyncMock) as mock_change_state, \
         patch.object(LifecycleBridge, "_broadcast_transition", new_callable=AsyncMock):
        
        mock_reset.return_value = SimpleNamespace(success=True)
        mock_change_state.return_value = SimpleNamespace(success=True)
        
        bridge = LifecycleBridge()
        await bridge.configure("test-scenario")
        assert bridge._simulation_duration_s == 25.0


@pytest.mark.asyncio
async def test_backup_timer_forces_inactive():
    """Verify backup timer task forces LifecycleState.INACTIVE after duration + 30s."""
    LifecycleBridge = get_lifecycle_bridge_class()
    
    mock_yaml = {"simulation": {"duration_s": 0.01}}
    
    with patch("lifecycle_bridge_under_test._load_scenario_yaml", return_value=mock_yaml), \
         patch("lifecycle_bridge_under_test._extract_injection_params", return_value={}), \
         patch("lifecycle_bridge_under_test._print_injection_summary"), \
         patch.object(LifecycleBridge, "_reset_to_unconfigured", new_callable=AsyncMock) as mock_reset, \
         patch.object(LifecycleBridge, "_change_state", new_callable=AsyncMock) as mock_change_state, \
         patch.object(LifecycleBridge, "_broadcast_transition", new_callable=AsyncMock):
        
        mock_reset.return_value = SimpleNamespace(success=True)
        mock_change_state.return_value = SimpleNamespace(success=True)
        
        # We patch asyncio.sleep inside the backup timer to run quickly in the test
        # duration + 30s is the sleep duration.
        original_sleep = asyncio.sleep
        async def mock_sleep(delay, result=None):
            if delay > 10.0:
                await original_sleep(0.02)
            else:
                await original_sleep(delay)

        with patch("asyncio.sleep", new=mock_sleep):
            bridge = LifecycleBridge()
            await bridge.configure("test-scenario")
            
            # Mock state to ACTIVE, ensure we don't deactivate automatically via normal timer
            with patch.object(bridge, "deactivate", new_callable=AsyncMock) as mock_deactivate:
                # To prevent deactivate call from changing status, we can make it return success
                mock_deactivate.return_value = SimpleNamespace(success=True)
                
                await bridge.activate()
                assert bridge._state == "active" or bridge._state.value == "active"
                
                # Let backup timer execute
                await asyncio.sleep(0.05)
                
                # Verify that state is forced to INACTIVE (the backup timer forces self._state)
                state_str = bridge._state.value if hasattr(bridge._state, "value") else str(bridge._state)
                assert state_str.lower() == "inactive"


@pytest.mark.asyncio
async def test_timers_are_cancelled_on_deactivate_and_cleanup():
    """Verify that active timer tasks are properly cancelled in deactivate and cleanup."""
    LifecycleBridge = get_lifecycle_bridge_class()
    
    mock_yaml = {"simulation": {"duration_s": 100.0}}
    
    with patch("lifecycle_bridge_under_test._load_scenario_yaml", return_value=mock_yaml), \
         patch("lifecycle_bridge_under_test._extract_injection_params", return_value={}), \
         patch("lifecycle_bridge_under_test._print_injection_summary"), \
         patch.object(LifecycleBridge, "_reset_to_unconfigured", new_callable=AsyncMock) as mock_reset, \
         patch.object(LifecycleBridge, "_change_state", new_callable=AsyncMock) as mock_change_state, \
         patch.object(LifecycleBridge, "_broadcast_transition", new_callable=AsyncMock):
        
        mock_reset.return_value = SimpleNamespace(success=True)
        mock_change_state.return_value = SimpleNamespace(success=True)
        
        bridge = LifecycleBridge()
        await bridge.configure("test-scenario")
        await bridge.activate()
        
        assert bridge._timer_task is not None
        assert bridge._backup_timer_task is not None
        assert not bridge._timer_task.cancelled()
        assert not bridge._backup_timer_task.cancelled()
        
        # Deactivate cancels both
        await bridge.deactivate()
        assert bridge._timer_task is None
        assert bridge._backup_timer_task is None
        
        # Re-activate and check cleanup cancels too
        await bridge.activate()
        assert bridge._timer_task is not None
        assert bridge._backup_timer_task is not None
        
        await bridge.cleanup()
        assert bridge._timer_task is None
        assert bridge._backup_timer_task is None

