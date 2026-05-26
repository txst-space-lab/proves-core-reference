"""
mosaic_test.py:

Integration tests for the Mosaic Handler component.
"""

from datetime import datetime
from time import sleep

import pytest
from common import proves_send_and_assert_command
from fprime_gds.common.data_types.event_data import EventData
from fprime_gds.common.models.serialize.numerical_types import U32Type
from fprime_gds.common.models.serialize.time_type import TimeType
from fprime_gds.common.testing_fw.api import IntegrationTestAPI

mosaicHandler = "ReferenceDeployment.mosaicHandler"


@pytest.fixture(autouse=True)
def setup_test(fprime_test_api: IntegrationTestAPI, start_gds):
    """Fixture to turn on face 4 before each test"""
    proves_send_and_assert_command(
        fprime_test_api,
        "ReferenceDeployment.face4LoadSwitch.TURN_ON",
        [],
    )


def test_01_observe_gamma_reading(fprime_test_api: IntegrationTestAPI, start_gds):
    """Test that a gamma reading is received from the mosaic payload"""
    start: TimeType = TimeType().set_datetime(
        datetime.now(), time_base=TimeType.TimeBase("TB_DONT_CARE")
    )

    # Wait for a few seconds to allow the payload to power on and start sending data
    sleep(5)

    # Assert that we receive a GammaReadingReceived event with some bytes
    result: EventData = fprime_test_api.assert_event(
        f"{mosaicHandler}.GammaReadingReceived", start=start, timeout=2
    )

    assert result is not None
    assert len(result.get_args()) == 1
    num_bytes: U32Type = result.args[0]
    assert num_bytes.val > 0
