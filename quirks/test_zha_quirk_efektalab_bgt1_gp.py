"""Test the bgt1-gp ZHA quirk against frames produced by include/gpd_frame.h."""

from unittest.mock import MagicMock

import pytest

from zha.quirks import DEVICE_REGISTRY
from zigpy.device import GreenPowerCommandReceived, GreenPowerDevice
from zigpy.zgp.commands import GPCommissioningPayload, GPMultiClusterReportingPayload
from zigpy.zgp.types import ApplicationID, GPDCommandID, SrcID

import zha_quirk_efektalab_bgt1_gp  # noqa: F401  registers the quirk

# tests/test_gpd_frame.c with BOARD_BGT1 (model 0xF202), after byte 13.
COMMISSIONING = bytes.fromhex("30450b6b6d02f20201000204")
REPORT = bytes.fromhex("02040000290cfe01002000201e0100210020c8")


@pytest.mark.parametrize("model", [0xF200, 0xF202])
def test_bgt1_gp_quirk(model):
    frame = bytearray(COMMISSIONING)
    frame[5:7] = model.to_bytes(2, "little")
    commissioning, rest = GPCommissioningPayload.deserialize(bytes(frame))
    assert rest == b""
    gpd = GreenPowerDevice(
        MagicMock(), application_id=ApplicationID.SrcID, src_id=SrcID(0x0EF62815)
    )
    gpd.device_id = commissioning.device_id
    gpd.gpd_manufacturer_id = commissioning.manufacturer_id
    gpd.gpd_model_id = commissioning.model_id
    entry = DEVICE_REGISTRY.match_green_power_entry(gpd)
    assert entry is not None
    zha_device = entry.zha_device_factory(DEVICE_REGISTRY.resolve(gpd), MagicMock())
    assert zha_device.name == "EfektaLab BGT1 GP"
    entities = {e.unique_id.rsplit("-", 1)[1]: e for e in zha_device.discover_entities()}
    assert set(entities) == {"temperature", "battery", "battery_voltage"}
    for entity in entities.values():
        entity.on_add()
        assert entity.native_value is None

    report, rest = GPMultiClusterReportingPayload.deserialize(REPORT)
    assert rest == b""
    gpd.emit(
        GreenPowerCommandReceived.event_type,
        GreenPowerCommandReceived(
            device_ieee="", endpoint_id=0,
            command_id=GPDCommandID.MultiClusterReporting, command=report,
        ),
    )
    values = {name: e.native_value for name, e in entities.items()}
    assert values == {"temperature": -5.0, "battery_voltage": 3.0, "battery": 100.0}
