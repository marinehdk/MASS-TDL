from scenario_authoring.pipeline.stage1_group import group_by_mmsi


def test_group_by_mmsi_two_vessels(sample_records_two_vessels):
    """Two vessels correctly grouped, each sorted by timestamp."""
    groups = group_by_mmsi(sample_records_two_vessels)

    assert len(groups) == 2
    assert 111111111 in groups
    assert 222222222 in groups
    assert len(groups[111111111]) == 5
    assert len(groups[222222222]) == 5
    # Verify time-sorted
    for _mmsi, recs in groups.items():
        for i in range(len(recs) - 1):
            assert recs[i].timestamp <= recs[i + 1].timestamp


def test_group_filters_sparse_vessels(sample_records_sparse_vessel):
    """Vessels with <10 records are discarded."""
    groups = group_by_mmsi(sample_records_sparse_vessel)
    assert 111111111 in groups  # 14 records -> kept
    assert 222222222 not in groups  # 3 records -> discarded
