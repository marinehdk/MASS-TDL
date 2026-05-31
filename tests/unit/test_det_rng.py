import pickle
from multiprocessing import Process, Queue

import numpy as np
import pytest

# Under TDD, we attempt to import the RNG factory functions.
# These imports will initially fail, making the tests RED.
from sil_common.det_rng import _NODE_ID, make_rng, register_node


def test_make_rng_returns_generator():
    """Verify that make_rng returns a numpy.random.Generator instance."""
    rng = make_rng(root=42, episode=0, node="target_vessel", worker=0)
    assert isinstance(rng, np.random.Generator)


def test_identical_keys_produce_identical_sequence():
    """Verify that identical keys lead to identical draw sequences."""
    rng1 = make_rng(root=42, episode=1, node="target_vessel", worker=2)
    rng2 = make_rng(root=42, episode=1, node="target_vessel", worker=2)

    draws1 = rng1.random(100)
    draws2 = rng2.random(100)

    np.testing.assert_array_equal(draws1, draws2)


def test_differing_key_components_produce_different_sequences():
    """Verify that changing any single key component produces a different sequence."""
    base_key = {"root": 42, "episode": 1, "node": "target_vessel", "worker": 2}

    # Draw from base RNG
    rng_base = make_rng(**base_key)
    base_draws = rng_base.random(10)

    # 1. Differing root
    key_root = base_key.copy()
    key_root["root"] = 43
    rng_root = make_rng(**key_root)
    assert not np.array_equal(base_draws, rng_root.random(10))

    # 2. Differing episode
    key_ep = base_key.copy()
    key_ep["episode"] = 2
    rng_ep = make_rng(**key_ep)
    assert not np.array_equal(base_draws, rng_ep.random(10))

    # 3. Differing node
    key_node = base_key.copy()
    key_node["node"] = "ship_dynamics"
    rng_node = make_rng(**key_node)
    assert not np.array_equal(base_draws, rng_node.random(10))

    # 4. Differing worker
    key_worker = base_key.copy()
    key_worker["worker"] = 3
    rng_worker = make_rng(**key_worker)
    assert not np.array_equal(base_draws, rng_worker.random(10))


def _child_process_draw(pickled_key, queue):
    """Helper target function to draw numbers in a separate process."""
    try:
        key = pickle.loads(pickled_key)
        # Call make_rng inside the child process
        rng = make_rng(
            root=key["root"], episode=key["episode"], node=key["node"], worker=key["worker"]
        )
        draws = rng.random(50)
        queue.put(("SUCCESS", draws))
    except Exception as e:
        queue.put(("ERROR", str(e)))


def test_cross_process_reproducibility():
    """Verify that we can pickle the key, recreate RNG in a child process, and get identical draws."""
    key = {"root": 99, "episode": 5, "node": "target_vessel", "worker": 0}
    pickled_key = pickle.dumps(key)

    # Main process draw
    rng_main = make_rng(**key)
    draws_main = rng_main.random(50)

    # Spawn child process
    queue = Queue()
    p = Process(target=_child_process_draw, args=(pickled_key, queue))
    p.start()
    p.join(timeout=5)

    assert not p.is_alive(), "Child process timed out"

    status, result = queue.get()
    assert status == "SUCCESS", f"Child process failed with error: {result}"
    np.testing.assert_array_equal(draws_main, result)


def test_unregistered_node_raises_error():
    """Verify that requesting an RNG for an unregistered node raises ValueError."""
    with pytest.raises(ValueError) as excinfo:
        make_rng(root=42, episode=0, node="unknown_non_existent_node", worker=0)
    assert "unknown_non_existent_node" in str(excinfo.value)


def test_dynamic_node_registration():
    """Verify that register_node correctly adds a node and allows creating its RNG."""
    node_name = "dynamic_test_node"
    # Ensure it's not present or clean it up if it is
    if node_name in _NODE_ID:
        del _NODE_ID[node_name]

    register_node(node_name, 999)
    assert _NODE_ID[node_name] == 999

    # Now make_rng should work
    rng = make_rng(root=42, episode=0, node=node_name, worker=0)
    assert isinstance(rng, np.random.Generator)

    # Trying to register again with different ID should raise ValueError
    with pytest.raises(ValueError):
        register_node(node_name, 888)

    # Re-registering with same ID should be fine
    register_node(node_name, 999)
