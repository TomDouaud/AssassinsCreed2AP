from test.bases import WorldTestBase

from .. import AC2World


class AC2TestBase(WorldTestBase):
    game = "Assassin's Creed II"
    world: AC2World
