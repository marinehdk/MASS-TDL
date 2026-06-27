import importlib.util
from pathlib import Path


def _load_clean_runner():
    path = Path(__file__).resolve().parents[2] / "scripts" / "run_colregs_clean_8probe.py"
    spec = importlib.util.spec_from_file_location("run_colregs_clean_8probe", path)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


def test_profile_gnc_is_consumed_before_delegating_to_runner(monkeypatch):
    wrapper = _load_clean_runner()
    checked_images = []
    delegated_args = []

    class FakeRunner:
        @staticmethod
        def main(argv):
            delegated_args.extend(argv)
            return 0

    def fake_image_running(image_substr):
        checked_images.append(image_substr)
        return image_substr == "mass-l3-gnc:mpc_latest"

    monkeypatch.setattr(wrapper, "_load_runner", lambda: FakeRunner)
    monkeypatch.setattr(wrapper, "_any_container_running_image", fake_image_running, raising=False)

    assert wrapper.main(["--profile", "gnc", "--list"]) == 0
    assert delegated_args == ["--profile", "gnc", "--list"]
    assert checked_images == ["mass-l3-gnc:mpc_latest"]
