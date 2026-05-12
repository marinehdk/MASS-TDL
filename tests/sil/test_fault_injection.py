"""Unit tests for FaultInjectionNode."""
import pytest
from fault_injection.node import FaultInjectionNode


class TestFaultInjectionNode:
    def test_trigger_valid_types(self):
        node = FaultInjectionNode()
        for ft in ("ais_dropout", "radar_noise_spike", "disturbance_step"):
            fid = node.trigger(ft)
            assert len(fid) == 8, f"fault_id should be 8 chars, got {fid!r}"

    def test_trigger_invalid_type_raises(self):
        node = FaultInjectionNode()
        with pytest.raises(ValueError, match="Unknown fault type"):
            node.trigger("nonexistent_fault")

    def test_list_active(self):
        node = FaultInjectionNode()
        fid1 = node.trigger("ais_dropout")
        fid2 = node.trigger("radar_noise_spike")
        active = node.list_active()
        assert len(active) == 2
        ids = {f["fault_id"] for f in active}
        assert ids == {fid1, fid2}

    def test_cancel(self):
        node = FaultInjectionNode()
        fid = node.trigger("ais_dropout")
        assert node.cancel(fid) is True
        assert len(node.list_active()) == 0
        assert node.cancel(fid) is False  # already gone

    def test_get_active_types(self):
        node = FaultInjectionNode()
        node.trigger("ais_dropout")
        node.trigger("radar_noise_spike")
        types = node.get_active_types()
        assert "ais_dropout" in types
        assert "radar_noise_spike" in types
        assert "disturbance_step" not in types

    def test_payload_passthrough(self):
        node = FaultInjectionNode()
        fid = node.trigger("disturbance_step", payload={"magnitude": 15.0})
        entry = node._active[fid]
        assert entry["payload"]["magnitude"] == 15.0

    def test_empty_payload_defaults_to_dict(self):
        node = FaultInjectionNode()
        fid = node.trigger("ais_dropout")
        entry = node._active[fid]
        assert entry["payload"] == {}
