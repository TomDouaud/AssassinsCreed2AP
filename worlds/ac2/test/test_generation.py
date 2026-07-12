from . import AC2TestBase


class TestDefault(AC2TestBase):
    """Runs the standard WorldTestBase battery with default options."""
    options = {}


class TestAllChecks(AC2TestBase):
    """Every optional check category enabled at once."""
    options = {
        "chests": 1,
        "chest_sanity": "individual",
        "feathers": 1,
        "feather_sanity": "individual",
        "statues": 1,
        "shop_items": 1,
        "codex_pages": 1,
    }


class TestNoGating(AC2TestBase):
    """Region gating off (open mode)."""
    options = {"region_gating": 0}
