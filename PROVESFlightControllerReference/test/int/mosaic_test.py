"""
mosaic_test.py:

Integration tests for the Mosaic Handler component.

Diagnostic strategy: telemetry has been observed to drop under load on this
deployment, so the lower-numbered tests below use events only — they're
event-only "layered" probes of the UART RX path:

  L0 (test_00_uart_drivers_configured)
      Both ZephyrUartDriver instances should emit ConfigureStatus(true, 115200)
      at boot. If only one fires, that UART's Zephyr device didn't pass
      device_is_ready() and configure returned early.

  L1 (test_01_uart1_drains_bytes)
      ZephyrUartDriver emits SchedInGotBytes(N) the first few times its schedIn
      drains > 0 bytes from the ring buffer. peripheralUartDriver2 should hit
      this within the first MOSAIC chunk after power-on. If not, no IRQ data
      arrived — MOSAIC TX isn't reaching FCB P5 or uart1 IRQ isn't firing.

  L2 (test_02_payloadcom2_sees_data)
      PayloadCom emits UartReceived per chunk. payload2 fires it iff bytes
      crossed from peripheralUartDriver2.$recv -> payload2.uartDataIn.

  L3 (test_03_radiation_payload_sees_data)
      RadiationPayload emits RawDataDump for the first few chunks. Fires iff
      payload2.uartDataOut -> mosaicHandler.dataIn is wired and active.

  L4 (test_04_gamma_reading_event)
      End-to-end: parser produced a full record, GammaReadingReceived fires.
"""

from time import sleep

import pytest
from common import proves_send_and_assert_command
from fprime_gds.common.data_types.event_data import EventData
from fprime_gds.common.testing_fw.api import IntegrationTestAPI

mosaicHandler = "ReferenceDeployment.mosaicHandler"


@pytest.fixture(autouse=True)
def setup_test(fprime_test_api: IntegrationTestAPI, start_gds):
    """Turn on MOSAIC payload power before each test."""
    proves_send_and_assert_command(
        fprime_test_api,
        "ReferenceDeployment.payloadPowerLoadSwitch.TURN_ON",
        [],
    )


def test_00_uart1_schedin_alive(fprime_test_api: IntegrationTestAPI, start_gds):
    """L0: peripheralUartDriver2.schedIn must be ticked by the rate group.
    Heartbeat fires every 100 calls (~10 s at 10 Hz). If we don't see it in 15 s,
    schedIn isn't being called at all — rate-group wiring or component init is broken."""
    evt = fprime_test_api.assert_event(
        "ReferenceDeployment.peripheralUartDriver2.SchedInHeartbeat",
        timeout=15,
    )
    assert evt is not None, (
        "peripheralUartDriver2.SchedInHeartbeat never fired — schedIn isn't being scheduled"
    )


def test_01_uart1_drains_bytes(fprime_test_api: IntegrationTestAPI, start_gds):
    """L1: peripheralUartDriver2 should drain > 0 bytes within a few seconds.
    If L0 passed but this fails, the ring buffer is empty — uart1 IRQ isn't
    putting any data in. Either MOSAIC TX isn't wired to FCB P5, or uart1's
    Zephyr device didn't pass device_is_ready() (configure returned early)."""
    sleep(5)
    evt = fprime_test_api.assert_event(
        "ReferenceDeployment.peripheralUartDriver2.SchedInGotBytes",
        timeout=10,
    )
    assert evt is not None, (
        "peripheralUartDriver2 never drained any bytes — ring buffer stayed empty"
    )


def test_02_payloadcom2_sees_data(fprime_test_api: IntegrationTestAPI, start_gds):
    sleep(5)
    evt = fprime_test_api.assert_event(
        "ReferenceDeployment.payload2.UartReceived",
        timeout=10,
    )
    assert evt is not None


def test_03_radiation_payload_sees_data(fprime_test_api: IntegrationTestAPI, start_gds):
    sleep(5)
    evt = fprime_test_api.assert_event(
        f"{mosaicHandler}.RawDataDump",
        timeout=10,
    )
    assert evt is not None


def test_04_gamma_reading_event(fprime_test_api: IntegrationTestAPI, start_gds):
    sleep(5)
    result: EventData = fprime_test_api.assert_event(
        f"{mosaicHandler}.GammaReadingReceived",
        timeout=10,
    )
    assert result is not None
    args = result.get_args() if hasattr(result, "get_args") else result.args
    assert len(args) == 2
    num_bytes = args[0]
    val = num_bytes.val if hasattr(num_bytes, "val") else num_bytes
    assert val > 0
