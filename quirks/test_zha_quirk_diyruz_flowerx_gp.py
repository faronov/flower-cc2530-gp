"""Test the flower-gp ZHA quirk against frames produced by include/gpd_frame.h."""

from unittest.mock import MagicMock

from zha.quirks import DEVICE_REGISTRY
from zigpy.device import GreenPowerCommandReceived, GreenPowerDevice
from zigpy.zgp.commands import GPCommissioningPayload, GPMultiClusterReportingPayload
from zigpy.zgp.types import ApplicationID, GPDCommandID, SrcID

import zha_quirk_diyruz_flowerx_gp  # noqa: F401  registers the quirk

# tests/test_gpd_frame.c frames for IEEE 00:12:4b:00:0e:f6:28:15, after byte 13.
COMMISSIONING = bytes.fromhex("33450b6b6d00f1040100000402040804")
REPORT = bytes.fromhex(
    "02040000290cfe080400002110270004000021ffff01002000201e0100210020c8"
)


def _report(command):
    return GreenPowerCommandReceived(
        device_ieee="", endpoint_id=0,
        command_id=GPDCommandID.MultiClusterReporting, command=command,
    )


def test_flower_gp_quirk():
    commissioning, rest = GPCommissioningPayload.deserialize(COMMISSIONING)
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
    assert zha_device.name == "DIYRuZ FlowerX GP"

    entities = {e.unique_id.rsplit("-", 1)[1]: e for e in zha_device.discover_entities()}
    assert set(entities) == {
        "soil_moisture", "temperature", "illuminance", "battery", "battery_voltage",
    }
    for entity in entities.values():
        entity.on_add()
        assert entity.native_value is None

    report, rest = GPMultiClusterReportingPayload.deserialize(REPORT)
    assert rest == b""
    gpd.emit(GreenPowerCommandReceived.event_type, _report(report))
    values = {name: e.native_value for name, e in entities.items()}
    assert values == {
        "temperature": -5.0, "soil_moisture": 100.0, "illuminance": None,
        "battery_voltage": 3.0, "battery": 100.0,
    }

    report.reports[2].attribute.value.value = 1  # 10000*log10(1 lx)+1
    gpd.emit(GreenPowerCommandReceived.event_type, _report(report))
    assert entities["illuminance"].native_value == 1.0

    gpd.emit(GreenPowerCommandReceived.event_type, _report(None))
    assert entities["illuminance"].native_value == 1.0
