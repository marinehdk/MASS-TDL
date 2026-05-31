import numpy as np

_NODE_ID = {
    "ship_dynamics": 1,
    "env_disturbance": 2,
    "target_vessel": 3,
    "sensor_mock": 4,
    "tracker_mock": 5,
    "fault_injection": 6,
    "scoring": 7,
    "scenario_authoring": 8,
}


def register_node(name: str, node_id: int) -> None:
    """
    Register a node name with a unique integer ID.
    Ensures registration is stable and does not conflict.
    """
    if not isinstance(name, str):
        raise TypeError("Node name must be a string")
    if not isinstance(node_id, int):
        raise TypeError("node_id must be an integer")
    if name in _NODE_ID:
        if _NODE_ID[name] != node_id:
            raise ValueError(
                f"Node '{name}' is already registered with a different ID ({_NODE_ID[name]})"
            )
    else:
        _NODE_ID[name] = node_id


def make_seed_sequence(root: int, episode: int, node: str, worker: int) -> np.random.SeedSequence:
    """
    Generates a stable, auditable NumPy SeedSequence keyed on (root, episode, node, worker).
    """
    if not isinstance(root, int):
        raise TypeError("root must be an integer")
    if not isinstance(episode, int):
        raise TypeError("episode must be an integer")
    if not isinstance(worker, int):
        raise TypeError("worker must be an integer")
    if not isinstance(node, str):
        raise TypeError("node must be a string")

    if node not in _NODE_ID:
        raise ValueError(
            f"Node '{node}' is not registered in the _NODE_ID registry. "
            f"Please use register_node(name, node_id) first."
        )
    node_id = _NODE_ID[node]
    return np.random.SeedSequence(entropy=[root, episode, node_id, worker])


def make_rng(root: int, episode: int, node: str, worker: int) -> np.random.Generator:
    """
    Creates an independent, reproducible NumPy random number Generator
    keyed on (root, episode, node, worker).

    Docstring Note: Action-space RNG is seeded separately by the Gym layer (D1).
    """
    seq = make_seed_sequence(root, episode, node, worker)
    return np.random.default_rng(seq)
